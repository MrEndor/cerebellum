#pragma once
#include <cstdint>
#include <cstring>

namespace cere::net {

inline constexpr std::size_t kFlowKeySize = 16;
inline constexpr std::size_t kFlowKeyPad = 3;

struct FlowKey {
  uint32_t src_ip{};
  uint32_t dst_ip{};
  uint16_t src_port{};
  uint16_t dst_port{};
  uint8_t proto{};
  uint8_t pad[kFlowKeyPad]{};
};
static_assert(sizeof(FlowKey) == kFlowKeySize);

inline bool operator==(const FlowKey& lhs, const FlowKey& rhs) noexcept {
  return std::memcmp(&lhs, &rhs, sizeof(FlowKey)) == 0;
}

inline uint32_t Hash(const FlowKey& key) noexcept {
#ifdef __SSE4_2__
  static constexpr uint32_t kCrcSeed = 0xDEADBEEF;
  static constexpr std::size_t kHalf = sizeof(FlowKey) / 2;
  uint32_t crc = kCrcSeed;
  uint64_t lo_word{};
  uint64_t hi_word{};
  std::memcpy(&lo_word, reinterpret_cast<const char*>(&key), kHalf);
  std::memcpy(&hi_word, reinterpret_cast<const char*>(&key) + kHalf, kHalf);
  crc = static_cast<uint32_t>(
      __builtin_ia32_crc32di(crc, static_cast<unsigned long long>(lo_word)));
  crc = static_cast<uint32_t>(
      __builtin_ia32_crc32di(crc, static_cast<unsigned long long>(hi_word)));
  return crc;
#else

  static constexpr uint32_t kFnvOffsetBasis = 2166136261U;
  static constexpr uint32_t kFnvPrime = 16777619U;
  const auto* data = reinterpret_cast<const uint8_t*>(&key);
  uint32_t hash = kFnvOffsetBasis;
  for (std::size_t i = 0; i < sizeof(FlowKey); ++i) {
    hash ^= data[i];
    hash *= kFnvPrime;
  }
  return hash;
#endif
}

inline uint32_t SteerWorker(const FlowKey& key, uint32_t n_workers) noexcept {
  return Hash(key) % n_workers;
}

}
