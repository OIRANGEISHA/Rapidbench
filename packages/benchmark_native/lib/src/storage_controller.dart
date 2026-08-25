import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'storage_bindings.dart';
import 'storage_models.dart';

final class StorageBenchmarkController extends ChangeNotifier {
  StorageBenchmarkController._(this._engine);

  static const _channel = MethodChannel(
    'dev.cpu_benchmark.benchmark_app/storage_benchmark',
  );
  static const _pollInterval = Duration(milliseconds: 200);
  static const _durationMs = 3000;
  static const _warmupMs = 750;

  static Future<StorageBenchmarkController> create() async {
    final directory = await _channel.invokeMethod<String>(
      'getBenchmarkDirectory',
    );
    if (directory == null || directory.isEmpty) {
      throw StateError('Android did not provide a Storage benchmark directory');
    }
    return StorageBenchmarkController._(NativeStorageEngine(directory));
  }

  final NativeStorageEngine _engine;
  Timer? _pollTimer;
  StorageBenchmarkSnapshot _snapshot = const StorageBenchmarkSnapshot();
  Object? _lastError;
  bool _disposed = false;

  StorageBenchmarkSnapshot get snapshot => _snapshot;
  Object? get lastError => _lastError;
  bool get isRunning => _snapshot.state.isRunning;
  StorageBenchmarkResult? resultFor(StorageBenchmarkTest test) =>
      _snapshot.results[test];

  void startAll() => _start(StorageBenchmarkTest.all);

  void startSingle(StorageBenchmarkTest test) {
    if (test.isRunnableCard) _start(test);
  }

  void _start(StorageBenchmarkTest test) {
    if (isRunning) return;
    _lastError = null;
    try {
      final runId = _engine.start(
        test,
        durationMs: _durationMs,
        warmupMs: _warmupMs,
      );
      _snapshot = StorageBenchmarkSnapshot(
        runId: runId,
        state: StorageBenchmarkState.preparing,
        phase: StorageBenchmarkPhase.prepare,
        activeTest: test,
        results: test == StorageBenchmarkTest.all
            ? const {}
            : Map.unmodifiable(
                Map<StorageBenchmarkTest, StorageBenchmarkResult>.of(
                  _snapshot.results,
                )..remove(test),
              ),
      );
      _pollTimer?.cancel();
      _pollTimer = Timer.periodic(_pollInterval, (_) => _poll());
      notifyListeners();
    } catch (error) {
      _lastError = error;
      notifyListeners();
    }
  }

  void stop() {
    if (!isRunning || _snapshot.runId == 0) return;
    try {
      _engine.requestStop(_snapshot.runId);
    } catch (error) {
      _lastError = error;
      notifyListeners();
    }
  }

  void _poll() {
    if (_disposed) return;
    try {
      _snapshot = _engine.readSnapshot();
      if (_snapshot.state.isTerminal) {
        _pollTimer?.cancel();
        _pollTimer = null;
      }
      notifyListeners();
    } catch (error) {
      _lastError = error;
      _pollTimer?.cancel();
      _pollTimer = null;
      notifyListeners();
    }
  }

  @override
  void dispose() {
    _disposed = true;
    _pollTimer?.cancel();
    if (_snapshot.state.isRunning && _snapshot.runId != 0) {
      try {
        _engine.requestStop(_snapshot.runId);
      } catch (_) {
        // Destruction joins the cooperative native stop path.
      }
    }
    _engine.dispose();
    super.dispose();
  }
}
