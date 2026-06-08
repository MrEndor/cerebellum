#pragma once
#include "common/config.hpp"
#include "graph/node.hpp"

namespace cere::nodes {

class ArpNode : public graph::Node {
 public:
  static constexpr uint16_t kNextTx = 0;
  static constexpr uint16_t kNextDrop = 1;

  explicit ArpNode(const common::Config& cfg) : cfg_(cfg) {}

  void Process(graph::Frame& in,
               std::span<graph::Frame> nexts) noexcept override;

  std::string_view Name() const noexcept override { return "arp"; }
  uint16_t NextCount() const noexcept override { return kOutputCount; }

 private:
  static constexpr uint16_t kOutputCount = 2;

  bool BuildReply(rte_mbuf* mbuf) const noexcept;

  const common::Config& cfg_;
};

}
