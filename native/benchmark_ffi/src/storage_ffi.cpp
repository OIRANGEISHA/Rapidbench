#include "storage_ffi.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>

#include "benchmark/storage_engine.h"
#include "benchmark_ffi.h"

namespace {

benchmark::StorageEngine *ToStorageEngine(bm_storage_engine_handle handle) {
  return static_cast<benchmark::StorageEngine *>(handle);
}

bool ValidRequest(const bm_storage_request_v1 *request) {
  return request != nullptr &&
         request->struct_size == sizeof(bm_storage_request_v1) &&
         request->abi_version == BM_STORAGE_ABI_VERSION;
}

bool ValidSnapshot(const bm_storage_snapshot_v1 *snapshot) {
  return snapshot != nullptr &&
         snapshot->struct_size == sizeof(bm_storage_snapshot_v1) &&
         snapshot->abi_version == BM_STORAGE_ABI_VERSION;
}

void CopyResult(const benchmark::StorageResult &source,
                bm_storage_result_v1 *destination) {
  *destination = {};
  destination->struct_size = sizeof(*destination);
  destination->test_id = static_cast<std::uint32_t>(source.test);
  destination->valid = source.valid ? 1U : 0U;
  destination->stopped = source.stopped ? 1U : 0U;
  destination->io_mode = static_cast<std::uint32_t>(source.io_mode);
  destination->error_code = source.error_code;
  destination->thread_count = source.thread_count;
  destination->queue_depth = source.queue_depth;
  destination->max_outstanding = source.max_outstanding;
  destination->elapsed_ns = source.elapsed_ns;
  destination->completed_bytes = source.completed_bytes;
  destination->completed_io = source.completed_io;
  destination->completed_rows = source.completed_rows;
  destination->mbps = source.mbps;
  destination->iops = source.iops;
  destination->rows_per_second = source.rows_per_second;
}

} // namespace

extern "C" {

int32_t bm_storage_engine_create(const char *directory,
                                 bm_storage_engine_handle *out_engine) {
  if (directory == nullptr || directory[0] == '\0' || out_engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  auto *engine =
      new (std::nothrow) benchmark::StorageEngine(std::string(directory));
  if (engine == nullptr) {
    return BM_STATUS_INTERNAL_ERROR;
  }
  *out_engine = engine;
  return BM_STATUS_OK;
}

int32_t bm_storage_engine_destroy(bm_storage_engine_handle engine) {
  if (engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  delete ToStorageEngine(engine);
  return BM_STATUS_OK;
}

int32_t bm_storage_start(bm_storage_engine_handle engine,
                         const bm_storage_request_v1 *request,
                         uint64_t *out_run_id) {
  if (engine == nullptr || request == nullptr || out_run_id == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  if (request->abi_version != BM_STORAGE_ABI_VERSION) {
    return BM_STATUS_ABI_MISMATCH;
  }
  if (!ValidRequest(request)) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  benchmark::StorageRequest native{};
  native.test = static_cast<benchmark::StorageTest>(request->test_id);
  native.duration_ms = request->duration_ms;
  native.warmup_ms = request->warmup_ms;
  native.file_size_mib_override = request->file_size_mib_override;
  return ToStorageEngine(engine)->Start(native, out_run_id);
}

int32_t bm_storage_request_stop(bm_storage_engine_handle engine,
                                uint64_t run_id) {
  if (engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  return ToStorageEngine(engine)->RequestStop(run_id);
}

int32_t bm_storage_get_snapshot(bm_storage_engine_handle engine,
                                bm_storage_snapshot_v1 *out_snapshot) {
  if (engine == nullptr || out_snapshot == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  if (out_snapshot->abi_version != BM_STORAGE_ABI_VERSION) {
    return BM_STATUS_ABI_MISMATCH;
  }
  if (!ValidSnapshot(out_snapshot)) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  const benchmark::StorageSnapshot source =
      ToStorageEngine(engine)->GetSnapshot();
  bm_storage_snapshot_v1 result{};
  result.struct_size = sizeof(result);
  result.abi_version = BM_STORAGE_ABI_VERSION;
  result.run_id = source.run_id;
  result.state = static_cast<std::uint32_t>(source.state);
  result.phase = static_cast<std::uint32_t>(source.phase);
  result.active_test_id = static_cast<std::uint32_t>(source.active_test);
  result.io_mode = static_cast<std::uint32_t>(source.io_mode);
  result.error_code = source.error_code;
  result.alignment = source.alignment;
  result.test_file_mib = source.test_file_mib;
  result.block_size = source.block_size;
  result.queue_depth = source.queue_depth;
  result.thread_count = source.thread_count;
  result.current_outstanding = source.current_outstanding;
  result.max_outstanding = source.max_outstanding;
  result.result_count = BM_STORAGE_RESULT_COUNT;
  result.elapsed_ns = source.elapsed_ns;
  result.completed_bytes = source.completed_bytes;
  result.completed_io = source.completed_io;
  result.completed_rows = source.completed_rows;
  result.current_mbps = source.current_mbps;
  result.current_iops = source.current_iops;
  result.current_rows_per_second = source.current_rows_per_second;
  result.progress = source.progress;
  for (std::size_t index = 0U; index < BM_STORAGE_RESULT_COUNT; ++index) {
    CopyResult(source.results[index], &result.results[index]);
  }
  *out_snapshot = result;
  return BM_STATUS_OK;
}

} // extern "C"

static_assert(sizeof(bm_storage_request_v1) == 28,
              "bm_storage_request_v1 ABI layout changed unexpectedly");
static_assert(sizeof(bm_storage_result_v1) == 96,
              "bm_storage_result_v1 ABI layout changed unexpectedly");
static_assert(sizeof(bm_storage_snapshot_v1) == 1096,
              "bm_storage_snapshot_v1 ABI layout changed unexpectedly");
