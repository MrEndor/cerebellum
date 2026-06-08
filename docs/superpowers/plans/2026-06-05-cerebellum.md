# Cerebellum Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a userspace stateful L4 load balancer (DPDK + VPP-style node graph) with a React monitoring dashboard.

**Architecture:** DPDK RSS steers each 5-tuple to one lcore; that lcore runs an independent Graph (RxNode → Ip4ParseNode → LbNode → DnatNode → EtherRewriteNode → TxNode) over Frame batches of 64 packets. Controlplane reads per-lcore stats from shared memory and serves a REST API. React polls the API every second.

**Tech Stack:** C++23 · DPDK (pkg-config) · Catch2 · Crow · yaml-cpp · fmt · React 18 · Tailwind CSS · Vite · nginx · Docker Compose

---

## Task 1: Repo cleanup + root CMake

**Files:**
- Delete: `backend/` (entire directory — old fat-tree simulator)
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `justfile`
- Create: `cmake/CompilerWarnings.cmake`
- Create: `cmake/ProjectOptions.cmake`
- Create: `cmake/Sanitizers.cmake`
- Create: `cmake/Coverage.cmake`
- Create: `cmake/Dependencies.cmake`
- Copy:   `cmake/CPM.cmake` (from `backend/cmake/CPM.cmake` before deleting)

- [ ] **Copy CPM.cmake before deleting old backend**

```bash
cp backend/cmake/CPM.cmake /tmp/CPM.cmake
```

- [ ] **Delete old backend, create new structure**

```bash
rm -rf backend
mkdir -p cmake dataplane/include dataplane/src dataplane/tests \
         controlplane/include controlplane/src \
         libs/ipc/include/ipc libs/common/include/common \
         ui nginx
cp /tmp/CPM.cmake cmake/CPM.cmake
```

- [ ] **Write `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.25)
project(cerebellum LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)

include(cmake/ProjectOptions.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Coverage.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(libs)
add_subdirectory(dataplane)
add_subdirectory(controlplane)
```

- [ ] **Write `cmake/ProjectOptions.cmake`**

```cmake
include_guard(GLOBAL)

add_library(cerebellum_project_options INTERFACE)
target_compile_features(cerebellum_project_options INTERFACE cxx_std_23)
set_target_properties(cerebellum_project_options
    PROPERTIES INTERFACE_CXX_EXTENSIONS OFF)

option(CEREBELLUM_BUILD_TESTS       "Build unit tests"               OFF)
option(CEREBELLUM_ENABLE_SANITIZERS "Enable ASAN + UBSAN"            OFF)
option(CEREBELLUM_ENABLE_COVERAGE   "Instrument for coverage"        OFF)
```

- [ ] **Write `cmake/CompilerWarnings.cmake`**

```cmake
include_guard(GLOBAL)

add_library(cerebellum_compiler_warnings INTERFACE)
target_compile_options(cerebellum_compiler_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual -Wnull-dereference
        -Wdouble-promotion -Wformat=2 -Werror>)
```

- [ ] **Write `cmake/Sanitizers.cmake`**

```cmake
include_guard(GLOBAL)

function(cerebellum_enable_sanitizers target)
    if(NOT CEREBELLUM_ENABLE_SANITIZERS)
        return()
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(WARNING "Sanitizers require GCC/Clang; skipping")
        return()
    endif()
    set(flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_compile_options(${target} PRIVATE ${flags})
    target_link_options(${target} PRIVATE ${flags})
endfunction()
```

- [ ] **Write `cmake/Coverage.cmake`**

```cmake
include_guard(GLOBAL)

function(cerebellum_enable_coverage target)
    if(NOT CEREBELLUM_ENABLE_COVERAGE)
        return()
    endif()
    target_compile_options(${target} PRIVATE --coverage -O0 -g)
    target_link_options(${target} PRIVATE --coverage)
endfunction()

if(CEREBELLUM_ENABLE_COVERAGE)
    find_program(GCOVR gcovr)
    if(GCOVR)
        add_custom_target(coverage_report
            COMMAND ${GCOVR}
                --root "${CMAKE_SOURCE_DIR}"
                --filter "${CMAKE_SOURCE_DIR}/dataplane/"
                --filter "${CMAKE_SOURCE_DIR}/controlplane/"
                --html-details "${CMAKE_BINARY_DIR}/coverage/index.html"
                --print-summary --fail-under-line 70
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Generating coverage report")
    endif()
endif()
```

- [ ] **Write `cmake/Dependencies.cmake`**

```cmake
include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CPM.cmake")

find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

CPMAddPackage(NAME fmt      GITHUB_REPOSITORY fmtlib/fmt      GIT_TAG 11.0.2)
CPMAddPackage(NAME yaml-cpp GITHUB_REPOSITORY jbeder/yaml-cpp GIT_TAG 0.8.0
    OPTIONS "YAML_CPP_BUILD_TESTS OFF" "YAML_CPP_BUILD_TOOLS OFF")
CPMAddPackage(NAME Crow     GITHUB_REPOSITORY CrowCpp/Crow    GIT_TAG v1.2.0)

if(CEREBELLUM_BUILD_TESTS)
    CPMAddPackage(NAME Catch2 GITHUB_REPOSITORY catchorg/Catch2 GIT_TAG v3.6.0
        OPTIONS "CATCH_INSTALL_DOCS OFF")
endif()
```

- [ ] **Write `CMakePresets.json`**

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "release",
      "displayName": "Release",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "debug-asan",
      "displayName": "Debug + Tests + Sanitizers",
      "binaryDir": "${sourceDir}/build-test",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
        "CEREBELLUM_BUILD_TESTS": "ON",
        "CEREBELLUM_ENABLE_SANITIZERS": "ON"
      }
    },
    {
      "name": "coverage",
      "displayName": "Coverage",
      "binaryDir": "${sourceDir}/build-cov",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CEREBELLUM_BUILD_TESTS": "ON",
        "CEREBELLUM_ENABLE_COVERAGE": "ON"
      }
    }
  ],
  "buildPresets": [
    { "name": "release",    "configurePreset": "release",    "jobs": 0 },
    { "name": "debug-asan", "configurePreset": "debug-asan", "jobs": 0 },
    { "name": "coverage",   "configurePreset": "coverage",   "jobs": 0 }
  ],
  "testPresets": [
    {
      "name": "debug-asan",
      "configurePreset": "debug-asan",
      "output": { "outputOnFailure": true },
      "environment": { "ASAN_OPTIONS": "detect_leaks=0" }
    }
  ],
  "workflowPresets": [
    {
      "name": "ci",
      "steps": [
        { "type": "configure", "name": "debug-asan" },
        { "type": "build",     "name": "debug-asan" },
        { "type": "test",      "name": "debug-asan" }
      ]
    }
  ]
}
```

- [ ] **Write `justfile`**

```just
set shell := ["bash", "-eou", "pipefail", "-c"]

default:
    @just --list

build:
    cmake --preset release
    cmake --build --preset release --parallel

test:
    cmake --preset debug-asan
    cmake --build --preset debug-asan --parallel
    ctest --preset debug-asan

coverage:
    cmake --preset coverage
    cmake --build --preset coverage --parallel
    ctest --preset coverage
    cmake --build --preset coverage --target coverage_report

fmt:
    find dataplane controlplane libs -name '*.hpp' -o -name '*.cpp' \
        | xargs --no-run-if-empty clang-format -i

fmt-check:
    find dataplane controlplane libs -name '*.hpp' -o -name '*.cpp' \
        | xargs --no-run-if-empty clang-format --dry-run --Werror

up:
    docker compose up --build

down:
    docker compose down

clean:
    rm -rf build build-test build-cov
```

- [ ] **Write stub CMakeLists for each subdirectory**

```bash
printf 'add_subdirectory(ipc)\nadd_subdirectory(common)\n' > libs/CMakeLists.txt
printf '# dataplane\n'    > dataplane/CMakeLists.txt
printf '# controlplane\n' > controlplane/CMakeLists.txt
touch libs/ipc/CMakeLists.txt libs/common/CMakeLists.txt
```

- [ ] **Verify CMake configures without errors**

```bash
cmake --preset release 2>&1 | grep -E "^CMake Error" | head -5
```

Expected: no output (no errors). DPDK found via pkg-config.

- [ ] **Commit**

```bash
git add CMakeLists.txt CMakePresets.json justfile cmake/ libs/ dataplane/ controlplane/
git commit -m "feat: cmake scaffolding for cerebellum"
```

---

## Task 2: libs/ipc — CacheAligned, LcoreStats, ShmProvider

**Files:**
- Create: `libs/ipc/include/ipc/cache_aligned.hpp`
- Create: `libs/ipc/include/ipc/lcore_stats.hpp`
- Create: `libs/ipc/include/ipc/shm_provider.hpp`
- Modify: `libs/ipc/CMakeLists.txt`

- [ ] **Write `libs/ipc/include/ipc/cache_aligned.hpp`**

```cpp
#pragma once
#include <atomic>
#include <new>

namespace cere::ipc {

template <typename T>
struct alignas(std::hardware_destructive_interference_size) CacheAligned {
  std::atomic<T> value{};
};

}  // namespace cere::ipc
```

- [ ] **Write `libs/ipc/include/ipc/lcore_stats.hpp`**

```cpp
#pragma once
#include "cache_aligned.hpp"
#include <cstdint>

namespace cere::ipc {

inline constexpr uint32_t kMaxLcores   = 32;
inline constexpr uint32_t kMaxBackends = 64;

struct LcoreStats {
  CacheAligned<uint64_t> rx_packets;
  CacheAligned<uint64_t> tx_packets;
  CacheAligned<uint64_t> dropped;
  CacheAligned<uint64_t> new_flows;
  CacheAligned<uint64_t> active_flows;
  CacheAligned<uint64_t> backend_flows[kMaxBackends];
};

struct StatsView {
  LcoreStats lcores[kMaxLcores];
};

}  // namespace cere::ipc
```

- [ ] **Write `libs/ipc/include/ipc/shm_provider.hpp`**

```cpp
#pragma once
#include <cstddef>
#include <string>
#include <stdexcept>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

namespace cere::ipc {

class ShmProvider {
 public:
  enum class Mode { kCreate, kAttach };

  ShmProvider(std::string name, std::size_t size, Mode mode)
      : name_(std::move(name)), size_(size) {
    int flags = (mode == Mode::kCreate) ? (O_CREAT | O_RDWR) : O_RDWR;
    fd_ = shm_open(name_.c_str(), flags, 0600);
    if (fd_ < 0) throw std::runtime_error("shm_open failed: " + name_);
    if (mode == Mode::kCreate && ftruncate(fd_, static_cast<off_t>(size)) < 0)
      throw std::runtime_error("ftruncate failed");
    ptr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (ptr_ == MAP_FAILED) throw std::runtime_error("mmap failed");
  }

  ~ShmProvider() {
    if (ptr_ && ptr_ != MAP_FAILED) munmap(ptr_, size_);
    if (fd_ >= 0) close(fd_);
  }

  ShmProvider(const ShmProvider&) = delete;
  ShmProvider& operator=(const ShmProvider&) = delete;

  template <typename T>
  T* As() noexcept { return static_cast<T*>(ptr_); }

