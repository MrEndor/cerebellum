#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <userver/clients/http/client.hpp>
#include <userver/components/component_base.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/rcu/rcu.hpp>
#include <userver/utils/periodic_task.hpp>
#include <userver/yaml_config/schema.hpp>
#include <vector>

#include "common/config.hpp"
#include "health/health_checker.hpp"
#include "ipc/control_view.hpp"
#include "ipc/lcore_stats.hpp"
#include "ipc/shm_provider.hpp"
#include "stats/stats_aggregator.hpp"

namespace cere::control {

class MonitorComponent final : public userver::components::ComponentBase {
 public:
  static constexpr std::string_view kName = "monitor";

  MonitorComponent(const userver::components::ComponentConfig& config,
                   const userver::components::ComponentContext& context);
  ~MonitorComponent() override;

  AggregatedStats LatestStats() const { return latest_.ReadCopy(); }
  const HealthChecker& Health() const noexcept { return health_; }
  const common::Config& Config() const noexcept { return config_; }

  bool DataplaneConnected() const noexcept { return shm_ != nullptr; }

  static userver::yaml_config::Schema GetStaticConfigSchema();

 private:
  void PublishControl();

  void WriteMetrics(const AggregatedStats& stats);

  common::Config config_;
  userver::clients::http::Client& http_;
  std::string tsdb_url_;
  std::unique_ptr<ipc::ShmProvider> shm_;
  std::unique_ptr<ipc::StatsView> fallback_;
  std::unique_ptr<ipc::ShmProvider> control_shm_;
  ipc::ControlView* control_{nullptr};
  std::vector<uint8_t> published_enabled_;
  std::unique_ptr<StatsAggregator> aggregator_;
  userver::rcu::Variable<AggregatedStats> latest_;
  HealthChecker health_;
  userver::utils::PeriodicTask poller_;
};

}
