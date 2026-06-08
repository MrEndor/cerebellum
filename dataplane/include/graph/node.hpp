#pragma once
#include <span>
#include <string_view>

#include "graph/frame.hpp"

namespace cere::graph {

class Node {
 public:
  Node() = default;
  virtual ~Node() = default;

  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;
  Node(Node&&) = delete;
  Node& operator=(Node&&) = delete;

  virtual void Process(Frame& in, std::span<Frame> nexts) noexcept = 0;
  virtual std::string_view Name() const noexcept = 0;
  virtual uint16_t NextCount() const noexcept = 0;
};

}
