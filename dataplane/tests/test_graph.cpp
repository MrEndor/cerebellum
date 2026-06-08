#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <vector>

#include "graph/dispatcher.hpp"
#include "graph/graph.hpp"

using namespace cere::graph;

namespace {

class CountNode : public Node {
 public:
  explicit CountNode(std::string_view name) : name_(name) {}
  void Process(Frame& in, std::span<Frame> nexts) noexcept override {
    count_ += in.count;
    if (!nexts.empty()) {
      for (uint16_t i = 0; i < in.count; ++i) {
        in.MoveTo(i, nexts[0]);
      }
    }
  }
  std::string_view Name() const noexcept override { return name_; }
  uint16_t NextCount() const noexcept override { return 1; }
  uint32_t Count() const noexcept { return count_; }

 private:
  std::string_view name_;
  uint32_t count_{0};
};

class SplitNode : public Node {
 public:
  void Process(Frame& in, std::span<Frame> nexts) noexcept override {
    for (uint16_t i = 0; i < in.count; ++i) {
      in.MoveTo(i, nexts[i % 2]);
    }
  }
  std::string_view Name() const noexcept override { return "split"; }
  uint16_t NextCount() const noexcept override { return 2; }
};

class SinkNode : public Node {
 public:
  void Process(Frame& in, std::span<Frame>) noexcept override {
    seen_ += in.count;
  }
  std::string_view Name() const noexcept override { return "sink"; }
  uint16_t NextCount() const noexcept override { return 0; }
  uint32_t Count() const noexcept { return seen_; }

 private:
  uint32_t seen_{0};
};

class ReplicateNode : public Node {
 public:
  void Process(Frame& in, std::span<Frame> nexts) noexcept override {
    for (uint16_t i = 0; i < in.count; ++i) {
      in.MoveTo(i, nexts[0]);
      in.MoveTo(i, nexts[1]);
    }
  }
  std::string_view Name() const noexcept override { return "replicate"; }
  uint16_t NextCount() const noexcept override { return 2; }
};

class LoopNode : public Node {
 public:
  void Process(Frame& in, std::span<Frame> nexts) noexcept override {
    const uint16_t out = (passes_ == 0) ? 0 : 1;
    for (uint16_t i = 0; i < in.count; ++i) {
      in.MoveTo(i, nexts[out]);
    }
    ++passes_;
  }
  std::string_view Name() const noexcept override { return "loop"; }
  uint16_t NextCount() const noexcept override { return 2; }

 private:
  uint32_t passes_{0};
};

class RecordNode : public Node {
 public:
  void Process(Frame& in, std::span<Frame>) noexcept override {
    for (uint16_t i = 0; i < in.count; ++i) {
      received_.push_back(in.bufs[i]);
    }
  }
  std::string_view Name() const noexcept override { return "record"; }
  uint16_t NextCount() const noexcept override { return 0; }
  const std::vector<rte_mbuf*>& Received() const noexcept { return received_; }

 private:
  std::vector<rte_mbuf*> received_;
};

}

TEST_CASE("Graph - linear chain passes all packets", "[graph]") {
  Graph graph;
  auto* node_a = new CountNode("a");
  auto* node_b = new CountNode("b");
  auto* sink = new SinkNode;
  const auto id_a = graph.AddNode(std::unique_ptr<Node>(node_a));
  const auto id_b = graph.AddNode(std::unique_ptr<Node>(node_b));
  const auto id_sink = graph.AddNode(std::unique_ptr<Node>(sink));
  graph.Wire(id_a, 0, id_b);
  graph.Wire(id_b, 0, id_sink);

  Frame root;
  root.count = 5;
  Dispatcher dispatcher(graph, id_a);
  dispatcher.RunOnce(root);

  REQUIRE(node_a->Count() == 5);
  REQUIRE(node_b->Count() == 5);
  REQUIRE(sink->Count() == 5);
}

