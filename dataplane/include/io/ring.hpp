#pragma once
#include <rte_lcore.h>
#include <rte_ring.h>

#include <stdexcept>
#include <string>

namespace cere::io {

class Ring {
 public:
  Ring(const std::string& name, unsigned count, unsigned flags) {
    ring_ = rte_ring_create(name.c_str(), count, SOCKET_ID_ANY, flags);
    if (ring_ == nullptr) {
      throw std::runtime_error("rte_ring_create failed: " + name);
    }
  }

  ~Ring() {
    if (ring_ != nullptr) {
      rte_ring_free(ring_);
    }
  }

  Ring(const Ring&) = delete;
  Ring& operator=(const Ring&) = delete;
  Ring(Ring&&) = delete;
  Ring& operator=(Ring&&) = delete;

  rte_ring* Get() const noexcept { return ring_; }

 private:
  rte_ring* ring_{nullptr};
};

}
