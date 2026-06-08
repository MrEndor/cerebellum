#include <catch2/catch_test_macros.hpp>

#include "lb/atomic_rcu.hpp"
#include "lb/backend_pool.hpp"

using namespace cere::lb;
using cere::common::BackendInfo;
using cere::net::FlowKey;

static BackendInfo MakeBackend(uint32_t ip, uint16_t port) {
  BackendInfo backend;
  backend.ip.value = ip;
  backend.port = port;
  return backend;
}

TEST_CASE("BackendPool - Select is deterministic", "[backend_pool]") {
  BackendPool pool(
      {MakeBackend(1U, 80U), MakeBackend(2U, 80U), MakeBackend(3U, 80U)});
  FlowKey key{.src_ip = 42U,
              .dst_ip = 1U,
              .src_port = 9000U,
              .dst_port = 80U,
              .proto = 6U};
  REQUIRE(pool.Select(key) == pool.Select(key));
  REQUIRE(pool.ActiveCount() == 3U);
}

TEST_CASE("BackendPool - Select distributes load", "[backend_pool]") {
  BackendPool pool(
      {MakeBackend(1U, 80U), MakeBackend(2U, 80U), MakeBackend(3U, 80U)});
  std::array<int, 3> cnt{};
  for (uint16_t port = 1000U; port < 1300U; ++port) {
    FlowKey key{.src_ip = 1U,
                .dst_ip = 2U,
                .src_port = port,
                .dst_port = 80U,
                .proto = 6U};
    cnt[pool.Select(key)]++;
  }
  REQUIRE(cnt[0] > 20);
  REQUIRE(cnt[1] > 20);
  REQUIRE(cnt[2] > 20);
}

TEST_CASE("BackendPool - disabled slots keep stable indices",
          "[backend_pool]") {
  std::vector<BackendInfo> slots{MakeBackend(1U, 80U), MakeBackend(2U, 80U),
                                 MakeBackend(3U, 80U)};
  const std::vector<uint8_t> enabled{1U, 0U, 1U};
  BackendPool pool(slots, enabled);

  REQUIRE(pool.ActiveCount() == 2U);

  for (uint16_t port = 1000U; port < 1300U; ++port) {
    FlowKey key{.src_ip = 1U,
                .dst_ip = 2U,
                .src_port = port,
                .dst_port = 80U,
                .proto = 6U};
    REQUIRE(pool.Select(key) != 1U);
  }

  REQUIRE(pool.At(1U).ip.value == 2U);
  REQUIRE(pool.At(2U).ip.value == 3U);
}

TEST_CASE("AtomicRcu - initial load is nullptr", "[atomic_rcu]") {
  AtomicRcu<BackendPool> rcu;
  REQUIRE(rcu.Load() == nullptr);
}

TEST_CASE("AtomicRcu - store and load", "[atomic_rcu]") {
  AtomicRcu<BackendPool> rcu;
  rcu.Store(std::make_shared<BackendPool>(
      std::vector{MakeBackend(10U, 80U), MakeBackend(11U, 80U)}));
  REQUIRE(rcu.Load()->ActiveCount() == 2U);
}

TEST_CASE("AtomicRcu - swap replaces value", "[atomic_rcu]") {
  AtomicRcu<BackendPool> rcu;
  rcu.Store(std::make_shared<BackendPool>(std::vector{MakeBackend(1U, 80U)}));
  rcu.Store(std::make_shared<BackendPool>(
      std::vector{MakeBackend(2U, 80U), MakeBackend(3U, 80U)}));
  REQUIRE(rcu.Load()->ActiveCount() == 2U);
}
