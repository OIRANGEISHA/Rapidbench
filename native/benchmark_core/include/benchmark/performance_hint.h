#ifndef CPU_BENCHMARK_PERFORMANCE_HINT_H_
#define CPU_BENCHMARK_PERFORMANCE_HINT_H_

#include <cstdint>
#include <memory>
#include <vector>

namespace benchmark {

class PerformanceHintSession final {
public:
  PerformanceHintSession(const std::vector<std::int32_t> &thread_ids,
                         std::int64_t target_duration_ns);
  ~PerformanceHintSession();

  PerformanceHintSession(const PerformanceHintSession &) = delete;
  PerformanceHintSession &operator=(const PerformanceHintSession &) = delete;

  bool IsActive() const;
  std::int64_t PreferredUpdateRateNs() const;
  void UpdateTargetDuration(std::int64_t target_duration_ns);
  void ReportActualDuration(std::int64_t actual_duration_ns);
  bool RequestMaximumPerformance();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace benchmark

#endif // CPU_BENCHMARK_PERFORMANCE_HINT_H_
