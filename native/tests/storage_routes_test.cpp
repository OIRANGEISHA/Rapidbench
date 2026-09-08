#include "storage_ffi.h"
#include "benchmark/storage_execution.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace {
bool Wait(bm_storage_engine_handle engine, bm_storage_snapshot_v2 &snapshot) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
  do {
    snapshot = {};
    snapshot.struct_size = sizeof(snapshot);
    snapshot.abi_version = BM_STORAGE_ABI_VERSION;
    if (bm_storage_get_snapshot(engine, &snapshot) != 0) return false;
    if (snapshot.state >= 6U) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } while (std::chrono::steady_clock::now() < deadline);
  return false;
}
}

int main() {
  using benchmark::StorageTest;
  using benchmark::StorageIoMode;
  using benchmark::detail::SelectStorageExecution;
  using benchmark::detail::StorageExecution;
  // Simulate the fallback route even when the test phone supports Direct I/O.
  for (const auto test : {StorageTest::kSequentialRead, StorageTest::kSequentialWrite}) {
    if (SelectStorageExecution(test, StorageIoMode::kDirect) != StorageExecution::kAio ||
        SelectStorageExecution(test, StorageIoMode::kBufferedCompatibility) != StorageExecution::kSync) return 4;
  }
  for (const auto test : {StorageTest::kRandom4KQ8T1Read, StorageTest::kRandom4KQ8T1Write}) {
    if (SelectStorageExecution(test, StorageIoMode::kBufferedCompatibility) != StorageExecution::kUnavailable) return 5;
  }
  std::puts("STORAGE_FALLBACK simulated sequential Q1T1 and unavailable QD8 passed");
  const auto directory = std::filesystem::current_path() / "beta5_storage_routes_data";
  if (!std::filesystem::create_directory(directory)) return 1;
  bm_storage_engine_handle engine = nullptr;
  if (bm_storage_engine_create(directory.string().c_str(), &engine) != 0) return 2;
  bool ok = true;
  for (std::uint32_t id = 1; id <= BM_STORAGE_RESULT_COUNT && ok; ++id) {
    bm_storage_request_v1 request{};
    request.struct_size = sizeof(request);
    request.abi_version = BM_STORAGE_ABI_VERSION;
    request.test_id = id;
    request.duration_ms = 250;
    request.warmup_ms = 50;
    request.file_size_mib_override = 16;
    std::uint64_t run = 0;
    bm_storage_snapshot_v2 snapshot{};
    ok = bm_storage_start(engine, &request, &run) == 0 && Wait(engine, snapshot);
    if (!ok) break;
    if (std::filesystem::exists(directory / "storage_bench.dat")) {
      std::fprintf(stderr, "Scratch file still exists at terminal state for test %u\n", id);
      ok = false;
      break;
    }
    const auto &result = snapshot.results[id - 1];
    const bool q8_unavailable = (id == 5 || id == 6) && result.io_mode == 2;
    if (q8_unavailable) {
      ok = result.valid == 0 && result.error_code == -37 && snapshot.state == 8;
    } else {
      ok = snapshot.state == 6 && result.test_id == id && result.valid != 0 &&
           result.error_code == 0 && result.elapsed_ns > 0;
      if (id <= 8) {
        const auto expected_qd = (id <= 2 && result.io_mode == 1) || id == 5 || id == 6 ? 8U : 1U;
        const auto expected_threads = id == 7 || id == 8 ? 4U : 1U;
        ok = ok && result.queue_depth == expected_qd &&
             result.thread_count == expected_threads && result.completed_bytes > 0 &&
             result.completed_io > 0 && result.mbps > 0;
        if (expected_qd == 8) ok = ok && result.max_outstanding == 8;
      } else {
        ok = ok && result.completed_rows > 0 && result.rows_per_second > 0;
      }
    }
    std::printf("STORAGE_ROUTE id=%u mode=%u qd=%u threads=%u max=%u valid=%u error=%d pass=%d\n",
                id, result.io_mode, result.queue_depth, result.thread_count,
                result.max_outstanding, result.valid, result.error_code, ok);
  }
  bm_storage_engine_destroy(engine);
  std::filesystem::remove_all(directory);
  return ok ? 0 : 3;
}
