#pragma once
#include "common/config.hpp"
#include "graph/node.hpp"
#include "graph/packet_meta.hpp"

namespace cere::nodes {

class DnatNode : public graph::Node {
 public:
  explicit DnatNode(const common::Config& cfg) : cfg_(cfg) {}

  void Process(graph::Frame& in,
               std::span<graph::Frame> nexts) noexcept override;

  std::string_view Name() const noexcept override { return "dnat"; }
  uint16_t NextCount() const noexcept override { return 1; }

 private:
  void Rewrite(const graph::PacketMeta& meta, rte_mbuf* mbuf) const noexcept;

  const common::Config& cfg_;
};

}
