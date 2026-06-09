# Getting started

## Requirements

- A **C++23** compiler and **CMake ≥ 3.25**
- **DPDK** (`libdpdk-dev`) on the include path — for building and running the dataplane
- **Docker** + Docker Compose — for the full stack
- [`just`](https://github.com/casey/just) — optional, wraps the CMake presets

## Build & test

```bash
just test     # configure (debug + ASAN + tests), build, run the Catch2 unit tests
just build    # release build of both binaries
just fmt      # clang-format
```

The unit tests run without DPDK hardware: the graph engine forward-declares
`rte_mbuf`, so the conntrack table, port-pool steering, backend pool, the
shared-memory seqlock and the dispatch engine are all tested on any machine.

The two planes build independently if you only need one:

```bash
cmake -DCEREBELLUM_BUILD_DATAPLANE=OFF ...      # controlplane only
cmake -DCEREBELLUM_BUILD_CONTROLPLANE=OFF ...   # dataplane only
```

## Run the binaries directly

```bash
# Controlplane (serves zeros / dataplane="detached" until the dataplane is up):
./build/controlplane/cerebellum_controlplane --config controlplane/static_config.yaml

# Single-thread dataplane over AF_XDP on eth0
# (needs CAP_NET_ADMIN / NET_RAW and an unlimited memlock):
./build/dataplane/cerebellum_dataplane --config config.yaml \
    -l 0-1 -n 4 --no-huge -m 512 --vdev net_af_xdp0,iface=eth0,start_queue=0,queue_count=1
```

The lcore mask chooses the mode automatically:

- `-l 0-1` — **single-thread**: one worker owns the AF_XDP queue and runs the
  whole graph. Simplest and fastest on a veth copy-mode stand.
- `-l 0-4` and up — **software distributor**: an IO lcore steers flows to worker
  lcores, with a dedicated TX lcore. Workers are shared-nothing.

Check it is alive:

```bash
curl 127.0.0.1:8080/api/v1/stats
curl 127.0.0.1:8080/api/v1/backends
```

## Run the whole stack with Docker

### Dev (builds locally, HTTP on `localhost`)

```bash
CEREBELLUM_IFACE=eth0 docker compose up --build
```

This brings up the dataplane, controlplane, nginx + dashboard, three demo
`whoami` backends, VictoriaMetrics and Grafana. Compose gates start-up on
readiness: the controlplane waits for the dataplane's shared segments, and nginx
waits for the controlplane's health check. The dashboard is served on
`console.localhost`.

### Production (images from ghcr, env-driven, TLS)

```bash
cp .env.template .env       # then edit it
docker compose -f docker-compose.prod.yml up -d
```

Set in `.env`:

| Variable                    | What                                                            |
|-----------------------------|-----------------------------------------------------------------|
| `IMAGE_PREFIX`, `IMAGE_TAG` | which ghcr images to pull                                       |
| `DOMAIN`                    | base domain — serves `console.${DOMAIN}` and `whoami.${DOMAIN}` |
| `CEREBELLUM_IFACE`          | the host interface the dataplane binds AF_XDP to                |
| `DATAPLANE_LCORES`          | lcore mask, e.g. `0-1` (single-thread) or `0-4` (distributor)   |
| `TLS_CERT_DIR`              | directory holding `fullchain.pem` + `privkey.pem`               |
| `GRAFANA_ADMIN_PASSWORD`    | Grafana admin password                                          |

TLS is **bring-your-own cert**: mount a cert directory and the `10-tls.envsh`
entrypoint enables TLS only when certs are present, redirecting `80 → 443`. No
certbot.

## Configuration

Both planes read the same `config.yaml`:

```yaml
vip: "10.0.1.253"          # the virtual IP clients hit
vip_port: 80
self_ip: "10.0.1.254"      # the load balancer's own address (used for SNAT)
self_mac: "aa:bb:cc:dd:ee:ff"
health_interval_ms: 3000   # how often backends are probed
conntrack_timeout_ms: 30000
backends:
  - ip: "10.0.1.1"
    port: 8080
    probe_port: 8080       # TCP port the health checker connects to
    mac: "52:54:00:11:22:33"
```

A backend that fails its probe is published to the dataplane as disabled — new
flows avoid it while established flows drain. Slot-stable backend indexing keeps
conntrack entries valid across rebuilds.
