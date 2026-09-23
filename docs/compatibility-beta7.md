# 1.1.1 Beta 7 compatibility work

Status: implementation and connected-device Debug regression complete. Release
validation and publication are tracked in [the release record](releases/1.1.1-beta.7.md).
Android version: `1.1.1-beta.7`, versionCode `4021`.

## Scope and preserved behavior

- No SoC/GPU model whitelist, vendor score multiplier or vendor-specific compiler
  target was introduced. CPU application workloads still choose automatic Single
  or all-present-core Multi, without core/cluster selectors.
- Legacy CPU main scoring, Memory read/write/system `memcpy()` payload accounting,
  all 11 Storage routes and GPU operation counting are preserved.
- STNP128 remains the generic AArch64 write kernel; it is a hint, not a promise
  that every memory system avoids read-for-ownership traffic.
- GPU workgroup size stays 64 and existing 8/12/16 accumulator autotuning remains.
  The 70–110% host/GPU timing acceptance rule remains unchanged. Changing these
  defaults needs controlled cross-device evidence and a separate method review.
- No dependency, signing, package ID or published ABI structure changes. New GPU
  and Memory JSON diagnostics functions extend the C API without resizing v1
  snapshots. GPU method remains `gpu-throughput-v2`.

## Implementation

### CPU topology

Use a common ranking source across detected CPUs: complete capacity data first,
complete maximum frequencies second, otherwise a deterministic unknown fallback.
Frequency selection is labelled inferred; missing information is not presented
as proof that a selected CPU is fastest. Physical cluster/policy metadata remains
preferred when useful. Constant IDs no longer collapse distinct measured ranks
into one performance group. Groups inferred from ranks are not claims about
physical cluster wiring.

### GPU compatibility and diagnostics

Create baseline pipelines independently. Unsupported tests have an unavailable
state; All skips them while usable tests remain runnable. Native-FP16 pipeline
rejection can use an explicitly emulated FP32 path if available. Optional floating
point variants may be absent without invalidating their baseline variant.
Numerical validation failures still fail the active item and stop the current
sequence; they are not silently converted into valid results. Queue/submission
faults and device loss block reuse until the App restarts.

Each item's diagnostics retain run ID, state, reason, accumulator count, summed
host time, raw positive finite GPU time observations, and host/timestamp batch
counts. Raw GPU observations include rejected timing samples and must not be
interpreted as validated GPU elapsed time. Timing summary includes every measured
batch in the current run rather than only its final batch. Old per-item results
retain their own run IDs.

### Memory CPU availability

Choose online/allowed CPUs, allocate and warm workers, then recheck from the
unpinned coordinator. If membership changed, join those workers and prepare a
new set. Two retries maximum avoid endless warm-up under changing cpusets/hotplug.
The final attempt freezes its worker set and flags continued instability. No
thread-count change occurs inside a scored interval; preparation bytes never
enter the result. Present, online, allowed and actual worker counts are retained.
This cannot override Android cpusets, thermal limits or permission restrictions.

### Short CPU Peak boundary

The concurrent publication regression reproduced a case where an approximately
120 ms run had insufficient score windows. The main score correctly fell back
to whole-run throughput, but Peak retained only incomplete windows and was lower.
Peak now also includes the measured whole-run fallback in this case and on stop.
The normal window-median main score and CPU workload are unchanged.

## Verification ledger

Host toolchain and artifacts stay under `H:\test`. Native executables use the
existing Android ARM64 toolchain. The physically connected test device is OnePlus
PJZ110 / Snapdragon 8 Elite / Adreno 830. No other physical device was tested.

| Check | Evidence / status |
| --- | --- |
| Native Android compilation | Passed; existing Vulkan aggregate-initializer warnings remain |
| Topology regression | Passed on device; synthetic 4/6/8/10/12-core, sparse IDs, missing/partial metadata, constant IDs and restricted masks |
| Memory engine | Read/write/copy, stop/restart, injected delayed availability, unstable topology retry bound and permanent restriction passed |
| CPU publication stress | Initial intermittent failure recorded, then 10 × 12 single/multi runs passed after Peak fix |
| CPU application tests | Sort/JSON/Sobel single and all-core, reference checks, FFI and cancellation passed |
| Storage routes | All 11 passed, including direct sequential QD8, 4K Q1T1/Q8T1/Q1T4 and SQLite Insert/Update/Delete; buffered routing is a simulated check |
| GPU full native run | Each item 6 seconds, completed with validation; stop/restart cleared stale scores |
| GPU injected pipeline failures | Native FP16 rejection, all FP16 unavailable, optional 12/16 rejection, all pipelines unavailable and injected device loss passed |
| Flutter tests | 28 passed, including 320px / 2x-font GPU failure cards and large values; static analysis clean |
| Debug App installation | Final package installed over Beta 6 without clearing data; device reports `1.1.1-beta.7` / `4021`, DEBUGGABLE |
| App module UI regression | CPU cross-module exclusion, background stop, Memory stop/restart/full sequence, all 11 Storage items and all GPU items passed; no process crash or Flutter overflow |
| CPU application UI | All six Single/Multi cases verified; stop/restart, background stop, independent automatic selection despite legacy CPU/cluster changes passed; no crash/overflow |
| About version | Device and About pages display `1.1.1-beta.7` / `4021` with the correct project URL; XML and screenshot retained |
| APK contents | Both diagnostic exports present; fault-injection function and messages absent |

GPU injection is compiled only into `gpu_pipeline_fallback_test`, not into the
App library. It runs real available Vulkan workloads on the connected device
while deliberately rejecting selected pipeline creation paths. This demonstrates
our fallback control flow, not behavior of a particular untested GPU driver.

Local evidence: `artifacts/beta7-native-device-tests.log`,
`artifacts/beta7-cpu-publication-diagnostic.log`,
`artifacts/beta7-native-device-followup.log` (initial reproduced failure),
`artifacts/beta7-native-device-final.log`,
`artifacts/beta7-gpu-injected-fallback.log`, and
`artifacts/beta7-flutter-tests.log`, `artifacts/beta7-debug-ui-test.log`,
`artifacts/beta7-debug-applications-ui.log`, and `artifacts/beta7-debug-about.xml`.
The full module UI suite preceded a behavior-neutral result-card class visibility
change for widget tests; the final rebuilt/reinstalled APK then passed the CPU
application UI suite and About checks. These local artifacts are not public release
assets. Short native regression rates are correctness evidence, not advertised
device performance scores.

Final Debug artifact: `artifacts/RapidBench-1.1.1-beta.7-arm64-v8a-debug.apk`
(80,199,880 bytes). SHA-256:
`4fec2333b2d91454ef1e1608fb2429ab3778ae359f619998bfd99d972a8fbbd0`.
This is a Debug QA package, not a size-optimized or published Release asset.

## Remaining cross-device work

Physical tests on older Snapdragon/Kirin, Exynos, MediaTek/Mali/Immortalis and
other GPU drivers remain necessary. Do not claim this device plus injected cases
certifies all devices. Memory latency/scale sweeps, unified export/run metadata,
and sustained/repeated benchmark statistics from the separate four-stage roadmap
remain future work; this compatibility patch does not complete that roadmap.
