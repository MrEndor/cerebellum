#pragma once
#include <atomic>
#include <cstddef>

namespace cere::ipc {

inline constexpr std::size_t kCacheLineSize = 64;

template <typename T>
struct alignas(kCacheLineSize) CacheAligned : std::atomic<T> {
  using std::atomic<T>::atomic;
  using std::atomic<T>::operator=;
};

}
