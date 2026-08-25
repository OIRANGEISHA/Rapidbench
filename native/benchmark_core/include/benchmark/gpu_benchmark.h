#ifndef RAPIDBENCH_GPU_BENCHMARK_H_
#define RAPIDBENCH_GPU_BENCHMARK_H_

#include <cstdint>
#include <memory>
#include <string>

namespace benchmark {

enum class GpuTest : std::uint32_t {
  kNone = 0,
  kFp32 = 1,
  kFp16 = 2,
  kInt32 = 3,
  kMixed = 4,
  kMemoryBandwidth = 5,
  kAll = 6,
};

enum class GpuState : std::uint32_t {
  kIdle = 0,
  kWarmingUp = 1,
  kRunning = 2,
  kStopping = 3,
  kStopped = 4,
  kCompleted = 5,
  kError = 6,
};

enum class GpuFp16Mode : std::uint32_t {
  kEmulated = 0,
  kNative = 1,
};

enum class GpuTimingMode : std::uint32_t {
  kHostFallback = 0,
  kGpuTimestamp = 1,
};

struct GpuRequest {
  GpuTest test = GpuTest::kNone;
  std::uint32_t duration_ms = 6000;
  std::uint32_t warmup_ms = 700;
};

struct GpuSnapshot {
  std::uint64_t run_id = 0;
  GpuState state = GpuState::kIdle;
  GpuTest active_test = GpuTest::kNone;
  GpuFp16Mode fp16_mode = GpuFp16Mode::kEmulated;
  GpuTimingMode timing_mode = GpuTimingMode::kHostFallback;
  std::int32_t error_code = 0;
  bool overall_score_valid = false;
  bool reduced_working_set = false;
  bool vulkan_available = false;
  std::uint64_t elapsed_ns = 0;
  std::uint64_t buffer_bytes = 0;
  double fp32_gflops = 0.0;
  double fp16_gflops = 0.0;
  double fp16_scaling = 0.0;
  double int32_gops = 0.0;
  double mixed_gwork = 0.0;
  double memory_bandwidth_gbps = 0.0;
  double overall_score = 0.0;
  double progress = 0.0;
  double gpu_batch_ms = 0.0;
  std::uint32_t workgroup_size = 64;
  std::uint32_t dispatch_count = 0;
  std::uint32_t iteration_count = 64;
  std::string last_error;
};

class GpuBenchmarkEngine {
public:
  GpuBenchmarkEngine();
  ~GpuBenchmarkEngine();

  GpuBenchmarkEngine(const GpuBenchmarkEngine &) = delete;
  GpuBenchmarkEngine &operator=(const GpuBenchmarkEngine &) = delete;

  std::int32_t Start(const GpuRequest &request, std::uint64_t *out_run_id);
  std::int32_t RequestStop(std::uint64_t run_id);
  GpuSnapshot GetSnapshot() const;
  std::string GetCapabilitiesJson() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace benchmark

#endif // RAPIDBENCH_GPU_BENCHMARK_H_
