import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

import 'storage_models.dart';

const int _storageAbiVersion = 1;
const int _storageResultCount = 10;

final class BmStorageRequestV1 extends Struct {
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
  external int fileSizeMiBOverride;
  @Uint32()
  external int reserved0;
}

final class BmStorageResultV1 extends Struct {
  @Uint32()
  external int structSize;
  @Uint32()
  external int testId;
  @Uint32()
  external int valid;
  @Uint32()
  external int stopped;
  @Uint32()
  external int ioMode;
  @Int32()
  external int errorCode;
  @Uint32()
  external int threadCount;
  @Uint32()
  external int queueDepth;
  @Uint32()
  external int maxOutstanding;
  @Uint64()
  external int elapsedNs;
  @Uint64()
  external int completedBytes;
  @Uint64()
  external int completedIo;
  @Uint64()
  external int completedRows;
  @Double()
  external double mbps;
  @Double()
  external double iops;
  @Double()
  external double rowsPerSecond;
}

final class BmStorageSnapshotV1 extends Struct {
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
  external int activeTestId;
  @Uint32()
  external int ioMode;
  @Int32()
  external int errorCode;
  @Uint32()
  external int alignment;
  @Uint32()
  external int testFileMiB;
  @Uint32()
  external int blockSize;
  @Uint32()
  external int queueDepth;
  @Uint32()
  external int threadCount;
  @Uint32()
  external int currentOutstanding;
  @Uint32()
  external int maxOutstanding;
  @Uint32()
  external int resultCount;
  @Uint64()
  external int elapsedNs;
  @Uint64()
  external int completedBytes;
  @Uint64()
  external int completedIo;
  @Uint64()
  external int completedRows;
  @Double()
  external double currentMbps;
  @Double()
  external double currentIops;
  @Double()
  external double currentRowsPerSecond;
  @Double()
  external double progress;
  @Array(_storageResultCount)
  external Array<BmStorageResultV1> results;
}

typedef _CreateNative = Int32 Function(Pointer<Utf8>, Pointer<Pointer<Void>>);
typedef _CreateDart = int Function(Pointer<Utf8>, Pointer<Pointer<Void>>);
typedef _DestroyNative = Int32 Function(Pointer<Void>);
typedef _DestroyDart = int Function(Pointer<Void>);
typedef _StartNative = Int32 Function(
  Pointer<Void>,
  Pointer<BmStorageRequestV1>,
  Pointer<Uint64>,
);
typedef _StartDart = int Function(
  Pointer<Void>,
  Pointer<BmStorageRequestV1>,
  Pointer<Uint64>,
);
typedef _StopNative = Int32 Function(Pointer<Void>, Uint64);
typedef _StopDart = int Function(Pointer<Void>, int);
typedef _SnapshotNative = Int32 Function(
  Pointer<Void>,
  Pointer<BmStorageSnapshotV1>,
);
typedef _SnapshotDart = int Function(
  Pointer<Void>,
  Pointer<BmStorageSnapshotV1>,
);

final class NativeStorageEngine {
  NativeStorageEngine(String directory) : _library = _openLibrary() {
    _create = _library.lookupFunction<_CreateNative, _CreateDart>(
      'bm_storage_engine_create',
    );
    _destroy = _library.lookupFunction<_DestroyNative, _DestroyDart>(
      'bm_storage_engine_destroy',
    );
    _start = _library.lookupFunction<_StartNative, _StartDart>(
      'bm_storage_start',
    );
    _stop = _library.lookupFunction<_StopNative, _StopDart>(
      'bm_storage_request_stop',
    );
    _read = _library.lookupFunction<_SnapshotNative, _SnapshotDart>(
      'bm_storage_get_snapshot',
    );
    final path = directory.toNativeUtf8();
    final outEngine = calloc<Pointer<Void>>();
    try {
      _check(_create(path, outEngine), 'bm_storage_engine_create');
      _handle = outEngine.value;
    } finally {
      calloc.free(path);
      calloc.free(outEngine);
    }
    _snapshot = calloc<BmStorageSnapshotV1>();
  }

