#include "benchmark/engine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#include "benchmark/cpu_workload.h"
#include "benchmark/performance_hint.h"
#include "benchmark/topology.h"

namespace benchmark {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::int32_t kOk = 0;
constexpr std::int32_t kInvalidArgument = -1;
constexpr std::int32_t kBusy = -2;
constexpr std::int32_t kTopologyUnavailable = -10;
constexpr std::int32_t kThreadCreationFailed = -11;
constexpr std::uint32_t kMaximumBenchmarkThreads = 64;
constexpr std::uint32_t kWindowCount = 3;
constexpr std::uint64_t kRequestFlagSingleCpuExplicit = 1ULL << 0U;
constexpr std::uint64_t kRequestFlagMultiGroupExplicit = 1ULL << 1U;
constexpr std::uint32_t kRequestSingleCpuShift = 8U;
constexpr std::uint64_t kRequestSingleCpuMask = 0xFFULL
                                                << kRequestSingleCpuShift;
constexpr std::uint32_t kRequestMultiGroupShift = 16U;
constexpr std::uint64_t kRequestMultiGroupMask = 0xFFULL
                                                 << kRequestMultiGroupShift;
constexpr std::uint32_t kAffinityCheckBatches = 256;
constexpr std::uint32_t kAffinityPinAttempts = 4;
constexpr auto kAffinityRetryDelay = std::chrono::milliseconds(1);
constexpr std::uint32_t kWorkloadBatchesPerSchedulingCheck = 8;
constexpr std::int64_t kMinimumHintUpdateNs = 20'000'000;
constexpr std::int64_t kMaximumHintUpdateNs = 100'000'000;
constexpr std::uint32_t kTelemetryUtilizationMask = 0x3FFU;
constexpr std::uint32_t kTelemetryHintActive = 1U << 10U;
constexpr std::uint32_t kTelemetryAffinityChecked = 1U << 11U;
constexpr std::uint32_t kTelemetryPerformanceRequestActive = 1U << 12U;
constexpr std::uint32_t kTelemetryFrequencyShift = 16U;

bool IsRunning(State state) {
  return state == State::kPreparing || state == State::kWarmingUp ||
         state == State::kMeasuring;
}

bool IsCpuTest(TestId test_id) {
  return test_id == TestId::kPhase1Workload || test_id == TestId::kCpuSingle ||
         test_id == TestId::kCpuMulti;
}

bool IsSelectableCpu(const Topology &topology, std::uint32_t logical_cpu) {
  return std::any_of(topology.cpus.begin(), topology.cpus.end(),
                     [logical_cpu](const CpuInfo &cpu) {
                       return cpu.logical_cpu == logical_cpu;
                     });
}

std::vector<std::uint32_t>
SelectPerformanceGroupCpus(const Topology &topology,
                           std::uint32_t performance_group) {
  std::vector<std::uint32_t> selected;
  for (const CpuInfo &cpu : topology.cpus) {
    if (cpu.performance_group == performance_group) {
      selected.push_back(cpu.logical_cpu);
    }
  }
  std::sort(selected.begin(), selected.end());
  return selected;
}

std::uint32_t PackTelemetry(std::uint32_t peak_frequency_khz,
                            std::uint32_t utilization_permille,
                            bool performance_hint_active, bool affinity_checked,
                            bool performance_request_active) {
  const std::uint32_t peak_mhz =
      std::min<std::uint32_t>((peak_frequency_khz + 500U) / 1000U, 0xFFFFU);
  std::uint32_t telemetry =
      (peak_mhz << kTelemetryFrequencyShift) |
      std::min(utilization_permille, kTelemetryUtilizationMask);
  if (performance_hint_active) {
    telemetry |= kTelemetryHintActive;
  }
  if (affinity_checked) {
    telemetry |= kTelemetryAffinityChecked;
  }
  if (performance_request_active) {
    telemetry |= kTelemetryPerformanceRequestActive;
  }
  return telemetry;
}

double ScoreFrom(std::uint64_t work, std::uint64_t elapsed_ns) {
  if (elapsed_ns == 0) {
    return 0.0;
  }
  const double seconds = static_cast<double>(elapsed_ns) / 1'000'000'000.0;
  return (static_cast<double>(work) / seconds) / 1'000.0;
}

struct WorkerContext {
  explicit WorkerContext(std::uint32_t cpu) : logical_cpu(cpu) {}

