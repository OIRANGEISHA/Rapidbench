#include "benchmark_ffi.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "benchmark/device_cpu_isa.h"
#include "benchmark/device_vulkan.h"

namespace {

int32_t CopyJson(const std::string &json, char *out_json, uint32_t capacity,
                 uint32_t *out_required) {
  if (out_required == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  const std::uint32_t required = static_cast<std::uint32_t>(json.size() + 1U);
  *out_required = required;
  if (out_json == nullptr && capacity == 0U) {
    return BM_STATUS_OK;
  }
  if (out_json == nullptr || capacity < required) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  std::memcpy(out_json, json.c_str(), required);
  return BM_STATUS_OK;
}

} // namespace

extern "C" BM_FFI_EXPORT int32_t bm_get_vulkan_info_json(
    char *out_json, uint32_t capacity, uint32_t *out_required) {
  return CopyJson(benchmark::QueryVulkanInfoJson(), out_json, capacity,
                  out_required);
}

extern "C" BM_FFI_EXPORT int32_t bm_get_cpu_isa_info_json(
    char *out_json, uint32_t capacity, uint32_t *out_required) {
  return CopyJson(benchmark::QueryCpuIsaInfoJson(), out_json, capacity,
                  out_required);
}

