#include "benchmark/storage_engine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/aio_abi.h>
#include <linux/stat.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#endif
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "benchmark/performance_hint.h"
#include "sqlite3.h"

namespace benchmark {
namespace {

constexpr std::int32_t kStatusOk = 0;
constexpr std::int32_t kStatusInvalidArgument = -1;
constexpr std::int32_t kStatusBusy = -2;
constexpr std::int32_t kStatusInternalError = -3;
constexpr std::int32_t kErrorInsufficientStorage = -30;
constexpr std::int32_t kErrorDirectory = -31;
constexpr std::int32_t kErrorOpen = -32;
constexpr std::int32_t kErrorAllocation = -35;
constexpr std::int32_t kErrorIo = -36;
constexpr std::int32_t kErrorAio = -37;
constexpr std::int32_t kErrorSqlite = -38;
constexpr std::int32_t kErrorThread = -39;

constexpr std::size_t kKiB = 1024ULL;
constexpr std::size_t kMiB = 1024ULL * kKiB;
constexpr std::size_t kSequentialBlock = kMiB;
constexpr std::size_t kRandomBlock = 4ULL * kKiB;
constexpr std::uint32_t kRandomQueueDepth = 8U;
constexpr std::uint32_t kRandomThreadCount = 4U;
constexpr std::uint32_t kSqliteTransactionRows = 500U;
constexpr std::uint32_t kSqliteInsertLimit = 100000U;
constexpr std::uint32_t kSqliteDeleteLimit = 50000U;
constexpr std::size_t kSqlitePayloadBytes = 512U;
constexpr std::uint64_t kPublishIntervalNs = 100000000ULL;

std::uint64_t NowNs() {
#if defined(__linux__)
  timespec value{};
  clock_gettime(CLOCK_MONOTONIC_RAW, &value);
  return static_cast<std::uint64_t>(value.tv_sec) * 1000000000ULL +
         static_cast<std::uint64_t>(value.tv_nsec);
#else
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
#endif
}

bool IsTerminal(StorageState state) {
  return state == StorageState::kIdle || state == StorageState::kCompleted ||
         state == StorageState::kStopped || state == StorageState::kError;
}

bool IsRawTest(StorageTest test) {
  return test >= StorageTest::kSequentialRead &&
         test <= StorageTest::kRandom4KQ1T4Write;
}

bool IsWriteTest(StorageTest test) {
  return test == StorageTest::kSequentialWrite ||
         test == StorageTest::kRandom4KQ1T1Write ||
         test == StorageTest::kRandom4KQ8T1Write ||
         test == StorageTest::kRandom4KQ1T4Write;
}

bool IsRandomTest(StorageTest test) {
  return test >= StorageTest::kRandom4KQ1T1Read &&
         test <= StorageTest::kRandom4KQ1T4Write;
}

std::size_t ResultIndex(StorageTest test) {
  return static_cast<std::size_t>(test) - 1U;
}

std::uint32_t ChooseFileMiB(const std::string &directory,
                            std::uint32_t override_mib) {
  if (override_mib != 0U) {
    return std::clamp(override_mib, 16U, 512U);
  }
#if defined(__linux__)
  struct statvfs info {};
  if (statvfs(directory.c_str(), &info) != 0) {
    return 0U;
  }
  const std::uint64_t free_bytes =
      static_cast<std::uint64_t>(info.f_bavail) * info.f_frsize;
  if (free_bytes >= 2ULL * 1024ULL * kMiB) {
    return 512U;
  }
  if (free_bytes >= 1ULL * 1024ULL * kMiB) {
    return 256U;
  }
  if (free_bytes >= 512ULL * kMiB) {
    return 128U;
  }
#endif
  return 0U;
}

void FillPattern(std::uint8_t *data, std::size_t size, std::uint64_t seed) {
  std::uint64_t x = seed;
  for (std::size_t offset = 0U; offset < size; offset += sizeof(x)) {
    x ^= x << 13U;
    x ^= x >> 7U;
    x ^= x << 17U;
    const std::size_t count = std::min(sizeof(x), size - offset);
    std::memcpy(data + offset, &x, count);
  }
}

class AlignedMemory final {
public:
  AlignedMemory() = default;
  ~AlignedMemory() { std::free(data_); }
  AlignedMemory(const AlignedMemory &) = delete;
  AlignedMemory &operator=(const AlignedMemory &) = delete;

  bool Allocate(std::size_t alignment, std::size_t size) {
    std::free(data_);
    data_ = nullptr;
    if (alignment < sizeof(void *) || (alignment & (alignment - 1U)) != 0U) {
      return false;
    }
    return posix_memalign(reinterpret_cast<void **>(&data_), alignment, size) ==
               0 &&
           data_ != nullptr;
  }
  std::uint8_t *get() const { return data_; }

private:
  std::uint8_t *data_ = nullptr;
};

struct RawFile final {
  int fd = -1;
  std::string path;
  std::uint64_t size = 0U;
  std::uint32_t alignment = 4096U;
  StorageIoMode mode = StorageIoMode::kUnavailable;
  AlignedMemory sequential;
  AlignedMemory random;

  ~RawFile() {
#if defined(__linux__)
    if (fd >= 0) {
      close(fd);
    }
    if (!path.empty()) {
      unlink(path.c_str());
    }
#endif
  }
};

struct RunContext final {
  StorageSnapshot snapshot{};
  std::atomic<bool> &stop;
  std::function<void(const StorageSnapshot &)> publish;
  std::uint64_t last_publish_ns = 0U;

  bool StopRequested() const {
    return stop.load(std::memory_order_acquire);
  }

  void SetState(StorageState state, StoragePhase phase) {
    snapshot.state = state;
    snapshot.phase = phase;
    publish(snapshot);
  }

