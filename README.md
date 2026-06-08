# Cerebellum

A userspace **stateful L4 load balancer** with a live web dashboard.

The dataplane forwards TCP/UDP flows on **DPDK** via the **AF_XDP** PMD — it
attaches to an existing kernel netdev instead of unbinding the NIC, so no VFIO
or hugepages are required. Packets run through a **VPP-style node graph** in
64-packet batches; RSS pins each 5-tuple flow to one lcore, so every worker owns
its connection-tracking table with **no locks** on the hot path. The controlplane
(built on **userver**) health-checks backends, aggregates dataplane stats, and
publishes the live backend set; a React dashboard polls its REST API once a second.

```
   kernel NIC ──AF_XDP──▶ per-lcore graph (RSS by 5-tuple):
                          rx → ip4-parse → lb → dnat → ether → tx
                                   │                         ▲
            /cerebellum_stats (DP→CP)       /cerebellum_control (CP→DP)
                                   │                         │
        ┌──────────────────────────▼─────────────────────────┴─┐   ┌────────────┐
        │ controlplane — userver REST  :8080                    │◄──┤ nginx+React│
        │ health checks · cached stats · backend enable/disable │   │ :80        │
        └───────────────────────────────────────────────────────┘   └────────────┘
```

The two planes share memory only — no RPC. The dataplane creates both segments;
the controlplane attaches them. Each segment begins with an `ShmHeader`
(magic + version) that `ShmProvider` stamps on create and verifies on attach, so
a mismatched build degrades gracefully instead of reading a stale layout.

## Layout

| Path            | What |
|-----------------|------|
| `dataplane/`    | DPDK I/O, graph engine (`graph/`), pipeline `nodes/`, conntrack + backend pool (`lb/`) |
| `controlplane/` | userver component + REST handlers, health checker, stats aggregator |
| `libs/ipc/`     | `StatsView` (DP→CP stats) and `ControlView` (CP→DP backend set, seqlock), `CacheAligned<T>` (one atomic per cache line), `ShmHeader` ABI guard |
| `libs/common/`  | config types + YAML loader (shared by both planes) |
| `ui/`           | Vite + React + Tailwind dashboard |
| `nginx/`        | reverse proxy + static host |

The graph engine (`frame`/`node`/`graph`/`dispatcher`/`frame_pool`) only moves
opaque `rte_mbuf*` handles and is DPDK-free; the single DPDK seam is
`packet_meta` (mbuf dynfield), compiled into the dataplane binary. See
`docs/graph-architecture.md`.

## Build & test

Requires a C++23 compiler, CMake ≥ 3.25, and DPDK (`libdpdk-dev`) on the include path.

```bash
just test     # configure (debug+ASAN+tests), build, run Catch2 unit tests
just build    # release build of both binaries
just fmt      # clang-format
```

Unit tests cover `FlowKey` hashing, `ConntrackTable` (open-addressing + lazy
expiry), `BackendPool`/`AtomicRcu`, the `ControlView` seqlock + `ShmHeader`
guard, and the graph dispatch engine. The engine forward-declares `rte_mbuf`,
so the tests build and run without DPDK hardware.

Plane builds are independent: `-DCEREBELLUM_BUILD_DATAPLANE=OFF` (controlplane
only) and `-DCEREBELLUM_BUILD_CONTROLPLANE=OFF` (dataplane only).

## Run

```bash
# Controlplane alone (serves zeros / dataplane="detached" until the DP is up):
./build/controlplane/cerebellum_controlplane --config controlplane/static_config.yaml
curl 127.0.0.1:8080/api/v1/stats
curl 127.0.0.1:8080/api/v1/backends

# Dataplane over AF_XDP on host interface eth0 (needs CAP_NET_ADMIN/NET_RAW + memlock):
./build/dataplane/cerebellum_dataplane --config config.yaml \
    -l 0-1 -n 4 --no-huge -m 512 --vdev net_af_xdp0,iface=eth0

# Frontend dev server (proxies /api to the controlplane):
cd ui && npm install && npm run dev
```

## Deploy

```bash
CEREBELLUM_IFACE=eth0 docker compose up --build
# dashboard on http://<host>/ ; API proxied at /api/v1/*
```

The dataplane container is cloud-native: no `privileged`, no VFIO, no hugepages.
It runs with host networking/IPC, `cap_add` `NET_ADMIN`/`NET_RAW`/`SYS_ADMIN`/
`IPC_LOCK`, `memlock` unlimited, and binds AF_XDP to `${CEREBELLUM_IFACE}`
(default `eth0`). Compose gates start-up on readiness: the controlplane waits for
the dataplane's shared segments, and nginx waits for the controlplane's HTTP
health check. nginx serves HTTP out of the box; terminate TLS there with Let's
Encrypt for production.

## API

`GET /api/v1/stats` — `Cache-Control: max-age=1`; `dataplane` is `connected`
when the shared-memory segment is attached, else `detached`.

```json
{ "dataplane": "connected", "rx_pps": 0, "tx_pps": 0,
  "dropped_pps": 0, "new_flows_ps": 0, "active_flows": 0 }
```

`GET /api/v1/backends`

```json
{
  "vip": "10.0.0.1", "vip_port": 80,
  "backends": [
    { "ip": "10.0.1.1", "port": 8080, "status": "up", "fail_count": 0, "last_check_ms": 312 }
  ]
}
```

Backend health drives forwarding: a backend that fails its probe is published to
the dataplane as disabled, so new flows avoid it while established flows drain
(slot-stable backend indexing keeps conntrack entries valid across rebuilds).

## Configuration

See `config.yaml`: VIP + port, this host's MAC, health-check interval,
conntrack timeout, and the backend list (IP, port, probe port, MAC). Both planes
read the same file.
