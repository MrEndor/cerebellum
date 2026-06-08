#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace cere::common {

struct IPv4Addr {
  static constexpr uint32_t kOctet0Shift = 24U;
  static constexpr uint32_t kOctet1Shift = 16U;
  static constexpr uint32_t kOctet2Shift = 8U;
  static constexpr uint32_t kOctetMask = 0xFFU;
  static constexpr int kOctetCount = 4;

  uint32_t value{};

  static IPv4Addr FromString(std::string_view str);
  std::string ToString() const;

  bool operator==(const IPv4Addr&) const = default;
};

struct MacAddr {
  static constexpr std::size_t kMacLen = 6;
  std::array<uint8_t, kMacLen> bytes{};
  bool operator==(const MacAddr&) const = default;
};

}
