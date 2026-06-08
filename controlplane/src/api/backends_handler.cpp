#include "api/backends_handler.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <userver/components/component_context.hpp>
#include <userver/formats/common/type.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/http/content_type.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_response.hpp>

#include "health/backend_state.hpp"
#include "monitor/monitor_component.hpp"

namespace cere::control {

namespace {

constexpr uint64_t kNsPerMs = 1'000'000;

std::string StatusName(HealthStatus status) {
  switch (status) {
    case HealthStatus::kUp:
      return "up";
    case HealthStatus::kDown:
      return "down";
    case HealthStatus::kDraining:
      return "draining";
    case HealthStatus::kUnknown:
      break;
  }
  return "unknown";
}

uint64_t NowMs() noexcept {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(system_clock::now().time_since_epoch())
          .count());
}

}

BackendsHandler::BackendsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : userver::server::handlers::HttpHandlerBase(config, context),
      monitor_(context.FindComponent<MonitorComponent>()) {}

std::string BackendsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  const common::Config& cfg = monitor_.Config();
  const uint64_t now_ms = NowMs();

  const auto stats = monitor_.LatestStats();

  userver::formats::json::ValueBuilder body;
  body["vip"] = cfg.vip.ToString();
  body["vip_port"] = cfg.vip_port;

  userver::formats::json::ValueBuilder backends(
      userver::formats::common::Type::kArray);
  const auto& states = monitor_.Health().States();
  for (std::size_t i = 0; i < states.size(); ++i) {
    const auto& state = states[i];
    const uint64_t last_ms = state.last_check_ns / kNsPerMs;
    const uint64_t age_ms = now_ms > last_ms ? now_ms - last_ms : 0;
    const uint64_t flows =
        i < stats.backend_flows.size() ? stats.backend_flows[i] : uint64_t{0};

    userver::formats::json::ValueBuilder entry;
    entry["ip"] = state.info.ip.ToString();
    entry["port"] = state.info.port;
    entry["status"] = StatusName(state.status);
    entry["fail_count"] = state.fail_count;
    entry["last_check_ms"] = age_ms;
    entry["flows"] = flows;
    backends.PushBack(std::move(entry));
  }
  body["backends"] = std::move(backends);

  request.GetHttpResponse().SetContentType(
      userver::http::content_type::kApplicationJson);
  return userver::formats::json::ToString(body.ExtractValue());
}

}
