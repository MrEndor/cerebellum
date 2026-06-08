#include "monitor/monitor_component.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <sstream>
#include <string>
#include <userver/clients/http/component.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <utility>
#include <vector>

#include "common/config_loader.hpp"
#include "health/backend_state.hpp"
#include "userver/yaml_config/merge_schemas.hpp"

namespace cere::control {

namespace {

constexpr char kShmName[] = "/cerebellum_stats";
constexpr char kControlShmName[] = "/cerebellum_control";
constexpr auto kStatsInterval = std::chrono::seconds{1};

}

MonitorComponent::MonitorComponent(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : ComponentBase(config, context),
      config_(common::LoadConfig(config["app-config"].As<std::string>())),
      http_(context.FindComponent<userver::components::HttpClient>()
                .GetHttpClient()),
      tsdb_url_(config["tsdb-url"].As<std::string>("")),
      health_(config_) {
  ipc::StatsView* view = nullptr;
  try {
    shm_ = std::make_unique<ipc::ShmProvider>(kShmName, sizeof(ipc::StatsView),
                                              ipc::ShmProvider::Mode::kAttach);
    view = shm_->As<ipc::StatsView>();
  } catch (const std::exception&) {
    shm_.reset();
    fallback_ = std::make_unique<ipc::StatsView>();
    view = fallback_.get();
  }

  try {
    control_shm_ = std::make_unique<ipc::ShmProvider>(
        kControlShmName, sizeof(ipc::ControlView),
        ipc::ShmProvider::Mode::kAttach);
    control_ = control_shm_->As<ipc::ControlView>();
  } catch (const std::exception&) {
    control_shm_.reset();
    control_ = nullptr;
  }

  aggregator_ = std::make_unique<StatsAggregator>(*view, ipc::kMaxLcores);
  health_.Start();

  poller_.Start("stats-poller",
                {kStatsInterval, {userver::utils::PeriodicTask::Flags::kNow}},
                [this] {
                  const AggregatedStats agg = aggregator_->Tick();
                  latest_.Assign(agg);
                  PublishControl();
                  WriteMetrics(agg);
                });
}

void MonitorComponent::WriteMetrics(const AggregatedStats& stats) {
  if (tsdb_url_.empty()) {
    return;
  }

  std::ostringstream body;
  body << "cerebellum_stats rx_pps=" << stats.rx_pps
       << ",tx_pps=" << stats.tx_pps << ",dropped_pps=" << stats.dropped_pps
       << ",new_flows_ps=" << stats.new_flows_ps
       << ",active_flows=" << stats.active_flows << '\n';
  const std::vector<BackendState> states = health_.States();
  for (std::size_t i = 0; i < states.size(); ++i) {
    const uint64_t flows =
        i < stats.backend_flows.size() ? stats.backend_flows[i] : 0U;
    const int up = states[i].status == HealthStatus::kUp ? 1 : 0;
    body << "cerebellum_backend,addr=" << states[i].info.ip.ToString() << ':'
         << states[i].info.port << " flows=" << flows << "i,up=" << up << "i\n";
  }
  try {
    const auto response = http_.CreateRequest()
                              .post(tsdb_url_ + "/write")
                              .data(body.str())
                              .timeout(std::chrono::milliseconds{500})
                              .perform();
    static_cast<void>(response);
  } catch (const std::exception&) {
  }
}

void MonitorComponent::PublishControl() {
  if (control_ == nullptr) {
    return;
  }
  const std::vector<BackendState> states = health_.States();

  std::vector<uint8_t> enabled;
  enabled.reserve(states.size());
  for (const BackendState& state : states) {
    enabled.push_back(state.status == HealthStatus::kUp ? 1U : 0U);
  }
  if (enabled == published_enabled_) {
    return;
  }

  std::vector<ipc::ControlBackend> entries;
  entries.reserve(states.size());
  for (std::size_t i = 0; i < states.size(); ++i) {
    ipc::ControlBackend entry;
    entry.ip = states[i].info.ip.value;
    entry.port = states[i].info.port;
    std::copy(states[i].info.mac.bytes.begin(), states[i].info.mac.bytes.end(),
              std::begin(entry.mac));
    entry.enabled = enabled[i];
    entries.push_back(entry);
  }
  ipc::PublishControl(*control_, entries.data(),
                      static_cast<uint32_t>(entries.size()));
  published_enabled_ = std::move(enabled);
}

MonitorComponent::~MonitorComponent() {
  poller_.Stop();
  health_.Stop();
}

userver::yaml_config::Schema MonitorComponent::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<ComponentBase>(R"(
type: object
description: Cerebellum monitor (health checking + stats aggregation)
additionalProperties: false
properties:
    app-config:
        type: string
        description: path to the cerebellum YAML config (vip, backends)
    tsdb-url:
        type: string
        description: base URL of the time-series DB (InfluxDB line protocol); empty disables export
        defaultDescription: ''
)");
}

}
