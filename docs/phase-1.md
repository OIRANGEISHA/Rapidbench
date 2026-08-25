# Phase 1: Native execution path

## Scope

Phase 1 proves that Flutter can start and stop work owned by a native C++
thread, then poll a coherent native snapshot without callbacks from the
benchmark loop.

The phase deliberately excludes:

- publishable CPU scoring;
- CPU topology and affinity;
- compression, crypto, cache, and memory benchmarks;
- device and thermal information;
- result persistence.

## State machine

```text
idle -> preparing -> warming_up -> measuring -> completed
                                      |             
                                      +-> cancelled
```

An engine accepts one active run. Every accepted run receives a monotonically
increasing run ID. Stop is cooperative and is observed at native batch
boundaries.

## Acceptance checklist

- [ ] Native host smoke test builds and passes.
- [ ] Android release build loads `libbenchmark_ffi.so`.
- [ ] Work runs on a native worker thread.
- [ ] Flutter polls snapshots every approximately 200 ms.
- [ ] Snapshot run ID, elapsed time, and completed work never move backward.
- [ ] Start/stop can be repeated 20 times without a crash or stale run data.
- [ ] UI stays responsive during warm-up and measurement.
- [ ] Stop reaches a terminal state within 250 ms on the validation device.
- [ ] Thread creation and warm-up are outside the measured interval.

## Non-goal warning

The Phase 1 score is only `million placeholder operations per second`. It is a
transport and lifecycle signal, not a CPU benchmark result.

