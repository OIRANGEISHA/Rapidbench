#include "benchmark/cpu_application_engine.h"

#include "benchmark/performance_hint.h"
#include "benchmark/topology.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace benchmark {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
bool Running(State state) {
  return state == State::kPreparing || state == State::kWarmingUp ||
         state == State::kMeasuring;
}
std::uint64_t Ns(Clock::duration duration) {
  return static_cast<std::uint64_t>(std::max<std::int64_t>(
      0,
      std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count()));
}
struct Worker {
  explicit Worker(std::uint32_t target, CpuApplicationTest test)
      : cpu(target), workload(test) {}
  std::uint32_t cpu;
  CpuApplicationWorkload workload;
  std::atomic<std::int32_t> tid{-1};
  std::atomic<std::uint64_t> units{0}, elapsed{0}, last_batch_ns{0};
  std::atomic<bool> affinity_failed{false}, performance_requested{false};
  // Populated by the coordinator only after all workers have joined.
  std::uint64_t digest = 0;
  bool validated = false;
};
struct Gates {
  std::atomic<unsigned> ready{0}, warm_done{0}, done{0}, phase{0};
  std::atomic<std::int32_t> error{0};
  Clock::time_point warm_deadline{}, start{}, deadline{};
};
void Fail(Gates *gate, std::atomic<bool> *stop, std::int32_t code) {
  std::int32_t expected = 0;
  gate->error.compare_exchange_strong(expected, code);
  stop->store(true);
}
bool WaitPhase(Gates *gate, const std::atomic<bool> *stop, unsigned phase) {
  while (gate->phase.load(std::memory_order_acquire) < phase) {
    if (stop->load())
      return false;
    std::this_thread::sleep_for(1ms);
  }
  return !stop->load();
}
void RunWorker(Worker *worker, Gates *gate, std::atomic<bool> *stop) {
  try {
    worker->tid.store(CurrentThreadId());
    worker->performance_requested.store(
        RequestMaximumPerformanceForCurrentThread());
    bool pinned = PinCurrentThreadToCpu(worker->cpu);
    worker->affinity_failed.store(!pinned);
    worker->workload.RunBatch();
    if (!worker->workload.Validate())
      Fail(gate, stop, -30);
    gate->ready.fetch_add(1);
    if (WaitPhase(gate, stop, 1)) {
      while (!stop->load() && Clock::now() < gate->warm_deadline)
        worker->workload.RunBatch();
      gate->warm_done.fetch_add(1);
      if (WaitPhase(gate, stop, 2)) {
        while (!stop->load() && Clock::now() < gate->start)
          std::this_thread::yield();
        auto next_affinity_check = gate->start;
        auto last_end = gate->start;
        std::uint64_t units = 0;
        while (!stop->load() && Clock::now() < gate->deadline) {
          const auto batch_start = Clock::now();
          units += worker->workload.RunBatch();
          last_end = Clock::now();
          worker->last_batch_ns.store(Ns(last_end - batch_start));
          worker->units.store(units, std::memory_order_relaxed);
          if (last_end >= next_affinity_check) {
            const auto actual = CurrentLogicalCpu();
            if (!pinned || (actual >= 0 &&
                            actual != static_cast<std::int32_t>(worker->cpu))) {
              worker->affinity_failed.store(true);
              pinned = PinCurrentThreadToCpu(worker->cpu);
            }
            next_affinity_check = last_end + 100ms;
          }
        }
        worker->elapsed.store(units == 0 ? 0 : Ns(last_end - gate->start));
      }
    }
  } catch (const std::bad_alloc &) {
    Fail(gate, stop, -31);
  } catch (...) {
    Fail(gate, stop, -32);
  }
  gate->done.fetch_add(1);
}
} // namespace

std::vector<std::uint32_t> SelectCpuApplicationCpus(const Topology &topology,
                                                    bool multi) {
  std::vector<std::uint32_t> cpus;
  for (const auto &cpu : topology.cpus)
    cpus.push_back(cpu.logical_cpu);
  std::sort(cpus.begin(), cpus.end());
  cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
  if (!multi) {
    const auto found =
        std::find(cpus.begin(), cpus.end(),
                  static_cast<std::uint32_t>(topology.preferred_single_cpu));
    if (found == cpus.end())
      return {};
    return {*found};
  }
  if (cpus.size() > 64)
    return {}; // Do not silently truncate an unsupported topology.
  return cpus;
}

