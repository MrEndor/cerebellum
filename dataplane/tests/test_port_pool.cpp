#include <catch2/catch_test_macros.hpp>
#include <set>

#include "lb/port_pool.hpp"

using cere::lb::PortPool;

TEST_CASE("PortPool - hands out distinct ports in range", "[port_pool]") {
  PortPool pool(1024, 1027);
  REQUIRE(pool.Available() == 4U);

  std::set<uint16_t> seen;
  for (int i = 0; i < 4; ++i) {
    const uint16_t port = pool.Acquire();
    REQUIRE(port >= 1024U);
    REQUIRE(port <= 1027U);
    REQUIRE(seen.insert(port).second);
  }
  REQUIRE(pool.Available() == 0U);
}

TEST_CASE("PortPool - returns 0 when exhausted", "[port_pool]") {
  PortPool pool(1024, 1025);
  REQUIRE(pool.Acquire() != 0U);
  REQUIRE(pool.Acquire() != 0U);
  REQUIRE(pool.Acquire() == 0U);
}

TEST_CASE("PortPool - released ports are reusable", "[port_pool]") {
  PortPool pool(1024, 1024);
  const uint16_t port = pool.Acquire();
  REQUIRE(port == 1024U);
  REQUIRE(pool.Acquire() == 0U);

  pool.Release(port);
  REQUIRE(pool.Available() == 1U);
  REQUIRE(pool.Acquire() == 1024U);
}

TEST_CASE("PortPool::ForWorker partitions ports by port % K", "[port_pool]") {
  constexpr uint16_t kWorkers = 4;
  constexpr uint16_t kLow = 1024;
  constexpr uint16_t kHigh = 2047;

  std::set<uint16_t> all;
  for (uint16_t w = 0; w < kWorkers; ++w) {
    PortPool pool = PortPool::ForWorker(w, kWorkers, kLow, kHigh);
    for (uint16_t port = pool.Acquire(); port != 0; port = pool.Acquire()) {
      REQUIRE(port % kWorkers == w);
      REQUIRE(all.insert(port).second);
    }
  }

  REQUIRE(all.size() == static_cast<std::size_t>(kHigh - kLow) + 1U);
}
