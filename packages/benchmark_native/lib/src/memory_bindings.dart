import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

import 'benchmark_bindings.dart' show bmAbiVersion;
import 'benchmark_models.dart';
import 'memory_models.dart';

const int _frequencyCurrentAvailable = 1 << 0;
const int _frequencyMaximumAvailable = 1 << 1;

final class BmMemoryRequestV1 extends Struct {
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
  external int reserved0;
}

final class BmMemorySnapshotV1 extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int abiVersion;

  @Uint64()
  external int runId;

  @Uint32()
  external int state;

  @Uint32()
  external int testId;

  @Uint32()
  external int threadCount;

  @Int32()
  external int errorCode;

  @Uint32()
  external int affinityFailures;

  @Uint32()
  external int performanceRequestFailures;

  @Uint64()
  external int bufferBytes;

  @Uint64()
  external int elapsedNs;

  @Uint64()
  external int processedBytes;

  @Double()
  external double bandwidthGbps;

  @Double()
  external double progress;
}

final class BmMemoryFrequencyV1 extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int abiVersion;

  @Int32()
  external int status;

  @Uint32()
  external int flags;

  @Uint64()
  external int currentHz;

  @Uint64()
  external int maximumHz;
}

typedef _GetAbiNative = Uint32 Function();
typedef _GetAbiDart = int Function();
typedef _CreateNative = Int32 Function(Pointer<Pointer<Void>>);
typedef _CreateDart = int Function(Pointer<Pointer<Void>>);
typedef _DestroyNative = Int32 Function(Pointer<Void>);
typedef _DestroyDart = int Function(Pointer<Void>);
typedef _StartNative = Int32 Function(
  Pointer<Void>,
  Pointer<BmMemoryRequestV1>,
  Pointer<Uint64>,
);
typedef _StartDart = int Function(
  Pointer<Void>,
  Pointer<BmMemoryRequestV1>,
  Pointer<Uint64>,
);
typedef _StopNative = Int32 Function(Pointer<Void>, Uint64);
typedef _StopDart = int Function(Pointer<Void>, int);
typedef _SnapshotNative = Int32 Function(
  Pointer<Void>,
  Pointer<BmMemorySnapshotV1>,
);
typedef _SnapshotDart = int Function(
  Pointer<Void>,
  Pointer<BmMemorySnapshotV1>,
);
typedef _FrequencyNative = Int32 Function(Pointer<BmMemoryFrequencyV1>);
typedef _FrequencyDart = int Function(Pointer<BmMemoryFrequencyV1>);

final class NativeMemoryEngine {
  NativeMemoryEngine() : _library = _openLibrary() {
    _getAbi = _library.lookupFunction<_GetAbiNative, _GetAbiDart>(
      'bm_get_abi_version',
    );
    _create = _library.lookupFunction<_CreateNative, _CreateDart>(
      'bm_memory_engine_create',
    );
    _destroy = _library.lookupFunction<_DestroyNative, _DestroyDart>(
      'bm_memory_engine_destroy',
    );
    _start = _library.lookupFunction<_StartNative, _StartDart>(
      'bm_memory_start',
    );
    _stop = _library.lookupFunction<_StopNative, _StopDart>(
      'bm_memory_request_stop',
    );
    _snapshotFunction = _library.lookupFunction<_SnapshotNative, _SnapshotDart>(
      'bm_memory_get_snapshot',
    );
    _frequency = _library.lookupFunction<_FrequencyNative, _FrequencyDart>(
      'bm_get_memory_frequency',
    );
    if (_getAbi() != bmAbiVersion) {
      throw StateError('Native ABI mismatch while opening memory benchmark');
    }
    final outEngine = calloc<Pointer<Void>>();
    try {
      _check(_create(outEngine), 'bm_memory_engine_create');
      _handle = outEngine.value;
    } finally {
      calloc.free(outEngine);
    }
    _snapshot = calloc<BmMemorySnapshotV1>();
  }

