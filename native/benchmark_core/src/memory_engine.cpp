#include "benchmark/memory_engine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#if defined(_WIN32)
#include <malloc.h>
#endif
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "benchmark/memory_kernels.h"
#include "benchmark/performance_hint.h"
#include "benchmark/topology.h"

namespace benchmark {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kStateIdle = 0U;
constexpr std::uint32_t kStatePreparing = 1U;
constexpr std::uint32_t kStateWarmingUp = 2U;
constexpr std::uint32_t kStateMeasuring = 3U;
constexpr std::uint32_t kStateCompleted = 4U;
constexpr std::uint32_t kStateCancelled = 5U;
constexpr std::uint32_t kStateError = 6U;
constexpr std::int32_t kStatusOk = 0;
constexpr std::int32_t kStatusInvalidArgument = -1;
constexpr std::int32_t kStatusBusy = -2;
constexpr std::int32_t kStatusInternalError = -3;
constexpr std::int32_t kErrorAllocationFailed = -20;
constexpr std::int32_t kErrorNoCpuAvailable = -21;
constexpr std::int32_t kErrorThreadCreationFailed = -22;
constexpr std::size_t kTargetBufferBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMinimumBufferBytes = 32ULL * 1024ULL * 1024ULL;
constexpr std::size_t kAlignment = 128U;
constexpr std::int64_t kMinimumHintUpdateNs = 20'000'000;
constexpr std::int64_t kMaximumHintUpdateNs = 100'000'000;

struct alignas(64) ByteCounter {
  std::atomic<std::uint64_t> value{0U};
};

std::uint64_t NowNs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          Clock::now().time_since_epoch())
          .count());
}

std::size_t RoundDown(std::size_t value, std::size_t alignment) {
  return value - (value % alignment);
}

std::size_t AvailableMemoryBytes() {
  std::ifstream stream("/proc/meminfo");
  std::string key;
  std::uint64_t value_kib = 0U;
  std::string unit;
  while (stream >> key >> value_kib >> unit) {
    if (key == "MemAvailable:") {
      return static_cast<std::size_t>(value_kib * 1024ULL);
    }
  }
  return 0U;
}

std::size_t ChooseBufferBytes(std::size_t thread_count) {
  std::size_t target = kTargetBufferBytes;
  const std::size_t available = AvailableMemoryBytes();
  if (available > 0U) {
    target = std::min(target, available / 8U);
  }
  target = std::max(target, kMinimumBufferBytes);
  const std::size_t granularity =
      std::max<std::size_t>(kAlignment, thread_count * kAlignment * 2U);
  return RoundDown(target, granularity);
}

void FreeAligned(void *memory) {
#if defined(_WIN32)
  _aligned_free(memory);
#else
  std::free(memory);
#endif
}

using AlignedBuffer = std::unique_ptr<std::uint8_t, decltype(&FreeAligned)>;

AlignedBuffer AllocateBuffer(std::size_t *byte_count) {
  std::size_t requested = *byte_count;
  while (requested >= kMinimumBufferBytes) {
    requested = RoundDown(requested, kAlignment);
    void *memory = nullptr;
#if defined(_WIN32)
    memory = _aligned_malloc(requested, kAlignment);
    const bool allocated = memory != nullptr;
#else
    const bool allocated =
        posix_memalign(&memory, kAlignment, requested) == 0 &&
        memory != nullptr;
#endif
    if (allocated) {
      *byte_count = requested;
      return AlignedBuffer(static_cast<std::uint8_t *>(memory), &FreeAligned);
    }
    requested /= 2U;
  }
  return AlignedBuffer(nullptr, &FreeAligned);
}

bool IsTerminal(std::uint32_t state) {
  return state == kStateCompleted || state == kStateCancelled ||
         state == kStateError || state == kStateIdle;
}

} // namespace

MemoryEngine::MemoryEngine() { snapshot_.state = kStateIdle; }

MemoryEngine::~MemoryEngine() {
  stop_requested_.store(true, std::memory_order_release);
  if (coordinator_.joinable()) {
    coordinator_.join();
  }
}