 private:
  std::string name_;
  std::size_t size_;
  int         fd_{-1};
  void*       ptr_{nullptr};
};

}  // namespace cere::ipc
```

- [ ] **Write `libs/ipc/CMakeLists.txt`**

```cmake
add_library(ipc INTERFACE)
target_include_directories(ipc INTERFACE include)
target_link_libraries(ipc INTERFACE cerebellum_project_options)
```

- [ ] **Build target**

```bash
cmake --preset debug-asan && cmake --build --preset debug-asan --target ipc 2>&1 | tail -3
```

Expected: `Built target ipc`

- [ ] **Commit**

```bash
git add libs/ipc/
git commit -m "feat: ipc library — CacheAligned, LcoreStats, ShmProvider"
```

---

## Task 3: libs/common — net types + Config

**Files:**
- Create: `libs/common/include/common/net_types.hpp`
- Create: `libs/common/include/common/config.hpp`
- Modify: `libs/common/CMakeLists.txt`

- [ ] **Write `libs/common/include/common/net_types.hpp`**

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <format>
#include <cstdio>

namespace cere::common {

struct IPv4Addr {
  uint32_t value{};

  static IPv4Addr FromString(std::string_view s) {
    unsigned a{}, b{}, c{}, d{};
    if (std::sscanf(s.data(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
      throw std::invalid_argument(std::format("bad IPv4: {}", s));
    return {(a << 24) | (b << 16) | (c << 8) | d};
  }

  std::string ToString() const {
    return std::format("{}.{}.{}.{}",
        (value >> 24) & 0xFF, (value >> 16) & 0xFF,
        (value >>  8) & 0xFF,  value        & 0xFF);
  }

  bool operator==(const IPv4Addr&) const = default;
};

struct MacAddr {
  std::array<uint8_t, 6> bytes{};
  bool operator==(const MacAddr&) const = default;
};

}  // namespace cere::common
```

- [ ] **Write `libs/common/include/common/config.hpp`**

```cpp
#pragma once
#include "net_types.hpp"
#include <cstdint>
#include <vector>

namespace cere::common {

struct BackendInfo {
  IPv4Addr ip{};
  MacAddr  mac{};
  uint16_t port{};
  uint16_t probe_port{};
};

struct Config {
  IPv4Addr vip{};
  uint16_t vip_port{};
  MacAddr  self_mac{};
  std::vector<BackendInfo> backends;
  uint32_t health_interval_ms{3000};
  uint32_t conntrack_timeout_ms{30000};
};

}  // namespace cere::common
```

- [ ] **Write `libs/common/CMakeLists.txt`**

```cmake
add_library(common INTERFACE)
target_include_directories(common INTERFACE include)
target_link_libraries(common INTERFACE cerebellum_project_options fmt::fmt)
```

- [ ] **Update `libs/CMakeLists.txt`**

```cmake
add_subdirectory(ipc)
add_subdirectory(common)
```

- [ ] **Build**

```bash
cmake --build --preset debug-asan --target common 2>&1 | tail -3
```

Expected: `Built target common`

- [ ] **Commit**

```bash
git add libs/common/ libs/CMakeLists.txt
git commit -m "feat: common library — IPv4Addr, MacAddr, Config, BackendInfo"
```

---

## Task 4: dataplane net/ + FlowKey hash + tests

**Files:**
- Create: `dataplane/include/net/flow_key.hpp`
- Create: `dataplane/tests/test_flow_key.cpp`
- Create: `dataplane/tests/CMakeLists.txt`
- Modify: `dataplane/CMakeLists.txt`

- [ ] **Write `dataplane/include/net/flow_key.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <cstring>

namespace cere::net {

struct FlowKey {
  uint32_t src_ip{};
  uint32_t dst_ip{};
  uint16_t src_port{};
  uint16_t dst_port{};
  uint8_t  proto{};
  uint8_t  pad[3]{};  // zero explicitly — no uninitialized bytes reach the hash
};
static_assert(sizeof(FlowKey) == 16);

inline bool operator==(const FlowKey& a, const FlowKey& b) noexcept {
  return std::memcmp(&a, &b, sizeof(FlowKey)) == 0;
}

inline uint32_t Hash(const FlowKey& k) noexcept {
#if defined(__SSE4_2__)
  uint32_t h = 0xdeadbeef;
  uint64_t lo, hi;
  std::memcpy(&lo, reinterpret_cast<const char*>(&k),     8);
  std::memcpy(&hi, reinterpret_cast<const char*>(&k) + 8, 8);
  h = static_cast<uint32_t>(__builtin_ia32_crc32di(h, lo));
  h = static_cast<uint32_t>(__builtin_ia32_crc32di(h, hi));
  return h;
#else
  const auto* p = reinterpret_cast<const uint8_t*>(&k);
  uint32_t h = 2166136261u;
  for (std::size_t i = 0; i < sizeof(FlowKey); ++i) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
#endif
}

// Build the canonical forward key from the reverse packet's perspective.
// rev packet: src=(backend_ip, backend_port), dst=(client_ip, client_port)
// forward key: src=client_ip, dst=vip_ip, sport=client_port, dport=vip_port
inline FlowKey ReverseKey(uint32_t client_ip, uint32_t vip_ip,
                           uint16_t client_port, uint16_t vip_port,
                           uint8_t proto) noexcept {
  return {client_ip, vip_ip, client_port, vip_port, proto};
}

}  // namespace cere::net
```

- [ ] **Write `dataplane/tests/test_flow_key.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "net/flow_key.hpp"

using cere::net::FlowKey;
using cere::net::Hash;

TEST_CASE("FlowKey size is 16 bytes", "[flow_key]") {
  REQUIRE(sizeof(FlowKey) == 16);
}

TEST_CASE("FlowKey equality", "[flow_key]") {
  FlowKey a{.src_ip=1,.dst_ip=2,.src_port=100,.dst_port=80,.proto=6};
  FlowKey b{.src_ip=1,.dst_ip=2,.src_port=100,.dst_port=80,.proto=6};
  FlowKey c{.src_ip=1,.dst_ip=2,.src_port=101,.dst_port=80,.proto=6};
  REQUIRE(a == b);
  REQUIRE(!(a == c));
}

TEST_CASE("FlowKey hash is deterministic", "[flow_key]") {
  FlowKey k{.src_ip=0x01020304,.dst_ip=0x0a000001,.src_port=12345,.dst_port=80,.proto=6};
  REQUIRE(Hash(k) == Hash(k));
}

TEST_CASE("FlowKey different keys produce different hashes", "[flow_key]") {
  FlowKey a{.src_ip=1,.dst_ip=2,.src_port=100,.dst_port=80,.proto=6};
  FlowKey b{.src_ip=1,.dst_ip=2,.src_port=101,.dst_port=80,.proto=6};
  REQUIRE(Hash(a) != Hash(b));
}

TEST_CASE("FlowKey padding bytes are zero", "[flow_key]") {
  FlowKey k{.src_ip=1,.proto=17};
  const auto* raw = reinterpret_cast<const uint8_t*>(&k);
  REQUIRE(raw[13] == 0);
  REQUIRE(raw[14] == 0);
  REQUIRE(raw[15] == 0);
}
```

- [ ] **Write `dataplane/tests/CMakeLists.txt`**

```cmake
add_executable(cerebellum_tests
    test_flow_key.cpp
)

target_include_directories(cerebellum_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/dataplane/include)

target_link_libraries(cerebellum_tests PRIVATE
    cerebellum_project_options
    cerebellum_compiler_warnings
    common
    Catch2::Catch2WithMain)

cerebellum_enable_sanitizers(cerebellum_tests)
cerebellum_enable_coverage(cerebellum_tests)

include(CTest)
include(Catch)
catch_discover_tests(cerebellum_tests)
```

- [ ] **Write `dataplane/CMakeLists.txt`**

```cmake
add_library(dataplane_headers INTERFACE)
target_include_directories(dataplane_headers INTERFACE include)
target_link_libraries(dataplane_headers INTERFACE
    cerebellum_project_options
    cerebellum_compiler_warnings
    common ipc)

if(CEREBELLUM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Run the test**

```bash
cmake --preset debug-asan && cmake --build --preset debug-asan && \
ctest --preset debug-asan -R test_flow_key -V
```

Expected: `All tests passed (5 assertions in 5 test cases)`

- [ ] **Commit**

```bash
git add dataplane/
git commit -m "feat: FlowKey with CRC32c hash + tests"
```

---

## Task 5: ConntrackTable + tests

**Files:**
- Create: `dataplane/include/lb/conntrack_table.hpp`
- Create: `dataplane/tests/test_conntrack_table.cpp`
- Modify: `dataplane/tests/CMakeLists.txt`

- [ ] **Write `dataplane/include/lb/conntrack_table.hpp`**

```cpp
#pragma once
#include "net/flow_key.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace cere::lb {

inline constexpr uint8_t kFlagSynSeen     = 0x01;
inline constexpr uint8_t kFlagEstablished = 0x02;
inline constexpr uint8_t kFlagFinSeen     = 0x04;

struct ConntrackEntry {
  net::FlowKey key{};
  uint8_t      backend_idx{};
  uint8_t      flags{};
  uint32_t     last_seen_ms{};
};

struct LookupResult {
  ConntrackEntry* entry;
  bool            is_new;
};

template <uint32_t kCapacity = (1u << 20)>
class ConntrackTable {
  static_assert((kCapacity & (kCapacity - 1)) == 0,
                "kCapacity must be a power of 2");

 public:
  explicit ConntrackTable(uint32_t timeout_ms = 30'000)
      : timeout_ms_(timeout_ms) {}

  // Forward path: find existing entry or insert new one with new_backend_idx.
  // Returns nullopt only if the table is completely full of unexpired entries.
  std::optional<LookupResult> LookupOrInsert(
      const net::FlowKey& key, uint8_t new_backend_idx, uint32_t now_ms) noexcept;

  // Read-only lookup (reverse path). Returns nullptr on miss or expiry.
  ConntrackEntry* Lookup(const net::FlowKey& key, uint32_t now_ms) noexcept;

  uint32_t Size()                     const noexcept { return size_; }
  void     SetTimeoutMs(uint32_t ms)        noexcept { timeout_ms_ = ms; }

 private:
  static constexpr uint32_t kMask = kCapacity - 1;

  struct Slot {
    ConntrackEntry entry{};
    bool           occupied{false};
  };

  bool IsExpired(const ConntrackEntry& e, uint32_t now_ms) const noexcept {
    return (now_ms - e.last_seen_ms) > timeout_ms_;
  }

  std::array<Slot, kCapacity> slots_{};
  uint32_t size_{0};
  uint32_t timeout_ms_;
};

template <uint32_t N>
std::optional<LookupResult> ConntrackTable<N>::LookupOrInsert(
    const net::FlowKey& key, uint8_t new_backend_idx, uint32_t now_ms) noexcept {
  const uint32_t start = net::Hash(key) & kMask;
  for (uint32_t i = 0; i < N; ++i) {
    Slot& slot = slots_[(start + i) & kMask];
    if (!slot.occupied || IsExpired(slot.entry, now_ms)) {
      bool was_new = !slot.occupied;
      slot.occupied       = true;
      slot.entry          = {key, new_backend_idx, 0, now_ms};
      if (was_new) ++size_;
      return LookupResult{&slot.entry, true};
    }
    if (slot.entry.key == key) {
      slot.entry.last_seen_ms = now_ms;
      return LookupResult{&slot.entry, false};
    }
  }
  return std::nullopt;
}

template <uint32_t N>
ConntrackEntry* ConntrackTable<N>::Lookup(
    const net::FlowKey& key, uint32_t now_ms) noexcept {
  const uint32_t start = net::Hash(key) & kMask;
  for (uint32_t i = 0; i < N; ++i) {
    Slot& slot = slots_[(start + i) & kMask];
    if (!slot.occupied) return nullptr;
    if (slot.entry.key == key) {
      if (IsExpired(slot.entry, now_ms)) return nullptr;
      slot.entry.last_seen_ms = now_ms;
      return &slot.entry;
    }
  }
  return nullptr;
}

}  // namespace cere::lb
```

- [ ] **Write `dataplane/tests/test_conntrack_table.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "lb/conntrack_table.hpp"

