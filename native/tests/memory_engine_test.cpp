#include "benchmark/memory_engine.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

bool Wait(benchmark::MemoryEngine &engine, benchmark::MemorySnapshot &snapshot) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  do {
    snapshot = engine.GetSnapshot();
    if (snapshot.state >= 4) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (std::chrono::steady_clock::now() < deadline);
  return false;
}

int main() {
  benchmark::MemoryEngine engine;
  for (const auto test : {benchmark::MemoryTest::kRead, benchmark::MemoryTest::kWrite,
                           benchmark::MemoryTest::kCopy}) {
    benchmark::MemoryRequest request{test, 250, 0};
    std::uint64_t run = 0;
    benchmark::MemorySnapshot snapshot;
    if (engine.Start(request, &run) != 0 || !Wait(engine, snapshot) ||
        snapshot.state != 4 || snapshot.error_code != 0 ||
        snapshot.elapsed_ns < 250000000ULL || snapshot.processed_bytes == 0 ||
        !std::isfinite(snapshot.bandwidth_gbps) || snapshot.bandwidth_gbps <= 0)
      return 1;
    const double expected = static_cast<double>(snapshot.processed_bytes) / snapshot.elapsed_ns;
    if (std::abs(snapshot.bandwidth_gbps - expected) > 1e-9) return 2;
    std::printf("MEMORY id=%u elapsedMs=%.3f GBps=%.3f\n", static_cast<unsigned>(test),
                snapshot.elapsed_ns / 1.0e6, snapshot.bandwidth_gbps);
  }
  // Stop during preparation/warm-up and verify the same engine can restart.
  benchmark::MemoryRequest request{benchmark::MemoryTest::kCopy, 250, 500};
  std::uint64_t run = 0;
  benchmark::MemorySnapshot snapshot;
  if (engine.Start(request, &run) != 0 || engine.RequestStop(run) != 0 ||
      !Wait(engine, snapshot) || snapshot.state != 5) return 3;
  request.warmup_ms = 0;
  if (engine.Start(request, &run) != 0 || !Wait(engine, snapshot) ||
      snapshot.state != 4 || snapshot.bandwidth_gbps <= 0) return 4;
  std::puts("Memory engine stop/restart passed");

  auto topology = benchmark::DetectTopology();
  const auto available = benchmark::SelectBenchmarkCpus(topology, 0);
  if (available.empty()) return 5;
  const auto restricted = [&]() {
    auto result = topology;
    for (auto &cpu : result.cpus) cpu.allowed = cpu.logical_cpu == available.front();
    result.allowed_count = 1;
    return result;
  };
  unsigned reads = 0;
  benchmark::MemoryEngine late_online([&]() { return ++reads == 1 ? restricted() : topology; });
  request.warmup_ms = 30;
  if (late_online.Start(request, &run) != 0 || !Wait(late_online, snapshot) ||
      snapshot.state != 4 || snapshot.thread_count != available.size() ||
      snapshot.preparation_attempts != (available.size() > 1 ? 2U : 1U) ||
      snapshot.topology_unstable || snapshot.bandwidth_gbps <= 0) return 6;
  std::printf("MEMORY late-online attempts=%u workers=%u\n", snapshot.preparation_attempts, snapshot.thread_count);
  reads = 0;
  benchmark::MemoryEngine unstable([&]() { return (++reads % 2) ? restricted() : topology; });
  if (unstable.Start(request, &run) != 0 || !Wait(unstable, snapshot) ||
      snapshot.state != 4 || snapshot.thread_count != 1 ||
      (available.size() > 1 && (!snapshot.topology_unstable || snapshot.preparation_attempts != 3))) return 7;
  std::printf("MEMORY changing-topology attempts=%u workers=%u unstable=%d\n", snapshot.preparation_attempts, snapshot.thread_count, snapshot.topology_unstable);
  benchmark::MemoryEngine limited(restricted);
  if (limited.Start(request, &run) != 0 || !Wait(limited, snapshot) ||
      snapshot.state != 4 || snapshot.thread_count != 1 || snapshot.allowed_cpus != 1 ||
      snapshot.present_cpus != topology.cpus.size()) return 8;
  std::puts("Memory availability refresh and bounded retry passed");
  return 0;
}
