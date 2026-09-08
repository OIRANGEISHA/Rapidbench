#ifndef RAPIDBENCH_STORAGE_EXECUTION_H_
#define RAPIDBENCH_STORAGE_EXECUTION_H_

#include "benchmark/storage_engine.h"

namespace benchmark::detail {
enum class StorageExecution { kSync, kAio, kThreaded, kUnavailable };

// Keep advertised queue depth honest when Direct I/O is unavailable.
inline StorageExecution SelectStorageExecution(StorageTest test, StorageIoMode mode) {
  const bool sequential = test == StorageTest::kSequentialRead ||
                          test == StorageTest::kSequentialWrite;
  const bool q8 = test == StorageTest::kRandom4KQ8T1Read ||
                  test == StorageTest::kRandom4KQ8T1Write;
  if (sequential || q8) {
    if (mode == StorageIoMode::kDirect) return StorageExecution::kAio;
    return sequential ? StorageExecution::kSync : StorageExecution::kUnavailable;
  }
  if (test == StorageTest::kRandom4KQ1T4Read ||
      test == StorageTest::kRandom4KQ1T4Write) return StorageExecution::kThreaded;
  return StorageExecution::kSync;
}
} // namespace benchmark::detail
#endif
