#include "gpu_ffi.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>

#include "benchmark/gpu_benchmark.h"

namespace {

benchmark::GpuBenchmarkEngine *ToGpuEngine(bm_gpu_engine_handle handle) {
  return static_cast<benchmark::GpuBenchmarkEngine *>(handle);
}

bool ValidRequest(const bm_gpu_request_v1 *request) {
  return request != nullptr &&
         request->struct_size == sizeof(bm_gpu_request_v1) &&
         request->abi_version == BM_ABI_VERSION;
}

bool ValidSnapshot(const bm_gpu_snapshot_v1 *snapshot) {
  return snapshot != nullptr &&
         snapshot->struct_size == sizeof(bm_gpu_snapshot_v1) &&
         snapshot->abi_version == BM_ABI_VERSION;
}

int32_t CopyJson(const std::string &json, char *out_json,
                 std::uint32_t capacity, std::uint32_t *out_required) {
  if (out_required == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  const auto required = static_cast<std::uint32_t>(json.size() + 1U);
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

extern "C" {

int32_t bm_gpu_engine_create(bm_gpu_engine_handle *out_engine) {
  if (out_engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  auto *engine = new (std::nothrow) benchmark::GpuBenchmarkEngine();
  if (engine == nullptr) {
    return BM_STATUS_INTERNAL_ERROR;
  }
  *out_engine = engine;
  return BM_STATUS_OK;
}

int32_t bm_gpu_engine_destroy(bm_gpu_engine_handle engine) {
  if (engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  delete ToGpuEngine(engine);
  return BM_STATUS_OK;
}

int32_t bm_gpu_start(bm_gpu_engine_handle engine,
                     const bm_gpu_request_v1 *request,
                     std::uint64_t *out_run_id) {
  if (engine == nullptr || request == nullptr || out_run_id == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  if (request->abi_version != BM_ABI_VERSION) {
    return BM_STATUS_ABI_MISMATCH;
  }
  if (!ValidRequest(request)) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  benchmark::GpuRequest native_request{};
  native_request.test = static_cast<benchmark::GpuTest>(request->test_id);
  native_request.duration_ms = request->duration_ms;
  native_request.warmup_ms = request->warmup_ms;
  return ToGpuEngine(engine)->Start(native_request, out_run_id);
}

int32_t bm_gpu_request_stop(bm_gpu_engine_handle engine,
                            std::uint64_t run_id) {
  if (engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  return ToGpuEngine(engine)->RequestStop(run_id);
}

int32_t bm_gpu_get_snapshot(bm_gpu_engine_handle engine,
                            bm_gpu_snapshot_v1 *out_snapshot) {
  if (engine == nullptr || out_snapshot == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  if (out_snapshot->abi_version != BM_ABI_VERSION) {
    return BM_STATUS_ABI_MISMATCH;
  }
  if (!ValidSnapshot(out_snapshot)) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  const benchmark::GpuSnapshot source = ToGpuEngine(engine)->GetSnapshot();
  bm_gpu_snapshot_v1 result{};
  result.struct_size = sizeof(result);
  result.abi_version = BM_ABI_VERSION;
  result.run_id = source.run_id;
  result.state = static_cast<std::uint32_t>(source.state);
  result.active_test = static_cast<std::uint32_t>(source.active_test);
  result.fp16_mode = static_cast<std::uint32_t>(source.fp16_mode);
  result.timing_mode = static_cast<std::uint32_t>(source.timing_mode);
  result.error_code = source.error_code;
  if (source.overall_score_valid) {
    result.flags |= BM_GPU_FLAG_OVERALL_SCORE_VALID;
  }
  if (source.reduced_working_set) {
    result.flags |= BM_GPU_FLAG_REDUCED_WORKING_SET;
  }
  if (source.vulkan_available) {
    result.flags |= BM_GPU_FLAG_VULKAN_AVAILABLE;
  }
  result.elapsed_ns = source.elapsed_ns;
  result.buffer_bytes = source.buffer_bytes;
  result.fp32_gflops = source.fp32_gflops;
  result.fp16_gflops = source.fp16_gflops;
  result.fp16_scaling = source.fp16_scaling;
  result.int32_gops = source.int32_gops;
  result.mixed_gwork = source.mixed_gwork;
  result.memory_bandwidth_gbps = source.memory_bandwidth_gbps;
  result.overall_score = source.overall_score;
  result.progress = source.progress;
  result.gpu_batch_ms = source.gpu_batch_ms;
  result.workgroup_size = source.workgroup_size;
  result.dispatch_count = source.dispatch_count;
  result.iteration_count = source.iteration_count;
  const std::size_t error_length = std::min<std::size_t>(
      source.last_error.size(), BM_GPU_LAST_ERROR_CAPACITY - 1U);
  std::memcpy(result.last_error, source.last_error.data(), error_length);
  result.last_error[error_length] = '\0';
  *out_snapshot = result;
  return BM_STATUS_OK;
}

int32_t bm_gpu_get_capabilities_json(bm_gpu_engine_handle engine,
                                     char *out_json, std::uint32_t capacity,
                                     std::uint32_t *out_required) {
  if (engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  return CopyJson(ToGpuEngine(engine)->GetCapabilitiesJson(), out_json,
                  capacity, out_required);
}

} // extern "C"

static_assert(sizeof(bm_gpu_request_v1) == 24,
              "bm_gpu_request_v1 ABI layout changed unexpectedly");
static_assert(sizeof(bm_gpu_snapshot_v1) == 336,
              "bm_gpu_snapshot_v1 ABI layout changed unexpectedly");
