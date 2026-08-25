#include "gpu_ffi.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

bool ReadSnapshot(bm_gpu_engine_handle engine, bm_gpu_snapshot_v1 *snapshot) {
  std::memset(snapshot, 0, sizeof(*snapshot));
  snapshot->struct_size = sizeof(*snapshot);
  snapshot->abi_version = BM_ABI_VERSION;
  return bm_gpu_get_snapshot(engine, snapshot) == BM_STATUS_OK;
}

void PrintSnapshot(const char *label, const bm_gpu_snapshot_v1 &snapshot) {
  std::printf(
      "%s state=%u active=%u fp16Mode=%u timing=%u fp32=%.3f "
      "fp16=%.3f int32=%.3f mixed=%.3f bandwidth=%.3f "
      "bufferMiB=%.1f progress=%.3f error=%d message=%s\n",
      label, snapshot.state, snapshot.active_test, snapshot.fp16_mode,
      snapshot.timing_mode, snapshot.fp32_gflops, snapshot.fp16_gflops,
      snapshot.int32_gops, snapshot.mixed_gwork,
      snapshot.memory_bandwidth_gbps,
      static_cast<double>(snapshot.buffer_bytes) / (1024.0 * 1024.0),
      snapshot.progress, snapshot.error_code, snapshot.last_error);
}

bool WaitForTerminal(bm_gpu_engine_handle engine,
                     bm_gpu_snapshot_v1 *snapshot,
                     std::chrono::seconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!ReadSnapshot(engine, snapshot)) {
      return false;
    }
    if (snapshot->state == BM_GPU_STATE_STOPPED ||
        snapshot->state == BM_GPU_STATE_COMPLETED ||
        snapshot->state == BM_GPU_STATE_ERROR) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return false;
}

bool Start(bm_gpu_engine_handle engine, std::uint32_t test,
           std::uint32_t duration_ms, std::uint32_t warmup_ms,
           std::uint64_t *run_id) {
  bm_gpu_request_v1 request{};
  request.struct_size = sizeof(request);
  request.abi_version = BM_ABI_VERSION;
  request.test_id = test;
  request.duration_ms = duration_ms;
  request.warmup_ms = warmup_ms;
  return bm_gpu_start(engine, &request, run_id) == BM_STATUS_OK;
}

} // namespace

int main() {
  bm_gpu_engine_handle engine = nullptr;
  if (bm_gpu_engine_create(&engine) != BM_STATUS_OK || engine == nullptr) {
    std::fprintf(stderr, "engine_create_failed\n");
    return 1;
  }

  std::uint32_t required = 0U;
  if (bm_gpu_get_capabilities_json(engine, nullptr, 0U, &required) !=
          BM_STATUS_OK ||
      required < 2U) {
    std::fprintf(stderr, "capability_size_failed\n");
    bm_gpu_engine_destroy(engine);
    return 2;
  }
  std::vector<char> capabilities(required);
  if (bm_gpu_get_capabilities_json(engine, capabilities.data(), required,
                                   &required) != BM_STATUS_OK) {
    std::fprintf(stderr, "capability_read_failed\n");
    bm_gpu_engine_destroy(engine);
    return 3;
  }
  std::printf("CAPABILITIES %s\n", capabilities.data());

  std::uint64_t run_id = 0U;
  bm_gpu_snapshot_v1 snapshot{};
  if (!Start(engine, BM_GPU_TEST_FP32, 700U, 200U, &run_id) ||
      !WaitForTerminal(engine, &snapshot, std::chrono::seconds(15))) {
    std::fprintf(stderr, "fp32_timeout_or_start_failed\n");
    bm_gpu_engine_destroy(engine);
    return 4;
  }
  PrintSnapshot("FP32", snapshot);
  if (snapshot.state != BM_GPU_STATE_COMPLETED ||
      !std::isfinite(snapshot.fp32_gflops) || snapshot.fp32_gflops <= 0.0) {
    bm_gpu_engine_destroy(engine);
    return 5;
  }

  if (!Start(engine, BM_GPU_TEST_ALL, 700U, 150U, &run_id)) {
    bm_gpu_engine_destroy(engine);
    return 6;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(900));
  if (bm_gpu_request_stop(engine, run_id) != BM_STATUS_OK ||
      !WaitForTerminal(engine, &snapshot, std::chrono::seconds(15))) {
    std::fprintf(stderr, "stop_failed\n");
    bm_gpu_engine_destroy(engine);
    return 7;
  }
  PrintSnapshot("STOP", snapshot);
  if (snapshot.state != BM_GPU_STATE_STOPPED || snapshot.fp32_gflops <= 0.0) {
    bm_gpu_engine_destroy(engine);
    return 8;
  }

  if (!Start(engine, BM_GPU_TEST_ALL, 500U, 100U, &run_id) ||
      !WaitForTerminal(engine, &snapshot, std::chrono::seconds(45))) {
    std::fprintf(stderr, "full_timeout_or_start_failed\n");
    bm_gpu_engine_destroy(engine);
    return 9;
  }
  PrintSnapshot("FULL", snapshot);
  const bool full_valid =
      snapshot.state == BM_GPU_STATE_COMPLETED &&
      snapshot.fp32_gflops > 0.0 && snapshot.fp16_gflops > 0.0 &&
      snapshot.int32_gops > 0.0 && snapshot.mixed_gwork > 0.0 &&
      snapshot.memory_bandwidth_gbps > 0.0;
  bm_gpu_engine_destroy(engine);
  return full_valid ? 0 : 10;
}
