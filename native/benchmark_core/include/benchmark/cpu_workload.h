#ifndef CPU_BENCHMARK_CPU_WORKLOAD_H_
#define CPU_BENCHMARK_CPU_WORKLOAD_H_

#include <array>
#include <cstdint>

namespace benchmark {

constexpr std::uint64_t kCpuWorkUnitsPerBatch = 1024;

struct CpuWorkloadState {
  std::array<std::uint32_t, 128> integers{};
  std::array<float, 128> floating_point{};
  std::uint64_t checksum = 0;
};

void InitializeCpuWorkload(CpuWorkloadState *state, std::uint64_t seed);
void RunCpuWorkloadBatch(CpuWorkloadState *state);

} // namespace benchmark

#endif // CPU_BENCHMARK_CPU_WORKLOAD_H_
