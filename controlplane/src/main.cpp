#include <userver/clients/dns/component.hpp>
#include <userver/clients/http/component_list.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/utils/daemon_run.hpp>

#include "api/backends_handler.hpp"
#include "api/stats_handler.hpp"
#include "monitor/monitor_component.hpp"

int main(int argc, char** argv) {
  const auto component_list =
      userver::components::MinimalServerComponentList()
          .Append<cere::control::MonitorComponent>()
          .Append<cere::control::StatsHandler>()
          .Append<cere::control::BackendsHandler>()
          .Append<userver::clients::dns::Component>()
          .AppendComponentList(userver::clients::http::ComponentList());
  return userver::utils::DaemonMain(argc, argv, component_list);
}
