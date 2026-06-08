#pragma once
#include <vector>

#include "common/config.hpp"
#include "graph/node.hpp"
#include "graph/packet_meta.hpp"
#include "ipc/lcore_stats.hpp"
#include "lb/atomic_rcu.hpp"
#include "lb/backend_pool.hpp"
#include "lb/nat_table.hpp"
#include "lb/port_pool.hpp"
#include "net/flow_key.hpp"

namespace cere::nodes {

class LbNode : public graph::Node {
 public:
  static constexpr uint16_t kNextDnat = 0;
  static constexpr uint16_t kNextDrop = 1;

  LbNode(const common::Config& cfg, lb::AtomicRcu<lb::BackendPool>& pool_rcu,
         ipc::LcoreStats& stats, uint16_t worker_id, uint16_t n_workers)
      : pool_rcu_(pool_rcu), stats_(stats), nat_(cfg.conntrack_timeout_ms) {
    port_pools_.reserve(ipc::kMaxBackends);
    for (uint32_t i = 0; i < ipc::kMaxBackends; ++i) {
      port_pools_.push_back(lb::PortPool::ForWorker(worker_id, n_workers));
    }
  }

  void Process(graph::Frame& in,
               std::span<graph::Frame> nexts) noexcept override;

  std::string_view Name() const noexcept override { return "lb"; }
  uint16_t NextCount() const noexcept override { return 2; }

 private:
  static constexpr uint64_t kMsPerSec = 1000;
  static constexpr uint8_t kProtoTcp = 6;

  static constexpr uint8_t kTcpFin = 0x01;
  static constexpr uint8_t kTcpSyn = 0x02;
  static constexpr uint8_t kTcpRst = 0x04;
  static constexpr uint8_t kFinForward = 0x1;
  static constexpr uint8_t kFinReverse = 0x2;

  static uint32_t NowMs() noexcept;

  bool HandlePacket(graph::PacketMeta& meta, const lb::BackendPool& pool,
                    uint32_t now_ms) noexcept;

  lb::NatFlow* CreateFlow(const graph::PacketMeta& meta,
                          const net::FlowKey& key, const lb::BackendPool& pool,
                          uint32_t now_ms) noexcept;

  void MaybeTeardown(lb::NatFlow& flow, graph::Direction dir,
                     uint8_t tcp_flags) noexcept;

  lb::AtomicRcu<lb::BackendPool>& pool_rcu_;
  ipc::LcoreStats& stats_;
  lb::NatTable<> nat_;

  std::vector<lb::PortPool> port_pools_;
};

}
