import 'package:benchmark_native/benchmark_native.dart';
import 'package:flutter/material.dart';

const _accent = Color(0xFF49B6A7);
const _secondary = Color(0xFF8C989F);

class CpuApplicationSection extends StatefulWidget {
  const CpuApplicationSection({super.key, this.controller});
  final CpuApplicationController? controller;
  @override
  State<CpuApplicationSection> createState() => _CpuApplicationSectionState();
}

class _CpuApplicationSectionState extends State<CpuApplicationSection> {
  CpuApplicationController? _controller;
  Object? _initializationError;
  @override
  void initState() {
    super.initState();
    try {
      _controller = widget.controller ?? CpuApplicationController();
    } catch (error) {
      // Keep legacy CPU usable, but preserve the additive API failure reason.
      _initializationError = error;
    }
  }

  @override
  void dispose() {
    if (widget.controller == null) _controller?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final controller = _controller;
    if (controller == null) {
      return Text('Application tests unavailable: $_initializationError',
          style: const TextStyle(color: _secondary, fontSize: 12));
    }
    return AnimatedBuilder(
        animation: controller,
        builder: (context, _) {
          final snapshot = controller.snapshot;
          final selection = controller.multi
              ? 'All cores · one worker per core'
              : 'Auto · highest-performance core';
          return ExpansionTile(
            tilePadding: EdgeInsets.zero,
            childrenPadding: const EdgeInsets.only(bottom: 8),
            iconColor: _accent,
            collapsedIconColor: _secondary,
            title: Text(
                controller.isRunning
                    ? 'APPLICATION WORKLOADS · RUNNING'
                    : 'APPLICATION WORKLOADS',
                style: const TextStyle(
                    color: _accent, fontSize: 13, fontWeight: FontWeight.w600)),
            subtitle: const Text('Optional · separate from the CPU score',
                style: TextStyle(color: _secondary, fontSize: 12)),
            children: [
              Align(
                  alignment: Alignment.centerLeft,
                  child: Wrap(spacing: 8, runSpacing: 4, children: [
                    ChoiceChip(
                        label: const Text('Single'),
                        selected: !controller.multi,
                        selectedColor: _accent.withValues(alpha: 0.25),
                        onSelected: controller.canStart
                            ? (_) => controller.selectMulti(false)
                            : null),
                    ChoiceChip(
                        label: const Text('Multi'),
                        selected: controller.multi,
                        selectedColor: _accent.withValues(alpha: 0.25),
                        onSelected: controller.canStart
                            ? (_) => controller.selectMulti(true)
                            : null),
                  ])),
              const SizedBox(height: 6),
              Align(
                  alignment: Alignment.centerLeft,
                  child: Text(selection,
                      style: const TextStyle(
                          color: _secondary, fontSize: 12, height: 1.4))),
              const SizedBox(height: 12),
              for (final test in CpuApplicationTest.values)
                Padding(
                  padding: const EdgeInsets.only(bottom: 8),
                  child: CpuApplicationResultCard(
                      test: test,
                      live: controller.isRunning && snapshot.test == test
                          ? snapshot
                          : null,
                      result: controller.resultFor(test),
                      onTap: controller.canStart
                          ? () => controller.start(test)
                          : null),
                ),
              if (controller.lastError != null)
                Padding(
                    padding: const EdgeInsets.symmetric(vertical: 8),
                    child: Text(controller.lastError.toString(),
                        style: const TextStyle(
                            color: Color(0xFFE0A15C), fontSize: 12))),
              SizedBox(
                  width: double.infinity,
                  child: OutlinedButton(
                      onPressed: controller.isRunning ? controller.stop : null,
                      child: const Text('STOP APPLICATION TEST'))),
            ],
          );
        });
  }
}

class CpuApplicationResultCard extends StatelessWidget {
  const CpuApplicationResultCard(
      {super.key, required this.test, this.live, this.result, this.onTap});
  final CpuApplicationTest test;
  final CpuApplicationSnapshot? live, result;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final current = live ?? result;
    final measuring = live?.state == BenchmarkState.measuring;
    final showValue = current != null &&
        current.unitsPerSecond.isFinite &&
        current.unitsPerSecond > 0 &&
        (measuring || current.hasResult);
    final status = live != null
        ? (measuring ? 'MEASURING' : 'PREPARING / WARM-UP')
        : result?.hasResult == true
            ? result!.state == BenchmarkState.cancelled
                ? 'STOPPED · PARTIAL'
                : 'COMPLETED · VERIFIED'
            : 'TAP TO RUN';
    return Material(
      color: const Color(0xFF1A1F24),
      borderRadius: BorderRadius.circular(10),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(10),
        child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(test.label,
                    style: const TextStyle(
                        color: Color(0xFFE7ECEF),
                        fontSize: 14,
                        fontWeight: FontWeight.w600)),
                const SizedBox(height: 5),
                Text(test.description,
                    style: const TextStyle(color: _secondary, fontSize: 12)),
                const SizedBox(height: 12),
                SizedBox(
                    width: double.infinity,
                    child: FittedBox(
                        fit: BoxFit.scaleDown,
                        alignment: Alignment.centerLeft,
                        child: Text(
                            showValue
                                ? current.displayValue.toStringAsFixed(2)
                                : '—',
                            style: const TextStyle(
                                color: _accent,
                                fontSize: 34,
                                fontWeight: FontWeight.w600,
                                fontFeatures: [
                                  FontFeature.tabularFigures()
                                ])))),
                Text(test.unit,
                    style: const TextStyle(color: _secondary, fontSize: 12)),
                const SizedBox(height: 10),
                Text(status,
                    style: const TextStyle(
                        color: _accent,
                        fontSize: 11,
                        fontWeight: FontWeight.w600)),
                if (current != null && current.threadCount > 0) ...[
                  const SizedBox(height: 6),
                  Text(current.executionLabel,
                      style: const TextStyle(color: _secondary, fontSize: 12)),
                  if (current.inputBytes > 0)
                    Text(
                        '${(current.inputBytes / 1024).toStringAsFixed(1)} KiB input / worker · v${current.methodVersion}',
                        style:
                            const TextStyle(color: _secondary, fontSize: 11)),
                  if (current.affinityFailures > 0)
                    Text(
                        'Affinity fallback: ${current.affinityFailures} worker(s)',
                        style: const TextStyle(
                            color: Color(0xFFE0A15C), fontSize: 12)),
                ],
              ],
            )),
      ),
    );
  }
}
