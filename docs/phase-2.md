# Phase 2: CPU Bench

## Scope

Phase 2 replaces the Phase 1 placeholder loop with one versioned CPU workload
used by both Single Thread and Multi Thread. It also adds Linux/Android CPU
topology discovery, best-effort affinity, synchronized worker start, live
snapshots, stop handling, and a score with an explicit unit.

## Workload v1

One work unit is one update of persistent integer and scalar floating-point
state. Every update contains:

- 64-bit shifts, XOR, rotate, addition, and multiplication;
- scalar double-precision multiply/add operations;
- one data-dependent branch;
- a consumed checksum so the optimizer cannot discard the loop.

A batch contains 256 work units. The build remains optimized (`-O3`) but does
not enable fast-math, and implicit floating-point contraction is disabled.
Loop-carried state and explicit Clang loop metadata keep this workload scalar;
NEON and crypto instructions are intentionally outside this CPU Compute score.

## Timing and score

- Worker creation, state allocation, affinity setup, and warm-up are outside
  the measurement interval.
- `std::chrono::steady_clock` supplies the monotonic clock.
- Single warm-up: 800 ms; measured duration: 3,000 ms.
- Multi warm-up: 1,000 ms; measured duration: 3,500 ms.
- All workers receive the same future start timestamp after a barrier.
- Flutter polls a native snapshot every 200 ms.

The displayed unit is **thousands of standardized work units per second**:

```text
score = completed_work / elapsed_seconds / 1000
```

The measured interval is split into three windows. The final score is the
median of their throughputs. `score_variation` is `(max - min) / median`; a
spread above 7% sets a quality flag. This score is only comparable with the
same workload version and is not calibrated to CPU-Z or another product.

## Topology and affinity

The native topology reader intersects online CPUs with the process affinity
mask and reads, when exposed by the kernel:

- logical CPU ID;
- `cpu_capacity`;
- `cpuinfo_max_freq` or `scaling_max_freq`;
- `cluster_id`, with `physical_package_id` as a fallback;
- `core_id`.

Performance groups are ordered by capacity first and maximum frequency second.
If those fields are missing, the result is explicitly marked as inferred.
CPU numbering is never used as a performance signal.

Single Thread selects the highest-ranked allowed online CPU. Multi Thread uses
one worker per allowed online logical CPU unless a smaller thread count is
explicitly requested. Every worker calls `sched_setaffinity()` for its selected
CPU and verifies its resulting mask. Failure does not abort the benchmark, but
it is surfaced as a lower-confidence quality flag.

## Non-root limitations

- Sysfs capacity, maximum frequency, and cluster identifiers may be missing or
  vendor-specific.
- Android may change cpusets or online state around a run.
- Affinity is a request within the app's permitted cpuset; it cannot override
  scheduler, thermal, DVFS, or power policy.
- A chosen prime CPU may be temporarily unavailable or receive less runtime
  than expected.
- No non-root API can lock frequency, silence background tasks, or guarantee a
  fixed thermal state.

These conditions are represented as metadata or quality flags rather than
silently guessed.

## Acceptance

- [x] Native ABI v2 layout compiles for Android ARM64.
- [x] Independent ARM64 smoke test validates topology, Single, Multi, and Stop
  on the connected PJZ110 device.
- [x] Flutter static analysis passes.
- [x] Debug APK builds and packages `libbenchmark_ffi.so`.
- [x] Flutter UI completes a Single -> Multi sequence on an unlocked device.
- [x] Three repeated full runs have reasonable ordering and variation.
- [x] Affinity reports zero failures on the validation device, or the run is
  visibly marked lower confidence.

All Phase 2 acceptance items passed on the connected PJZ110 device. Broader
cross-SoC behavior remains **Needs Device Validation** for Phase 8.
