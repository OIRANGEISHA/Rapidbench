#include "benchmark/memory_timing.h"
#include <cmath>
#include <cstdio>

int main() {
  using benchmark::detail::MemoryWorkerCompletion;
  using benchmark::detail::SummarizeMemoryMeasurement;
  // A 3-second test with a final counted pass ending at 3.25 seconds must use
  // 3.25 seconds, rather than report the 3.0-second deadline's inflated result.
  const MemoryWorkerCompletion overrun[] = {
      {32500000000ULL, 4250000000ULL}, {32500000000ULL, 4200000000ULL}};
  const auto full = SummarizeMemoryMeasurement(1000000000ULL, overrun, 2);
  if (full.bytes != 65000000000ULL || full.elapsed_ns != 3250000000ULL ||
      std::abs(full.gbps - 20.0) > 1e-9) return 1;
  // Early cancellation must use the last completed pass, including its tail.
  const MemoryWorkerCompletion stopped[] = {
      {1000000000ULL, 1100000000ULL}, {2000000000ULL, 1120000000ULL},
      {0, 9000000000ULL}};
  const auto early = SummarizeMemoryMeasurement(1000000000ULL, stopped, 3);
  if (early.elapsed_ns != 120000000ULL || std::abs(early.gbps - 25.0) > 1e-9)
    return 2;
  const MemoryWorkerCompletion empty[] = {{0, 0}, {0, 0}};
  const auto none = SummarizeMemoryMeasurement(1000000000ULL, empty, 2);
  if (none.bytes != 0 || none.elapsed_ns != 0 || none.gbps != 0) return 3;
  std::puts("Memory timing tests passed: overrun, cancellation, no work");
  return 0;
}
