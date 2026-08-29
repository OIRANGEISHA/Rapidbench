#include "benchmark/topology.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#if defined(__linux__)
#include <dirent.h>
#include <sched.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

namespace benchmark {
namespace {

#if defined(__linux__)
struct SchedulerAttributes {
  std::uint32_t size = sizeof(SchedulerAttributes);
  std::uint32_t policy = 0;
  std::uint64_t flags = 0;
  std::int32_t nice = 0;
  std::uint32_t priority = 0;
  std::uint64_t runtime = 0;
  std::uint64_t deadline = 0;
  std::uint64_t period = 0;
  std::uint32_t utilization_min = 0;
  std::uint32_t utilization_max = 0;
};

constexpr std::uint64_t kSchedulerFlagKeepPolicy = 0x08ULL;
constexpr std::uint64_t kSchedulerFlagKeepParameters = 0x10ULL;
constexpr std::uint64_t kSchedulerFlagKeepAll =
    kSchedulerFlagKeepPolicy | kSchedulerFlagKeepParameters;
constexpr std::uint64_t kSchedulerFlagUtilClampMin = 0x20ULL;
constexpr std::uint64_t kSchedulerFlagUtilClampMax = 0x40ULL;
constexpr std::uint32_t kMaximumSchedulerUtilization = 1024U;

bool ApplyUtilizationClamp(std::uint64_t flags, bool set_maximum) {
#if defined(__NR_sched_setattr)
  SchedulerAttributes attributes;
  attributes.flags = flags;
  attributes.utilization_min = kMaximumSchedulerUtilization;
  if (set_maximum) {
    attributes.utilization_max = kMaximumSchedulerUtilization;
  }
  return syscall(__NR_sched_setattr, 0, &attributes, 0) == 0;
#else
  (void)flags;
  (void)set_maximum;
  return false;
#endif
}
#endif

std::optional<std::string> ReadLine(const std::string &path) {
  std::ifstream stream(path);
  std::string line;
  if (!stream.is_open() || !std::getline(stream, line)) {
    return std::nullopt;
  }
  return line;
}

template <typename T> std::optional<T> ReadInteger(const std::string &path) {
  const auto line = ReadLine(path);
  if (!line.has_value()) {
    return std::nullopt;
  }
  std::istringstream stream(*line);
  T value{};
  stream >> value;
  if (stream.fail()) {
    return std::nullopt;
  }
  return value;
}

std::vector<std::uint32_t> ParseCpuList(const std::string &value) {
  std::vector<std::uint32_t> cpus;
  std::size_t position = 0;
  while (position < value.size()) {
    while (position < value.size() &&
           (value[position] == ',' ||
            std::isspace(static_cast<unsigned char>(value[position])) != 0)) {
      ++position;
    }
    if (position >= value.size()) {
      break;
    }

    std::size_t end = position;
    while (end < value.size() &&
           std::isdigit(static_cast<unsigned char>(value[end])) != 0) {
      ++end;
    }
    if (end == position) {
      break;
    }
    const auto first = static_cast<std::uint32_t>(
        std::stoul(value.substr(position, end - position)));
    std::uint32_t last = first;
    if (end < value.size() && value[end] == '-') {
      position = end + 1;
      end = position;
      while (end < value.size() &&
             std::isdigit(static_cast<unsigned char>(value[end])) != 0) {
        ++end;
      }
      if (end == position) {
        break;
      }
      last = static_cast<std::uint32_t>(
          std::stoul(value.substr(position, end - position)));
    }
    if (last >= first && last - first < 4096U) {
      for (std::uint32_t cpu = first; cpu <= last; ++cpu) {
        cpus.push_back(cpu);
      }
    }
    position = end;
  }
  std::sort(cpus.begin(), cpus.end());
  cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
  return cpus;
}

std::optional<std::int32_t>
ReadFrequencyPolicyId(const std::string &cpufreq_root) {
  for (const char *name : {"related_cpus", "affected_cpus"}) {
    const auto value = ReadLine(cpufreq_root + name);
    if (!value.has_value()) {
      continue;
    }
    const std::vector<std::uint32_t> cpus = ParseCpuList(*value);
    if (!cpus.empty() &&
        cpus.front() <= static_cast<std::uint32_t>(INT32_MAX)) {
      return static_cast<std::int32_t>(cpus.front());
    }
  }
  return std::nullopt;
}

std::string FrequencyPolicyRoot(std::int32_t policy_id) {
  if (policy_id < 0) {
    return {};
  }
  return "/sys/devices/system/cpu/cpufreq/policy" + std::to_string(policy_id) +
         "/";
}

std::optional<std::int32_t>
ReadFrequencyPolicyIdForCpu(std::uint32_t logical_cpu,
                            const std::string &per_cpu_root) {
  if (const auto direct = ReadFrequencyPolicyId(per_cpu_root);
      direct.has_value()) {
    return direct;
  }
#if defined(__linux__)
  constexpr const char *kPolicyDirectory = "/sys/devices/system/cpu/cpufreq/";
  DIR *directory = opendir(kPolicyDirectory);
  if (directory == nullptr) {
    return std::nullopt;
  }
  while (const dirent *entry = readdir(directory)) {
    const std::string name(entry->d_name);
    const std::string id_text =
        name.rfind("policy", 0U) == 0U ? name.substr(6U) : std::string{};
    if (id_text.empty() ||
        !std::all_of(id_text.begin(), id_text.end(), [](char character) {
          return std::isdigit(static_cast<unsigned char>(character)) != 0;
        })) {
      continue;
    }
    std::uint64_t policy_number = 0U;
    for (const char digit : id_text) {
      policy_number =
          policy_number * 10U + static_cast<std::uint64_t>(digit - '0');
    }
    if (policy_number > static_cast<std::uint64_t>(INT32_MAX)) {
      continue;
    }
    const std::string root = std::string(kPolicyDirectory) + name + "/";
    for (const char *list_name : {"related_cpus", "affected_cpus"}) {
      const auto list = ReadLine(root + list_name);
      if (list.has_value()) {
        const auto cpus = ParseCpuList(*list);
        if (std::binary_search(cpus.begin(), cpus.end(), logical_cpu)) {
          closedir(directory);
          return static_cast<std::int32_t>(policy_number);
        }
      }
    }
  }
  closedir(directory);
#endif
  return std::nullopt;
}

std::uint32_t ReadFrequencyValue(const std::string &cpufreq_root,
                                 std::int32_t policy_id,
                                 const char *primary_name,
                                 const char *secondary_name) {
  for (const char *name : {primary_name, secondary_name}) {
    const auto value = ReadInteger<std::uint32_t>(cpufreq_root + name);
    if (value.has_value() && *value > 0) {
      return *value;
    }
  }
  const std::string policy_root = FrequencyPolicyRoot(policy_id);
  if (policy_root.empty() || policy_root == cpufreq_root) {
    return 0;
  }
  for (const char *name : {primary_name, secondary_name}) {
    const auto value = ReadInteger<std::uint32_t>(policy_root + name);
    if (value.has_value() && *value > 0) {
      return *value;
    }
  }
  return 0;
}

std::vector<FrequencyResidency>
ReadFrequencyResidencyFile(const std::string &path) {
  std::vector<FrequencyResidency> entries;
  std::ifstream stream(path);
  FrequencyResidency entry;
  while (stream >> entry.frequency_khz >> entry.residency) {
    if (entry.frequency_khz > 0) {
      entries.push_back(entry);
    }
  }
  return entries;
}

std::vector<std::uint32_t> DetectPresentCpus() {
#if defined(__linux__)
  if (const auto present = ReadLine("/sys/devices/system/cpu/present");
      present.has_value()) {
    const auto parsed = ParseCpuList(*present);
    if (!parsed.empty()) {
      return parsed;
    }
  }
  const long configured = sysconf(_SC_NPROCESSORS_CONF);
  if (configured > 0) {
    std::vector<std::uint32_t> cpus;
    cpus.reserve(static_cast<std::size_t>(configured));
    for (long cpu = 0; cpu < configured; ++cpu) {
      cpus.push_back(static_cast<std::uint32_t>(cpu));
    }
    return cpus;
  }
#endif
  const unsigned int count = std::max(1U, std::thread::hardware_concurrency());
  std::vector<std::uint32_t> cpus;
  cpus.reserve(count);
  for (unsigned int cpu = 0; cpu < count; ++cpu) {
    cpus.push_back(cpu);
  }
  return cpus;
}

bool ContainsCpu(const std::vector<std::uint32_t> &cpus,
                 std::uint32_t logical_cpu) {
  return std::binary_search(cpus.begin(), cpus.end(), logical_cpu);
}

std::uint64_t PerformanceRank(const CpuInfo &cpu) {
  if (cpu.capacity != 0) {
    return (static_cast<std::uint64_t>(cpu.capacity) << 32U) |
           cpu.max_frequency_khz;
  }
  return cpu.max_frequency_khz;
}

enum class PerformanceGroupSource {
  kCluster,
  kFrequencyPolicy,
  kPerformanceRank,
};

std::int64_t PerformanceGroupIdentity(const CpuInfo &cpu,
                                      PerformanceGroupSource source) {
  switch (source) {
  case PerformanceGroupSource::kCluster:
    return cpu.cluster_id;
  case PerformanceGroupSource::kFrequencyPolicy:
    return cpu.frequency_policy_id;
  case PerformanceGroupSource::kPerformanceRank:
    return static_cast<std::int64_t>(PerformanceRank(cpu));
  }
  return 0;
}

constexpr std::uint32_t kMaximumPerformanceGroups = 16;

} // namespace

void AssignPerformanceGroups(Topology *topology) {
  if (topology == nullptr) {
    return;
  }

  std::vector<std::int32_t> cluster_ids;
  std::vector<std::int32_t> frequency_policy_ids;
  bool clusters_complete = true;
  bool policies_complete = true;
  for (const CpuInfo &cpu : topology->cpus) {
    if (cpu.cluster_id < 0) {
      clusters_complete = false;
    } else {
      cluster_ids.push_back(cpu.cluster_id);
    }
    if (cpu.frequency_policy_id < 0) {
      policies_complete = false;
    } else {
      frequency_policy_ids.push_back(cpu.frequency_policy_id);
    }
  }

  if (cluster_ids.empty() && frequency_policy_ids.empty()) {
    clusters_complete = false;
    policies_complete = false;
  }
  std::sort(cluster_ids.begin(), cluster_ids.end());
  cluster_ids.erase(std::unique(cluster_ids.begin(), cluster_ids.end()),
                    cluster_ids.end());
  std::sort(frequency_policy_ids.begin(), frequency_policy_ids.end());
  frequency_policy_ids.erase(
      std::unique(frequency_policy_ids.begin(), frequency_policy_ids.end()),
      frequency_policy_ids.end());

  PerformanceGroupSource source = PerformanceGroupSource::kPerformanceRank;
  if (clusters_complete && cluster_ids.size() > 1) {
    source = PerformanceGroupSource::kCluster;
  } else if (policies_complete && frequency_policy_ids.size() > 1) {
    source = PerformanceGroupSource::kFrequencyPolicy;
  } else if (clusters_complete) {
    source = PerformanceGroupSource::kCluster;
  } else if (policies_complete) {
    source = PerformanceGroupSource::kFrequencyPolicy;
  } else {
    topology->quality_flags |= kQualityPerformanceGroupsInferred;
  }

  std::map<std::int64_t, std::uint64_t> group_ranks;
  for (const CpuInfo &cpu : topology->cpus) {
    const std::int64_t identity = PerformanceGroupIdentity(cpu, source);
    auto [entry, inserted] =
        group_ranks.emplace(identity, PerformanceRank(cpu));
    if (!inserted) {
      entry->second = std::max(entry->second, PerformanceRank(cpu));
    }
  }

  std::vector<std::pair<std::int64_t, std::uint64_t>> ordered_groups(
      group_ranks.begin(), group_ranks.end());
  std::sort(ordered_groups.begin(), ordered_groups.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.second, left.first) <
                     std::tie(right.second, right.first);
            });
  if (ordered_groups.size() > kMaximumPerformanceGroups) {
    topology->quality_flags |= kQualityTopologyTruncated;
  }
  topology->performance_group_count = static_cast<std::uint32_t>(
      std::min<std::size_t>(ordered_groups.size(), kMaximumPerformanceGroups));

  std::map<std::int64_t, std::uint32_t> assigned_groups;
  for (std::size_t index = 0; index < ordered_groups.size(); ++index) {
    assigned_groups[ordered_groups[index].first] = static_cast<std::uint32_t>(
        std::min<std::size_t>(index, kMaximumPerformanceGroups - 1));
  }

  topology->preferred_single_cpu = -1;
  std::uint64_t best_rank = 0;
  for (CpuInfo &cpu : topology->cpus) {
    const auto group =
        assigned_groups.find(PerformanceGroupIdentity(cpu, source));
    if (group != assigned_groups.end()) {
      cpu.performance_group = group->second;
    }
    const std::uint64_t rank = PerformanceRank(cpu);
    if (topology->preferred_single_cpu < 0 || rank > best_rank) {
      best_rank = rank;
      topology->preferred_single_cpu =
          static_cast<std::int32_t>(cpu.logical_cpu);
    }
  }
}

