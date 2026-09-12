#include "cpu_application_ffi.h"
#include "benchmark/cpu_application_engine.h"

namespace {
auto *Engine(bm_cpu_application_handle handle) {
  return static_cast<benchmark::CpuApplicationEngine *>(handle);
}
} // namespace
extern "C" {
int32_t bm_cpu_application_create(bm_cpu_application_handle *out_engine) {
  if (!out_engine)
    return BM_STATUS_INVALID_ARGUMENT;
  *out_engine = nullptr;
  try {
    *out_engine = new benchmark::CpuApplicationEngine();
    return BM_STATUS_OK;
  } catch (...) {
    return BM_STATUS_INTERNAL_ERROR;
  }
}
int32_t bm_cpu_application_destroy(bm_cpu_application_handle engine) {
  if (!engine)
    return BM_STATUS_INVALID_ARGUMENT;
  delete Engine(engine);
  return BM_STATUS_OK;
}
int32_t bm_cpu_application_start(bm_cpu_application_handle engine,
                                 const bm_cpu_application_request_v1 *request,
                                 uint64_t *run_id) {
  if (!engine || !request || !run_id ||
      request->struct_size != sizeof(*request))
    return BM_STATUS_INVALID_ARGUMENT;
  if (request->abi_version != BM_ABI_VERSION)
    return BM_STATUS_ABI_MISMATCH;
  try {
    benchmark::CpuApplicationRequest native;
    native.test = static_cast<benchmark::CpuApplicationTest>(request->test_id);
    native.duration_ms = request->duration_ms;
    native.warmup_ms = request->warmup_ms;
    native.requested_threads = request->requested_threads;
    return Engine(engine)->Start(native, run_id);
  } catch (...) {
    return BM_STATUS_INTERNAL_ERROR;
  }
}
int32_t bm_cpu_application_stop(bm_cpu_application_handle engine,
                                uint64_t run_id) {
  if (!engine)
    return BM_STATUS_INVALID_ARGUMENT;
  try {
    return Engine(engine)->RequestStop(run_id);
  } catch (...) {
    return BM_STATUS_INTERNAL_ERROR;
  }
}
int32_t bm_cpu_application_snapshot(bm_cpu_application_handle engine,
                                    bm_cpu_application_snapshot_v1 *snapshot) {
  if (!engine || !snapshot || snapshot->struct_size != sizeof(*snapshot))
    return BM_STATUS_INVALID_ARGUMENT;
  if (snapshot->abi_version != BM_ABI_VERSION)
    return BM_STATUS_ABI_MISMATCH;
  try {
    const auto native = Engine(engine)->GetSnapshot();
    *snapshot = {sizeof(*snapshot),
                 BM_ABI_VERSION,
                 native.run_id,
                 static_cast<uint32_t>(native.state),
                 static_cast<uint32_t>(native.test),
                 native.thread_count,
                 native.flags,
                 native.error_code,
                 native.affinity_failures,
                 native.input_bytes,
                 native.completed_units,
                 native.elapsed_ns,
                 native.units_per_second,
                 native.progress,
                 native.checksum,
                 native.method_version,
                 native.requested_threads,
                 native.selected_cpu,
                 0};
    return BM_STATUS_OK;
  } catch (...) {
    return BM_STATUS_INTERNAL_ERROR;
  }
}
}
static_assert(sizeof(bm_cpu_application_request_v1) == 24,
              "CPU application request layout");
static_assert(sizeof(bm_cpu_application_snapshot_v1) == 104,
              "CPU application snapshot layout");
