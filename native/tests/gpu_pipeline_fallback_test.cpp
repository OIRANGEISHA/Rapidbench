#include "benchmark/gpu_benchmark.h"
#include <chrono>
#include <cstdio>
#include <thread>

namespace {
unsigned rejected = 0;
bool lost = false;
bool Run(benchmark::GpuBenchmarkEngine &engine, benchmark::GpuTest test,
         benchmark::GpuSnapshot &snapshot) {
  std::uint64_t run = 0;
  if (engine.Start({test, 500, 100}, &run) != 0) return false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  do {
    snapshot = engine.GetSnapshot();
    if (snapshot.state == benchmark::GpuState::kCompleted) return true;
    if (snapshot.state == benchmark::GpuState::kError) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } while (std::chrono::steady_clock::now() < deadline);
  return false;
}
}
namespace benchmark::detail {
int GpuPipelineFaultForTest(unsigned slot) {
  return (rejected & (1U << slot)) ? (lost ? 2 : 1) : 0;
}
}
int main() {
  using namespace benchmark;
  GpuSnapshot snapshot;
  rejected = 1U << 3; // Reject the native-half baseline.
  {
    GpuBenchmarkEngine engine;
    if (!Run(engine, GpuTest::kFp16, snapshot) ||
        snapshot.fp16_mode != GpuFp16Mode::kEmulated || snapshot.fp16_gflops <= 0 ||
        snapshot.diagnostics[2].state != detail::GpuItemState::kCompleted) return 1;
    std::puts("GPU injected native-FP16 rejection: validated FP32 emulation passed");
  }
  rejected = 0x1f8U; // Reject every native/emulated half pipeline.
  {
    GpuBenchmarkEngine engine;
    if (!Run(engine, GpuTest::kAll, snapshot) || snapshot.fp16_gflops != 0 ||
        snapshot.diagnostics[2].state != detail::GpuItemState::kUnavailable ||
        snapshot.fp32_gflops <= 0 || snapshot.int32_gops <= 0 ||
        snapshot.mixed_gwork <= 0 || snapshot.memory_bandwidth_gbps <= 0) return 2;
    if (Run(engine, GpuTest::kFp16, snapshot) || snapshot.fatal_error ||
        snapshot.diagnostics[2].state != detail::GpuItemState::kFailed) return 3;
    if (!Run(engine, GpuTest::kInt32, snapshot)) return 4;
    std::puts("GPU injected all-FP16 rejection: All skips, single fails, INT32 restart passes");
  }
  rejected = 6U; // Reject optional FP32 12/16-chain variants.
  {
    GpuBenchmarkEngine engine;
    if (!Run(engine, GpuTest::kFp32, snapshot) ||
        snapshot.diagnostics[1].fp_accumulators != 8) return 5;
    std::puts("GPU injected optional-variant rejection: validated 8-chain fallback passed");
  }
  rejected = 0xfffU;
  {
    GpuBenchmarkEngine engine;
    if (engine.GetSnapshot().vulkan_available || Run(engine, GpuTest::kAll, snapshot)) return 6;
    std::puts("GPU injected all-pipeline rejection: whole module unavailable passed");
  }
  rejected = 1U; lost = true;
  {
    GpuBenchmarkEngine engine;
    if (engine.GetSnapshot().vulkan_available || Run(engine, GpuTest::kAll, snapshot)) return 7;
    std::puts("GPU injected device loss: global stop passed");
  }
}
