#ifndef CPU_BENCHMARK_FFI_H_
#define CPU_BENCHMARK_FFI_H_

#include <stdint.h>

#if defined(_WIN32)
#if defined(BM_FFI_BUILDING_LIBRARY)
#define BM_FFI_EXPORT __declspec(dllexport)
#else
#define BM_FFI_EXPORT __declspec(dllimport)
#endif
#else
#define BM_FFI_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BM_ABI_VERSION 4U
#define BM_MAX_LOGICAL_CPUS 64U
#define BM_MAX_PERFORMANCE_GROUPS 16U

#define BM_TEST_PHASE1_WORKLOAD 1U
#define BM_TEST_CPU_SINGLE 2U
#define BM_TEST_CPU_MULTI 3U

#define BM_MEMORY_TEST_READ 1U
#define BM_MEMORY_TEST_WRITE 2U
#define BM_MEMORY_TEST_COPY 3U

#define BM_REQUEST_FLAG_SINGLE_CPU_EXPLICIT (1ULL << 0U)
#define BM_REQUEST_FLAG_MULTI_GROUP_EXPLICIT (1ULL << 1U)
#define BM_REQUEST_SINGLE_CPU_SHIFT 8U
#define BM_REQUEST_SINGLE_CPU_MASK (0xFFULL << BM_REQUEST_SINGLE_CPU_SHIFT)
#define BM_REQUEST_MULTI_GROUP_SHIFT 16U
#define BM_REQUEST_MULTI_GROUP_MASK (0xFFULL << BM_REQUEST_MULTI_GROUP_SHIFT)

#define BM_CPU_FLAG_ONLINE (1U << 0U)
#define BM_CPU_FLAG_ALLOWED (1U << 1U)

#define BM_TELEMETRY_HINT_ACTIVE (1U << 10U)
#define BM_TELEMETRY_AFFINITY_CHECKED (1U << 11U)
#define BM_TELEMETRY_PERFORMANCE_REQUEST_ACTIVE (1U << 12U)
#define BM_TELEMETRY_FREQUENCY_SHIFT 16U

#define BM_MEMORY_FREQUENCY_CURRENT_AVAILABLE (1U << 0U)
#define BM_MEMORY_FREQUENCY_MAXIMUM_AVAILABLE (1U << 1U)

#define BM_QUALITY_CAPACITY_MISSING (1ULL << 0U)
#define BM_QUALITY_MAX_FREQUENCY_MISSING (1ULL << 1U)
#define BM_QUALITY_CLUSTER_ID_MISSING (1ULL << 2U)
#define BM_QUALITY_AFFINITY_UNAVAILABLE (1ULL << 3U)
#define BM_QUALITY_TOPOLOGY_TRUNCATED (1ULL << 4U)
#define BM_QUALITY_PERFORMANCE_GROUPS_INFERRED (1ULL << 5U)
#define BM_QUALITY_AFFINITY_FAILED (1ULL << 16U)
#define BM_QUALITY_THREAD_COUNT_REDUCED (1ULL << 17U)
#define BM_QUALITY_HIGH_SCORE_VARIATION (1ULL << 18U)
#define BM_QUALITY_SELECTION_FALLBACK (1ULL << 19U)
#define BM_QUALITY_PERFORMANCE_REQUEST_UNAVAILABLE (1ULL << 20U)

typedef void *bm_engine_handle;
typedef void *bm_memory_engine_handle;

typedef enum bm_status_code {
  BM_STATUS_OK = 0,
  BM_STATUS_INVALID_ARGUMENT = -1,
  BM_STATUS_BUSY = -2,
  BM_STATUS_INTERNAL_ERROR = -3,
  BM_STATUS_ABI_MISMATCH = -4,
} bm_status_code;

#define BM_ERROR_TOPOLOGY_UNAVAILABLE (-10)
#define BM_ERROR_THREAD_CREATION_FAILED (-11)
#define BM_ERROR_AFFINITY_UNAVAILABLE (-12)
#define BM_MEMORY_ERROR_ALLOCATION_FAILED (-20)
#define BM_MEMORY_ERROR_NO_CPU_AVAILABLE (-21)
#define BM_MEMORY_ERROR_THREAD_CREATION_FAILED (-22)

typedef enum bm_state {
  BM_STATE_IDLE = 0,
  BM_STATE_PREPARING = 1,
  BM_STATE_WARMING_UP = 2,
  BM_STATE_MEASURING = 3,
  BM_STATE_COMPLETED = 4,
  BM_STATE_CANCELLED = 5,
  BM_STATE_ERROR = 6,
} bm_state;

