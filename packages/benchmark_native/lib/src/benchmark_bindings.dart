import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

import 'benchmark_models.dart';

const int bmAbiVersion = 4;
const int bmMaxLogicalCpus = 64;
const int bmMaxPerformanceGroups = 16;
const int bmTestPhase1Workload = 1;
const int bmTestCpuSingle = 2;
const int bmTestCpuMulti = 3;

const int bmRequestFlagSingleCpuExplicit = 1 << 0;
const int bmRequestFlagMultiGroupExplicit = 1 << 1;
const int bmRequestSingleCpuShift = 8;
const int bmRequestMultiGroupShift = 16;

const int bmTelemetryUtilizationMask = 0x3FF;
const int bmTelemetryHintActive = 1 << 10;
const int bmTelemetryAffinityChecked = 1 << 11;
const int bmTelemetryPerformanceRequestActive = 1 << 12;
const int bmTelemetryFrequencyShift = 16;

const int bmCpuFlagOnline = 1 << 0;
const int bmCpuFlagAllowed = 1 << 1;

const int bmQualityCapacityMissing = 1 << 0;
const int bmQualityMaxFrequencyMissing = 1 << 1;
const int bmQualityClusterIdMissing = 1 << 2;
const int bmQualityAffinityUnavailable = 1 << 3;
const int bmQualityTopologyTruncated = 1 << 4;
const int bmQualityPerformanceGroupsInferred = 1 << 5;
const int bmQualityAffinityFailed = 1 << 16;
const int bmQualityThreadCountReduced = 1 << 17;
const int bmQualityHighScoreVariation = 1 << 18;
const int bmQualitySelectionFallback = 1 << 19;
const int bmQualityPerformanceRequestUnavailable = 1 << 20;

final class BmRequestV2 extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int abiVersion;

  @Uint32()
  external int testId;

  @Uint32()
  external int durationMs;

  @Uint32()
  external int warmupMs;

  @Uint32()
  external int requestedThreads;

  @Uint64()
  external int flags;
}

final class BmSnapshotV4 extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int abiVersion;

  @Uint64()
  external int runId;

  @Uint32()
  external int state;

  @Uint32()
  external int phase;

  @Uint32()
  external int testId;

  @Uint32()
  external int threadCount;

  @Uint64()
  external int qualityFlags;

  @Uint64()
  external int elapsedNs;

  @Uint64()
  external int completedWork;

  @Double()
  external double currentValue;

  @Double()
  external double progress;

  @Double()
  external double scoreVariation;

  @Double()
  external double peakScore;

  @Int32()
  external int errorCode;

  @Int32()
  external int selectedCpu;

  @Uint32()
  external int affinityFailures;

  @Uint32()
  external int telemetry;

  @Uint32()
  external int peakFrequencyGroupCount;

  @Uint32()
  external int peakFrequencyMhzGroup0;

  @Uint32()
  external int peakFrequencyMhzGroup1;

  @Uint32()
  external int peakFrequencyMhzGroup2;

  @Uint32()
  external int peakFrequencyMhzGroup3;

  @Uint32()
  external int peakFrequencyMhzGroup4;

  @Uint32()
  external int peakFrequencyMhzGroup5;

  @Uint32()
  external int peakFrequencyMhzGroup6;

  @Uint32()
  external int peakFrequencyMhzGroup7;

  @Uint32()
  external int peakFrequencyMhzGroup8;

  @Uint32()
  external int peakFrequencyMhzGroup9;

  @Uint32()
  external int peakFrequencyMhzGroup10;

  @Uint32()
  external int peakFrequencyMhzGroup11;

  @Uint32()
  external int peakFrequencyMhzGroup12;

  @Uint32()
  external int peakFrequencyMhzGroup13;

  @Uint32()
  external int peakFrequencyMhzGroup14;

  @Uint32()
  external int peakFrequencyMhzGroup15;

  int peakFrequencyMhzForGroup(int group) {
    return switch (group) {
      0 => peakFrequencyMhzGroup0,
      1 => peakFrequencyMhzGroup1,
      2 => peakFrequencyMhzGroup2,
      3 => peakFrequencyMhzGroup3,
      4 => peakFrequencyMhzGroup4,
      5 => peakFrequencyMhzGroup5,
      6 => peakFrequencyMhzGroup6,
      7 => peakFrequencyMhzGroup7,
      8 => peakFrequencyMhzGroup8,
      9 => peakFrequencyMhzGroup9,
      10 => peakFrequencyMhzGroup10,
      11 => peakFrequencyMhzGroup11,
      12 => peakFrequencyMhzGroup12,
      13 => peakFrequencyMhzGroup13,
      14 => peakFrequencyMhzGroup14,
      15 => peakFrequencyMhzGroup15,
      _ => 0,
    };
  }
}

