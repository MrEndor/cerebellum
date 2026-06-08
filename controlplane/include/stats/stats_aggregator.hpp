#pragma once
#include <array>
#include <cstdint>

#include "ipc/lcore_stats.hpp"

namespace cere::control {

struct AggregatedStats {
  uint64_t rx_pps{};
  uint64_t tx_pps{};
  uint64_t dropped_pps{};
  uint64_t new_flows_ps{};
  uint64_t active_flows{};
  std::array<uint64_t, ipc::kMaxBackends> backend_flows{};
};

class StatsAggregator {
 public:
  StatsAggregator(const ipc::StatsView& view, uint32_t n_lcores)
      : view_(view), n_lcores_(n_lcores) {}

  AggregatedStats Tick();

 private:
  struct Snapshot {
    uint64_t rx{};
    uint64_t tx{};
    uint64_t dropped{};
    uint64_t new_flows{};
  };

  const ipc::StatsView& view_;
  uint32_t n_lcores_;
  Snapshot prev_{};
};

}
