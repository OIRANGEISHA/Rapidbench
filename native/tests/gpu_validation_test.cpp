#include "benchmark/gpu_validation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using namespace benchmark;
using namespace benchmark::detail;

bool Expect(bool condition, const char *message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

void FillSamples(std::vector<std::uint32_t> *output,
                 const GpuValidationRequest &request, bool truncate = false) {
  const bool memory = request.test == GpuTest::kMemoryBandwidth;
  const auto indices = GpuSampleIndices(
      memory ? static_cast<std::uint32_t>(request.memory_input_bytes / 256U)
             : kGpuComputeInvocations);
  for (std::uint32_t region = 0; region < (memory ? 1U : kGpuOutputRegions);
       ++region) {
    if (!memory && (request.written_regions & (1U << region)) == 0)
      continue;
    for (const auto invocation : indices) {
      const auto expected = ReferenceGpuSample(request, invocation, truncate);
      for (std::uint32_t lane = 0; lane < (memory ? 1U : 4U); ++lane) {
        (*output)[region * kGpuRegionBytes / 4U +
                  invocation * (memory ? 1U : 4U) + lane] = expected[lane];
      }
    }
  }
}

bool TestRingAndCounts() {
  GpuValidationRequest integer_request;
  integer_request.test = GpuTest::kInt32;
  const std::array<std::uint32_t, 4> golden = {0xD6F1A897U, 0x6D510A1DU,
                                               0x273E83BCU, 0x9197FF6DU};
  if (!Expect(ReferenceGpuSample(integer_request, 0U) == golden,
              "independently calculated INT32 golden vector changed"))
    return false;
  for (std::uint32_t ring = 0; ring < 32; ++ring) {
    for (const std::uint32_t repetitions : {1U, 2U, 16U, 17U, 32U, 256U}) {
      std::uint32_t mask = 0U;
      for (std::uint32_t index = 0; index < repetitions; ++index) {
        const auto region = GpuOutputRegion(ring, index);
        if (!Expect(region < 16U, "ring out of bounds"))
          return false;
        if (index % 16U == 0U)
          mask = 0U;
        if (!Expect((mask & (1U << region)) == 0U,
                    "region reused before barrier"))
          return false;
        mask |= 1U << region;
      }
      if (repetitions >= 16U &&
          !Expect(GpuWrittenRegions(ring, repetitions) == 0xFFFFU,
                  "ring coverage incomplete"))
        return false;
    }
  }
  return Expect(GpuWrittenRegions(0U, 0U) == 0U, "empty batch has writes") &&
         Expect(GpuWrittenRegions(1U, 2U) == 0x180U, "ring offset incorrect") &&
         Expect(GpuOperationsPerInvocation(GpuTest::kFp32, 8) == 4096,
                "FP32-8 count") &&
         Expect(GpuOperationsPerInvocation(GpuTest::kFp32, 12) == 6144,
                "FP32-12 count") &&
         Expect(GpuOperationsPerInvocation(GpuTest::kFp16, 16) == 8192,
                "FP16-16 count") &&
         Expect(GpuOperationsPerInvocation(GpuTest::kFp16, 9) == 0,
                "invalid FP count") &&
         Expect(GpuOperationsPerInvocation(GpuTest::kInt32, 8) == 5632,
                "INT32 count") &&
         Expect(GpuOperationsPerInvocation(GpuTest::kMixed, 8) == 256,
                "Mixed count");
}

bool TestReferenceAndCorruption() {
  std::vector<std::uint32_t> output(kGpuRegionBytes * kGpuOutputRegions / 4U);
  for (const auto test : {GpuTest::kFp32, GpuTest::kFp16, GpuTest::kInt32}) {
    for (const bool native : {false, true}) {
      for (const std::uint32_t accumulators : {8U, 12U, 16U}) {
        GpuValidationRequest request;
        request.test = test;
        request.native_fp16 = native;
        request.fp_accumulators = accumulators;
        request.written_regions = 0xFFFFU;
        FillSamples(&output, request);
        const auto checked =
            ValidateGpuOutput(output.data(), output.size() * 4ULL, request);
        if (!Expect(checked.valid && checked.checked_values == 16U * 8U * 4U,
                    "full reference failed or did not check every region"))
          return false;
        const auto last = output.size() - 1U;
        for (const std::uint32_t corruption :
             {0U, 0x7F800000U, 0x7FC00000U, 0x3F800000U}) {
          const auto saved = output[last];
          output[last] = corruption;
          const bool rejected =
              !ValidateGpuOutput(output.data(), output.size() * 4ULL, request)
                   .valid;
          output[last] = saved;
          if (!Expect(rejected, "corrupt tail region passed validation"))
            return false;
        }
        if (native && test == GpuTest::kFp16) {
          FillSamples(&output, request, true);
          if (!Expect(ValidateGpuOutput(output.data(), output.size() * 4ULL,
                                        request)
                          .valid,
                      "permitted half truncation reference rejected"))
            return false;
        }
      }
    }
  }
  GpuValidationRequest request;
  request.test = GpuTest::kFp32;
  request.written_regions = 1U;
  std::fill(output.begin(), output.end(), 0U);
  FillSamples(&output, request);
  request.written_regions = 0xFFFFU;
  return Expect(!ValidateGpuOutput(output.data(), output.size() * 4ULL, request)
                     .valid,
                "legacy shader writing region zero only was accepted") &&
         Expect(!ValidateGpuOutput(output.data(), 64U, request).valid,
                "short output accepted") &&
         Expect(
             !ValidateGpuOutput(nullptr, output.size() * 4ULL, request).valid,
             "null output accepted");
}

bool TestMemoryOracle() {
  GpuValidationRequest request;
  request.test = GpuTest::kMemoryBandwidth;
  request.memory_input_bytes = 1024U * 1024U;
  std::vector<std::uint32_t> output(request.memory_input_bytes / 256U);
  for (std::uint32_t quarter = 0; quarter < 4U; ++quarter) {
    request.memory_element_offset =
        quarter * (request.memory_input_bytes / 16U / 4U);
    FillSamples(&output, request);
    const auto checked =
        ValidateGpuOutput(output.data(), output.size() * 4ULL, request);
    if (!Expect(checked.valid && checked.checked_values == 8U,
                "memory ring oracle failed"))
      return false;
    output.back() = 0U;
    if (!Expect(!ValidateGpuOutput(output.data(), output.size() * 4ULL, request)
                     .valid,
                "missing memory tail accepted"))
      return false;
  }
  request.memory_element_offset = 1U;
  return Expect(
      !ValidateGpuOutput(output.data(), output.size() * 4ULL, request).valid,
      "unaligned memory offset accepted");
}

bool TestMixedPolicy() {
  GpuValidationRequest request;
  request.test = GpuTest::kMixed;
  request.written_regions = 0xFFFFU;
  std::vector<std::uint32_t> output(kGpuRegionBytes * kGpuOutputRegions / 4U);
  GpuMixedReference reference{};
  if (!Expect(!CaptureGpuMixedReference(output.data(), output.size() * 4ULL,
                                        &reference)
                   .valid,
              "zero Mixed reference accepted"))
    return false;
  // An independent bounded trajectory uses the mid-point of each legal
  // integer-derived perturbation. It must pass bounds and same-run comparison.
  const auto indices = GpuSampleIndices(kGpuComputeInvocations);
  for (const auto invocation : indices) {
    for (std::uint32_t lane = 0U; lane < 4U; ++lane) {
      const auto seed_bits = GpuInputWord(invocation * 4ULL + lane);
      float seed;
      std::memcpy(&seed, &seed_bits, 4U);
      float fp0 = seed + .125F * static_cast<float>(lane + 1U);
      float fp1 = seed + .625F + .125F * static_cast<float>(lane);
      for (std::uint32_t iteration = 0; iteration < 64U; ++iteration) {
        fp0 = std::fma(fp0, .99991F, fp1);
        fp1 = fp1 * .99983F + .75F * .00001F;
      }
      const float value = fp0 + fp1 + .75F;
      std::uint32_t bits;
      std::memcpy(&bits, &value, 4U);
      for (std::uint32_t region = 0; region < 16U; ++region) {
        output[region * kGpuRegionBytes / 4U + invocation * 4ULL + lane] = bits;
      }
    }
  }
  if (!Expect(CaptureGpuMixedReference(output.data(), output.size() * 4ULL,
                                       &reference)
                      .valid &&
                  ValidateGpuOutput(output.data(), output.size() * 4ULL,
                                    request, &reference)
                      .valid,
              "bounded repeatable Mixed sample rejected"))
    return false;
  auto &word = output[kGpuRegionBytes / 4U];
  float changed;
  std::memcpy(&changed, &word, 4U);
  changed += .05F;
  std::memcpy(&word, &changed, 4U);
  return Expect(ValidateGpuOutput(output.data(), output.size() * 4ULL, request)
                    .valid,
                "Mixed corruption fixture unexpectedly escaped bounds") &&
         Expect(!ValidateGpuOutput(output.data(), output.size() * 4ULL, request,
                                   &reference)
                     .valid,
                "different Mixed result in a later region accepted");
}
} // namespace

int main() {
  if (!TestRingAndCounts() || !TestReferenceAndCorruption() ||
      !TestMemoryOracle() || !TestMixedPolicy())
    return 1;
  std::cout
      << "GPU validation oracle, corruption, ring and counting tests passed\n";
  return 0;
}
