#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "benchmark_ffi.h"

namespace {

bool IsTerminal(std::uint32_t state) {
  return state == BM_STATE_COMPLETED || state == BM_STATE_CANCELLED ||
         state == BM_STATE_ERROR;
}

int Fail(const char *message, bm_engine_handle engine = nullptr) {
  std::cerr << message << '\n';
  if (engine != nullptr) {
    bm_engine_destroy(engine);
  }
  return 1;
}

bool WaitForTerminal(bm_engine_handle engine, bm_snapshot_v4 *snapshot) {
  std::uint64_t previous_elapsed = 0;
  std::uint64_t previous_work = 0;
  for (int poll = 0; poll < 2000; ++poll) {
    snapshot->struct_size = sizeof(*snapshot);
    snapshot->abi_version = BM_ABI_VERSION;
    if (bm_get_snapshot(engine, snapshot) != BM_STATUS_OK) {
      return false;
    }
    if (snapshot->elapsed_ns < previous_elapsed ||
        snapshot->completed_work < previous_work) {
      return false;
    }
    previous_elapsed = snapshot->elapsed_ns;
    previous_work = snapshot->completed_work;
    if (IsTerminal(snapshot->state)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

bool StartRun(bm_engine_handle engine, std::uint32_t test_id,
              std::uint32_t duration_ms, std::uint32_t warmup_ms,
              std::uint64_t *run_id, std::uint64_t flags = 0) {
  bm_request_v2 request{};
  request.struct_size = sizeof(request);
  request.abi_version = BM_ABI_VERSION;
  request.test_id = test_id;
  request.duration_ms = duration_ms;
  request.warmup_ms = warmup_ms;
  request.flags = flags;
  return bm_start(engine, &request, run_id) == BM_STATUS_OK && *run_id != 0;
}

bool HasConsistentAffinityStatus(const bm_snapshot_v4 &snapshot,
                                 std::uint32_t attempted_workers) {
  return snapshot.affinity_failures <= attempted_workers &&
         (snapshot.affinity_failures == 0 ||
          (snapshot.quality_flags & BM_QUALITY_AFFINITY_FAILED) != 0);
}

bool HasValidScoreSummary(const bm_snapshot_v4 &snapshot) {
  return snapshot.current_value > 0.0 &&
         snapshot.peak_score >= snapshot.current_value;
}

bool HasConsistentPerformanceRequestStatus(const bm_snapshot_v4 &snapshot) {
  const bool active =
      (snapshot.telemetry & BM_TELEMETRY_PERFORMANCE_REQUEST_ACTIVE) != 0;
  const bool unavailable = (snapshot.quality_flags &
                            BM_QUALITY_PERFORMANCE_REQUEST_UNAVAILABLE) != 0;
  return active != unavailable;
}

bool HasValidFfiErrorContract(bm_engine_handle engine) {
  if (bm_get_topology(nullptr) != BM_STATUS_INVALID_ARGUMENT ||
      bm_request_stop(engine, 0) != BM_STATUS_INVALID_ARGUMENT) {
    return false;
  }

  bm_request_v2 request{};
  request.struct_size = sizeof(request);
  request.abi_version = BM_ABI_VERSION;
  request.test_id = BM_TEST_CPU_SINGLE;
  request.duration_ms = 99;
  std::uint64_t run_id = 0;
  if (bm_start(engine, &request, &run_id) != BM_STATUS_INVALID_ARGUMENT) {
    return false;
  }

  request.duration_ms = 100;
  request.abi_version = BM_ABI_VERSION + 1;
  if (bm_start(engine, &request, &run_id) != BM_STATUS_ABI_MISMATCH) {
    return false;
  }

  bm_snapshot_v4 snapshot{};
  snapshot.struct_size = sizeof(snapshot);
  snapshot.abi_version = BM_ABI_VERSION + 1;
  return bm_get_snapshot(engine, &snapshot) == BM_STATUS_ABI_MISMATCH;
}

void PrintResult(const char *label, const bm_snapshot_v4 &snapshot) {
  std::cout
      << std::fixed << std::setprecision(1) << label
      << " score=" << snapshot.current_value
      << " peak_score=" << snapshot.peak_score
      << " threads=" << snapshot.thread_count
      << " selected_cpu=" << snapshot.selected_cpu
      << " variation=" << std::setprecision(2)
      << snapshot.score_variation * 100.0 << "%"
      << " affinity_failures=" << snapshot.affinity_failures << " boost_active="
      << ((snapshot.telemetry & BM_TELEMETRY_PERFORMANCE_REQUEST_ACTIVE) != 0)
      << " peak_mhz=" << (snapshot.telemetry >> BM_TELEMETRY_FREQUENCY_SHIFT);
  for (std::uint32_t group = 0; group < snapshot.peak_frequency_group_count;
       ++group) {
    std::cout << " g" << group
              << "_peak_mhz=" << snapshot.peak_frequency_mhz_by_group[group];
  }
  std::cout << " quality=0x" << std::hex << snapshot.quality_flags << std::dec
            << '\n';
}

} // namespace

int main(int argc, char **argv) {
  const bool full = argc > 1 && std::strcmp(argv[1], "--full") == 0;
  const std::uint32_t single_duration = full ? 3000U : 300U;
  const std::uint32_t single_warmup = full ? 800U : 50U;
  const std::uint32_t multi_duration = full ? 3500U : 300U;
  const std::uint32_t multi_warmup = full ? 1000U : 50U;

  if (bm_get_abi_version() != BM_ABI_VERSION) {
    return Fail("ABI version mismatch");
  }

  std::uint32_t isa_json_size = 0;
  if (bm_get_cpu_isa_info_json(nullptr, 0, &isa_json_size) != BM_STATUS_OK ||
      isa_json_size < 3U) {
    return Fail("CPU ISA query sizing failed");
  }
  std::vector<char> isa_json(isa_json_size);
  if (bm_get_cpu_isa_info_json(isa_json.data(), isa_json.size(),
                               &isa_json_size) != BM_STATUS_OK ||
      std::string(isa_json.data()).find("\"architectureLevel\"") ==
          std::string::npos ||
      std::string(isa_json.data()).find("\"features\"") ==
          std::string::npos) {
    return Fail("CPU ISA query payload failed");
  }

  bm_topology_v1 topology{};
  topology.struct_size = sizeof(topology);
  topology.abi_version = BM_ABI_VERSION;
  if (bm_get_topology(&topology) != BM_STATUS_OK || topology.cpu_count == 0 ||
      topology.allowed_count == 0 || topology.preferred_single_cpu < 0) {
    return Fail("topology discovery failed");
  }
  std::cout << "topology cpus=" << topology.cpu_count
            << " online=" << topology.online_count
            << " allowed=" << topology.allowed_count
            << " groups=" << topology.performance_group_count
            << " preferred_cpu=" << topology.preferred_single_cpu
            << " quality=0x" << std::hex << topology.quality_flags << std::dec
            << '\n';
  for (std::uint32_t index = 0; index < topology.cpu_count; ++index) {
    const bm_cpu_info_v1 &cpu = topology.cpus[index];
    if ((cpu.flags & BM_CPU_FLAG_ALLOWED) != 0) {
      std::cout << "cpu=" << cpu.logical_cpu
                << " group=" << cpu.performance_group
                << " capacity=" << cpu.capacity
                << " max_khz=" << cpu.max_frequency_khz
                << " cluster=" << cpu.cluster_id << '\n';
    }
  }

  std::uint32_t explicit_single_cpu =
      static_cast<std::uint32_t>(topology.preferred_single_cpu);
  for (std::uint32_t index = 0; index < topology.cpu_count; ++index) {
    const bm_cpu_info_v1 &cpu = topology.cpus[index];
    if ((cpu.flags & BM_CPU_FLAG_ALLOWED) != 0 &&
        (cpu.flags & BM_CPU_FLAG_ONLINE) != 0) {
      explicit_single_cpu = cpu.logical_cpu;
      break;
    }
  }
  const std::uint64_t explicit_single_flags =
      BM_REQUEST_FLAG_SINGLE_CPU_EXPLICIT |
      (static_cast<std::uint64_t>(explicit_single_cpu)
       << BM_REQUEST_SINGLE_CPU_SHIFT);

  std::uint32_t explicit_multi_group = 0;
  std::uint32_t explicit_multi_threads = 0;
  bool found_multi_group = false;
  for (std::uint32_t index = 0; index < topology.cpu_count; ++index) {
    const bm_cpu_info_v1 &cpu = topology.cpus[index];
    if ((cpu.flags & BM_CPU_FLAG_ALLOWED) == 0 ||
        (cpu.flags & BM_CPU_FLAG_ONLINE) == 0) {
      continue;
    }
    if (!found_multi_group) {
      explicit_multi_group = cpu.performance_group;
      found_multi_group = true;
    }
    if (cpu.performance_group == explicit_multi_group) {
      ++explicit_multi_threads;
    }
  }
  const std::uint64_t explicit_multi_flags =
      BM_REQUEST_FLAG_MULTI_GROUP_EXPLICIT |
      (static_cast<std::uint64_t>(explicit_multi_group)
       << BM_REQUEST_MULTI_GROUP_SHIFT);

  bm_engine_handle engine = nullptr;
  if (bm_engine_create(&engine) != BM_STATUS_OK || engine == nullptr) {
    return Fail("engine creation failed");
  }
  if (!HasValidFfiErrorContract(engine)) {
    return Fail("FFI error contract failed", engine);
  }

  bm_snapshot_v4 snapshot{};
  std::uint64_t run_id = 0;

  const std::uint64_t invalid_single_flags =
      BM_REQUEST_FLAG_SINGLE_CPU_EXPLICIT |
      (0xFFULL << BM_REQUEST_SINGLE_CPU_SHIFT);
  if (!StartRun(engine, BM_TEST_CPU_SINGLE, 100, 20, &run_id,
                invalid_single_flags) ||
      !WaitForTerminal(engine, &snapshot) ||
      snapshot.state != BM_STATE_COMPLETED || snapshot.thread_count != 1 ||
      snapshot.selected_cpu != topology.preferred_single_cpu ||
      !HasValidScoreSummary(snapshot) ||
      (snapshot.quality_flags & BM_QUALITY_SELECTION_FALLBACK) == 0 ||
      !HasConsistentAffinityStatus(snapshot, 1) ||
      !HasConsistentPerformanceRequestStatus(snapshot)) {
    return Fail("invalid single CPU fallback failed", engine);
  }
  PrintResult("single-fallback", snapshot);

  const std::uint64_t invalid_multi_flags =
      BM_REQUEST_FLAG_MULTI_GROUP_EXPLICIT |
      (0xFFULL << BM_REQUEST_MULTI_GROUP_SHIFT);
  if (!StartRun(engine, BM_TEST_CPU_MULTI, 100, 20, &run_id,
                invalid_multi_flags) ||
      !WaitForTerminal(engine, &snapshot) ||
      snapshot.state != BM_STATE_COMPLETED || snapshot.thread_count == 0 ||
      snapshot.peak_frequency_group_count != topology.performance_group_count ||
      !HasValidScoreSummary(snapshot) ||
      (snapshot.quality_flags & BM_QUALITY_SELECTION_FALLBACK) == 0 ||
      !HasConsistentAffinityStatus(snapshot, topology.allowed_count) ||
      !HasConsistentPerformanceRequestStatus(snapshot)) {
    return Fail("invalid multi group fallback failed", engine);
  }
  PrintResult("multi-fallback", snapshot);

  if (!StartRun(engine, BM_TEST_CPU_SINGLE, single_duration, single_warmup,
                &run_id, explicit_single_flags) ||
      !WaitForTerminal(engine, &snapshot) ||
      snapshot.state != BM_STATE_COMPLETED || snapshot.thread_count != 1 ||
      snapshot.selected_cpu != static_cast<std::int32_t>(explicit_single_cpu) ||
      !HasValidScoreSummary(snapshot) || snapshot.completed_work == 0 ||
      snapshot.progress != 1.0 || !HasConsistentAffinityStatus(snapshot, 1) ||
      !HasConsistentPerformanceRequestStatus(snapshot)) {
    return Fail("single-thread run failed", engine);
  }
  PrintResult("single", snapshot);

  if (!StartRun(engine, BM_TEST_CPU_MULTI, multi_duration, multi_warmup,
                &run_id, explicit_multi_flags) ||
      !WaitForTerminal(engine, &snapshot) ||
      snapshot.state != BM_STATE_COMPLETED ||
      snapshot.thread_count != explicit_multi_threads ||
      !HasValidScoreSummary(snapshot) || snapshot.completed_work == 0 ||
      snapshot.progress != 1.0 ||
      !HasConsistentAffinityStatus(snapshot, explicit_multi_threads) ||
      !HasConsistentPerformanceRequestStatus(snapshot)) {
    return Fail("multi-thread run failed", engine);
  }
  PrintResult("multi", snapshot);

  if (!StartRun(engine, BM_TEST_CPU_MULTI, 2000, 50, &run_id)) {
    return Fail("cancel run start failed", engine);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  if (bm_request_stop(engine, run_id) != BM_STATUS_OK ||
      !WaitForTerminal(engine, &snapshot) ||
      snapshot.state != BM_STATE_CANCELLED) {
    return Fail("cancel path failed", engine);
  }

  if (bm_engine_destroy(engine) != BM_STATUS_OK) {
    return Fail("engine destruction failed");
  }

  std::cout << "Phase 2 native smoke test passed\n";
  return 0;
}

