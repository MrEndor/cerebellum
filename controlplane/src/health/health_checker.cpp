#include "health/health_checker.hpp"

#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace cere::control {

namespace {

constexpr int kProbeTimeoutSec = 1;

uint64_t NowNs() noexcept {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<nanoseconds>(system_clock::now().time_since_epoch())
          .count());
}

bool WaitForConnect(int fdesc) noexcept {
  fd_set write_set;
  FD_ZERO(&write_set);
  FD_SET(fdesc, &write_set);
  timeval timeout{.tv_sec = kProbeTimeoutSec, .tv_usec = 0};
  if (select(fdesc + 1, nullptr, &write_set, nullptr, &timeout) <= 0) {
    return false;
  }
  int err = 0;
  socklen_t len = sizeof(err);
  getsockopt(fdesc, SOL_SOCKET, SO_ERROR, &err, &len);
  return err == 0;
}

}

HealthChecker::HealthChecker(const common::Config& cfg) : cfg_(cfg) {
  for (const auto& backend : cfg_.backends) {
    states_.push_back({backend, HealthStatus::kUnknown, 0, 0, 0});
  }
}

HealthChecker::~HealthChecker() { Stop(); }

void HealthChecker::Start() {
  running_.store(true, std::memory_order_relaxed);
  thread_ = std::thread(&HealthChecker::RunLoop, this);
}

void HealthChecker::Stop() {
  running_.store(false, std::memory_order_relaxed);
  if (thread_.joinable()) {
    thread_.join();
  }
}

std::vector<BackendState> HealthChecker::States() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return states_;
}

void HealthChecker::RunLoop() {
  while (running_.load(std::memory_order_relaxed)) {
    std::vector<BackendState> snapshot;
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      snapshot = states_;
    }
    for (auto& state : snapshot) {
      const bool healthy = Probe(state.info);
      state.status = healthy ? HealthStatus::kUp : HealthStatus::kDown;
      if (healthy) {
        ++state.success_count;
      } else {
        ++state.fail_count;
      }
      state.last_check_ns = NowNs();
    }
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      states_ = std::move(snapshot);
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(cfg_.health_interval_ms));
  }
}

bool HealthChecker::Probe(const common::BackendInfo& backend) noexcept {
  const int fdesc = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (fdesc < 0) {
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(backend.ip.value);
  addr.sin_port = htons(backend.probe_port);

  bool healthy = false;
  const int rc =
      connect(fdesc, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (rc == 0) {
    healthy = true;
  } else if (errno == EINPROGRESS) {
    healthy = WaitForConnect(fdesc);
  }
  close(fdesc);
  return healthy;
}

}
