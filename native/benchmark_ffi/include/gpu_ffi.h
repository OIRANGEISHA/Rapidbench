#ifndef RAPIDBENCH_GPU_FFI_H_
#define RAPIDBENCH_GPU_FFI_H_

#include "benchmark_ffi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *bm_gpu_engine_handle;

#define BM_GPU_TEST_NONE 0U
#define BM_GPU_TEST_FP32 1U
#define BM_GPU_TEST_FP16 2U
#define BM_GPU_TEST_INT32 3U
#define BM_GPU_TEST_MIXED 4U
#define BM_GPU_TEST_MEMORY_BANDWIDTH 5U
#define BM_GPU_TEST_ALL 6U

#define BM_GPU_STATE_IDLE 0U
#define BM_GPU_STATE_WARMING_UP 1U
#define BM_GPU_STATE_RUNNING 2U
#define BM_GPU_STATE_STOPPING 3U
#define BM_GPU_STATE_STOPPED 4U
#define BM_GPU_STATE_COMPLETED 5U
#define BM_GPU_STATE_ERROR 6U

#define BM_GPU_FP16_EMULATED 0U
#define BM_GPU_FP16_NATIVE 1U
#define BM_GPU_TIMING_HOST_FALLBACK 0U
#define BM_GPU_TIMING_TIMESTAMP 1U

#define BM_GPU_FLAG_OVERALL_SCORE_VALID (1U << 0U)
#define BM_GPU_FLAG_REDUCED_WORKING_SET (1U << 1U)
#define BM_GPU_FLAG_VULKAN_AVAILABLE (1U << 2U)
#define BM_GPU_LAST_ERROR_CAPACITY 192U

typedef struct bm_gpu_request_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t test_id;
  uint32_t duration_ms;
  uint32_t warmup_ms;
  uint32_t reserved_0;
} bm_gpu_request_v1;

typedef struct bm_gpu_snapshot_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint64_t run_id;
  uint32_t state;
  uint32_t active_test;
  uint32_t fp16_mode;
  uint32_t timing_mode;
  int32_t error_code;
  uint32_t flags;
  uint64_t elapsed_ns;
  uint64_t buffer_bytes;
  double fp32_gflops;
  double fp16_gflops;
  double fp16_scaling;
  double int32_gops;
  double mixed_gwork;
  double memory_bandwidth_gbps;
  double overall_score;
  double progress;
  double gpu_batch_ms;
  uint32_t workgroup_size;
  uint32_t dispatch_count;
  uint32_t iteration_count;
  uint32_t reserved_0;
  char last_error[BM_GPU_LAST_ERROR_CAPACITY];
} bm_gpu_snapshot_v1;

BM_FFI_EXPORT int32_t bm_gpu_engine_create(bm_gpu_engine_handle *out_engine);
BM_FFI_EXPORT int32_t bm_gpu_engine_destroy(bm_gpu_engine_handle engine);
BM_FFI_EXPORT int32_t bm_gpu_start(bm_gpu_engine_handle engine,
                                   const bm_gpu_request_v1 *request,
                                   uint64_t *out_run_id);
BM_FFI_EXPORT int32_t bm_gpu_request_stop(bm_gpu_engine_handle engine,
                                          uint64_t run_id);
BM_FFI_EXPORT int32_t bm_gpu_get_snapshot(bm_gpu_engine_handle engine,
                                          bm_gpu_snapshot_v1 *out_snapshot);
BM_FFI_EXPORT int32_t bm_gpu_get_capabilities_json(
    bm_gpu_engine_handle engine, char *out_json, uint32_t capacity,
    uint32_t *out_required);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // RAPIDBENCH_GPU_FFI_H_
