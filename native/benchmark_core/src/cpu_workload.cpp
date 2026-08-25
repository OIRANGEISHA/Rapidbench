#include "benchmark/cpu_workload.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace benchmark {
namespace {

constexpr std::uint32_t kRoundsPerBatch = 8;

std::uint32_t RotateLeft32(std::uint32_t value, unsigned int count) {
  return (value << count) | (value >> (32U - count));
}

std::uint64_t RotateLeft64(std::uint64_t value, unsigned int count) {
  return (value << count) | (value >> (64U - count));
}

std::uint64_t SplitMix64(std::uint64_t *state) {
  std::uint64_t value = (*state += 0x9E3779B97F4A7C15ULL);
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

} // namespace

void InitializeCpuWorkload(CpuWorkloadState *state, std::uint64_t seed) {
  if (state == nullptr) {
    return;
  }

  std::uint64_t generator = seed;
  for (std::size_t index = 0; index < state->integers.size(); ++index) {
    const std::uint64_t value = SplitMix64(&generator);
    state->integers[index] = static_cast<std::uint32_t>(value ^ (value >> 32U));
    state->floating_point[index] =
        0.25F + static_cast<float>((value >> 16U) & 0xFFFFU) / 65536.0F;
  }
  state->checksum = SplitMix64(&generator);
}

void RunCpuWorkloadBatch(CpuWorkloadState *state) {
  for (std::uint32_t round = 0; round < kRoundsPerBatch; ++round) {
    const std::uint32_t round_salt = 0x9E3779B9U + round * 0x7F4A7C15U;

#if defined(__clang__)
#pragma clang loop vectorize(enable) interleave(enable)
#endif
    for (std::size_t index = 0; index < state->integers.size(); ++index) {
      std::uint32_t value = state->integers[index] + round_salt +
                            static_cast<std::uint32_t>(index) * 0x85EBCA6BU;
      value ^= value >> 16U;
      value *= 0x85EBCA6BU;
      value ^= value >> 13U;
      value *= 0xC2B2AE35U;
      value ^= value >> 16U;
      value = RotateLeft32(value, 7U) ^ RotateLeft32(value + round_salt, 17U);
      state->integers[index] = value;
    }

#if defined(__clang__)
#pragma clang loop vectorize(enable) interleave(enable)
#endif
    for (std::size_t index = 0; index < state->floating_point.size(); ++index) {
      const std::uint32_t integer = state->integers[index];
      const float low =
          static_cast<float>(integer & 0xFFFFU) * 0.0000152587890625F;
      const float high =
          static_cast<float>(integer >> 16U) * 0.0000152587890625F;
      float value = state->floating_point[index];
      value = value * 0.999755859375F + low * 0.00048828125F;
      value = value * 1.0001220703125F + high * 0.000244140625F;
      state->floating_point[index] = value;
    }
  }

  std::uint64_t checksum = state->checksum;
#if defined(__clang__)
#pragma clang loop vectorize(disable) interleave(disable) unroll(enable)
#endif
  for (std::size_t index = 0; index < state->integers.size(); index += 4) {
    const std::uint64_t first =
        static_cast<std::uint64_t>(state->integers[index]) |
        (static_cast<std::uint64_t>(state->integers[index + 1]) << 32U);
    const std::uint64_t second =
        static_cast<std::uint64_t>(state->integers[index + 2]) |
        (static_cast<std::uint64_t>(state->integers[index + 3]) << 32U);
    checksum ^= first + RotateLeft64(second, 23U);
    checksum = RotateLeft64(checksum, 11U) * 0xD6E8FEB86659FD93ULL;
  }

  std::uint32_t floating_bits = 0;
  const std::size_t floating_index =
      static_cast<std::size_t>(checksum) & (state->floating_point.size() - 1U);
  std::memcpy(&floating_bits, &state->floating_point[floating_index],
              sizeof(floating_bits));
  state->checksum =
      checksum ^ (static_cast<std::uint64_t>(floating_bits) << 17U);
}

} // namespace benchmark
