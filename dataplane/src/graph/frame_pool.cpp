#include "graph/frame_pool.hpp"

#include <cstddef>
#include <memory>

namespace cere::graph {

FramePool::FramePool(std::size_t reserve) {
  storage_.reserve(reserve);
  free_.reserve(reserve);
  for (std::size_t i = 0; i < reserve; ++i) {
    storage_.push_back(std::make_unique<Frame>());
    free_.push_back(storage_.back().get());
  }
}

Frame* FramePool::Acquire() {
  if (free_.empty()) {
    storage_.push_back(std::make_unique<Frame>());
    return storage_.back().get();
  }

  Frame* frame = free_.back();
  free_.pop_back();
  frame->Clear();
  return frame;
}

}
