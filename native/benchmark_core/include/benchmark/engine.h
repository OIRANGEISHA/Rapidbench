#ifndef CPU_BENCHMARK_ENGINE_H_
#define CPU_BENCHMARK_ENGINE_H_

#include <array>
#include <cstdint>
#include <memory>

namespace benchmark {

constexpr std::uint32_t kMaximumTelemetryPerformanceGroups = 16U;

enum class State : std::uint32_t {
  kIdle = 0,
  kPreparing = 1,
  kWarmingUp = 2,
  kMeasuring = 3,
  kCompleted = 4,
  kCancelled = 5,
  kError = 6,
};

enum class Phase : std::uint32_t {
  kNone = 0,
  kWarmUp = 1,
  kMeasure = 2,
  kFinalize = 3,
};

enum class TestId : std::uint32_t {
  kPhase1Workload = 1,
  kCpuSingle = 2,
  kCpuMulti = 3,
};

struct Request {
  TestId test_id = TestId::kCpuSingle;
  std::uint32_t duration_ms = 0;
  std::uint32_t warmup_ms = 0;
  std::uint32_t requested_threads = 0;
  std::uint64_t flags = 0;
};

struct Snapshot {
  std::uint64_t run_id = 0;
  State state = State::kIdle;
  Phase phase = Phase::kNone;
  TestId test_id = TestId::kCpuSingle;
  std::uint32_t thread_count = 0;
  std::uint64_t quality_flags = 0;
  std::uint64_t elapsed_ns = 0;
  std::uint64_t completed_work = 0;
  double current_value = 0.0;
  double progress = 0.0;
  double score_variation = 0.0;
  double peak_score = 0.0;
  std::int32_t error_code = 0;
  std::int32_t selected_cpu = -1;
  std::uint32_t affinity_failures = 0;
  std::uint32_t telemetry = 0;
  std::uint32_t peak_frequency_group_count = 0;
  std::array<std::uint32_t, kMaximumTelemetryPerformanceGroups>
      peak_frequency_khz_by_group{};
};

class Engine final {
public:
  Engine();
  ~Engine();

  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;

  std::int32_t Start(const Request &request, std::uint64_t *run_id);
  std::int32_t RequestStop(std::uint64_t run_id);
  Snapshot GetSnapshot() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace benchmark

#endif // CPU_BENCHMARK_ENGINE_H_
