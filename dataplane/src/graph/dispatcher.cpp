#include "graph/dispatcher.hpp"

#include <algorithm>
#include <span>

namespace cere::graph {

void Dispatcher::RunOnce(Frame& root_frame) noexcept {
  Reset();

  Frame* seed = pool_.Acquire();
  for (uint16_t i = 0; i < root_frame.count; ++i) {
    seed->Push(root_frame.bufs[i]);
  }
  pending_[root_].push_back(seed);
  Schedule(root_);

  while (!queue_.empty()) {
    const NodeId node_id = queue_.back();
    queue_.pop_back();
    scheduled_[node_id] = 0;
    DrainNode(node_id);
  }
}

void Dispatcher::Schedule(NodeId node_id) noexcept {
  if (scheduled_[node_id] == 0) {
    scheduled_[node_id] = 1;
    queue_.push_back(node_id);
  }
}

void Dispatcher::RouteInto(NodeId target, const Frame& src) {
  auto& pend = pending_[target];
  for (uint16_t i = 0; i < src.count; ++i) {
    if (pend.empty() || pend.back()->Full()) {
      pend.push_back(pool_.Acquire());
    }
    pend.back()->Push(src.bufs[i]);
  }
}

void Dispatcher::RunNode(NodeId node_id, Frame& in) {
  const uint16_t fanout = graph_.Fanout(node_id);
  for (uint16_t out = 0; out < fanout; ++out) {
    scratch_[out].Clear();
  }

  graph_.GetNode(node_id).Process(in, std::span{scratch_.data(), fanout});

  for (uint16_t out = 0; out < fanout; ++out) {
    const NodeId target = graph_.Target(node_id, out);
    if (target == Graph::kInvalid || scratch_[out].count == 0) {
      continue;
    }
    RouteInto(target, scratch_[out]);
    Schedule(target);
  }
}

void Dispatcher::DrainNode(NodeId node_id) {
  drain_scratch_.clear();
  drain_scratch_.swap(pending_[node_id]);

  for (Frame* frame : drain_scratch_) {
    RunNode(node_id, *frame);
    pool_.Release(frame);
  }
}

void Dispatcher::Reset() noexcept {
  for (auto& pend : pending_) {
    for (Frame* frame : pend) {
      pool_.Release(frame);
    }
    pend.clear();
  }
  queue_.clear();
  std::ranges::fill(scheduled_, uint8_t{0});
}

}
