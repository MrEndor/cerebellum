#include "common/net_types.hpp"

#include <cstdio>
#include <format>
#include <stdexcept>

namespace cere::common {

IPv4Addr IPv4Addr::FromString(std::string_view str) {
  const std::string tmp(str);
  unsigned oct0{};
  unsigned oct1{};
  unsigned oct2{};
  unsigned oct3{};
  if (std::sscanf(tmp.c_str(), "%u.%u.%u.%u", &oct0, &oct1, &oct2, &oct3) !=
      kOctetCount) {
    throw std::invalid_argument(std::format("bad IPv4: {}", str));
  }
  if (oct0 > kOctetMask || oct1 > kOctetMask || oct2 > kOctetMask ||
      oct3 > kOctetMask) {
    throw std::invalid_argument(
        std::format("IPv4 octet out of range: {}", str));
  }
  return {(oct0 << kOctet0Shift) | (oct1 << kOctet1Shift) |
          (oct2 << kOctet2Shift) | (oct3 & kOctetMask)};
}

std::string IPv4Addr::ToString() const {
  return std::format("{}.{}.{}.{}", (value >> kOctet0Shift) & kOctetMask,
                     (value >> kOctet1Shift) & kOctetMask,
                     (value >> kOctet2Shift) & kOctetMask, value & kOctetMask);
}

}