  void UpdateProgress(std::uint64_t start_ns, std::uint64_t duration_ns,
                      std::uint64_t bytes, std::uint64_t io,
                      std::uint64_t rows, bool force = false) {
    const std::uint64_t now = NowNs();
    if (!force && now - last_publish_ns < kPublishIntervalNs) {
      return;
    }
    const std::uint64_t elapsed = now > start_ns ? now - start_ns : 0U;
    snapshot.elapsed_ns = elapsed;
    snapshot.completed_bytes = bytes;
    snapshot.completed_io = io;
    snapshot.completed_rows = rows;
    const double seconds = static_cast<double>(elapsed) / 1.0e9;
    snapshot.current_mbps =
        seconds > 0.0 ? static_cast<double>(bytes) / 1.0e6 / seconds : 0.0;
    snapshot.current_iops =
        seconds > 0.0 ? static_cast<double>(io) / seconds : 0.0;
    snapshot.current_rows_per_second =
        seconds > 0.0 ? static_cast<double>(rows) / seconds : 0.0;
    snapshot.progress = duration_ns == 0U
                            ? 0.0
                            : std::min(1.0, static_cast<double>(elapsed) /
                                                static_cast<double>(duration_ns));
    last_publish_ns = now;
    publish(snapshot);
  }
};

StorageResult MakeResult(const RunContext &context, StorageTest test,
                         bool valid, bool stopped, std::int32_t error,
                         std::uint64_t elapsed, std::uint64_t bytes,
                         std::uint64_t io, std::uint64_t rows) {
  StorageResult result{};
  result.test = test;
  result.valid = valid;
  result.stopped = stopped;
  result.io_mode = context.snapshot.io_mode;
  result.error_code = error;
  result.thread_count = context.snapshot.thread_count;
  result.queue_depth = context.snapshot.queue_depth;
  result.max_outstanding = context.snapshot.max_outstanding;
  result.elapsed_ns = elapsed;
  result.completed_bytes = bytes;
  result.completed_io = io;
  result.completed_rows = rows;
  const double seconds = static_cast<double>(elapsed) / 1.0e9;
  if (seconds > 0.0) {
    result.mbps = static_cast<double>(bytes) / 1.0e6 / seconds;
    result.iops = static_cast<double>(io) / seconds;
    result.rows_per_second = static_cast<double>(rows) / seconds;
  }
  return result;
}

std::uint32_t DetectDirectAlignment(int fd) {
  std::uint32_t alignment = 4096U;
#if defined(__linux__) && defined(SYS_statx) && defined(STATX_DIOALIGN)
  struct statx info {};
  if (syscall(SYS_statx, fd, "", AT_EMPTY_PATH, STATX_DIOALIGN, &info) == 0) {
    const std::uint32_t memory_alignment = info.stx_dio_mem_align;
    const std::uint32_t offset_alignment = info.stx_dio_offset_align;
    alignment = std::max({alignment, memory_alignment, offset_alignment});
  }
#else
  (void)fd;
#endif
  if ((alignment & (alignment - 1U)) != 0U) {
    return 0U;
  }
  return alignment;
}

bool OpenAndPrepareRawFile(RunContext &context, const StorageRequest &request,
                           RawFile *file, std::int32_t *error) {
#if !defined(__linux__)
  (void)context;
  (void)request;
  (void)file;
  *error = kErrorOpen;
  return false;
#else
  std::error_code fs_error;
  std::filesystem::create_directories(file->path.substr(0, file->path.find_last_of('/')),
                                      fs_error);
  if (fs_error) {
    *error = kErrorDirectory;
    return false;
  }
  const std::uint32_t file_mib = ChooseFileMiB(
      file->path.substr(0, file->path.find_last_of('/')), request.file_size_mib_override);
  if (file_mib == 0U) {
    *error = kErrorInsufficientStorage;
    return false;
  }
  file->size = static_cast<std::uint64_t>(file_mib) * kMiB;
  context.snapshot.test_file_mib = file_mib;
  context.snapshot.block_size = static_cast<std::uint32_t>(kSequentialBlock);

  int flags = O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC;
#ifdef O_DIRECT
  file->fd = open(file->path.c_str(), flags | O_DIRECT, 0600);
#endif
  if (file->fd >= 0) {
    file->alignment = DetectDirectAlignment(file->fd);
    if (file->alignment == 0U || file->alignment > kRandomBlock ||
        kRandomBlock % file->alignment != 0U) {
      close(file->fd);
      file->fd = -1;
    } else {
      file->mode = StorageIoMode::kDirect;
    }
  }
  if (file->fd < 0) {
    file->fd = open(file->path.c_str(), flags, 0600);
    file->alignment = 4096U;
    file->mode = StorageIoMode::kBufferedCompatibility;
  }
  if (file->fd < 0) {
    *error = kErrorOpen;
    return false;
  }
  if (!file->sequential.Allocate(
          file->alignment, kSequentialBlock * kRandomQueueDepth) ||
      !file->random.Allocate(file->alignment,
                             kRandomBlock * kRandomQueueDepth)) {
    *error = kErrorAllocation;
    return false;
  }
  FillPattern(file->sequential.get(),
              kSequentialBlock * kRandomQueueDepth,
              0xA51C7E9D37B264F1ULL);
  FillPattern(file->random.get(), kRandomBlock * kRandomQueueDepth,
              0x9E3779B97F4A7C15ULL);
  if (ftruncate(file->fd, static_cast<off_t>(file->size)) != 0) {
    *error = kErrorIo;
    return false;
  }
  context.snapshot.io_mode = file->mode;
  context.snapshot.alignment = file->alignment;
  context.SetState(StorageState::kPreparing, StoragePhase::kPrepare);

  for (std::uint64_t offset = 0U; offset < file->size;
       offset += kSequentialBlock) {
    if (context.StopRequested()) {
      return false;
    }
    const ssize_t written = pwrite(file->fd, file->sequential.get(),
                                   kSequentialBlock,
                                   static_cast<off_t>(offset));
    if (written != static_cast<ssize_t>(kSequentialBlock)) {
      if (file->mode == StorageIoMode::kDirect &&
          (errno == EINVAL || errno == EOPNOTSUPP || errno == ENOTSUP)) {
        close(file->fd);
        file->fd = open(file->path.c_str(), flags, 0600);
        file->mode = StorageIoMode::kBufferedCompatibility;
        file->alignment = 4096U;
        context.snapshot.io_mode = file->mode;
        context.snapshot.alignment = file->alignment;
        if (file->fd < 0 ||
            ftruncate(file->fd, static_cast<off_t>(file->size)) != 0) {
          *error = kErrorOpen;
          return false;
        }
        offset = static_cast<std::uint64_t>(-kSequentialBlock);
        continue;
      }
      *error = kErrorIo;
      return false;
    }
  }
  if (fdatasync(file->fd) != 0) {
    *error = kErrorIo;
    return false;
  }
  if (file->mode == StorageIoMode::kBufferedCompatibility) {
    posix_fadvise(file->fd, 0, 0, POSIX_FADV_DONTNEED);
  }
#if defined(__ANDROID__)
  __android_log_print(
      ANDROID_LOG_INFO, "RapidBenchStorage",
      "prepared file=%uMiB mode=%s alignment=%u path=%s", file_mib,
      file->mode == StorageIoMode::kDirect ? "DIRECT"
                                           : "BUFFERED_COMPATIBILITY",
      file->alignment, file->path.c_str());
#else
  std::fprintf(stderr,
               "RapidBench storage prepared: file=%u MiB mode=%s align=%u\n",
               file_mib,
               file->mode == StorageIoMode::kDirect ? "direct" : "buffered",
               file->alignment);
#endif
  return true;
#endif
}

std::vector<std::uint64_t> MakeRandomOffsets(std::uint64_t file_size,
                                             std::uint64_t seed,
                                             std::uint64_t begin = 0U,
                                             std::uint64_t end = 0U) {
  if (end == 0U || end > file_size) {
    end = file_size;
  }
  begin = (begin / kRandomBlock) * kRandomBlock;
  end = (end / kRandomBlock) * kRandomBlock;
  std::vector<std::uint64_t> offsets;
  offsets.reserve(static_cast<std::size_t>((end - begin) / kRandomBlock));
  for (std::uint64_t offset = begin; offset + kRandomBlock <= end;
       offset += kRandomBlock) {
    offsets.push_back(offset);
  }
  std::mt19937_64 random(seed);
  std::shuffle(offsets.begin(), offsets.end(), random);
  return offsets;
}

bool DoSyncIo(int fd, bool write, void *buffer, std::size_t size,
              std::uint64_t offset) {
#if defined(__linux__)
  const ssize_t result = write
                             ? pwrite(fd, buffer, size, static_cast<off_t>(offset))
                             : pread(fd, buffer, size, static_cast<off_t>(offset));
  return result == static_cast<ssize_t>(size);
#else
  (void)fd;
  (void)write;
  (void)buffer;
  (void)size;
  (void)offset;
  return false;
#endif
}

StorageResult RunSyncTest(RunContext &context, RawFile &file,
                          const StorageRequest &request, StorageTest test) {
  const bool write = IsWriteTest(test);
  const bool random = IsRandomTest(test);
  const std::size_t block = random ? kRandomBlock : kSequentialBlock;
  std::vector<std::uint64_t> offsets =
      random ? MakeRandomOffsets(file.size, 0xC6BC279692B5CC83ULL)
             : std::vector<std::uint64_t>{};
  std::size_t cursor = 0U;
  std::uint64_t sequential_offset = 0U;
  void *buffer = random ? static_cast<void *>(file.random.get())
                        : static_cast<void *>(file.sequential.get());
  context.snapshot.active_test = test;
  context.snapshot.io_mode = file.mode;
  context.snapshot.block_size = static_cast<std::uint32_t>(block);
  context.snapshot.queue_depth = 1U;
  context.snapshot.thread_count = 1U;
  context.snapshot.max_outstanding = 1U;
  context.snapshot.current_outstanding = 1U;
#if defined(__linux__)
  PerformanceHintSession hints(
      std::vector<std::int32_t>{
          static_cast<std::int32_t>(syscall(SYS_gettid))},
      1000000);
  hints.RequestMaximumPerformance();
#endif
  context.SetState(StorageState::kWarmingUp, StoragePhase::kWarmUp);

  const std::uint64_t warm_end =
      NowNs() + static_cast<std::uint64_t>(request.warmup_ms) * 1000000ULL;
  while (NowNs() < warm_end && !context.StopRequested()) {
    const std::uint64_t offset = random ? offsets[cursor++ % offsets.size()]
                                        : sequential_offset;
    if (!DoSyncIo(file.fd, write, buffer, block, offset)) {
      return MakeResult(context, test, false, false, kErrorIo, 0, 0, 0, 0);
    }
    if (!random) {
      sequential_offset = (sequential_offset + block) % file.size;
    }
  }
  if (context.StopRequested()) {
    return MakeResult(context, test, false, true, 0, 0, 0, 0, 0);
  }
  if (!write && file.mode == StorageIoMode::kBufferedCompatibility) {
    posix_fadvise(file.fd, 0, 0, POSIX_FADV_DONTNEED);
  }

  cursor = 0U;
  sequential_offset = 0U;
  const std::uint64_t duration_ns =
      static_cast<std::uint64_t>(request.duration_ms) * 1000000ULL;
  const std::uint64_t start = NowNs();
  const std::uint64_t deadline = start + duration_ns;
  std::uint64_t bytes = 0U;
  std::uint64_t io = 0U;
  context.SetState(StorageState::kMeasuring, StoragePhase::kMeasure);
  while (NowNs() < deadline && !context.StopRequested()) {
    const std::uint64_t offset = random ? offsets[cursor++ % offsets.size()]
                                        : sequential_offset;
    if (!DoSyncIo(file.fd, write, buffer, block, offset)) {
      const std::uint64_t elapsed = NowNs() - start;
      return MakeResult(context, test, false, false, kErrorIo, elapsed, bytes,
                        io, 0);
    }
    bytes += block;
    ++io;
    if (!random) {
      sequential_offset = (sequential_offset + block) % file.size;
    }
    context.UpdateProgress(start, duration_ns, bytes, io, 0);
  }
  const std::uint64_t elapsed = NowNs() - start;
  const bool stopped = context.StopRequested();
  context.snapshot.current_outstanding = 0U;
  context.UpdateProgress(start, duration_ns, bytes, io, 0, true);
  if (write) {
    context.SetState(StorageState::kFlushing, StoragePhase::kFlush);
    if (fdatasync(file.fd) != 0) {
      return MakeResult(context, test, false, stopped, kErrorIo, elapsed,
                        bytes, io, 0);
    }
  }
  return MakeResult(context, test, io > 0U, stopped, 0, elapsed, bytes, io,
                    0);
}

#if defined(__linux__)
int AioSetup(unsigned events, aio_context_t *context) {
  return static_cast<int>(syscall(__NR_io_setup, events, context));
}
int AioDestroy(aio_context_t context) {
  return static_cast<int>(syscall(__NR_io_destroy, context));
}
int AioSubmit(aio_context_t context, long count, iocb **requests) {
  return static_cast<int>(syscall(__NR_io_submit, context, count, requests));
}
int AioGetEvents(aio_context_t context, long minimum, long maximum,
                 io_event *events, timespec *timeout) {
  return static_cast<int>(
      syscall(__NR_io_getevents, context, minimum, maximum, events, timeout));
}
#endif

StorageResult RunAioTest(RunContext &context, RawFile &file,
                         const StorageRequest &request, StorageTest test) {
  const bool random = IsRandomTest(test);
  const std::size_t block = random ? kRandomBlock : kSequentialBlock;
  context.snapshot.active_test = test;
  context.snapshot.block_size = static_cast<std::uint32_t>(block);
  context.snapshot.queue_depth = kRandomQueueDepth;
  context.snapshot.thread_count = 1U;
  context.snapshot.max_outstanding = 0U;
  context.snapshot.io_mode = file.mode;
  if (file.mode != StorageIoMode::kDirect) {
    return MakeResult(context, test, false, false, kErrorAio, 0, 0, 0, 0);
  }
#if !defined(__linux__)
  return MakeResult(context, test, false, false, kErrorAio, 0, 0, 0, 0);
#else
  PerformanceHintSession hints(
      std::vector<std::int32_t>{
          static_cast<std::int32_t>(syscall(SYS_gettid))},
      1000000);
  hints.RequestMaximumPerformance();
  const bool write = IsWriteTest(test);
  const auto offsets = random
                           ? MakeRandomOffsets(file.size,
                                               0xD1B54A32D192ED03ULL)
                           : std::vector<std::uint64_t>{};
  std::size_t cursor = 0U;
  std::uint64_t sequential_offset = 0U;
  std::uint8_t *const buffers =
      random ? file.random.get() : file.sequential.get();

  auto phase = [&](std::uint64_t duration_ns, bool measuring,
                   std::uint64_t *bytes, std::uint64_t *io,
                   std::uint64_t *elapsed, std::int32_t *phase_error) {
    aio_context_t aio = 0;
    if (AioSetup(kRandomQueueDepth, &aio) != 0) {
      *phase_error = kErrorAio;
      return false;
    }
    std::array<iocb, kRandomQueueDepth> control{};
    std::array<iocb *, kRandomQueueDepth> submissions{};
    std::array<io_event, kRandomQueueDepth> completions{};
    auto prepare = [&](std::uint32_t slot) {
      iocb &cb = control[slot];
      std::memset(&cb, 0, sizeof(cb));
      cb.aio_data = slot;
      cb.aio_lio_opcode = write ? IOCB_CMD_PWRITE : IOCB_CMD_PREAD;
      cb.aio_fildes = static_cast<std::uint32_t>(file.fd);
      cb.aio_buf = reinterpret_cast<std::uint64_t>(
          buffers + static_cast<std::size_t>(slot) * block);
      cb.aio_nbytes = block;
      const std::uint64_t offset =
          random ? offsets[cursor++ % offsets.size()] : sequential_offset;
      if (!random) {
        sequential_offset = (sequential_offset + block) % file.size;
      }
      cb.aio_offset = static_cast<std::int64_t>(offset);
      submissions[slot] = &cb;
    };
    for (std::uint32_t slot = 0U; slot < kRandomQueueDepth; ++slot) {
      prepare(slot);
    }
    int submitted = AioSubmit(aio, kRandomQueueDepth, submissions.data());
    if (submitted != static_cast<int>(kRandomQueueDepth)) {
      AioDestroy(aio);
      *phase_error = kErrorAio;
      return false;
    }
    std::uint32_t outstanding = kRandomQueueDepth;
    context.snapshot.current_outstanding = outstanding;
    context.snapshot.max_outstanding =
        std::max(context.snapshot.max_outstanding, outstanding);
    const std::uint64_t start = NowNs();
    const std::uint64_t deadline = start + duration_ns;
    bool accepting = true;
    while (outstanding > 0U) {
      if (accepting && (NowNs() >= deadline || context.StopRequested())) {
        accepting = false;
      }
      timespec timeout{0, 50000000L};
      const int completed =
          AioGetEvents(aio, 1, kRandomQueueDepth, completions.data(), &timeout);
      if (completed < 0) {
        *phase_error = kErrorAio;
        AioDestroy(aio);
        return false;
      }
      outstanding -= static_cast<std::uint32_t>(completed);
      for (int index = 0; index < completed; ++index) {
        if (completions[static_cast<std::size_t>(index)].res !=
            static_cast<std::int64_t>(block)) {
          *phase_error = kErrorIo;
          AioDestroy(aio);
          return false;
        }
        if (measuring) {
          *bytes += block;
          ++(*io);
        }
      }
      if (accepting && completed > 0) {
        for (int index = 0; index < completed; ++index) {
          const std::uint32_t slot = static_cast<std::uint32_t>(
              completions[static_cast<std::size_t>(index)].data);
          prepare(slot);
          submissions[static_cast<std::size_t>(index)] = &control[slot];
        }
        const int resubmitted = AioSubmit(aio, completed, submissions.data());
        if (resubmitted != completed) {
          *phase_error = kErrorAio;
          AioDestroy(aio);
          return false;
        }
        outstanding += static_cast<std::uint32_t>(resubmitted);
      }
      context.snapshot.current_outstanding = outstanding;
      context.snapshot.max_outstanding =
          std::max(context.snapshot.max_outstanding, outstanding);
      if (measuring) {
        context.UpdateProgress(start, duration_ns, *bytes, *io, 0);
      }
    }
    *elapsed = NowNs() - start;
    context.snapshot.current_outstanding = 0U;
    AioDestroy(aio);
    return true;
  };

  context.SetState(StorageState::kWarmingUp, StoragePhase::kWarmUp);
  std::uint64_t ignored_bytes = 0U;
  std::uint64_t ignored_io = 0U;
  std::uint64_t ignored_elapsed = 0U;
  std::int32_t phase_error = 0;
  if (!phase(static_cast<std::uint64_t>(request.warmup_ms) * 1000000ULL,
             false, &ignored_bytes, &ignored_io, &ignored_elapsed,
             &phase_error)) {
    return MakeResult(context, test, false, false, phase_error, 0, 0, 0, 0);
  }
  if (context.StopRequested()) {
    return MakeResult(context, test, false, true, 0, 0, 0, 0, 0);
  }
  cursor = 0U;
  sequential_offset = 0U;
  context.SetState(StorageState::kMeasuring, StoragePhase::kMeasure);
  std::uint64_t bytes = 0U;
  std::uint64_t io = 0U;
  std::uint64_t elapsed = 0U;
  if (!phase(static_cast<std::uint64_t>(request.duration_ms) * 1000000ULL,
             true, &bytes, &io, &elapsed, &phase_error)) {
    return MakeResult(context, test, false, false, phase_error, elapsed, bytes,
                      io, 0);
  }
  const bool stopped = context.StopRequested();
  context.UpdateProgress(NowNs() - elapsed,
                         static_cast<std::uint64_t>(request.duration_ms) *
                             1000000ULL,
                         bytes, io, 0, true);
  if (write) {
    context.SetState(StorageState::kFlushing, StoragePhase::kFlush);
    if (fdatasync(file.fd) != 0) {
      return MakeResult(context, test, false, stopped, kErrorIo, elapsed,
                        bytes, io, 0);
    }
  }
  const bool qd_valid = context.snapshot.max_outstanding >= kRandomQueueDepth;
#if defined(__ANDROID__)
  __android_log_print(
      ANDROID_LOG_INFO, "RapidBenchStorage",
      "Q8 validation test=%u maxOutstanding=%u valid=%d",
      static_cast<std::uint32_t>(test), context.snapshot.max_outstanding,
      qd_valid ? 1 : 0);
#else
  std::fprintf(stderr,
               "RapidBench storage Q8 validation: max_outstanding=%u valid=%d\n",
               context.snapshot.max_outstanding, qd_valid ? 1 : 0);
#endif
  return MakeResult(context, test, io > 0U && qd_valid, stopped,
                    qd_valid ? 0 : kErrorAio, elapsed, bytes, io, 0);
#endif
}

StorageResult RunThreadedTest(RunContext &context, RawFile &file,
                              const StorageRequest &request,
                              StorageTest test) {
  const bool write = IsWriteTest(test);
  context.snapshot.active_test = test;
  context.snapshot.io_mode = file.mode;
  context.snapshot.block_size = static_cast<std::uint32_t>(kRandomBlock);
  context.snapshot.queue_depth = 1U;
  context.snapshot.thread_count = kRandomThreadCount;
  context.snapshot.max_outstanding = 0U;

  auto phase = [&](std::uint64_t duration_ns, bool measuring,
                   std::uint64_t *bytes, std::uint64_t *io,
                   std::uint64_t *elapsed, std::int32_t *phase_error) {
    std::atomic<bool> go{false};
    std::atomic<std::uint32_t> ready{0U};
    std::atomic<std::uint32_t> outstanding{0U};
    std::atomic<std::uint32_t> phase_max_outstanding{0U};
    std::array<std::atomic<std::uint64_t>, kRandomThreadCount> counters{};
    std::array<std::atomic<std::int32_t>, kRandomThreadCount> errors{};
    std::array<std::int32_t, kRandomThreadCount> tids{};
    std::array<AlignedMemory, kRandomThreadCount> buffers{};
    for (auto &buffer : buffers) {
      if (!buffer.Allocate(file.alignment, kRandomBlock)) {
        *phase_error = kErrorAllocation;
        return false;
      }
      FillPattern(buffer.get(), kRandomBlock, 0x243F6A8885A308D3ULL);
    }
    std::vector<std::thread> workers;
    try {
      for (std::uint32_t worker = 0U; worker < kRandomThreadCount; ++worker) {
        workers.emplace_back([&, worker]() {
#if defined(__linux__)
          tids[worker] = static_cast<std::int32_t>(syscall(SYS_gettid));
#endif
          const std::uint64_t region_size = file.size / kRandomThreadCount;
          const std::uint64_t begin = region_size * worker;
          const std::uint64_t end = worker + 1U == kRandomThreadCount
                                        ? file.size
                                        : region_size * (worker + 1U);
          auto offsets = MakeRandomOffsets(
              file.size, 0x94D049BB133111EBULL + worker, begin, end);
          std::size_t cursor = 0U;
          ready.fetch_add(1U, std::memory_order_release);
          while (!go.load(std::memory_order_acquire)) {
            std::this_thread::yield();
          }
          const std::uint64_t deadline = NowNs() + duration_ns;
          while (NowNs() < deadline && !context.StopRequested()) {
            const std::uint32_t current =
                outstanding.fetch_add(1U, std::memory_order_relaxed) + 1U;
            std::uint32_t observed =
                phase_max_outstanding.load(std::memory_order_relaxed);
            while (current > observed &&
                   !phase_max_outstanding.compare_exchange_weak(
                       observed, current, std::memory_order_relaxed)) {
            }
            const bool ok = DoSyncIo(file.fd, write, buffers[worker].get(),
                                     kRandomBlock,
                                     offsets[cursor++ % offsets.size()]);
            outstanding.fetch_sub(1U, std::memory_order_relaxed);
            if (!ok) {
              errors[worker].store(kErrorIo, std::memory_order_release);
              break;
            }
            counters[worker].fetch_add(1U, std::memory_order_relaxed);
          }
        });
      }
    } catch (...) {
      go.store(true, std::memory_order_release);
      for (auto &worker : workers) {
        if (worker.joinable()) {
          worker.join();
        }
      }
      *phase_error = kErrorThread;
      return false;
    }
    while (ready.load(std::memory_order_acquire) < kRandomThreadCount) {
      std::this_thread::yield();
    }
    std::vector<std::int32_t> worker_tids(tids.begin(), tids.end());
    PerformanceHintSession hints(worker_tids, 1000000);
    hints.RequestMaximumPerformance();
    const std::uint64_t start = NowNs();
    go.store(true, std::memory_order_release);
    while (true) {
      std::uint64_t current_io = 0U;
      for (auto &counter : counters) {
        current_io += counter.load(std::memory_order_relaxed);
      }
      const std::uint32_t current = outstanding.load(std::memory_order_relaxed);
      context.snapshot.current_outstanding = current;
      context.snapshot.max_outstanding =
          std::max(context.snapshot.max_outstanding, current);
      if (measuring) {
        context.UpdateProgress(start, duration_ns, current_io * kRandomBlock,
                               current_io, 0);
      }
      if (NowNs() - start >= duration_ns || context.StopRequested()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (auto &worker : workers) {
      worker.join();
    }
    *elapsed = NowNs() - start;
    context.snapshot.max_outstanding = std::max(
        context.snapshot.max_outstanding,
        phase_max_outstanding.load(std::memory_order_relaxed));
    *io = 0U;
    for (std::uint32_t worker = 0U; worker < kRandomThreadCount; ++worker) {
      *io += counters[worker].load(std::memory_order_relaxed);
      if (errors[worker].load(std::memory_order_acquire) != 0) {
        *phase_error = errors[worker].load(std::memory_order_relaxed);
      }
    }
    *bytes = *io * kRandomBlock;
    context.snapshot.current_outstanding = 0U;
    return *phase_error == 0;
  };

  context.SetState(StorageState::kWarmingUp, StoragePhase::kWarmUp);
  std::uint64_t bytes = 0U;
  std::uint64_t io = 0U;
  std::uint64_t elapsed = 0U;
  std::int32_t error = 0;
  if (!phase(static_cast<std::uint64_t>(request.warmup_ms) * 1000000ULL,
             false, &bytes, &io, &elapsed, &error)) {
    return MakeResult(context, test, false, false, error, 0, 0, 0, 0);
  }
  if (context.StopRequested()) {
    return MakeResult(context, test, false, true, 0, 0, 0, 0, 0);
  }
  bytes = io = elapsed = 0U;
  context.SetState(StorageState::kMeasuring, StoragePhase::kMeasure);
  if (!phase(static_cast<std::uint64_t>(request.duration_ms) * 1000000ULL,
             true, &bytes, &io, &elapsed, &error)) {
    return MakeResult(context, test, false, false, error, elapsed, bytes, io,
                      0);
  }
  const bool stopped = context.StopRequested();
  context.UpdateProgress(NowNs() - elapsed,
                         static_cast<std::uint64_t>(request.duration_ms) *
                             1000000ULL,
                         bytes, io, 0, true);
  if (write) {
    context.SetState(StorageState::kFlushing, StoragePhase::kFlush);
    if (fdatasync(file.fd) != 0) {
      return MakeResult(context, test, false, stopped, kErrorIo, elapsed,
                        bytes, io, 0);
    }
  }
  return MakeResult(context, test, io > 0U, stopped, 0, elapsed, bytes, io,
                    0);
}

int ExecSql(sqlite3 *database, const char *sql) {
  char *message = nullptr;
  const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
  if (message != nullptr) {
    std::fprintf(stderr, "RapidBench sqlite: %s\n", message);
    sqlite3_free(message);
  }
  return result;
}

bool ConfigureSqlite(sqlite3 *database) {
  return ExecSql(database, "PRAGMA page_size=4096;") == SQLITE_OK &&
         ExecSql(database, "PRAGMA journal_mode=WAL;") == SQLITE_OK &&
         ExecSql(database, "PRAGMA synchronous=NORMAL;") == SQLITE_OK &&
         ExecSql(database, "PRAGMA cache_size=-8192;") == SQLITE_OK &&
         ExecSql(database, "PRAGMA temp_store=MEMORY;") == SQLITE_OK &&
         ExecSql(database, "PRAGMA wal_autocheckpoint=1000;") == SQLITE_OK;
}

bool CreateSqliteSchema(sqlite3 *database) {
  return ExecSql(database,
                 "CREATE TABLE benchmark("
                 "id INTEGER PRIMARY KEY,"
                 "timestamp INTEGER NOT NULL,"
                 "value INTEGER NOT NULL,"
                 "name TEXT NOT NULL,"
                 "payload BLOB NOT NULL);") == SQLITE_OK &&
         ExecSql(database,
                 "CREATE INDEX benchmark_value_idx ON benchmark(value);") ==
             SQLITE_OK;
}

bool PopulateSqlite(sqlite3 *database, std::uint32_t rows,
                    const std::array<std::uint8_t, kSqlitePayloadBytes> &payload,
                    std::atomic<bool> *stop) {
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(
          database,
          "INSERT INTO benchmark(id,timestamp,value,name,payload)"
          " VALUES(?1,?2,?3,?4,?5);",
          -1, &statement, nullptr) != SQLITE_OK) {
    return false;
  }
  bool ok = true;
  for (std::uint32_t base = 0U;
       base < rows && ok && !stop->load(std::memory_order_acquire);
       base += kSqliteTransactionRows) {
    ok = ExecSql(database, "BEGIN IMMEDIATE;") == SQLITE_OK;
    const std::uint32_t end = std::min(rows, base + kSqliteTransactionRows);
    for (std::uint32_t row = base; row < end && ok; ++row) {
      sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(row + 1U));
      sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(row));
      sqlite3_bind_int(statement, 3, static_cast<int>(row * 2654435761U));
      sqlite3_bind_text(statement, 4, "RapidBench", -1, SQLITE_STATIC);
      sqlite3_bind_blob(statement, 5, payload.data(), payload.size(),
                        SQLITE_STATIC);
      ok = sqlite3_step(statement) == SQLITE_DONE;
      sqlite3_reset(statement);
      sqlite3_clear_bindings(statement);
    }
    ok = ok && ExecSql(database, "COMMIT;") == SQLITE_OK;
  }
  sqlite3_finalize(statement);
  return ok;
}

void RemoveSqliteFiles(const std::string &path) {
#if defined(__linux__)
  unlink(path.c_str());
  unlink((path + "-wal").c_str());
  unlink((path + "-shm").c_str());
#else
  (void)path;
#endif
}

StorageResult RunSqliteTest(RunContext &context,
                            const StorageRequest &request,
                            StorageTest test, const std::string &path) {
  RemoveSqliteFiles(path);
  context.snapshot.active_test = test;
  context.snapshot.io_mode = StorageIoMode::kSqlite;
  context.snapshot.block_size = 0U;
  context.snapshot.queue_depth = 1U;
  context.snapshot.thread_count = 1U;
  context.snapshot.max_outstanding = 1U;
  context.SetState(StorageState::kPreparing, StoragePhase::kPrepare);
  sqlite3 *database = nullptr;
  sqlite3_stmt *statement = nullptr;
  std::array<std::uint8_t, kSqlitePayloadBytes> payload{};
  FillPattern(payload.data(), payload.size(), 0xDB4F0B9175AE2165ULL);
  auto finish = [&]() {
    if (statement != nullptr) {
      sqlite3_finalize(statement);
      statement = nullptr;
    }
    if (database != nullptr) {
      sqlite3_close(database);
      database = nullptr;
    }
    RemoveSqliteFiles(path);
  };
  if (sqlite3_open_v2(path.c_str(), &database,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK ||
      !ConfigureSqlite(database) || !CreateSqliteSchema(database)) {
    finish();
    return MakeResult(context, test, false, false, kErrorSqlite, 0, 0, 0, 0);
  }
  if (test == StorageTest::kSqliteDelete &&
      !PopulateSqlite(database, kSqliteInsertLimit, payload, &context.stop)) {
    finish();
    return MakeResult(context, test, false, false, kErrorSqlite, 0, 0, 0, 0);
  }
  if (context.StopRequested()) {
    finish();
    return MakeResult(context, test, false, true, 0, 0, 0, 0, 0);
  }

  std::vector<std::uint32_t> delete_ids;
  if (test == StorageTest::kSqliteDelete) {
    delete_ids.resize(kSqliteInsertLimit);
    std::iota(delete_ids.begin(), delete_ids.end(), 1U);
    std::mt19937 random(0x5EED1234U);
    std::shuffle(delete_ids.begin(), delete_ids.end(), random);
  }
  const char *sql = test == StorageTest::kSqliteInsert
                        ? "INSERT INTO benchmark(id,timestamp,value,name,payload)"
                          " VALUES(?1,?2,?3,?4,?5);"
                        : "DELETE FROM benchmark WHERE id=?1;";
  if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
    finish();
    return MakeResult(context, test, false, false, kErrorSqlite, 0, 0, 0, 0);
  }

  context.SetState(StorageState::kMeasuring, StoragePhase::kMeasure);
  const std::uint64_t duration_ns =
      static_cast<std::uint64_t>(request.duration_ms) * 1000000ULL;
  const std::uint64_t start = NowNs();
  const std::uint32_t row_limit = test == StorageTest::kSqliteInsert
                                      ? kSqliteInsertLimit
                                      : kSqliteDeleteLimit;
  std::uint64_t rows = 0U;
  std::int32_t error = 0;
  while (rows < row_limit && NowNs() - start < duration_ns &&
         !context.StopRequested()) {
    if (ExecSql(database, "BEGIN IMMEDIATE;") != SQLITE_OK) {
      error = kErrorSqlite;
      break;
    }
    std::uint32_t transaction_rows = 0U;
    while (transaction_rows < kSqliteTransactionRows && rows < row_limit) {
      const std::uint32_t id = test == StorageTest::kSqliteInsert
                                   ? static_cast<std::uint32_t>(rows + 1U)
                                   : delete_ids[static_cast<std::size_t>(rows)];
      sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(id));
      if (test == StorageTest::kSqliteInsert) {
        sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(rows));
        sqlite3_bind_int(statement, 3,
                         static_cast<int>(id * 2654435761U));
        sqlite3_bind_text(statement, 4, "RapidBench", -1, SQLITE_STATIC);
        sqlite3_bind_blob(statement, 5, payload.data(), payload.size(),
                          SQLITE_STATIC);
      }
      if (sqlite3_step(statement) != SQLITE_DONE) {
        error = kErrorSqlite;
        break;
      }
      sqlite3_reset(statement);
      sqlite3_clear_bindings(statement);
      ++rows;
      ++transaction_rows;
    }
    if (error != 0 || ExecSql(database, "COMMIT;") != SQLITE_OK) {
      error = kErrorSqlite;
      break;
    }
    context.UpdateProgress(start, duration_ns, 0, 0, rows);
  }
  const std::uint64_t elapsed = NowNs() - start;
  const bool stopped = context.StopRequested();
  context.UpdateProgress(start, duration_ns, 0, 0, rows, true);
  finish();
  return MakeResult(context, test, error == 0 && rows > 0U, stopped, error,
                    elapsed, 0, 0, rows);
}

