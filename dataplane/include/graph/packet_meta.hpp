#pragma once
#include <rte_mbuf.h>

#include <cstdint>

#include "common/net_types.hpp"

namespace cere::graph {

enum class Direction : uint8_t { kForward, kReverse };

struct PacketMeta {
  uint32_t src_ip{};
  uint32_t dst_ip{};
  uint16_t src_port{};
  uint16_t dst_port{};
  uint8_t proto{};
  Direction dir{Direction::kForward};

  common::IPv4Addr backend_ip{};
  common::MacAddr backend_mac{};
  uint16_t backend_port{};

  uint16_t snat_port{};
  common::IPv4Addr client_ip{};
  common::MacAddr client_mac{};
  uint16_t client_port{};
  uint8_t tcp_flags{};
};

inline constexpr uint16_t kPacketMetaPrivSize =
    RTE_ALIGN(sizeof(PacketMeta), RTE_MBUF_PRIV_ALIGN);

inline PacketMeta& Meta(rte_mbuf* mbuf) noexcept {
  return *static_cast<PacketMeta*>(rte_mbuf_to_priv(mbuf));
}

}
