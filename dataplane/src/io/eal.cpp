#include "io/eal.hpp"

#include <stdexcept>
#include <vector>

namespace cere::io {

Eal::Eal(std::span<std::string> args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (std::string& arg : args) {
    argv.push_back(arg.data());
  }
  if (rte_eal_init(static_cast<int>(argv.size()), argv.data()) < 0) {
    throw std::runtime_error("rte_eal_init failed");
  }
}

}
