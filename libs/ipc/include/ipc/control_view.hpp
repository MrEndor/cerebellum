#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>

#include "lcore_stats.hpp"
#include "shm_header.hpp"

namespace cere::ipc {

inline constexpr uint32_t kMacBytes = 6;

struct ControlBackend {
  uint32_t ip{};
  uint16_t port{};
  uint8_t mac[kMacBytes]{};
  uint8_t enabled{};
};

struct ControlView {
  ShmHeader header;
  std::atomic<uint32_t> seq;
  uint32_t count;
  ControlBackend backends[kMaxBackends];
};

inline void PublishControl(ControlView& view, const ControlBackend* src,
                           uint32_t count) noexcept {
  const uint32_t start = view.seq.load(std::memory_order_relaxed);
  view.seq.store(start + 1, std::memory_order_release);
  std::atomic_thread_fence(std::memory_order_release);

  view.count = std::min(count, kMaxBackends);
  for (uint32_t i = 0; i < view.count; ++i) {
    view.backends[i] = src[i];
  }

  std::atomic_thread_fence(std::memory_order_release);
  view.seq.store(start + 2, std::memory_order_release);
}

inline uint32_t ReadControl(const ControlView& view, ControlBackend* out,
                            uint32_t& out_count) noexcept {
  while (true) {
    const uint32_t seq_before = view.seq.load(std::memory_order_acquire);
    if ((seq_before & 1U) != 0U) {
      continue;
    }
    std::atomic_thread_fence(std::memory_order_acquire);

    const uint32_t count = std::min(view.count, kMaxBackends);
    for (uint32_t i = 0; i < count; ++i) {
      out[i] = view.backends[i];
    }

    std::atomic_thread_fence(std::memory_order_acquire);
    if (view.seq.load(std::memory_order_acquire) == seq_before) {
      out_count = count;
      return seq_before;
    }
  }
}

}
