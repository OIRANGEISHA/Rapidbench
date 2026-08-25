import 'package:benchmark_native/benchmark_native.dart';
import 'package:flutter/material.dart';

class StorageBenchPage extends StatefulWidget {
  const StorageBenchPage({super.key});

  @override
  State<StorageBenchPage> createState() => _StorageBenchPageState();
}

class _StorageBenchPageState extends State<StorageBenchPage> {
  StorageBenchmarkController? _controller;
  Object? _initializationError;

  @override
  void initState() {
    super.initState();
    _initialize();
  }

  Future<void> _initialize() async {
    try {
      final controller = await StorageBenchmarkController.create();
      if (!mounted) {
        controller.dispose();
        return;
      }
      setState(() => _controller = controller);
    } catch (error) {
      if (mounted) setState(() => _initializationError = error);
    }
  }

  @override
  void dispose() {
    _controller?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final controller = _controller;
    return Scaffold(
      appBar: AppBar(elevation: 0, title: const Text('STORAGE BENCH')),
      body: controller == null
          ? _StorageLoadState(error: _initializationError)
          : AnimatedBuilder(
              animation: controller,
              builder: (context, _) => _StorageBody(controller: controller),
            ),
    );
  }
}

class _StorageBody extends StatelessWidget {
  const _StorageBody({required this.controller});

  final StorageBenchmarkController controller;

