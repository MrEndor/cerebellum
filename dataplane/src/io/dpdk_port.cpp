#include "io/dpdk_port.hpp"

#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include <format>
#include <stdexcept>

#include "graph/packet_meta.hpp"

namespace cere::io {

DpdkPort::DpdkPort(uint16_t port_id, uint16_t n_queues) : port_id_(port_id) {
  Configure(n_queues);

  pool_ = rte_pktmbuf_pool_create(
      std::format("pool_{}", port_id_).c_str(), kMbufCount, kMbufCacheSize,
      graph::kPacketMetaPrivSize, RTE_MBUF_DEFAULT_BUF_SIZE,
      static_cast<int>(rte_socket_id()));
  if (pool_ == nullptr) {
    throw std::runtime_error("mbuf pool create failed");
  }

  SetupQueues(n_queues);

  if (rte_eth_dev_start(port_id_) < 0) {
    throw std::runtime_error(std::format("port {} start failed", port_id_));
  }
  rte_eth_promiscuous_enable(port_id_);
}

DpdkPort::~DpdkPort() {
  rte_eth_dev_stop(port_id_);
  rte_eth_dev_close(port_id_);
  if (pool_ != nullptr) {
    rte_mempool_free(pool_);
  }
}

void DpdkPort::Configure(uint16_t n_queues) {
  rte_eth_dev_info dev_info{};
  if (rte_eth_dev_info_get(port_id_, &dev_info) < 0) {
    throw std::runtime_error(std::format("port {} info get failed", port_id_));
  }

  rte_eth_conf conf{};

  const uint64_t rss_hf = (RTE_ETH_RSS_IP | RTE_ETH_RSS_TCP | RTE_ETH_RSS_UDP) &
                          dev_info.flow_type_rss_offloads;
  if (n_queues > 1 && rss_hf != 0) {
    conf.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
    conf.rx_adv_conf.rss_conf.rss_hf = rss_hf;
  }

  if (rte_eth_dev_configure(port_id_, n_queues, n_queues, &conf) < 0) {
    throw std::runtime_error(std::format("port {} configure failed", port_id_));
  }
}

void DpdkPort::SetupQueues(uint16_t n_queues) {
  const auto socket = static_cast<unsigned>(rte_eth_dev_socket_id(port_id_));
  for (uint16_t queue = 0; queue < n_queues; ++queue) {
    if (rte_eth_rx_queue_setup(port_id_, queue, kRxRingSize, socket, nullptr,
                               pool_) < 0) {
      throw std::runtime_error(std::format("rx queue {} setup failed", queue));
    }
    if (rte_eth_tx_queue_setup(port_id_, queue, kTxRingSize, socket, nullptr) <
        0) {
      throw std::runtime_error(std::format("tx queue {} setup failed", queue));
    }
  }
}

}
