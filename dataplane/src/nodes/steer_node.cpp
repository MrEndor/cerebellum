#include "nodes/steer_node.hpp"

#include <rte_mbuf.h>
#include <rte_ring.h>

#include "lb/steering.hpp"

namespace cere::nodes {

void SteerNode::Process(graph::Frame& in,
                        std::span<graph::Frame> nexts) noexcept {
  for (uint16_t i = 0; i < in.count; ++i) {
    rte_mbuf* mbuf = in.bufs[i];
    const lb::SteerResult res = lb::ClassifyForSteer(mbuf, cfg_, n_workers_);
    switch (res.cls) {
      case lb::SteerClass::kArp:
        in.MoveTo(i, nexts[kNextArp]);
        break;
      case lb::SteerClass::kSteer:
        if (rte_ring_enqueue(worker_rings_[res.worker], mbuf) != 0) {
          rte_pktmbuf_free(mbuf);
          stats_.dropped.fetch_add(1, std::memory_order_relaxed);
        }
        break;
      case lb::SteerClass::kDrop:
        rte_pktmbuf_free(mbuf);
        break;
    }
  }
  in.count = 0;
}

}