  std::uint32_t logical_cpu;
  std::atomic<std::int32_t> thread_id{-1};
  std::atomic<std::uint64_t> completed_work{0};
  std::atomic<std::uint64_t> elapsed_ns{0};
  std::atomic<std::uint64_t> thread_cpu_time_ns{0};
  std::atomic<std::uint32_t> affinity_checks{0};
  std::atomic<bool> affinity_violated{false};
  std::atomic<bool> affinity_ok{false};
  std::atomic<bool> performance_request_ok{false};
  std::uint64_t checksum = 0;
};

struct SharedRun {
  std::mutex mutex;
  std::condition_variable cv;
  std::uint32_t ready = 0;
  std::uint32_t warmup_done = 0;
  std::uint32_t finished = 0;
  bool warmup_started = false;
  bool measurement_started = false;
  Clock::time_point warmup_deadline{};
  Clock::time_point measurement_start{};
  Clock::time_point measurement_deadline{};
};

struct Totals {
  std::uint64_t work = 0;
  std::uint64_t elapsed_ns = 0;
  std::uint64_t thread_cpu_time_ns = 0;
  std::uint64_t checksum = 0;
  std::uint32_t affinity_checks = 0;
};

using FrequencyPeaks =
    std::array<std::uint32_t, kMaximumTelemetryPerformanceGroups>;

std::uint32_t PerformanceGroupForCpu(const Topology &topology,
                                     std::uint32_t logical_cpu) {
  for (const CpuInfo &cpu : topology.cpus) {
    if (cpu.logical_cpu == logical_cpu) {
      return cpu.performance_group;
    }
  }
  return kMaximumTelemetryPerformanceGroups;
}

struct FrequencyResidencyBaseline {
  std::uint32_t logical_cpu = 0;
  std::uint32_t performance_group = kMaximumTelemetryPerformanceGroups;
  std::vector<FrequencyResidency> entries;
};

std::vector<FrequencyResidencyBaseline> CaptureFrequencyResidencyBaselines(
    const Topology &topology, const std::vector<std::uint32_t> &logical_cpus) {
  std::vector<FrequencyResidencyBaseline> baselines;
  baselines.reserve(logical_cpus.size());
  for (const std::uint32_t logical_cpu : logical_cpus) {
    baselines.push_back({logical_cpu,
                         PerformanceGroupForCpu(topology, logical_cpu),
                         ReadFrequencyResidency(logical_cpu)});
  }
  return baselines;
}

void UpdateHighestFrequenciesSince(
    const std::vector<FrequencyResidencyBaseline> &baselines,
    std::uint32_t *highest_frequency_khz,
    FrequencyPeaks *highest_frequency_khz_by_group) {
  for (const FrequencyResidencyBaseline &baseline : baselines) {
    const std::vector<FrequencyResidency> current =
        ReadFrequencyResidency(baseline.logical_cpu);
    for (const FrequencyResidency &entry : current) {
      const auto previous =
          std::find_if(baseline.entries.begin(), baseline.entries.end(),
                       [&entry](const FrequencyResidency &candidate) {
                         return candidate.frequency_khz == entry.frequency_khz;
                       });
      if (previous != baseline.entries.end() &&
          entry.residency > previous->residency) {
        *highest_frequency_khz =
            std::max(*highest_frequency_khz, entry.frequency_khz);
        if (baseline.performance_group <
            highest_frequency_khz_by_group->size()) {
          (*highest_frequency_khz_by_group)[baseline.performance_group] =
              std::max(
                  (*highest_frequency_khz_by_group)[baseline.performance_group],
                  entry.frequency_khz);
        }
      }
    }
  }
}

Totals ReadTotals(const std::vector<std::unique_ptr<WorkerContext>> &contexts) {
  Totals totals;
  for (const auto &context : contexts) {
    totals.work += context->completed_work.load(std::memory_order_acquire);
    totals.elapsed_ns = std::max(
        totals.elapsed_ns, context->elapsed_ns.load(std::memory_order_acquire));
    totals.thread_cpu_time_ns +=
        context->thread_cpu_time_ns.load(std::memory_order_acquire);
    totals.affinity_checks +=
        context->affinity_checks.load(std::memory_order_acquire);
    totals.checksum ^= context->checksum + context->logical_cpu;
  }
  return totals;
}

std::uint32_t CountInactiveWorkers(
    const std::vector<std::unique_ptr<WorkerContext>> &contexts) {
  return static_cast<std::uint32_t>(
      std::count_if(contexts.begin(), contexts.end(), [](const auto &context) {
        return !context->affinity_ok.load(std::memory_order_acquire);
      }));
}

bool AnyWorkerPerformanceRequestActive(
    const std::vector<std::unique_ptr<WorkerContext>> &contexts) {
  return std::any_of(contexts.begin(), contexts.end(), [](const auto &context) {
    return context->performance_request_ok.load(std::memory_order_acquire);
  });
}

std::uint32_t CountMeasurementAffinityFailures(
    const std::vector<std::unique_ptr<WorkerContext>> &contexts) {
  return static_cast<std::uint32_t>(
      std::count_if(contexts.begin(), contexts.end(), [](const auto &context) {
        return !context->affinity_ok.load(std::memory_order_acquire) ||
               context->affinity_violated.load(std::memory_order_acquire);
      }));
}

bool PinWorkerWithRetry(WorkerContext *context) {
  for (std::uint32_t attempt = 0; attempt < kAffinityPinAttempts; ++attempt) {
    if (PinCurrentThreadToCpu(context->logical_cpu)) {
      context->affinity_ok.store(true, std::memory_order_release);
      return true;
    }
    if (attempt + 1 < kAffinityPinAttempts) {
      std::this_thread::sleep_for(kAffinityRetryDelay);
    }
  }
  context->affinity_ok.store(false, std::memory_order_release);
  return false;
}

bool CheckWorkerCpu(WorkerContext *context) {
  const std::int32_t actual_cpu = CurrentLogicalCpu();
  if (actual_cpu < 0) {
    return context->affinity_ok.load(std::memory_order_acquire);
  }
  context->affinity_checks.fetch_add(1, std::memory_order_relaxed);
  if (actual_cpu == static_cast<std::int32_t>(context->logical_cpu)) {
    return true;
  }
  context->affinity_violated.store(true, std::memory_order_release);
  return PinWorkerWithRetry(context);
}

void RunWorker(WorkerContext *context, SharedRun *shared,
               std::atomic<bool> *stop_requested, std::uint64_t seed) {
  CpuWorkloadState workload;
  InitializeCpuWorkload(&workload, seed ^ context->logical_cpu);
  context->thread_id.store(CurrentThreadId(), std::memory_order_release);
  context->performance_request_ok.store(
      RequestMaximumPerformanceForCurrentThread(), std::memory_order_release);
  if (PinWorkerWithRetry(context)) {
    CheckWorkerCpu(context);
  }

  Clock::time_point warmup_deadline;
  {
    std::unique_lock<std::mutex> lock(shared->mutex);
    ++shared->ready;
    shared->cv.notify_all();
    shared->cv.wait(lock, [&] {
      return shared->warmup_started ||
             stop_requested->load(std::memory_order_acquire);
    });
    warmup_deadline = shared->warmup_deadline;
  }

  std::uint32_t batches_since_affinity_check = 0;
  while (!stop_requested->load(std::memory_order_acquire) &&
         Clock::now() < warmup_deadline) {
    if (!context->affinity_ok.load(std::memory_order_acquire)) {
      PinWorkerWithRetry(context);
    }
    for (std::uint32_t batch = 0; batch < kWorkloadBatchesPerSchedulingCheck;
         ++batch) {
      RunCpuWorkloadBatch(&workload);
    }
    batches_since_affinity_check += kWorkloadBatchesPerSchedulingCheck;
    if (batches_since_affinity_check >= kAffinityCheckBatches) {
      if (context->affinity_ok.load(std::memory_order_acquire)) {
        CheckWorkerCpu(context);
      } else {
        PinWorkerWithRetry(context);
      }
      batches_since_affinity_check = 0;
    }
  }

  if (!stop_requested->load(std::memory_order_acquire)) {
    PinWorkerWithRetry(context);
  }

  Clock::time_point measurement_start;
  Clock::time_point measurement_deadline;
  {
    std::unique_lock<std::mutex> lock(shared->mutex);
    ++shared->warmup_done;
    shared->cv.notify_all();
    shared->cv.wait(lock, [&] {
      return shared->measurement_started ||
             stop_requested->load(std::memory_order_acquire);
    });
    if (stop_requested->load(std::memory_order_acquire)) {
      context->checksum = workload.checksum;
      return;
    }
    measurement_start = shared->measurement_start;
    measurement_deadline = shared->measurement_deadline;
  }

  context->affinity_violated.store(false, std::memory_order_release);
  while (!stop_requested->load(std::memory_order_acquire) &&
         Clock::now() < measurement_start) {
    std::this_thread::yield();
  }

  const std::uint64_t cpu_time_start = CurrentThreadCpuTimeNs();
  std::uint64_t local_work = 0;
  batches_since_affinity_check = 0;
  while (!stop_requested->load(std::memory_order_acquire) &&
         Clock::now() < measurement_deadline) {
    for (std::uint32_t batch = 0; batch < kWorkloadBatchesPerSchedulingCheck;
         ++batch) {
      RunCpuWorkloadBatch(&workload);
    }
    local_work += static_cast<std::uint64_t>(kCpuWorkUnitsPerBatch) *
                  kWorkloadBatchesPerSchedulingCheck;
    context->completed_work.store(local_work, std::memory_order_release);
    batches_since_affinity_check += kWorkloadBatchesPerSchedulingCheck;
    if (batches_since_affinity_check >= kAffinityCheckBatches) {
      if (context->affinity_ok.load(std::memory_order_acquire)) {
        CheckWorkerCpu(context);
      } else {
        PinWorkerWithRetry(context);
      }
      batches_since_affinity_check = 0;
    }
  }

  const auto end = Clock::now();
  const auto elapsed = end > measurement_start ? end - measurement_start
                                               : Clock::duration::zero();
  const std::uint64_t cpu_time_end = CurrentThreadCpuTimeNs();
  context->completed_work.store(local_work, std::memory_order_release);
  context->elapsed_ns.store(
      static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed)
              .count()),
      std::memory_order_release);
  if (cpu_time_end >= cpu_time_start) {
    context->thread_cpu_time_ns.store(cpu_time_end - cpu_time_start,
                                      std::memory_order_release);
  }
  if (context->affinity_ok.load(std::memory_order_acquire)) {
    CheckWorkerCpu(context);
  }
  context->checksum = workload.checksum;

  {
    std::lock_guard<std::mutex> lock(shared->mutex);
    ++shared->finished;
  }
  shared->cv.notify_all();
}

