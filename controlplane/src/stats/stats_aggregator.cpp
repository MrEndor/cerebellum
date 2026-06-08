#include "stats/stats_aggregator.hpp"

namespace cere::control {

AggregatedStats StatsAggregator::Tick() {
  Snapshot current{};
  AggregatedStats out;

  for (uint32_t lcore = 0; lcore < n_lcores_; ++lcore) {
    const ipc::LcoreStats& stats = view_.lcores[lcore];
    current.rx += stats.rx_packets.load(std::memory_order_relaxed);
    current.tx += stats.tx_packets.load(std::memory_order_relaxed);
    current.dropped += stats.dropped.load(std::memory_order_relaxed);
    current.new_flows += stats.new_flows.load(std::memory_order_relaxed);
    out.active_flows += stats.active_flows.load(std::memory_order_relaxed);
    for (uint32_t backend = 0; backend < ipc::kMaxBackends; ++backend) {
      out.backend_flows[backend] +=
          stats.backend_flows[backend].load(std::memory_order_relaxed);
    }
  }

  out.rx_pps = current.rx - prev_.rx;
  out.tx_pps = current.tx - prev_.tx;
  out.dropped_pps = current.dropped - prev_.dropped;
  out.new_flows_ps = current.new_flows - prev_.new_flows;
  prev_ = current;
  return out;
}

}
