import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

import 'benchmark_bindings.dart' show bmAbiVersion;
import 'benchmark_models.dart';
import 'cpu_application_models.dart';

abstract interface class CpuApplicationEngine {
  int start(CpuApplicationTest test, {required bool multi});
  CpuApplicationSnapshot readSnapshot();
  void requestStop(int id);
  void dispose();
}

final class BmCpuApplicationRequest extends Struct {
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
}

final class BmCpuApplicationSnapshot extends Struct {
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

  @Uint32()
  external int flags;

  @Int32()
  external int errorCode;

  @Uint32()
  external int affinityFailures;

  @Uint64()
  external int inputBytes;

  @Uint64()
  external int completedUnits;

  @Uint64()
  external int elapsedNs;

  @Double()
  external double unitsPerSecond;

  @Double()
  external double progress;

  @Uint64()
  external int checksum;

  @Uint32()
  external int methodVersion;

  @Uint32()
  external int requestedThreads;

  @Int32()
  external int selectedCpu;

  @Uint32()
  external int reserved;
}

typedef _CreateN = Int32 Function(Pointer<Pointer<Void>>);
typedef _CreateD = int Function(Pointer<Pointer<Void>>);
typedef _DestroyN = Int32 Function(Pointer<Void>);
typedef _DestroyD = int Function(Pointer<Void>);
typedef _StartN = Int32 Function(
  Pointer<Void>,
  Pointer<BmCpuApplicationRequest>,
  Pointer<Uint64>,
);
typedef _StartD = int Function(
  Pointer<Void>,
  Pointer<BmCpuApplicationRequest>,
  Pointer<Uint64>,
);
typedef _StopN = Int32 Function(Pointer<Void>, Uint64);
typedef _StopD = int Function(Pointer<Void>, int);
typedef _SnapshotN = Int32 Function(
  Pointer<Void>,
  Pointer<BmCpuApplicationSnapshot>,
);
typedef _SnapshotD = int Function(
  Pointer<Void>,
  Pointer<BmCpuApplicationSnapshot>,
);

final class NativeCpuApplicationEngine implements CpuApplicationEngine {
  NativeCpuApplicationEngine() {
    final library = DynamicLibrary.open(
      Platform.isWindows
          ? 'benchmark_ffi.dll'
          : Platform.isMacOS
          ? 'libbenchmark_ffi.dylib'
          : 'libbenchmark_ffi.so',
    );
    final create = library.lookupFunction<_CreateN, _CreateD>(
      'bm_cpu_application_create',
    );
    _destroy = library.lookupFunction<_DestroyN, _DestroyD>(
      'bm_cpu_application_destroy',
    );
    _start = library.lookupFunction<_StartN, _StartD>(
      'bm_cpu_application_start',
    );
    _stop = library.lookupFunction<_StopN, _StopD>('bm_cpu_application_stop');
    _snapshot = library.lookupFunction<_SnapshotN, _SnapshotD>(
      'bm_cpu_application_snapshot',
    );
    final out = calloc<Pointer<Void>>();
    try {
      _check(create(out));
      _handle = out.value;
    } finally {
      calloc.free(out);
    }
  }
  late final Pointer<Void> _handle;
  late final _DestroyD _destroy;
  late final _StartD _start;
  late final _StopD _stop;
  late final _SnapshotD _snapshot;
  bool _disposed = false;
  void _ensureOpen() {
    if (_disposed) throw StateError('CPU application engine is closed');
  }

  static void _check(int status) {
    if (status != 0) throw StateError('CPU application native status $status');
  }

  @override
  int start(CpuApplicationTest test, {required bool multi}) {
    _ensureOpen();
    final request = calloc<BmCpuApplicationRequest>();
    final id = calloc<Uint64>();
    try {
      request.ref
        ..structSize = sizeOf<BmCpuApplicationRequest>()
        ..abiVersion = bmAbiVersion
        ..testId = test.nativeId
        ..durationMs = 3000
        ..warmupMs = 700
        ..requestedThreads = multi ? 0 : 1;
      _check(_start(_handle, request, id));
      return id.value;
    } finally {
      calloc.free(request);
      calloc.free(id);
    }
  }

  @override
  CpuApplicationSnapshot readSnapshot() {
    _ensureOpen();
    final out = calloc<BmCpuApplicationSnapshot>();
    try {
      out.ref
        ..structSize = sizeOf<BmCpuApplicationSnapshot>()
        ..abiVersion = bmAbiVersion;
      _check(_snapshot(_handle, out));
      final s = out.ref;
      return CpuApplicationSnapshot(
        runId: s.runId,
        state: BenchmarkState.fromNative(s.state),
        test: CpuApplicationTest.fromNative(s.testId),
        threadCount: s.threadCount,
        flags: s.flags,
        errorCode: s.errorCode,
        affinityFailures: s.affinityFailures,
        inputBytes: s.inputBytes,
        completedUnits: s.completedUnits,
        elapsedNs: s.elapsedNs,
        unitsPerSecond: s.unitsPerSecond,
        progress: s.progress.clamp(0.0, 1.0),
        methodVersion: s.methodVersion,
        selectedCpu: s.selectedCpu,
      );
    } finally {
      calloc.free(out);
    }
  }

  @override
  void requestStop(int id) {
    _ensureOpen();
    _check(_stop(_handle, id));
  }

  @override
  void dispose() {
    if (_disposed) return;
    _check(_destroy(_handle));
    _disposed = true;
  }
}
