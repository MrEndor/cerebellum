#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "common/net_types.hpp"

using cere::common::IPv4Addr;

TEST_CASE("IPv4Addr - round-trips a valid dotted quad") {
  REQUIRE(IPv4Addr::FromString("10.0.1.255").ToString() == "10.0.1.255");
  REQUIRE(IPv4Addr::FromString("0.0.0.0").ToString() == "0.0.0.0");
  REQUIRE(IPv4Addr::FromString("255.255.255.255").ToString() ==
          "255.255.255.255");
}

TEST_CASE("IPv4Addr - rejects malformed input") {
  REQUIRE_THROWS_AS(IPv4Addr::FromString("10.0.0"), std::invalid_argument);
  REQUIRE_THROWS_AS(IPv4Addr::FromString("garbage"), std::invalid_argument);
}

TEST_CASE("IPv4Addr - rejects octet out of range") {
  REQUIRE_THROWS_AS(IPv4Addr::FromString("256.0.0.0"), std::invalid_argument);
  REQUIRE_THROWS_AS(IPv4Addr::FromString("1.2.3.999"), std::invalid_argument);
}
