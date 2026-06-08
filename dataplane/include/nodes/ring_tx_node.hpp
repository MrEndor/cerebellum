#pragma once
#include <rte_mbuf.h>
#include <rte_ring.h>

#include "graph/node.hpp"
#include "ipc/lcore_stats.hpp"

namespace cere::nodes {

class RingTxNode : public graph::Node {
 public:
  RingTxNode(rte_ring* ring, ipc::LcoreStats& stats)
      : ring_(ring), stats_(stats) {}

  void Process(graph::Frame& in, std::span<graph::Frame>) noexcept override {
    const unsigned sent = rte_ring_enqueue_burst(
        ring_, reinterpret_cast<void**>(in.bufs.data()), in.count, nullptr);
    if (sent < in.count) {
      rte_pktmbuf_free_bulk(in.bufs.data() + sent, in.count - sent);
      stats_.dropped.fetch_add(in.count - sent, std::memory_order_relaxed);
    }
    in.count = 0;
  }

  std::string_view Name() const noexcept override { return "ring-tx"; }
  uint16_t NextCount() const noexcept override { return 0; }

 private:
  rte_ring* ring_;
  ipc::LcoreStats& stats_;
};

}