Topology DetectTopology() {
  Topology topology;
  const std::vector<std::uint32_t> present = DetectPresentCpus();

  std::vector<std::uint32_t> online = present;
#if defined(__linux__)
  if (const auto online_text = ReadLine("/sys/devices/system/cpu/online");
      online_text.has_value()) {
    const auto parsed = ParseCpuList(*online_text);
    if (!parsed.empty()) {
      online = parsed;
    }
  }

  cpu_set_t allowed_set;
  CPU_ZERO(&allowed_set);
  const bool affinity_available =
      sched_getaffinity(0, sizeof(allowed_set), &allowed_set) == 0;
  if (!affinity_available) {
    topology.quality_flags |= kQualityAffinityUnavailable;
  }
#else
  const bool affinity_available = false;
  topology.quality_flags |= kQualityAffinityUnavailable;
#endif

  bool missing_capacity = false;
  bool missing_frequency = false;
  bool missing_cluster = false;
  topology.cpus.reserve(present.size());

  for (const std::uint32_t logical_cpu : present) {
    const std::string root =
        "/sys/devices/system/cpu/cpu" + std::to_string(logical_cpu) + "/";
    CpuInfo cpu;
    cpu.logical_cpu = logical_cpu;
    cpu.online = ContainsCpu(online, logical_cpu);
#if defined(__linux__)
    cpu.allowed = cpu.online &&
                  (!affinity_available ||
                   (logical_cpu < CPU_SETSIZE &&
                    CPU_ISSET(static_cast<int>(logical_cpu), &allowed_set)));
#else
    cpu.allowed = cpu.online;
#endif

    cpu.cluster_id = ReadInteger<std::int32_t>(root + "topology/cluster_id")
                         .value_or(ReadInteger<std::int32_t>(
                                       root + "topology/physical_package_id")
                                       .value_or(-1));
    cpu.core_id =
        ReadInteger<std::int32_t>(root + "topology/core_id").value_or(-1);
    cpu.frequency_policy_id =
        ReadFrequencyPolicyIdForCpu(logical_cpu, root + "cpufreq/")
            .value_or(-1);
    cpu.max_frequency_khz =
        ReadFrequencyValue(root + "cpufreq/", cpu.frequency_policy_id,
                           "cpuinfo_max_freq", "scaling_max_freq");
    cpu.capacity =
        ReadInteger<std::uint32_t>(root + "cpu_capacity").value_or(0);

    if (cpu.online) {
      ++topology.online_count;
    }
    if (cpu.allowed) {
      ++topology.allowed_count;
    }
    missing_capacity = missing_capacity || cpu.capacity == 0;
    missing_frequency = missing_frequency || cpu.max_frequency_khz == 0;
    missing_cluster =
        missing_cluster || (cpu.cluster_id < 0 && cpu.frequency_policy_id < 0);
    topology.cpus.push_back(cpu);
  }

  if (missing_capacity) {
    topology.quality_flags |= kQualityCapacityMissing;
  }
  if (missing_frequency) {
    topology.quality_flags |= kQualityMaxFrequencyMissing;
  }
  if (missing_cluster) {
    topology.quality_flags |= kQualityClusterIdMissing;
  }

  AssignPerformanceGroups(&topology);
  return topology;
}

