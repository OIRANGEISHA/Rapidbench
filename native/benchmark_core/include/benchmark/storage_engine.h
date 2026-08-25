#ifndef RAPIDBENCH_STORAGE_ENGINE_H_
#define RAPIDBENCH_STORAGE_ENGINE_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace benchmark {

constexpr std::size_t kStorageResultCount = 10U;

enum class StorageTest : std::uint32_t {
  kNone = 0,
  kSequentialRead = 1,
  kSequentialWrite = 2,
  kRandom4KQ1T1Read = 3,
  kRandom4KQ1T1Write = 4,
  kRandom4KQ8T1Read = 5,
  kRandom4KQ8T1Write = 6,
  kRandom4KQ1T4Read = 7,
  kRandom4KQ1T4Write = 8,
  kSqliteInsert = 9,
  kSqliteDelete = 10,
  kAll = 11,
};

enum class StorageState : std::uint32_t {
  kIdle = 0,
  kPreparing = 1,
  kWarmingUp = 2,
  kMeasuring = 3,
  kFlushing = 4,
  kStopping = 5,
  kCompleted = 6,
  kStopped = 7,
  kError = 8,
};

enum class StoragePhase : std::uint32_t {
  kNone = 0,
  kPrepare = 1,
  kWarmUp = 2,
  kMeasure = 3,
  kFlush = 4,
};

enum class StorageIoMode : std::uint32_t {
  kUnavailable = 0,
  kDirect = 1,
  kBufferedCompatibility = 2,
  kSqlite = 3,
};

struct StorageRequest {
  StorageTest test = StorageTest::kNone;
  std::uint32_t duration_ms = 3000;
  std::uint32_t warmup_ms = 750;
  std::uint32_t file_size_mib_override = 0;
};

struct StorageResult {
  StorageTest test = StorageTest::kNone;
  bool valid = false;
  bool stopped = false;
  StorageIoMode io_mode = StorageIoMode::kUnavailable;
  std::int32_t error_code = 0;
  std::uint32_t thread_count = 0;
  std::uint32_t queue_depth = 0;
  std::uint32_t max_outstanding = 0;
  std::uint64_t elapsed_ns = 0;
  std::uint64_t completed_bytes = 0;
  std::uint64_t completed_io = 0;
  std::uint64_t completed_rows = 0;
  double mbps = 0.0;
  double iops = 0.0;
  double rows_per_second = 0.0;
};

struct StorageSnapshot {
  std::uint64_t run_id = 0;
  StorageState state = StorageState::kIdle;
  StoragePhase phase = StoragePhase::kNone;
  StorageTest active_test = StorageTest::kNone;
  StorageIoMode io_mode = StorageIoMode::kUnavailable;
  std::int32_t error_code = 0;
  std::uint32_t alignment = 0;
  std::uint32_t test_file_mib = 0;
  std::uint32_t block_size = 0;
  std::uint32_t queue_depth = 0;
  std::uint32_t thread_count = 0;
  std::uint32_t current_outstanding = 0;
  std::uint32_t max_outstanding = 0;
  std::uint64_t elapsed_ns = 0;
  std::uint64_t completed_bytes = 0;
  std::uint64_t completed_io = 0;
  std::uint64_t completed_rows = 0;
  double current_mbps = 0.0;
  double current_iops = 0.0;
  double current_rows_per_second = 0.0;
  double progress = 0.0;
  std::array<StorageResult, kStorageResultCount> results{};
};

class StorageEngine final {
public:
  explicit StorageEngine(std::string directory);
  ~StorageEngine();

  StorageEngine(const StorageEngine &) = delete;
  StorageEngine &operator=(const StorageEngine &) = delete;

  std::int32_t Start(const StorageRequest &request, std::uint64_t *out_run_id);
  std::int32_t RequestStop(std::uint64_t run_id);
  StorageSnapshot GetSnapshot() const;

private:
  void Run(StorageRequest request, std::uint64_t run_id);
  void Publish(const StorageSnapshot &snapshot);

  const std::string directory_;
  mutable std::mutex mutex_;
  StorageSnapshot snapshot_;
  std::thread coordinator_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<std::uint64_t> next_run_id_{1};
};

} // namespace benchmark

#endif // RAPIDBENCH_STORAGE_ENGINE_H_
