#include "graph/graph.hpp"

#include <format>
#include <stdexcept>
#include <utility>

namespace cere::graph {

Graph::NodeId Graph::AddNode(std::unique_ptr<Node> node) {
  const auto node_id = static_cast<NodeId>(nodes_.size());
  routing_.emplace_back(node->NextCount(), kInvalid);
  nodes_.push_back(std::move(node));
  return node_id;
}

void Graph::Wire(NodeId from, uint16_t out_idx, NodeId dest) {
  if (from >= nodes_.size() || dest >= nodes_.size()) {
    throw std::out_of_range(
        std::format("Wire: invalid node {} or {}", from, dest));
  }
  if (out_idx >= routing_[from].size()) {
    throw std::out_of_range(
        std::format("Wire: out_idx {} >= NextCount()", out_idx));
  }
  routing_[from][out_idx] = dest;
}

}