using namespace cere::lb;
using cere::net::FlowKey;

static FlowKey K(uint32_t src, uint32_t dst, uint16_t sp, uint16_t dp) {
  return {.src_ip=src,.dst_ip=dst,.src_port=sp,.dst_port=dp,.proto=6};
}

TEST_CASE("ConntrackTable - new flow", "[conntrack]") {
  ConntrackTable<64> t;
  auto r = t.LookupOrInsert(K(1,2,100,80), 3, 1000);
  REQUIRE(r.has_value());
  REQUIRE(r->is_new == true);
  REQUIRE(r->entry->backend_idx == 3);
  REQUIRE(t.Size() == 1);
}

TEST_CASE("ConntrackTable - existing flow keeps backend", "[conntrack]") {
  ConntrackTable<64> t;
  auto key = K(1,2,100,80);
  t.LookupOrInsert(key, 3, 1000);
  auto r = t.LookupOrInsert(key, 7, 2000);
  REQUIRE(r.has_value());
  REQUIRE(r->is_new == false);
  REQUIRE(r->entry->backend_idx == 3);
  REQUIRE(t.Size() == 1);
}

TEST_CASE("ConntrackTable - expired entry replaced", "[conntrack]") {
  ConntrackTable<64> t(100);
  auto key = K(1,2,100,80);
  t.LookupOrInsert(key, 1, 0);
  auto r = t.LookupOrInsert(key, 5, 200);
  REQUIRE(r.has_value());
  REQUIRE(r->is_new == true);
  REQUIRE(r->entry->backend_idx == 5);
}

TEST_CASE("ConntrackTable - Lookup hit and miss", "[conntrack]") {
  ConntrackTable<64> t;
  t.LookupOrInsert(K(10,20,1000,80), 2, 0);
  REQUIRE(t.Lookup(K(10,20,1000,80), 500) != nullptr);
  REQUIRE(t.Lookup(K(10,20,1001,80), 500) == nullptr);
}

TEST_CASE("ConntrackTable - Lookup expired returns nullptr", "[conntrack]") {
  ConntrackTable<64> t(50);
  t.LookupOrInsert(K(1,2,100,80), 1, 0);
  REQUIRE(t.Lookup(K(1,2,100,80), 100) == nullptr);
}

TEST_CASE("ConntrackTable - multiple distinct keys", "[conntrack]") {
  ConntrackTable<128> t;
  for (uint16_t p = 1000; p < 1010; ++p)
    t.LookupOrInsert(K(1,2,p,80), static_cast<uint8_t>(p%4), 0);
  REQUIRE(t.Size() == 10);
  for (uint16_t p = 1000; p < 1010; ++p) {
    auto* e = t.Lookup(K(1,2,p,80), 500);
    REQUIRE(e != nullptr);
    REQUIRE(e->backend_idx == p%4);
  }
}
```

- [ ] **Add to `dataplane/tests/CMakeLists.txt`**

Replace the `add_executable` sources list:
```cmake
add_executable(cerebellum_tests
    test_flow_key.cpp
    test_conntrack_table.cpp
)
```

- [ ] **Run all tests**

```bash
cmake --build --preset debug-asan && ctest --preset debug-asan -V
```

Expected: all tests pass.

- [ ] **Commit**

```bash
git add dataplane/include/lb/conntrack_table.hpp dataplane/tests/
git commit -m "feat: ConntrackTable — open-addressing hash, lazy expiry, tests"
```

---

## Task 6: BackendPool + AtomicRcu + tests

**Files:**
- Create: `dataplane/include/lb/atomic_rcu.hpp`
- Create: `dataplane/include/lb/backend_pool.hpp`
- Create: `dataplane/tests/test_backend_pool.cpp`
- Modify: `dataplane/tests/CMakeLists.txt`

- [ ] **Write `dataplane/include/lb/atomic_rcu.hpp`**

```cpp
#pragma once
#include <atomic>
#include <memory>

namespace cere::lb {

// Single-writer, multi-reader via atomic shared_ptr.
// Reader holds the returned shared_ptr for the duration of access.
template <typename T>
class AtomicRcu {
 public:
  explicit AtomicRcu(std::shared_ptr<T> initial = nullptr)
      : ptr_(std::move(initial)) {}

  std::shared_ptr<T> Load() const noexcept {
    return std::atomic_load_explicit(&ptr_, std::memory_order_acquire);
  }

  void Store(std::shared_ptr<T> p) noexcept {
    std::atomic_store_explicit(&ptr_, std::move(p), std::memory_order_release);
  }

 private:
  std::shared_ptr<T> ptr_;
};

}  // namespace cere::lb
```

- [ ] **Write `dataplane/include/lb/backend_pool.hpp`**

```cpp
#pragma once
#include "common/config.hpp"
#include "net/flow_key.hpp"
#include <span>
#include <vector>

namespace cere::lb {

class BackendPool {
 public:
  explicit BackendPool(std::vector<common::BackendInfo> active)
      : backends_(std::move(active)) {}

  uint8_t Select(const net::FlowKey& key) const noexcept {
    return static_cast<uint8_t>(net::Hash(key) % backends_.size());
  }

  const common::BackendInfo& At(uint8_t idx) const noexcept {
    return backends_[idx];
  }

  uint8_t ActiveCount() const noexcept {
    return static_cast<uint8_t>(backends_.size());
  }

  std::span<const common::BackendInfo> All() const noexcept { return backends_; }

 private:
  std::vector<common::BackendInfo> backends_;
};

}  // namespace cere::lb
```

- [ ] **Write `dataplane/tests/test_backend_pool.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "lb/backend_pool.hpp"
#include "lb/atomic_rcu.hpp"

using namespace cere::lb;
using cere::common::BackendInfo;
using cere::net::FlowKey;

static BackendInfo B(uint32_t ip, uint16_t port) {
  BackendInfo b; b.ip.value = ip; b.port = port; return b;
}

TEST_CASE("BackendPool - Select is deterministic", "[backend_pool]") {
  BackendPool p({B(1,80), B(2,80), B(3,80)});
  FlowKey k{.src_ip=42,.dst_ip=1,.src_port=9000,.dst_port=80,.proto=6};
  REQUIRE(p.Select(k) == p.Select(k));
  REQUIRE(p.ActiveCount() == 3);
}

TEST_CASE("BackendPool - Select distributes load", "[backend_pool]") {
  BackendPool p({B(1,80), B(2,80), B(3,80)});
  std::array<int,3> cnt{};
  for (uint16_t port = 1000; port < 1300; ++port) {
    FlowKey k{.src_ip=1,.dst_ip=2,.src_port=port,.dst_port=80,.proto=6};
    cnt[p.Select(k)]++;
  }
  REQUIRE(cnt[0] > 20);
  REQUIRE(cnt[1] > 20);
  REQUIRE(cnt[2] > 20);
}

TEST_CASE("AtomicRcu - initial load is nullptr", "[atomic_rcu]") {
  AtomicRcu<BackendPool> rcu;
  REQUIRE(rcu.Load() == nullptr);
}

TEST_CASE("AtomicRcu - store and load", "[atomic_rcu]") {
  AtomicRcu<BackendPool> rcu;
  rcu.Store(std::make_shared<BackendPool>(std::vector{B(10,80), B(11,80)}));
  REQUIRE(rcu.Load()->ActiveCount() == 2);
}

TEST_CASE("AtomicRcu - swap replaces value", "[atomic_rcu]") {
  AtomicRcu<BackendPool> rcu;
  rcu.Store(std::make_shared<BackendPool>(std::vector{B(1,80)}));
  rcu.Store(std::make_shared<BackendPool>(std::vector{B(2,80), B(3,80)}));
  REQUIRE(rcu.Load()->ActiveCount() == 2);
}
```

- [ ] **Add to `dataplane/tests/CMakeLists.txt`**

```cmake
add_executable(cerebellum_tests
    test_flow_key.cpp
    test_conntrack_table.cpp
    test_backend_pool.cpp
)
```

- [ ] **Run all tests**

```bash
cmake --build --preset debug-asan && ctest --preset debug-asan -V
```

Expected: all tests pass.

- [ ] **Commit**

```bash
git add dataplane/include/lb/ dataplane/tests/
git commit -m "feat: BackendPool, AtomicRcu, tests"
```

---

## Task 7: Graph engine — Frame, Node, Graph + tests

**Files:**
- Create: `dataplane/include/graph/frame.hpp`
- Create: `dataplane/include/graph/node.hpp`
- Create: `dataplane/include/graph/graph.hpp`
- Create: `dataplane/tests/test_graph.cpp`
- Modify: `dataplane/tests/CMakeLists.txt`

- [ ] **Write `dataplane/include/graph/frame.hpp`**

```cpp
#pragma once
#include <array>
#include <cstdint>

// Provide a minimal rte_mbuf stub for test builds that don't link DPDK.
#ifndef RTE_MBUF_DEFAULT_BUF_SIZE
struct rte_mbuf { uint8_t raw[128]; };
#endif

namespace cere::graph {

inline constexpr uint16_t kBurstSize = 64;

enum class Direction : uint8_t { kForward, kReverse };

struct PacketMeta {
  uint32_t  src_ip{};
  uint32_t  dst_ip{};
  uint16_t  src_port{};
  uint16_t  dst_port{};
  uint8_t   proto{};
  uint8_t   backend_idx{};
  Direction dir{Direction::kForward};
};

struct Frame {
  std::array<rte_mbuf*,  kBurstSize> mbufs{};
  std::array<PacketMeta, kBurstSize> meta{};
  uint16_t count{0};

  void Clear() noexcept { count = 0; }

  void MoveTo(uint16_t i, Frame& dst) noexcept {
    dst.mbufs[dst.count] = mbufs[i];
    dst.meta[dst.count]  = meta[i];
    ++dst.count;
  }
};

}  // namespace cere::graph
```

- [ ] **Write `dataplane/include/graph/node.hpp`**

```cpp
#pragma once
#include "frame.hpp"
#include <span>
#include <string_view>

namespace cere::graph {

class Node {
 public:
  virtual ~Node() = default;
  virtual void             Process(Frame& in, std::span<Frame> nexts) noexcept = 0;
  virtual std::string_view Name()      const noexcept = 0;
  virtual uint16_t         NextCount() const noexcept = 0;
};

}  // namespace cere::graph
```

- [ ] **Write `dataplane/include/graph/graph.hpp`**

```cpp
#pragma once
#include "node.hpp"
#include <memory>
#include <vector>
#include <stdexcept>
#include <format>

namespace cere::graph {

class Graph {
 public:
  using NodeId = uint16_t;
  static constexpr NodeId kInvalid = 0xFFFF;

