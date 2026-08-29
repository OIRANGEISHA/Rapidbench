# RapidBench

[简体中文](README.zh-CN.md)

Current channel: **1.0.1 Beta 2 / Preview**.

RapidBench is a native Android benchmark for a quick assessment of device performance and a compact overview of CPU and GPU capability support. It combines short, repeatable CPU, memory, storage, and Vulkan compute tests with hardware topology, Arm ISA, and Vulkan feature reporting.

It is intended for fast device checks, tuning comparisons, and regression testing. It is not a substitute for controlled laboratory measurements.

## What RapidBench measures

| Area | Tests and information |
| --- | --- |
| CPU | Selectable single core, dynamically detected multi-core clusters or all cores, sustained and peak score, affinity checks, utilization, and per-cluster peak frequency |
| Memory | Multi-thread read, write, and bidirectional-traffic system `memcpy()` bandwidth |
| Storage | Sequential read/write, 4 KiB Q1T1, 4 KiB Q8T1, 4 KiB Q1T4, SQLite insert, and SQLite delete |
| GPU | Vulkan FP32, native or emulated FP16, INT32, mixed compute, and GPU memory bandwidth |
| Device | CPU topology, maximum frequencies, capacity groups, kernel-reported Arm ISA level, HWCAP/HWCAP2 instruction features, memory information, and Vulkan features/extensions |

Each benchmark card can be run individually. CPU and multi-core selectors are generated from the current device instead of assuming a fixed number of clusters.

## Implementation

The interface is written in Flutter. Benchmark execution is implemented in C++17 and exposed to Dart through a stable FFI layer. Storage receives an Android app-private benchmark directory through a platform channel. The measurement loops stay in native code so UI polling does not become part of the timed workload.

```text
Flutter UI
  -> Dart controllers and FFI bindings
  -> native C ABI
  -> C++ benchmark engines
  -> CPU threads / libc / Linux AIO / SQLite / Vulkan compute
```

### CPU algorithm

- RapidBench reads present and online CPUs, affinity masks, capacity, cluster IDs, global CPUFreq policies, and maximum frequency data at runtime. It groups CPUs by observed performance characteristics, including kernels that do not expose per-CPU CPUFreq links.
- A single-thread test tries to pin one worker to the selected logical CPU. A multi-thread test creates one worker for every present CPU in the selected group or across the device. If Android refuses an affinity request, the worker keeps producing load, periodically retries pinning, and reports an affinity warning instead of aborting the run.
- Where Android exposes it, the engine requests maximum-performance scheduling through performance hints and reports actual work duration during the run.
- Every worker owns deterministic state containing 128 32-bit integers and 128 floating-point values. One batch executes eight rounds of integer avalanche operations (add, xor, multiply, shift, and rotate), vectorizable floating-point recurrences, and a checksum reduction. The checksum keeps the results observable and prevents the workload from being optimized away.
- CPU tests use a 2.5-second warm-up followed by a 10-second measurement.
- Native throughput is calculated from completed fixed work units and elapsed time. The UI reports millions of fixed work units per second. The measurement is split into three equal windows; the median window is the final score, the fastest window is the peak score, and a variation flag is raised when window spread exceeds 7%.

The CPU number is meaningful only within the same RapidBench workload version. It is not a synthetic conversion to another benchmark's score.

### Memory algorithm

- The engine uses all currently allowed CPUs and divides aligned buffers into independent regions per worker.
- The working set targets 256 MiB, is capped to one eighth of available memory, and falls back as low as 32 MiB when allocation pressure requires it.
- Read uses an unrolled native load-and-reduction kernel. Write uses an unrolled 128-byte store pattern. Copy calls the platform's multi-threaded system `memcpy()` path on non-overlapping regions.
- Copy reports total memory-system traffic: copied payload is counted once for the source read and once for the destination write. A 20 GB/s payload copy is therefore shown as approximately 40 GB/s of bidirectional traffic.
- Workers synchronize before warm-up and measurement. The default measurement is 3 seconds after a 1-second warm-up.

### Storage algorithm

