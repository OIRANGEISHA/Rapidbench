#include "benchmark_ffi.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>

#include "benchmark/engine.h"
#include "benchmark/memory_engine.h"
#include "benchmark/memory_frequency.h"
#include "benchmark/topology.h"

namespace {

benchmark::Engine *ToEngine(bm_engine_handle handle) {
  return static_cast<benchmark::Engine *>(handle);
}

benchmark::MemoryEngine *ToMemoryEngine(bm_memory_engine_handle handle) {
  return static_cast<benchmark::MemoryEngine *>(handle);
}

bool ValidRequest(const bm_request_v2 *request) {
  return request != nullptr && request->struct_size == sizeof(bm_request_v2) &&
         request->abi_version == BM_ABI_VERSION;
}

bool ValidSnapshot(bm_snapshot_v4 *snapshot) {
  return snapshot != nullptr &&
         snapshot->struct_size == sizeof(bm_snapshot_v4) &&
         snapshot->abi_version == BM_ABI_VERSION;
}

bool ValidTopology(bm_topology_v1 *topology) {
  return topology != nullptr &&
         topology->struct_size == sizeof(bm_topology_v1) &&
         topology->abi_version == BM_ABI_VERSION;
}

bool ValidMemoryRequest(const bm_memory_request_v1 *request) {
  return request != nullptr &&
         request->struct_size == sizeof(bm_memory_request_v1) &&
         request->abi_version == BM_ABI_VERSION;
}

bool ValidMemorySnapshot(bm_memory_snapshot_v1 *snapshot) {
  return snapshot != nullptr &&
         snapshot->struct_size == sizeof(bm_memory_snapshot_v1) &&
         snapshot->abi_version == BM_ABI_VERSION;
}

bool ValidMemoryFrequency(bm_memory_frequency_v1 *frequency) {
  return frequency != nullptr &&
         frequency->struct_size == sizeof(bm_memory_frequency_v1) &&
         frequency->abi_version == BM_ABI_VERSION;
}

} // namespace

