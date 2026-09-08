# RapidBench 1.0.4 Beta 5 implementation plan

Date: 2026-09-08
Release owner: OIRANGEISHA
Base: published Beta 4 commit `71c6f9d90b0f4b397f7ed4430e9f8717b68fadd1`
Target: `1.0.4-beta.5+4019`, Android API 24+, arm64-v8a

## Goal and scope

Improve measurement isolation, memory timing correctness, release signing, and
regression coverage without changing the CPU/GPU workloads or Storage test IDs.
Storage retains all 11 tests: sequential read/write, 4K Q1T1 read/write, 4K
Q8T1 read/write, 4K Q1T4 read/write, and SQLite Insert/Update/Delete.

## Ordered implementation

1. [x] Fetch the public source baseline and preserve the previous local history.
   Work on `geisha/beta5-stability`; retain `geisha/beta4-local-backup`.
2. [x] Add an application-wide benchmark lease. Only one module may run at a
   time, including preparation, warm-up, flushing, cancellation, and sequences.
   Navigation remains available. Failed starts, completion, stop, and disposal
   must release ownership safely; a stale owner must not release a newer run.
   Add regression coverage for contention, cancellation, and restart.
3. [x] Fix memory elapsed time to include the actual completion time of every
   counted kernel pass, using worker completion timestamps rather than a
   deadline clamp or coordinator join overhead. Cover overrun and early stop.
   Keep read/write payload counting and bidirectional system memcpy unchanged.
4. [x] Wire explicit release signing to the configured keystore and alias;
   preserve the existing Beta certificate for in-place upgrades. Verify the
   final APK certificate rather than assuming environment validation is enough.
5. [x] Correct English/Chinese documentation: sequential Direct I/O uses QD8;
   Buffered sequential fallback uses Q1T1; 4K Q1T1, Q8T1, and Q1T4 remain
   separate tests. Document timing correction and actual validation boundaries.
6. [x] Run Flutter analysis/tests, native regressions, and meaningful layout
   tests for narrow screens, large numbers, and text scaling. Exercise Storage
   routes and fallback/error reporting without changing their workloads.
7. [x] Build Debug and Release arm64 APKs; inspect version, ABI, debuggability,
   signing, and hashes. Notify the user immediately before phone installation.
   On the connected PJZ110, check module exclusion, stop/restart, memory,
   Storage's 11 tests, CPU/GPU smoke, navigation, and app lifecycle behavior.
8. [x] Record results and remaining limitations, and prepare verified release
   artifacts and notes for the established GitHub pre-release workflow.

## Score compatibility

This patch corrects an elapsed-time accounting defect; it does not introduce a
new scoring formula or workload. Memory results that previously included work
after the deadline with a truncated denominator may decrease. Use Beta 5 for
new memory comparisons and disclose the correction. CPU/GPU and Storage
workloads, units, and result IDs remain unchanged.

## Validation boundary

The available phone is PJZ110 / Snapdragon 8 Elite. Other CPU topologies and GPU
timing failures require regression simulations and must not be described as
real-device validated. No claim of guaranteed maximum clocks or theoretical
DRAM throughput is part of this release. The retained Beta signing certificate
and absence of build attestations remain documented pre-Stable limitations.

## Completion record

Implementation and test evidence are recorded in `docs/releases/1.0.4-beta.5.md`.
This checklist records local release readiness. The final source commit, public
tag, uploader and asset verification are recorded with the GitHub pre-release.
Public Beta 4 tags and assets are immutable and will not be replaced.
