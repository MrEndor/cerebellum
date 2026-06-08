#pragma once
#include <array>
#include <cstdint>

#include "common/net_types.hpp"
#include "net/flow_key.hpp"

namespace cere::lb {

struct NatFlow {
  net::FlowKey fwd_key{};
  common::MacAddr client_mac{};
  uint32_t backend_ip{};
  common::MacAddr backend_mac{};
  uint16_t backend_port{};
  uint16_t snat_port{};
  uint8_t backend_idx{};
  uint32_t last_seen_ms{};
  uint8_t fin_seen{0};
  bool reaped{false};
  bool occupied{false};
};

inline constexpr uint32_t kNatCapacityBits = 20U;
inline constexpr uint32_t kNatDefaultCapacity = 1U << kNatCapacityBits;

template <uint32_t kCapacity = kNatDefaultCapacity>
class NatTable {
  static_assert((kCapacity & (kCapacity - 1U)) == 0U,
                "kCapacity must be a power of 2");

 public:
  static constexpr uint32_t kDefaultTimeoutMs = 30'000U;

  explicit NatTable(uint32_t timeout_ms = kDefaultTimeoutMs)
      : timeout_ms_(timeout_ms) {}

  void SetTimeoutMs(uint32_t timeout_ms) noexcept { timeout_ms_ = timeout_ms; }
  uint32_t Size() const noexcept { return size_; }

  NatFlow* LookupForward(const net::FlowKey& key, uint32_t now_ms) noexcept;

  NatFlow* Insert(const NatFlow& flow, uint32_t now_ms, uint16_t& evicted_port,
                  uint8_t& evicted_backend_idx) noexcept;

  NatFlow* LookupReverse(uint32_t backend_ip, uint16_t backend_port,
                         uint16_t snat_port, uint8_t proto,
                         uint32_t now_ms) noexcept;

  void Close(NatFlow& flow, uint16_t& released_port,
             uint8_t& backend_idx) noexcept {
    released_port = 0U;
    backend_idx = 0U;
    if (!flow.occupied || flow.reaped) {
      return;
    }
    Reap(flow);
    released_port = flow.snat_port;
    backend_idx = flow.backend_idx;
    flow.snat_port = 0U;
  }

 private:
  static constexpr uint32_t kMask = kCapacity - 1U;

  bool Expired(const NatFlow& flow, uint32_t now_ms) const noexcept {
    return flow.occupied &&
           (flow.reaped || (now_ms - flow.last_seen_ms) > timeout_ms_);
  }

  void Reap(NatFlow& flow) noexcept {
    if (!flow.reaped) {
      flow.reaped = true;
      if (size_ > 0U) {
        --size_;
      }
    }
  }

  static uint32_t RevHash(uint32_t backend_ip, uint16_t backend_port,
                          uint16_t snat_port, uint8_t proto) noexcept {
    const net::FlowKey key{backend_ip, 0U, backend_port, snat_port, proto};
    return net::Hash(key);
  }

  void IndexReverse(uint32_t slot, uint32_t now_ms) noexcept;

  std::array<NatFlow, kCapacity> slots_{};
  std::array<uint32_t, kCapacity> rev_{};
  uint32_t size_{0};
  uint32_t timeout_ms_;
};

template <uint32_t kCapacity>
NatFlow* NatTable<kCapacity>::LookupForward(const net::FlowKey& key,
                                            uint32_t now_ms) noexcept {
  const uint32_t start = net::Hash(key) & kMask;
  for (uint32_t i = 0; i < kCapacity; ++i) {
    NatFlow& flow = slots_[(start + i) & kMask];
    if (!flow.occupied) {
      return nullptr;
    }
    if (Expired(flow, now_ms)) {
      Reap(flow);
      continue;
    }
    if (flow.fwd_key == key) {
      flow.last_seen_ms = now_ms;
      return &flow;
    }
  }
  return nullptr;
}

template <uint32_t kCapacity>
NatFlow* NatTable<kCapacity>::Insert(const NatFlow& flow, uint32_t now_ms,
                                     uint16_t& evicted_port,
                                     uint8_t& evicted_backend_idx) noexcept {
  evicted_port = 0U;
  evicted_backend_idx = 0U;
  const uint32_t start = net::Hash(flow.fwd_key) & kMask;
  for (uint32_t i = 0; i < kCapacity; ++i) {
    const uint32_t slot = (start + i) & kMask;
    NatFlow& victim = slots_[slot];
    if (!victim.occupied) {
      ++size_;
    } else if (Expired(victim, now_ms)) {
      evicted_port = victim.snat_port;
      evicted_backend_idx = victim.backend_idx;

      if (victim.reaped) {
        ++size_;
      }
    } else {
      continue;
    }
    victim = flow;
    victim.occupied = true;
    victim.last_seen_ms = now_ms;
    IndexReverse(slot, now_ms);
    return &victim;
  }
  return nullptr;
}

template <uint32_t kCapacity>
void NatTable<kCapacity>::IndexReverse(uint32_t slot,
                                       uint32_t now_ms) noexcept {
  const NatFlow& flow = slots_[slot];
  const uint32_t start = RevHash(flow.backend_ip, flow.backend_port,
                                 flow.snat_port, flow.fwd_key.proto) &
                         kMask;
  for (uint32_t i = 0; i < kCapacity; ++i) {
    const uint32_t cell = (start + i) & kMask;
    const uint32_t ptr = rev_[cell];

    if (ptr == 0U || !slots_[ptr - 1U].occupied ||
        Expired(slots_[ptr - 1U], now_ms)) {
      rev_[cell] = slot + 1U;
      return;
    }
  }
}

template <uint32_t kCapacity>
NatFlow* NatTable<kCapacity>::LookupReverse(uint32_t backend_ip,
                                            uint16_t backend_port,
                                            uint16_t snat_port, uint8_t proto,
                                            uint32_t now_ms) noexcept {
  const uint32_t start =
      RevHash(backend_ip, backend_port, snat_port, proto) & kMask;
  for (uint32_t i = 0; i < kCapacity; ++i) {
    const uint32_t ptr = rev_[(start + i) & kMask];
    if (ptr == 0U) {
      return nullptr;
    }
    NatFlow& flow = slots_[ptr - 1U];
    if (flow.occupied && Expired(flow, now_ms)) {
      Reap(flow);
      continue;
    }
    if (flow.occupied && flow.backend_ip == backend_ip &&
        flow.backend_port == backend_port && flow.snat_port == snat_port &&
        flow.fwd_key.proto == proto) {
      flow.last_seen_ms = now_ms;
      return &flow;
    }
  }
  return nullptr;
}

}