StorageResult RunRawTest(RunContext &context, RawFile &file,
                         const StorageRequest &request, StorageTest test) {
  if (test == StorageTest::kSequentialRead ||
      test == StorageTest::kSequentialWrite ||
      test == StorageTest::kRandom4KQ8T1Read ||
      test == StorageTest::kRandom4KQ8T1Write) {
    if (file.mode == StorageIoMode::kDirect) {
      return RunAioTest(context, file, request, test);
    }
    if (test == StorageTest::kSequentialRead ||
        test == StorageTest::kSequentialWrite) {
      return RunSyncTest(context, file, request, test);
    }
    return MakeResult(context, test, false, false, kErrorAio, 0, 0, 0, 0);
  }
  if (test == StorageTest::kRandom4KQ1T4Read ||
      test == StorageTest::kRandom4KQ1T4Write) {
    return RunThreadedTest(context, file, request, test);
  }
  return RunSyncTest(context, file, request, test);
}

} // namespace

StorageEngine::StorageEngine(std::string directory)
    : directory_(std::move(directory)) {
  snapshot_.state = StorageState::kIdle;
}

StorageEngine::~StorageEngine() {
  stop_requested_.store(true, std::memory_order_release);
  if (coordinator_.joinable()) {
    coordinator_.join();
  }
}

