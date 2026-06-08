# Graph Engine Architecture

The dataplane forwards packets through a **VPP-style node graph**: each lcore
builds a `Graph` (topology) and runs it with a `Dispatcher` (work-queue
scheduler), packets moving in batches (`Frame`) until the burst is fully
processed. The engine only shuffles buffer handles (`rte_mbuf*`); per-packet
metadata rides inside the mbuf, and the engine never dereferences a buffer.

Responsibilities are split so the build phase and the hot path stay separate:

| File | Type | Role |
|------|------|------|
| `graph/frame.hpp`       | `Frame`      | batch of buffer handles |
| `graph/node.hpp`        | `Node`       | abstract processing stage |
| `graph/graph.hpp`       | `Graph`      | **topology**: nodes + wiring (build-time) |
| `graph/dispatcher.hpp`  | `Dispatcher` | **runtime**: scheduler + per-burst state |
| `graph/frame_pool.hpp`  | `FramePool`  | reusable Frame free-list |
| `graph/packet_meta.hpp` | `PacketMeta` | metadata accessor (production only) |
| `nodes/*.hpp`           | nodes        | concrete stages |

The engine (`frame`/`node`/`graph`/`dispatcher`/`frame_pool`) is DPDK-free: it
only moves opaque `rte_mbuf*` handles (forward-declared), so it builds and unit-
tests without DPDK. The single DPDK seam is `packet_meta` (mbuf dynfield offset
+ `Meta()`/`InitMeta()`), compiled into the `cerebellum_dataplane` binary — not
into the `graph_engine` library.

---

## 1. Types and ownership

```mermaid
classDiagram
    class Frame {
        +array~rte_mbuf*, 64~ bufs
        +uint16 count
        +Clear()
        +Full() bool
        +Push(rte_mbuf*)
        +MoveTo(idx, dst)
    }

    class Node {
        <<abstract>>
        +Process(in, nexts)*
        +Name()*
        +NextCount()*
    }

    class Graph {
        -vector~unique_ptr~Node~~ nodes_
        -vector~vector~NodeId~~ routing_
        +AddNode(node) NodeId
        +Wire(from, out, dest)
        +NodeCount() uint16
        +GetNode(id) Node&
        +Fanout(id) uint16
        +Target(from, out) NodeId
    }

    class Dispatcher {
        -Graph& graph_
        -NodeId root_
        -vector~vector~Frame*~~ pending_
        -vector~uint8~ scheduled_
        -vector~NodeId~ queue_
        -array~Frame,8~ scratch_
        -FramePool pool_
        +RunOnce(root_frame)
    }

    class FramePool {
        -vector~unique_ptr~Frame~~ storage_
        -vector~Frame*~ free_
        +Acquire() Frame*
        +Release(Frame*)
    }

    class PacketMeta {
        +uint32 src_ip
        +uint32 dst_ip
        +uint16 src_port
        +uint16 dst_port
        +uint8 proto
        +uint8 backend_idx
        +Direction dir
    }

    Graph "1" o-- "many" Node : owns
    Dispatcher "1" --> "1" Graph : runs (read topology)
    Dispatcher "1" *-- "1" FramePool : owns
    Dispatcher ..> Frame : routes
    Node ..> Frame : Process()
    FramePool "1" *-- "many" Frame : pools
    Node ..> PacketMeta : Meta(buf) (nodes only)
```

Two boundaries:
- **Build vs run**: `Graph` is the immutable topology (`AddNode`/`Wire`);
  `Dispatcher` holds all per-burst state and the pool. You can't rewire while
  running, and each is testable on its own.
- **Engine vs DPDK**: `Frame`/`Node`/`Graph`/`Dispatcher` know nothing about
  `PacketMeta` or DPDK; `rte_mbuf` is a forward-declared opaque handle. Only
  nodes (and `main`, which calls `InitMeta()`) include `packet_meta.hpp`.

---

## 2. The production LB pipeline

Wired once per lcore in `main.cpp::BuildGraph`. Arcs are named outputs
(`kNextFwd`, `kNextDrop`, …).

```mermaid
flowchart LR
    RX[RxNode<br/>rte_eth_rx_burst] --> P[Ip4ParseNode]

    P -->|fwd: dst==VIP| LB[LbNode<br/>conntrack + select]
    P -->|rev: src is backend| LB
    P -->|drop: other| D[DropNode<br/>free mbufs]

    LB -->|dnat| NAT[DnatNode<br/>rewrite L3/L4 + cksum]
    LB -->|drop: no backend| D

    NAT --> ETH[EtherRewriteNode<br/>src/dst MAC]
    ETH --> TX[TxNode<br/>rte_eth_tx_burst]

    classDef sink fill:#fde,stroke:#c66
    class TX,D sink
```

