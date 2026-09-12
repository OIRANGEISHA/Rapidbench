#ifndef RAPIDBENCH_CPU_APPLICATION_FFI_H_
#define RAPIDBENCH_CPU_APPLICATION_FFI_H_
#include "benchmark_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef void *bm_cpu_application_handle;
// Additive API: existing CPU request/snapshot layouts and IDs do not change.
typedef struct bm_cpu_application_request_v1 {
  uint32_t struct_size, abi_version, test_id, duration_ms, warmup_ms,
      requested_threads;
} bm_cpu_application_request_v1;
typedef struct bm_cpu_application_snapshot_v1 {
  uint32_t struct_size, abi_version;
  uint64_t run_id;
  uint32_t state, test_id, thread_count, flags;
  int32_t error_code;
  uint32_t affinity_failures;
  uint64_t input_bytes, completed_units, elapsed_ns;
  double units_per_second, progress;
  uint64_t checksum;
  uint32_t method_version, requested_threads;
  int32_t selected_cpu;
  uint32_t
      reserved; // Always zero; keeps the snapshot's explicit 8-byte layout.
} bm_cpu_application_snapshot_v1;
BM_FFI_EXPORT int32_t
bm_cpu_application_create(bm_cpu_application_handle *out_engine);
BM_FFI_EXPORT int32_t
bm_cpu_application_destroy(bm_cpu_application_handle engine);
BM_FFI_EXPORT int32_t bm_cpu_application_start(
    bm_cpu_application_handle engine,
    const bm_cpu_application_request_v1 *request, uint64_t *run_id);
BM_FFI_EXPORT int32_t bm_cpu_application_stop(bm_cpu_application_handle engine,
                                              uint64_t run_id);
BM_FFI_EXPORT int32_t bm_cpu_application_snapshot(
    bm_cpu_application_handle engine, bm_cpu_application_snapshot_v1 *snapshot);
#ifdef __cplusplus
}
#endif
#endif