  NodeId AddNode(std::unique_ptr<Node> node) {
    NodeId id = static_cast<NodeId>(nodes_.size());
    routing_.push_back(std::vector<NodeId>(node->NextCount(), kInvalid));
    nodes_.push_back(std::move(node));
    pending_.emplace_back();
    return id;
  }

  void Wire(NodeId from, uint16_t out_idx, NodeId to) {
    if (from >= nodes_.size() || to >= nodes_.size())
      throw std::out_of_range(
          std::format("Wire: invalid node {} or {}", from, to));
    if (out_idx >= routing_[from].size())
      throw std::out_of_range(
          std::format("Wire: out_idx {} >= NextCount()", out_idx));
    routing_[from][out_idx] = to;
  }

  // Drive one burst starting from `root`.
  // `root_frame` is populated by RxNode.Process() on the first call;
  // subsequent nodes receive their pending frames.
  void RunOnce(NodeId root, Frame& root_frame) noexcept {
    for (auto& f : pending_) f.Clear();
    pending_[root] = root_frame;

    for (NodeId id = root; id < static_cast<NodeId>(nodes_.size()); ++id) {
      Frame& in = pending_[id];
      if (in.count == 0) continue;

      const uint16_t nc = nodes_[id]->NextCount();
      // Use a local array for this node's output frames (max 8 outputs)
      std::array<Frame, 8> nexts{};
      nodes_[id]->Process(in, std::span<Frame>{nexts.data(), nc});

      for (uint16_t out = 0; out < nc; ++out) {
        NodeId target = routing_[id][out];
        if (target == kInvalid || nexts[out].count == 0) continue;
        Frame& dst = pending_[target];
        for (uint16_t p = 0; p < nexts[out].count; ++p)
          nexts[out].MoveTo(p, dst);
      }
      in.Clear();
    }
  }

 private:
  std::vector<std::unique_ptr<Node>> nodes_;
  std::vector<std::vector<NodeId>>   routing_;
  std::vector<Frame>                 pending_;
};

}  // namespace cere::graph
```

- [ ] **Write `dataplane/tests/test_graph.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "graph/graph.hpp"

using namespace cere::graph;

class CountNode : public Node {
 public:
  explicit CountNode(std::string_view n) : name_(n) {}
  void Process(Frame& in, std::span<Frame> nexts) noexcept override {
    count_ += in.count;
    if (!nexts.empty())
      for (uint16_t i = 0; i < in.count; ++i) in.MoveTo(i, nexts[0]);
  }
  std::string_view Name()      const noexcept override { return name_; }
  uint16_t         NextCount() const noexcept override { return 1; }
  uint32_t         Count()     const noexcept { return count_; }
 private:
  std::string_view name_;
  uint32_t         count_{0};
};

class SplitNode : public Node {
 public:
  void Process(Frame& in, std::span<Frame> nexts) noexcept override {
    for (uint16_t i = 0; i < in.count; ++i) in.MoveTo(i, nexts[i % 2]);
  }
  std::string_view Name()      const noexcept override { return "split"; }
  uint16_t         NextCount() const noexcept override { return 2; }
};

class SinkNode : public Node {
 public:
  void             Process(Frame& in, std::span<Frame>) noexcept override { n_ += in.count; }
  std::string_view Name()      const noexcept override { return "sink"; }
  uint16_t         NextCount() const noexcept override { return 0; }
  uint32_t         Count()     const noexcept { return n_; }
 private:
  uint32_t n_{0};
};

TEST_CASE("Graph - linear chain passes all packets", "[graph]") {
  Graph g;
  auto* a    = new CountNode("a");
  auto* b    = new CountNode("b");
  auto* sink = new SinkNode;
  auto id_a    = g.AddNode(std::unique_ptr<Node>(a));
  auto id_b    = g.AddNode(std::unique_ptr<Node>(b));
  auto id_sink = g.AddNode(std::unique_ptr<Node>(sink));
  g.Wire(id_a, 0, id_b);
  g.Wire(id_b, 0, id_sink);

  Frame root; root.count = 5;
  g.RunOnce(id_a, root);

  REQUIRE(a->Count()    == 5);
  REQUIRE(b->Count()    == 5);
  REQUIRE(sink->Count() == 5);
}

TEST_CASE("Graph - splitter distributes packets", "[graph]") {
  Graph g;
  auto* sp   = new SplitNode;
  auto* even = new SinkNode;
  auto* odd  = new SinkNode;
  auto id_sp   = g.AddNode(std::unique_ptr<Node>(sp));
  auto id_even = g.AddNode(std::unique_ptr<Node>(even));
  auto id_odd  = g.AddNode(std::unique_ptr<Node>(odd));
  g.Wire(id_sp, 0, id_even);
  g.Wire(id_sp, 1, id_odd);

  Frame root; root.count = 6;
  g.RunOnce(id_sp, root);

  REQUIRE(even->Count() == 3);
  REQUIRE(odd->Count()  == 3);
}

TEST_CASE("Graph - Wire invalid node throws", "[graph]") {
  Graph g;
  g.AddNode(std::make_unique<SinkNode>());
  REQUIRE_THROWS_AS(g.Wire(0, 0, 99), std::out_of_range);
}
```

- [ ] **Add to `dataplane/tests/CMakeLists.txt`**

```cmake
add_executable(cerebellum_tests
    test_flow_key.cpp
    test_conntrack_table.cpp
    test_backend_pool.cpp
    test_graph.cpp
)
```

- [ ] **Run all tests**

```bash
cmake --build --preset debug-asan && ctest --preset debug-asan -V
```

Expected: all tests pass.

- [ ] **Commit**

```bash
git add dataplane/include/graph/ dataplane/tests/
git commit -m "feat: graph engine — Frame, Node, Graph with VPP-style dispatch, tests"
```

---

## Task 8: Pipeline nodes (DPDK-dependent)

**Files:**
- Create: `dataplane/include/nodes/drop_node.hpp`
- Create: `dataplane/include/nodes/ip4_parse_node.hpp`
- Create: `dataplane/include/nodes/lb_node.hpp`
- Create: `dataplane/include/nodes/dnat_node.hpp`
- Create: `dataplane/include/nodes/ether_rewrite_node.hpp`
- Create: `dataplane/include/nodes/rx_node.hpp`
- Create: `dataplane/include/nodes/tx_node.hpp`

- [ ] **Write `dataplane/include/nodes/drop_node.hpp`**

```cpp
#pragma once
#include "graph/node.hpp"
#include <rte_mbuf.h>

namespace cere::nodes {

class DropNode : public graph::Node {
 public:
  void Process(graph::Frame& in, std::span<graph::Frame>) noexcept override {
    rte_pktmbuf_free_bulk(in.mbufs.data(), in.count);
    in.count = 0;
  }
  std::string_view Name()      const noexcept override { return "drop"; }
  uint16_t         NextCount() const noexcept override { return 0; }
};

}  // namespace cere::nodes
```

- [ ] **Write `dataplane/include/nodes/ip4_parse_node.hpp`**

```cpp
#pragma once
#include "graph/node.hpp"
#include "common/config.hpp"
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

namespace cere::nodes {

class Ip4ParseNode : public graph::Node {
 public:
  static constexpr uint16_t kNextFwd  = 0;
  static constexpr uint16_t kNextRev  = 1;
  static constexpr uint16_t kNextDrop = 2;

  explicit Ip4ParseNode(const common::Config& cfg) : cfg_(cfg) {}

  void Process(graph::Frame& in, std::span<graph::Frame> nexts) noexcept override {
    for (uint16_t i = 0; i < in.count; ++i) {
      auto* eth = rte_pktmbuf_mtod(in.mbufs[i], rte_ether_hdr*);
      if (rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4) {
        in.MoveTo(i, nexts[kNextDrop]); continue;
      }
      auto* ip4 = reinterpret_cast<rte_ipv4_hdr*>(eth + 1);
      uint8_t proto = ip4->next_proto_id;
      if (proto != IPPROTO_TCP && proto != IPPROTO_UDP) {
        in.MoveTo(i, nexts[kNextDrop]); continue;
      }
      auto* l4 = reinterpret_cast<uint8_t*>(ip4) + (ip4->ihl * 4);
      uint16_t sp, dp;
      if (proto == IPPROTO_TCP) {
        auto* tcp = reinterpret_cast<rte_tcp_hdr*>(l4);
        sp = rte_be_to_cpu_16(tcp->src_port);
        dp = rte_be_to_cpu_16(tcp->dst_port);
      } else {
        auto* udp = reinterpret_cast<rte_udp_hdr*>(l4);
        sp = rte_be_to_cpu_16(udp->src_port);
        dp = rte_be_to_cpu_16(udp->dst_port);
      }
      uint32_t src_ip = rte_be_to_cpu_32(ip4->src_addr);
      uint32_t dst_ip = rte_be_to_cpu_32(ip4->dst_addr);

      graph::PacketMeta& meta = in.meta[i];
      meta = {src_ip, dst_ip, sp, dp, proto};

      if (dst_ip == cfg_.vip.value && dp == cfg_.vip_port) {
        meta.dir = graph::Direction::kForward;
        in.MoveTo(i, nexts[kNextFwd]);
      } else if (IsBackend(src_ip)) {
        meta.dir = graph::Direction::kReverse;
        in.MoveTo(i, nexts[kNextRev]);
      } else {
        in.MoveTo(i, nexts[kNextDrop]);
      }
    }
  }

  std::string_view Name()      const noexcept override { return "ip4-parse"; }
  uint16_t         NextCount() const noexcept override { return 3; }

 private:
  bool IsBackend(uint32_t ip) const noexcept {
    for (const auto& b : cfg_.backends)
      if (b.ip.value == ip) return true;
    return false;
  }
  const common::Config& cfg_;
};

}  // namespace cere::nodes
```

- [ ] **Write `dataplane/include/nodes/lb_node.hpp`**

```cpp
#pragma once
#include "graph/node.hpp"
#include "lb/conntrack_table.hpp"
#include "lb/backend_pool.hpp"
#include "lb/atomic_rcu.hpp"
#include "ipc/lcore_stats.hpp"
#include "common/config.hpp"
#include <rte_cycles.h>

namespace cere::nodes {

class LbNode : public graph::Node {
 public:
  static constexpr uint16_t kNextDnat = 0;
  static constexpr uint16_t kNextDrop = 1;

  LbNode(const common::Config& cfg,
         lb::AtomicRcu<lb::BackendPool>& pool_rcu,
         ipc::LcoreStats& stats)
      : cfg_(cfg), pool_rcu_(pool_rcu), stats_(stats) {}

