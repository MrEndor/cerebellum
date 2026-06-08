#include <catch2/catch_test_macros.hpp>

#include "lb/nat_table.hpp"

using cere::lb::NatFlow;
using cere::lb::NatTable;
using cere::net::FlowKey;

namespace {

constexpr uint32_t kVip = 0x0A000001;
constexpr uint16_t kVipPort = 80;
constexpr uint32_t kClient = 0xC0A80005;
constexpr uint32_t kBackend = 0x0A000101;
constexpr uint16_t kBackendPort = 8080;
constexpr uint8_t kTcp = 6;

NatFlow MakeFlow(uint16_t client_port, uint16_t snat_port) {
  NatFlow flow;
  flow.fwd_key = FlowKey{kClient, kVip, client_port, kVipPort, kTcp};
  flow.backend_ip = kBackend;
  flow.backend_port = kBackendPort;
  flow.snat_port = snat_port;
  flow.backend_idx = 1;
  return flow;
}

}

TEST_CASE("NatTable - insert then forward lookup", "[nat_table]") {
  NatTable<1024> nat;
  uint16_t evicted = 7;
  uint8_t evicted_idx = 9;
  const FlowKey key{kClient, kVip, 40000, kVipPort, kTcp};

  REQUIRE(nat.LookupForward(key, 1000) == nullptr);
  NatFlow* inserted =
      nat.Insert(MakeFlow(40000, 50000), 1000, evicted, evicted_idx);
  REQUIRE(inserted != nullptr);
  REQUIRE(evicted == 0U);
  REQUIRE(nat.Size() == 1U);

  NatFlow* found = nat.LookupForward(key, 1001);
  REQUIRE(found == inserted);
  REQUIRE(found->snat_port == 50000U);
  REQUIRE(found->backend_idx == 1U);
}

TEST_CASE("NatTable - reverse lookup recovers the flow", "[nat_table]") {
  NatTable<1024> nat;
  uint16_t evicted = 0;
  uint8_t evicted_idx = 0;
  nat.Insert(MakeFlow(40000, 50000), 1000, evicted, evicted_idx);

  NatFlow* found = nat.LookupReverse(kBackend, kBackendPort, 50000, kTcp, 1002);
  REQUIRE(found != nullptr);
  REQUIRE(found->fwd_key.src_ip == kClient);
  REQUIRE(found->fwd_key.src_port == 40000U);

  REQUIRE(nat.LookupReverse(kBackend, kBackendPort, 50001, kTcp, 1002) ==
          nullptr);
}

TEST_CASE("NatTable - expiry frees and recycles the port", "[nat_table]") {
  NatTable<1024> nat(1000);
  uint16_t evicted = 0;
  uint8_t evicted_idx = 0;
  const FlowKey key{kClient, kVip, 40000, kVipPort, kTcp};
  nat.Insert(MakeFlow(40000, 50000), 1000, evicted, evicted_idx);

  REQUIRE(nat.LookupForward(key, 5000) == nullptr);

  REQUIRE(nat.Size() == 0U);
  REQUIRE(nat.LookupReverse(kBackend, kBackendPort, 50000, kTcp, 5000) ==
          nullptr);

  nat.Insert(MakeFlow(40000, 51000), 5000, evicted, evicted_idx);
  REQUIRE(evicted == 50000U);
  REQUIRE(evicted_idx == 1U);
  REQUIRE(nat.Size() == 1U);
}

TEST_CASE("NatTable - close releases the port and frees the flow",
          "[nat_table]") {
  NatTable<1024> nat;
  uint16_t evicted = 0;
  uint8_t evicted_idx = 0;
  const FlowKey key{kClient, kVip, 40000, kVipPort, kTcp};
  NatFlow* flow =
      nat.Insert(MakeFlow(40000, 50000), 1000, evicted, evicted_idx);
  REQUIRE(flow != nullptr);
  REQUIRE(nat.Size() == 1U);

  uint16_t released = 0;
  uint8_t backend_idx = 0;
  nat.Close(*flow, released, backend_idx);
  REQUIRE(released == 50000U);
  REQUIRE(backend_idx == 1U);
  REQUIRE(nat.Size() == 0U);

  REQUIRE(nat.LookupForward(key, 1001) == nullptr);
  REQUIRE(nat.LookupReverse(kBackend, kBackendPort, 50000, kTcp, 1001) ==
          nullptr);

  uint16_t released_again = 7;
  uint8_t idx_again = 9;
  nat.Close(*flow, released_again, idx_again);
  REQUIRE(released_again == 0U);

  nat.Insert(MakeFlow(40000, 52000), 1002, evicted, evicted_idx);
  REQUIRE(evicted == 0U);
}
