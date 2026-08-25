import 'dart:convert';
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

import 'benchmark_bindings.dart' show bmAbiVersion;
import 'gpu_models.dart';

const int _overallScoreValid = 1 << 0;
const int _reducedWorkingSet = 1 << 1;
const int _vulkanAvailable = 1 << 2;
const int _lastErrorCapacity = 192;

final class BmGpuRequestV1 extends Struct {
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

final class BmGpuSnapshotV1 extends Struct {
  @Uint32()
  external int structSize;
  @Uint32()
  external int abiVersion;
  @Uint64()
  external int runId;
  @Uint32()
  external int state;
  @Uint32()
  external int activeTest;
  @Uint32()
  external int fp16Mode;
  @Uint32()
  external int timingMode;
  @Int32()
  external int errorCode;
  @Uint32()
  external int flags;
  @Uint64()
  external int elapsedNs;
  @Uint64()
  external int bufferBytes;
  @Double()
  external double fp32Gflops;
  @Double()
  external double fp16Gflops;
  @Double()
  external double fp16Scaling;
  @Double()
  external double int32Gops;
  @Double()
  external double mixedGwork;
  @Double()
  external double memoryBandwidthGbps;
  @Double()
  external double overallScore;
  @Double()
  external double progress;
  @Double()
  external double gpuBatchMs;
  @Uint32()
  external int workgroupSize;
  @Uint32()
  external int dispatchCount;
  @Uint32()
  external int iterationCount;
  @Uint32()
  external int reserved0;
  @Array(_lastErrorCapacity)
  external Array<Uint8> lastError;
}

typedef _CreateNative = Int32 Function(Pointer<Pointer<Void>>);
typedef _CreateDart = int Function(Pointer<Pointer<Void>>);
typedef _DestroyNative = Int32 Function(Pointer<Void>);
typedef _DestroyDart = int Function(Pointer<Void>);
typedef _StartNative = Int32 Function(
  Pointer<Void>,
  Pointer<BmGpuRequestV1>,
  Pointer<Uint64>,
);
typedef _StartDart = int Function(
  Pointer<Void>,
  Pointer<BmGpuRequestV1>,
  Pointer<Uint64>,
);
typedef _StopNative = Int32 Function(Pointer<Void>, Uint64);
typedef _StopDart = int Function(Pointer<Void>, int);
typedef _SnapshotNative = Int32 Function(
  Pointer<Void>,
  Pointer<BmGpuSnapshotV1>,
);
typedef _SnapshotDart = int Function(Pointer<Void>, Pointer<BmGpuSnapshotV1>);
typedef _CapabilitiesNative = Int32 Function(
  Pointer<Void>,
  Pointer<Char>,
  Uint32,
  Pointer<Uint32>,
);
typedef _CapabilitiesDart = int Function(
  Pointer<Void>,
  Pointer<Char>,
  int,
  Pointer<Uint32>,
);

final class NativeGpuEngine {
  NativeGpuEngine() : _library = _openLibrary() {
    _create = _library.lookupFunction<_CreateNative, _CreateDart>(
      'bm_gpu_engine_create',
    );
    _destroy = _library.lookupFunction<_DestroyNative, _DestroyDart>(
      'bm_gpu_engine_destroy',
    );
    _start = _library.lookupFunction<_StartNative, _StartDart>('bm_gpu_start');
    _stop = _library.lookupFunction<_StopNative, _StopDart>(
      'bm_gpu_request_stop',
    );
    _read = _library.lookupFunction<_SnapshotNative, _SnapshotDart>(
      'bm_gpu_get_snapshot',
    );
    _capabilities = _library
        .lookupFunction<_CapabilitiesNative, _CapabilitiesDart>(
          'bm_gpu_get_capabilities_json',
        );
    final outEngine = calloc<Pointer<Void>>();
    try {
      _check(_create(outEngine), 'bm_gpu_engine_create');
      _handle = outEngine.value;
    } finally {
      calloc.free(outEngine);
    }
    _snapshot = calloc<BmGpuSnapshotV1>();
  }

