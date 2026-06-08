# Cerebellum — Design Spec

**Date:** 2026-06-04  
**Status:** Approved

---

## Overview

**Cerebellum** is a userspace stateful L4 load balancer written from scratch in C++.

The dataplane is modelled after VPP's vector packet processing architecture, implemented in modern C++. Packets move through a directed graph of nodes; each node receives a **Frame** (a fixed-size batch of packet pointers + metadata) and dispatches subsets to one of its named next outputs. Processing N packets through the same node in a tight loop keeps instruction cache hot. DPDK RSS steers flows to lcores by 5-tuple; each lcore runs an independent graph instance with its own conntrack table — no locks anywhere in the hot path. The controlplane reads stats from shared memory and exposes a REST API. The dashboard is a React app that polls the API and displays live stats and backend health.

**Stack:** C++ · DPDK · React + Tailwind · nginx · Docker Compose

---

## Architecture

```
                        bare-metal server
┌──────────────────────────────────────────────────────┐
│                                                      │
│  NIC (DPDK-bound)                                    │
│   │ RSS by 5-tuple                                   │
│   ├── queue 0 → lcore 0 ──┐                          │
│   ├── queue 1 → lcore 1 ──┤ per-lcore Graph          │
│   └── queue N → lcore N ──┘ (node pipeline)          │
│                  │                                   │
│         shared memory (StatsView)                    │
│                  │                                   │
│         ┌────────▼────────┐   ┌──────────────────┐  │
│         │  Controlplane   │   │  nginx + React   │  │
│         │  REST :8080     │◄──┤  :80/:443        │  │
│         └─────────────────┘   └──────────────────┘  │
└──────────────────────────────────────────────────────┘
```

All four services run in Docker Compose. nginx proxies `/api/*` to controlplane, serves the React SPA from `/`.

---

## Repository Layout

```
cerebellum/
  dataplane/
    include/
      net/          ← flow_key.hpp, ipv4_addr.hpp, mac_addr.hpp
      lb/           ← conntrack_table.hpp, backend_pool.hpp
      io/           ← eal.hpp, dpdk_port.hpp, dpdk_queue.hpp
      graph/        ← node.hpp, frame.hpp, graph.hpp, dispatcher.hpp
      nodes/        ← rx_node.hpp, ip4_parse_node.hpp, lb_node.hpp,
                       dnat_node.hpp, ether_rewrite_node.hpp, tx_node.hpp, drop_node.hpp
    src/
    CMakeLists.txt
  controlplane/
    include/
      api/          ← RestServer (Crow)
      health/       ← HealthChecker, ProbeScheduler, BackendState
      stats/        ← StatsAggregator
      config/       ← YamlLoader, Config
    src/
    CMakeLists.txt
  libs/
    ipc/            ← StatsView, ShmProvider (shared memory between DP and CP)
    common/         ← Config struct, BackendInfo
  ui/               ← Vite + React + Tailwind
  cmake/            ← ProjectOptions, CompilerWarnings, Sanitizers, Coverage, Dependencies
  CMakeLists.txt
  CMakePresets.json ← release, debug-asan, coverage
  justfile
  nginx/
```

---

## Dataplane

### Thread Model

DPDK EAL launches N lcores (one per RX queue). RSS ensures all packets of a 5-tuple flow arrive at the same queue → same lcore. Each lcore runs an independent `Graph` instance with its own `ConntrackTable` and `BackendPool` snapshot — **no shared mutable state, no locks**.

### VPP-Inspired Processing Model

Packets are processed in **Frames** (fixed-size batches). Each `Node` receives a `Frame`, processes all packets in a tight loop (keeping icache hot), and dispatches subsets to named next outputs. The `Dispatcher` drains the pending-frame queue until the burst is fully processed.

```
┌─ RxNode ──────────────────────────────────────────────────────┐
│  rte_eth_rx_burst → Frame{mbufs[], meta[], count}             │
└───────────────────────────────┬───────────────────────────────┘
                                │ next[0] = all packets
                                ▼
┌─ Ip4ParseNode ────────────────────────────────────────────────┐
│  for each pkt: extract 5-tuple into meta[]                    │
│  prefetch meta[i+4] while processing meta[i]                  │
└────────────┬──────────────────────────────────────────────────┘
             │ next[FWD] | next[REV] | next[DROP]
             ▼
┌─ LbNode ──────────────────────────────────────────────────────┐
│  FWD: conntrack lookup → on miss: hash % backends → insert    │
│  REV: reversed-key lookup                                     │
│  writes backend_idx into meta[]                               │
└────────────┬──────────────────────────────────────────────────┘
             │ next[DNAT] | next[DROP]
             ▼
┌─ DnatNode ────────────────────────────────────────────────────┐
│  FWD: dst_ip/dst_port ← backend.ip/port                       │
│  REV: src_ip/src_port ← VIP/VIP_PORT                         │
│  incremental IP + TCP/UDP checksum update                     │
└────────────┬──────────────────────────────────────────────────┘
             │ next[0] = all packets
             ▼
┌─ EtherRewriteNode ────────────────────────────────────────────┐
│  src_mac ← self, dst_mac ← backend.mac (or gateway mac)      │
└────────────┬──────────────────────────────────────────────────┘
             │ next[0] = all packets
             ▼
┌─ TxNode ──────────────────────────────────────────────────────┐
│  rte_eth_tx_burst                                             │
└───────────────────────────────────────────────────────────────┘
```

