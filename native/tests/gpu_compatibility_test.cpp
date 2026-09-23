#include "benchmark/gpu_compatibility.h"
#include <cstdio>
#include <limits>

int main() {
  using namespace benchmark::detail;
  GpuPipelineSupport support;
  if (support.AvailableMask() != 0) return 1;
  support.fp32 = support.fp16_emulated = support.int32 = support.mixed = support.memory = true;
  if (support.AvailableMask() != 0x3e) return 2;
  // Native FP16 rejection must not erase an available emulated pipeline.
  support.fp16_native = false;
  if ((support.AvailableMask() & (1U << 2)) == 0) return 3;
  support.fp16_emulated = false;
  if (support.AvailableMask() != (0x3e & ~(1U << 2))) return 4;
  support.fp32 = false;
  if ((support.AvailableMask() & (1U << 3)) == 0) return 5;
  GpuDiagnostics items{};
  auto &item = items[1];
  item.run_id = 7;
  item.AddBatch(.01, .009, true);
  item.AddBatch(.01, .001, false);
  item.AddBatch(.01, std::numeric_limits<double>::quiet_NaN(), false);
  if (item.host_batches != 2 || item.timestamp_batches != 1 || item.gpu_observations != 2) return 6;
  item.reason = "driver \"error\"\nretry";
  item.state = GpuItemState::kFailed;
  const auto json = GpuDiagnosticsJson(7, items, true);
  if (json.find("\"fatal\":true") == std::string::npos ||
      json.find("driver \\\"error\\\"\\nretry") == std::string::npos ||
      json.find("\"5\":") == std::string::npos ||
      json.find("nan") != std::string::npos) return 7;
  item = {};
  if (item.host_batches || item.timestamp_batches || item.run_id ||
      item.state != GpuItemState::kReady) return 8;
  std::puts("GPU pipeline support and timing diagnostics passed");
}
