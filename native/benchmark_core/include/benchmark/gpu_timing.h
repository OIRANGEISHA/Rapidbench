#pragma once

namespace benchmark::detail {

struct GpuElapsedSelection {
  double seconds = 0.0;
  bool used_gpu_timestamp = false;
  bool disable_gpu_timestamps = false;
};

// A fence wait is the independent wall-clock reference. Timestamp queries are
// retained only for batches large enough to amortize submission overhead and
// when the device clock agrees with that reference within a conservative band.
GpuElapsedSelection SelectGpuElapsedSeconds(double host_seconds,
                                            double gpu_seconds,
                                            bool timestamp_valid);

} // namespace benchmark::detail
