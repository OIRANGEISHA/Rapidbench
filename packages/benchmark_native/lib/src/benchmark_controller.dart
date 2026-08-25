import 'dart:async';

import 'package:flutter/foundation.dart';

import 'benchmark_bindings.dart';
import 'benchmark_models.dart';

final class BenchmarkController extends ChangeNotifier {
  factory BenchmarkController() {
    final bindings = BenchmarkBindings.open();
    return BenchmarkController._(
      NativeBenchmarkEngine(bindings),
      bindings.readTopology(),
    );
  }

  BenchmarkController._(this._engine, CpuTopology topology)
    : _topology = topology,
      _selectedSingleCpu = topology.preferredSingleCpu;

  static const _pollInterval = Duration(milliseconds: 200);
  static const _singleDurationMs = 10000;
  static const _singleWarmupMs = 2500;
  static const _multiDurationMs = 10000;
  static const _multiWarmupMs = 2500;

  final NativeBenchmarkEngine _engine;
  CpuTopology _topology;
  Timer? _pollTimer;
  BenchmarkSnapshot _snapshot = const BenchmarkSnapshot();
  BenchmarkTest? _activeTest;
  double? _singleScore;
  double? _multiScore;
  double? _singleVariation;
  double? _multiVariation;
  Object? _lastError;
  bool _runSequence = false;
  bool _disposed = false;
  int _selectedSingleCpu;
  int? _selectedMultiGroup;
  BenchmarkSnapshot? _singleResult;
  BenchmarkSnapshot? _multiResult;

  BenchmarkSnapshot get snapshot => _snapshot;
  CpuTopology get topology => _topology;
  BenchmarkTest? get activeTest => _activeTest;
  double? get singleScore =>
      _activeTest == BenchmarkTest.cpuSingle &&
          _snapshot.state.isRunning &&
          _snapshot.currentValue > 0
      ? _snapshot.currentValue
      : _singleScore;
  double? get multiScore =>
      _activeTest == BenchmarkTest.cpuMulti &&
          _snapshot.state.isRunning &&
          _snapshot.currentValue > 0
      ? _snapshot.currentValue
      : _multiScore;
  double? get singleVariation => _singleVariation;
  double? get multiVariation => _multiVariation;
  Object? get lastError => _lastError;
  bool get isRunning => _snapshot.state.isRunning;
  int get selectedSingleCpu => _selectedSingleCpu;
  int? get selectedMultiGroup => _selectedMultiGroup;
  BenchmarkSnapshot? get singleResult => _singleResult;
  BenchmarkSnapshot? get multiResult => _multiResult;
  double? get multiThreadMultiplier {
    final single = singleScore;
    final multi = multiScore;
    if (single == null || multi == null || single <= 0) {
      return null;
    }
    return multi / single;
  }

  void selectSingleCpu(int logicalCpu) {
    if (isRunning || logicalCpu == _selectedSingleCpu) {
      return;
    }
    final selectable = topology.allowedCpus.any(
      (cpu) => cpu.online && cpu.logicalCpu == logicalCpu,
    );
    if (!selectable) {
      throw ArgumentError.value(logicalCpu, 'logicalCpu', 'CPU is unavailable');
    }
    _selectedSingleCpu = logicalCpu;
    _clearResults();
    notifyListeners();
  }

  void selectMultiGroup(int? performanceGroup) {
    if (isRunning || performanceGroup == _selectedMultiGroup) {
      return;
    }
    if (performanceGroup != null) {
      final selectable = topology.allowedCpus.any(
        (cpu) => cpu.online && cpu.performanceGroup == performanceGroup,
      );
      if (!selectable) {
        throw ArgumentError.value(
          performanceGroup,
          'performanceGroup',
          'Performance group is unavailable',
        );
      }
    }
    _selectedMultiGroup = performanceGroup;
    _clearResults();
    notifyListeners();
  }

  void _clearResults() {
    _singleScore = null;
    _multiScore = null;
    _singleVariation = null;
    _multiVariation = null;
    _singleResult = null;
    _multiResult = null;
    _lastError = null;
  }

  void startCpuBench() {
    if (isRunning) {
      return;
    }
    _clearResults();
    try {
      _refreshTopologyAndSelections();
    } catch (error) {
      _lastError = error;
      notifyListeners();
      return;
    }
    _runSequence = true;
    _startTest(BenchmarkTest.cpuSingle);
  }

  void _refreshTopologyAndSelections() {
    final refreshed = _engine.readTopology();
    _topology = refreshed;
    final available = refreshed.allowedCpus
        .where((cpu) => cpu.online)
        .toList(growable: false);
    if (available.isEmpty) {
      throw StateError('No online CPU is available for benchmarking');
    }

    if (!available.any((cpu) => cpu.logicalCpu == _selectedSingleCpu)) {
      final preferred = refreshed.preferredSingleCpu;
      _selectedSingleCpu = available.any((cpu) => cpu.logicalCpu == preferred)
          ? preferred
          : available.first.logicalCpu;
    }
    if (_selectedMultiGroup != null &&
        !available.any((cpu) => cpu.performanceGroup == _selectedMultiGroup)) {
      _selectedMultiGroup = null;
    }
  }

