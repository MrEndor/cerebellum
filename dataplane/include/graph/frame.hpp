#pragma once
#include <array>
#include <cassert>
#include <cstdint>

struct rte_mbuf;

namespace cere::graph {

inline constexpr uint16_t kBurstSize = 64;

struct Frame {
  std::array<rte_mbuf*, kBurstSize> bufs{};
  uint16_t count{0};

  void Clear() noexcept { count = 0; }
  bool Full() const noexcept { return count == kBurstSize; }

  void Push(rte_mbuf* mbuf) noexcept {
    assert(count < kBurstSize);
    bufs[count++] = mbuf;
  }

  void MoveTo(uint16_t idx, Frame& dst) noexcept { dst.Push(bufs[idx]); }
};

}
