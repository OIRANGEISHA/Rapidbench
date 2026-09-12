import 'dart:async';

import 'benchmark_models.dart';
import 'benchmark_run_coordinator.dart';
import 'cpu_application_bindings.dart';
import 'cpu_application_models.dart';

final class CpuApplicationController extends ExclusiveBenchmarkController {
  CpuApplicationController({CpuApplicationEngine? engine, super.coordinator})
    : _engine = engine ?? NativeCpuApplicationEngine();
  final CpuApplicationEngine _engine;
  final Map<CpuApplicationTest, CpuApplicationSnapshot> _results = {};
  CpuApplicationSnapshot _snapshot = const CpuApplicationSnapshot();
  Timer? _timer;
  Object? _lastError;
  bool _multi = false, _disposed = false;
  CpuApplicationSnapshot get snapshot => _snapshot;
  Object? get lastError => _lastError;
  bool get multi => _multi;
  bool get isRunning => !engineUnavailable && snapshot.state.isRunning;
  CpuApplicationSnapshot? resultFor(CpuApplicationTest test) => _results[test];

  void selectMulti(bool value) {
    if (!canStart || _multi == value) return;
    _multi = value;
    clearResults();
  }

  void clearResults() {
    if (isRunning) return;
    _results.clear();
    _snapshot = const CpuApplicationSnapshot();
    _lastError = null;
    notifyListeners();
  }

  void start(CpuApplicationTest test) {
    if (!beginBenchmark(BenchmarkModule.cpu, stop)) return;
    _results.remove(test);
    _lastError = null;
    try {
      final id = _engine.start(test, multi: _multi);
      _snapshot = CpuApplicationSnapshot(
        runId: id,
        state: BenchmarkState.preparing,
        test: test,
      );
      _timer = Timer.periodic(
        const Duration(milliseconds: 120),
        (_) => _poll(),
      );
      notifyListeners();
    } catch (error) {
      _fail(error);
    }
  }

  void stop() {
    if (!isRunning || snapshot.runId == 0) return;
    try {
      _engine.requestStop(snapshot.runId);
    } catch (error) {
      _fail(error);
    }
  }

  void _poll() {
    if (_disposed) return;
    try {
      _snapshot = _engine.readSnapshot();
      if (snapshot.state.isTerminal) {
        if (snapshot.hasResult) _results[snapshot.test] = snapshot;
        if (snapshot.state == BenchmarkState.error) {
          _lastError = 'Test failed (${snapshot.errorCode}); no valid result.';
        }
        _timer?.cancel();
        _timer = null;
        finishBenchmark();
      }
      notifyListeners();
    } catch (error) {
      _fail(error);
    }
  }

  void _fail(Object error) {
    _timer?.cancel();
    _timer = null;
    _lastError = error;
    // Destruction joins native workers before another module can start.
    abortBenchmark(_engine.dispose);
    notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    _timer?.cancel();
    _engine.dispose();
    super.dispose();
  }
}
