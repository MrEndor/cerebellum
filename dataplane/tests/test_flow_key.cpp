#include <catch2/catch_test_macros.hpp>

#include "net/flow_key.hpp"

using cere::net::FlowKey;
using cere::net::Hash;

TEST_CASE("FlowKey size is 16 bytes", "[flow_key]") {
  REQUIRE(sizeof(FlowKey) == 16);
}

TEST_CASE("FlowKey equality", "[flow_key]") {
  FlowKey a{
      .src_ip = 1, .dst_ip = 2, .src_port = 100, .dst_port = 80, .proto = 6};
  FlowKey b{
      .src_ip = 1, .dst_ip = 2, .src_port = 100, .dst_port = 80, .proto = 6};
  FlowKey c{
      .src_ip = 1, .dst_ip = 2, .src_port = 101, .dst_port = 80, .proto = 6};
  REQUIRE(a == b);
  REQUIRE(!(a == c));
}

TEST_CASE("FlowKey hash is deterministic", "[flow_key]") {
  FlowKey k{.src_ip = 0x01020304,
            .dst_ip = 0x0a000001,
            .src_port = 12345,
            .dst_port = 80,
            .proto = 6};
  REQUIRE(Hash(k) == Hash(k));
}

TEST_CASE("FlowKey different keys produce different hashes", "[flow_key]") {
  FlowKey a{
      .src_ip = 1, .dst_ip = 2, .src_port = 100, .dst_port = 80, .proto = 6};
  FlowKey b{
      .src_ip = 1, .dst_ip = 2, .src_port = 101, .dst_port = 80, .proto = 6};
  REQUIRE(Hash(a) != Hash(b));
}

TEST_CASE("FlowKey padding bytes are zero", "[flow_key]") {
  FlowKey k{.src_ip = 1, .proto = 17};
  const auto* raw = reinterpret_cast<const uint8_t*>(&k);
  REQUIRE(raw[13] == 0);
  REQUIRE(raw[14] == 0);
  REQUIRE(raw[15] == 0);
}
