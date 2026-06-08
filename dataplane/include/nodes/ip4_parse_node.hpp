#pragma once
#include "common/config.hpp"
#include "graph/node.hpp"
#include "graph/packet_meta.hpp"

struct rte_ether_hdr;

namespace cere::nodes {

class Ip4ParseNode : public graph::Node {
 public:
  static constexpr uint16_t kNextFwd = 0;
  static constexpr uint16_t kNextRev = 1;
  static constexpr uint16_t kNextDrop = 2;
  static constexpr uint16_t kNextArp = 3;

  explicit Ip4ParseNode(const common::Config& cfg) : cfg_(cfg) {}

  void Process(graph::Frame& in,
               std::span<graph::Frame> nexts) noexcept override;

  std::string_view Name() const noexcept override { return "ip4-parse"; }
  uint16_t NextCount() const noexcept override { return kOutputCount; }

 private:
  static constexpr uint16_t kOutputCount = 4;
  static constexpr uint16_t kPrefetchAhead = 4;

  uint16_t Classify(graph::PacketMeta& meta, rte_mbuf* mbuf) const noexcept;
  uint16_t ClassifyIpv4(graph::PacketMeta& meta,
                        rte_ether_hdr* eth) const noexcept;

  const common::Config& cfg_;
};

}