  @override
  Widget build(BuildContext context) {
    final snapshot = controller.snapshot;
    return SafeArea(
      child: ListView(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 24),
        children: [
          const _ColumnHeaders(),
          const SizedBox(height: 8),
          _ResultRow(
            title: 'SEQUENTIAL',
            read: StorageBenchmarkTest.sequentialRead,
            write: StorageBenchmarkTest.sequentialWrite,
            controller: controller,
          ),
          _ResultRow(
            title: '4K Q1T1',
            read: StorageBenchmarkTest.random4KQ1T1Read,
            write: StorageBenchmarkTest.random4KQ1T1Write,
            controller: controller,
          ),
          _ResultRow(
            title: '4K Q8T1',
            read: StorageBenchmarkTest.random4KQ8T1Read,
            write: StorageBenchmarkTest.random4KQ8T1Write,
            controller: controller,
          ),
          _ResultRow(
            title: '4K Q1T4',
            read: StorageBenchmarkTest.random4KQ1T4Read,
            write: StorageBenchmarkTest.random4KQ1T4Write,
            controller: controller,
          ),
          const SizedBox(height: 12),
          const _SectionLabel('DATABASE'),
          const SizedBox(height: 8),
          Row(
            children: [
              Expanded(
                child: _StorageResultCard(
                  test: StorageBenchmarkTest.sqliteInsert,
                  controller: controller,
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: _StorageResultCard(
                  test: StorageBenchmarkTest.sqliteDelete,
                  controller: controller,
                ),
              ),
            ],
          ),
          const SizedBox(height: 14),
          SizedBox(
            height: 34,
            child: Align(
              alignment: Alignment.centerLeft,
              child: Text(
                _statusLabel(snapshot, controller.lastError),
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: snapshot.state == StorageBenchmarkState.error ||
                          controller.lastError != null
                      ? const Color(0xFFFF8A80)
                      : const Color(0xFF8C989F),
                  fontSize: 11,
                  height: 1.35,
                ),
              ),
            ),
          ),
          const SizedBox(height: 10),
          Row(
            children: [
              Expanded(
                child: ElevatedButton(
                  onPressed: controller.isRunning ? null : controller.startAll,
                  child: const Text('BENCH ALL'),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: OutlinedButton(
                  onPressed: controller.isRunning ? controller.stop : null,
                  child: const Text('STOP'),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  static String _statusLabel(
    StorageBenchmarkSnapshot snapshot,
    Object? controllerError,
  ) {
    if (controllerError != null) return controllerError.toString();
    if (snapshot.state == StorageBenchmarkState.error) {
      return _errorLabel(snapshot.errorCode);
    }
    final state = switch (snapshot.state) {
      StorageBenchmarkState.idle => 'Ready',
      StorageBenchmarkState.preparing => 'Preparing test file',
      StorageBenchmarkState.warmingUp => 'Warming up',
      StorageBenchmarkState.measuring => 'Measuring',
      StorageBenchmarkState.flushing => 'Flushing writes',
      StorageBenchmarkState.stopping => 'Stopping safely',
      StorageBenchmarkState.completed => 'Completed',
      StorageBenchmarkState.stopped => 'Stopped',
      StorageBenchmarkState.error => 'Error',
    };
    if (!snapshot.state.isRunning) return state;
    final mode = switch (snapshot.ioMode) {
      StorageIoMode.direct => 'DIRECT',
      StorageIoMode.bufferedCompatibility => 'COMPATIBILITY',
      StorageIoMode.sqlite => 'SQLITE',
      StorageIoMode.unavailable => '',
    };
    final suffix = mode.isEmpty ? '' : ' • $mode';
    return '$state • ${snapshot.activeTest.label}$suffix';
  }

  static String _errorLabel(int code) => switch (code) {
        -30 => 'Insufficient free internal storage for this benchmark.',
        -31 => 'Private benchmark directory is unavailable.',
        -32 => 'Unable to create the temporary benchmark file.',
        -34 => 'Direct I/O alignment is unsupported on this device.',
        -35 => 'Unable to allocate an aligned I/O buffer.',
        -36 => 'Storage I/O failed before a reliable result was produced.',
        -37 => 'Native AIO QD8 is unavailable on this device.',
        -38 => 'SQLite benchmark failed.',
        -39 => 'Unable to create all four storage workers.',
        _ => 'Storage benchmark failed with error $code.',
      };
}

class _ColumnHeaders extends StatelessWidget {
  const _ColumnHeaders();

  @override
  Widget build(BuildContext context) {
    return const Row(
      children: [
        SizedBox(width: 84),
        Expanded(child: _HeaderLabel('READ')),
        SizedBox(width: 8),
        Expanded(child: _HeaderLabel('WRITE')),
      ],
    );
  }
}

class _HeaderLabel extends StatelessWidget {
  const _HeaderLabel(this.label);
  final String label;

  @override
  Widget build(BuildContext context) {
    return Text(
      label,
      textAlign: TextAlign.center,
      style: const TextStyle(
        color: Color(0xFF8C989F),
        fontSize: 11,
        fontWeight: FontWeight.w700,
        letterSpacing: 1.1,
      ),
    );
  }
}

class _ResultRow extends StatelessWidget {
  const _ResultRow({
    required this.title,
    required this.read,
    required this.write,
    required this.controller,
  });

  final String title;
  final StorageBenchmarkTest read;
  final StorageBenchmarkTest write;
  final StorageBenchmarkController controller;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Row(
        children: [
          SizedBox(
            width: 84,
            child: Text(
              title,
              maxLines: 2,
              style: const TextStyle(
                color: Color(0xFF8C989F),
                fontSize: 11,
                height: 1.25,
                fontWeight: FontWeight.w700,
                letterSpacing: 1.1,
              ),
            ),
          ),
          Expanded(
            child: _StorageResultCard(test: read, controller: controller),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: _StorageResultCard(test: write, controller: controller),
          ),
        ],
      ),
    );
  }
}

class _StorageResultCard extends StatelessWidget {
  const _StorageResultCard({required this.test, required this.controller});

  final StorageBenchmarkTest test;
  final StorageBenchmarkController controller;

  @override
  Widget build(BuildContext context) {
    final snapshot = controller.snapshot;
    final live = snapshot.state.isRunning && snapshot.activeTest == test;
    final result = controller.resultFor(test);
    final enabled = !controller.isRunning;
    final validResult = result != null && result.valid;
    final primary = live
        ? _livePrimary(snapshot, test.metric)
        : _resultPrimary(result, test.metric);
    final secondary = live
        ? _liveSecondary(snapshot, test.metric)
        : _resultSecondary(result, test.metric);
    final status =
        result != null && result.errorCode != 0 ? 'UNAVAILABLE' : null;

    return Semantics(
      button: true,
      enabled: enabled,
      label: test.label,
      child: Material(
        color: Colors.transparent,
        child: InkWell(
          onTap: enabled ? () => controller.startSingle(test) : null,
          borderRadius: BorderRadius.circular(5),
          child: AnimatedContainer(
            duration: const Duration(milliseconds: 160),
            height: 86,
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 9),
            decoration: BoxDecoration(
              color: const Color(0xFF1A1F24),
              border: Border.all(
                color: live ? const Color(0xFF49B6A7) : const Color(0xFF293139),
              ),
              borderRadius: BorderRadius.circular(5),
            ),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                if (test.metric == StorageMetric.rows) ...[
                  Text(
                    test == StorageBenchmarkTest.sqliteInsert
                        ? 'SQLITE INSERT'
                        : 'SQLITE DELETE',
                    maxLines: 1,
                    style: const TextStyle(
                      color: Color(0xFFB7C0C5),
                      fontSize: 9,
                      fontWeight: FontWeight.w700,
                      letterSpacing: .55,
                    ),
                  ),
                  const SizedBox(height: 3),
                ],
                SizedBox(
                  width: double.infinity,
                  height: test.metric == StorageMetric.rows ? 23 : 27,
                  child: FittedBox(
                    fit: BoxFit.scaleDown,
                    alignment: Alignment.center,
                    child: Text(
                      primary,
                      maxLines: 1,
                      style: TextStyle(
                        color: live || validResult
                            ? const Color(0xFF49B6A7)
                            : const Color(0xFF6E787E),
                        fontSize: 24,
                        height: 1,
                        fontWeight: FontWeight.w700,
                        fontFeatures: const [FontFeature.tabularFigures()],
                      ),
                    ),
                  ),
                ),
                const SizedBox(height: 6),
                SizedBox(
                  height: 16,
                  child: FittedBox(
                    fit: BoxFit.scaleDown,
                    child: Text(
                      status ?? secondary,
                      maxLines: 1,
                      style: TextStyle(
                        color: result != null && result.errorCode != 0
                            ? const Color(0xFFFF8A80)
                            : result == null
                                ? const Color(0xFF49B6A7)
                                : const Color(0xFF8C989F),
                        fontSize: 10,
                        fontWeight: FontWeight.w600,
                        letterSpacing: .25,
                        fontFeatures: const [FontFeature.tabularFigures()],
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  static String _livePrimary(
    StorageBenchmarkSnapshot snapshot,
    StorageMetric metric,
  ) =>
      switch (metric) {
        StorageMetric.rows => _rows(snapshot.currentRowsPerSecond),
        StorageMetric.random => _randomMbps(snapshot.currentMbps),
        StorageMetric.throughput => _mbps(snapshot.currentMbps),
      };

  static String _resultPrimary(
    StorageBenchmarkResult? result,
    StorageMetric metric,
  ) {
    if (result == null || !result.valid) return '—';
    return switch (metric) {
      StorageMetric.rows => _rows(result.rowsPerSecond),
      StorageMetric.random => _randomMbps(result.mbps),
      StorageMetric.throughput => _mbps(result.mbps),
    };
  }

  static String _liveSecondary(
    StorageBenchmarkSnapshot snapshot,
    StorageMetric metric,
  ) =>
      switch (metric) {
        StorageMetric.random => _iops(snapshot.currentIops),
        StorageMetric.rows => 'rows/s',
        StorageMetric.throughput => 'MB/s',
      };

  static String _resultSecondary(
    StorageBenchmarkResult? result,
    StorageMetric metric,
  ) {
    if (result == null || !result.valid) return 'TAP TO RUN';
    return switch (metric) {
      StorageMetric.random => _iops(result.iops),
      StorageMetric.rows => 'rows/s',
      StorageMetric.throughput => 'MB/s',
    };
  }

  static String _mbps(double value) {
    if (!value.isFinite || value <= 0) return '—';
    final digits = value >= 1000
        ? 0
        : value >= 100
            ? 1
            : 2;
    return value.toStringAsFixed(digits);
  }

  static String _randomMbps(double value) {
    final formatted = _mbps(value);
    return formatted == '—' ? formatted : '$formatted MB/s';
  }

  static String _iops(double value) {
    if (!value.isFinite || value <= 0) return '— IOPS';
    return value >= 1000
        ? '${(value / 1000).toStringAsFixed(1)}K IOPS'
        : '${value.toStringAsFixed(0)} IOPS';
  }

  static String _rows(double value) {
    if (!value.isFinite || value <= 0) return '—';
    return value >= 1000
        ? '${(value / 1000).toStringAsFixed(1)}K'
        : value.toStringAsFixed(0);
  }
}

class _SectionLabel extends StatelessWidget {
  const _SectionLabel(this.label);
  final String label;

  @override
  Widget build(BuildContext context) {
    return Text(
      label,
      style: const TextStyle(
        color: Color(0xFF8C989F),
        fontSize: 11,
        fontWeight: FontWeight.w700,
        letterSpacing: 1.1,
      ),
    );
  }
}

class _StorageLoadState extends StatelessWidget {
  const _StorageLoadState({required this.error});
  final Object? error;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Text(
          error == null ? 'Preparing Storage benchmark…' : error.toString(),
          textAlign: TextAlign.center,
          style: TextStyle(
            color: error == null
                ? const Color(0xFF8C989F)
                : const Color(0xFFFF8A80),
          ),
        ),
      ),
    );
  }
}