void JoinAll(std::vector<std::thread> *threads) {
  for (std::thread &thread : *threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

} // namespace

class Engine::Impl final {
public:
  Impl() = default;

  ~Impl() {
    stop_requested_.store(true, std::memory_order_release);
    if (coordinator_.joinable()) {
      coordinator_.join();
    }
  }

  std::int32_t Start(const Request &request, std::uint64_t *run_id) {
    if (run_id == nullptr || !IsCpuTest(request.test_id) ||
        request.duration_ms < 100 || request.duration_ms > 60'000 ||
        request.warmup_ms > 30'000 ||
        request.requested_threads > kMaximumBenchmarkThreads) {
      return kInvalidArgument;
    }

    {
      std::lock_guard<std::mutex> lock(snapshot_mutex_);
      if (IsRunning(snapshot_.state)) {
        return kBusy;
      }
    }

    if (coordinator_.joinable()) {
      coordinator_.join();
    }

    stop_requested_.store(false, std::memory_order_release);
    const std::uint64_t new_run_id =
        next_run_id_.fetch_add(1, std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> lock(snapshot_mutex_);
      snapshot_ = {};
      snapshot_.run_id = new_run_id;
      snapshot_.state = State::kPreparing;
      snapshot_.test_id = request.test_id;
    }

    *run_id = new_run_id;
    coordinator_ = std::thread(&Impl::Run, this, request, new_run_id);
    return kOk;
  }

  std::int32_t RequestStop(std::uint64_t run_id) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (run_id == 0 || run_id != snapshot_.run_id) {
      return kInvalidArgument;
    }
    if (IsRunning(snapshot_.state)) {
      stop_requested_.store(true, std::memory_order_release);
    }
    return kOk;
  }

  Snapshot GetSnapshot() const {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return snapshot_;
  }

private:
  bool StopRequested() const {
    return stop_requested_.load(std::memory_order_acquire);
  }

  void PublishConfiguration(std::uint64_t run_id, TestId test_id,
                            std::uint32_t thread_count,
                            std::int32_t selected_cpu,
                            std::uint64_t quality_flags) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (snapshot_.run_id != run_id) {
      return;
    }
    snapshot_.test_id = test_id;
    snapshot_.thread_count = thread_count;
    snapshot_.selected_cpu = selected_cpu;
    snapshot_.quality_flags = quality_flags;
  }

  void PublishAffinity(std::uint64_t run_id, std::uint32_t affinity_failures) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (snapshot_.run_id != run_id) {
      return;
    }
    snapshot_.affinity_failures = affinity_failures;
    if (affinity_failures != 0) {
      snapshot_.quality_flags |= kQualityAffinityFailed;
    }
  }

  void PublishTelemetry(std::uint64_t run_id, std::uint32_t peak_frequency_khz,
                        const FrequencyPeaks &peak_frequency_khz_by_group,
                        std::uint32_t peak_frequency_group_count,
                        const Totals &totals, std::size_t worker_count,
                        bool performance_hint_active,
                        bool performance_request_active) {
    std::uint32_t utilization_permille = 0;
    if (totals.elapsed_ns != 0 && worker_count != 0) {
      const std::uint64_t available_cpu_ns =
          totals.elapsed_ns * static_cast<std::uint64_t>(worker_count);
      utilization_permille = static_cast<std::uint32_t>(std::min<std::uint64_t>(
          1000, (totals.thread_cpu_time_ns * 1000) / available_cpu_ns));
    }
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (snapshot_.run_id != run_id) {
      return;
    }
    snapshot_.telemetry = PackTelemetry(
        peak_frequency_khz, utilization_permille, performance_hint_active,
        totals.affinity_checks != 0, performance_request_active);
    snapshot_.peak_frequency_group_count = std::min<std::uint32_t>(
        peak_frequency_group_count, kMaximumTelemetryPerformanceGroups);
    snapshot_.peak_frequency_khz_by_group = peak_frequency_khz_by_group;
  }

  void AddQualityFlag(std::uint64_t run_id, std::uint64_t quality_flag) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (snapshot_.run_id == run_id) {
      snapshot_.quality_flags |= quality_flag;
    }
  }

  void PublishState(std::uint64_t run_id, State state, Phase phase) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (snapshot_.run_id != run_id) {
      return;
    }
    snapshot_.state = state;
    snapshot_.phase = phase;
  }

  void PublishMeasurement(std::uint64_t run_id, std::uint64_t elapsed_ns,
                          std::uint64_t completed_work, double score,
                          double progress, double score_variation,
                          double peak_score, std::uint64_t checksum) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (snapshot_.run_id != run_id) {
      return;
    }
    snapshot_.elapsed_ns = elapsed_ns;
    snapshot_.completed_work = completed_work;
    snapshot_.current_value = score;
    snapshot_.progress = std::clamp(progress, 0.0, 1.0);
    snapshot_.score_variation = score_variation;
    snapshot_.peak_score = peak_score;
    checksum_sink_ = checksum;
  }

  void PublishError(std::uint64_t run_id, std::int32_t error_code) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (snapshot_.run_id != run_id) {
      return;
    }
    snapshot_.state = State::kError;
    snapshot_.phase = Phase::kNone;
    snapshot_.error_code = error_code;
  }

  void Run(Request request, std::uint64_t run_id) {
    const Topology topology = DetectTopology();
    std::vector<std::uint32_t> selected_cpus;
    std::int32_t selected_single_cpu = -1;
    std::uint64_t quality_flags = topology.quality_flags;

    if (request.test_id == TestId::kCpuMulti) {
      if ((request.flags & kRequestFlagMultiGroupExplicit) != 0) {
        const auto requested_group = static_cast<std::uint32_t>(
            (request.flags & kRequestMultiGroupMask) >>
            kRequestMultiGroupShift);
        selected_cpus = SelectPerformanceGroupCpus(topology, requested_group);
        if (selected_cpus.empty()) {
          selected_cpus = SelectPresentCpuBenchmarkCpus(
              topology, request.requested_threads);
          quality_flags |= kQualitySelectionFallback;
        }
      } else {
        selected_cpus =
            SelectPresentCpuBenchmarkCpus(topology, request.requested_threads);
        if (request.requested_threads != 0 &&
            selected_cpus.size() < request.requested_threads) {
          quality_flags |= kQualityThreadCountReduced;
        }
      }
    } else {
      if (request.test_id == TestId::kCpuSingle &&
          (request.flags & kRequestFlagSingleCpuExplicit) != 0) {
        const auto requested_cpu = static_cast<std::uint32_t>(
            (request.flags & kRequestSingleCpuMask) >> kRequestSingleCpuShift);
        if (IsSelectableCpu(topology, requested_cpu)) {
          selected_single_cpu = static_cast<std::int32_t>(requested_cpu);
        } else if (topology.preferred_single_cpu >= 0) {
          selected_single_cpu = topology.preferred_single_cpu;
          quality_flags |= kQualitySelectionFallback;
        }
      } else if (topology.preferred_single_cpu >= 0) {
        selected_single_cpu = topology.preferred_single_cpu;
      }
      if (selected_single_cpu >= 0) {
        selected_cpus.push_back(
            static_cast<std::uint32_t>(selected_single_cpu));
      }
    }

    if (selected_cpus.size() > kMaximumBenchmarkThreads) {
      selected_cpus.resize(kMaximumBenchmarkThreads);
      quality_flags |= kQualityTopologyTruncated;
    }
    if (selected_cpus.empty()) {
      PublishError(run_id, kTopologyUnavailable);
      return;
    }

    std::vector<std::uint32_t> selected_cpu_groups;
    selected_cpu_groups.reserve(selected_cpus.size());
    for (const std::uint32_t cpu : selected_cpus) {
      selected_cpu_groups.push_back(PerformanceGroupForCpu(topology, cpu));
    }
    const std::uint32_t peak_frequency_group_count = std::min<std::uint32_t>(
        std::max<std::uint32_t>(topology.performance_group_count, 1U),
        kMaximumTelemetryPerformanceGroups);
    std::uint32_t peak_frequency_khz = 0;
    FrequencyPeaks peak_frequency_khz_by_group{};
    const std::vector<FrequencyResidencyBaseline>
        frequency_residency_baselines =
            CaptureFrequencyResidencyBaselines(topology, selected_cpus);

    PublishConfiguration(run_id, request.test_id,
                         static_cast<std::uint32_t>(selected_cpus.size()),
                         selected_single_cpu, quality_flags);

    SharedRun shared;
    std::vector<std::unique_ptr<WorkerContext>> contexts;
    std::vector<std::thread> workers;
    contexts.reserve(selected_cpus.size());
    workers.reserve(selected_cpus.size());

    try {
      for (std::uint32_t cpu : selected_cpus) {
        contexts.push_back(std::make_unique<WorkerContext>(cpu));
        workers.emplace_back(RunWorker, contexts.back().get(), &shared,
                             &stop_requested_, run_id * 0x9E3779B97F4A7C15ULL);
      }
    } catch (const std::system_error &) {
      stop_requested_.store(true, std::memory_order_release);
      {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.warmup_started = true;
        shared.measurement_started = true;
      }
      shared.cv.notify_all();
      JoinAll(&workers);
      PublishError(run_id, kThreadCreationFailed);
      return;
    }

    {
      std::unique_lock<std::mutex> lock(shared.mutex);
      shared.cv.wait(lock,
                     [&] { return shared.ready == selected_cpus.size(); });
    }

    std::vector<std::int32_t> worker_thread_ids;
    worker_thread_ids.reserve(contexts.size());
    for (const auto &context : contexts) {
      const std::int32_t thread_id =
          context->thread_id.load(std::memory_order_acquire);
      if (thread_id > 0) {
        worker_thread_ids.push_back(thread_id);
      }
    }

    PerformanceHintSession performance_hint(worker_thread_ids,
                                            kMinimumHintUpdateNs / 2);
    const bool performance_hint_boost_requested =
        performance_hint.RequestMaximumPerformance();
    const bool performance_request_active =
        AnyWorkerPerformanceRequestActive(contexts) ||
        performance_hint.IsActive() || performance_hint_boost_requested;
    if (!performance_request_active) {
      quality_flags |= kQualityPerformanceRequestUnavailable;
    }
    const std::int64_t preferred_hint_update_ns =
        performance_hint.PreferredUpdateRateNs();
    const std::int64_t hint_update_ns = std::clamp<std::int64_t>(
        preferred_hint_update_ns > 0 ? preferred_hint_update_ns
                                     : kMinimumHintUpdateNs,
        kMinimumHintUpdateNs, kMaximumHintUpdateNs);
    performance_hint.UpdateTargetDuration(hint_update_ns / 2);
    performance_hint.ReportActualDuration(hint_update_ns);
    auto last_hint_report = Clock::now();
    auto next_hint_report =
        last_hint_report + std::chrono::nanoseconds(hint_update_ns);

    PublishState(run_id, State::kWarmingUp, Phase::kWarmUp);
    {
      std::lock_guard<std::mutex> lock(shared.mutex);
      shared.warmup_deadline =
          Clock::now() + std::chrono::milliseconds(request.warmup_ms);
      shared.warmup_started = true;
    }
    shared.cv.notify_all();

    {
      std::unique_lock<std::mutex> lock(shared.mutex);
      while (shared.warmup_done != selected_cpus.size()) {
        if (shared.cv.wait_until(lock, next_hint_report, [&] {
              return shared.warmup_done == selected_cpus.size();
            })) {
          break;
        }
        const auto now = Clock::now();
        lock.unlock();
        performance_hint.ReportActualDuration(std::max<std::int64_t>(
            1, std::chrono::duration_cast<std::chrono::nanoseconds>(
                   now - last_hint_report)
                   .count()));
        lock.lock();
        last_hint_report = now;
        next_hint_report = now + std::chrono::nanoseconds(hint_update_ns);
      }
    }

    const std::uint32_t inactive_worker_count = CountInactiveWorkers(contexts);
    const std::size_t active_worker_count = contexts.size();
    PublishConfiguration(run_id, request.test_id,
                         static_cast<std::uint32_t>(active_worker_count),
                         selected_single_cpu, quality_flags);
    PublishAffinity(run_id, inactive_worker_count);

    if (StopRequested()) {
      {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.measurement_started = true;
      }
      shared.cv.notify_all();
      JoinAll(&workers);
      const Totals stopped_totals = ReadTotals(contexts);
      PublishAffinity(run_id, CountInactiveWorkers(contexts));
      UpdateHighestFrequenciesSince(frequency_residency_baselines,
                                    &peak_frequency_khz,
                                    &peak_frequency_khz_by_group);
      PublishTelemetry(run_id, peak_frequency_khz, peak_frequency_khz_by_group,
                       peak_frequency_group_count, stopped_totals,
                       active_worker_count, performance_hint.IsActive(),
                       performance_request_active);
      PublishState(run_id, State::kCancelled, Phase::kNone);
      return;
    }

    const auto measurement_start = Clock::now() + std::chrono::milliseconds(20);
    const auto measurement_deadline =
        measurement_start + std::chrono::milliseconds(request.duration_ms);
    PublishState(run_id, State::kMeasuring, Phase::kMeasure);
    {
      std::lock_guard<std::mutex> lock(shared.mutex);
      shared.measurement_start = measurement_start;
      shared.measurement_deadline = measurement_deadline;
      shared.measurement_started = true;
    }
    shared.cv.notify_all();

    const std::uint64_t requested_duration_ns =
        static_cast<std::uint64_t>(request.duration_ms) * 1'000'000ULL;
    auto next_publish = measurement_start;
    auto next_frequency_sample = measurement_start;
    last_hint_report = measurement_start;
    next_hint_report =
        measurement_start + std::chrono::nanoseconds(hint_update_ns);
    std::uint32_t boundary_index = 1;
    std::uint64_t previous_window_work = 0;
    std::uint64_t previous_window_elapsed = 0;
    std::vector<double> window_scores;
    window_scores.reserve(kWindowCount);
    double peak_score = 0.0;

    while (true) {
      const auto now = Clock::now();
      const std::uint64_t live_elapsed_ns =
          now > measurement_start
              ? static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        now - measurement_start)
                        .count())
              : 0;
      const Totals totals = ReadTotals(contexts);

      if (now >= next_hint_report) {
        performance_hint.ReportActualDuration(std::max<std::int64_t>(
            1, std::chrono::duration_cast<std::chrono::nanoseconds>(
                   now - last_hint_report)
                   .count()));
        last_hint_report = now;
        next_hint_report = now + std::chrono::nanoseconds(hint_update_ns);
      }

      if (now >= next_frequency_sample) {
        for (std::size_t index = 0; index < selected_cpus.size(); ++index) {
          const std::uint32_t frequency_khz =
              ReadCurrentFrequencyKhz(selected_cpus[index]);
          peak_frequency_khz = std::max(peak_frequency_khz, frequency_khz);
          const std::uint32_t performance_group = selected_cpu_groups[index];
          if (performance_group < peak_frequency_khz_by_group.size()) {
            peak_frequency_khz_by_group[performance_group] = std::max(
                peak_frequency_khz_by_group[performance_group], frequency_khz);
          }
        }
        PublishAffinity(run_id, CountMeasurementAffinityFailures(contexts));
        PublishTelemetry(
            run_id, peak_frequency_khz, peak_frequency_khz_by_group,
            peak_frequency_group_count, totals, active_worker_count,
            performance_hint.IsActive(), performance_request_active);
        next_frequency_sample = now + std::chrono::milliseconds(200);
      }

      if (boundary_index < kWindowCount &&
          live_elapsed_ns >=
              (requested_duration_ns * boundary_index) / kWindowCount) {
        const std::uint64_t window_work = totals.work - previous_window_work;
        const std::uint64_t window_elapsed =
            live_elapsed_ns - previous_window_elapsed;
        const double window_score = ScoreFrom(window_work, window_elapsed);
        window_scores.push_back(window_score);
        peak_score = std::max(peak_score, window_score);
        previous_window_work = totals.work;
        previous_window_elapsed = live_elapsed_ns;
        ++boundary_index;
      }

      if (now >= next_publish) {
        PublishMeasurement(run_id, live_elapsed_ns, totals.work,
                           ScoreFrom(totals.work, live_elapsed_ns),
                           static_cast<double>(live_elapsed_ns) /
                               static_cast<double>(requested_duration_ns),
                           0.0, peak_score, totals.checksum);
        next_publish = now + std::chrono::milliseconds(50);
      }

      bool finished = false;
      {
        std::lock_guard<std::mutex> lock(shared.mutex);
        finished = shared.finished == selected_cpus.size();
      }
      if (finished) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    JoinAll(&workers);
    for (std::size_t index = 0; index < selected_cpus.size(); ++index) {
      const std::uint32_t frequency_khz =
          ReadCurrentFrequencyKhz(selected_cpus[index]);
      peak_frequency_khz = std::max(peak_frequency_khz, frequency_khz);
      const std::uint32_t performance_group = selected_cpu_groups[index];
      if (performance_group < peak_frequency_khz_by_group.size()) {
        peak_frequency_khz_by_group[performance_group] = std::max(
            peak_frequency_khz_by_group[performance_group], frequency_khz);
      }
    }
    UpdateHighestFrequenciesSince(frequency_residency_baselines,
                                  &peak_frequency_khz,
                                  &peak_frequency_khz_by_group);
    const Totals final_totals = ReadTotals(contexts);
    PublishAffinity(run_id, CountMeasurementAffinityFailures(contexts));
    PublishTelemetry(run_id, peak_frequency_khz, peak_frequency_khz_by_group,
                     peak_frequency_group_count, final_totals,
                     active_worker_count, performance_hint.IsActive(),
                     performance_request_active);
    if (final_totals.elapsed_ns > previous_window_elapsed &&
        final_totals.work >= previous_window_work) {
      const double final_window_score =
          ScoreFrom(final_totals.work - previous_window_work,
                    final_totals.elapsed_ns - previous_window_elapsed);
      window_scores.push_back(final_window_score);
      peak_score = std::max(peak_score, final_window_score);
    }

    if (StopRequested()) {
      PublishMeasurement(run_id, final_totals.elapsed_ns, final_totals.work,
                         ScoreFrom(final_totals.work, final_totals.elapsed_ns),
                         static_cast<double>(final_totals.elapsed_ns) /
                             static_cast<double>(requested_duration_ns),
                         0.0, peak_score, final_totals.checksum);
      PublishState(run_id, State::kCancelled, Phase::kNone);
      return;
    }

    double final_score = ScoreFrom(final_totals.work, final_totals.elapsed_ns);
    double variation = 0.0;
    if (window_scores.size() >= kWindowCount) {
      std::sort(window_scores.begin(), window_scores.end());
      final_score = window_scores[window_scores.size() / 2];
      if (final_score > 0.0) {
        variation =
            (window_scores.back() - window_scores.front()) / final_score;
      }
    }
    if (variation > 0.07) {
      AddQualityFlag(run_id, kQualityHighScoreVariation);
    }

    PublishMeasurement(run_id, final_totals.elapsed_ns, final_totals.work,
                       final_score, 1.0, variation, peak_score,
                       final_totals.checksum);
    PublishState(run_id, State::kCompleted, Phase::kFinalize);
  }

  mutable std::mutex snapshot_mutex_;
  Snapshot snapshot_{};
  std::thread coordinator_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<std::uint64_t> next_run_id_{1};
  volatile std::uint64_t checksum_sink_ = 0;
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

std::int32_t Engine::Start(const Request &request, std::uint64_t *run_id) {
  return impl_->Start(request, run_id);
}

std::int32_t Engine::RequestStop(std::uint64_t run_id) {
  return impl_->RequestStop(run_id);
}

Snapshot Engine::GetSnapshot() const { return impl_->GetSnapshot(); }

} // namespace benchmark