std::int32_t StorageEngine::Start(const StorageRequest &request,
                                  std::uint64_t *out_run_id) {
  if (out_run_id == nullptr || directory_.empty() ||
      request.duration_ms < 250U || request.duration_ms > 30000U ||
      request.warmup_ms > 5000U ||
      (request.test < StorageTest::kSequentialRead ||
       request.test > StorageTest::kAll)) {
    return kStatusInvalidArgument;
  }
  StorageSnapshot initial{};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!IsTerminal(snapshot_.state)) {
      return kStatusBusy;
    }
    initial.results = snapshot_.results;
  }
  if (coordinator_.joinable()) {
    coordinator_.join();
  }
  const std::uint64_t run_id =
      next_run_id_.fetch_add(1U, std::memory_order_relaxed);
  initial.run_id = run_id;
  initial.state = StorageState::kPreparing;
  initial.phase = StoragePhase::kPrepare;
  initial.active_test = request.test;
  if (request.test == StorageTest::kAll) {
    initial.results = {};
  } else {
    initial.results[ResultIndex(request.test)] = {};
    initial.results[ResultIndex(request.test)].test = request.test;
  }
  stop_requested_.store(false, std::memory_order_release);
  Publish(initial);
  try {
    coordinator_ = std::thread(&StorageEngine::Run, this, request, run_id);
  } catch (...) {
    initial.state = StorageState::kError;
    initial.error_code = kStatusInternalError;
    Publish(initial);
    return kStatusInternalError;
  }
  *out_run_id = run_id;
  return kStatusOk;
}

