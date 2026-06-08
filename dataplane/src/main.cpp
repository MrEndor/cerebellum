#include <rte_launch.h>
#include <rte_lcore.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "common/config.hpp"
#include "common/config_loader.hpp"
#include "graph/dispatcher.hpp"
#include "graph/graph.hpp"
#include "io/dpdk_port.hpp"
#include "io/eal.hpp"
#include "io/ring.hpp"
#include "ipc/control_view.hpp"
#include "ipc/lcore_stats.hpp"
#include "ipc/shm_provider.hpp"
#include "lb/atomic_rcu.hpp"
#include "lb/backend_pool.hpp"
#include "nodes/arp_node.hpp"
#include "nodes/dnat_node.hpp"
#include "nodes/drop_node.hpp"
#include "nodes/ether_rewrite_node.hpp"
#include "nodes/ip4_parse_node.hpp"
#include "nodes/lb_node.hpp"
#include "nodes/ring_rx_node.hpp"
#include "nodes/ring_tx_node.hpp"
#include "nodes/rx_node.hpp"
#include "nodes/steer_node.hpp"
#include "nodes/tx_node.hpp"

namespace {
using namespace cere;

std::atomic<bool> g_running{true};

void OnSignal(int) { g_running.store(false, std::memory_order_relaxed); }

constexpr char kControlShmName[] = "/cerebellum_control";
constexpr auto kControlPollInterval = std::chrono::milliseconds{200};

constexpr unsigned kRingSize = 1024;
constexpr unsigned kEgressRingSize = 2048;

std::shared_ptr<lb::BackendPool> BuildPool(const ipc::ControlBackend* src,
                                           uint32_t count) {
  std::vector<common::BackendInfo> slots;
  std::vector<uint8_t> enabled;
  slots.reserve(count);
  enabled.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    common::BackendInfo backend;
    backend.ip.value = src[i].ip;
    backend.port = src[i].port;
    std::copy(std::begin(src[i].mac), std::end(src[i].mac),
              backend.mac.bytes.begin());
    slots.push_back(backend);
    enabled.push_back(src[i].enabled);
  }
  return std::make_shared<lb::BackendPool>(std::move(slots), enabled);
}

void SeedControl(ipc::ControlView& view, const common::Config& cfg) {
  std::vector<ipc::ControlBackend> entries;
  entries.reserve(cfg.backends.size());
  for (const common::BackendInfo& backend : cfg.backends) {
    ipc::ControlBackend entry;
    entry.ip = backend.ip.value;
    entry.port = backend.port;
    std::copy(backend.mac.bytes.begin(), backend.mac.bytes.end(),
              std::begin(entry.mac));
    entry.enabled = 1;
    entries.push_back(entry);
  }
  ipc::PublishControl(view, entries.data(),
                      static_cast<uint32_t>(entries.size()));
}

enum class Role : uint8_t { kMono, kIo, kWorker };

struct LcoreArg {
  Role role;
  uint16_t port_id;
  uint16_t worker_id;
  uint32_t n_workers;
  const common::Config* cfg;
  lb::AtomicRcu<lb::BackendPool>* pool_rcu;
  ipc::LcoreStats* stats;
  rte_ring** worker_rings;
  rte_ring* ingress;
  rte_ring* egress;
};

graph::Graph::NodeId BuildMonolithicGraph(graph::Graph& graph,
                                          const LcoreArg& arg) {
  const auto rx_id = graph.AddNode(
      std::make_unique<nodes::RxNode>(arg.port_id, 0, *arg.stats));
  const auto parse =
      graph.AddNode(std::make_unique<nodes::Ip4ParseNode>(*arg.cfg));
  const auto load_balance = graph.AddNode(std::make_unique<nodes::LbNode>(
      *arg.cfg, *arg.pool_rcu, *arg.stats, 0, 1));
  const auto dnat = graph.AddNode(std::make_unique<nodes::DnatNode>(*arg.cfg));
  const auto ether =
      graph.AddNode(std::make_unique<nodes::EtherRewriteNode>(*arg.cfg));
  const auto tx_id = graph.AddNode(
      std::make_unique<nodes::TxNode>(arg.port_id, 0, *arg.stats));
  const auto drop = graph.AddNode(std::make_unique<nodes::DropNode>());
  const auto arp = graph.AddNode(std::make_unique<nodes::ArpNode>(*arg.cfg));

  graph.Wire(rx_id, 0, parse);
  graph.Wire(parse, nodes::Ip4ParseNode::kNextFwd, load_balance);
  graph.Wire(parse, nodes::Ip4ParseNode::kNextRev, load_balance);
  graph.Wire(parse, nodes::Ip4ParseNode::kNextDrop, drop);
  graph.Wire(parse, nodes::Ip4ParseNode::kNextArp, arp);
  graph.Wire(arp, nodes::ArpNode::kNextTx, tx_id);
  graph.Wire(arp, nodes::ArpNode::kNextDrop, drop);
  graph.Wire(load_balance, nodes::LbNode::kNextDnat, dnat);
  graph.Wire(load_balance, nodes::LbNode::kNextDrop, drop);
  graph.Wire(dnat, 0, ether);
  graph.Wire(ether, 0, tx_id);
  return rx_id;
}

