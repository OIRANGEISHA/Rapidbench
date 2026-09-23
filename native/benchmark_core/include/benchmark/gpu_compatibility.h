#ifndef RAPIDBENCH_GPU_COMPATIBILITY_H_
#define RAPIDBENCH_GPU_COMPATIBILITY_H_
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace benchmark::detail {
// Independent pipeline outcomes; no vendor/model-based decisions.
struct GpuPipelineSupport {
  bool fp32 = false, fp16_native = false, fp16_emulated = false;
  bool int32 = false, mixed = false, memory = false;
  std::uint32_t AvailableMask() const {
    return (fp32 ? 1U << 1 : 0U) |
           (fp16_native || fp16_emulated ? 1U << 2 : 0U) |
           (int32 ? 1U << 3 : 0U) | (mixed ? 1U << 4 : 0U) |
           (memory ? 1U << 5 : 0U);
  }
};
enum class GpuItemState : std::uint32_t {
  kReady, kUnavailable, kRunning, kCompleted, kStopped, kFailed
};
struct GpuItemDiagnostics {
  std::uint64_t run_id = 0;
  GpuItemState state = GpuItemState::kReady;
  double host_seconds = 0, gpu_seconds = 0;
  std::uint32_t gpu_observations = 0, host_batches = 0, timestamp_batches = 0;
  std::uint32_t fp_accumulators = 0;
  std::string reason;
  void AddBatch(double host, double gpu, bool used_timestamp) {
    if (host > 0 && std::isfinite(host)) host_seconds += host;
    // Raw positive finite observations are diagnostics, not validated timings.
    if (gpu > 0 && std::isfinite(gpu)) {
      gpu_seconds += gpu;
      ++gpu_observations;
    }
    if (used_timestamp) ++timestamp_batches;
    else ++host_batches;
  }
};
using GpuDiagnostics = std::array<GpuItemDiagnostics, 6>;
std::string GpuDiagnosticsJson(std::uint64_t run_id,
                                const GpuDiagnostics &items, bool fatal);
} // namespace benchmark::detail
#endif