  void Process(graph::Frame& in, std::span<graph::Frame> nexts) noexcept override {
    auto pool = pool_rcu_.Load();
    if (!pool || pool->ActiveCount() == 0) {
      for (uint16_t i = 0; i < in.count; ++i) in.MoveTo(i, nexts[kNextDrop]);
      return;
    }
    uint32_t now_ms = static_cast<uint32_t>(
        rte_get_timer_cycles() / (rte_get_timer_hz() / 1000));

    for (uint16_t i = 0; i < in.count; ++i) {
      graph::PacketMeta& meta = in.meta[i];

      if (meta.dir == graph::Direction::kForward) {
        net::FlowKey key{meta.src_ip, meta.dst_ip,
                         meta.src_port, meta.dst_port, meta.proto};
        auto res = conntrack_.LookupOrInsert(key, pool->Select(key), now_ms);
        if (!res) { in.MoveTo(i, nexts[kNextDrop]); continue; }
        if (res->is_new) {
          stats_.new_flows.value.fetch_add(1, std::memory_order_relaxed);
          stats_.backend_flows[res->entry->backend_idx].value
              .fetch_add(1, std::memory_order_relaxed);
        }
        meta.backend_idx = res->entry->backend_idx;
      } else {
        net::FlowKey fwd = net::ReverseKey(
            meta.dst_ip, cfg_.vip.value, meta.dst_port, cfg_.vip_port, meta.proto);
        auto* entry = conntrack_.Lookup(fwd, now_ms);
        if (!entry) { in.MoveTo(i, nexts[kNextDrop]); continue; }
        meta.backend_idx = entry->backend_idx;
      }
      in.MoveTo(i, nexts[kNextDnat]);
    }
    stats_.active_flows.value.store(conntrack_.Size(), std::memory_order_relaxed);
  }

  std::string_view Name()      const noexcept override { return "lb"; }
  uint16_t         NextCount() const noexcept override { return 2; }

 private:
  const common::Config&           cfg_;
  lb::AtomicRcu<lb::BackendPool>& pool_rcu_;
  ipc::LcoreStats&                stats_;
  lb::ConntrackTable<>            conntrack_;
};

}  // namespace cere::nodes
```

- [ ] **Write `dataplane/include/nodes/dnat_node.hpp`**

```cpp
#pragma once
#include "graph/node.hpp"
#include "lb/backend_pool.hpp"
#include "lb/atomic_rcu.hpp"
#include "common/config.hpp"
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

namespace cere::nodes {

class DnatNode : public graph::Node {
 public:
  DnatNode(const common::Config& cfg,
           lb::AtomicRcu<lb::BackendPool>& pool_rcu)
      : cfg_(cfg), pool_rcu_(pool_rcu) {}

  void Process(graph::Frame& in, std::span<graph::Frame> nexts) noexcept override {
    auto pool = pool_rcu_.Load();
    for (uint16_t i = 0; i < in.count; ++i) {
      const graph::PacketMeta& meta = in.meta[i];
      auto* eth = rte_pktmbuf_mtod(in.mbufs[i], rte_ether_hdr*);
      auto* ip4 = reinterpret_cast<rte_ipv4_hdr*>(eth + 1);
      auto* l4  = reinterpret_cast<uint8_t*>(ip4) + (ip4->ihl * 4);
      bool  tcp = (meta.proto == IPPROTO_TCP);

      if (meta.dir == graph::Direction::kForward && pool) {
        const auto& b = pool->At(meta.backend_idx);
        ip4->dst_addr = rte_cpu_to_be_32(b.ip.value);
        if (tcp) reinterpret_cast<rte_tcp_hdr*>(l4)->dst_port = rte_cpu_to_be_16(b.port);
        else     reinterpret_cast<rte_udp_hdr*>(l4)->dst_port = rte_cpu_to_be_16(b.port);
      } else {
        ip4->src_addr = rte_cpu_to_be_32(cfg_.vip.value);
        if (tcp) reinterpret_cast<rte_tcp_hdr*>(l4)->src_port = rte_cpu_to_be_16(cfg_.vip_port);
        else     reinterpret_cast<rte_udp_hdr*>(l4)->src_port = rte_cpu_to_be_16(cfg_.vip_port);
      }
      ip4->hdr_checksum = 0;
      ip4->hdr_checksum = rte_ipv4_cksum(ip4);
      in.MoveTo(i, nexts[0]);
    }
  }

  std::string_view Name()      const noexcept override { return "dnat"; }
  uint16_t         NextCount() const noexcept override { return 1; }

 private:
  const common::Config&           cfg_;
  lb::AtomicRcu<lb::BackendPool>& pool_rcu_;
};

}  // namespace cere::nodes
```

- [ ] **Write `dataplane/include/nodes/ether_rewrite_node.hpp`**

```cpp
#pragma once
#include "graph/node.hpp"
#include "lb/backend_pool.hpp"
#include "lb/atomic_rcu.hpp"
#include "common/config.hpp"
#include <rte_ether.h>
#include <cstring>

namespace cere::nodes {

class EtherRewriteNode : public graph::Node {
 public:
  EtherRewriteNode(const common::Config& cfg,
                   lb::AtomicRcu<lb::BackendPool>& pool_rcu)
      : cfg_(cfg), pool_rcu_(pool_rcu) {}

  void Process(graph::Frame& in, std::span<graph::Frame> nexts) noexcept override {
    auto pool = pool_rcu_.Load();
    for (uint16_t i = 0; i < in.count; ++i) {
      auto* eth = rte_pktmbuf_mtod(in.mbufs[i], rte_ether_hdr*);
      const graph::PacketMeta& meta = in.meta[i];
      if (meta.dir == graph::Direction::kForward && pool)
        std::memcpy(eth->dst_addr.addr_bytes,
                    pool->At(meta.backend_idx).mac.bytes.data(), RTE_ETHER_ADDR_LEN);
      std::memcpy(eth->src_addr.addr_bytes,
                  cfg_.self_mac.bytes.data(), RTE_ETHER_ADDR_LEN);
      in.MoveTo(i, nexts[0]);
    }
  }

  std::string_view Name()      const noexcept override { return "ether-rewrite"; }
  uint16_t         NextCount() const noexcept override { return 1; }

 private:
  const common::Config&           cfg_;
  lb::AtomicRcu<lb::BackendPool>& pool_rcu_;
};

}  // namespace cere::nodes
```

- [ ] **Write `dataplane/include/nodes/rx_node.hpp`**

```cpp
#pragma once
#include "graph/node.hpp"
#include <rte_ethdev.h>

namespace cere::nodes {

class RxNode : public graph::Node {
 public:
  RxNode(uint16_t port_id, uint16_t queue_id)
      : port_id_(port_id), queue_id_(queue_id) {}

  void Process(graph::Frame& in, std::span<graph::Frame> nexts) noexcept override {
    in.count = rte_eth_rx_burst(
        port_id_, queue_id_, in.mbufs.data(), graph::kBurstSize);
    for (uint16_t i = 0; i < in.count; ++i) in.MoveTo(i, nexts[0]);
  }

  std::string_view Name()      const noexcept override { return "rx"; }
  uint16_t         NextCount() const noexcept override { return 1; }

 private:
  uint16_t port_id_;
  uint16_t queue_id_;
};

}  // namespace cere::nodes
```

- [ ] **Write `dataplane/include/nodes/tx_node.hpp`**

```cpp
#pragma once
#include "graph/node.hpp"
#include "ipc/lcore_stats.hpp"
#include <rte_ethdev.h>
#include <rte_mbuf.h>

namespace cere::nodes {

class TxNode : public graph::Node {
 public:
  TxNode(uint16_t port_id, uint16_t queue_id, ipc::LcoreStats& stats)
      : port_id_(port_id), queue_id_(queue_id), stats_(stats) {}

  void Process(graph::Frame& in, std::span<graph::Frame>) noexcept override {
    uint16_t sent = rte_eth_tx_burst(
        port_id_, queue_id_, in.mbufs.data(), in.count);
    if (sent < in.count)
      rte_pktmbuf_free_bulk(in.mbufs.data() + sent, in.count - sent);
    stats_.tx_packets.value.fetch_add(sent, std::memory_order_relaxed);
    stats_.dropped.value.fetch_add(in.count - sent, std::memory_order_relaxed);
  }

  std::string_view Name()      const noexcept override { return "tx"; }
  uint16_t         NextCount() const noexcept override { return 0; }

 private:
  uint16_t         port_id_;
  uint16_t         queue_id_;
  ipc::LcoreStats& stats_;
};

}  // namespace cere::nodes
```

- [ ] **Verify nodes compile against DPDK headers**

```bash
cmake --build --preset release --target dataplane_headers 2>&1 | tail -5
```

Expected: `Built target dataplane_headers`

- [ ] **Commit**

```bash
git add dataplane/include/nodes/
git commit -m "feat: pipeline nodes — rx, ip4-parse, lb, dnat, ether-rewrite, tx, drop"
```

---

## Task 9: DPDK I/O + dataplane main

**Files:**
- Create: `dataplane/include/io/eal.hpp`
- Create: `dataplane/include/io/dpdk_port.hpp`
- Create: `dataplane/src/main.cpp`
- Modify: `dataplane/CMakeLists.txt`

- [ ] **Write `dataplane/include/io/eal.hpp`**

```cpp
#pragma once
#include <rte_eal.h>
#include <stdexcept>
#include <vector>
#include <string>

namespace cere::io {

class Eal {
 public:
  explicit Eal(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& a : args) argv.push_back(a.data());
    if (rte_eal_init(static_cast<int>(argv.size()), argv.data()) < 0)
      throw std::runtime_error("rte_eal_init failed");
  }
  ~Eal() { rte_eal_cleanup(); }
  Eal(const Eal&) = delete;
  Eal& operator=(const Eal&) = delete;
};

}  // namespace cere::io
```

- [ ] **Write `dataplane/include/io/dpdk_port.hpp`**

```cpp
#pragma once
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <stdexcept>
#include <format>

namespace cere::io {

inline constexpr uint16_t kRxRingSize    = 1024;
inline constexpr uint16_t kTxRingSize    = 1024;
inline constexpr uint16_t kMbufCount     = 8192;
inline constexpr uint16_t kMbufCacheSize = 256;

class DpdkPort {
 public:
  DpdkPort(uint16_t port_id, uint16_t n_queues) : port_id_(port_id) {
    rte_eth_conf conf{};
    conf.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
    conf.rx_adv_conf.rss_conf.rss_hf =
        RTE_ETH_RSS_IP | RTE_ETH_RSS_TCP | RTE_ETH_RSS_UDP;
    if (rte_eth_dev_configure(port_id_, n_queues, n_queues, &conf) < 0)
      throw std::runtime_error(std::format("port {} configure failed", port_id_));

    pool_ = rte_pktmbuf_pool_create(
        std::format("pool_{}", port_id_).c_str(),
        kMbufCount, kMbufCacheSize, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!pool_) throw std::runtime_error("mbuf pool create failed");

    for (uint16_t q = 0; q < n_queues; ++q) {
      if (rte_eth_rx_queue_setup(port_id_, q, kRxRingSize,
              rte_eth_dev_socket_id(port_id_), nullptr, pool_) < 0)
        throw std::runtime_error(std::format("rx queue {} setup failed", q));
      if (rte_eth_tx_queue_setup(port_id_, q, kTxRingSize,
              rte_eth_dev_socket_id(port_id_), nullptr) < 0)
        throw std::runtime_error(std::format("tx queue {} setup failed", q));
    }
    if (rte_eth_dev_start(port_id_) < 0)
      throw std::runtime_error(std::format("port {} start failed", port_id_));
    rte_eth_promiscuous_enable(port_id_);
  }

  ~DpdkPort() {
    rte_eth_dev_stop(port_id_);
    rte_eth_dev_close(port_id_);
    if (pool_) rte_mempool_free(pool_);
  }

  DpdkPort(const DpdkPort&) = delete;
  DpdkPort& operator=(const DpdkPort&) = delete;

  uint16_t     PortId() const noexcept { return port_id_; }
  rte_mempool* Pool()   const noexcept { return pool_; }