graph::Graph::NodeId BuildRxGraph(graph::Graph& graph, const LcoreArg& arg) {
  const auto rx_id = graph.AddNode(
      std::make_unique<nodes::RxNode>(arg.port_id, 0, *arg.stats));
  const auto steer = graph.AddNode(std::make_unique<nodes::SteerNode>(
      arg.worker_rings, arg.n_workers, *arg.cfg, *arg.stats));
  const auto arp = graph.AddNode(std::make_unique<nodes::ArpNode>(*arg.cfg));
  const auto egress = graph.AddNode(
      std::make_unique<nodes::RingTxNode>(arg.egress, *arg.stats));
  const auto drop = graph.AddNode(std::make_unique<nodes::DropNode>());

  graph.Wire(rx_id, 0, steer);
  graph.Wire(steer, nodes::SteerNode::kNextArp, arp);
  graph.Wire(arp, nodes::ArpNode::kNextTx, egress);
  graph.Wire(arp, nodes::ArpNode::kNextDrop, drop);
  return rx_id;
}

graph::Graph::NodeId BuildWorkerGraph(graph::Graph& graph,
                                      const LcoreArg& arg) {
  const auto rx_id =
      graph.AddNode(std::make_unique<nodes::RingRxNode>(arg.ingress));
  const auto parse =
      graph.AddNode(std::make_unique<nodes::Ip4ParseNode>(*arg.cfg));
  const auto load_balance = graph.AddNode(std::make_unique<nodes::LbNode>(
      *arg.cfg, *arg.pool_rcu, *arg.stats, arg.worker_id,
      static_cast<uint16_t>(arg.n_workers)));
  const auto dnat = graph.AddNode(std::make_unique<nodes::DnatNode>(*arg.cfg));
  const auto ether =
      graph.AddNode(std::make_unique<nodes::EtherRewriteNode>(*arg.cfg));
  const auto egress = graph.AddNode(
      std::make_unique<nodes::RingTxNode>(arg.egress, *arg.stats));
  const auto drop = graph.AddNode(std::make_unique<nodes::DropNode>());

  graph.Wire(rx_id, 0, parse);
  graph.Wire(parse, nodes::Ip4ParseNode::kNextFwd, load_balance);
  graph.Wire(parse, nodes::Ip4ParseNode::kNextRev, load_balance);
  graph.Wire(parse, nodes::Ip4ParseNode::kNextDrop, drop);
  graph.Wire(parse, nodes::Ip4ParseNode::kNextArp, drop);
  graph.Wire(load_balance, nodes::LbNode::kNextDnat, dnat);
  graph.Wire(load_balance, nodes::LbNode::kNextDrop, drop);
  graph.Wire(dnat, 0, ether);
  graph.Wire(ether, 0, egress);
  return rx_id;
}

graph::Graph::NodeId BuildTxGraph(graph::Graph& graph, const LcoreArg& arg) {
  const auto rx_id =
      graph.AddNode(std::make_unique<nodes::RingRxNode>(arg.egress));
  const auto tx_id = graph.AddNode(
      std::make_unique<nodes::TxNode>(arg.port_id, 0, *arg.stats));
  graph.Wire(rx_id, 0, tx_id);
  return rx_id;
}

void RunMono(const LcoreArg& arg) {
  graph::Graph graph;
  graph::Dispatcher dispatcher(graph, BuildMonolithicGraph(graph, arg));
  graph::Frame frame;
  while (g_running.load(std::memory_order_relaxed)) {
    frame.Clear();
    dispatcher.RunOnce(frame);
  }
}

void RunWorker(const LcoreArg& arg) {
  graph::Graph graph;
  graph::Dispatcher dispatcher(graph, BuildWorkerGraph(graph, arg));
  graph::Frame frame;
  while (g_running.load(std::memory_order_relaxed)) {
    frame.Clear();
    dispatcher.RunOnce(frame);
  }
}

void RunIo(const LcoreArg& arg) {
  graph::Graph rx_graph;
  graph::Graph tx_graph;
  graph::Dispatcher rx_disp(rx_graph, BuildRxGraph(rx_graph, arg));
  graph::Dispatcher tx_disp(tx_graph, BuildTxGraph(tx_graph, arg));
  graph::Frame frame;
  while (g_running.load(std::memory_order_relaxed)) {
    frame.Clear();
    rx_disp.RunOnce(frame);
    frame.Clear();
    tx_disp.RunOnce(frame);
  }
}

int LcoreMain(void* raw) {
  auto* arg = static_cast<LcoreArg*>(raw);
  switch (arg->role) {
    case Role::kMono:
      RunMono(*arg);
      break;
    case Role::kIo:
      RunIo(*arg);
      break;
    case Role::kWorker:
      RunWorker(*arg);
      break;
  }
  return 0;
}

