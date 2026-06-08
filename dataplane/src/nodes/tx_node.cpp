#include "nodes/tx_node.hpp"

#include <rte_ethdev.h>
#include <rte_mbuf.h>

namespace cere::nodes {

void TxNode::Process(graph::Frame& in, std::span<graph::Frame>) noexcept {
  const uint16_t sent =
      rte_eth_tx_burst(port_id_, queue_id_, in.bufs.data(), in.count);
  const uint16_t unsent = in.count - sent;

  if (unsent > 0) {
    rte_pktmbuf_free_bulk(in.bufs.data() + sent, unsent);
  }

  stats_.tx_packets.fetch_add(sent, std::memory_order_relaxed);
  stats_.dropped.fetch_add(unsent, std::memory_order_relaxed);
  in.count = 0;
}

}
