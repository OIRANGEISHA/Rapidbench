#ifndef RAPIDBENCH_MEMORY_FREQUENCY_H_
#define RAPIDBENCH_MEMORY_FREQUENCY_H_

#include <cstdint>

namespace benchmark {

struct MemoryFrequency {
  std::int32_t status = 1;
  std::uint32_t flags = 0U;
  std::uint64_t current_hz = 0U;
  std::uint64_t maximum_hz = 0U;
};

MemoryFrequency ReadMemoryFrequency();

} // namespace benchmark

#endif // RAPIDBENCH_MEMORY_FREQUENCY_H_
