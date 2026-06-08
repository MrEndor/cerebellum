#pragma once
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include <cstdint>

#include "common/config.hpp"
#include "net/flow_key.hpp"

namespace cere::lb {

enum class SteerClass : uint8_t { kSteer, kArp, kDrop };

struct SteerResult {
  SteerClass cls;
  uint32_t worker;
};

struct L4Ports {
  uint16_t src;
  uint16_t dst;
};

inline L4Ports ReadL4Ports(const uint8_t* l4_hdr, uint8_t proto) noexcept {
  if (proto == IPPROTO_TCP) {
    const auto* tcp = reinterpret_cast<const rte_tcp_hdr*>(l4_hdr);
    return {rte_be_to_cpu_16(tcp->src_port), rte_be_to_cpu_16(tcp->dst_port)};
  }
  const auto* udp = reinterpret_cast<const rte_udp_hdr*>(l4_hdr);
  return {rte_be_to_cpu_16(udp->src_port), rte_be_to_cpu_16(udp->dst_port)};
}

inline SteerResult ClassifyForSteer(rte_mbuf* mbuf, const common::Config& cfg,
                                    uint32_t n_workers) noexcept {
  const auto* eth = rte_pktmbuf_mtod(mbuf, const rte_ether_hdr*);
  const uint16_t ether_type = rte_be_to_cpu_16(eth->ether_type);
  if (ether_type == RTE_ETHER_TYPE_ARP) {
    return {SteerClass::kArp, 0};
  }
  if (ether_type != RTE_ETHER_TYPE_IPV4) {
    return {SteerClass::kDrop, 0};
  }

  const auto* ip4 = reinterpret_cast<const rte_ipv4_hdr*>(eth + 1);
  const uint8_t proto = ip4->next_proto_id;
  if (proto != IPPROTO_TCP && proto != IPPROTO_UDP) {
    return {SteerClass::kDrop, 0};
  }

  const auto* l4_hdr =
      reinterpret_cast<const uint8_t*>(ip4) + rte_ipv4_hdr_len(ip4);
  const L4Ports ports = ReadL4Ports(l4_hdr, proto);
  const uint32_t src_ip = rte_be_to_cpu_32(ip4->src_addr);
  const uint32_t dst_ip = rte_be_to_cpu_32(ip4->dst_addr);

  if (dst_ip == cfg.vip.value && ports.dst == cfg.vip_port) {
    const net::FlowKey key{src_ip, dst_ip, ports.src, ports.dst, proto};
    return {SteerClass::kSteer, net::SteerWorker(key, n_workers)};
  }
  if (dst_ip == cfg.self_ip.value) {
    return {SteerClass::kSteer, ports.dst % n_workers};
  }
  return {SteerClass::kDrop, 0};
}

}
