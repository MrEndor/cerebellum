#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

#include "ipc/control_view.hpp"
#include "ipc/shm_header.hpp"

using cere::ipc::ControlBackend;
using cere::ipc::ControlView;
using cere::ipc::kMaxBackends;
using cere::ipc::PublishControl;
using cere::ipc::ReadControl;

TEST_CASE("ControlView - publish then read round-trips", "[control_view]") {
  ControlView view{};
  std::vector<ControlBackend> in(2);
  in[0].ip = 0x0A000101U;
  in[0].port = 80U;
  in[0].enabled = 1U;
  in[0].mac[0] = 0xAAU;
  in[1].ip = 0x0A000102U;
  in[1].port = 8080U;
  in[1].enabled = 0U;
  in[1].mac[5] = 0x99U;

  PublishControl(view, in.data(), 2U);

  std::vector<ControlBackend> out(kMaxBackends);
  uint32_t count = 0;
  const uint32_t seq = ReadControl(view, out.data(), count);

  REQUIRE(count == 2U);
  REQUIRE((seq & 1U) == 0U);
  REQUIRE(out[0].ip == 0x0A000101U);
  REQUIRE(out[0].port == 80U);
  REQUIRE(out[0].enabled == 1U);
  REQUIRE(out[0].mac[0] == 0xAAU);
  REQUIRE(out[1].enabled == 0U);
  REQUIRE(out[1].mac[5] == 0x99U);
}

TEST_CASE("ControlView - seq advances on each publish", "[control_view]") {
  ControlView view{};
  std::vector<ControlBackend> in(1);
  std::vector<ControlBackend> out(kMaxBackends);
  uint32_t count = 0;

  PublishControl(view, in.data(), 1U);
  const uint32_t first = ReadControl(view, out.data(), count);
  PublishControl(view, in.data(), 1U);
  const uint32_t second = ReadControl(view, out.data(), count);

  REQUIRE(second != first);
}

TEST_CASE("ControlView - count is clamped to kMaxBackends", "[control_view]") {
  ControlView view{};
  std::vector<ControlBackend> in(kMaxBackends);
  std::vector<ControlBackend> out(kMaxBackends);
  uint32_t count = 0;

  PublishControl(view, in.data(), kMaxBackends + 10U);
  ReadControl(view, out.data(), count);

  REQUIRE(count == kMaxBackends);
}

TEST_CASE("ShmHeader - init passes its own check", "[shm_header]") {
  cere::ipc::ShmHeader header{};
  REQUIRE_FALSE(cere::ipc::CheckShmHeader(header));
  cere::ipc::InitShmHeader(header);
  REQUIRE(cere::ipc::CheckShmHeader(header));
}

TEST_CASE("ShmHeader - rejects bad magic or version", "[shm_header]") {
  cere::ipc::ShmHeader header{};
  cere::ipc::InitShmHeader(header);

  cere::ipc::ShmHeader bad_magic = header;
  bad_magic.magic ^= 0x1U;
  REQUIRE_FALSE(cere::ipc::CheckShmHeader(bad_magic));

  cere::ipc::ShmHeader bad_version = header;
  bad_version.version += 1U;
  REQUIRE_FALSE(cere::ipc::CheckShmHeader(bad_version));
}