- Tests operate on a prepared file in Android app-private storage. RapidBench tries `O_DIRECT` with aligned buffers and reports when it must use a buffered fallback. Cache-drop advice is issued where available.
- Sequential tests use 1 MiB blocks at Q1T1. Random tests use 4 KiB blocks in deterministic shuffled order.
- Q1T1 performs synchronous pread/pwrite operations. Q8T1 uses native Linux AIO with eight requests kept outstanding and validates that queue depth 8 was actually reached. Q1T4 uses four native worker threads over separate file regions.
- Write tests flush with `fdatasync()` after the timed phase; flush time is reported separately from throughput.
- SQLite tests use an indexed table and a 512-byte payload. Insert and delete operations are committed in 500-row immediate transactions; delete order is shuffled deterministically.
- Storage tests use a 750 ms warm-up and a 3-second measurement by default.

### GPU algorithm

- RapidBench loads Vulkan dynamically, selects a compute-capable queue, and prefers a queue with timestamps and without graphics duties when available.
- Compute dispatches use 1,024 workgroups of 64 invocations. FP shaders run 64 iterations over independent `vec4` accumulator chains.
- FP32, native FP16, and emulated FP16 provide 8-, 12-, and 16-accumulator variants. During warm-up, RapidBench measures available variants in forward and reverse order and selects the highest-throughput version for that GPU. No Adreno, Mali, Xclipse, PowerVR, or other vendor-name whitelist is used.
- The 12- and 16-accumulator pipelines are optional. If a driver rejects them, the test falls back to the required 8-accumulator pipeline. Native FP16 is enabled only when Vulkan reports `shaderFloat16`; otherwise the compatible emulated path is used.
- A 16-region output ring lets consecutive dispatches write independent ranges. A compute barrier is inserted only before a region is reused, reducing unnecessary dispatch serialization while preserving correctness.
- FLOPS/GOPS are derived from the exact operation count of the selected shader. Supported GPU timestamps are accepted only when they remain plausible against the independently measured host fence duration; invalid or implausible samples use the declared host-timing fallback.
- Each GPU item measures for approximately 6 seconds after a 700 ms warm-up.

### Device capability reporting

- Arm ISA level comes directly from the kernel's `/proc/cpuinfo` `CPU architecture` value. RapidBench does not infer a minor ISA revision such as Armv8.7-A from unrelated feature bits. Mixed or missing reports remain explicitly unknown.
- Supported instruction features are read independently from Linux `AT_HWCAP` and `AT_HWCAP2`, including NEON/ASIMD, AES, SHA, CRC32, LSE, FP16, DotProd, SVE/SVE2, BF16, I8MM, and other flags exposed by the device.
- Vulkan reporting enumerates the API and driver version, device extensions, and supported features such as shader FP16/int8, descriptor indexing, timeline semaphores, buffer device address, dynamic rendering, synchronization2, fragment shading rate, ray query/tracing, acceleration structures, and mesh shaders when the driver exposes them.

## Compatibility

- Android 7.0 / API 24 or newer.
- The published APK is arm64-v8a only.
- CPU, memory, storage, and device information remain usable without Vulkan. GPU tests require a Vulkan compute-capable device.
- The implementation uses standard Android/Linux and Vulkan interfaces and avoids GPU vendor-specific model checks. Real-device validation for this release was performed on a PJZ110 with Adreno 830; other GPU families should still be tested on their actual drivers.

## Build

Requirements:

- Flutter stable
- JDK 17
- Android SDK
- Android NDK with `glslc`
- CMake and Ninja

```powershell
cd app/benchmark_app
flutter pub get
flutter analyze
flutter build apk --release --target-platform android-arm64
```

The generated APK is located at:

```text
app/benchmark_app/build/app/outputs/flutter-apk/app-release.apk
```

### Signing note

The current repository config builds an optimized, minified Release variant but signs it with the Android debug key for direct testing. Before Play Store distribution or any trusted production release, replace the `release` signing configuration with your own protected release keystore. Never commit signing passwords or private keys.

## Interpreting results

- Keep the device cool, battery level sufficient, and screen state consistent.
- Close heavy background tasks and run the same RapidBench version when comparing devices.
- Thermal limits, power mode, firmware scheduling, filesystem state, and vendor drivers can change results.
- Peak values show the fastest measurement window; sustained values are better for repeatable comparisons.
- Storage and memory unit conventions are stated in the UI. Copy bandwidth intentionally counts both read and write traffic.

## Privacy

RapidBench does not request network permission. Storage benchmark files and SQLite databases are created inside app-private storage and removed by the benchmark engine. Benchmark results remain on the device unless the user exports or shares them externally.
