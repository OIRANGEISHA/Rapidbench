import 'package:flutter/foundation.dart';

enum BenchmarkModule {
  cpu('CPU'),
  memory('MEMORY'),
  storage('STORAGE'),
  gpu('GPU');

  const BenchmarkModule(this.label);
  final String label;
}

/// Owns one complete run, including preparation, sequences, and native cleanup.
final class BenchmarkRunLease {
  BenchmarkRunLease._(this.module, this._requestStop);
  final BenchmarkModule module;
  final VoidCallback _requestStop;
}

final class BenchmarkRunCoordinator extends ChangeNotifier {
  static final instance = BenchmarkRunCoordinator();
  BenchmarkRunLease? _active;
  bool _foreground = true;
  BenchmarkModule? get activeModule => _active?.module;
  bool get canStart => _foreground && _active == null;

  BenchmarkRunLease? acquire(BenchmarkModule module, VoidCallback requestStop) {
    if (!canStart) return null;
    final lease = BenchmarkRunLease._(module, requestStop);
    _active = lease;
    notifyListeners();
    return lease;
  }

  void release(BenchmarkRunLease lease) {
    // Late completion/disposal from an older run cannot unlock a newer one.
    if (!identical(_active, lease)) return;
    _active = null;
    notifyListeners();
  }

  void setForeground(bool foreground) {
    if (_foreground == foreground) return;
    _foreground = foreground;
    if (!foreground) _active?._requestStop();
    // A stop request does not release ownership: workers must finish first.
    notifyListeners();
  }
}

/// Shared ownership rules used by all four public benchmark controllers.
abstract class ExclusiveBenchmarkController extends ChangeNotifier {
  ExclusiveBenchmarkController({BenchmarkRunCoordinator? coordinator})
    : _coordinator = coordinator ?? BenchmarkRunCoordinator.instance {
    _coordinator.addListener(notifyListeners);
  }
  final BenchmarkRunCoordinator _coordinator;
  BenchmarkRunLease? _lease;
  bool _engineUnavailable = false;
  bool get canStart => !_engineUnavailable && _coordinator.canStart;
  bool get engineUnavailable => _engineUnavailable;

  @protected
  bool beginBenchmark(BenchmarkModule module, VoidCallback requestStop) {
    if (!canStart) return false;
    _lease = _coordinator.acquire(module, requestStop);
    return _lease != null;
  }

  @protected
  void finishBenchmark() {
    final lease = _lease;
    _lease = null;
    if (lease != null) _coordinator.release(lease);
  }

  @protected
  void abortBenchmark(VoidCallback destroyEngine) {
    // A failed FFI read may leave native work running. Destruction requests stop
    // and joins workers before another module can take ownership.
    _engineUnavailable = true;
    destroyEngine();
    finishBenchmark();
  }

  @override
  void dispose() {
    _coordinator.removeListener(notifyListeners);
    finishBenchmark();
    super.dispose();
  }
}