  final DynamicLibrary _library;
  late final _CreateDart _create;
  late final _DestroyDart _destroy;
  late final _StartDart _start;
  late final _StopDart _stop;
  late final _SnapshotDart _read;
  late final Pointer<Void> _handle;
  late final Pointer<BmStorageSnapshotV1> _snapshot;
  bool _disposed = false;

  int start(
    StorageBenchmarkTest test, {
    int durationMs = 3000,
    int warmupMs = 750,
    int fileSizeMiBOverride = 0,
  }) {
    _ensureOpen();
    final request = calloc<BmStorageRequestV1>();
    final runId = calloc<Uint64>();
    try {
      request.ref
        ..structSize = sizeOf<BmStorageRequestV1>()
        ..abiVersion = _storageAbiVersion
        ..testId = test.nativeId
        ..durationMs = durationMs
        ..warmupMs = warmupMs
        ..fileSizeMiBOverride = fileSizeMiBOverride;
      _check(_start(_handle, request, runId), 'bm_storage_start');
      return runId.value;
    } finally {
      calloc.free(request);
      calloc.free(runId);
    }
  }

  StorageBenchmarkSnapshot readSnapshot() {
    _ensureOpen();
    _snapshot.ref
      ..structSize = sizeOf<BmStorageSnapshotV1>()
      ..abiVersion = _storageAbiVersion;
    _check(_read(_handle, _snapshot), 'bm_storage_get_snapshot');
    final native = _snapshot.ref;
    final results = <StorageBenchmarkTest, StorageBenchmarkResult>{};
    final count = native.resultCount.clamp(0, _storageResultCount);
    for (var index = 0; index < count; index += 1) {
      final item = native.results[index];
      if (item.testId < 1 || item.testId > _storageResultCount) {
        continue;
      }
      final test = StorageBenchmarkTest.fromNative(item.testId);
      results[test] = StorageBenchmarkResult(
        test: test,
        valid: item.valid != 0,
        stopped: item.stopped != 0,
        ioMode: StorageIoMode.fromNative(item.ioMode),
        errorCode: item.errorCode,
        threadCount: item.threadCount,
        queueDepth: item.queueDepth,
        maxOutstanding: item.maxOutstanding,
        elapsed: Duration(microseconds: item.elapsedNs ~/ 1000),
        completedBytes: item.completedBytes,
        completedIo: item.completedIo,
        completedRows: item.completedRows,
        mbps: item.mbps,
        iops: item.iops,
        rowsPerSecond: item.rowsPerSecond,
      );
    }
    return StorageBenchmarkSnapshot(
      runId: native.runId,
      state: StorageBenchmarkState.fromNative(native.state),
      phase: StorageBenchmarkPhase.fromNative(native.phase),
      activeTest: StorageBenchmarkTest.fromNative(native.activeTestId),
      ioMode: StorageIoMode.fromNative(native.ioMode),
      errorCode: native.errorCode,
      alignment: native.alignment,
      testFileMiB: native.testFileMiB,
      blockSize: native.blockSize,
      queueDepth: native.queueDepth,
      threadCount: native.threadCount,
      currentOutstanding: native.currentOutstanding,
      maxOutstanding: native.maxOutstanding,
      elapsed: Duration(microseconds: native.elapsedNs ~/ 1000),
      completedBytes: native.completedBytes,
      completedIo: native.completedIo,
      completedRows: native.completedRows,
      currentMbps: native.currentMbps,
      currentIops: native.currentIops,
      currentRowsPerSecond: native.currentRowsPerSecond,
      progress: native.progress.clamp(0.0, 1.0),
      results: Map.unmodifiable(results),
    );
  }

  void requestStop(int runId) {
    _ensureOpen();
    _check(_stop(_handle, runId), 'bm_storage_request_stop');
  }

  void dispose() {
    if (_disposed) return;
    _destroy(_handle);
    calloc.free(_snapshot);
    _disposed = true;
  }

  void _ensureOpen() {
    if (_disposed) {
      throw StateError('NativeStorageEngine has been disposed');
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
