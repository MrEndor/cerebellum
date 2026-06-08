#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "common/config.hpp"
#include "health/backend_state.hpp"

namespace cere::control {

class HealthChecker {
 public:
  explicit HealthChecker(const common::Config& cfg);
  ~HealthChecker();

  HealthChecker(const HealthChecker&) = delete;
  HealthChecker& operator=(const HealthChecker&) = delete;
  HealthChecker(HealthChecker&&) = delete;
  HealthChecker& operator=(HealthChecker&&) = delete;

  void Start();
  void Stop();

  std::vector<BackendState> States() const;

 private:
  void RunLoop();
  static bool Probe(const common::BackendInfo& backend) noexcept;

  const common::Config& cfg_;
  std::vector<BackendState> states_;
  mutable std::mutex mutex_;
  std::thread thread_;
  std::atomic<bool> running_{false};
};

}