TEST_CASE("Graph - splitter distributes packets", "[graph]") {
  Graph graph;
  auto* splitter = new SplitNode;
  auto* even = new SinkNode;
  auto* odd = new SinkNode;
  const auto id_split = graph.AddNode(std::unique_ptr<Node>(splitter));
  const auto id_even = graph.AddNode(std::unique_ptr<Node>(even));
  const auto id_odd = graph.AddNode(std::unique_ptr<Node>(odd));
  graph.Wire(id_split, 0, id_even);
  graph.Wire(id_split, 1, id_odd);

  Frame root;
  root.count = 6;
  Dispatcher dispatcher(graph, id_split);
  dispatcher.RunOnce(root);

  REQUIRE(even->Count() == 3);
  REQUIRE(odd->Count() == 3);
}

TEST_CASE("Graph - unwired output drops packets silently", "[graph]") {
  Graph graph;
  auto* splitter = new SplitNode;
  auto* even = new SinkNode;
  const auto id_split = graph.AddNode(std::unique_ptr<Node>(splitter));
  const auto id_even = graph.AddNode(std::unique_ptr<Node>(even));
  graph.Wire(id_split, 0, id_even);

  Frame root;
  root.count = 6;
  Dispatcher dispatcher(graph, id_split);
  dispatcher.RunOnce(root);

  REQUIRE(even->Count() == 3);
}

TEST_CASE("Graph - Wire invalid node id throws", "[graph]") {
  Graph graph;
  graph.AddNode(std::make_unique<SinkNode>());
  REQUIRE_THROWS_AS(graph.Wire(0, 0, 99), std::out_of_range);
}

TEST_CASE("Graph - Wire invalid output index throws", "[graph]") {
  Graph graph;
  graph.AddNode(std::make_unique<SinkNode>());
  REQUIRE_THROWS_AS(graph.Wire(0, 0, 0), std::out_of_range);
}

TEST_CASE("Graph - replication fans a full burst past kBurstSize", "[graph]") {
  Graph graph;
  auto* replicate = new ReplicateNode;
  auto* sink = new SinkNode;
  const auto id_rep = graph.AddNode(std::unique_ptr<Node>(replicate));
  const auto id_sink = graph.AddNode(std::unique_ptr<Node>(sink));
  graph.Wire(id_rep, 0, id_sink);
  graph.Wire(id_rep, 1, id_sink);

  Frame root;
  root.count = kBurstSize;
  Dispatcher dispatcher(graph, id_rep);
  dispatcher.RunOnce(root);

  REQUIRE(sink->Count() == 2 * kBurstSize);
}

TEST_CASE("Graph - back-edge loops then terminates", "[graph]") {
  Graph graph;
  auto* loop = new LoopNode;
  auto* sink = new SinkNode;
  const auto id_loop = graph.AddNode(std::unique_ptr<Node>(loop));
  const auto id_sink = graph.AddNode(std::unique_ptr<Node>(sink));
  graph.Wire(id_loop, 0, id_loop);
  graph.Wire(id_loop, 1, id_sink);

  Frame root;
  root.count = 3;
  Dispatcher dispatcher(graph, id_loop);
  dispatcher.RunOnce(root);

  REQUIRE(sink->Count() == 3);
}

TEST_CASE("Graph - the same handle a node emits arrives downstream",
          "[graph]") {
  Graph graph;
  auto* fwd = new CountNode("fwd");
  auto* record = new RecordNode;
  const auto id_fwd = graph.AddNode(std::unique_ptr<Node>(fwd));
  const auto id_record = graph.AddNode(std::unique_ptr<Node>(record));
  graph.Wire(id_fwd, 0, id_record);

  std::array<std::byte, 4> dummy{};
  Frame root;
  for (std::byte& slot : dummy) {
    root.Push(reinterpret_cast<rte_mbuf*>(&slot));
  }
  Dispatcher dispatcher(graph, id_fwd);
  dispatcher.RunOnce(root);

  REQUIRE(record->Received().size() == dummy.size());
  for (std::size_t i = 0; i < dummy.size(); ++i) {
    REQUIRE(record->Received()[i] == reinterpret_cast<rte_mbuf*>(&dummy[i]));
  }
}
