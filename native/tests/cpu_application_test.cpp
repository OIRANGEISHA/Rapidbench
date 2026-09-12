#include "benchmark/cpu_application_engine.h"
#include "benchmark/topology.h"
#include "cpu_application_ffi.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace benchmark;
void Check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void Kernels() {
  for (auto test : {CpuApplicationTest::kSort, CpuApplicationTest::kJson,
                    CpuApplicationTest::kSobel}) {
    CpuApplicationWorkload workload(test);
    Check(!workload.Validate(), "unwritten output accepted");
    Check(workload.RunBatch() == workload.UnitsPerBatch(), "incorrect units");
    Check(workload.Validate(), "reference mismatch");
    const auto digest = workload.Digest();
    Check(digest != 0 && workload.InputBytes() > 0, "invalid corpus");
    for (int repeat = 0; repeat < 3; ++repeat) {
      workload.RunBatch();
      Check(workload.Validate() && workload.Digest() == digest,
            "non-repeatable workload");
    }
    std::cout << "kernel " << static_cast<unsigned>(test)
              << " input=" << workload.InputBytes() << " digest=" << digest
              << '\n';
  }
  JsonRecordSummary parsed;
  Check(
      ParseApplicationJson(
          " [ { \"id\" : 9, \"value\" : 17, \"active\":true,\"name\":\"\"} ] ",
          &parsed),
      "small JSON");
  Check(parsed.rows == 1 && parsed.id_sum == 9 && parsed.value_sum == 17 &&
            parsed.active_count == 1 &&
            parsed.name_hash == 14695981039346656037ULL,
        "JSON small golden");
  Check(ParseApplicationJson("[]", &parsed) && parsed.rows == 0, "empty JSON");
  for (const char *bad :
       {"", "[", "[{}]", "[]x", "[true]", "[{},]",
        "[{\"id\":01,\"value\":0,\"active\":true,\"name\":\"a\"}]",
        "[{\"id\":18446744073709551616,\"value\":0,\"active\":true,\"name\":"
        "\"a\"}]",
        "[{\"id\":0,\"value\":0,\"active\":true,\"name\":\"a\\nb\"}]",
        "[{\"id\":-1,\"value\":0,\"active\":true,\"name\":\"a\"}]"}) {
    Check(!ParseApplicationJson(bad, &parsed), "malformed JSON accepted");
  }
  std::vector<std::uint8_t> pixels{0, 1, 2, 0, 1, 2, 0, 1, 2}, out(9, 99);
  ApplicationSobel(pixels, 3, 3, &out);
  Check(out == std::vector<std::uint8_t>({0, 0, 0, 0, 8, 0, 0, 0, 0}),
        "Sobel ramp golden");
  bool threw = false;
  try {
    ApplicationSobel(pixels, 3, 3, &pixels);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  Check(threw, "Sobel alias accepted");
}
void AutomaticSelection() {
  for (unsigned count : {1U, 8U, 10U, 12U, 64U, 65U}) {
    Topology topology;
    for (unsigned index = 0; index < count; ++index) {
      CpuInfo cpu;
      cpu.logical_cpu = index * 2; // Sparse IDs must not become worker indexes.
      cpu.performance_group = index % 3;
      cpu.capacity = 512 + index;
      cpu.max_frequency_khz = 4000000 - index * 10000;
      // Deliberately leave online/allowed false: cpusets must not truncate
      // Multi.
      topology.cpus.push_back(cpu);
    }
    AssignPerformanceGroups(&topology);
    const auto single = SelectCpuApplicationCpus(topology, false);
    Check(single == std::vector<std::uint32_t>{(count - 1) * 2},
          "automatic Single ignored highest capacity");
    const auto multi = SelectCpuApplicationCpus(topology, true);
    Check(count > 64 ? multi.empty() : multi.size() == count,
          "automatic Multi truncated present CPUs");
    for (unsigned index = 0; index < multi.size(); ++index)
      Check(multi[index] == index * 2, "automatic Multi lost sparse CPU ID");
    for (auto &cpu : topology.cpus)
      cpu.capacity = 0;
    AssignPerformanceGroups(&topology);
    Check(SelectCpuApplicationCpus(topology, false) ==
              std::vector<std::uint32_t>{0},
          "automatic Single frequency fallback failed");
  }
  Topology topology;
  Check(SelectCpuApplicationCpus(topology, false).empty() &&
            SelectCpuApplicationCpus(topology, true).empty(),
        "empty topology invented cores");
  CpuInfo cpu;
  cpu.logical_cpu = 9;
  topology.cpus = {cpu, cpu};
  topology.preferred_single_cpu = 9;
  Check(SelectCpuApplicationCpus(topology, true) ==
            std::vector<std::uint32_t>{9},
        "duplicate CPU spawned extra workers");
  topology.preferred_single_cpu = 8;
  Check(SelectCpuApplicationCpus(topology, false).empty(),
        "unavailable preferred core invented a target");
}
CpuApplicationSnapshot Wait(CpuApplicationEngine &engine) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto snapshot = engine.GetSnapshot();
    if (snapshot.state == State::kCompleted ||
        snapshot.state == State::kCancelled || snapshot.state == State::kError)
      return snapshot;
    Check(std::isfinite(snapshot.units_per_second), "non-finite live rate");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  throw std::runtime_error("engine timeout");
}
void Runs(bool full) {
  CpuApplicationEngine engine;
  const auto topology = DetectTopology();
  Check(!topology.cpus.empty(), "topology unavailable");
  for (auto test : {CpuApplicationTest::kSort, CpuApplicationTest::kJson,
                    CpuApplicationTest::kSobel}) {
    for (unsigned threads : {1U, 0U}) {
      CpuApplicationRequest request;
      request.test = test;
      request.requested_threads = threads;
      request.duration_ms = full ? 3000 : 150;
      request.warmup_ms = full ? 700 : 30;
      std::uint64_t id = 0, busy = 0;
      Check(engine.Start(request, &id) == 0, "start failed");
      Check(engine.Start(request, &busy) == -2, "busy start allowed");
      const auto result = Wait(engine);
      Check(result.state == State::kCompleted && result.error_code == 0,
            "run failed");
      Check((result.flags & kApplicationValidated) != 0 &&
                result.completed_units > 0 && result.elapsed_ns > 0,
            "not validated");
      Check(result.thread_count == (threads == 1 ? 1 : topology.cpus.size()),
            "worker truncation");
      Check(result.selected_cpu ==
                (threads == 1 ? topology.preferred_single_cpu : -1),
            "automatic single target mismatch");
      const double expected = result.completed_units * 1e9 / result.elapsed_ns;
      Check(std::abs(result.units_per_second / expected - 1) < 1e-12,
            "denominator mismatch");
      // The last completion can precede the deadline by loop-check overhead;
      // the denominator must be its actual time, not necessarily the deadline.
      Check(result.elapsed_ns >= (request.duration_ms - 10) * 1000000ULL,
            "measurement too short");
      std::cout << "run=" << id << " test=" << static_cast<unsigned>(test)
                << " workers=" << result.thread_count
                << " target_cpu=" << result.selected_cpu
                << " rate=" << result.units_per_second
                << " elapsed_ns=" << result.elapsed_ns
                << " affinity=" << result.affinity_failures << '\n';
    }
  }
  CpuApplicationRequest request;
  request.duration_ms = 10000;
  request.warmup_ms = 0;
  std::uint64_t id = 0;
  Check(engine.Start(request, &id) == 0, "cancel start");
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (engine.GetSnapshot().completed_units == 0 &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  Check(engine.RequestStop(id + 1) == -1, "wrong run stop accepted");
  Check(engine.RequestStop(id) == 0, "stop failed");
  const auto stopped = Wait(engine);
  Check(stopped.state == State::kCancelled && stopped.units_per_second > 0,
        "partial result lost");
  request.duration_ms = 150;
  Check(engine.Start(request, &id) == 0 &&
            engine.GetSnapshot().completed_units == 0,
        "restart did not clear");
  Check(Wait(engine).state == State::kCompleted, "restart failed");
  Check(engine.Start(request, &id) == 0 && engine.RequestStop(id) == 0 &&
            Wait(engine).state == State::kCancelled,
        "preparation stop failed");
  for (unsigned invalid : {2U, 8U, 65U}) {
    request.requested_threads = invalid;
    Check(engine.Start(request, &id) == -1, "manual worker count accepted");
  }
}
void Ffi() {
  Check(bm_cpu_application_create(nullptr) == BM_STATUS_INVALID_ARGUMENT,
        "null create");
  bm_cpu_application_handle handle = nullptr;
  Check(bm_cpu_application_create(&handle) == 0, "ffi create");
  bm_cpu_application_request_v1 request{
      sizeof(request), BM_ABI_VERSION, 2, 150, 20, 1};
  std::uint64_t id = 0;
  request.struct_size = 32; // Reject the earlier, unreleased selector layout.
  Check(bm_cpu_application_start(handle, &request, &id) ==
            BM_STATUS_INVALID_ARGUMENT,
        "obsolete request layout accepted");
  request.struct_size = sizeof(request);
  ++request.abi_version;
  Check(bm_cpu_application_start(handle, &request, &id) ==
            BM_STATUS_ABI_MISMATCH,
        "ABI mismatch accepted");
  request.abi_version = BM_ABI_VERSION;
  Check(bm_cpu_application_start(handle, &request, &id) == 0, "ffi start");
  bm_cpu_application_snapshot_v1 snapshot{};
  snapshot.struct_size = sizeof(snapshot);
  snapshot.abi_version = BM_ABI_VERSION;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  do {
    Check(bm_cpu_application_snapshot(handle, &snapshot) == 0, "ffi snapshot");
    if (snapshot.state >= 4)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < deadline);
  Check(snapshot.state == 4 && snapshot.method_version == 1 &&
            snapshot.flags & kApplicationValidated,
        "ffi result");
  Check(bm_cpu_application_destroy(handle) == 0, "ffi destroy");
}
} // namespace
int main(int argc, char **argv) {
  try {
    Kernels();
    Runs(argc > 1 && std::string(argv[1]) == "--full");
    AutomaticSelection();
    Ffi();
    std::cout
        << "CPU application kernels, engine, cancellation, automatic cores and "
           "FFI passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
