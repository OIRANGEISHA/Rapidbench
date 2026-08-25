import 'package:benchmark_native/benchmark_native.dart';
import 'package:flutter/material.dart';

class MemoryBenchPage extends StatefulWidget {
  const MemoryBenchPage({super.key});

  @override
  State<MemoryBenchPage> createState() => _MemoryBenchPageState();
}

class _MemoryBenchPageState extends State<MemoryBenchPage> {
  MemoryBenchmarkController? _controller;
  Object? _initializationError;

  @override
  void initState() {
    super.initState();
    try {
      _controller = MemoryBenchmarkController();
    } catch (error) {
      _initializationError = error;
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
      appBar: AppBar(elevation: 0, title: const Text('MEMORY BENCH')),
      body: controller == null
          ? _MemoryLoadFailure(error: _initializationError)
          : AnimatedBuilder(
              animation: controller,
              builder: (context, _) => _MemoryBody(controller: controller),
            ),
    );
  }
}

class _MemoryBody extends StatelessWidget {
  const _MemoryBody({required this.controller});

  final MemoryBenchmarkController controller;

  @override
  Widget build(BuildContext context) {
    final snapshot = controller.snapshot;
    return SafeArea(
      child: ListView(
        padding: const EdgeInsets.fromLTRB(20, 16, 20, 28),
        children: [
          const Text(
            'SUSTAINED MEMORY BANDWIDTH',
            style: TextStyle(
              color: Color(0xFF8C989F),
              fontSize: 12,
              letterSpacing: 1.3,
            ),
          ),
          const SizedBox(height: 10),
          for (final test in MemoryBenchmarkTest.values) ...[
            _MemoryResultCard(
              test: test,
              result: snapshot.state.isRunning && snapshot.test == test
                  ? snapshot
                  : controller.resultFor(test),
              live: snapshot.state.isRunning && snapshot.test == test,
              onTap: controller.isRunning
                  ? null
                  : () => controller.startSingle(test),
            ),
            if (test != MemoryBenchmarkTest.values.last)
              const SizedBox(height: 8),
          ],
          const SizedBox(height: 18),
          _InformationRow(
            label: 'DRAM Frequency',
            value: _frequencyLabel(controller.frequency),
          ),
          _InformationRow(
            label: 'State',
            value: _stateLabel(snapshot.state, snapshot.test),
          ),
          if (snapshot.affinityFailures > 0)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(
                '${snapshot.affinityFailures} worker affinity request(s) were '
                'not accepted; this result may be scheduler-limited.',
                style: const TextStyle(
                  color: Color(0xFFFF8A80),
                  fontSize: 12,
                  height: 1.4,
                ),
              ),
            ),
          if (snapshot.performanceRequestFailures > 0)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(
                '${snapshot.performanceRequestFailures} worker performance '
                'request(s) were not accepted; this result may be '
                'scheduler-limited.',
                style: const TextStyle(
                  color: Color(0xFFFF8A80),
                  fontSize: 12,
                  height: 1.4,
                ),
              ),
            ),
          if (snapshot.state == BenchmarkState.error)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(
                _memoryError(snapshot.errorCode),
                style: const TextStyle(color: Color(0xFFFF8A80)),
              ),
            ),
          if (controller.lastError != null)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(
                controller.lastError.toString(),
                style: const TextStyle(color: Color(0xFFFF8A80)),
              ),
            ),
          const SizedBox(height: 18),
          Row(
            children: [
              Expanded(
                child: ElevatedButton(
                  onPressed: controller.isRunning ? null : controller.start,
                  child: const Text('BENCH MEMORY'),
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

  static String _frequencyLabel(MemoryFrequencyInfo? frequency) {
    if (frequency == null || !frequency.available) {
      return 'Unavailable';
    }
    final parts = <String>[];
    if (frequency.hasCurrent) {
      parts.add('${(frequency.currentHz / 1000000).round()} MHz current');
    }
    if (frequency.hasMaximum) {
      parts.add('${(frequency.maximumHz / 1000000).round()} MHz max');
    }
    return parts.isEmpty ? 'Unavailable' : parts.join(' / ');
  }

  static String _stateLabel(
    BenchmarkState state,
    MemoryBenchmarkTest activeTest,
  ) {
    final label = switch (state) {
      BenchmarkState.idle => 'Idle',
      BenchmarkState.preparing => 'Preparing',
      BenchmarkState.warmingUp => 'Warming up',
      BenchmarkState.measuring => 'Measuring',
      BenchmarkState.completed => 'Completed',
      BenchmarkState.cancelled => 'Stopped',
      BenchmarkState.error => 'Error',
    };
    return state.isRunning ? '$label ${activeTest.label}' : label;
  }

  static String _memoryError(int code) {
    return switch (code) {
      -20 => 'Unable to allocate a safe memory benchmark buffer.',
      -21 => 'No eligible CPU core is available for the memory test.',
      -22 => 'A native memory worker could not be created.',
      _ => 'Native memory test failed with error $code.',
    };
  }
}

class _MemoryResultCard extends StatelessWidget {
  const _MemoryResultCard({
    required this.test,
    required this.result,
    required this.live,
    required this.onTap,
  });

  final MemoryBenchmarkTest test;
  final MemoryBenchmarkSnapshot? result;
  final bool live;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final value = result?.bandwidthGbps;
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(5),
        child: Container(
          padding: const EdgeInsets.fromLTRB(16, 14, 16, 14),
          decoration: BoxDecoration(
            color: const Color(0xFF1A1F24),
            border: Border.all(
              color: live ? const Color(0xFF49B6A7) : const Color(0xFF293139),
            ),
            borderRadius: BorderRadius.circular(5),
          ),
          child: Row(
            children: [
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text(
                          test == MemoryBenchmarkTest.copy
                              ? 'COPY'
                              : test.label.toUpperCase(),
                          style: const TextStyle(
                            color: Color(0xFFB7C0C5),
                            fontSize: 12,
                            fontWeight: FontWeight.w700,
                            letterSpacing: 1,
                          ),
                        ),
                        const SizedBox(width: 5),
                        Icon(
                          live ? Icons.bolt_rounded : Icons.play_arrow_rounded,
                          color: const Color(0xFF49B6A7),
                          size: 17,
                        ),
                      ],
                    ),
                    const SizedBox(height: 7),
                    Text(
                      result == null
                          ? 'Tap to run this test'
                          : '${_bufferLabel(result!.bufferBytes)} • '
                              '${result!.threadCount} threads',
                      style: const TextStyle(
                        color: Color(0xFF78838A),
                        fontSize: 11,
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 12),
              ConstrainedBox(
                constraints: const BoxConstraints(minWidth: 132),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.end,
                  children: [
                    FittedBox(
                      fit: BoxFit.scaleDown,
                      child: Text(
                        value == null || value <= 0
                            ? '—'
                            : value.toStringAsFixed(2),
                        maxLines: 1,
                        style: const TextStyle(
                          fontSize: 31,
                          height: 1,
                          fontWeight: FontWeight.w700,
                          color: Color(0xFF49B6A7),
                          fontFeatures: [FontFeature.tabularFigures()],
                        ),
                      ),
                    ),
                    const SizedBox(height: 3),
                    const Text(
                      'GB/s',
                      style: TextStyle(
                        color: Color(0xFF8C989F),
                        fontSize: 11,
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  static String _bufferLabel(int bytes) {
    if (bytes <= 0) {
      return 'Preparing buffer';
    }
    return '${(bytes / (1024 * 1024)).round()} MiB';
  }
}

class _InformationRow extends StatelessWidget {
  const _InformationRow({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(
            child:
                Text(label, style: const TextStyle(color: Color(0xFF8C989F))),
          ),
          const SizedBox(width: 16),
          Flexible(
            child: Text(
              value,
              textAlign: TextAlign.right,
              style:
                  const TextStyle(fontFeatures: [FontFeature.tabularFigures()]),
            ),
          ),
        ],
      ),
    );
  }
}

class _MemoryLoadFailure extends StatelessWidget {
  const _MemoryLoadFailure({required this.error});

  final Object? error;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(20),
      child: Text(
        'Native memory engine could not be loaded.\n${error ?? 'Unknown error'}',
        style: const TextStyle(color: Color(0xFFFF8A80), height: 1.4),
      ),
    );
  }
}


