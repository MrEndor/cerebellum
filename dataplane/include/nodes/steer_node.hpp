#pragma once
#include <cstdint>

#include "common/config.hpp"
#include "graph/node.hpp"
#include "ipc/lcore_stats.hpp"

struct rte_ring;

namespace cere::nodes {

class SteerNode : public graph::Node {
 public:
  static constexpr uint16_t kNextArp = 0;

  SteerNode(rte_ring** worker_rings, uint32_t n_workers,
            const common::Config& cfg, ipc::LcoreStats& stats)
      : worker_rings_(worker_rings),
        n_workers_(n_workers),
        cfg_(cfg),
        stats_(stats) {}

  void Process(graph::Frame& in,
               std::span<graph::Frame> nexts) noexcept override;

  std::string_view Name() const noexcept override { return "steer"; }
  uint16_t NextCount() const noexcept override { return 1; }

 private:
  rte_ring** worker_rings_;
  uint32_t n_workers_;
  const common::Config& cfg_;
  ipc::LcoreStats& stats_;
};

}