void RunControlLoop(const ipc::ControlView& control,
                    lb::AtomicRcu<lb::BackendPool>& pool_rcu,
                    uint32_t applied_seq) {
  std::vector<ipc::ControlBackend> snapshot(ipc::kMaxBackends);
  uint32_t count = 0;
  while (g_running.load(std::memory_order_relaxed)) {
    const uint32_t seq = ipc::ReadControl(control, snapshot.data(), count);
    if (seq != applied_seq) {
      pool_rcu.Store(BuildPool(snapshot.data(), count));
      applied_seq = seq;
    }
    std::this_thread::sleep_for(kControlPollInterval);
  }
}

void RunDataplane(const common::Config& cfg) {
  ipc::ShmProvider shm("/cerebellum_stats", sizeof(ipc::StatsView),
                       ipc::ShmProvider::Mode::kCreate);
  auto* view = shm.As<ipc::StatsView>();

  ipc::ShmProvider control_shm(kControlShmName, sizeof(ipc::ControlView),
                               ipc::ShmProvider::Mode::kCreate);
  auto* control = control_shm.As<ipc::ControlView>();
  SeedControl(*control, cfg);

  std::vector<ipc::ControlBackend> snapshot(ipc::kMaxBackends);
  uint32_t snapshot_count = 0;
  const uint32_t applied_seq =
      ipc::ReadControl(*control, snapshot.data(), snapshot_count);

  lb::AtomicRcu<lb::BackendPool> pool_rcu;
  pool_rcu.Store(BuildPool(snapshot.data(), snapshot_count));

  const unsigned total = rte_lcore_count() - 1;
  if (total < 1) {
    throw std::runtime_error("need >= 2 lcores: main + worker");
  }
  if (total > ipc::kMaxLcores) {
    throw std::runtime_error("lcore count exceeds ipc::kMaxLcores");
  }
  const bool distributor = total >= 2;
  const auto n_workers = static_cast<uint32_t>(distributor ? total - 1 : 1);
  io::DpdkPort port(0, 1);

  std::vector<std::unique_ptr<io::Ring>> ingress;
  std::unique_ptr<io::Ring> egress;
  std::vector<rte_ring*> worker_rings;
  if (distributor) {
    for (uint32_t i = 0; i < n_workers; ++i) {
      ingress.push_back(
          std::make_unique<io::Ring>("cere_ingress_" + std::to_string(i),
                                     kRingSize, RING_F_SP_ENQ | RING_F_SC_DEQ));
      worker_rings.push_back(ingress.back()->Get());
    }
    egress = std::make_unique<io::Ring>("cere_egress", kEgressRingSize,
                                        RING_F_SC_DEQ);
  }

  std::vector<LcoreArg> args(total);
  unsigned idx = 0;
  uint16_t worker_id = 0;
  unsigned lcore_id = 0;
  RTE_LCORE_FOREACH_WORKER(lcore_id) {
    LcoreArg& arg = args[idx];
    arg = {};
    arg.port_id = port.PortId();
    arg.n_workers = n_workers;
    arg.cfg = &cfg;
    arg.pool_rcu = &pool_rcu;
    arg.stats = &view->lcores[idx];
    if (!distributor) {
      arg.role = Role::kMono;
    } else {
      arg.worker_rings = worker_rings.data();
      arg.egress = egress->Get();
      if (idx == 0) {
        arg.role = Role::kIo;
      } else {
        arg.role = Role::kWorker;
        arg.worker_id = worker_id;
        arg.ingress = worker_rings[worker_id];
        ++worker_id;
      }
    }
    rte_eal_remote_launch(LcoreMain, &arg, lcore_id);
    ++idx;
  }
  std::cout << (distributor
                    ? "cerebellum_dataplane: IO + " +
                          std::to_string(n_workers) + " workers"
                    : std::string("cerebellum_dataplane: single-thread"))
            << '\n';

  RunControlLoop(*control, pool_rcu, applied_seq);
  rte_eal_mp_wait_lcore();
}

}

int main(int argc, char** argv) {
  using namespace cere;

  std::string config_path = "./config.yaml";

  CLI::App cli{"Cerebellum L4 load balancer dataplane"};
  cli.add_option("--config,-c", config_path, "YAML config file")
      ->capture_default_str();
  cli.allow_extras();

  try {
    cli.parse(argc, argv);
  } catch (const CLI::ParseError& exc) {
    return cli.exit(exc);
  }

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  try {
    std::vector<std::string> eal_args{argv[0]};
    for (const std::string& extra : cli.remaining()) {
      eal_args.push_back(extra);
    }

    io::Eal eal(std::span{eal_args});
    RunDataplane(common::LoadConfig(config_path));
    return 0;
  } catch (const std::exception& exc) {
    std::cerr << "Error: " << exc.what() << '\n';
    return 1;
  }
}