std::int32_t MemoryEngine::Start(const MemoryRequest &request,
                                 std::uint64_t *out_run_id) {
  if (out_run_id == nullptr || request.duration_ms < 250U ||
      request.duration_ms > 30000U || request.warmup_ms > 5000U ||
      (request.test != MemoryTest::kRead &&
       request.test != MemoryTest::kWrite &&
       request.test != MemoryTest::kCopy)) {
    return kStatusInvalidArgument;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!IsTerminal(snapshot_.state)) {
      return kStatusBusy;
    }
  }
  if (coordinator_.joinable()) {
    coordinator_.join();
  }

  const std::uint64_t run_id =
      next_run_id_.fetch_add(1U, std::memory_order_relaxed);
  stop_requested_.store(false, std::memory_order_release);
  MemorySnapshot initial{};
  initial.run_id = run_id;
  initial.state = kStatePreparing;
  initial.test = request.test;
  Publish(initial);
  try {
    coordinator_ = std::thread(&MemoryEngine::Run, this, request, run_id);
  } catch (...) {
    initial.state = kStateError;
    initial.error_code = kStatusInternalError;
    Publish(initial);
    return kStatusInternalError;
  }
  *out_run_id = run_id;
  return kStatusOk;
}

std::int32_t MemoryEngine::RequestStop(std::uint64_t run_id) {
  const MemorySnapshot snapshot = GetSnapshot();
  if (run_id == 0U || snapshot.run_id != run_id) {
    return kStatusInvalidArgument;
  }
  stop_requested_.store(true, std::memory_order_release);
  return kStatusOk;
}

MemorySnapshot MemoryEngine::GetSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

void MemoryEngine::Publish(const MemorySnapshot &snapshot) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = snapshot;
}

