#include <cstdint>
#include <iostream>

#include "benchmark/topology.h"

namespace {

benchmark::CpuInfo Cpu(std::uint32_t logical_cpu, std::int32_t cluster_id,
                       std::int32_t policy_id, std::uint32_t capacity,
                       std::uint32_t max_frequency_khz) {
  benchmark::CpuInfo cpu;
  cpu.logical_cpu = logical_cpu;
  cpu.online = true;
  cpu.allowed = true;
  cpu.cluster_id = cluster_id;
  cpu.frequency_policy_id = policy_id;
  cpu.capacity = capacity;
  cpu.max_frequency_khz = max_frequency_khz;
  return cpu;
}

bool Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

bool TestThreeHardwareClusters() {
  benchmark::Topology topology;
  topology.cpus = {
      Cpu(0, 0, 0, 400, 1800000), Cpu(1, 0, 0, 400, 1800000),
      Cpu(2, 1, 2, 400, 1800000), Cpu(3, 1, 2, 400, 1800000),
      Cpu(4, 1, 2, 400, 1800000), Cpu(5, 2, 5, 1024, 3200000),
  };
  benchmark::AssignPerformanceGroups(&topology);
  return Expect(topology.performance_group_count == 3,
                "three cluster topology was collapsed") &&
         Expect(topology.cpus[0].performance_group == 0 &&
                    topology.cpus[2].performance_group == 1 &&
                    topology.cpus[5].performance_group == 2,
                "hardware cluster ordering is incorrect") &&
         Expect(topology.preferred_single_cpu == 5,
                "preferred CPU did not select the fastest cluster");
}

bool TestFrequencyPolicyFallback() {
  benchmark::Topology topology;
  topology.cpus = {
      Cpu(0, -1, 0, 300, 1600000),  Cpu(1, -1, 0, 300, 1600000),
      Cpu(2, -1, 2, 600, 2400000),  Cpu(3, -1, 2, 600, 2400000),
      Cpu(4, -1, 4, 1024, 3100000),
  };
  benchmark::AssignPerformanceGroups(&topology);
  return Expect(topology.performance_group_count == 3,
                "CPUFreq policies were not used as group fallback") &&
         Expect(topology.cpus[0].performance_group == 0 &&
                    topology.cpus[2].performance_group == 1 &&
                    topology.cpus[4].performance_group == 2,
                "CPUFreq policy ordering is incorrect") &&
         Expect((topology.quality_flags &
                 benchmark::kQualityPerformanceGroupsInferred) == 0,
                "direct CPUFreq grouping was incorrectly marked inferred");
}

bool TestUnhelpfulClusterIdUsesPolicies() {
  benchmark::Topology topology;
  topology.cpus = {
      Cpu(0, 0, 0, 300, 1600000),  Cpu(1, 0, 0, 300, 1600000),
      Cpu(2, 0, 2, 600, 2400000),  Cpu(3, 0, 2, 600, 2400000),
      Cpu(4, 0, 4, 1024, 3100000),
  };
  benchmark::AssignPerformanceGroups(&topology);
  return Expect(topology.performance_group_count == 3,
                "constant cluster_id hid CPUFreq policies");
}

bool TestPerformanceOrderingIgnoresClusterNumber() {
  benchmark::Topology topology;
  topology.cpus = {
      Cpu(0, 9, 0, 300, 1500000),
      Cpu(1, 1, 1, 1024, 3300000),
  };
  benchmark::AssignPerformanceGroups(&topology);
  return Expect(topology.cpus[0].performance_group == 0 &&
                    topology.cpus[1].performance_group == 1,
                "group order followed cluster number instead of performance");
}

bool TestDisallowedCpusAreIgnored() {
  benchmark::Topology topology;
  topology.cpus = {
      Cpu(0, 0, 0, 300, 1500000),
      Cpu(1, 1, 1, 700, 2400000),
      Cpu(2, 2, 2, 1024, 3300000),
  };
  topology.cpus[2].allowed = false;
  topology.cpus[2].online = false;
  benchmark::AssignPerformanceGroups(&topology);
  return Expect(topology.performance_group_count == 2,
                "offline or disallowed CPUs created a selectable group") &&
         Expect(topology.preferred_single_cpu == 1,
                "preferred CPU selected a disallowed core");
}

bool TestGroupLimit() {
  benchmark::Topology topology;
  for (std::uint32_t cpu = 0; cpu < 20; ++cpu) {
    topology.cpus.push_back(Cpu(cpu, static_cast<std::int32_t>(cpu),
                                static_cast<std::int32_t>(cpu), 100 + cpu,
                                1000000 + cpu * 10000));
  }
  benchmark::AssignPerformanceGroups(&topology);
  return Expect(topology.performance_group_count == 16,
                "performance group count exceeded the telemetry ABI") &&
         Expect(topology.cpus.back().performance_group == 15,
                "overflow groups were not folded into the final group") &&
         Expect((topology.quality_flags &
                 benchmark::kQualityTopologyTruncated) != 0,
                "group truncation was not marked in quality flags");
}

bool TestNoAllowedCpu() {
  benchmark::Topology topology;
  topology.cpus = {Cpu(0, 0, 0, 300, 1500000)};
  topology.cpus[0].allowed = false;
  benchmark::AssignPerformanceGroups(&topology);
  return Expect(topology.performance_group_count == 0,
                "empty selectable topology created a performance group") &&
         Expect(topology.preferred_single_cpu == -1,
                "empty selectable topology selected a preferred CPU");
}

bool TestRankFallback() {
  benchmark::Topology topology;
  topology.cpus = {
      Cpu(0, -1, -1, 300, 1600000),
      Cpu(1, -1, -1, 300, 1600000),
      Cpu(2, -1, -1, 900, 2800000),
  };
  benchmark::AssignPerformanceGroups(&topology);
  return Expect(topology.performance_group_count == 2,
                "rank fallback did not preserve performance classes") &&
         Expect((topology.quality_flags &
                 benchmark::kQualityPerformanceGroupsInferred) != 0,
                "rank fallback was not marked inferred");
}

} // namespace

int main() {
  if (!TestThreeHardwareClusters() || !TestFrequencyPolicyFallback() ||
      !TestUnhelpfulClusterIdUsesPolicies() ||
      !TestPerformanceOrderingIgnoresClusterNumber() ||
      !TestDisallowedCpusAreIgnored() || !TestGroupLimit() ||
      !TestNoAllowedCpu() || !TestRankFallback()) {
    return 1;
  }
  std::cout << "Topology grouping tests passed\n";
  return 0;
}
