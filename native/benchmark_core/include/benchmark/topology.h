#ifndef CPU_BENCHMARK_TOPOLOGY_H_
#define CPU_BENCHMARK_TOPOLOGY_H_

#include <cstdint>
#include <vector>

namespace benchmark {

constexpr std::uint64_t kQualityCapacityMissing = 1ULL << 0U;
constexpr std::uint64_t kQualityMaxFrequencyMissing = 1ULL << 1U;
constexpr std::uint64_t kQualityClusterIdMissing = 1ULL << 2U;
constexpr std::uint64_t kQualityAffinityUnavailable = 1ULL << 3U;
constexpr std::uint64_t kQualityTopologyTruncated = 1ULL << 4U;
constexpr std::uint64_t kQualityPerformanceGroupsInferred = 1ULL << 5U;
constexpr std::uint64_t kQualityAffinityFailed = 1ULL << 16U;
constexpr std::uint64_t kQualityThreadCountReduced = 1ULL << 17U;
constexpr std::uint64_t kQualityHighScoreVariation = 1ULL << 18U;
constexpr std::uint64_t kQualitySelectionFallback = 1ULL << 19U;
constexpr std::uint64_t kQualityPerformanceRequestUnavailable = 1ULL << 20U;

struct CpuInfo {
  std::uint32_t logical_cpu = 0;
  bool online = false;
  bool allowed = false;
  std::int32_t cluster_id = -1;
  std::int32_t core_id = -1;
  std::int32_t frequency_policy_id = -1;
  std::uint32_t max_frequency_khz = 0;
  std::uint32_t capacity = 0;
  std::uint32_t performance_group = 0;
};

struct FrequencyResidency {
  std::uint32_t frequency_khz = 0;
  std::uint64_t residency = 0;
};

struct Topology {
  std::vector<CpuInfo> cpus;
  std::uint32_t online_count = 0;
  std::uint32_t allowed_count = 0;
  std::uint32_t performance_group_count = 0;
  std::int32_t preferred_single_cpu = -1;
  std::uint64_t quality_flags = 0;
};

Topology DetectTopology();

void AssignPerformanceGroups(Topology *topology);

std::vector<std::uint32_t> SelectBenchmarkCpus(const Topology &topology,
                                               std::uint32_t requested_threads);

std::uint32_t ReadCurrentFrequencyKhz(std::uint32_t logical_cpu);

std::vector<FrequencyResidency>
ReadFrequencyResidency(std::uint32_t logical_cpu);

std::int32_t CurrentLogicalCpu();

std::int32_t CurrentThreadId();

std::uint64_t CurrentThreadCpuTimeNs();

bool RequestMaximumPerformanceForCurrentThread();

bool PinCurrentThreadToCpu(std::uint32_t logical_cpu);

} // namespace benchmark

#endif // CPU_BENCHMARK_TOPOLOGY_H_
