#include "benchmark/gpu_validation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>

namespace benchmark::detail {
namespace {

float AsFloat(std::uint32_t bits) {
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::uint32_t AsWord(float value) {
  std::uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float Half(float value, bool truncate) {
  if (!std::isfinite(value))
    return value;
  const float magnitude = std::abs(value);
  if (magnitude >= 65520.0F && !truncate) {
    return std::copysign(std::numeric_limits<float>::infinity(), value);
  }
  if (magnitude > 65504.0F && truncate)
    return std::copysign(65504.0F, value);
  if (magnitude < 0.00006103515625F) {
    const double scaled = static_cast<double>(magnitude) * 16777216.0;
    double rounded = std::floor(scaled);
    const double tail = scaled - rounded;
    if (!truncate &&
        (tail > 0.5 || (tail == 0.5 && std::fmod(rounded, 2.0) != 0.0))) {
      rounded += 1.0;
    }
    return std::copysign(static_cast<float>(rounded / 16777216.0), value);
  }
  std::uint32_t bits = AsWord(value);
  if (!truncate)
    bits += 0xFFFU + ((bits >> 13U) & 1U);
  return AsFloat(bits & ~0x1FFFU);
}

float ReferenceFp(const GpuValidationRequest &request, std::uint32_t invocation,
                  std::uint32_t lane, bool truncate_half) {
  constexpr std::array<float, 16> offsets = {
      .101F, .113F, .139F, .163F, .181F, .199F, .229F, .251F,
      .277F, .293F, .317F, .347F, .373F, .397F, .419F, .443F};
  constexpr float fp32_eight_offsets[8][4] = {
      {.101F, .103F, .107F, .109F}, {.113F, .127F, .131F, .137F},
      {.139F, .149F, .151F, .157F}, {.163F, .167F, .173F, .179F},
      {.181F, .191F, .193F, .197F}, {.199F, .211F, .223F, .227F},
      {.229F, .233F, .239F, .241F}, {.251F, .257F, .263F, .269F}};
  const bool half = request.test == GpuTest::kFp16 && request.native_fp16;
  const auto round = [&](float value) {
    return half ? Half(value, truncate_half) : value;
  };
  const auto add = [&](float left, float right) { return round(left + right); };
  const auto fma = [&](float a, float b, float c) {
    return round(std::fma(a, b, c));
  };
  const float seed = round(AsFloat(GpuInputWord(invocation * 4ULL + lane)));
  std::array<float, 16> a{};
  for (std::uint32_t index = 0; index < request.fp_accumulators; ++index) {
    const float offset =
        request.test == GpuTest::kFp32 && request.fp_accumulators == 8U
            ? fp32_eight_offsets[index][lane]
            : offsets[index];
    a[index] = add(seed, round(offset));
  }
  for (std::uint32_t iteration = 0; iteration < 64U; ++iteration) {
    if (request.fp_accumulators == 8U) {
      constexpr float multiplier = 0.0009765625F;
      a[0] = fma(a[0], multiplier, a[4]);
      a[1] = fma(a[1], multiplier, a[5]);
      a[2] = fma(a[2], multiplier, a[6]);
      a[3] = fma(a[3], multiplier, a[7]);
      a[4] = fma(a[4], multiplier, a[1]);
      a[5] = fma(a[5], multiplier, a[2]);
      a[6] = fma(a[6], multiplier, a[3]);
      a[7] = fma(a[7], multiplier, a[0]);
    } else {
      for (std::uint32_t index = 0; index < request.fp_accumulators; ++index) {
        a[index] = fma(a[index], 0.9990234375F,
                       static_cast<float>(index + 1U) * 0.0009765625F);
      }
    }
  }
  float sum = add(a[0], a[1]);
  if (request.fp_accumulators == 8U) {
    for (std::uint32_t index = 2; index < 8U; index += 2U) {
      sum = add(sum, add(a[index], a[index + 1U]));
    }
  } else {
    sum = add(sum, add(a[2], a[3]));
    for (std::uint32_t index = 4; index < request.fp_accumulators;
         index += 4U) {
      const float group =
          add(add(a[index], a[index + 1U]), add(a[index + 2U], a[index + 3U]));
      sum = add(sum, group);
    }
  }
  return sum;
}

std::uint32_t ReferenceInt(std::uint32_t invocation, std::uint32_t lane) {
  const std::uint32_t seed = GpuInputWord(invocation * 4ULL + lane);
  std::uint32_t a0 = seed + 0x9E3779B9U, a1 = seed ^ 0x85EBCA6BU;
  std::uint32_t a2 = seed + 0xC2B2AE35U, a3 = seed ^ 0x27D4EB2FU;
  std::uint32_t a4 = seed + 0x165667B1U, a5 = seed ^ 0xD3A2646CU;
  std::uint32_t a6 = seed + 0xFD7046C5U, a7 = seed ^ 0xB55A4F09U;
  for (std::uint32_t iteration = 0; iteration < 64U; ++iteration) {
    a0 = a0 * 1664525U + a4;
    a1 = (a1 ^ a5) + (a0 >> 5U);
    a2 = (a2 & a6) | (a1 << 7U);
    a3 = (a3 + a7) ^ (a2 >> 3U);
    a4 = (a4 * 22695477U) ^ a0;
    a5 = (a5 + a1) | (a4 >> 11U);
    a6 = (a6 ^ a2) + (a5 << 9U);
    a7 = (a7 + a3) ^ (a6 >> 13U);
  }
  return (a0 ^ a1) + (a2 ^ a3) + (a4 ^ a5) + (a6 ^ a7);
}

float ReferenceMemory(const GpuValidationRequest &request,
                      std::uint32_t invocation) {
  const std::uint64_t elements = request.memory_input_bytes / 16U;
  if (elements == 0U)
    return 0.0F;
  const std::uint64_t base =
      (invocation * 16ULL + request.memory_element_offset) % elements;
  std::array<float, 4> sums{};
  for (std::uint32_t item = 0; item < 16U; ++item) {
    for (std::uint32_t lane = 0; lane < 4U; ++lane) {
      sums[lane] += AsFloat(GpuInputWord((base + item) * 4U + lane));
    }
  }
  return ((sums[0] * .25F + sums[1] * .25F) + sums[2] * .25F) + sums[3] * .25F;
}

bool InMixedBounds(float actual, std::uint32_t invocation, std::uint32_t lane) {
  const float seed = AsFloat(GpuInputWord(invocation * 4ULL + lane));
  float low0 = seed + .125F * static_cast<float>(lane + 1U), high0 = low0;
  float low1 = seed + .625F + .125F * static_cast<float>(lane), high1 = low1;
  for (std::uint32_t iteration = 0; iteration < 64U; ++iteration) {
    low0 = std::fma(low0, .99991F, low1);
    high0 = std::fma(high0, .99991F, high1);
    low1 = low1 * .99983F + .5F * .00001F;
    high1 = high1 * .99983F + 1.F * .00001F;
  }
  const float tolerance = (high0 + high1) * .0001F;
  return std::isfinite(actual) && actual >= low0 + low1 + .5F - tolerance &&
         actual <= high0 + high1 + 1.F + tolerance;
}

std::uint32_t ReadWord(const void *data, std::uint64_t word) {
  std::uint32_t result;
  std::memcpy(&result, static_cast<const std::uint8_t *>(data) + word * 4U, 4U);
  return result;
}

} // namespace

std::uint32_t GpuOutputRegion(std::uint32_t ring, std::uint32_t repetition) {
  return ((ring * 7U) + repetition) % kGpuOutputRegions;
}

std::uint32_t GpuWrittenRegions(std::uint32_t ring, std::uint32_t repetitions) {
  std::uint32_t mask = 0U;
  for (std::uint32_t index = 0;
       index < std::min(repetitions, kGpuOutputRegions); ++index)
    mask |= 1U << GpuOutputRegion(ring, index);
  return mask;
}

std::uint32_t GpuInputWord(std::uint64_t word_index) {
  return 0x3E800000U +
         static_cast<std::uint32_t>((word_index * 2654435761ULL) & 0x000FFFFFU);
}

std::uint64_t GpuOperationsPerInvocation(GpuTest test,
                                         std::uint32_t fp_accumulators) {
  if (test == GpuTest::kFp32 || test == GpuTest::kFp16) {
    return fp_accumulators == 8U || fp_accumulators == 12U ||
                   fp_accumulators == 16U
               ? fp_accumulators * 4ULL * 2ULL * 64ULL
               : 0ULL;
  }
  if (test == GpuTest::kInt32)
    return 5632ULL;
  if (test == GpuTest::kMixed)
    return 256ULL;
  return 0ULL;
}

std::array<std::uint32_t, kGpuValidationSamples>
GpuSampleIndices(std::uint32_t invocations) {
  const std::uint32_t last = invocations == 0 ? 0 : invocations - 1U;
  return {0U,
          std::min(1U, last),
          std::min(63U, last),
          std::min(64U, last),
          invocations / 4U,
          invocations / 2U,
          invocations * 3U / 4U,
          last};
}

std::array<std::uint32_t, 4>
ReferenceGpuSample(const GpuValidationRequest &request,
                   std::uint32_t invocation, bool truncate_half) {
  std::array<std::uint32_t, 4> words{};
  for (std::uint32_t lane = 0; lane < 4U; ++lane) {
    switch (request.test) {
    case GpuTest::kFp32:
    case GpuTest::kFp16:
      if (request.fp_accumulators == 8U || request.fp_accumulators == 12U ||
          request.fp_accumulators == 16U) {
        words[lane] =
            AsWord(ReferenceFp(request, invocation, lane, truncate_half));
      }
      break;
    case GpuTest::kInt32:
      words[lane] = ReferenceInt(invocation, lane);
      break;
    case GpuTest::kMemoryBandwidth:
      words[0] = AsWord(ReferenceMemory(request, invocation));
      return words;
    default:
      return words;
    }
  }
  return words;
}

GpuValidationResult
ValidateGpuOutput(const void *data, std::uint64_t bytes,
                  const GpuValidationRequest &request,
                  const GpuMixedReference *mixed_reference) {
  GpuValidationResult result;
  const bool memory = request.test == GpuTest::kMemoryBandwidth;
  const bool fp =
      request.test == GpuTest::kFp32 || request.test == GpuTest::kFp16;
  const bool half = request.test == GpuTest::kFp16 && request.native_fp16;
  const std::uint64_t required_bytes =
      memory ? request.memory_input_bytes / 64U
             : kGpuRegionBytes * kGpuOutputRegions;
  if (data == nullptr || bytes < required_bytes || required_bytes == 0U ||
      (!fp && !memory && request.test != GpuTest::kInt32 &&
       request.test != GpuTest::kMixed) ||
      (fp && request.fp_accumulators != 8U && request.fp_accumulators != 12U &&
       request.fp_accumulators != 16U) ||
      (!memory && (request.written_regions == 0U ||
                   (request.written_regions & ~0xFFFFU) != 0U)) ||
      (memory &&
       (request.memory_input_bytes % (256U * 64U) != 0U ||
        request.memory_input_bytes / 16U > UINT32_MAX ||
        request.memory_element_offset >= request.memory_input_bytes / 16U ||
        request.memory_element_offset % 16U != 0U))) {
    result.error = "Invalid GPU validation buffer or workload description";
    return result;
  }
  const std::uint32_t invocations =
      memory ? static_cast<std::uint32_t>(request.memory_input_bytes / 256U)
             : kGpuComputeInvocations;
  const auto samples = GpuSampleIndices(invocations);
  const std::uint32_t lanes = memory ? 1U : 4U;
  const std::uint32_t regions = memory ? 1U : kGpuOutputRegions;
  for (std::uint32_t region = 0; region < regions; ++region) {
    if (!memory && (request.written_regions & (1U << region)) == 0U)
      continue;
    for (std::size_t sample = 0; sample < samples.size(); ++sample) {
      const auto expected = ReferenceGpuSample(request, samples[sample]);
      const auto truncated =
          half ? ReferenceGpuSample(request, samples[sample], true) : expected;
      for (std::uint32_t lane = 0; lane < lanes; ++lane) {
        const std::uint64_t word =
            region * kGpuRegionBytes / 4U + samples[sample] * lanes + lane;
        const std::uint32_t actual_word = ReadWord(data, word);
        const float actual = AsFloat(actual_word);
        bool valid = false;
        if (request.test == GpuTest::kInt32) {
          valid = actual_word == expected[lane];
        } else if (request.test == GpuTest::kMixed) {
          valid = InMixedBounds(actual, samples[sample], lane);
          if (valid && mixed_reference != nullptr) {
            const float reference = AsFloat((*mixed_reference)[sample][lane]);
            valid =
                std::isfinite(reference) &&
                std::abs(actual - reference) <= std::abs(reference) * .00001F;
          }
        } else {
          const float low =
              std::min(AsFloat(expected[lane]), AsFloat(truncated[lane]));
          const float high =
              std::max(AsFloat(expected[lane]), AsFloat(truncated[lane]));
          // Native half allows the RTE/RTZ reference envelope and a small
          // reduction tolerance. This is validation, never a score correction.
          const float tolerance = half ? std::max(.0001F, high * .003F)
                                       : std::max(.00002F, high * .00003F);
          valid = std::isfinite(actual) && actual >= low - tolerance &&
                  actual <= high + tolerance;
        }
        ++result.checked_values;
        if (!valid) {
          std::ostringstream message;
          message << "GPU output mismatch: test="
                  << static_cast<std::uint32_t>(request.test)
                  << " accumulators=" << request.fp_accumulators
                  << " region=" << region << " invocation=" << samples[sample]
                  << " lane=" << lane << " bits=0x" << std::hex << actual_word;
          result.error = message.str();
          return result;
        }
      }
    }
  }
  result.valid = result.checked_values > 0U;
  return result;
}

GpuValidationResult CaptureGpuMixedReference(const void *data,
                                             std::uint64_t bytes,
                                             GpuMixedReference *reference) {
  GpuValidationRequest request;
  request.test = GpuTest::kMixed;
  request.written_regions = 1U;
  auto result = ValidateGpuOutput(data, bytes, request);
  if (!result.valid || reference == nullptr) {
    result.valid = false;
    if (result.error.empty())
      result.error = "Missing Mixed reference destination";
    return result;
  }
  const auto indices = GpuSampleIndices(kGpuComputeInvocations);
  for (std::size_t sample = 0; sample < indices.size(); ++sample) {
    for (std::uint32_t lane = 0; lane < 4U; ++lane) {
      (*reference)[sample][lane] =
          ReadWord(data, indices[sample] * 4ULL + lane);
    }
  }
  return result;
}

} // namespace benchmark::detail