namespace {

std::vector<std::uint32_t> SelectCpuCandidates(const Topology &topology,
                                               std::uint32_t requested_threads,
                                               bool include_all_present) {
  std::vector<const CpuInfo *> candidates;
  for (const CpuInfo &cpu : topology.cpus) {
    if (include_all_present || (cpu.online && cpu.allowed)) {
      candidates.push_back(&cpu);
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const CpuInfo *lhs, const CpuInfo *rhs) {
              return std::tie(rhs->performance_group, rhs->capacity,
                              rhs->max_frequency_khz, lhs->logical_cpu) <
                     std::tie(lhs->performance_group, lhs->capacity,
                              lhs->max_frequency_khz, rhs->logical_cpu);
            });

  if (requested_threads != 0 && requested_threads < candidates.size()) {
    candidates.resize(requested_threads);
  }
  std::vector<std::uint32_t> selected;
  selected.reserve(candidates.size());
  for (const CpuInfo *cpu : candidates) {
    selected.push_back(cpu->logical_cpu);
  }
  return selected;
}

} // namespace

std::vector<std::uint32_t>
SelectBenchmarkCpus(const Topology &topology, std::uint32_t requested_threads) {
  return SelectCpuCandidates(topology, requested_threads, false);
}

std::vector<std::uint32_t>
SelectPresentCpuBenchmarkCpus(const Topology &topology,
                              std::uint32_t requested_threads) {
  return SelectCpuCandidates(topology, requested_threads, true);
}

