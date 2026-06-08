#pragma once
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

#include "common/config.hpp"
#include "net/flow_key.hpp"

namespace cere::lb {

class BackendPool {
 public:
  explicit BackendPool(std::vector<common::BackendInfo> slots)
      : slots_(std::move(slots)) {
    active_.reserve(slots_.size());
    for (uint8_t i = 0; i < slots_.size(); ++i) {
      active_.push_back(i);
    }
  }

  BackendPool(std::vector<common::BackendInfo> slots,
              const std::vector<uint8_t>& enabled)
      : slots_(std::move(slots)) {
    assert(enabled.size() == slots_.size());
    for (uint8_t i = 0; i < slots_.size(); ++i) {
      if (enabled[i] != 0U) {
        active_.push_back(i);
      }
    }
  }

  uint8_t Select(const net::FlowKey& key) const noexcept {
    assert(!active_.empty());
    return active_[net::Hash(key) % active_.size()];
  }

  const common::BackendInfo& At(uint8_t idx) const noexcept {
    assert(idx < slots_.size());
    return slots_[idx];
  }

  uint16_t ActiveCount() const noexcept {
    return static_cast<uint16_t>(active_.size());
  }

  std::span<const common::BackendInfo> All() const noexcept { return slots_; }

 private:
  std::vector<common::BackendInfo> slots_;
  std::vector<uint8_t> active_;
};

}
