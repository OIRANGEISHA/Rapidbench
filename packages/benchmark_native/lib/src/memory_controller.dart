import 'dart:async';

import 'package:flutter/foundation.dart';

import 'benchmark_models.dart';
import 'memory_bindings.dart';
import 'memory_models.dart';

final class MemoryBenchmarkController extends ChangeNotifier {
  MemoryBenchmarkController() : _engine = NativeMemoryEngine() {
    _refreshFrequency();
  }

  static const _pollInterval = Duration(milliseconds: 120);
  static const _durationMs = 3000;
  static const _warmupMs = 1000;

  final NativeMemoryEngine _engine;
  Timer? _pollTimer;
  MemoryBenchmarkSnapshot _snapshot = const MemoryBenchmarkSnapshot();
  final Map<MemoryBenchmarkTest, MemoryBenchmarkSnapshot> _results = {};
  MemoryFrequencyInfo? _frequency;
  Object? _lastError;
  int _sequenceIndex = -1;
  bool _runSequence = false;
  bool _disposed = false;

  MemoryBenchmarkSnapshot get snapshot => _snapshot;
  MemoryFrequencyInfo? get frequency => _frequency;
  Object? get lastError => _lastError;
  bool get isRunning => _snapshot.state.isRunning;
  MemoryBenchmarkSnapshot? resultFor(MemoryBenchmarkTest test) =>
      _results[test];

  void start() {
    if (isRunning) {
      return;
    }
    _results.clear();
    _lastError = null;
    _runSequence = true;
    _sequenceIndex = 0;
    _refreshFrequency();
    _startCurrentTest();
  }

  void startSingle(MemoryBenchmarkTest test) {
    if (isRunning) {
      return;
    }
    _results.remove(test);
    _lastError = null;
    _runSequence = false;
    _sequenceIndex = test.index;
    _refreshFrequency();
    _startCurrentTest();
  }

  void stop() {
    _runSequence = false;
    _sequenceIndex = -1;
    if (!isRunning || _snapshot.runId == 0) {
      return;
    }
    try {
      _engine.requestStop(_snapshot.runId);
    } catch (error) {
      _lastError = error;
      notifyListeners();
    }
  }

  void _startCurrentTest() {
    if (_sequenceIndex < 0 ||
        _sequenceIndex >= MemoryBenchmarkTest.values.length) {
      _sequenceIndex = -1;
      return;
    }
    final test = MemoryBenchmarkTest.values[_sequenceIndex];
    try {
      final runId = _engine.start(
        test,
        durationMs: _durationMs,
        warmupMs: _warmupMs,
      );
      _snapshot = MemoryBenchmarkSnapshot(
        runId: runId,
        state: BenchmarkState.preparing,
        test: test,
      );
      _pollTimer ??= Timer.periodic(_pollInterval, (_) => _poll());
      notifyListeners();
    } catch (error) {
      _runSequence = false;
      _sequenceIndex = -1;
      _lastError = error;
      _pollTimer?.cancel();
      _pollTimer = null;
      notifyListeners();
    }
  }

  void _poll() {
    if (_disposed) {
      return;
    }
    try {
      _snapshot = _engine.readSnapshot();
      if (_snapshot.state == BenchmarkState.completed ||
          _snapshot.state == BenchmarkState.cancelled) {
        _results[_snapshot.test] = _snapshot;
      }
      if (_snapshot.state == BenchmarkState.completed &&
          _runSequence &&
          _sequenceIndex >= 0 &&
          _sequenceIndex + 1 < MemoryBenchmarkTest.values.length) {
        _sequenceIndex += 1;
        _startCurrentTest();
        return;
      }
      if (_snapshot.state.isTerminal) {
        _runSequence = false;
        _sequenceIndex = -1;
        _pollTimer?.cancel();
        _pollTimer = null;
        _refreshFrequency();
      }
      notifyListeners();
    } catch (error) {
      _runSequence = false;
      _sequenceIndex = -1;
      _lastError = error;
      _pollTimer?.cancel();
      _pollTimer = null;
      notifyListeners();
    }
  }

  void _refreshFrequency() {
    try {
      _frequency = _engine.readFrequency();
    } catch (_) {
      _frequency = const MemoryFrequencyInfo(
        available: false,
        currentHz: 0,
        maximumHz: 0,
      );
    }
  }

  @override
  void dispose() {
    _disposed = true;
    _runSequence = false;
    _sequenceIndex = -1;
    _pollTimer?.cancel();
    if (_snapshot.state.isRunning && _snapshot.runId != 0) {
      _engine.requestStop(_snapshot.runId);
    }
    _engine.dispose();
    super.dispose();
  }
}