std::uint32_t ReadCurrentFrequencyKhz(std::uint32_t logical_cpu) {
#if defined(__linux__)
  const std::string root =
      "/sys/devices/system/cpu/cpu" + std::to_string(logical_cpu) + "/cpufreq/";
  const std::int32_t policy_id =
      ReadFrequencyPolicyIdForCpu(logical_cpu, root).value_or(-1);
  return ReadFrequencyValue(root, policy_id, "scaling_cur_freq",
                            "cpuinfo_cur_freq");
#else
  (void)logical_cpu;
  return 0;
#endif
}

std::vector<FrequencyResidency>
ReadFrequencyResidency(std::uint32_t logical_cpu) {
#if defined(__linux__)
  const std::string root =
      "/sys/devices/system/cpu/cpu" + std::to_string(logical_cpu) + "/cpufreq/";
  std::vector<FrequencyResidency> entries =
      ReadFrequencyResidencyFile(root + "stats/time_in_state");
  if (!entries.empty()) {
    return entries;
  }
  const std::int32_t policy_id =
      ReadFrequencyPolicyIdForCpu(logical_cpu, root).value_or(-1);
  const std::string policy_root = FrequencyPolicyRoot(policy_id);
  if (!policy_root.empty()) {
    entries = ReadFrequencyResidencyFile(policy_root + "stats/time_in_state");
  }
  return entries;
#else
  (void)logical_cpu;
  return {};
#endif
}