It is a forward DAG today, but the engine itself imposes no ordering — any node
may route to any node (including itself).

---

## 3. Scheduler: `Dispatcher::RunOnce`

The `Dispatcher` is constructed with the `Graph` and the root node id. A node is
**scheduled** the moment packets are routed to it; the work-queue drains
scheduled nodes until empty.

```mermaid
flowchart TD
    A[Dispatcher.RunOnce root_frame] --> B[Reset: release pending frames,<br/>clear queue + scheduled_]
    B --> C[Acquire seed frame,<br/>copy root_frame handles in]
    C --> D[pending_root += seed<br/>Schedule root]
    D --> E{queue_ empty?}
    E -->|yes| Z[done: all packets at sinks,<br/>all frames back in pool]
    E -->|no| F[pop node N<br/>scheduled_N = 0]
    F --> G[DrainNode N]
    G --> E

    subgraph DrainNode
      G1[swap pending_N into drain_scratch_<br/>pending_N now empty] --> G2{for each frame in<br/>drain_scratch_}
      G2 --> G3[RunNode N, frame]
      G3 --> G4[pool.Release frame]
      G4 --> G2
    end

    subgraph RunNode
      R1[clear scratch_0..fanout] --> R2[node.Process in, scratch_]
      R2 --> R3{for each out arc}
      R3 --> R4{target valid and<br/>scratch_out non-empty?}
      R4 -->|no| R3
      R4 -->|yes| R5[RouteInto target, scratch_out]
      R5 --> R6[Schedule target]
      R6 --> R3
    end
```

Why `pending_N` is swapped out before draining: a node routing back to itself
(self-edge) appends to `pending_N` during `Process`; swapping first means those
new frames land in a fresh list for a **later** pop, so the vector being iterated
is never mutated/reallocated underfoot, and per-dispatch work stays bounded.

---

## 4. Frame lifecycle (one hop)

```mermaid
sequenceDiagram
    participant Q as work-queue
    participant D as Dispatcher
    participant Pool as FramePool
    participant N as Node N
    participant T as pending[target]

    Q->>D: pop N
    D->>D: DrainNode(N): swap pending[N] into drain_scratch_
    loop each input frame
        D->>N: Process(in, scratch_[0..fanout])
        N-->>D: handles distributed into scratch_ arcs
        loop each non-empty arc
            D->>Pool: Acquire() as target frames fill
            D->>T: RouteInto(target, scratch_[out])
            D->>Q: Schedule(target)
        end
        D->>Pool: Release(input frame)
    end
```

No allocation or zeroing on the hot path: `scratch_` is reused (only `count`
reset), and pending frames come from `FramePool` (a free-list); the only growth
is the first time the pool runs dry.

---

## 5. Fan-in and replication

Metadata-in-mbuf makes replication a pointer operation: push the same handle to
multiple arcs (production also bumps `rte_pktmbuf_refcnt_update`). Accumulation
beyond `kBurstSize` at a target is absorbed by chaining pool frames in its
pending FIFO.

```mermaid
flowchart LR
    RPL[ReplicateNode<br/>each pkt to both arcs<br/>64 in] -->|arc 0: 64| S{Sink pending FIFO}
    RPL -->|arc 1: 64| S
    S --> F0["Frame #0 (64)"]
    S --> F1["Frame #1 (64)"]
    F0 --> SINK[SinkNode drains 128]
    F1 --> SINK
```

`RouteInto` opens a new pooled frame whenever the current one is `Full()`, so a
node can receive far more than 64 packets in a single dispatch without overflow.

---

## Invariants

- **No locks**: each lcore owns its `Graph`; RSS pins a 5-tuple flow to one lcore.
- **Engine is DPDK-free**: `frame/node/graph/dispatcher/frame_pool` only move
  opaque `rte_mbuf*`; the sole DPDK coupling is `packet_meta` (dynfield +
  `Meta()`), compiled into the dataplane binary, not the `graph_engine` library.
- **Termination is the caller's contract**: cycles are allowed; the graph must
  eventually route every packet to a sink (`NextCount() == 0`).
- **Unwired arcs drop silently**: production wires every arc (to a real node or
  `DropNode`); an unwired arc discards handles (a leak in production, fine in tests).
