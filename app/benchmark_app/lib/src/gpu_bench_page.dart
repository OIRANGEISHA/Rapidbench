import 'package:benchmark_native/benchmark_native.dart';
import 'package:flutter/material.dart';

class GpuBenchPage extends StatefulWidget {
  const GpuBenchPage({super.key});

  @override
  State<GpuBenchPage> createState() => _GpuBenchPageState();
}

class _GpuBenchPageState extends State<GpuBenchPage> {
  GpuBenchmarkController? _controller;
  Object? _initializationError;

  @override
  void initState() {
    super.initState();
    try {
      _controller = GpuBenchmarkController();
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
      appBar: AppBar(elevation: 0, title: const Text('GPU BENCH')),
      body: controller == null
          ? _GpuLoadFailure(error: _initializationError)
          : AnimatedBuilder(
              animation: controller,
              builder: (context, _) => _GpuBody(controller: controller),
            ),
    );
  }
}

class _GpuBody extends StatelessWidget {
  const _GpuBody({required this.controller});

  final GpuBenchmarkController controller;

  static const _tests = <GpuBenchmarkTest>[
    GpuBenchmarkTest.fp32,
    GpuBenchmarkTest.fp16,
    GpuBenchmarkTest.int32,
    GpuBenchmarkTest.mixed,
    GpuBenchmarkTest.memoryBandwidth,
  ];