 private:
  uint16_t     port_id_;
  rte_mempool* pool_{nullptr};
};

}  // namespace cere::io
```

- [ ] **Write `dataplane/src/main.cpp`**

```cpp
#include "io/eal.hpp"
#include "io/dpdk_port.hpp"
#include "graph/graph.hpp"
#include "nodes/rx_node.hpp"
#include "nodes/ip4_parse_node.hpp"
#include "nodes/lb_node.hpp"
#include "nodes/dnat_node.hpp"
#include "nodes/ether_rewrite_node.hpp"
#include "nodes/tx_node.hpp"
#include "nodes/drop_node.hpp"
#include "lb/atomic_rcu.hpp"
#include "lb/backend_pool.hpp"
#include "ipc/shm_provider.hpp"
#include "ipc/lcore_stats.hpp"
#include "common/config.hpp"
#include "config/yaml_loader.hpp"

#include <rte_launch.h>
#include <rte_lcore.h>
#include <atomic>
#include <cstdio>
#include <vector>

namespace {

std::atomic<bool> g_running{true};

struct LcoreArg {
  uint16_t                         queue_id;
  uint16_t                         port_id;
  const common::Config*            cfg;
  lb::AtomicRcu<lb::BackendPool>*  pool_rcu;
  ipc::LcoreStats*                 stats;
};

int LcoreMain(void* raw) {
  auto* a = static_cast<LcoreArg*>(raw);
  graph::Graph g;

  auto rx    = g.AddNode(std::make_unique<nodes::RxNode>(a->port_id, a->queue_id));
  auto parse = g.AddNode(std::make_unique<nodes::Ip4ParseNode>(*a->cfg));
  auto lb    = g.AddNode(std::make_unique<nodes::LbNode>(*a->cfg, *a->pool_rcu, *a->stats));
  auto dnat  = g.AddNode(std::make_unique<nodes::DnatNode>(*a->cfg, *a->pool_rcu));
  auto eth   = g.AddNode(std::make_unique<nodes::EtherRewriteNode>(*a->cfg, *a->pool_rcu));
  auto tx    = g.AddNode(std::make_unique<nodes::TxNode>(a->port_id, a->queue_id, *a->stats));
  auto drop  = g.AddNode(std::make_unique<nodes::DropNode>());

  g.Wire(rx,    0,                              parse);
  g.Wire(parse, nodes::Ip4ParseNode::kNextFwd,  lb);
  g.Wire(parse, nodes::Ip4ParseNode::kNextRev,  lb);
  g.Wire(parse, nodes::Ip4ParseNode::kNextDrop, drop);
  g.Wire(lb,    nodes::LbNode::kNextDnat,       dnat);
  g.Wire(lb,    nodes::LbNode::kNextDrop,       drop);
  g.Wire(dnat,  0,                              eth);
  g.Wire(eth,   0,                              tx);

  graph::Frame root;
  while (g_running.load(std::memory_order_relaxed)) {
    root.Clear();
    g.RunOnce(rx, root);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <config.yaml> [dpdk args...]\n", argv[0]);
    return 1;
  }

  // argv[1] is config path; remaining args go to EAL
  std::vector<std::string> eal_args{"cerebellum_dataplane"};
  for (int i = 2; i < argc; ++i) eal_args.emplace_back(argv[i]);

  try {
    io::Eal eal(eal_args);
    auto cfg = control::LoadConfig(argv[1]);

    ipc::ShmProvider shm("/cerebellum_stats",
        sizeof(ipc::StatsView), ipc::ShmProvider::Mode::kCreate);
    auto* view = shm.As<ipc::StatsView>();

    lb::AtomicRcu<lb::BackendPool> pool_rcu;
    uint16_t n_queues = static_cast<uint16_t>(rte_lcore_count() - 1);
    io::DpdkPort port(0, n_queues);

    uint16_t queue = 0;
    std::vector<LcoreArg> args(n_queues);
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
      args[queue] = {queue, 0, &cfg, &pool_rcu, &view->lcores[queue]};
      rte_eal_remote_launch(LcoreMain, &args[queue], lcore_id);
      ++queue;
    }

    std::printf("cerebellum_dataplane: %u queues running\n", n_queues);
    rte_eal_mp_wait_lcore();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 1;
  }
  return 0;
}
```

- [ ] **Update `dataplane/CMakeLists.txt`**

```cmake
add_library(dataplane_headers INTERFACE)
target_include_directories(dataplane_headers INTERFACE include)
target_link_libraries(dataplane_headers INTERFACE
    cerebellum_project_options
    cerebellum_compiler_warnings
    common ipc)

add_executable(cerebellum_dataplane src/main.cpp)
target_include_directories(cerebellum_dataplane PRIVATE
    include
    ${CMAKE_SOURCE_DIR}/controlplane/include)
target_link_libraries(cerebellum_dataplane PRIVATE
    dataplane_headers
    yaml-cpp::yaml-cpp
    fmt::fmt
    ${DPDK_LINK_LIBRARIES})
target_compile_options(cerebellum_dataplane PRIVATE ${DPDK_CFLAGS_OTHER})

