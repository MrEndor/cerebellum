#include "nodes/lb_node.hpp"

#include <rte_cycles.h>
#include <rte_prefetch.h>

namespace cere::nodes {

uint32_t LbNode::NowMs() noexcept {
  return static_cast<uint32_t>(rte_get_timer_cycles() /
                               (rte_get_timer_hz() / kMsPerSec));
}

void LbNode::Process(graph::Frame& in, std::span<graph::Frame> nexts) noexcept {
  auto pool = pool_rcu_.Load();
  if (!pool || pool->ActiveCount() == 0) {
    for (uint16_t i = 0; i < in.count; ++i) {
      in.MoveTo(i, nexts[kNextDrop]);
    }
    return;
  }

  const uint32_t now_ms = NowMs();

  constexpr uint16_t kPrefetchAhead = 4;
  for (uint16_t i = 0; i < in.count; ++i) {
    if (i + kPrefetchAhead < in.count) {
      rte_prefetch0(&graph::Meta(in.bufs[i + kPrefetchAhead]));
    }
    if (HandlePacket(graph::Meta(in.bufs[i]), *pool, now_ms)) {
      in.MoveTo(i, nexts[kNextDnat]);
    } else {
      in.MoveTo(i, nexts[kNextDrop]);
    }
  }

  stats_.active_flows.store(nat_.Size(), std::memory_order_relaxed);
}

bool LbNode::HandlePacket(graph::PacketMeta& meta, const lb::BackendPool& pool,
                          uint32_t now_ms) noexcept {
  if (meta.dir == graph::Direction::kForward) {
    const net::FlowKey key{meta.src_ip, meta.dst_ip, meta.src_port,
                           meta.dst_port, meta.proto};
    lb::NatFlow* flow = nat_.LookupForward(key, now_ms);
    if (flow == nullptr) {
      if (meta.proto == kProtoTcp && (meta.tcp_flags & kTcpSyn) == 0) {
        return false;
      }
      flow = CreateFlow(meta, key, pool, now_ms);
      if (flow == nullptr) {
        return false;
      }
    }

    meta.backend_ip.value = flow->backend_ip;
    meta.backend_mac = flow->backend_mac;
    meta.backend_port = flow->backend_port;
    meta.snat_port = flow->snat_port;
    MaybeTeardown(*flow, meta.dir, meta.tcp_flags);
    return true;
  }

  lb::NatFlow* flow = nat_.LookupReverse(meta.src_ip, meta.src_port,
                                         meta.dst_port, meta.proto, now_ms);
  if (flow == nullptr) {
    return false;
  }
  meta.client_ip.value = flow->fwd_key.src_ip;
  meta.client_port = flow->fwd_key.src_port;
  meta.client_mac = flow->client_mac;
  MaybeTeardown(*flow, meta.dir, meta.tcp_flags);
  return true;
}

void LbNode::MaybeTeardown(lb::NatFlow& flow, graph::Direction dir,
                           uint8_t tcp_flags) noexcept {
  bool close = (tcp_flags & kTcpRst) != 0;
  if ((tcp_flags & kTcpFin) != 0) {
    flow.fin_seen |=
        (dir == graph::Direction::kForward) ? kFinForward : kFinReverse;
    close = close || flow.fin_seen == (kFinForward | kFinReverse);
  }
  if (!close) {
    return;
  }
  uint16_t released_port = 0;
  uint8_t backend_idx = 0;
  nat_.Close(flow, released_port, backend_idx);
  if (released_port != 0) {
    port_pools_[backend_idx].Release(released_port);
  }
}

lb::NatFlow* LbNode::CreateFlow(const graph::PacketMeta& meta,
                                const net::FlowKey& key,
                                const lb::BackendPool& pool,
                                uint32_t now_ms) noexcept {
  const uint8_t backend_idx = pool.Select(key);
  const common::BackendInfo& backend = pool.At(backend_idx);

  const uint16_t snat_port = port_pools_[backend_idx].Acquire();
  if (snat_port == 0) {
    return nullptr;
  }

  lb::NatFlow flow;
  flow.fwd_key = key;
  flow.client_mac = meta.client_mac;
  flow.backend_ip = backend.ip.value;
  flow.backend_mac = backend.mac;
  flow.backend_port = backend.port;
  flow.snat_port = snat_port;
  flow.backend_idx = backend_idx;

  uint16_t evicted_port = 0;
  uint8_t evicted_idx = 0;
  lb::NatFlow* inserted = nat_.Insert(flow, now_ms, evicted_port, evicted_idx);
  if (evicted_port != 0) {
    port_pools_[evicted_idx].Release(evicted_port);
  }
  if (inserted == nullptr) {
    port_pools_[backend_idx].Release(snat_port);
    return nullptr;
  }

  stats_.new_flows.fetch_add(1, std::memory_order_relaxed);
  stats_.backend_flows[backend_idx].fetch_add(1, std::memory_order_relaxed);
  return inserted;
}

}