  @override
  Widget build(BuildContext context) {
    final capabilities = controller.capabilities;
    final snapshot = controller.snapshot;
    return SafeArea(
      child: ListView(
        padding: const EdgeInsets.fromLTRB(20, 16, 20, 28),
        children: [
          _GpuIdentityCard(capabilities: capabilities),
          const SizedBox(height: 18),
          const Text(
            'VULKAN COMPUTE',
            style: TextStyle(
              color: Color(0xFF8C989F),
              fontSize: 12,
              letterSpacing: 1.3,
            ),
          ),
          const SizedBox(height: 10),
          for (final test in _tests) ...[
            _GpuResultCard(
              test: test,
              snapshot: snapshot,
              enabled: capabilities.available && !controller.isRunning,
              live: snapshot.state.isRunning && snapshot.activeTest == test,
              onTap: () => controller.startSingle(test),
            ),
            if (test != _tests.last) const SizedBox(height: 8),
          ],
          const SizedBox(height: 18),
          _InformationRow(
            label: 'State',
            value: _stateLabel(snapshot),
          ),
          _InformationRow(
            label: 'Timing',
            value: snapshot.timingMode.label,
          ),
          if (snapshot.bufferBytes > 0)
            _InformationRow(
              label: 'Bandwidth Working Set',
              value: '${(snapshot.bufferBytes / (1024 * 1024)).round()} MiB',
            ),
          if (snapshot.reducedWorkingSet)
            const Padding(
              padding: EdgeInsets.only(top: 6),
              child: Text(
                'Reduced working set is active because the preferred Vulkan '
                'storage-buffer allocation was unavailable.',
                style: TextStyle(
                  color: Color(0xFFFFCC80),
                  fontSize: 11,
                  height: 1.35,
                ),
              ),
            ),
          if (!capabilities.available)
            Padding(
              padding: const EdgeInsets.only(top: 10),
              child: Text(
                'Vulkan Compute Unavailable\n${capabilities.reason}',
                style: const TextStyle(
                  color: Color(0xFFFF8A80),
                  fontSize: 12,
                  height: 1.4,
                ),
              ),
            ),
          if (snapshot.state == GpuBenchmarkState.error ||
              snapshot.lastError.isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(top: 10),
              child: Text(
                snapshot.lastError.isNotEmpty
                    ? snapshot.lastError
                    : 'Native GPU test failed with error ${snapshot.errorCode}.',
                style: const TextStyle(color: Color(0xFFFF8A80)),
              ),
            ),
          if (controller.lastError != null)
            Padding(
              padding: const EdgeInsets.only(top: 10),
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
                  onPressed: capabilities.available && !controller.isRunning
                      ? controller.startAll
                      : null,
                  child: const Text('BENCH GPU'),
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

  static String _stateLabel(GpuBenchmarkSnapshot snapshot) {
    final label = switch (snapshot.state) {
      GpuBenchmarkState.idle => 'Idle',
      GpuBenchmarkState.warmingUp => 'Warming up',
      GpuBenchmarkState.running => 'Running',
      GpuBenchmarkState.stopping => 'Stopping',
      GpuBenchmarkState.stopped => 'Stopped',
      GpuBenchmarkState.completed => 'Completed',
      GpuBenchmarkState.error => 'Error',
    };
    return snapshot.state.isRunning &&
            snapshot.activeTest != GpuBenchmarkTest.none
        ? '$label • ${snapshot.activeTest.label}'
        : label;
  }
}

class _GpuIdentityCard extends StatelessWidget {
  const _GpuIdentityCard({required this.capabilities});

  final GpuCapabilities capabilities;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.fromLTRB(15, 13, 15, 13),
      decoration: BoxDecoration(
        color: const Color(0xFF1A1F24),
        border: Border.all(color: const Color(0xFF293139)),
        borderRadius: BorderRadius.circular(5),
      ),
      child: Row(
        children: [
          const Icon(
            Icons.developer_board_outlined,
            color: Color(0xFF49B6A7),
            size: 25,
          ),
          const SizedBox(width: 13),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  capabilities.deviceName,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w700,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  capabilities.available
                      ? 'Vulkan ${capabilities.apiVersion} • Compute Queue ${capabilities.computeQueueFamily}'
                      : 'Vulkan Compute Unavailable',
                  style: const TextStyle(
                    color: Color(0xFF8C989F),
                    fontSize: 11,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _GpuResultCard extends StatelessWidget {
  const _GpuResultCard({
    required this.test,
    required this.snapshot,
    required this.enabled,
    required this.live,
    required this.onTap,
  });

  final GpuBenchmarkTest test;
  final GpuBenchmarkSnapshot snapshot;
  final bool enabled;
  final bool live;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final formatted = _formatValue(test, snapshot.valueFor(test), snapshot);
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: enabled ? onTap : null,
        borderRadius: BorderRadius.circular(5),
        child: Container(
          padding: const EdgeInsets.fromLTRB(15, 13, 15, 13),
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
                    Wrap(
                      spacing: 7,
                      runSpacing: 5,
                      crossAxisAlignment: WrapCrossAlignment.center,
                      children: [
                        Text(
                          test.label.toUpperCase(),
                          style: const TextStyle(
                            color: Color(0xFFB7C0C5),
                            fontSize: 12,
                            fontWeight: FontWeight.w700,
                            letterSpacing: 0.85,
                          ),
                        ),
                        if (test == GpuBenchmarkTest.fp16)
                          _ModeBadge(mode: snapshot.fp16Mode),
                      ],
                    ),
                    const SizedBox(height: 7),
                    Text(
                      _detailLabel(test, snapshot),
                      maxLines: 2,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                        color: Color(0xFF78838A),
                        fontSize: 11,
                        height: 1.25,
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 10),
              ConstrainedBox(
                constraints: const BoxConstraints(minWidth: 112, maxWidth: 142),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.end,
                  children: [
                    FittedBox(
                      fit: BoxFit.scaleDown,
                      child: Text(
                        formatted.$1,
                        maxLines: 1,
                        style: const TextStyle(
                          fontSize: 29,
                          height: 1,
                          fontWeight: FontWeight.w700,
                          color: Color(0xFF49B6A7),
                          fontFeatures: [FontFeature.tabularFigures()],
                        ),
                      ),
                    ),
                    const SizedBox(height: 3),
                    Text(
                      formatted.$2,
                      maxLines: 1,
                      style: const TextStyle(
                        color: Color(0xFF8C989F),
                        fontSize: 10.5,
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

  static (String, String) _formatValue(
    GpuBenchmarkTest test,
    double value,
    GpuBenchmarkSnapshot snapshot,
  ) {
    if (value <= 0 || !value.isFinite) {
      return ('—', _baseUnit(test, snapshot));
    }
    if (test == GpuBenchmarkTest.fp32 ||
        (test == GpuBenchmarkTest.fp16 &&
            snapshot.fp16Mode == GpuFp16Mode.native)) {
      return value >= 1000
          ? ((value / 1000).toStringAsFixed(2), 'TFLOPS')
          : (value.toStringAsFixed(1), 'GFLOPS');
    }
    if (test == GpuBenchmarkTest.int32) {
      return value >= 1000
          ? ((value / 1000).toStringAsFixed(2), 'TOPS')
          : (value.toStringAsFixed(1), 'GOPS');
    }
    return (value.toStringAsFixed(2), _baseUnit(test, snapshot));
  }

  static String _baseUnit(
    GpuBenchmarkTest test,
    GpuBenchmarkSnapshot snapshot,
  ) =>
      switch (test) {
        GpuBenchmarkTest.fp32 => 'GFLOPS',
        GpuBenchmarkTest.fp16 =>
          snapshot.fp16Mode == GpuFp16Mode.native ? 'GFLOPS' : 'GOp/s (FP32)',
        GpuBenchmarkTest.int32 => 'GOPS',
        GpuBenchmarkTest.mixed => 'GWork/s',
        GpuBenchmarkTest.memoryBandwidth => 'GB/s',
        _ => '',
      };

  String _detailLabel(
    GpuBenchmarkTest test,
    GpuBenchmarkSnapshot snapshot,
  ) {
    if (live) {
      return snapshot.state == GpuBenchmarkState.warmingUp
          ? 'Warming up GPU'
          : 'Measuring • ${snapshot.dispatchCount} dispatches/batch';
    }
    if (test == GpuBenchmarkTest.fp16) {
      if (snapshot.fp16Mode == GpuFp16Mode.emulated) {
        return 'Executed using FP32 fallback';
      }
      if (snapshot.fp16Scaling > 0) {
        return '${snapshot.fp16Scaling.toStringAsFixed(2)}× FP32';
      }
    }
    if (test == GpuBenchmarkTest.memoryBandwidth && snapshot.bufferBytes > 0) {
      return '${(snapshot.bufferBytes / (1024 * 1024)).round()} MiB read-dominant working set';
    }
    return snapshot.valueFor(test) > 0
        ? 'Tap to benchmark again'
        : 'Tap to run this test';
  }
}

class _ModeBadge extends StatelessWidget {
  const _ModeBadge({required this.mode});

  final GpuFp16Mode mode;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 3),
      decoration: BoxDecoration(
        color: const Color(0xFF12332F),
        border: Border.all(color: const Color(0xFF245E56)),
        borderRadius: BorderRadius.circular(3),
      ),
      child: Text(
        mode == GpuFp16Mode.native ? 'NATIVE' : 'EMULATED',
        style: const TextStyle(
          color: Color(0xFF78D0C2),
          fontSize: 9,
          fontWeight: FontWeight.w700,
          letterSpacing: 0.6,
        ),
      ),
    );
  }
}

class _InformationRow extends StatelessWidget {
  const _InformationRow({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 5),
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

class _GpuLoadFailure extends StatelessWidget {
  const _GpuLoadFailure({required this.error});

  final Object? error;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(20),
      child: Text(
        'Native GPU engine could not be loaded.\n${error ?? 'Unknown error'}',
        style: const TextStyle(color: Color(0xFFFF8A80), height: 1.4),
      ),
    );
  }
}