  final DynamicLibrary _library;
  late final _CreateDart _create;
  late final _DestroyDart _destroy;
  late final _StartDart _start;
  late final _StopDart _stop;
  late final _SnapshotDart _read;
  late final _CapabilitiesDart _capabilities;
  late final Pointer<Void> _handle;
  late final Pointer<BmGpuSnapshotV1> _snapshot;
  bool _disposed = false;

  int start(
    GpuBenchmarkTest test, {
    int durationMs = 6000,
    int warmupMs = 700,
  }) {
    _ensureOpen();
    final request = calloc<BmGpuRequestV1>();
    final runId = calloc<Uint64>();
    try {
      request.ref
        ..structSize = sizeOf<BmGpuRequestV1>()
        ..abiVersion = bmAbiVersion
        ..testId = test.nativeId
        ..durationMs = durationMs
        ..warmupMs = warmupMs;
      _check(_start(_handle, request, runId), 'bm_gpu_start');
      return runId.value;
    } finally {
      calloc.free(request);
      calloc.free(runId);
    }
  }

  void requestStop(int runId) {
    _ensureOpen();
    _check(_stop(_handle, runId), 'bm_gpu_request_stop');
  }

  GpuBenchmarkSnapshot readSnapshot() {
    _ensureOpen();
    _snapshot.ref
      ..structSize = sizeOf<BmGpuSnapshotV1>()
      ..abiVersion = bmAbiVersion;
    _check(_read(_handle, _snapshot), 'bm_gpu_get_snapshot');
    final native = _snapshot.ref;
    return GpuBenchmarkSnapshot(
      runId: native.runId,
      state: GpuBenchmarkState.fromNative(native.state),
      activeTest: GpuBenchmarkTest.fromNative(native.activeTest),
      fp16Mode: GpuFp16Mode.fromNative(native.fp16Mode),
      timingMode: GpuTimingMode.fromNative(native.timingMode),
      errorCode: native.errorCode,
      overallScoreValid: (native.flags & _overallScoreValid) != 0,
      reducedWorkingSet: (native.flags & _reducedWorkingSet) != 0,
      vulkanAvailable: (native.flags & _vulkanAvailable) != 0,
      elapsed: Duration(microseconds: native.elapsedNs ~/ 1000),
      bufferBytes: native.bufferBytes,
      fp32Gflops: native.fp32Gflops,
      fp16Gflops: native.fp16Gflops,
      fp16Scaling: native.fp16Scaling,
      int32Gops: native.int32Gops,
      mixedGwork: native.mixedGwork,
      memoryBandwidthGbps: native.memoryBandwidthGbps,
      overallScore: native.overallScore,
      progress: native.progress.clamp(0.0, 1.0),
      gpuBatchMs: native.gpuBatchMs,
      workgroupSize: native.workgroupSize,
      dispatchCount: native.dispatchCount,
      iterationCount: native.iterationCount,
      lastError: _decodeError(native.lastError),
    );
  }

  GpuCapabilities readCapabilities() {
    _ensureOpen();
    final required = calloc<Uint32>();
    try {
      _check(
        _capabilities(_handle, nullptr, 0, required),
        'bm_gpu_get_capabilities_json(size)',
      );
      final buffer = calloc<Uint8>(required.value);
      try {
        _check(
          _capabilities(_handle, buffer.cast<Char>(), required.value, required),
          'bm_gpu_get_capabilities_json',
        );
        final decoded = jsonDecode(
          utf8.decode(buffer.asTypedList(required.value - 1)),
        );
        if (decoded is! Map) {
          throw const FormatException('Unexpected GPU capability response');
        }
        return GpuCapabilities.fromJson(Map<String, dynamic>.from(decoded));
      } finally {
        calloc.free(buffer);
      }
    } finally {
      calloc.free(required);
    }
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
      throw StateError('NativeGpuEngine has been disposed');
    }
  }

  static String _decodeError(Array<Uint8> bytes) {
    final values = <int>[];
    for (var index = 0; index < _lastErrorCapacity; ++index) {
      final value = bytes[index];
      if (value == 0) {
        break;
      }
      values.add(value);
    }
    return utf8.decode(values, allowMalformed: true);
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
