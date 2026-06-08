#pragma once
#include "graph/node.hpp"
#include "ipc/lcore_stats.hpp"

namespace cere::nodes {

class TxNode : public graph::Node {
 public:
  TxNode(uint16_t port_id, uint16_t queue_id, ipc::LcoreStats& stats)
      : port_id_(port_id), queue_id_(queue_id), stats_(stats) {}

  void Process(graph::Frame& in, std::span<graph::Frame>) noexcept override;

  std::string_view Name() const noexcept override { return "tx"; }
  uint16_t NextCount() const noexcept override { return 0; }

 private:
  uint16_t port_id_;
  uint16_t queue_id_;
  ipc::LcoreStats& stats_;
};

}