final class BmCpuInfoV1 extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int logicalCpu;

  @Int32()
  external int clusterId;

  @Int32()
  external int coreId;

  @Uint32()
  external int maxFrequencyKhz;

  @Uint32()
  external int capacity;

  @Uint32()
  external int performanceGroup;

  @Uint32()
  external int flags;
}

final class BmTopologyV1 extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int abiVersion;

  @Uint32()
  external int cpuCount;

  @Uint32()
  external int onlineCount;

  @Uint32()
  external int allowedCount;

  @Uint32()
  external int performanceGroupCount;

  @Int32()
  external int preferredSingleCpu;

  @Uint32()
  external int reserved0;

  @Uint64()
  external int qualityFlags;

  @Array(bmMaxLogicalCpus)
  external Array<BmCpuInfoV1> cpus;
}

typedef _GetAbiNative = Uint32 Function();
typedef _GetAbiDart = int Function();
typedef _GetTopologyNative = Int32 Function(Pointer<BmTopologyV1>);
typedef _GetTopologyDart = int Function(Pointer<BmTopologyV1>);
typedef _EngineCreateNative = Int32 Function(Pointer<Pointer<Void>>);
typedef _EngineCreateDart = int Function(Pointer<Pointer<Void>>);
typedef _EngineDestroyNative = Int32 Function(Pointer<Void>);
typedef _EngineDestroyDart = int Function(Pointer<Void>);
typedef _StartNative = Int32 Function(
  Pointer<Void>,
  Pointer<BmRequestV2>,
  Pointer<Uint64>,
);
typedef _StartDart = int Function(
  Pointer<Void>,
  Pointer<BmRequestV2>,
  Pointer<Uint64>,
);
typedef _StopNative = Int32 Function(Pointer<Void>, Uint64);
typedef _StopDart = int Function(Pointer<Void>, int);
typedef _SnapshotNative = Int32 Function(Pointer<Void>, Pointer<BmSnapshotV4>);
typedef _SnapshotDart = int Function(Pointer<Void>, Pointer<BmSnapshotV4>);

final class BenchmarkBindings {
  BenchmarkBindings._(this._library)
    : getAbiVersion = _library.lookupFunction<_GetAbiNative, _GetAbiDart>(
        'bm_get_abi_version',
      ),
      getTopology = _library
          .lookupFunction<_GetTopologyNative, _GetTopologyDart>(
            'bm_get_topology',
          ),
      engineCreate = _library
          .lookupFunction<_EngineCreateNative, _EngineCreateDart>(
            'bm_engine_create',
          ),
      engineDestroy = _library
          .lookupFunction<_EngineDestroyNative, _EngineDestroyDart>(
            'bm_engine_destroy',
          ),
      start = _library.lookupFunction<_StartNative, _StartDart>('bm_start'),
      requestStop = _library.lookupFunction<_StopNative, _StopDart>(
        'bm_request_stop',
      ),
      getSnapshot = _library.lookupFunction<_SnapshotNative, _SnapshotDart>(
        'bm_get_snapshot',
      );

  factory BenchmarkBindings.open() {
    final DynamicLibrary library;
    if (Platform.isAndroid || Platform.isLinux) {
      library = DynamicLibrary.open('libbenchmark_ffi.so');
    } else if (Platform.isWindows) {
      library = DynamicLibrary.open('benchmark_ffi.dll');
    } else if (Platform.isMacOS) {
      library = DynamicLibrary.open('libbenchmark_ffi.dylib');
    } else {
      throw UnsupportedError(
        'Unsupported platform: ${Platform.operatingSystem}',
      );
    }

    final bindings = BenchmarkBindings._(library);
    final actualAbi = bindings.getAbiVersion();
    if (actualAbi != bmAbiVersion) {
      throw StateError(
        'Native ABI mismatch: Dart=$bmAbiVersion, native=$actualAbi',
      );
    }
    return bindings;
  }

  final DynamicLibrary _library;
  final _GetAbiDart getAbiVersion;
  final _GetTopologyDart getTopology;
  final _EngineCreateDart engineCreate;
  final _EngineDestroyDart engineDestroy;
  final _StartDart start;
  final _StopDart requestStop;
  final _SnapshotDart getSnapshot;

