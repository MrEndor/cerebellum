#include "nodes/arp_node.hpp"

#include <rte_arp.h>
#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_mbuf.h>

#include <cstring>

namespace cere::nodes {

void ArpNode::Process(graph::Frame& in,
                      std::span<graph::Frame> nexts) noexcept {
  for (uint16_t i = 0; i < in.count; ++i) {
    const uint16_t out = BuildReply(in.bufs[i]) ? kNextTx : kNextDrop;
    in.MoveTo(i, nexts[out]);
  }
}

bool ArpNode::BuildReply(rte_mbuf* mbuf) const noexcept {
  auto* eth = rte_pktmbuf_mtod(mbuf, rte_ether_hdr*);
  auto* arp = reinterpret_cast<rte_arp_hdr*>(eth + 1);

  if (arp->arp_opcode != rte_cpu_to_be_16(RTE_ARP_OP_REQUEST)) {
    return false;
  }
  const uint32_t target = rte_be_to_cpu_32(arp->arp_data.arp_tip);
  if (target != cfg_.self_ip.value && target != cfg_.vip.value) {
    return false;
  }

  rte_ether_addr self{};
  std::memcpy(self.addr_bytes, cfg_.self_mac.bytes.data(), RTE_ETHER_ADDR_LEN);

  eth->dst_addr = eth->src_addr;
  eth->src_addr = self;

  arp->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REPLY);
  arp->arp_data.arp_tha = arp->arp_data.arp_sha;
  arp->arp_data.arp_tip = arp->arp_data.arp_sip;
  arp->arp_data.arp_sha = self;
  arp->arp_data.arp_sip = rte_cpu_to_be_32(target);
  return true;
}

}
