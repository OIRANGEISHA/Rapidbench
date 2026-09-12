#ifndef RAPIDBENCH_GPU_VALIDATION_H_
#define RAPIDBENCH_GPU_VALIDATION_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "benchmark/gpu_benchmark.h"

namespace benchmark::detail {

constexpr std::uint32_t kGpuComputeInvocations = 1024U * 64U;
constexpr std::uint32_t kGpuOutputRegions = 16U;
constexpr std::uint64_t kGpuRegionBytes = kGpuComputeInvocations * 16ULL;
constexpr std::size_t kGpuValidationSamples = 8U;

std::uint32_t GpuOutputRegion(std::uint32_t ring, std::uint32_t repetition);
std::uint32_t GpuWrittenRegions(std::uint32_t ring, std::uint32_t repetitions);
std::uint32_t GpuInputWord(std::uint64_t word_index);
std::uint64_t GpuOperationsPerInvocation(GpuTest test,
                                         std::uint32_t fp_accumulators);
std::array<std::uint32_t, kGpuValidationSamples>
GpuSampleIndices(std::uint32_t invocations);

struct GpuValidationRequest {
  GpuTest test = GpuTest::kNone;
  std::uint32_t fp_accumulators = 8U;
  bool native_fp16 = false;
  std::uint32_t written_regions = 0U;
  std::uint64_t memory_input_bytes = 0U;
  std::uint32_t memory_element_offset = 0U;
};

struct GpuValidationResult {
  bool valid = false;
  std::size_t checked_values = 0U;
  std::string error;
};

// Mixed feeds floating-point bits back into integer arithmetic: permitted
// floating-point rounding differences can avalanche. Check finite mathematical
// bounds plus same-device repeatability, not a misleading cross-driver hash.
using GpuMixedReference =
    std::array<std::array<std::uint32_t, 4>, kGpuValidationSamples>;

// Independent scalar oracle. Mixed uses the explicitly separate policy above.
std::array<std::uint32_t, 4>
ReferenceGpuSample(const GpuValidationRequest &request,
                   std::uint32_t invocation, bool truncate_half = false);
GpuValidationResult
ValidateGpuOutput(const void *data, std::uint64_t bytes,
                  const GpuValidationRequest &request,
                  const GpuMixedReference *mixed_reference = nullptr);
GpuValidationResult CaptureGpuMixedReference(const void *data,
                                             std::uint64_t bytes,
                                             GpuMixedReference *reference);

} // namespace benchmark::detail
#endif
