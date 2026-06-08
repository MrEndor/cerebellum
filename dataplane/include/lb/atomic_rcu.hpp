#pragma once
#include <atomic>
#include <memory>

namespace cere::lb {

template <typename T>
class AtomicRcu {
 public:
  explicit AtomicRcu(std::shared_ptr<T> initial = nullptr)
      : ptr_(std::move(initial)) {}

  std::shared_ptr<T> Load() const noexcept {
    return ptr_.load(std::memory_order_acquire);
  }

  void Store(std::shared_ptr<T> ptr) noexcept {
    ptr_.store(std::move(ptr), std::memory_order_release);
  }

 private:
  std::atomic<std::shared_ptr<T>> ptr_;
};

}
