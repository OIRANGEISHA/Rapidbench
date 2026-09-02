#include "benchmark_ffi.h"
#include "storage_ffi.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

namespace {

constexpr std::uint32_t kStorageStateCompleted = 6U;
constexpr std::uint32_t kStorageStateStopped = 7U;
constexpr std::uint32_t kStorageStateError = 8U;

bool ReadSnapshot(bm_storage_engine_handle engine,
                  bm_storage_snapshot_v2 *snapshot) {
  std::memset(snapshot, 0, sizeof(*snapshot));
  snapshot->struct_size = sizeof(*snapshot);
  snapshot->abi_version = BM_STORAGE_ABI_VERSION;
  return bm_storage_get_snapshot(engine, snapshot) == BM_STATUS_OK;
}

bool WaitForTerminal(bm_storage_engine_handle engine,
                     bm_storage_snapshot_v2 *snapshot,
                     std::chrono::seconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!ReadSnapshot(engine, snapshot)) {
      return false;
    }
    if (snapshot->state == kStorageStateCompleted ||
        snapshot->state == kStorageStateStopped ||
        snapshot->state == kStorageStateError) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

} // namespace

int main() {
  const std::filesystem::path directory =
      std::filesystem::current_path() / "storage_sqlite_test_data";
  std::error_code filesystem_error;
  std::filesystem::remove_all(directory, filesystem_error);
  filesystem_error.clear();
  if (!std::filesystem::create_directories(directory, filesystem_error) ||
      filesystem_error) {
    std::fprintf(stderr, "storage_test_directory_failed\n");
    return 1;
  }

  bm_storage_engine_handle engine = nullptr;
  const std::string directory_string = directory.string();
  if (bm_storage_engine_create(directory_string.c_str(), &engine) !=
          BM_STATUS_OK ||
      engine == nullptr) {
    std::filesystem::remove_all(directory, filesystem_error);
    std::fprintf(stderr, "storage_engine_create_failed\n");
    return 2;
  }

  bm_storage_request_v1 request{};
  request.struct_size = sizeof(request);
  request.abi_version = BM_STORAGE_ABI_VERSION;
  request.test_id = BM_STORAGE_TEST_SQLITE_UPDATE;
  request.duration_ms = 300U;
  request.warmup_ms = 0U;
  request.file_size_mib_override = 16U;

  std::uint64_t run_id = 0U;
  bm_storage_snapshot_v2 snapshot{};
  const bool started =
      bm_storage_start(engine, &request, &run_id) == BM_STATUS_OK;
  const bool finished =
      started && WaitForTerminal(engine, &snapshot, std::chrono::seconds(45));

  const std::size_t result_index = BM_STORAGE_TEST_SQLITE_UPDATE - 1U;
  const bm_storage_result_v1 &result = snapshot.results[result_index];
  const bool valid = finished && snapshot.state == kStorageStateCompleted &&
                     snapshot.error_code == 0 &&
                     snapshot.result_count == BM_STORAGE_RESULT_COUNT &&
                     result.test_id == BM_STORAGE_TEST_SQLITE_UPDATE &&
                     result.valid != 0U && result.error_code == 0 &&
                     result.completed_rows > 0U && result.rows_per_second > 0.0;

  std::printf(
      "SQLITE_UPDATE state=%u resultCount=%u valid=%u rows=%llu qps=%.3f "
      "error=%d\n",
      snapshot.state, snapshot.result_count, result.valid,
      static_cast<unsigned long long>(result.completed_rows),
      result.rows_per_second, result.error_code);

  bm_storage_engine_destroy(engine);
  std::filesystem::remove_all(directory, filesystem_error);
  return valid ? 0 : 3;
}
