#include "benchmark/engine.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

int main() {
  benchmark::Engine engine;
  std::atomic<bool> done{false};
  std::atomic<bool> failed{false};
  std::vector<std::thread> observers;
  for (int observer = 0; observer < 2; ++observer) {
    observers.emplace_back([&] {
      std::uint64_t previous_run = 0, previous_work = 0;
      while (!done.load(std::memory_order_acquire)) {
        const auto snapshot = engine.GetSnapshot();
        if (!std::isfinite(snapshot.current_value) ||
            (snapshot.run_id == previous_run &&
             snapshot.completed_work < previous_work)) {
          failed.store(true, std::memory_order_release);
          std::cerr << "Invalid publication run=" << snapshot.run_id
                    << " work=" << snapshot.completed_work << " previous=" << previous_work
                    << " score=" << snapshot.current_value << '\n';
        }
        previous_run = snapshot.run_id;
        previous_work = snapshot.completed_work;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    });
  }
  std::uint32_t completed_runs = 0;
  for (std::uint32_t run = 0; run < 12U; ++run) {
    benchmark::Request request;
    request.test_id = run % 2U == 0U ? benchmark::TestId::kCpuSingle
                                     : benchmark::TestId::kCpuMulti;
    request.duration_ms = 120U;
    request.warmup_ms = 20U;
    std::uint64_t id = 0;
    if (engine.Start(request, &id) != 0) {
      std::cerr << "Start failed run=" << run << '\n';
      failed.store(true, std::memory_order_release);
      break;
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
      const auto snapshot = engine.GetSnapshot();
      if (snapshot.state == benchmark::State::kCompleted) {
        if (snapshot.run_id != id || snapshot.current_value <= 0.0 ||
            snapshot.completed_work == 0 ||
            snapshot.peak_score < snapshot.current_value) {
          std::cerr << "Invalid terminal run=" << id << " work=" << snapshot.completed_work
                    << " score=" << snapshot.current_value << " peak=" << snapshot.peak_score
                    << " elapsed=" << snapshot.elapsed_ns << '\n';
          failed.store(true, std::memory_order_release);
        }
        ++completed_runs;
        break;
      }
      if (snapshot.state == benchmark::State::kError) {
        std::cerr << "Engine error=" << snapshot.error_code << " run=" << id << '\n';
        failed.store(true, std::memory_order_release);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (completed_runs != run + 1U) {
      failed.store(true, std::memory_order_release);
      engine.RequestStop(id);
      break;
    }
  }
  done.store(true, std::memory_order_release);
  for (auto &observer : observers)
    observer.join();
  if (failed.load(std::memory_order_acquire) || completed_runs != 12U)
    return 1;
  std::cout << "CPU publication stress: 12 single/multi runs with concurrent "
               "observers passed\n";
  return 0;
}
