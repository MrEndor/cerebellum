#pragma once
#include <cstdint>

#include "common/config.hpp"

namespace cere::control {

enum class HealthStatus : uint8_t { kUnknown, kUp, kDown, kDraining };

struct BackendState {
  common::BackendInfo info{};
  HealthStatus status{HealthStatus::kUnknown};
  uint32_t fail_count{};
  uint32_t success_count{};
  uint64_t last_check_ns{};
};

}
