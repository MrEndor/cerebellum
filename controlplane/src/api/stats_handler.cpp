#include "api/stats_handler.hpp"

#include <string>
#include <string_view>
#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/http/content_type.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_response.hpp>

#include "monitor/monitor_component.hpp"
#include "stats/stats_aggregator.hpp"

namespace cere::control {

StatsHandler::StatsHandler(const userver::components::ComponentConfig& config,
                           const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      monitor_(context.FindComponent<MonitorComponent>()) {}

std::string StatsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
  const AggregatedStats stats = monitor_.LatestStats();

  userver::formats::json::ValueBuilder body;
  body["dataplane"] = monitor_.DataplaneConnected() ? "connected" : "detached";
  body["rx_pps"] = stats.rx_pps;
  body["tx_pps"] = stats.tx_pps;
  body["dropped_pps"] = stats.dropped_pps;
  body["new_flows_ps"] = stats.new_flows_ps;
  body["active_flows"] = stats.active_flows;

  auto& response = request.GetHttpResponse();
  response.SetContentType(userver::http::content_type::kApplicationJson);

  response.SetHeader(std::string_view{"Cache-Control"},
                     std::string{"max-age=1"});
  return userver::formats::json::ToString(body.ExtractValue());
}

}
