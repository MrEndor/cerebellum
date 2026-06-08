#pragma once
#include <rte_mbuf.h>

#include "graph/node.hpp"

namespace cere::nodes {

class DropNode : public graph::Node {
 public:
  void Process(graph::Frame& in, std::span<graph::Frame>) noexcept override {
    rte_pktmbuf_free_bulk(in.bufs.data(), in.count);
    in.count = 0;
  }
  std::string_view Name() const noexcept override { return "drop"; }
  uint16_t NextCount() const noexcept override { return 0; }
};

}
