#pragma once
#include <cassert>
#include <cstddef>
#include <string>

namespace cere::ipc {

inline constexpr int kShmPerms = 0660;

class ShmProvider {
 public:
  enum class Mode { kCreate, kAttach };

  ShmProvider(std::string name, std::size_t size, Mode mode);
  ~ShmProvider();

  ShmProvider(const ShmProvider&) = delete;
  ShmProvider& operator=(const ShmProvider&) = delete;
  ShmProvider(ShmProvider&&) = delete;
  ShmProvider& operator=(ShmProvider&&) = delete;

  template <typename T>
  T* As() noexcept {
    assert(size_ >= sizeof(T));
    return static_cast<T*>(ptr_);
  }

 private:
  std::string name_;
  std::size_t size_;
  Mode mode_;
  int fd_{-1};
  void* ptr_{nullptr};
};

}
