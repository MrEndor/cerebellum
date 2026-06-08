#include "nodes/ether_rewrite_node.hpp"

#include <rte_ether.h>
#include <rte_mbuf.h>
#include <rte_prefetch.h>

#include <cstring>

#include "graph/packet_meta.hpp"

namespace cere::nodes {

namespace {
constexpr uint16_t kPrefetchAhead = 4;
}

void EtherRewriteNode::Process(graph::Frame& in,
                               std::span<graph::Frame> nexts) noexcept {
  for (uint16_t i = 0; i < in.count; ++i) {
    if (i + kPrefetchAhead < in.count) {
      rte_prefetch0(rte_pktmbuf_mtod(in.bufs[i + kPrefetchAhead], void*));
    }
    auto* eth = rte_pktmbuf_mtod(in.bufs[i], rte_ether_hdr*);
    const graph::PacketMeta& meta = graph::Meta(in.bufs[i]);

    const auto& dst_mac = (meta.dir == graph::Direction::kForward)
                              ? meta.backend_mac
                              : meta.client_mac;
    std::memcpy(eth->dst_addr.addr_bytes, dst_mac.bytes.data(),
                RTE_ETHER_ADDR_LEN);
    std::memcpy(eth->src_addr.addr_bytes, cfg_.self_mac.bytes.data(),
                RTE_ETHER_ADDR_LEN);

    in.MoveTo(i, nexts[0]);
  }
}

}
