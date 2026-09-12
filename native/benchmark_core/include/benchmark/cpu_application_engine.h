#ifndef RAPIDBENCH_CPU_APPLICATION_ENGINE_H_
#define RAPIDBENCH_CPU_APPLICATION_ENGINE_H_

#include "benchmark/cpu_application_workload.h"
#include "benchmark/engine.h"
#include "benchmark/topology.h"
#include <memory>

namespace benchmark {
struct CpuApplicationRequest {
  CpuApplicationTest test = CpuApplicationTest::kSort;
  std::uint32_t duration_ms = 3000, warmup_ms = 700;
  // Only automatic modes: 1 = preferred performance core, 0 = all present CPUs.
  std::uint32_t requested_threads = 1;
};
struct CpuApplicationSnapshot {
  std::uint64_t run_id = 0;
  State state = State::kIdle;
  CpuApplicationTest test = CpuApplicationTest::kSort;
  std::uint32_t thread_count = 0, flags = 0;
  std::int32_t error_code = 0;
  std::uint32_t affinity_failures = 0;
  std::uint64_t input_bytes = 0, completed_units = 0, elapsed_ns = 0;
  double units_per_second = 0, progress = 0;
  std::uint64_t checksum = 0;
  std::uint32_t method_version = kCpuApplicationMethodVersion;
  std::uint32_t requested_threads = 1;
  // Actual target for Single; -1 for Multi. This is output, never user input.
  std::int32_t selected_cpu = -1;
};
constexpr std::uint32_t kApplicationValidated = 1U;
constexpr std::uint32_t kApplicationIndependentWorkers = 2U;
constexpr std::uint32_t kApplicationPerformanceRequested = 4U;

std::vector<std::uint32_t> SelectCpuApplicationCpus(const Topology &topology,
                                                    bool multi);

class CpuApplicationEngine final {
public:
  CpuApplicationEngine();
  ~CpuApplicationEngine();
  CpuApplicationEngine(const CpuApplicationEngine &) = delete;
  CpuApplicationEngine &operator=(const CpuApplicationEngine &) = delete;
  std::int32_t Start(const CpuApplicationRequest &, std::uint64_t *run_id);
  std::int32_t RequestStop(std::uint64_t run_id);
  CpuApplicationSnapshot GetSnapshot() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace benchmark
#endif
