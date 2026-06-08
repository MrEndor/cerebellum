#include "nodes/dnat_node.hpp"

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_prefetch.h>
#include <rte_tcp.h>
#include <rte_udp.h>

namespace cere::nodes {

namespace {

constexpr uint16_t kPrefetchAhead = 4;

void SetPorts(void* l4_hdr, uint8_t proto, uint16_t src_port,
              uint16_t dst_port) noexcept {
  const rte_be16_t src = rte_cpu_to_be_16(src_port);
  const rte_be16_t dst = rte_cpu_to_be_16(dst_port);
  if (proto == IPPROTO_TCP) {
    auto* tcp = static_cast<rte_tcp_hdr*>(l4_hdr);
    tcp->src_port = src;
    tcp->dst_port = dst;
  } else {
    auto* udp = static_cast<rte_udp_hdr*>(l4_hdr);
    udp->src_port = src;
    udp->dst_port = dst;
  }
}

void FixChecksums(rte_ipv4_hdr* ip4, uint8_t proto, void* l4_hdr) noexcept {
  ip4->hdr_checksum = 0;
  ip4->hdr_checksum = rte_ipv4_cksum(ip4);

  if (proto == IPPROTO_TCP) {
    auto* tcp = static_cast<rte_tcp_hdr*>(l4_hdr);
    tcp->cksum = 0;
    tcp->cksum = rte_ipv4_udptcp_cksum(ip4, l4_hdr);
  } else {
    auto* udp = static_cast<rte_udp_hdr*>(l4_hdr);
    udp->dgram_cksum = 0;
    udp->dgram_cksum = rte_ipv4_udptcp_cksum(ip4, l4_hdr);
  }
}

}

void DnatNode::Process(graph::Frame& in,
                       std::span<graph::Frame> nexts) noexcept {
  for (uint16_t i = 0; i < in.count; ++i) {
    if (i + kPrefetchAhead < in.count) {
      rte_prefetch0(rte_pktmbuf_mtod(in.bufs[i + kPrefetchAhead], void*));
    }
    Rewrite(graph::Meta(in.bufs[i]), in.bufs[i]);
    in.MoveTo(i, nexts[0]);
  }
}

void DnatNode::Rewrite(const graph::PacketMeta& meta,
                       rte_mbuf* mbuf) const noexcept {
  auto* eth = rte_pktmbuf_mtod(mbuf, rte_ether_hdr*);
  auto* ip4 = reinterpret_cast<rte_ipv4_hdr*>(eth + 1);
  auto* l4_hdr = reinterpret_cast<uint8_t*>(ip4) + rte_ipv4_hdr_len(ip4);

  if (meta.dir == graph::Direction::kForward) {
    ip4->src_addr = rte_cpu_to_be_32(cfg_.self_ip.value);
    ip4->dst_addr = rte_cpu_to_be_32(meta.backend_ip.value);
    SetPorts(l4_hdr, meta.proto, meta.snat_port, meta.backend_port);
  } else {
    ip4->src_addr = rte_cpu_to_be_32(cfg_.vip.value);
    ip4->dst_addr = rte_cpu_to_be_32(meta.client_ip.value);
    SetPorts(l4_hdr, meta.proto, cfg_.vip_port, meta.client_port);
  }

  FixChecksums(ip4, meta.proto, l4_hdr);
}

}