class CpuApplicationEngine::Impl {
public:
  ~Impl() {
    stop_.store(true);
    if (thread_.joinable())
      thread_.join();
  }
  std::int32_t Start(const CpuApplicationRequest &request,
                     std::uint64_t *run_id) {
    const auto kind = static_cast<std::uint32_t>(request.test);
    if (!run_id || kind < 1 || kind > 3 || request.duration_ms < 100 ||
        request.duration_ms > 60000 || request.warmup_ms > 30000 ||
        request.requested_threads > 1)
      return -1;
    std::lock_guard<std::mutex> control(control_mutex_);
    if (Running(GetSnapshot().state))
      return -2;
    if (thread_.joinable())
      thread_.join();
    stop_.store(false);
    CpuApplicationSnapshot snapshot;
    snapshot.run_id = next_id_++;
    snapshot.state = State::kPreparing;
    snapshot.test = request.test;
    snapshot.requested_threads = request.requested_threads;
    Publish(snapshot);
    *run_id = snapshot.run_id;
    try {
      thread_ = std::thread(&Impl::Run, this, request, snapshot);
    } catch (...) {
      snapshot.state = State::kError;
      snapshot.error_code = -11;
      Publish(snapshot);
      return -11;
    }
    return 0;
  }
  std::int32_t Stop(std::uint64_t id) {
    std::lock_guard<std::mutex> control(control_mutex_);
    const auto snapshot = GetSnapshot();
    if (id == 0 || id != snapshot.run_id)
      return -1;
    if (Running(snapshot.state))
      stop_.store(true);
    return 0;
  }
  CpuApplicationSnapshot GetSnapshot() const {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return snapshot_;
  }

private:
  void Publish(const CpuApplicationSnapshot &snapshot) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    snapshot_ = snapshot;
  }
  void Run(CpuApplicationRequest request, CpuApplicationSnapshot snapshot) {
    std::vector<std::unique_ptr<Worker>> workers;
    std::vector<std::thread> threads;
    Gates gate;
    std::unique_ptr<PerformanceHintSession> hint;
    try {
      const auto cpus = SelectCpuApplicationCpus(
          DetectTopology(), request.requested_threads == 0);
      if (cpus.empty())
        throw std::invalid_argument("Unavailable CPU selection");
      for (auto cpu : cpus) {
        if (stop_.load())
          break;
        workers.push_back(std::make_unique<Worker>(cpu, request.test));
      }
      if (!stop_.load()) {
        snapshot.thread_count = static_cast<std::uint32_t>(workers.size());
        snapshot.input_bytes = workers.front()->workload.InputBytes();
        snapshot.selected_cpu = request.requested_threads == 1
                                    ? static_cast<std::int32_t>(cpus.front())
                                    : -1;
        snapshot.flags =
            request.requested_threads != 1 ? kApplicationIndependentWorkers : 0;
        Publish(snapshot);
        threads.reserve(workers.size());
        for (auto &worker : workers)
          threads.emplace_back(RunWorker, worker.get(), &gate, &stop_);
        while (!stop_.load() && gate.ready.load() < workers.size())
          std::this_thread::sleep_for(2ms);
        if (!stop_.load()) {
          std::vector<std::int32_t> tids;
          for (auto &worker : workers)
            tids.push_back(worker->tid.load());
          // Desired batch latency; reports below use observed batch durations.
          hint = std::make_unique<PerformanceHintSession>(tids, 1'000'000);
          hint->RequestMaximumPerformance();
          if (hint->IsActive() ||
              std::any_of(workers.begin(), workers.end(), [](const auto &w) {
                return w->performance_requested.load();
              }))
            snapshot.flags |= kApplicationPerformanceRequested;
          snapshot.state = State::kWarmingUp;
          Publish(snapshot);
          gate.warm_deadline =
              Clock::now() + std::chrono::milliseconds(request.warmup_ms);
          gate.phase.store(1, std::memory_order_release);
          while (!stop_.load() && gate.warm_done.load() < workers.size())
            std::this_thread::sleep_for(2ms);
        }
        if (!stop_.load()) {
          snapshot.state = State::kMeasuring;
          gate.start = Clock::now() + 2ms;
          gate.deadline =
              gate.start + std::chrono::milliseconds(request.duration_ms);
          gate.phase.store(2, std::memory_order_release);
          auto last_hint = Clock::now();
          while (gate.done.load() < workers.size()) {
            const auto now = Clock::now();
            snapshot.elapsed_ns = now > gate.start ? Ns(now - gate.start) : 0;
            snapshot.completed_units = 0;
            for (auto &worker : workers)
              snapshot.completed_units +=
                  worker->units.load(std::memory_order_relaxed);
            snapshot.units_per_second =
                snapshot.elapsed_ns
                    ? snapshot.completed_units * 1e9 / snapshot.elapsed_ns
                    : 0;
            snapshot.progress = std::min(1.0, snapshot.elapsed_ns /
                                                  (request.duration_ms * 1e6));
            Publish(snapshot);
            if (now - last_hint >= 100ms) {
              std::uint64_t actual_batch_ns = 0;
              for (auto &worker : workers)
                actual_batch_ns =
                    std::max(actual_batch_ns, worker->last_batch_ns.load());
              if (actual_batch_ns != 0)
                hint->ReportActualDuration(
                    static_cast<std::int64_t>(actual_batch_ns));
              last_hint = now;
            }
            std::this_thread::sleep_for(20ms);
          }
        }
      }
    } catch (const std::bad_alloc &) {
      Fail(&gate, &stop_, -31);
    } catch (const std::invalid_argument &) {
      Fail(&gate, &stop_, -10);
    } catch (const std::system_error &) {
      Fail(&gate, &stop_, -11);
    } catch (...) {
      Fail(&gate, &stop_, -32);
    }
    // On any preparation/creation failure, all waiters observe stop_ and exit.
    for (auto &thread : threads)
      if (thread.joinable())
        thread.join();
    snapshot.completed_units = snapshot.elapsed_ns = snapshot.checksum = 0;
    bool validated = !workers.empty();
    for (auto &worker : workers) {
      // No worker may still be measuring while verification consumes CPU/cache.
      if (worker->units.load() != 0) {
        worker->validated = worker->workload.Validate();
        worker->digest = worker->workload.Digest();
        if (!worker->validated)
          Fail(&gate, &stop_, -30);
      }
      snapshot.completed_units += worker->units.load();
      snapshot.elapsed_ns =
          std::max(snapshot.elapsed_ns, worker->elapsed.load());
      snapshot.affinity_failures += worker->affinity_failed.load() ? 1 : 0;
      snapshot.checksum =
          (snapshot.checksum * 1099511628211ULL) ^ worker->digest;
      if (worker->units.load() != 0)
        validated = validated && worker->validated;
    }
    snapshot.error_code = gate.error.load();
    if (!stop_.load() && (!validated || snapshot.completed_units == 0))
      snapshot.error_code = -30;
    snapshot.units_per_second =
        snapshot.elapsed_ns
            ? snapshot.completed_units * 1e9 / snapshot.elapsed_ns
            : 0;
    if (validated && snapshot.completed_units > 0)
      snapshot.flags |= kApplicationValidated;
    snapshot.state = snapshot.error_code != 0 ? State::kError
                     : stop_.load()           ? State::kCancelled
                                              : State::kCompleted;
    if (snapshot.state == State::kError) {
      snapshot.units_per_second = 0;
      snapshot.flags &= ~kApplicationValidated;
    }
    snapshot.progress =
        snapshot.state == State::kCompleted
            ? 1.0
            : std::min(1.0, snapshot.elapsed_ns / (request.duration_ms * 1e6));
    hint.reset();
    workers.clear(); // Release corpus buffers before unlocking the next module.
    Publish(snapshot);
  }
  mutable std::mutex snapshot_mutex_;
  std::mutex control_mutex_;
  CpuApplicationSnapshot snapshot_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::uint64_t next_id_ = 1;
};
CpuApplicationEngine::CpuApplicationEngine()
    : impl_(std::make_unique<Impl>()) {}
CpuApplicationEngine::~CpuApplicationEngine() = default;
std::int32_t CpuApplicationEngine::Start(const CpuApplicationRequest &request,
                                         std::uint64_t *id) {
  return impl_->Start(request, id);
}
std::int32_t CpuApplicationEngine::RequestStop(std::uint64_t id) {
  return impl_->Stop(id);
}
CpuApplicationSnapshot CpuApplicationEngine::GetSnapshot() const {
  return impl_->GetSnapshot();
}
} // namespace benchmark
