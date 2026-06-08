#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cere::lb {

inline constexpr uint16_t kEphemeralLow = 1024;
inline constexpr uint16_t kEphemeralHigh = 65535;

class PortPool {
 public:
  explicit PortPool(uint16_t low = kEphemeralLow,
                    uint16_t high = kEphemeralHigh) {
    free_.reserve(static_cast<std::size_t>(high - low) + 1U);
    for (uint32_t port = high; port >= low; --port) {
      free_.push_back(static_cast<uint16_t>(port));
    }
  }

  static PortPool ForWorker(uint16_t worker_id, uint16_t n_workers,
                            uint16_t low = kEphemeralLow,
                            uint16_t high = kEphemeralHigh) {
    PortPool pool{EmptyTag{}};
    pool.free_.reserve((static_cast<std::size_t>(high - low) / n_workers) + 1U);
    for (uint32_t port = high; port >= low; --port) {
      if (port % n_workers == worker_id) {
        pool.free_.push_back(static_cast<uint16_t>(port));
      }
    }
    return pool;
  }

  uint16_t Acquire() noexcept {
    if (free_.empty()) {
      return 0;
    }
    const uint16_t port = free_.back();
    free_.pop_back();
    return port;
  }

  void Release(uint16_t port) noexcept { free_.push_back(port); }

  std::size_t Available() const noexcept { return free_.size(); }

 private:
  struct EmptyTag {};
  explicit PortPool([[maybe_unused]] EmptyTag tag) noexcept {}

  std::vector<uint16_t> free_;
};

}
