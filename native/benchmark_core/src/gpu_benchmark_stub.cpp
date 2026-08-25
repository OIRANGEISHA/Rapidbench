#include "benchmark/gpu_benchmark.h"

namespace benchmark {

class GpuBenchmarkEngine::Impl {
public:
  GpuSnapshot snapshot{};
};

GpuBenchmarkEngine::GpuBenchmarkEngine() : impl_(std::make_unique<Impl>()) {
  impl_->snapshot.last_error = "Vulkan compute is Android-only";
}

GpuBenchmarkEngine::~GpuBenchmarkEngine() = default;

std::int32_t GpuBenchmarkEngine::Start(const GpuRequest &,
                                       std::uint64_t *) {
  return -3;
}

std::int32_t GpuBenchmarkEngine::RequestStop(std::uint64_t) { return -1; }

GpuSnapshot GpuBenchmarkEngine::GetSnapshot() const { return impl_->snapshot; }

std::string GpuBenchmarkEngine::GetCapabilitiesJson() const {
  return "{\"status\":\"unavailable\",\"reason\":\"Vulkan compute is Android-only\"}";
}

} // namespace benchmark
