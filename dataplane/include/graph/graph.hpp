#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "graph/node.hpp"

namespace cere::graph {

class Graph {
 public:
  using NodeId = uint16_t;
  static constexpr NodeId kInvalid = 0xFFFF;
  static constexpr uint16_t kMaxFanout = 8;

  NodeId AddNode(std::unique_ptr<Node> node);
  void Wire(NodeId from, uint16_t out_idx, NodeId dest);

  uint16_t NodeCount() const noexcept {
    return static_cast<uint16_t>(nodes_.size());
  }
  Node& GetNode(NodeId node_id) const noexcept { return *nodes_[node_id]; }
  uint16_t Fanout(NodeId node_id) const noexcept {
    return static_cast<uint16_t>(routing_[node_id].size());
  }
  NodeId Target(NodeId from, uint16_t out_idx) const noexcept {
    return routing_[from][out_idx];
  }

 private:
  std::vector<std::unique_ptr<Node>> nodes_;
  std::vector<std::vector<NodeId>> routing_;
};

}
