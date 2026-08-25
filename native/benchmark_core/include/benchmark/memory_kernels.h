#ifndef RAPIDBENCH_MEMORY_KERNELS_H_
#define RAPIDBENCH_MEMORY_KERNELS_H_

#include <cstddef>
#include <cstdint>

namespace benchmark {

std::uint64_t MemoryReadKernel(const std::uint8_t *source,
                               std::size_t byte_count);

void MemoryWriteKernel(std::uint8_t *destination, std::size_t byte_count,
                       std::uint64_t pattern);

void MemoryCopyKernel(std::uint8_t *destination, const std::uint8_t *source,
                      std::size_t byte_count);

} // namespace benchmark

#endif // RAPIDBENCH_MEMORY_KERNELS_H_
