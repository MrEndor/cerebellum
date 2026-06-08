#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "graph/frame.hpp"
#include "graph/frame_pool.hpp"
#include "graph/graph.hpp"

namespace cere::graph {

class Dispatcher {
 public:
  using NodeId = Graph::NodeId;

  Dispatcher(Graph& graph, NodeId root)
      : graph_(graph),
        root_(root),
        pending_(graph.NodeCount()),
        scheduled_(graph.NodeCount(), 0),
        pool_(graph.NodeCount() + Graph::kMaxFanout) {}

  void RunOnce(Frame& root_frame) noexcept;

 private:
  void Schedule(NodeId node_id) noexcept;
  void RouteInto(NodeId target, const Frame& src);
  void RunNode(NodeId node_id, Frame& in);
  void DrainNode(NodeId node_id);
  void Reset() noexcept;

  Graph& graph_;
  NodeId root_;

  std::vector<std::vector<Frame*>> pending_;
  std::vector<uint8_t> scheduled_;
  std::vector<NodeId> queue_;
  std::vector<Frame*> drain_scratch_;
  std::array<Frame, Graph::kMaxFanout> scratch_;
  FramePool pool_;
};

}
