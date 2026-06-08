#pragma once
#include <cstdint>

struct rte_mempool;

namespace cere::io {

inline constexpr uint16_t kRxRingSize = 1024;
inline constexpr uint16_t kTxRingSize = 1024;
inline constexpr uint16_t kMbufCount = 8192;
inline constexpr uint16_t kMbufCacheSize = 256;

class DpdkPort {
 public:
  DpdkPort(uint16_t port_id, uint16_t n_queues);
  ~DpdkPort();

  DpdkPort(const DpdkPort&) = delete;
  DpdkPort& operator=(const DpdkPort&) = delete;
  DpdkPort(DpdkPort&&) = delete;
  DpdkPort& operator=(DpdkPort&&) = delete;

  uint16_t PortId() const noexcept { return port_id_; }
  rte_mempool* Pool() const noexcept { return pool_; }

 private:
  void Configure(uint16_t n_queues);
  void SetupQueues(uint16_t n_queues);

  uint16_t port_id_;
  rte_mempool* pool_{nullptr};
};

}