std::int32_t CurrentLogicalCpu() {
#if defined(__linux__)
  return static_cast<std::int32_t>(sched_getcpu());
#else
  return -1;
#endif
}

std::int32_t CurrentThreadId() {
#if defined(__linux__)
  return static_cast<std::int32_t>(syscall(__NR_gettid));
#else
  return -1;
#endif
}

std::uint64_t CurrentThreadCpuTimeNs() {
#if defined(__linux__)
  timespec value{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0) {
    return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(value.tv_nsec);
  }
#endif
  return 0;
}

bool RequestMaximumPerformanceForCurrentThread() {
#if defined(__linux__) && defined(__NR_sched_setattr)
  if (ApplyUtilizationClamp(kSchedulerFlagKeepAll | kSchedulerFlagUtilClampMin |
                                kSchedulerFlagUtilClampMax,
                            true)) {
    return true;
  }
  if (ApplyUtilizationClamp(kSchedulerFlagKeepAll | kSchedulerFlagUtilClampMin,
                            false)) {
    return true;
  }
  return ApplyUtilizationClamp(kSchedulerFlagUtilClampMin, false);
#else
  return false;
#endif
}

bool PinCurrentThreadToCpu(std::uint32_t logical_cpu) {
#if defined(__linux__)
  if (logical_cpu >= CPU_SETSIZE) {
    return false;
  }
  cpu_set_t requested;
  CPU_ZERO(&requested);
  CPU_SET(static_cast<int>(logical_cpu), &requested);
  if (sched_setaffinity(0, sizeof(requested), &requested) != 0) {
    return false;
  }

  cpu_set_t actual;
  CPU_ZERO(&actual);
  if (sched_getaffinity(0, sizeof(actual), &actual) != 0 ||
      !CPU_ISSET(static_cast<int>(logical_cpu), &actual)) {
    return false;
  }
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (cpu != static_cast<int>(logical_cpu) && CPU_ISSET(cpu, &actual)) {
      return false;
    }
  }
  return true;
#else
  (void)logical_cpu;
  return false;
#endif
}

} // namespace benchmark
