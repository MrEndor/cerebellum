#pragma once
#include <cstdint>

namespace cere::ipc {

inline constexpr uint32_t kShmMagic = 0x43455242U;

inline constexpr uint32_t kShmVersion = 1U;

struct ShmHeader {
  uint32_t magic;
  uint32_t version;
};

inline void InitShmHeader(ShmHeader& header) noexcept {
  header.magic = kShmMagic;
  header.version = kShmVersion;
}

inline bool CheckShmHeader(const ShmHeader& header) noexcept {
  return header.magic == kShmMagic && header.version == kShmVersion;
}

}
