#include "nodes/rx_node.hpp"

#include <rte_ethdev.h>

#include <atomic>

namespace cere::nodes {

void RxNode::Process(graph::Frame& in, std::span<graph::Frame> nexts) noexcept {
  in.count =
      rte_eth_rx_burst(port_id_, queue_id_, in.bufs.data(), graph::kBurstSize);
  stats_.rx_packets.fetch_add(in.count, std::memory_order_relaxed);

  for (uint16_t i = 0; i < in.count; ++i) {
    in.MoveTo(i, nexts[0]);
  }
}

}
