#include "benchmark/performance_hint.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace benchmark {

class PerformanceHintSession::Impl final {
public:
  Impl(const std::vector<std::int32_t> &thread_ids,
       std::int64_t target_duration_ns) {
#if defined(__ANDROID__)
    if (thread_ids.empty() || target_duration_ns <= 0) {
      return;
    }
    library_ = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (library_ == nullptr) {
      return;
    }

    get_manager_ = LoadSymbol<GetManagerFn>("APerformanceHint_getManager");
    get_preferred_update_rate_ = LoadSymbol<GetPreferredUpdateRateFn>(
        "APerformanceHint_getPreferredUpdateRateNanos");
    create_session_ =
        LoadSymbol<CreateSessionFn>("APerformanceHint_createSession");
    close_session_ =
        LoadSymbol<CloseSessionFn>("APerformanceHint_closeSession");
    update_target_ =
        LoadSymbol<UpdateTargetFn>("APerformanceHint_updateTargetWorkDuration");
    report_duration_ = LoadSymbol<ReportDurationFn>(
        "APerformanceHint_reportActualWorkDuration");
    set_prefer_power_efficiency_ = LoadSymbol<SetPreferPowerEfficiencyFn>(
        "APerformanceHint_setPreferPowerEfficiency");
    notify_workload_increase_ = LoadSymbol<NotifyWorkloadIncreaseFn>(
        "APerformanceHint_notifyWorkloadIncrease");
    if (get_manager_ == nullptr || create_session_ == nullptr ||
        close_session_ == nullptr || report_duration_ == nullptr) {
      return;
    }

    void *manager = get_manager_();
    if (manager == nullptr) {
      return;
    }
    if (get_preferred_update_rate_ != nullptr) {
      preferred_update_rate_ns_ = get_preferred_update_rate_(manager);
    }
    session_ = create_session_(manager, thread_ids.data(), thread_ids.size(),
                               target_duration_ns);
#else
    (void)thread_ids;
    (void)target_duration_ns;
#endif
  }

  ~Impl() {
#if defined(__ANDROID__)
    if (session_ != nullptr && close_session_ != nullptr) {
      close_session_(session_);
    }
    if (library_ != nullptr) {
      dlclose(library_);
    }
#endif
  }

  bool IsActive() const {
#if defined(__ANDROID__)
    return session_ != nullptr;
#else
    return false;
#endif
  }

  std::int64_t PreferredUpdateRateNs() const {
#if defined(__ANDROID__)
    return preferred_update_rate_ns_;
#else
    return 0;
#endif
  }

  void UpdateTargetDuration(std::int64_t target_duration_ns) {
#if defined(__ANDROID__)
    if (session_ != nullptr && update_target_ != nullptr &&
        target_duration_ns > 0) {
      update_target_(session_, target_duration_ns);
    }
#else
    (void)target_duration_ns;
#endif
  }

  void ReportActualDuration(std::int64_t actual_duration_ns) {
#if defined(__ANDROID__)
    if (session_ != nullptr && report_duration_ != nullptr &&
        actual_duration_ns > 0) {
      report_duration_(session_, actual_duration_ns);
    }
#else
    (void)actual_duration_ns;
#endif
  }

  bool RequestMaximumPerformance() {
#if defined(__ANDROID__)
    if (session_ == nullptr) {
      return false;
    }

    bool requested = false;
    if (set_prefer_power_efficiency_ != nullptr) {
      requested =
          set_prefer_power_efficiency_(session_, false) == 0 || requested;
    }
    if (notify_workload_increase_ != nullptr) {
      requested = notify_workload_increase_(session_, true, false,
                                            "cpu_benchmark_start") == 0 ||
                  requested;
    }
    return requested;
#else
    return false;
#endif
  }

private:
#if defined(__ANDROID__)
  using GetManagerFn = void *(*)();
  using GetPreferredUpdateRateFn = std::int64_t (*)(void *);
  using CreateSessionFn = void *(*)(void *, const std::int32_t *, std::size_t,
                                    std::int64_t);
  using CloseSessionFn = void (*)(void *);
  using UpdateTargetFn = int (*)(void *, std::int64_t);
  using ReportDurationFn = int (*)(void *, std::int64_t);
  using SetPreferPowerEfficiencyFn = int (*)(void *, bool);
  using NotifyWorkloadIncreaseFn = int (*)(void *, bool, bool, const char *);

  template <typename Function> Function LoadSymbol(const char *name) {
    void *symbol = dlsym(library_, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(symbol));
    std::memcpy(&function, &symbol, sizeof(function));
    return function;
  }

  void *library_ = nullptr;
  void *session_ = nullptr;
  std::int64_t preferred_update_rate_ns_ = 0;
  GetManagerFn get_manager_ = nullptr;
  GetPreferredUpdateRateFn get_preferred_update_rate_ = nullptr;
  CreateSessionFn create_session_ = nullptr;
  CloseSessionFn close_session_ = nullptr;
  UpdateTargetFn update_target_ = nullptr;
  ReportDurationFn report_duration_ = nullptr;
  SetPreferPowerEfficiencyFn set_prefer_power_efficiency_ = nullptr;
  NotifyWorkloadIncreaseFn notify_workload_increase_ = nullptr;
#endif
};

PerformanceHintSession::PerformanceHintSession(
    const std::vector<std::int32_t> &thread_ids,
    std::int64_t target_duration_ns)
    : impl_(std::make_unique<Impl>(thread_ids, target_duration_ns)) {}

PerformanceHintSession::~PerformanceHintSession() = default;

bool PerformanceHintSession::IsActive() const { return impl_->IsActive(); }

std::int64_t PerformanceHintSession::PreferredUpdateRateNs() const {
  return impl_->PreferredUpdateRateNs();
}

void PerformanceHintSession::UpdateTargetDuration(
    std::int64_t target_duration_ns) {
  impl_->UpdateTargetDuration(target_duration_ns);
}

void PerformanceHintSession::ReportActualDuration(
    std::int64_t actual_duration_ns) {
  impl_->ReportActualDuration(actual_duration_ns);
}

bool PerformanceHintSession::RequestMaximumPerformance() {
  return impl_->RequestMaximumPerformance();
}

} // namespace benchmark
