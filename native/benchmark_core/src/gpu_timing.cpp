#include "benchmark/gpu_timing.h"

#include <cmath>

namespace benchmark::detail {
namespace {

constexpr double kMinimumValidatedBatchSeconds = 0.002;
constexpr double kMinimumGpuToHostRatio = 0.70;
constexpr double kMaximumGpuToHostRatio = 1.10;

} // namespace

GpuElapsedSelection SelectGpuElapsedSeconds(double host_seconds,
                                            double gpu_seconds,
                                            bool timestamp_valid) {
  if (!(host_seconds > 0.0) || !std::isfinite(host_seconds)) {
    return {};
  }
  GpuElapsedSelection selection{host_seconds, false, false};
  if (!timestamp_valid) {
    selection.disable_gpu_timestamps = true;
    return selection;
  }
  if (host_seconds < kMinimumValidatedBatchSeconds) {
    return selection;
  }
  if (!(gpu_seconds > 0.0) || !std::isfinite(gpu_seconds)) {
    selection.disable_gpu_timestamps = true;
    return selection;
  }
  const double ratio = gpu_seconds / host_seconds;
  if (ratio < kMinimumGpuToHostRatio || ratio > kMaximumGpuToHostRatio) {
    return selection;
  }
  selection.seconds = gpu_seconds;
  selection.used_gpu_timestamp = true;
  return selection;
}

} // namespace benchmark::detail
