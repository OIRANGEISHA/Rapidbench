#ifndef RAPIDBENCH_STORAGE_FFI_H_
#define RAPIDBENCH_STORAGE_FFI_H_

#include <stdint.h>

#if defined(_WIN32)
#if defined(BM_FFI_BUILDING_LIBRARY)
#define BM_STORAGE_EXPORT __declspec(dllexport)
#else
#define BM_STORAGE_EXPORT __declspec(dllimport)
#endif
#else
#define BM_STORAGE_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BM_STORAGE_ABI_VERSION 2U
#define BM_STORAGE_RESULT_COUNT 11U

#define BM_STORAGE_TEST_SEQ_READ 1U
#define BM_STORAGE_TEST_SEQ_WRITE 2U
#define BM_STORAGE_TEST_4K_Q1T1_READ 3U
#define BM_STORAGE_TEST_4K_Q1T1_WRITE 4U
#define BM_STORAGE_TEST_4K_Q8T1_READ 5U
#define BM_STORAGE_TEST_4K_Q8T1_WRITE 6U
#define BM_STORAGE_TEST_4K_Q1T4_READ 7U
#define BM_STORAGE_TEST_4K_Q1T4_WRITE 8U
#define BM_STORAGE_TEST_SQLITE_INSERT 9U
#define BM_STORAGE_TEST_SQLITE_DELETE 10U
#define BM_STORAGE_TEST_SQLITE_UPDATE 11U
#define BM_STORAGE_TEST_ALL 12U

typedef void *bm_storage_engine_handle;

typedef struct bm_storage_request_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t test_id;
  uint32_t duration_ms;
  uint32_t warmup_ms;
  uint32_t file_size_mib_override;
  uint32_t reserved_0;
} bm_storage_request_v1;

typedef struct bm_storage_result_v1 {
  uint32_t struct_size;
  uint32_t test_id;
  uint32_t valid;
  uint32_t stopped;
  uint32_t io_mode;
  int32_t error_code;
  uint32_t thread_count;
  uint32_t queue_depth;
  uint32_t max_outstanding;
  uint64_t elapsed_ns;
  uint64_t completed_bytes;
  uint64_t completed_io;
  uint64_t completed_rows;
  double mbps;
  double iops;
  double rows_per_second;
} bm_storage_result_v1;

typedef struct bm_storage_snapshot_v2 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint64_t run_id;
  uint32_t state;
  uint32_t phase;
  uint32_t active_test_id;
  uint32_t io_mode;
  int32_t error_code;
  uint32_t alignment;
  uint32_t test_file_mib;
  uint32_t block_size;
  uint32_t queue_depth;
  uint32_t thread_count;
  uint32_t current_outstanding;
  uint32_t max_outstanding;
  uint32_t result_count;
  uint64_t elapsed_ns;
  uint64_t completed_bytes;
  uint64_t completed_io;
  uint64_t completed_rows;
  double current_mbps;
  double current_iops;
  double current_rows_per_second;
  double progress;
  bm_storage_result_v1 results[BM_STORAGE_RESULT_COUNT];
} bm_storage_snapshot_v2;

BM_STORAGE_EXPORT int32_t bm_storage_engine_create(
    const char *directory, bm_storage_engine_handle *out_engine);
BM_STORAGE_EXPORT int32_t
bm_storage_engine_destroy(bm_storage_engine_handle engine);
BM_STORAGE_EXPORT int32_t bm_storage_start(
    bm_storage_engine_handle engine, const bm_storage_request_v1 *request,
    uint64_t *out_run_id);
BM_STORAGE_EXPORT int32_t bm_storage_request_stop(
    bm_storage_engine_handle engine, uint64_t run_id);
BM_STORAGE_EXPORT int32_t bm_storage_get_snapshot(
    bm_storage_engine_handle engine, bm_storage_snapshot_v2 *out_snapshot);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // RAPIDBENCH_STORAGE_FFI_H_