### Core Abstractions (`graph/`)

```cpp
inline constexpr uint16_t kBurstSize = 64;

// Per-packet metadata — travels alongside rte_mbuf through the graph
struct PacketMeta {
    uint32_t src_ip, dst_ip;
    uint16_t src_port, dst_port;
    uint8_t  proto;
    uint8_t  backend_idx;
    uint8_t  dir;   // Direction::Forward | Direction::Reverse
};

// A batch of packets and their metadata
struct Frame {
    std::array<rte_mbuf*,   kBurstSize> mbufs;
    std::array<PacketMeta,  kBurstSize> meta;
    uint16_t count = 0;
};

// Abstract node — one subclass per processing step
class Node {
 public:
  virtual ~Node() = default;
  // Process `in`, distribute packets into `nexts[0..NextCount()-1]`
  virtual void             Process(Frame& in, std::span<Frame> nexts) noexcept = 0;
  virtual std::string_view Name()      const noexcept = 0;
  virtual uint16_t         NextCount() const noexcept = 0;
};

// Wires nodes together and drives the dispatch loop
class Graph {
 public:
  using NodeId = uint16_t;
  NodeId AddNode(std::unique_ptr<Node>);
  void   Wire(NodeId from, uint16_t out_idx, NodeId to);
  void   RunOnce();  // called per-burst; graph is already per-lcore
};
```

`Graph::run_once` injects the RX frame into the root node, then iterates a pending-frame list until all output frames are drained — processing all packets through node X before moving to node Y.

### 5-Tuple Flow Key

```cpp
// Explicit padding so no uninitialized bytes reach rte_hash_crc.
struct FlowKey {
    uint32_t src_ip{};
    uint32_t dst_ip{};
    uint16_t src_port{};
    uint16_t dst_port{};
    uint8_t  proto{};
    uint8_t  _pad[3]{};  // compiler would insert this anyway; zero it explicitly
};
static_assert(sizeof(FlowKey) == 16);
```

### ConntrackTable (`lb/`)

Per-lcore open-addressing hash table (no `rte_hash` dependency — plain C++ array + linear probe).

```cpp
struct ConntrackEntry {
    FlowKey  key;          // canonical forward direction (client→VIP)
    uint8_t  backend_idx;
    uint8_t  flags;        // kSynSeen | kEstablished | kFinSeen
    uint32_t last_seen_ms; // coarse timestamp for expiry
};
```

Expiry: lazy eviction on lookup if entry age > `timeout_ms`. `LbNode` calls `ConntrackTable::LookupOrInsert(FlowKey, backend_idx)`.

### Backend Selection (new flow)

```cpp
backend_idx = Hash(flow_key) % pool.ActiveCount();
```

`BackendPool` holds a compacted array of active backends. Config updates are delivered via `AtomicRCU<BackendPool>` (pointer-swap with epoch-based reclamation) — a single shared pointer per lcore, no per-lcore channel needed.

### Stats (shared memory, per-lcore)

Each counter occupies its own cache line to eliminate false sharing between the DP writer and the CP reader.

```cpp
static constexpr uint32_t kMaxBackends = 64;

// One atomic per cache line — prevents false sharing between any two accessors.
template <typename T>
struct alignas(std::hardware_destructive_interference_size) CacheAligned {
    std::atomic<T> value{};
};

struct LcoreStats {
    CacheAligned<uint64_t> rx_packets;
    CacheAligned<uint64_t> tx_packets;
    CacheAligned<uint64_t> dropped;
    CacheAligned<uint64_t> new_flows;
    CacheAligned<uint64_t> active_flows;
    CacheAligned<uint64_t> backend_flows[kMaxBackends];
};
```

`sizeof(LcoreStats)` = 69 cache lines ≈ 4.4 KB per lcore. With 16 lcores: ~70 KB total — well within reason for shared memory.

Written by dataplane with `memory_order_relaxed`. Controlplane reads and aggregates all lcores. Per-backend `active_flows` in `/api/backends` = sum of `backend_flows[i].value` across lcores.

---

## Controlplane

### Config

YAML file specifies VIP, VIP port, list of backends (IP + port + MAC), TCP probe port, health check interval.

`YamlLoader` → `Config` → `ConfigPublisher` pushes atomically to dataplane via lock-free ring (`rte_ring` or `SpscChannel` in shared memory).

### Health Checker

