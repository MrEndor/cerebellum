#pragma once
#include "common/config.hpp"
#include "graph/node.hpp"

namespace cere::nodes {

class EtherRewriteNode : public graph::Node {
 public:
  explicit EtherRewriteNode(const common::Config& cfg) : cfg_(cfg) {}

  void Process(graph::Frame& in,
               std::span<graph::Frame> nexts) noexcept override;

  std::string_view Name() const noexcept override { return "ether-rewrite"; }
  uint16_t NextCount() const noexcept override { return 1; }

 private:
  const common::Config& cfg_;
};

}