  CpuTopology readTopology() {
    final pointer = calloc<BmTopologyV1>();
    try {
      pointer.ref
        ..structSize = sizeOf<BmTopologyV1>()
        ..abiVersion = bmAbiVersion;
      _check(getTopology(pointer), 'bm_get_topology');
      final native = pointer.ref;
      final cpus = <CpuLogicalInfo>[];
      for (var index = 0; index < native.cpuCount; index++) {
        final cpu = native.cpus[index];
        cpus.add(
          CpuLogicalInfo(
            logicalCpu: cpu.logicalCpu,
            online: (cpu.flags & bmCpuFlagOnline) != 0,
            allowed: (cpu.flags & bmCpuFlagAllowed) != 0,
            clusterId: cpu.clusterId,
            coreId: cpu.coreId,
            maxFrequencyKhz: cpu.maxFrequencyKhz,
            capacity: cpu.capacity,
            performanceGroup: cpu.performanceGroup,
          ),
        );
      }
      return CpuTopology(
        cpus: List.unmodifiable(cpus),
        onlineCount: native.onlineCount,
        allowedCount: native.allowedCount,
        performanceGroupCount: native.performanceGroupCount,
        preferredSingleCpu: native.preferredSingleCpu,
        qualityFlags: native.qualityFlags,
      );
    } finally {
      calloc.free(pointer);
    }
  }

  static void _check(int status, String operation) {
    if (status != 0) {
      throw StateError('$operation failed with native status $status');
    }
  }
}

final class NativeBenchmarkEngine {
  NativeBenchmarkEngine(this._bindings) {
    final outEngine = calloc<Pointer<Void>>();
    try {
      _check(_bindings.engineCreate(outEngine), 'bm_engine_create');
      _handle = outEngine.value;
    } finally {
      calloc.free(outEngine);
    }

    _snapshot = calloc<BmSnapshotV4>();
    _snapshot.ref
      ..structSize = sizeOf<BmSnapshotV4>()
      ..abiVersion = bmAbiVersion;
  }

  final BenchmarkBindings _bindings;
  late final Pointer<Void> _handle;
  late final Pointer<BmSnapshotV4> _snapshot;
  bool _disposed = false;

  CpuTopology readTopology() {
    _ensureOpen();
    return _bindings.readTopology();
  }

  int start({
    required BenchmarkTest test,
    required int durationMs,
    required int warmupMs,
    int requestedThreads = 0,
    int? selectedCpu,
    int? selectedPerformanceGroup,
  }) {
    _ensureOpen();
    if (selectedCpu != null &&
        (test != BenchmarkTest.cpuSingle ||
            selectedCpu < 0 ||
            selectedCpu >= bmMaxLogicalCpus)) {
      throw ArgumentError.value(
        selectedCpu,
        'selectedCpu',
        'Must identify a valid logical CPU for a single-thread test',
      );
    }
    if (selectedPerformanceGroup != null &&
        (test != BenchmarkTest.cpuMulti ||
            selectedPerformanceGroup < 0 ||
            selectedPerformanceGroup > 0xFF)) {
      throw ArgumentError.value(
        selectedPerformanceGroup,
        'selectedPerformanceGroup',
        'Must identify a valid performance group for a multi-thread test',
      );
    }
    var requestFlags = 0;
    if (selectedCpu != null) {
      requestFlags |=
          bmRequestFlagSingleCpuExplicit |
          (selectedCpu << bmRequestSingleCpuShift);
    }
    if (selectedPerformanceGroup != null) {
      requestFlags |=
          bmRequestFlagMultiGroupExplicit |
          (selectedPerformanceGroup << bmRequestMultiGroupShift);
    }
    final request = calloc<BmRequestV2>();
    final runId = calloc<Uint64>();
    try {
      request.ref
        ..structSize = sizeOf<BmRequestV2>()
        ..abiVersion = bmAbiVersion
        ..testId = test.nativeId
        ..durationMs = durationMs
        ..warmupMs = warmupMs
        ..requestedThreads = requestedThreads
        ..flags = requestFlags;
      _check(_bindings.start(_handle, request, runId), 'bm_start');
      return runId.value;
    } finally {
      calloc.free(runId);
      calloc.free(request);
    }
  }

  BmSnapshotV4 get snapshot {
    _ensureOpen();
    _snapshot.ref
      ..structSize = sizeOf<BmSnapshotV4>()
      ..abiVersion = bmAbiVersion;
    _check(_bindings.getSnapshot(_handle, _snapshot), 'bm_get_snapshot');
    return _snapshot.ref;
  }

  void requestStop(int runId) {
    _ensureOpen();
    _check(_bindings.requestStop(_handle, runId), 'bm_request_stop');
  }

  void dispose() {
    if (_disposed) {
      return;
    }
    _bindings.engineDestroy(_handle);
    calloc.free(_snapshot);
    _disposed = true;
  }

  void _ensureOpen() {
    if (_disposed) {
      throw StateError('NativeBenchmarkEngine has been disposed');
    }
  }

  static void _check(int status, String operation) {
    if (status != 0) {
      throw StateError('$operation failed with native status $status');
    }
  }
}
