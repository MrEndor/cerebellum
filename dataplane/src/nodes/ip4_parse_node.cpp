#include "nodes/ip4_parse_node.hpp"

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_prefetch.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include <cstring>

namespace cere::nodes {

namespace {

struct L4Ports {
  uint16_t src;
  uint16_t dst;
};

L4Ports ReadPorts(const uint8_t* l4_hdr, uint8_t proto) noexcept {
  if (proto == IPPROTO_TCP) {
    const auto* tcp = reinterpret_cast<const rte_tcp_hdr*>(l4_hdr);
    return {rte_be_to_cpu_16(tcp->src_port), rte_be_to_cpu_16(tcp->dst_port)};
  }
  const auto* udp = reinterpret_cast<const rte_udp_hdr*>(l4_hdr);
  return {rte_be_to_cpu_16(udp->src_port), rte_be_to_cpu_16(udp->dst_port)};
}

uint8_t ReadTcpFlags(const uint8_t* l4_hdr, uint8_t proto) noexcept {
  if (proto != IPPROTO_TCP) {
    return 0;
  }
  return reinterpret_cast<const rte_tcp_hdr*>(l4_hdr)->tcp_flags;
}

}

void Ip4ParseNode::Process(graph::Frame& in,
                           std::span<graph::Frame> nexts) noexcept {
  for (uint16_t i = 0; i < in.count; ++i) {
    if (i + kPrefetchAhead < in.count) {
      rte_prefetch0(rte_pktmbuf_mtod(in.bufs[i + kPrefetchAhead], void*));
    }
    in.MoveTo(i, nexts[Classify(graph::Meta(in.bufs[i]), in.bufs[i])]);
  }
}

uint16_t Ip4ParseNode::Classify(graph::PacketMeta& meta,
                                rte_mbuf* mbuf) const noexcept {
  auto* eth = rte_pktmbuf_mtod(mbuf, rte_ether_hdr*);
  const uint16_t ether_type = rte_be_to_cpu_16(eth->ether_type);
  if (ether_type == RTE_ETHER_TYPE_ARP) {
    return kNextArp;
  }
  if (ether_type != RTE_ETHER_TYPE_IPV4) {
    return kNextDrop;
  }
  return ClassifyIpv4(meta, eth);
}

uint16_t Ip4ParseNode::ClassifyIpv4(graph::PacketMeta& meta,
                                    rte_ether_hdr* eth) const noexcept {
  auto* ip4 = reinterpret_cast<rte_ipv4_hdr*>(eth + 1);
  const uint8_t proto = ip4->next_proto_id;
  if (proto != IPPROTO_TCP && proto != IPPROTO_UDP) {
    return kNextDrop;
  }

  const auto* l4_hdr = reinterpret_cast<uint8_t*>(ip4) + rte_ipv4_hdr_len(ip4);
  const L4Ports ports = ReadPorts(l4_hdr, proto);
  const uint32_t src_ip = rte_be_to_cpu_32(ip4->src_addr);
  const uint32_t dst_ip = rte_be_to_cpu_32(ip4->dst_addr);

  meta = {src_ip,    dst_ip, ports.src,
          ports.dst, proto,  graph::Direction::kForward};
  meta.tcp_flags = ReadTcpFlags(l4_hdr, proto);

  std::memcpy(meta.client_mac.bytes.data(), eth->src_addr.addr_bytes,
              RTE_ETHER_ADDR_LEN);

  if (dst_ip == cfg_.vip.value && ports.dst == cfg_.vip_port) {
    meta.dir = graph::Direction::kForward;
    return kNextFwd;
  }

  if (dst_ip == cfg_.self_ip.value) {
    meta.dir = graph::Direction::kReverse;
    return kNextRev;
  }
  return kNextDrop;
}

}
