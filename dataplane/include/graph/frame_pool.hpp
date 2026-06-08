#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include "graph/frame.hpp"

namespace cere::graph {

class FramePool {
 public:
  FramePool() = default;

  explicit FramePool(std::size_t reserve);

  Frame* Acquire();

  void Release(Frame* frame) {
    frame->Clear();
    free_.push_back(frame);
  }

 private:
  std::vector<std::unique_ptr<Frame>> storage_;
  std::vector<Frame*> free_;
};

}
