#pragma once
#include <rte_mbuf.h>
#include <rte_ring.h>

#include "graph/node.hpp"

namespace cere::nodes {

class RingRxNode : public graph::Node {
 public:
  explicit RingRxNode(rte_ring* ring) : ring_(ring) {}

  void Process(graph::Frame& in,
               std::span<graph::Frame> nexts) noexcept override {
    in.count = static_cast<uint16_t>(
        rte_ring_dequeue_burst(ring_, reinterpret_cast<void**>(in.bufs.data()),
                               graph::kBurstSize, nullptr));
    for (uint16_t i = 0; i < in.count; ++i) {
      in.MoveTo(i, nexts[0]);
    }
  }

  std::string_view Name() const noexcept override { return "ring-rx"; }
  uint16_t NextCount() const noexcept override { return 1; }

 private:
  rte_ring* ring_;
};

}
