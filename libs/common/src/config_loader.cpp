#include "common/config_loader.hpp"

#include <yaml-cpp/yaml.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <format>
#include <stdexcept>
#include <string>

namespace cere::common {

namespace {

constexpr int kMacOctets = 6;
constexpr unsigned kMaxOctetValue = 0xFFU;

MacAddr ParseMac(const std::string& text) {
  std::array<unsigned, kMacOctets> octets{};
  unsigned* oct = octets.data();
  const int parsed = std::sscanf(text.c_str(), "%x:%x:%x:%x:%x:%x", &oct[0],
                                 &oct[1], &oct[2], &oct[3], &oct[4], &oct[5]);
  if (parsed != kMacOctets) {
    throw std::invalid_argument(std::format("bad MAC: {}", text));
  }
  MacAddr mac;
  for (std::size_t i = 0; i < kMacOctets; ++i) {
    if (octets[i] > kMaxOctetValue) {
      throw std::invalid_argument(
          std::format("MAC octet out of range: {}", text));
    }
    mac.bytes[i] = static_cast<uint8_t>(octets[i]);
  }
  return mac;
}

BackendInfo ParseBackend(const YAML::Node& node) {
  BackendInfo backend;
  backend.ip = IPv4Addr::FromString(node["ip"].as<std::string>());
  backend.port = node["port"].as<uint16_t>();
  backend.probe_port = node["probe_port"].as<uint16_t>();
  backend.mac = ParseMac(node["mac"].as<std::string>());
  return backend;
}

}

Config LoadConfig(std::string_view path) {
  const YAML::Node root = YAML::LoadFile(std::string(path));
  Config cfg;
  cfg.vip = IPv4Addr::FromString(root["vip"].as<std::string>());
  cfg.vip_port = root["vip_port"].as<uint16_t>();
  cfg.self_ip = IPv4Addr::FromString(root["self_ip"].as<std::string>());
  cfg.self_mac = ParseMac(root["self_mac"].as<std::string>());
  if (root["health_interval_ms"]) {
    cfg.health_interval_ms = root["health_interval_ms"].as<uint32_t>();
  }
  if (root["conntrack_timeout_ms"]) {
    cfg.conntrack_timeout_ms = root["conntrack_timeout_ms"].as<uint32_t>();
  }
  for (const auto& node : root["backends"]) {
    cfg.backends.push_back(ParseBackend(node));
  }
  return cfg;
}

}