  final DynamicLibrary _library;
  late final _GetAbiDart _getAbi;
  late final _CreateDart _create;
  late final _DestroyDart _destroy;
  late final _StartDart _start;
  late final _StopDart _stop;
  late final _SnapshotDart _snapshotFunction;
  late final _FrequencyDart _frequency;
  late final Pointer<Void> _handle;
  late final Pointer<BmMemorySnapshotV1> _snapshot;
  bool _disposed = false;

  int start(
    MemoryBenchmarkTest test, {
    int durationMs = 3000,
    int warmupMs = 750,
  }) {
    _ensureOpen();
    final request = calloc<BmMemoryRequestV1>();
    final runId = calloc<Uint64>();
    try {
      request.ref
        ..structSize = sizeOf<BmMemoryRequestV1>()
        ..abiVersion = bmAbiVersion
        ..testId = test.nativeId
        ..durationMs = durationMs
        ..warmupMs = warmupMs;
      _check(_start(_handle, request, runId), 'bm_memory_start');
      return runId.value;
    } finally {
      calloc.free(request);
      calloc.free(runId);
    }
  }

  MemoryBenchmarkSnapshot readSnapshot() {
    _ensureOpen();
    _snapshot.ref
      ..structSize = sizeOf<BmMemorySnapshotV1>()
      ..abiVersion = bmAbiVersion;
    _check(_snapshotFunction(_handle, _snapshot), 'bm_memory_get_snapshot');
    final native = _snapshot.ref;
    return MemoryBenchmarkSnapshot(
      runId: native.runId,
      state: BenchmarkState.fromNative(native.state),
      test: MemoryBenchmarkTest.fromNative(native.testId),
      threadCount: native.threadCount,
      errorCode: native.errorCode,
      affinityFailures: native.affinityFailures,
      performanceRequestFailures: native.performanceRequestFailures,
      bufferBytes: native.bufferBytes,
      elapsed: Duration(microseconds: native.elapsedNs ~/ 1000),
      processedBytes: native.processedBytes,
      bandwidthGbps: native.bandwidthGbps,
      progress: native.progress.clamp(0.0, 1.0),
    );
  }

  MemoryFrequencyInfo readFrequency() {
    _ensureOpen();
    final pointer = calloc<BmMemoryFrequencyV1>();
    try {
      pointer.ref
        ..structSize = sizeOf<BmMemoryFrequencyV1>()
        ..abiVersion = bmAbiVersion;
      _check(_frequency(pointer), 'bm_get_memory_frequency');
      final native = pointer.ref;
      return MemoryFrequencyInfo(
        available: native.status == 0 && native.flags != 0,
        currentHz: (native.flags & _frequencyCurrentAvailable) != 0
            ? native.currentHz
            : 0,
        maximumHz: (native.flags & _frequencyMaximumAvailable) != 0
            ? native.maximumHz
            : 0,
      );
    } finally {
      calloc.free(pointer);
    }
  }

  void requestStop(int runId) {
    _ensureOpen();
    _check(_stop(_handle, runId), 'bm_memory_request_stop');
  }

  void dispose() {
    if (_disposed) {
      return;
    }
    _destroy(_handle);
    calloc.free(_snapshot);
    _disposed = true;
  }

  void _ensureOpen() {
    if (_disposed) {
      throw StateError('NativeMemoryEngine has been disposed');
    }
  }

  static DynamicLibrary _openLibrary() {
    if (Platform.isAndroid || Platform.isLinux) {
      return DynamicLibrary.open('libbenchmark_ffi.so');
    }
    if (Platform.isWindows) {
      return DynamicLibrary.open('benchmark_ffi.dll');
    }
    if (Platform.isMacOS) {
      return DynamicLibrary.open('libbenchmark_ffi.dylib');
    }
    throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
  }

  static void _check(int status, String operation) {
    if (status != 0) {
      throw StateError('$operation failed with native status $status');
    }
  }
}
