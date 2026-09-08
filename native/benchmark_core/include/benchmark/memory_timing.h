#ifndef RAPIDBENCH_MEMORY_TIMING_H_
#define RAPIDBENCH_MEMORY_TIMING_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace benchmark::detail {

struct alignas(64) MemoryWorkerCompletion {
  std::uint64_t bytes = 0;
  std::uint64_t completed_at_ns = 0;
};

struct MemoryMeasurement {
  std::uint64_t bytes = 0;
  std::uint64_t elapsed_ns = 0;
  double gbps = 0;
};

// Called only after all workers join. Count full passes and include their full
// completion interval, including a last pass crossing the requested deadline.
// Coordinator polling/join delays and workers that did no work are excluded.
inline MemoryMeasurement SummarizeMemoryMeasurement(
    std::uint64_t started_at_ns, const MemoryWorkerCompletion *workers,
    std::size_t count) {
  MemoryMeasurement result;
  std::uint64_t finished_at_ns = started_at_ns;
  for (std::size_t index = 0; index < count; ++index) {
    if (workers[index].bytes == 0) continue;
    result.bytes += workers[index].bytes;
    finished_at_ns = std::max(finished_at_ns, workers[index].completed_at_ns);
  }
  result.elapsed_ns = finished_at_ns - started_at_ns;
  if (result.elapsed_ns > 0) {
    result.gbps = static_cast<double>(result.bytes) / result.elapsed_ns;
  }
  return result;
}

} // namespace benchmark::detail
#endif
