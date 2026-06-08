#pragma once
#include <cstdint>

#include "cache_aligned.hpp"
#include "shm_header.hpp"

namespace cere::ipc {

inline constexpr uint32_t kMaxLcores = 32;
inline constexpr uint32_t kMaxBackends = 64;

struct LcoreStats {
  CacheAligned<uint64_t> rx_packets;
  CacheAligned<uint64_t> tx_packets;
  CacheAligned<uint64_t> dropped;
  CacheAligned<uint64_t> new_flows;
  CacheAligned<uint64_t> active_flows;
  CacheAligned<uint64_t> backend_flows[kMaxBackends];
};

struct StatsView {
  ShmHeader header;
  LcoreStats lcores[kMaxLcores];
};

}
