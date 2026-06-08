#pragma once
#include <rte_eal.h>

#include <span>
#include <string>

namespace cere::io {

class Eal {
 public:
  explicit Eal(std::span<std::string> args);

  ~Eal() { rte_eal_cleanup(); }

  Eal(const Eal&) = delete;
  Eal& operator=(const Eal&) = delete;
  Eal(Eal&&) = delete;
  Eal& operator=(Eal&&) = delete;
};

}