`HealthChecker` opens a TCP connection to each backend's probe port every N seconds. Marks backend Up/Down/Draining in `BackendState`. Config updates are pushed to dataplane when backend set changes.

### REST API (Crow)

```
GET /api/stats
→ {
    "rx_pps":      <uint64>,
    "tx_pps":      <uint64>,
    "dropped_pps": <uint64>,
    "new_flows_ps":<uint64>,
    "active_flows":<uint64>
  }

GET /api/backends
→ {
    "vip":      "10.0.0.1",
    "vip_port": 80,
    "backends": [
      {
        "ip":         "10.0.1.1",
        "port":       8080,
        "status":     "up",
        "active_flows": 4128,
        "fail_count": 0,
        "last_check_ms": 312
      }
    ]
  }
```

Rates are delta/second, computed by `StatsAggregator` on a 1-second timer in `ControlManager`.

---

## Frontend (React + Tailwind)

Single page. Polls `/api/stats` + `/api/backends` every 1 second.

**Layout (light theme, Angie Console Light style):**

```
┌────────────────────────────────────────────────────────┐
│ ⬡ Cerebellum   L4 Load Balancer   eth0  ● Running      │
├────────────────────────────────────────────────────────┤
│ [RX pps]  [TX pps]  [Active flows]  [New flows/s]  [Dropped] │
├────────────────────────────────────────────────────────┤
│ VIP 10.0.0.1:80                                        │
│ ┌────────────────┬──────┬──────────────┬──────────┐    │
│ │ Backend        │Health│ Active flows │ Fails    │    │
│ │ 10.0.1.1:8080  │ UP   │        4128  │        0 │    │
│ │ 10.0.1.2:8080  │ UP   │        4213  │        0 │    │
│ │ 10.0.1.3:8080  │ DOWN │           — │        5 │    │
│ └────────────────┴──────┴──────────────┴──────────┘    │
└────────────────────────────────────────────────────────┘
```

Components: `Navbar`, `StatsBar` (5 cards), `BackendsTable`.

---

## CMake

Taken from the `arbor` repo. All targets link `cerebellum_project_options` (C++23, no extensions) and `cerebellum_compiler_warnings` (-Wall -Wextra -Werror). CPM for dependencies. Presets: `release`, `debug-asan`, `coverage`.

Dependencies via CPM: `fmt`, `yaml-cpp`, `Crow` (REST), `Catch2` (tests).  
DPDK: found via `find_package(PkgConfig)` + `pkg_check_modules(DPDK libdpdk)`.

**Code style:** Google C++ Style Guide.  
`.clang-format` taken from the `arbor` repo (`BasedOnStyle: Google`, `PointerAlignment: Left`).  
`.clang-tidy` enforces naming: types = `CamelCase`, functions = `CamelCase`, variables/fields = `snake_case`, private members = `snake_case_`, constants = `kCamelCase`, file names = `snake_case.hpp/.cpp`.

---

## Docker Compose

```yaml
services:
  dataplane:
    build: ./dataplane
    privileged: true
    network_mode: host
    volumes:
      - /dev/hugepages:/dev/hugepages
    devices:
      - /dev/vfio:/dev/vfio

  controlplane:
    build: ./controlplane
    expose: [8080]
    depends_on: [dataplane]
    ipc: host          # access shared memory with dataplane

  ui:
    build: ./ui
    volumes: [ui-dist:/app/dist]

  nginx:
    build: ./nginx
    ports: ["80:80", "443:443"]
    volumes:
      - ./nginx/conf:/etc/nginx/conf.d
      - certbot-data:/etc/letsencrypt
      - ui-dist:/usr/share/nginx/html
    depends_on: [controlplane, ui]

volumes:
  certbot-data:
  ui-dist:
```

---

## Week Plan

| Day | Goal |
|-----|------|
| 1 | CMake scaffolding: root + presets + cmake/ modules, empty library targets |
| 2 | Dataplane: `FlowKey`, `ConntrackTable`, `BackendPool`, unit tests with Catch2 |
| 3 | Dataplane: `Node`/`Frame`/`Graph` engine + all nodes; `Eal`/`DpdkPort`; lcore loop |
| 4 | Controlplane: `YamlLoader`, `HealthChecker`, `StatsAggregator`, REST API |
| 5 | React frontend: Navbar + StatsBar + BackendsTable, polling, Tailwind |
| 6 | Docker Compose full stack, nginx + HTTPS, deploy to server |
| 7 | End-to-end test under traffic, README, polish |

---

## Verification

```bash
# Unit tests (ConntrackTable, BackendPool, FlowKey hashing)
just test

# Controlplane alone (reads zero stats from shm if dataplane not running)
./build/cerebellum_controlplane --config config.yaml
curl localhost:8080/api/stats
curl localhost:8080/api/backends

# Full stack (all services including dataplane)
docker compose up

# Live dashboard
open https://<server>/
```