if(CEREBELLUM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Build the dataplane binary**

```bash
cmake --build --preset release --target cerebellum_dataplane 2>&1 | tail -5
```

Expected: `[100%] Linking CXX executable cerebellum_dataplane`

- [ ] **Commit**

```bash
git add dataplane/include/io/ dataplane/src/ dataplane/CMakeLists.txt
git commit -m "feat: DPDK I/O (Eal, DpdkPort), dataplane main loop"
```

---

## Task 10: Controlplane — config + health checker

**Files:**
- Create: `controlplane/include/config/yaml_loader.hpp`
- Create: `controlplane/src/config/yaml_loader.cpp`
- Create: `controlplane/include/health/backend_state.hpp`
- Create: `controlplane/include/health/health_checker.hpp`
- Create: `controlplane/src/health/health_checker.cpp`
- Create: `controlplane/CMakeLists.txt` (full version)

- [ ] **Create source directories**

```bash
mkdir -p controlplane/include/{config,health,stats,api} \
         controlplane/src/{config,health,stats,api}
```

- [ ] **Write `controlplane/include/config/yaml_loader.hpp`**

```cpp
#pragma once
#include "common/config.hpp"
#include <string_view>

namespace cere::control {

// YAML format (see config.yaml for example):
//   vip, vip_port, self_mac, health_interval_ms, conntrack_timeout_ms, backends[]
common::Config LoadConfig(std::string_view path);

}  // namespace cere::control
```

- [ ] **Write `controlplane/src/config/yaml_loader.cpp`**

```cpp
#include "config/yaml_loader.hpp"
#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <format>
#include <cstdio>

namespace cere::control {

namespace {
common::MacAddr ParseMac(std::string_view s) {
  common::MacAddr m;
  unsigned b[6]{};
  if (std::sscanf(s.data(), "%x:%x:%x:%x:%x:%x",
        &b[0],&b[1],&b[2],&b[3],&b[4],&b[5]) != 6)
    throw std::invalid_argument(std::format("bad MAC: {}", s));
  for (int i = 0; i < 6; ++i)
    m.bytes[static_cast<std::size_t>(i)] = static_cast<uint8_t>(b[i]);
  return m;
}
}  // namespace

common::Config LoadConfig(std::string_view path) {
  YAML::Node root = YAML::LoadFile(std::string(path));
  common::Config cfg;
  cfg.vip      = common::IPv4Addr::FromString(root["vip"].as<std::string>());
  cfg.vip_port = root["vip_port"].as<uint16_t>();
  cfg.self_mac = ParseMac(root["self_mac"].as<std::string>());
  if (root["health_interval_ms"])
    cfg.health_interval_ms = root["health_interval_ms"].as<uint32_t>();
  if (root["conntrack_timeout_ms"])
    cfg.conntrack_timeout_ms = root["conntrack_timeout_ms"].as<uint32_t>();
  for (const auto& b : root["backends"]) {
    common::BackendInfo bi;
    bi.ip         = common::IPv4Addr::FromString(b["ip"].as<std::string>());
    bi.port       = b["port"].as<uint16_t>();
    bi.probe_port = b["probe_port"].as<uint16_t>();
    bi.mac        = ParseMac(b["mac"].as<std::string>());
    cfg.backends.push_back(bi);
  }
  return cfg;
}

}  // namespace cere::control
```

- [ ] **Write `controlplane/include/health/backend_state.hpp`**

```cpp
#pragma once
#include "common/config.hpp"
#include <cstdint>

namespace cere::control {

enum class HealthStatus : uint8_t { kUnknown, kUp, kDown, kDraining };

struct BackendState {
  common::BackendInfo info{};
  HealthStatus        status{HealthStatus::kUnknown};
  uint32_t            fail_count{};
  uint32_t            success_count{};
  uint64_t            last_check_ns{};
};

}  // namespace cere::control
```

- [ ] **Write `controlplane/include/health/health_checker.hpp`**

```cpp
#pragma once
#include "backend_state.hpp"
#include "lb/atomic_rcu.hpp"
#include "lb/backend_pool.hpp"
#include "common/config.hpp"
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

namespace cere::control {

class HealthChecker {
 public:
  HealthChecker(const common::Config& cfg,
                lb::AtomicRcu<lb::BackendPool>& pool_rcu);
  ~HealthChecker();

  void Start();
  void Stop();
  std::vector<BackendState> States() const;

 private:
  void RunLoop();
  bool Probe(const common::BackendInfo& b) noexcept;
  void RebuildPool();

  const common::Config&           cfg_;
  lb::AtomicRcu<lb::BackendPool>& pool_rcu_;
  std::vector<BackendState>       states_;
  mutable std::mutex              mu_;
  std::thread                     thread_;
  std::atomic<bool>               running_{false};
};

}  // namespace cere::control
```

- [ ] **Write `controlplane/src/health/health_checker.cpp`**

```cpp
#include "health/health_checker.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <chrono>

namespace cere::control {

HealthChecker::HealthChecker(const common::Config& cfg,
                              lb::AtomicRcu<lb::BackendPool>& pool_rcu)
    : cfg_(cfg), pool_rcu_(pool_rcu) {
  for (const auto& b : cfg_.backends)
    states_.push_back({b, HealthStatus::kUnknown, 0, 0, 0});
}

HealthChecker::~HealthChecker() { Stop(); }

void HealthChecker::Start() {
  running_ = true;
  thread_  = std::thread(&HealthChecker::RunLoop, this);
}

void HealthChecker::Stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

std::vector<BackendState> HealthChecker::States() const {
  std::lock_guard lock(mu_);
  return states_;
}

void HealthChecker::RunLoop() {
  while (running_) {
    bool changed = false;
    {
      std::lock_guard lock(mu_);
      for (auto& s : states_) {
        HealthStatus prev = s.status;
        s.status          = Probe(s.info) ? HealthStatus::kUp : HealthStatus::kDown;
        if (s.status == HealthStatus::kUp) ++s.success_count;
        else                               ++s.fail_count;
        using namespace std::chrono;
        s.last_check_ns = static_cast<uint64_t>(
            duration_cast<nanoseconds>(
                system_clock::now().time_since_epoch()).count());
        if (s.status != prev) changed = true;
      }
    }
    if (changed) RebuildPool();
    std::this_thread::sleep_for(
        std::chrono::milliseconds(cfg_.health_interval_ms));
  }
}

bool HealthChecker::Probe(const common::BackendInfo& b) noexcept {
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (fd < 0) return false;
  sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(b.ip.value);
  addr.sin_port        = htons(b.probe_port);
  bool ok = false;
  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
    ok = true;
  } else if (errno == EINPROGRESS) {
    fd_set wfds; FD_ZERO(&wfds); FD_SET(fd, &wfds);
    timeval tv{1, 0};
    if (select(fd + 1, nullptr, &wfds, nullptr, &tv) > 0) {
      int err = 0; socklen_t len = sizeof(err);
      getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
      ok = (err == 0);
    }
  }
  close(fd);
  return ok;
}

void HealthChecker::RebuildPool() {
  std::vector<common::BackendInfo> active;
  {
    std::lock_guard lock(mu_);
    for (const auto& s : states_)
      if (s.status == HealthStatus::kUp) active.push_back(s.info);
  }
  pool_rcu_.Store(std::make_shared<lb::BackendPool>(std::move(active)));
}

}  // namespace cere::control
```

- [ ] **Write `controlplane/CMakeLists.txt` (stub — will be completed in Task 11)**

```cmake
add_executable(cerebellum_controlplane
    src/config/yaml_loader.cpp
    src/health/health_checker.cpp
    src/stats/stats_aggregator.cpp
    src/api/rest_server.cpp
    src/main.cpp
)
target_include_directories(cerebellum_controlplane PRIVATE
    include
    ${CMAKE_SOURCE_DIR}/dataplane/include)
target_link_libraries(cerebellum_controlplane PRIVATE
    cerebellum_project_options
    cerebellum_compiler_warnings
    common ipc
    yaml-cpp::yaml-cpp
    fmt::fmt
    Crow::Crow)
```

- [ ] **Create stub source files**

```bash
touch controlplane/src/stats/stats_aggregator.cpp \
      controlplane/src/api/rest_server.cpp
printf 'int main(){return 0;}\n' > controlplane/src/main.cpp
```

- [ ] **Build controlplane**

```bash
cmake --build --preset release --target cerebellum_controlplane 2>&1 | tail -5
```

Expected: `Built target cerebellum_controlplane`

- [ ] **Commit**

```bash
git add controlplane/
git commit -m "feat: controlplane scaffold — YamlLoader, HealthChecker"
```

---

## Task 11: Controlplane — StatsAggregator + REST API + main

**Files:**
- Create: `controlplane/include/stats/stats_aggregator.hpp`
- Replace: `controlplane/src/stats/stats_aggregator.cpp`
- Create: `controlplane/include/api/rest_server.hpp`
- Replace: `controlplane/src/api/rest_server.cpp`
- Replace: `controlplane/src/main.cpp`

- [ ] **Write `controlplane/include/stats/stats_aggregator.hpp`**

```cpp
#pragma once
#include "ipc/lcore_stats.hpp"
#include <cstdint>

namespace cere::control {

struct AggregatedStats {
  uint64_t rx_pps{};
  uint64_t tx_pps{};
  uint64_t dropped_pps{};
  uint64_t new_flows_ps{};
  uint64_t active_flows{};
  uint64_t backend_flows[ipc::kMaxBackends]{};
};

class StatsAggregator {
 public:
  StatsAggregator(const ipc::StatsView& view, uint32_t n_lcores)
      : view_(view), n_lcores_(n_lcores) {}

  // Call once per second. Returns rates since last call.
  AggregatedStats Tick() noexcept;

 private:
  struct Snapshot {
    uint64_t rx{}, tx{}, dropped{}, new_flows{};
  };

  const ipc::StatsView& view_;
  uint32_t              n_lcores_;
  Snapshot              prev_{};
};

}  // namespace cere::control
```

- [ ] **Write `controlplane/src/stats/stats_aggregator.cpp`**

```cpp
#include "stats/stats_aggregator.hpp"

namespace cere::control {

AggregatedStats StatsAggregator::Tick() noexcept {
  Snapshot cur{};
  uint64_t active = 0;
  AggregatedStats out;

  for (uint32_t l = 0; l < n_lcores_; ++l) {
    const auto& ls = view_.lcores[l];
    cur.rx        += ls.rx_packets.value.load(std::memory_order_relaxed);
    cur.tx        += ls.tx_packets.value.load(std::memory_order_relaxed);
    cur.dropped   += ls.dropped.value.load(std::memory_order_relaxed);
    cur.new_flows += ls.new_flows.value.load(std::memory_order_relaxed);
    active        += ls.active_flows.value.load(std::memory_order_relaxed);
    for (uint32_t b = 0; b < ipc::kMaxBackends; ++b)
      out.backend_flows[b] +=
          ls.backend_flows[b].value.load(std::memory_order_relaxed);
  }

  out.rx_pps       = cur.rx        - prev_.rx;
  out.tx_pps       = cur.tx        - prev_.tx;
  out.dropped_pps  = cur.dropped   - prev_.dropped;
  out.new_flows_ps = cur.new_flows - prev_.new_flows;
  out.active_flows = active;
  prev_ = cur;
  return out;
}

}  // namespace cere::control
```

- [ ] **Write `controlplane/include/api/rest_server.hpp`**

```cpp
#pragma once
#include "stats/stats_aggregator.hpp"
#include "health/health_checker.hpp"
#include "common/config.hpp"

namespace cere::control {

class RestServer {
 public:
  RestServer(uint16_t port,
             const common::Config& cfg,
             StatsAggregator& aggregator,
             HealthChecker& health);
  void Run();   // blocks until Stop()
  void Stop();

 private:
  uint16_t              port_;
  const common::Config& cfg_;
  StatsAggregator&      aggregator_;
  HealthChecker&        health_;
};

}  // namespace cere::control
```

- [ ] **Write `controlplane/src/api/rest_server.cpp`**

```cpp
#include "api/rest_server.hpp"
#include <crow.h>
#include <chrono>

namespace cere::control {

RestServer::RestServer(uint16_t port, const common::Config& cfg,
                       StatsAggregator& agg, HealthChecker& health)
    : port_(port), cfg_(cfg), aggregator_(agg), health_(health) {}

void RestServer::Run() {
  crow::SimpleApp app;

  CROW_ROUTE(app, "/api/stats")([this] {
    auto s = aggregator_.Tick();
    crow::json::wvalue r;
    r["rx_pps"]       = s.rx_pps;
    r["tx_pps"]       = s.tx_pps;
    r["dropped_pps"]  = s.dropped_pps;
    r["new_flows_ps"] = s.new_flows_ps;
    r["active_flows"] = s.active_flows;
    return crow::response{r};
  });

  CROW_ROUTE(app, "/api/backends")([this] {
    auto states = health_.States();
    crow::json::wvalue r;
    r["vip"]      = cfg_.vip.ToString();
    r["vip_port"] = cfg_.vip_port;

    std::vector<crow::json::wvalue> backends;
    for (const auto& s : states) {
      crow::json::wvalue b;
      b["ip"]   = s.info.ip.ToString();
      b["port"] = s.info.port;
      b["status"] = [&]() -> std::string {
        switch (s.status) {
          case HealthStatus::kUp:       return "up";
          case HealthStatus::kDown:     return "down";
          case HealthStatus::kDraining: return "draining";
          default:                      return "unknown";
        }
      }();
      b["fail_count"] = s.fail_count;
      using namespace std::chrono;
      auto now_ms = duration_cast<milliseconds>(
          system_clock::now().time_since_epoch()).count();
      auto last_ms = static_cast<int64_t>(s.last_check_ns / 1'000'000);
      b["last_check_ms"] = now_ms > last_ms ? now_ms - last_ms : 0;
      backends.push_back(std::move(b));
    }
    r["backends"] = std::move(backends);
    return crow::response{r};
  });

  app.port(port_).multithreaded().run();
}

void RestServer::Stop() {}

}  // namespace cere::control
```

- [ ] **Write `controlplane/src/main.cpp`**

```cpp
#include "config/yaml_loader.hpp"
#include "health/health_checker.hpp"
#include "stats/stats_aggregator.hpp"
#include "api/rest_server.hpp"
#include "lb/atomic_rcu.hpp"
#include "lb/backend_pool.hpp"
#include "ipc/shm_provider.hpp"
#include "ipc/lcore_stats.hpp"
#include <cstdio>
#include <stdexcept>
#include <csignal>

namespace { volatile std::sig_atomic_t g_stop = 0; }

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr,"Usage: %s <config.yaml>\n",argv[0]); return 1; }
  std::signal(SIGINT, [](int){ g_stop = 1; });
  std::signal(SIGTERM,[](int){ g_stop = 1; });
  try {
    auto cfg = cere::control::LoadConfig(argv[1]);

    cere::ipc::ShmProvider shm("/cerebellum_stats",
        sizeof(cere::ipc::StatsView), cere::ipc::ShmProvider::Mode::kAttach);
    auto* view = shm.As<cere::ipc::StatsView>();

    cere::lb::AtomicRcu<cere::lb::BackendPool> pool_rcu;
    cere::control::HealthChecker health(cfg, pool_rcu);
    health.Start();

    cere::control::StatsAggregator agg(*view, cere::ipc::kMaxLcores);
    cere::control::RestServer api(8080, cfg, agg, health);

    std::printf("cerebellum_controlplane: listening on :8080\n");
    api.Run();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 1;
  }
}
```

- [ ] **Build controlplane**

```bash
cmake --build --preset release --target cerebellum_controlplane 2>&1 | tail -5
```

Expected: `Built target cerebellum_controlplane`

- [ ] **Create sample `config.yaml` and smoke-test the API**

```bash
cat > config.yaml << 'EOF'
vip: "10.0.0.1"
vip_port: 80
self_mac: "aa:bb:cc:dd:ee:ff"
health_interval_ms: 3000
conntrack_timeout_ms: 30000
backends:
  - ip: "10.0.1.1"
    port: 8080
    probe_port: 8081
    mac: "52:54:00:11:22:33"
  - ip: "10.0.1.2"
    port: 8080
    probe_port: 8081
    mac: "52:54:00:44:55:66"
EOF

# Run controlplane (shm attach will fail if dataplane not running — use kCreate for test)
# Quick API test without shared memory: adjust Mode in main.cpp to kCreate temporarily
./build/cerebellum_controlplane config.yaml &
sleep 1
curl -s localhost:8080/api/stats   | python3 -m json.tool
curl -s localhost:8080/api/backends | python3 -m json.tool
kill %1
```

Expected: both endpoints return valid JSON.

- [ ] **Commit**

```bash
git add controlplane/ config.yaml
git commit -m "feat: controlplane — StatsAggregator, REST API, main"
```

---

## Task 12: React frontend

**Files:**
- Create: `ui/` (Vite scaffold)
- Create: `ui/src/types.ts`
- Create: `ui/src/components/Navbar.tsx`
- Create: `ui/src/components/StatsBar.tsx`
- Create: `ui/src/components/BackendsTable.tsx`
- Replace: `ui/src/App.tsx`

- [ ] **Scaffold Vite + React + TypeScript**

```bash
cd ui
npm create vite@latest . -- --template react-ts
npm install
npm install -D tailwindcss @tailwindcss/vite
```

- [ ] **Configure Tailwind in `ui/vite.config.ts`**

```ts
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

export default defineConfig({
  plugins: [react(), tailwindcss()],
  server: { proxy: { '/api': 'http://localhost:8080' } },
})
```

- [ ] **Replace `ui/src/index.css`**

```css
@import "tailwindcss";
```

- [ ] **Write `ui/src/types.ts`**

```ts
export interface Stats {
  rx_pps: number; tx_pps: number
  dropped_pps: number; new_flows_ps: number; active_flows: number
}
export interface Backend {
  ip: string; port: number
  status: 'up' | 'down' | 'draining' | 'unknown'
  fail_count: number; last_check_ms: number
}
export interface BackendsResponse {
  vip: string; vip_port: number; backends: Backend[]
}
```

- [ ] **Write `ui/src/components/Navbar.tsx`**

```tsx
export function Navbar({ running }: { running: boolean }) {
  return (
    <header className="bg-white border-b border-slate-200 px-6 py-3 flex justify-between items-center">
      <div className="flex items-center gap-3">
        <span className="font-bold text-slate-900 text-lg">⬡ Cerebellum</span>
        <span className="text-slate-400 text-sm">L4 Load Balancer</span>
      </div>
      <div className="flex items-center gap-3 text-sm">
        <span className="text-slate-500">DPDK · RSS</span>
        <span className={`px-3 py-0.5 rounded-full text-xs font-medium
          ${running ? 'bg-green-100 text-green-800' : 'bg-red-100 text-red-800'}`}>
          {running ? '● Running' : '● Offline'}
        </span>
      </div>
    </header>
  )
}
```

- [ ] **Write `ui/src/components/StatsBar.tsx`**

```tsx
import type { Stats } from '../types'

function Card({ label, value, color }: { label: string; value: number; color: string }) {
  return (
    <div className="bg-white border border-slate-200 rounded-lg p-4 text-center">
      <div className={`text-xl font-bold ${color}`}>{value.toLocaleString()}</div>
      <div className="text-slate-400 text-xs mt-1">{label}</div>
    </div>
  )
}

export function StatsBar({ stats }: { stats: Stats | null }) {
  const s = stats ?? { rx_pps:0, tx_pps:0, dropped_pps:0, new_flows_ps:0, active_flows:0 }
  return (
    <div className="grid grid-cols-5 gap-3 px-6 py-4 border-b border-slate-200">
      <Card label="RX packets/s"  value={s.rx_pps}       color="text-sky-500" />
      <Card label="TX packets/s"  value={s.tx_pps}       color="text-indigo-500" />
      <Card label="Active flows"  value={s.active_flows}  color="text-amber-500" />
      <Card label="New flows/s"   value={s.new_flows_ps}  color="text-emerald-500" />
      <Card label="Dropped/s"     value={s.dropped_pps}   color="text-red-500" />
    </div>
  )
}
```

- [ ] **Write `ui/src/components/BackendsTable.tsx`**

```tsx
import type { BackendsResponse } from '../types'

const badge: Record<string, string> = {
  up:       'bg-green-100 text-green-800',
  down:     'bg-red-100 text-red-800',
  draining: 'bg-yellow-100 text-yellow-800',
  unknown:  'bg-slate-100 text-slate-500',
}

export function BackendsTable({ data }: { data: BackendsResponse | null }) {
  if (!data) return null
  return (
    <div className="px-6 py-4">
      <div className="flex items-center gap-3 mb-3">
        <span className="font-semibold text-slate-900">VIP {data.vip}:{data.vip_port}</span>
        <span className="bg-slate-100 text-slate-500 text-xs px-2 py-0.5 rounded">
          {data.backends.length} backends
        </span>
      </div>
      <table className="w-full text-sm bg-white border border-slate-200 rounded-lg overflow-hidden">
        <thead>
          <tr className="bg-slate-50 text-slate-500 border-b border-slate-200 text-left">
            {['Backend','Health','Failures','Last check'].map(h =>
              <td key={h} className="px-4 py-2 font-medium">{h}</td>)}
          </tr>
        </thead>
        <tbody>
          {data.backends.map(b => (
            <tr key={`${b.ip}:${b.port}`}
                className="border-t border-slate-100 hover:bg-slate-50 transition-colors">
              <td className="px-4 py-2.5 font-medium text-slate-800">{b.ip}:{b.port}</td>
              <td className="px-4 py-2.5">
                <span className={`px-2 py-0.5 rounded text-xs font-medium ${badge[b.status] ?? badge.unknown}`}>
                  {b.status.toUpperCase()}
                </span>
              </td>
              <td className="px-4 py-2.5 text-slate-600">{b.fail_count}</td>
              <td className="px-4 py-2.5 text-slate-400">
                {b.last_check_ms < 2000
                  ? `${b.last_check_ms}ms ago`
                  : `${(b.last_check_ms/1000).toFixed(1)}s ago`}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}
```

- [ ] **Write `ui/src/App.tsx`**

```tsx
import { useEffect, useState } from 'react'
import { Navbar } from './components/Navbar'
import { StatsBar } from './components/StatsBar'
import { BackendsTable } from './components/BackendsTable'
import type { Stats, BackendsResponse } from './types'

export default function App() {
  const [stats, setStats]       = useState<Stats | null>(null)
  const [backends, setBackends] = useState<BackendsResponse | null>(null)
  const [online, setOnline]     = useState(false)

  useEffect(() => {
    const poll = async () => {
      try {
        const [sr, br] = await Promise.all([
          fetch('/api/stats'), fetch('/api/backends'),
        ])
        if (sr.ok && br.ok) {
          setStats(await sr.json())
          setBackends(await br.json())
          setOnline(true)
        } else { setOnline(false) }
      } catch { setOnline(false) }
    }
    poll()
    const id = setInterval(poll, 1000)
    return () => clearInterval(id)
  }, [])

  return (
    <div className="min-h-screen bg-slate-50 font-sans">
      <Navbar running={online} />
      <StatsBar stats={stats} />
      <BackendsTable data={backends} />
    </div>
  )
}
```

- [ ] **Start dev server and verify**

```bash
cd ui && npm run dev
# Open http://localhost:5173
# Expect: Navbar shows "● Offline", stats show 0, no console errors
```

- [ ] **Build production bundle**

```bash
npm run build
```

Expected: `dist/` created, no TypeScript errors, no warnings.

- [ ] **Commit**

```bash
cd .. && git add ui/
git commit -m "feat: React dashboard — Navbar, StatsBar, BackendsTable, 1s polling"
```

---

## Task 13: Docker Compose + nginx

**Files:**
- Create: `dataplane/Dockerfile`
- Create: `controlplane/Dockerfile`
- Create: `ui/Dockerfile`
- Create: `nginx/Dockerfile`
- Create: `nginx/conf/cerebellum.conf`
- Create: `docker-compose.yml`

- [ ] **Write `dataplane/Dockerfile`**

```dockerfile
FROM ubuntu:24.04 AS builder
RUN apt-get update && apt-get install -y \
    build-essential cmake ninja-build pkg-config git dpdk-dev libdpdk-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake --preset release -G Ninja \
 && cmake --build --preset release --target cerebellum_dataplane

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y libdpdk-dev && rm -rf /var/lib/apt/lists/*
COPY --from=builder /src/build/cerebellum_dataplane /usr/local/bin/
COPY config.yaml /etc/cerebellum/config.yaml
ENTRYPOINT ["cerebellum_dataplane", "/etc/cerebellum/config.yaml", "--", "-c", "0x3", "-n", "4"]
```

- [ ] **Write `controlplane/Dockerfile`**

```dockerfile
FROM ubuntu:24.04 AS builder
RUN apt-get update && apt-get install -y \
    build-essential cmake ninja-build pkg-config git \
    libyaml-cpp-dev dpdk-dev libdpdk-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake --preset release -G Ninja \
 && cmake --build --preset release --target cerebellum_controlplane

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y libyaml-cpp0.8 && rm -rf /var/lib/apt/lists/*
COPY --from=builder /src/build/cerebellum_controlplane /usr/local/bin/
COPY config.yaml /etc/cerebellum/config.yaml
ENTRYPOINT ["cerebellum_controlplane", "/etc/cerebellum/config.yaml"]
```

- [ ] **Write `ui/Dockerfile`**

```dockerfile
FROM node:22-alpine AS builder
WORKDIR /app
COPY ui/package*.json ./
RUN npm ci
COPY ui/ .
RUN npm run build

FROM alpine:3.20
COPY --from=builder /app/dist /dist
VOLUME /out
CMD ["sh", "-c", "cp -r /dist/. /out/"]
```

- [ ] **Write `nginx/conf/cerebellum.conf`**

```nginx
server {
    listen 80;
    server_name _;
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl;
    server_name _;
    ssl_certificate     /etc/letsencrypt/live/default/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/default/privkey.pem;

    root /usr/share/nginx/html;
    index index.html;

    location /api/ {
        proxy_pass http://controlplane:8080;
        proxy_set_header Host $host;
    }

    location / {
        try_files $uri $uri/ /index.html;
    }
}
```

- [ ] **Write `nginx/Dockerfile`**

```dockerfile
FROM nginx:1.27-alpine
COPY nginx/conf/cerebellum.conf /etc/nginx/conf.d/default.conf
```

- [ ] **Write `docker-compose.yml`**

```yaml
services:
  dataplane:
    build:
      context: .
      dockerfile: dataplane/Dockerfile
    privileged: true
    network_mode: host
    volumes:
      - /dev/hugepages:/dev/hugepages
    devices:
      - /dev/vfio:/dev/vfio
    restart: unless-stopped

  controlplane:
    build:
      context: .
      dockerfile: controlplane/Dockerfile
    expose: ["8080"]
    depends_on: [dataplane]
    ipc: host
    restart: unless-stopped

  ui:
    build:
      context: .
      dockerfile: ui/Dockerfile
    volumes:
      - ui-dist:/out

  nginx:
    build:
      context: .
      dockerfile: nginx/Dockerfile
    ports: ["80:80", "443:443"]
    volumes:
      - ./nginx/conf:/etc/nginx/conf.d
      - certbot-data:/etc/letsencrypt
      - ui-dist:/usr/share/nginx/html
    depends_on: [controlplane, ui]
    restart: unless-stopped

volumes:
  certbot-data:
  ui-dist:
```

- [ ] **Build all images**

```bash
docker compose build 2>&1 | grep -E "^(#|ERROR)" | tail -20
```

Expected: no `ERROR` lines.

- [ ] **Smoke-test controlplane + nginx (no dataplane)**

```bash
docker compose up controlplane ui nginx -d
sleep 3
# nginx forwards /api/* to controlplane
curl -sk --resolve localhost:443:127.0.0.1 https://localhost/api/backends \
  | python3 -m json.tool
docker compose down
```

Expected: valid JSON backends response.

- [ ] **Commit**

```bash
git add dataplane/Dockerfile controlplane/Dockerfile ui/Dockerfile nginx/ docker-compose.yml
git commit -m "feat: Docker Compose stack — dataplane, controlplane, ui, nginx"
```

---

## Verification checklist

```bash
# All unit tests pass
just test

# Standalone API (needs running controlplane)
./build/cerebellum_controlplane config.yaml &
sleep 1
curl localhost:8080/api/stats
curl localhost:8080/api/backends
kill %1

# Frontend dev mode
cd ui && npm run dev
# http://localhost:5173 — shows offline dashboard

# Partial Docker stack (no DPDK hardware required)
docker compose up controlplane ui nginx

# Full stack (bare-metal with hugepages + DPDK NIC)
docker compose up
# open https://<server>/
```
