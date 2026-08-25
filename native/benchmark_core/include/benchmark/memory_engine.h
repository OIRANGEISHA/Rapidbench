#ifndef RAPIDBENCH_MEMORY_ENGINE_H_
#define RAPIDBENCH_MEMORY_ENGINE_H_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace benchmark {

enum class MemoryTest : std::uint32_t {
  kNone = 0,
  kRead = 1,
  kWrite = 2,
  kCopy = 3,
};

struct MemoryRequest {
  MemoryTest test = MemoryTest::kNone;
  std::uint32_t duration_ms = 3000;
  std::uint32_t warmup_ms = 750;
};

struct MemorySnapshot {
  std::uint64_t run_id = 0;
  std::uint32_t state = 0;
  MemoryTest test = MemoryTest::kNone;
  std::uint32_t thread_count = 0;
  std::int32_t error_code = 0;
  std::uint32_t affinity_failures = 0;
  std::uint32_t performance_request_failures = 0;
  std::uint64_t buffer_bytes = 0;
  std::uint64_t elapsed_ns = 0;
  std::uint64_t processed_bytes = 0;
  double bandwidth_gbps = 0.0;
  double progress = 0.0;
};

class MemoryEngine {
public:
  MemoryEngine();
  ~MemoryEngine();

  MemoryEngine(const MemoryEngine &) = delete;
  MemoryEngine &operator=(const MemoryEngine &) = delete;

  std::int32_t Start(const MemoryRequest &request, std::uint64_t *out_run_id);
  std::int32_t RequestStop(std::uint64_t run_id);
  MemorySnapshot GetSnapshot() const;

private:
  void Run(MemoryRequest request, std::uint64_t run_id);
  void Publish(const MemorySnapshot &snapshot);

  mutable std::mutex mutex_;
  MemorySnapshot snapshot_;
  std::thread coordinator_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<std::uint64_t> next_run_id_{1};
};

} // namespace benchmark

#endif // RAPIDBENCH_MEMORY_ENGINE_H_