  void stop() {
    _runSequence = false;
    if (!_snapshot.state.isRunning || _snapshot.runId == 0) {
      return;
    }
    try {
      _engine.requestStop(_snapshot.runId);
    } catch (error) {
      _lastError = error;
      notifyListeners();
    }
  }

  void _startTest(BenchmarkTest test) {
    try {
      final durationMs = test == BenchmarkTest.cpuMulti
          ? _multiDurationMs
          : _singleDurationMs;
      final warmupMs = test == BenchmarkTest.cpuMulti
          ? _multiWarmupMs
          : _singleWarmupMs;
      final runId = _engine.start(
        test: test,
        durationMs: durationMs,
        warmupMs: warmupMs,
        selectedCpu: test == BenchmarkTest.cpuSingle
            ? _selectedSingleCpu
            : null,
        selectedPerformanceGroup: test == BenchmarkTest.cpuMulti
            ? _selectedMultiGroup
            : null,
      );
      _activeTest = test;
      _snapshot = BenchmarkSnapshot(
        runId: runId,
        state: BenchmarkState.preparing,
        test: test,
      );
      _pollTimer ??= Timer.periodic(_pollInterval, (_) => _poll());
      notifyListeners();
    } catch (error) {
      _runSequence = false;
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
      final native = _engine.snapshot;
      final peakFrequencyGroupCount = native.peakFrequencyGroupCount.clamp(
        0,
        bmMaxPerformanceGroups,
      );
      final peakFrequenciesMhzByGroup = List<int>.unmodifiable([
        for (var index = 0; index < peakFrequencyGroupCount; index++)
          native.peakFrequencyMhzForGroup(index),
      ]);
      _snapshot = BenchmarkSnapshot(
        runId: native.runId,
        state: BenchmarkState.fromNative(native.state),
        test: BenchmarkTest.fromNative(native.testId),
        threadCount: native.threadCount,
        qualityFlags: native.qualityFlags,
        elapsed: Duration(microseconds: native.elapsedNs ~/ 1000),
        completedWork: native.completedWork,
        currentValue: native.currentValue,
        peakScore: native.peakScore,
        progress: native.progress.clamp(0.0, 1.0),
        scoreVariation: native.scoreVariation,
        errorCode: native.errorCode,
        selectedCpu: native.selectedCpu,
        affinityFailures: native.affinityFailures,
        peakFrequencyMhz:
            (native.telemetry >> bmTelemetryFrequencyShift) & 0xFFFF,
        peakFrequenciesMhzByGroup: peakFrequenciesMhzByGroup,
        workerUtilization:
            (native.telemetry & bmTelemetryUtilizationMask) / 1000.0,
        performanceHintActive: (native.telemetry & bmTelemetryHintActive) != 0,
        performanceRequestActive:
            (native.telemetry & bmTelemetryPerformanceRequestActive) != 0,
        affinityChecked: (native.telemetry & bmTelemetryAffinityChecked) != 0,
      );

      if (_snapshot.state == BenchmarkState.completed ||
          _snapshot.state == BenchmarkState.cancelled) {
        _storeResult(_snapshot);
      }
      if (_snapshot.state == BenchmarkState.completed &&
          _snapshot.test == BenchmarkTest.cpuSingle &&
          _runSequence) {
        _startTest(BenchmarkTest.cpuMulti);
        return;
      }

      if (_snapshot.state.isTerminal) {
        _runSequence = false;
        _pollTimer?.cancel();
        _pollTimer = null;
      }
      notifyListeners();
    } catch (error) {
      _runSequence = false;
      _lastError = error;
      _pollTimer?.cancel();
      _pollTimer = null;
      notifyListeners();
    }
  }

  void _storeResult(BenchmarkSnapshot result) {
    if (result.test == BenchmarkTest.cpuSingle) {
      _singleScore = result.currentValue;
      _singleVariation = result.scoreVariation;
      _singleResult = result;
    } else if (result.test == BenchmarkTest.cpuMulti) {
      _multiScore = result.currentValue;
      _multiVariation = result.scoreVariation;
      _multiResult = result;
    }
  }

  @override
  void dispose() {
    _disposed = true;
    _runSequence = false;
    _pollTimer?.cancel();
    if (_snapshot.state.isRunning && _snapshot.runId != 0) {
      _engine.requestStop(_snapshot.runId);
    }
    _engine.dispose();
    super.dispose();
  }
}