typedef enum bm_phase {
  BM_PHASE_NONE = 0,
  BM_PHASE_WARM_UP = 1,
  BM_PHASE_MEASURE = 2,
  BM_PHASE_FINALIZE = 3,
} bm_phase;

typedef struct bm_request_v2 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t test_id;
  uint32_t duration_ms;
  uint32_t warmup_ms;
  uint32_t requested_threads;
  uint64_t flags;
} bm_request_v2;

typedef struct bm_snapshot_v4 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint64_t run_id;
  uint32_t state;
  uint32_t phase;
  uint32_t test_id;
  uint32_t thread_count;
  uint64_t quality_flags;
  uint64_t elapsed_ns;
  uint64_t completed_work;
  double current_value;
  double progress;
  double score_variation;
  double peak_score;
  int32_t error_code;
  int32_t selected_cpu;
  uint32_t affinity_failures;
  uint32_t telemetry;
  uint32_t peak_frequency_group_count;
  uint32_t peak_frequency_mhz_by_group[BM_MAX_PERFORMANCE_GROUPS];
} bm_snapshot_v4;

typedef struct bm_cpu_info_v1 {
  uint32_t struct_size;
  uint32_t logical_cpu;
  int32_t cluster_id;
  int32_t core_id;
  uint32_t max_frequency_khz;
  uint32_t capacity;
  uint32_t performance_group;
  uint32_t flags;
} bm_cpu_info_v1;

typedef struct bm_topology_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t cpu_count;
  uint32_t online_count;
  uint32_t allowed_count;
  uint32_t performance_group_count;
  int32_t preferred_single_cpu;
  uint32_t reserved_0;
  uint64_t quality_flags;
  bm_cpu_info_v1 cpus[BM_MAX_LOGICAL_CPUS];
} bm_topology_v1;

typedef struct bm_memory_request_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t test_id;
  uint32_t duration_ms;
  uint32_t warmup_ms;
  uint32_t reserved_0;
} bm_memory_request_v1;

typedef struct bm_memory_snapshot_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint64_t run_id;
  uint32_t state;
  uint32_t test_id;
  uint32_t thread_count;
  int32_t error_code;
  uint32_t affinity_failures;
  /* Activated from the original reserved slot; binary layout is unchanged. */
  uint32_t performance_request_failures;
  uint64_t buffer_bytes;
  uint64_t elapsed_ns;
  uint64_t processed_bytes;
  double bandwidth_gbps;
  double progress;
} bm_memory_snapshot_v1;

typedef struct bm_memory_frequency_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  int32_t status;
  uint32_t flags;
  uint64_t current_hz;
  uint64_t maximum_hz;
} bm_memory_frequency_v1;

BM_FFI_EXPORT uint32_t bm_get_abi_version(void);
BM_FFI_EXPORT int32_t bm_get_topology(bm_topology_v1 *out_topology);
BM_FFI_EXPORT int32_t bm_engine_create(bm_engine_handle *out_engine);
BM_FFI_EXPORT int32_t bm_engine_destroy(bm_engine_handle engine);
BM_FFI_EXPORT int32_t bm_start(bm_engine_handle engine,
                               const bm_request_v2 *request,
                               uint64_t *out_run_id);
BM_FFI_EXPORT int32_t bm_request_stop(bm_engine_handle engine, uint64_t run_id);
BM_FFI_EXPORT int32_t bm_get_snapshot(bm_engine_handle engine,
                                      bm_snapshot_v4 *out_snapshot);

BM_FFI_EXPORT int32_t
bm_memory_engine_create(bm_memory_engine_handle *out_engine);
BM_FFI_EXPORT int32_t bm_memory_engine_destroy(bm_memory_engine_handle engine);
BM_FFI_EXPORT int32_t bm_memory_start(bm_memory_engine_handle engine,
                                      const bm_memory_request_v1 *request,
                                      uint64_t *out_run_id);
BM_FFI_EXPORT int32_t bm_memory_request_stop(bm_memory_engine_handle engine,
                                             uint64_t run_id);
BM_FFI_EXPORT int32_t bm_memory_get_snapshot(
    bm_memory_engine_handle engine, bm_memory_snapshot_v1 *out_snapshot);
BM_FFI_EXPORT int32_t
bm_get_memory_frequency(bm_memory_frequency_v1 *out_frequency);
BM_FFI_EXPORT int32_t bm_get_cpu_isa_info_json(char *out_json,
                                               uint32_t capacity,
                                               uint32_t *out_required);
BM_FFI_EXPORT int32_t bm_get_vulkan_info_json(char *out_json,
                                              uint32_t capacity,
                                              uint32_t *out_required);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CPU_BENCHMARK_FFI_H_

