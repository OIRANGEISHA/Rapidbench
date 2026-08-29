#include <cmath>
#include <iostream>
#include <limits>

#include "benchmark/gpu_timing.h"

namespace {

bool Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

bool TestConsistentTimestampIsUsed() {
  const auto result =
      benchmark::detail::SelectGpuElapsedSeconds(0.008, 0.0078, true);
  return Expect(result.used_gpu_timestamp,
                "consistent GPU timestamp was rejected") &&
         Expect(std::abs(result.seconds - 0.0078) < 1.0e-12,
                "consistent GPU timestamp duration changed");
}

bool TestUnderreportedTimestampFallsBack() {
  const auto result =
      benchmark::detail::SelectGpuElapsedSeconds(0.008, 0.0032, true);
  return Expect(!result.used_gpu_timestamp,
                "underreported GPU timestamp was accepted") &&
         Expect(!result.disable_gpu_timestamps,
                "cold-start mismatch disabled later device timing") &&
         Expect(std::abs(result.seconds - 0.008) < 1.0e-12,
                "underreported timestamp did not use fence duration");
}

bool TestOverreportedTimestampFallsBack() {
  const auto result =
      benchmark::detail::SelectGpuElapsedSeconds(0.008, 0.010, true);
  return Expect(!result.used_gpu_timestamp,
                "overreported GPU timestamp was accepted") &&
         Expect(!result.disable_gpu_timestamps,
                "one timing mismatch disabled later device timing");
}

bool TestShortBatchUsesFenceDuration() {
  const auto result =
      benchmark::detail::SelectGpuElapsedSeconds(0.001, 0.00095, true);
  return Expect(!result.used_gpu_timestamp,
                "sub-millisecond calibration batch used a GPU timestamp") &&
         Expect(!result.disable_gpu_timestamps,
                "short calibration batch disabled later GPU timestamps");
}

bool TestInvalidTimestampDisablesDeviceTiming() {
  const auto result =
      benchmark::detail::SelectGpuElapsedSeconds(0.008, 0.0, true);
  return Expect(!result.used_gpu_timestamp && result.disable_gpu_timestamps,
                "invalid timestamp did not disable device timing");
}

bool TestInvalidHostDurationIsRejected() {
  const auto result = benchmark::detail::SelectGpuElapsedSeconds(
      std::numeric_limits<double>::quiet_NaN(), 0.008, true);
  return Expect(result.seconds == 0.0,
                "invalid independent timing reference was accepted");
}

} // namespace

int main() {
  if (!TestConsistentTimestampIsUsed() ||
      !TestUnderreportedTimestampFallsBack() ||
      !TestOverreportedTimestampFallsBack() ||
      !TestShortBatchUsesFenceDuration() ||
      !TestInvalidTimestampDisablesDeviceTiming() ||
      !TestInvalidHostDurationIsRejected()) {
    return 1;
  }
  std::cout << "GPU timing tests passed\n";
  return 0;
}