extern "C" {

uint32_t bm_get_abi_version(void) { return BM_ABI_VERSION; }

int32_t bm_get_topology(bm_topology_v1 *out_topology) {
  if (out_topology == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  if (out_topology->abi_version != BM_ABI_VERSION) {
    return BM_STATUS_ABI_MISMATCH;
  }
  if (!ValidTopology(out_topology)) {
    return BM_STATUS_INVALID_ARGUMENT;
  }

  const benchmark::Topology native = benchmark::DetectTopology();
  bm_topology_v1 result{};
  result.struct_size = sizeof(result);
  result.abi_version = BM_ABI_VERSION;
  result.online_count = native.online_count;
  result.allowed_count = native.allowed_count;
  result.performance_group_count = native.performance_group_count;
  result.preferred_single_cpu = native.preferred_single_cpu;
  result.quality_flags = native.quality_flags;

  const std::size_t copy_count =
      std::min<std::size_t>(native.cpus.size(), BM_MAX_LOGICAL_CPUS);
  result.cpu_count = static_cast<uint32_t>(copy_count);
  if (copy_count < native.cpus.size()) {
    result.quality_flags |= BM_QUALITY_TOPOLOGY_TRUNCATED;
  }

  for (std::size_t index = 0; index < copy_count; ++index) {
    const benchmark::CpuInfo &source = native.cpus[index];
    bm_cpu_info_v1 &destination = result.cpus[index];
    destination.struct_size = sizeof(destination);
    destination.logical_cpu = source.logical_cpu;
    destination.cluster_id = source.cluster_id;
    destination.core_id = source.core_id;
    destination.max_frequency_khz = source.max_frequency_khz;
    destination.capacity = source.capacity;
    destination.performance_group = source.performance_group;
    if (source.online) {
      destination.flags |= BM_CPU_FLAG_ONLINE;
    }
    if (source.allowed) {
      destination.flags |= BM_CPU_FLAG_ALLOWED;
    }
  }

  *out_topology = result;
  return BM_STATUS_OK;
}

int32_t bm_engine_create(bm_engine_handle *out_engine) {
  if (out_engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  auto *engine = new (std::nothrow) benchmark::Engine();
  if (engine == nullptr) {
    return BM_STATUS_INTERNAL_ERROR;
  }
  *out_engine = engine;
  return BM_STATUS_OK;
}

int32_t bm_engine_destroy(bm_engine_handle engine) {
  if (engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  delete ToEngine(engine);
  return BM_STATUS_OK;
}

int32_t bm_start(bm_engine_handle engine, const bm_request_v2 *request,
                 uint64_t *out_run_id) {
  if (engine == nullptr || request == nullptr || out_run_id == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  if (request->abi_version != BM_ABI_VERSION) {
    return BM_STATUS_ABI_MISMATCH;
  }
  if (!ValidRequest(request)) {
    return BM_STATUS_INVALID_ARGUMENT;
  }

  benchmark::Request native_request{};
  native_request.test_id = static_cast<benchmark::TestId>(request->test_id);
  native_request.duration_ms = request->duration_ms;
  native_request.warmup_ms = request->warmup_ms;
  native_request.requested_threads = request->requested_threads;
  native_request.flags = request->flags;
  return ToEngine(engine)->Start(native_request, out_run_id);
}

int32_t bm_request_stop(bm_engine_handle engine, uint64_t run_id) {
  if (engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  return ToEngine(engine)->RequestStop(run_id);
}

int32_t bm_get_snapshot(bm_engine_handle engine, bm_snapshot_v4 *out_snapshot) {
  if (engine == nullptr || out_snapshot == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  if (out_snapshot->abi_version != BM_ABI_VERSION) {
    return BM_STATUS_ABI_MISMATCH;
  }
  if (!ValidSnapshot(out_snapshot)) {
    return BM_STATUS_INVALID_ARGUMENT;
  }

  const benchmark::Snapshot snapshot = ToEngine(engine)->GetSnapshot();
  out_snapshot->run_id = snapshot.run_id;
  out_snapshot->state = static_cast<uint32_t>(snapshot.state);
  out_snapshot->phase = static_cast<uint32_t>(snapshot.phase);
  out_snapshot->test_id = static_cast<uint32_t>(snapshot.test_id);
  out_snapshot->thread_count = snapshot.thread_count;
  out_snapshot->quality_flags = snapshot.quality_flags;
  out_snapshot->elapsed_ns = snapshot.elapsed_ns;
  out_snapshot->completed_work = snapshot.completed_work;
  out_snapshot->current_value = snapshot.current_value;
  out_snapshot->progress = snapshot.progress;
  out_snapshot->score_variation = snapshot.score_variation;
  out_snapshot->peak_score = snapshot.peak_score;
  out_snapshot->error_code = snapshot.error_code;
  out_snapshot->selected_cpu = snapshot.selected_cpu;
  out_snapshot->affinity_failures = snapshot.affinity_failures;
  out_snapshot->telemetry = snapshot.telemetry;
  out_snapshot->peak_frequency_group_count = std::min<std::uint32_t>(
      snapshot.peak_frequency_group_count, BM_MAX_PERFORMANCE_GROUPS);
  std::fill_n(out_snapshot->peak_frequency_mhz_by_group,
              BM_MAX_PERFORMANCE_GROUPS, 0U);
  for (std::uint32_t index = 0;
       index < out_snapshot->peak_frequency_group_count; ++index) {
    out_snapshot->peak_frequency_mhz_by_group[index] =
        (snapshot.peak_frequency_khz_by_group[index] + 500U) / 1000U;
  }
  return BM_STATUS_OK;
}

int32_t bm_memory_engine_create(bm_memory_engine_handle *out_engine) {
  if (out_engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  auto *engine = new (std::nothrow) benchmark::MemoryEngine();
  if (engine == nullptr) {
    return BM_STATUS_INTERNAL_ERROR;
  }
  *out_engine = engine;
  return BM_STATUS_OK;
}

int32_t bm_memory_engine_destroy(bm_memory_engine_handle engine) {
  if (engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  delete ToMemoryEngine(engine);
  return BM_STATUS_OK;
}

int32_t bm_memory_start(bm_memory_engine_handle engine,
                        const bm_memory_request_v1 *request,
                        uint64_t *out_run_id) {
  if (engine == nullptr || request == nullptr || out_run_id == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  if (request->abi_version != BM_ABI_VERSION) {
    return BM_STATUS_ABI_MISMATCH;
  }
  if (!ValidMemoryRequest(request)) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  benchmark::MemoryRequest native_request{};
  native_request.test = static_cast<benchmark::MemoryTest>(request->test_id);
  native_request.duration_ms = request->duration_ms;
  native_request.warmup_ms = request->warmup_ms;
  return ToMemoryEngine(engine)->Start(native_request, out_run_id);
}

int32_t bm_memory_request_stop(bm_memory_engine_handle engine,
                               uint64_t run_id) {
  if (engine == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  return ToMemoryEngine(engine)->RequestStop(run_id);
}

int32_t bm_memory_get_snapshot(bm_memory_engine_handle engine,
                               bm_memory_snapshot_v1 *out_snapshot) {
  if (engine == nullptr || out_snapshot == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  if (out_snapshot->abi_version != BM_ABI_VERSION) {
    return BM_STATUS_ABI_MISMATCH;
  }
  if (!ValidMemorySnapshot(out_snapshot)) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  const benchmark::MemorySnapshot native =
      ToMemoryEngine(engine)->GetSnapshot();
  bm_memory_snapshot_v1 result{};
  result.struct_size = sizeof(result);
  result.abi_version = BM_ABI_VERSION;
  result.run_id = native.run_id;
  result.state = native.state;
  result.test_id = static_cast<std::uint32_t>(native.test);
  result.thread_count = native.thread_count;
  result.error_code = native.error_code;
  result.affinity_failures = native.affinity_failures;
  result.performance_request_failures = native.performance_request_failures;
  result.buffer_bytes = native.buffer_bytes;
  result.elapsed_ns = native.elapsed_ns;
  result.processed_bytes = native.processed_bytes;
  result.bandwidth_gbps = native.bandwidth_gbps;
  result.progress = native.progress;
  *out_snapshot = result;
  return BM_STATUS_OK;
}

int32_t bm_get_memory_frequency(bm_memory_frequency_v1 *out_frequency) {
  if (out_frequency == nullptr) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  if (out_frequency->abi_version != BM_ABI_VERSION) {
    return BM_STATUS_ABI_MISMATCH;
  }
  if (!ValidMemoryFrequency(out_frequency)) {
    return BM_STATUS_INVALID_ARGUMENT;
  }
  const benchmark::MemoryFrequency native = benchmark::ReadMemoryFrequency();
  bm_memory_frequency_v1 result{};
  result.struct_size = sizeof(result);
  result.abi_version = BM_ABI_VERSION;
  result.status = native.status;
  result.flags = native.flags;
  result.current_hz = native.current_hz;
  result.maximum_hz = native.maximum_hz;
  *out_frequency = result;
  return BM_STATUS_OK;
}

} // extern "C"

static_assert(sizeof(bm_request_v2) == 32,
              "bm_request_v2 ABI layout changed unexpectedly");
static_assert(sizeof(bm_snapshot_v4) == 172 || sizeof(bm_snapshot_v4) == 176,
              "bm_snapshot_v4 ABI layout changed unexpectedly");
static_assert(sizeof(bm_cpu_info_v1) == 32,
              "bm_cpu_info_v1 ABI layout changed unexpectedly");
static_assert(sizeof(bm_topology_v1) == 2088,
              "bm_topology_v1 ABI layout changed unexpectedly");
static_assert(sizeof(bm_memory_request_v1) == 24,
              "bm_memory_request_v1 ABI layout changed unexpectedly");
static_assert(sizeof(bm_memory_snapshot_v1) == 80,
              "bm_memory_snapshot_v1 ABI layout changed unexpectedly");
static_assert(sizeof(bm_memory_frequency_v1) == 32,
              "bm_memory_frequency_v1 ABI layout changed unexpectedly");
