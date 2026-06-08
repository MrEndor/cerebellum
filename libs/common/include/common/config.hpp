#pragma once
#include <cstdint>
#include <vector>

#include "net_types.hpp"

namespace cere::common {

struct BackendInfo {
  IPv4Addr ip{};
  MacAddr mac{};
  uint16_t port{};
  uint16_t probe_port{};
};

struct Config {
  static constexpr uint32_t kDefaultHealthIntervalMs = 3000;
  static constexpr uint32_t kDefaultConntrackTimeoutMs = 30000;

  IPv4Addr vip{};
  uint16_t vip_port{};
  IPv4Addr self_ip{};
  MacAddr self_mac{};
  std::vector<BackendInfo> backends;
  uint32_t health_interval_ms{kDefaultHealthIntervalMs};
  uint32_t conntrack_timeout_ms{kDefaultConntrackTimeoutMs};
};

}
