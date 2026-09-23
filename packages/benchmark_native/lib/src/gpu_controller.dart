import 'dart:async';

import 'benchmark_run_coordinator.dart';
import 'gpu_bindings.dart';
import 'gpu_models.dart';

final class GpuBenchmarkController extends ExclusiveBenchmarkController {
  GpuBenchmarkController() : _engine = NativeGpuEngine() {
    capabilities = _engine.readCapabilities();
    _snapshot = _engine.readSnapshot();
  }

  static const _pollInterval = Duration(milliseconds: 200);
  static const _durationMs = 6000;
  static const _warmupMs = 700;

  final NativeGpuEngine _engine;
  Timer? _pollTimer;
  late final GpuCapabilities capabilities;
  GpuBenchmarkSnapshot _snapshot = const GpuBenchmarkSnapshot();
  Object? _lastError;
  bool _disposed = false;

  GpuBenchmarkSnapshot get snapshot => _snapshot;
  Object? get lastError => _lastError;
  bool get isRunning => !engineUnavailable && _snapshot.state.isRunning;

  void startAll() => _start(GpuBenchmarkTest.all);

  void startSingle(GpuBenchmarkTest test) {
    if (test == GpuBenchmarkTest.none || test == GpuBenchmarkTest.all) {
      return;
    }
    _start(test);
  }

  void _start(GpuBenchmarkTest test) {
    if (!capabilities.supports(test) ||
        _snapshot.fatalError ||
        !beginBenchmark(BenchmarkModule.gpu, stop)) {
      return;
    }
    _lastError = null;
    try {
      _engine.start(test, durationMs: _durationMs, warmupMs: _warmupMs);
      _snapshot = _engine.readSnapshot();
      _pollTimer?.cancel();
      _pollTimer = Timer.periodic(_pollInterval, (_) => _poll());
      notifyListeners();
    } catch (error) {
      _lastError = error;
      abortBenchmark(_engine.dispose);
      notifyListeners();
    }
  }

  void stop() {
    if (!isRunning || _snapshot.runId == 0) {
      return;
    }
    try {
      _engine.requestStop(_snapshot.runId);
      _snapshot = _engine.readSnapshot();
      notifyListeners();
    } catch (error) {
      _lastError = error;
      notifyListeners();
    }
  }

  void _poll() {
    if (_disposed) {
      return;
    }
    try {
      _snapshot = _engine.readSnapshot();
      if (_snapshot.state.isTerminal) {
        _pollTimer?.cancel();
        _pollTimer = null;
        finishBenchmark();
      }
      notifyListeners();
    } catch (error) {
      _lastError = error;
      _pollTimer?.cancel();
      _pollTimer = null;
      abortBenchmark(_engine.dispose);
      notifyListeners();
    }
  }

  @override
  void dispose() {
    _disposed = true;
    _pollTimer?.cancel();
    _engine.dispose();
    super.dispose();
  }
}
