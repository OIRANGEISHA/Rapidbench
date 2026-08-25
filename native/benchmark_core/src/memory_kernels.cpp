#include "benchmark/memory_kernels.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace benchmark {

namespace {

constexpr std::size_t kUnrollBytes = 128U;
thread_local volatile std::uint64_t g_memory_read_sink = 0U;

#if defined(__aarch64__)
inline uint8x16_t StreamLoad(const std::uint8_t *address) {
  return vld1q_u8(address);
}

inline void StreamStoreNonTemporal128(std::uint8_t *address, uint8x16_t value) {
  asm volatile("stnp %q[value], %q[value], [%[address], #0]\n\t"
               "stnp %q[value], %q[value], [%[address], #32]\n\t"
               "stnp %q[value], %q[value], [%[address], #64]\n\t"
               "stnp %q[value], %q[value], [%[address], #96]\n\t"
               :
               : [address] "r"(address), [value] "w"(value)
               : "memory");
}
#endif

} // namespace

std::uint64_t MemoryReadKernel(const std::uint8_t *source,
                               std::size_t byte_count) {
  if (source == nullptr || byte_count == 0U) {
    return 0U;
  }
#if defined(__aarch64__)
  uint8x16_t accumulator0 = vdupq_n_u8(0U);
  uint8x16_t accumulator1 = vdupq_n_u8(0U);
  uint8x16_t accumulator2 = vdupq_n_u8(0U);
  uint8x16_t accumulator3 = vdupq_n_u8(0U);
  std::size_t offset = 0U;
  for (; offset + kUnrollBytes <= byte_count; offset += kUnrollBytes) {
    const uint8x16_t value0 = StreamLoad(source + offset);
    const uint8x16_t value1 = StreamLoad(source + offset + 16U);
    const uint8x16_t value2 = StreamLoad(source + offset + 32U);
    const uint8x16_t value3 = StreamLoad(source + offset + 48U);
    const uint8x16_t value4 = StreamLoad(source + offset + 64U);
    const uint8x16_t value5 = StreamLoad(source + offset + 80U);
    const uint8x16_t value6 = StreamLoad(source + offset + 96U);
    const uint8x16_t value7 = StreamLoad(source + offset + 112U);
    accumulator0 = veorq_u8(accumulator0, veorq_u8(value0, value4));
    accumulator1 = veorq_u8(accumulator1, veorq_u8(value1, value5));
    accumulator2 = veorq_u8(accumulator2, veorq_u8(value2, value6));
    accumulator3 = veorq_u8(accumulator3, veorq_u8(value3, value7));
  }
  const uint8x16_t combined = veorq_u8(veorq_u8(accumulator0, accumulator1),
                                       veorq_u8(accumulator2, accumulator3));
  std::uint64_t checksum = vaddlvq_u8(combined);
  for (; offset < byte_count; ++offset) {
    checksum ^= source[offset];
  }
#else
  std::uint64_t checksum = 0U;
  for (std::size_t offset = 0U; offset < byte_count; offset += 64U) {
    checksum ^= source[offset];
  }
#endif
  g_memory_read_sink ^= checksum;
  return checksum;
}

void MemoryWriteKernel(std::uint8_t *destination, std::size_t byte_count,
                       std::uint64_t pattern) {
  if (destination == nullptr || byte_count == 0U) {
    return;
  }
#if defined(__aarch64__)
  const uint64x2_t value = vdupq_n_u64(pattern);
  std::size_t offset = 0U;
  for (; offset + kUnrollBytes <= byte_count; offset += kUnrollBytes) {
    const uint8x16_t bytes = vreinterpretq_u8_u64(value);
    StreamStoreNonTemporal128(destination + offset, bytes);
  }
  for (; offset < byte_count; ++offset) {
    destination[offset] = static_cast<std::uint8_t>(pattern);
  }
#else
  auto *words = reinterpret_cast<std::uint64_t *>(destination);
  const std::size_t word_count = byte_count / sizeof(std::uint64_t);
  for (std::size_t index = 0U; index < word_count; ++index) {
    words[index] = pattern;
  }
#endif
}

void MemoryCopyKernel(std::uint8_t *destination, const std::uint8_t *source,
                      std::size_t byte_count) {
  if (destination == nullptr || source == nullptr || byte_count == 0U) {
    return;
  }
  std::memcpy(destination, source, byte_count);
}

} // namespace benchmark