std::int32_t StorageEngine::RequestStop(std::uint64_t run_id) {
  StorageSnapshot snapshot = GetSnapshot();
  if (run_id == 0U || snapshot.run_id != run_id ||
      IsTerminal(snapshot.state)) {
    return kStatusInvalidArgument;
  }
  stop_requested_.store(true, std::memory_order_release);
  snapshot.state = StorageState::kStopping;
  Publish(snapshot);
  return kStatusOk;
}

StorageSnapshot StorageEngine::GetSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

void StorageEngine::Publish(const StorageSnapshot &snapshot) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = snapshot;
}

void StorageEngine::Run(StorageRequest request, std::uint64_t run_id) {
  RunContext context{GetSnapshot(), stop_requested_,
                     [this](const StorageSnapshot &value) { Publish(value); }};
  context.snapshot.run_id = run_id;
  std::vector<StorageTest> tests;
  if (request.test == StorageTest::kAll) {
    // Write before read keeps allocation/setup outside the read measurement.
    tests = {StorageTest::kSequentialWrite,
             StorageTest::kSequentialRead,
             StorageTest::kRandom4KQ1T1Read,
             StorageTest::kRandom4KQ1T1Write,
             StorageTest::kRandom4KQ8T1Read,
             StorageTest::kRandom4KQ8T1Write,
             StorageTest::kRandom4KQ1T4Read,
             StorageTest::kRandom4KQ1T4Write,
             StorageTest::kSqliteInsert,
             StorageTest::kSqliteDelete};
  } else {
    tests.push_back(request.test);
  }
  RawFile file;
  file.path = directory_ + "/storage_bench.dat";
  bool raw_prepared = false;
  bool raw_unavailable = false;
  std::int32_t final_error = 0;
  for (StorageTest test : tests) {
    if (stop_requested_.load(std::memory_order_acquire)) {
      break;
    }
    StorageResult result{};
    if (IsRawTest(test)) {
      if (!raw_prepared && !raw_unavailable) {
        if (!OpenAndPrepareRawFile(context, request, &file, &final_error)) {
          if (stop_requested_.load(std::memory_order_acquire)) {
            break;
          }
          raw_unavailable = true;
        } else {
          raw_prepared = true;
        }
      }
      if (raw_prepared) {
        result = RunRawTest(context, file, request, test);
      } else {
        result = MakeResult(context, test, false, false, final_error, 0, 0, 0,
                            0);
      }
    } else {
      result = RunSqliteTest(context, request, test,
                             directory_ + "/storage_bench.db");
    }
    if (result.test == StorageTest::kNone) {
      result.test = test;
      result.error_code = final_error;
    }
    context.snapshot.results[ResultIndex(test)] = result;
#if defined(__ANDROID__)
    __android_log_print(
        ANDROID_LOG_INFO, "RapidBenchStorage",
        "result test=%u valid=%d stopped=%d error=%d mode=%u threads=%u qd=%u "
        "maxOutstanding=%u elapsedMs=%.1f MBps=%.2f IOPS=%.1f rowsps=%.1f",
        static_cast<std::uint32_t>(test), result.valid ? 1 : 0,
        result.stopped ? 1 : 0, result.error_code,
        static_cast<std::uint32_t>(result.io_mode), result.thread_count,
        result.queue_depth, result.max_outstanding,
        static_cast<double>(result.elapsed_ns) / 1.0e6, result.mbps,
        result.iops, result.rows_per_second);
#endif
    context.publish(context.snapshot);
    if (result.error_code != 0) {
      final_error = result.error_code;
      if (request.test != StorageTest::kAll) {
        break;
      }
    }
    if (result.stopped || stop_requested_.load(std::memory_order_acquire)) {
      break;
    }
  }

  context.snapshot.active_test = StorageTest::kNone;
  context.snapshot.phase = StoragePhase::kNone;
  context.snapshot.current_outstanding = 0U;
  context.snapshot.progress = 1.0;
  if (stop_requested_.load(std::memory_order_acquire)) {
    context.snapshot.state = StorageState::kStopped;
  } else if (final_error != 0 && request.test != StorageTest::kAll) {
    context.snapshot.state = StorageState::kError;
    context.snapshot.error_code = final_error;
  } else {
    context.snapshot.state = StorageState::kCompleted;
  }
  context.publish(context.snapshot);
}

} // namespace benchmark