void MemoryEngine::Run(MemoryRequest request, std::uint64_t run_id) {
  MemorySnapshot snapshot{};
  snapshot.run_id = run_id;
  snapshot.state = kStatePreparing;
  snapshot.test = request.test;

  const Topology topology = DetectTopology();
  std::vector<std::uint32_t> cpu_ids = SelectBenchmarkCpus(topology, 0U);
  if (cpu_ids.empty()) {
    snapshot.state = kStateError;
    snapshot.error_code = kErrorNoCpuAvailable;
    Publish(snapshot);
    return;
  }

  std::size_t buffer_bytes = ChooseBufferBytes(cpu_ids.size());
  AlignedBuffer buffer = AllocateBuffer(&buffer_bytes);
  if (!buffer) {
    snapshot.state = kStateError;
    snapshot.error_code = kErrorAllocationFailed;
    Publish(snapshot);
    return;
  }
  snapshot.buffer_bytes = buffer_bytes;
  snapshot.thread_count = static_cast<std::uint32_t>(cpu_ids.size());
  MemoryWriteKernel(buffer.get(), buffer_bytes, 0xA5C31F7296D84BE0ULL);

  const bool is_copy = request.test == MemoryTest::kCopy;
  const std::size_t active_bytes = is_copy ? buffer_bytes / 2U : buffer_bytes;
  const std::size_t region_bytes =
      RoundDown(active_bytes / cpu_ids.size(), kAlignment);
  if (region_bytes == 0U) {
    snapshot.state = kStateError;
    snapshot.error_code = kErrorAllocationFailed;
    Publish(snapshot);
    return;
  }

  std::atomic<std::uint32_t> phase{0U};
  std::atomic<std::uint32_t> ready_count{0U};
  std::atomic<std::uint32_t> warm_count{0U};
  std::atomic<std::uint32_t> affinity_failures{0U};
  std::atomic<std::uint32_t> performance_request_failures{0U};
  std::atomic<std::uint64_t> warmup_end_ns{0U};
  std::atomic<std::uint64_t> measure_end_ns{0U};
  auto processed = std::make_unique<ByteCounter[]>(cpu_ids.size());
  for (std::size_t index = 0U; index < cpu_ids.size(); ++index) {
    processed[index].value.store(0U, std::memory_order_relaxed);
  }
  std::vector<std::int32_t> worker_thread_ids(cpu_ids.size(), -1);
  // Read and Write report their payload once. Copy reports the total memory
  // traffic generated by the kernel: one source read plus one destination
  // write, matching the bidirectional convention used by AIDA64-style tests.
  const std::uint64_t processed_bytes_per_pass =
      static_cast<std::uint64_t>(region_bytes) * (is_copy ? 2ULL : 1ULL);

  std::vector<std::thread> workers;
  workers.reserve(cpu_ids.size());
  bool creation_failed = false;
  try {
    for (std::size_t index = 0U; index < cpu_ids.size(); ++index) {
      workers.emplace_back([&, index]() {
        if (!PinCurrentThreadToCpu(cpu_ids[index])) {
          affinity_failures.fetch_add(1U, std::memory_order_relaxed);
        }
        if (!RequestMaximumPerformanceForCurrentThread()) {
          performance_request_failures.fetch_add(1U, std::memory_order_relaxed);
        }
        worker_thread_ids[index] = CurrentThreadId();
        std::uint8_t *destination = buffer.get() + index * region_bytes;
        const std::uint8_t *source = destination;
        if (is_copy) {
          destination = buffer.get() + active_bytes + index * region_bytes;
        }
        const auto run_kernel = [&]() {
          switch (request.test) {
          case MemoryTest::kRead:
            MemoryReadKernel(source, region_bytes);
            break;
          case MemoryTest::kWrite:
            MemoryWriteKernel(destination, region_bytes,
                              0xD127B94E65A308FCULL + index);
            break;
          case MemoryTest::kCopy:
            MemoryCopyKernel(destination, source, region_bytes);
            break;
          default:
            break;
          }
        };

        ready_count.fetch_add(1U, std::memory_order_release);
        while (phase.load(std::memory_order_acquire) < 1U) {
          std::this_thread::yield();
        }
        while (!stop_requested_.load(std::memory_order_acquire) &&
               NowNs() < warmup_end_ns.load(std::memory_order_acquire)) {
          run_kernel();
        }
        warm_count.fetch_add(1U, std::memory_order_release);
        while (phase.load(std::memory_order_acquire) < 2U) {
          std::this_thread::yield();
        }
        while (!stop_requested_.load(std::memory_order_acquire) &&
               NowNs() < measure_end_ns.load(std::memory_order_acquire)) {
          run_kernel();
          processed[index].value.fetch_add(processed_bytes_per_pass,
                                           std::memory_order_relaxed);
        }
      });
    }
  } catch (...) {
    creation_failed = true;
  }

  if (creation_failed) {
    stop_requested_.store(true, std::memory_order_release);
  }
  while (ready_count.load(std::memory_order_acquire) < workers.size()) {
    std::this_thread::yield();
  }

  std::vector<std::int32_t> active_thread_ids;
  active_thread_ids.reserve(workers.size());
  for (std::size_t index = 0U; index < workers.size(); ++index) {
    if (worker_thread_ids[index] > 0) {
      active_thread_ids.push_back(worker_thread_ids[index]);
    }
  }
  PerformanceHintSession performance_hint(active_thread_ids,
                                          kMinimumHintUpdateNs / 2);
  const bool performance_hint_available = performance_hint.IsActive();
  const bool maximum_performance_requested =
      performance_hint.RequestMaximumPerformance();
  const auto effective_performance_request_failures = [&]() {
    return performance_hint_available || maximum_performance_requested
               ? 0U
               : performance_request_failures.load(std::memory_order_relaxed);
  };
  const std::int64_t preferred_hint_update_ns =
      performance_hint.PreferredUpdateRateNs();
  const std::int64_t hint_update_ns = std::clamp<std::int64_t>(
      preferred_hint_update_ns > 0 ? preferred_hint_update_ns
                                   : kMinimumHintUpdateNs,
      kMinimumHintUpdateNs, kMaximumHintUpdateNs);
  performance_hint.UpdateTargetDuration(hint_update_ns / 2);
  performance_hint.ReportActualDuration(hint_update_ns);
  auto last_hint_report = Clock::now();

  snapshot.thread_count = static_cast<std::uint32_t>(workers.size());
  snapshot.state = kStateWarmingUp;
  snapshot.affinity_failures =
      affinity_failures.load(std::memory_order_relaxed);
  snapshot.performance_request_failures =
      effective_performance_request_failures();
  Publish(snapshot);
  warmup_end_ns.store(NowNs() + static_cast<std::uint64_t>(request.warmup_ms) *
                                    1000000ULL,
                      std::memory_order_release);
  phase.store(1U, std::memory_order_release);
  while (warm_count.load(std::memory_order_acquire) < workers.size()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto now = Clock::now();
    const auto since_last_hint =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now -
                                                             last_hint_report)
            .count();
    if (since_last_hint >= hint_update_ns) {
      performance_hint.ReportActualDuration(since_last_hint);
      last_hint_report = now;
    }
  }

  const std::uint64_t measure_start_ns = NowNs();
  const std::uint64_t measure_duration_ns =
      static_cast<std::uint64_t>(request.duration_ms) * 1000000ULL;
  measure_end_ns.store(measure_start_ns + measure_duration_ns,
                       std::memory_order_release);
  snapshot.state = kStateMeasuring;
  snapshot.elapsed_ns = 0U;
  snapshot.progress = 0.0;
  Publish(snapshot);
  phase.store(2U, std::memory_order_release);

  while (!stop_requested_.load(std::memory_order_acquire)) {
    const std::uint64_t now_ns = NowNs();
    if (now_ns >= measure_start_ns + measure_duration_ns) {
      break;
    }
    std::uint64_t total_bytes = 0U;
    for (std::size_t index = 0U; index < workers.size(); ++index) {
      total_bytes += processed[index].value.load(std::memory_order_relaxed);
    }
    snapshot.elapsed_ns = now_ns - measure_start_ns;
    snapshot.processed_bytes = total_bytes;
    snapshot.bandwidth_gbps =
        snapshot.elapsed_ns == 0U
            ? 0.0
            : static_cast<double>(total_bytes) /
                  static_cast<double>(snapshot.elapsed_ns);
    snapshot.progress =
        std::min(1.0, static_cast<double>(snapshot.elapsed_ns) /
                          static_cast<double>(measure_duration_ns));
    snapshot.affinity_failures =
        affinity_failures.load(std::memory_order_relaxed);
    snapshot.performance_request_failures =
        effective_performance_request_failures();
    const auto hint_now = Clock::now();
    const auto since_last_hint =
        std::chrono::duration_cast<std::chrono::nanoseconds>(hint_now -
                                                             last_hint_report)
            .count();
    if (since_last_hint >= hint_update_ns) {
      performance_hint.ReportActualDuration(since_last_hint);
      last_hint_report = hint_now;
    }
    Publish(snapshot);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }
  stop_requested_.store(stop_requested_.load(std::memory_order_acquire),
                        std::memory_order_release);
  for (std::thread &worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }

  const std::uint64_t finish_ns = NowNs();
  std::uint64_t total_bytes = 0U;
  for (std::size_t index = 0U; index < workers.size(); ++index) {
    total_bytes += processed[index].value.load(std::memory_order_relaxed);
  }
  snapshot.elapsed_ns =
      std::min(finish_ns - measure_start_ns, measure_duration_ns);
  snapshot.processed_bytes = total_bytes;
  snapshot.bandwidth_gbps = snapshot.elapsed_ns == 0U
                                ? 0.0
                                : static_cast<double>(total_bytes) /
                                      static_cast<double>(snapshot.elapsed_ns);
  snapshot.progress =
      snapshot.elapsed_ns == 0U
          ? 0.0
          : std::min(1.0, static_cast<double>(snapshot.elapsed_ns) /
                              static_cast<double>(measure_duration_ns));
  snapshot.affinity_failures =
      affinity_failures.load(std::memory_order_relaxed);
  snapshot.performance_request_failures =
      effective_performance_request_failures();
  if (creation_failed) {
    snapshot.state = kStateError;
    snapshot.error_code = kErrorThreadCreationFailed;
  } else if (stop_requested_.load(std::memory_order_acquire)) {
    snapshot.state = kStateCancelled;
  } else {
    snapshot.state = kStateCompleted;
    snapshot.progress = 1.0;
  }
  Publish(snapshot);
}

} // namespace benchmark
