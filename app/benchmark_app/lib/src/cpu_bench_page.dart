import 'package:benchmark_native/benchmark_native.dart';
import 'package:flutter/material.dart';

class CpuBenchPage extends StatefulWidget {
  const CpuBenchPage({super.key});

  @override
  State<CpuBenchPage> createState() => _CpuBenchPageState();
}

class _CpuBenchPageState extends State<CpuBenchPage> {
  BenchmarkController? _controller;
  Object? _initializationError;

  @override
  void initState() {
    super.initState();
    try {
      _controller = BenchmarkController();
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
      appBar: AppBar(
        elevation: 0,
        title: const Text('CPU BENCH'),
      ),
      body: controller == null
          ? _LoadFailure(error: _initializationError)
          : AnimatedBuilder(
              animation: controller,
              builder: (context, _) => _BenchBody(controller: controller),
            ),
    );
  }
}

class _BenchBody extends StatelessWidget {
  const _BenchBody({required this.controller});

  final BenchmarkController controller;

  @override
  Widget build(BuildContext context) {
    final snapshot = controller.snapshot;
    final topology = controller.topology;
    final running = controller.isRunning;
    final singleLive =
        running && controller.activeTest == BenchmarkTest.cpuSingle;
    final multiLive =
        running && controller.activeTest == BenchmarkTest.cpuMulti;
    final selectedSingleCpu = _cpuById(topology, controller.selectedSingleCpu);
    final selectedMultiCpus =
        _cpusForGroup(topology, controller.selectedMultiGroup);

    return SafeArea(
      child: ListView(
        padding: const EdgeInsets.fromLTRB(20, 16, 20, 28),
        children: [
          const _SectionLabel('CPU COMPUTE / WORKLOAD V2'),
          const SizedBox(height: 10),
          _ScoreBlock(
            label: 'CPU Single Thread',
            score: controller.singleScore,
            peakScore: controller.singleResult?.peakScore,
            live: singleLive,
            detail: '${_cpuChoiceLabel(selectedSingleCpu)}'
                '${running ? '' : ' • tap to choose'}',
            metrics: _singleRuntimeMetrics(
              controller.singleResult,
              selectedSingleCpu?.maxFrequencyKhz ?? 0,
            ),
            accentMetric: _affinityMetric(controller.singleResult),
            accentMetricWarning:
                (controller.singleResult?.affinityFailures ?? 0) > 0,
            onTap: running ? null : () => _showCpuPicker(context, controller),
            showPickerIndicator: true,
          ),
          const SizedBox(height: 8),
          _ScoreBlock(
            label: 'CPU Multi Thread',
            score: controller.multiScore,
            peakScore: controller.multiResult?.peakScore,
            live: multiLive,
            detail: '${_multiChoiceLabel(controller.selectedMultiGroup)}'
                ' • ${snapshot.test == BenchmarkTest.cpuMulti && snapshot.threadCount > 0 ? snapshot.threadCount : selectedMultiCpus.length} workers'
                '${running ? '' : ' • tap to choose'}',
            metrics: _multiRuntimeMetrics(
              controller.multiResult,
              topology,
              selectedMultiCpus,
            ),
            accentMetric: controller.multiThreadMultiplier == null
                ? null
                : 'Scaling '
                    '${controller.multiThreadMultiplier!.toStringAsFixed(2)}×',
            onTap: running
                ? null
                : () => _showMultiGroupPicker(context, controller),
            showPickerIndicator: true,
          ),
          const SizedBox(height: 20),
          _DataRow(
            label: 'State',
            value: _stateLabel(snapshot.state, snapshot.errorCode),
          ),
          _DataRow(
            label: 'Active test',
            value: _testLabel(controller.activeTest),
          ),
          if (snapshot.state == BenchmarkState.error)
            _Notice(
              text: _nativeErrorMessage(snapshot.errorCode),
              isWarning: true,
            ),
          if (snapshot.affinityFailures > 0)
            _Notice(
              text: 'Affinity issue affected ${snapshot.affinityFailures} '
                  'worker(s). Affected workers were retried or excluded; '
                  'treat this run as lower confidence.',
              isWarning: true,
            ),
          if (snapshot.state == BenchmarkState.completed &&
              !snapshot.performanceRequestActive)
            const _Notice(
              text: 'The OS did not accept a worker performance request and '
                  'no performance hint session was available. Frequency and '
                  'score may be limited by the device scheduler.',
              isWarning: true,
            ),
          if (controller.lastError != null)
            _Notice(text: controller.lastError.toString(), isWarning: true),
          const SizedBox(height: 20),
          Row(
            children: [
              Expanded(
                child: ElevatedButton(
                  onPressed: running ? null : controller.startCpuBench,
                  child: const Text('BENCH CPU'),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: OutlinedButton(
                  onPressed: running ? controller.stop : null,
                  child: const Text('STOP'),
                ),
              ),
            ],
          ),
          const SizedBox(height: 26),
          const _SectionLabel('CPU TOPOLOGY'),
          const SizedBox(height: 8),
          _DataRow(
            label: 'Logical / online / allowed',
            value:
                '${topology.cpus.length} / ${topology.onlineCount} / ${topology.allowedCount}',
          ),
          _DataRow(
            label: 'Performance groups',
            value: '${topology.performanceGroupCount}',
          ),
          _DataRow(
            label: 'Selected single',
            value: 'CPU ${controller.selectedSingleCpu}',
          ),
          _DataRow(
            label: 'Selected multi',
            value: _multiChoiceLabel(controller.selectedMultiGroup),
          ),
          const SizedBox(height: 8),
          ..._topologyLines(topology).map(
            (line) => Padding(
              padding: const EdgeInsets.symmetric(vertical: 3),
              child: Text(
                line,
                style: const TextStyle(
                  color: Color(0xFFB7C0C5),
                  fontSize: 12,
                  fontFeatures: [FontFeature.tabularFigures()],
                ),
              ),
            ),
          ),
          if (_topologyIsInferred(topology))
            const _Notice(
              text: 'Some capacity, frequency, or cluster data is unavailable. '
                  'Performance groups are best-effort and need device validation.',
            ),
        ],
      ),
    );
  }

  static List<CpuLogicalInfo> _cpusForGroup(
    CpuTopology topology,
    int? performanceGroup,
  ) {
    final cpus = topology.allowedCpus
        .where(
          (cpu) =>
              cpu.online &&
              (performanceGroup == null ||
                  cpu.performanceGroup == performanceGroup),
        )
        .toList()
      ..sort((left, right) => left.logicalCpu.compareTo(right.logicalCpu));
    return cpus;
  }

  static int _maximumFrequencyKhz(Iterable<CpuLogicalInfo> cpus) {
    return cpus.fold<int>(
      0,
      (current, cpu) =>
          cpu.maxFrequencyKhz > current ? cpu.maxFrequencyKhz : current,
    );
  }

  static String _multiChoiceLabel(int? performanceGroup) {
    return performanceGroup == null ? 'All cores' : 'G$performanceGroup';
  }

  static List<String> _singleRuntimeMetrics(
    BenchmarkSnapshot? result,
    int maximumFrequencyKhz,
  ) {
    if (result == null) {
      return const [];
    }
    final metrics = <String>[];
    final maximumMhz = (maximumFrequencyKhz / 1000).round();
    if (result.peakFrequencyMhz > 0) {
      metrics.add(
        maximumMhz > 0
            ? 'Peak ${result.peakFrequencyMhz} / $maximumMhz MHz'
            : 'Peak ${result.peakFrequencyMhz} MHz',
      );
    } else if (maximumMhz > 0) {
      metrics.add('Peak unavailable / $maximumMhz MHz');
    } else {
      metrics.add('Peak unavailable');
    }
    if (result.workerUtilization > 0) {
      metrics.add(
        'Busy ${(result.workerUtilization * 100).toStringAsFixed(0)}%',
      );
    }
    return metrics;
  }

  static List<String> _multiRuntimeMetrics(
    BenchmarkSnapshot? result,
    CpuTopology topology,
    List<CpuLogicalInfo> selectedCpus,
  ) {
    if (result == null) {
      return const [];
    }
    final metrics = <String>[];
    final selectedGroups = selectedCpus
        .map((cpu) => cpu.performanceGroup)
        .toSet()
        .toList()
      ..sort();
    for (final group in selectedGroups) {
      final peakMhz = group < result.peakFrequenciesMhzByGroup.length
          ? result.peakFrequenciesMhzByGroup[group]
          : 0;
      final maximumMhz =
          (_maximumFrequencyKhz(_cpusForGroup(topology, group)) / 1000).round();
      if (peakMhz > 0) {
        metrics.add(
          maximumMhz > 0
              ? 'G$group peak $peakMhz / $maximumMhz MHz'
              : 'G$group peak $peakMhz MHz',
        );
      } else {
        metrics.add(
          maximumMhz > 0
              ? 'G$group peak unavailable / $maximumMhz MHz'
              : 'G$group peak unavailable',
        );
      }
    }
    if (metrics.isEmpty && result.peakFrequencyMhz > 0) {
      final maximumMhz = (_maximumFrequencyKhz(selectedCpus) / 1000).round();
      metrics.add(
        maximumMhz > 0
            ? 'Peak ${result.peakFrequencyMhz} / $maximumMhz MHz'
            : 'Peak ${result.peakFrequencyMhz} MHz',
      );
    }
    if (result.workerUtilization > 0) {
      metrics.add(
        'Busy ${(result.workerUtilization * 100).toStringAsFixed(0)}%',
      );
    }
    return metrics;
  }

  static String? _affinityMetric(BenchmarkSnapshot? result) {
    if (result == null) {
      return null;
    }
    if (!result.affinityChecked) {
      return 'Pin check unavailable';
    }
    if (result.affinityFailures == 0) {
      return 'CPU pinned';
    }
    return 'Affinity ×${result.affinityFailures}';
  }

  static CpuLogicalInfo? _cpuById(CpuTopology topology, int logicalCpu) {
    for (final cpu in topology.allowedCpus) {
      if (cpu.online && cpu.logicalCpu == logicalCpu) {
        return cpu;
      }
    }
    return null;
  }

  static String _maxFrequencyLabel(int maxFrequencyKhz) {
    if (maxFrequencyKhz <= 0) {
      return 'Max frequency unavailable';
    }
    return 'Max ${(maxFrequencyKhz / 1000).toStringAsFixed(0)} MHz';
  }

  static String _cpuChoiceLabel(CpuLogicalInfo? cpu) {
    if (cpu == null) {
      return 'No selectable CPU';
    }
    return 'CPU ${cpu.logicalCpu} • '
        '${_maxFrequencyLabel(cpu.maxFrequencyKhz)}';
  }

  static Future<void> _showCpuPicker(
    BuildContext context,
    BenchmarkController controller,
  ) async {
    final cpus = controller.topology.allowedCpus
        .where((cpu) => cpu.online)
        .toList()
      ..sort((left, right) => left.logicalCpu.compareTo(right.logicalCpu));
    if (cpus.isEmpty) {
      return;
    }

    final selectedCpu = await showModalBottomSheet<int>(
      context: context,
      backgroundColor: const Color(0xFF171C20),
      showDragHandle: true,
      builder: (sheetContext) => SafeArea(
        child: SizedBox(
          height: MediaQuery.sizeOf(sheetContext).height * 0.62,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Padding(
                padding: EdgeInsets.fromLTRB(20, 2, 20, 12),
                child: Text(
                  'SELECT SINGLE-THREAD CPU',
                  style: TextStyle(
                    color: Color(0xFF8C989F),
                    fontSize: 12,
                    letterSpacing: 1.2,
                  ),
                ),
              ),
              const Divider(height: 1, color: Color(0xFF293139)),
              Expanded(
                child: ListView.separated(
                  itemCount: cpus.length,
                  separatorBuilder: (context, index) => const Divider(
                    height: 1,
                    indent: 20,
                    endIndent: 20,
                    color: Color(0xFF293139),
                  ),
                  itemBuilder: (context, index) {
                    final cpu = cpus[index];
                    final selected =
                        cpu.logicalCpu == controller.selectedSingleCpu;
                    return ListTile(
                      contentPadding: const EdgeInsets.symmetric(
                        horizontal: 20,
                        vertical: 3,
                      ),
                      title: Text(
                        'CPU ${cpu.logicalCpu}',
                        style: const TextStyle(fontWeight: FontWeight.w600),
                      ),
                      subtitle: Text(
                        '${_maxFrequencyLabel(cpu.maxFrequencyKhz)}'
                        ' • Performance group ${cpu.performanceGroup}',
                        style: const TextStyle(
                          color: Color(0xFF8C989F),
                          fontSize: 12,
                        ),
                      ),
                      trailing: selected
                          ? const Icon(
                              Icons.check,
                              color: Color(0xFF49B6A7),
                            )
                          : null,
                      onTap: () =>
                          Navigator.of(sheetContext).pop(cpu.logicalCpu),
                    );
                  },
                ),
              ),
            ],
          ),
        ),
      ),
    );

    if (selectedCpu != null && !controller.isRunning) {
      controller.selectSingleCpu(selectedCpu);
    }
  }

  static Future<void> _showMultiGroupPicker(
    BuildContext context,
    BenchmarkController controller,
  ) async {
    final topology = controller.topology;
    final groups = topology.allowedCpus
        .where((cpu) => cpu.online)
        .map((cpu) => cpu.performanceGroup)
        .toSet()
        .toList()
      ..sort();
    final options = <int?>[null, ...groups];

    final selection = await showModalBottomSheet<int>(
      context: context,
      backgroundColor: const Color(0xFF171C20),
      showDragHandle: true,
      builder: (sheetContext) => SafeArea(
        child: SizedBox(
          height: MediaQuery.sizeOf(sheetContext).height * 0.48,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Padding(
                padding: EdgeInsets.fromLTRB(20, 2, 20, 12),
                child: Text(
                  'SELECT MULTI-THREAD CPU SET',
                  style: TextStyle(
                    color: Color(0xFF8C989F),
                    fontSize: 12,
                    letterSpacing: 1.2,
                  ),
                ),
              ),
              const Divider(height: 1, color: Color(0xFF293139)),
              Expanded(
                child: ListView.separated(
                  itemCount: options.length,
                  separatorBuilder: (context, index) => const Divider(
                    height: 1,
                    indent: 20,
                    endIndent: 20,
                    color: Color(0xFF293139),
                  ),
                  itemBuilder: (context, index) {
                    final group = options[index];
                    final cpus = _cpusForGroup(topology, group);
                    final cpuIds = cpus.map((cpu) => cpu.logicalCpu).join(',');
                    final maxKhz = _maximumFrequencyKhz(cpus);
                    final selected = group == controller.selectedMultiGroup;
                    return ListTile(
                      contentPadding: const EdgeInsets.symmetric(
                        horizontal: 20,
                        vertical: 3,
                      ),
                      title: Text(
                        _multiChoiceLabel(group),
                        style: const TextStyle(fontWeight: FontWeight.w600),
                      ),
                      subtitle: Text(
                        'CPU $cpuIds • ${_maxFrequencyLabel(maxKhz)}'
                        ' • ${cpus.length} workers',
                        style: const TextStyle(
                          color: Color(0xFF8C989F),
                          fontSize: 12,
                        ),
                      ),
                      trailing: selected
                          ? const Icon(
                              Icons.check,
                              color: Color(0xFF49B6A7),
                            )
                          : null,
                      onTap: () => Navigator.of(sheetContext).pop(group ?? -1),
                    );
                  },
                ),
              ),
            ],
          ),
        ),
      ),
    );

    if (selection != null && !controller.isRunning) {
      controller.selectMultiGroup(selection < 0 ? null : selection);
    }
  }

  static List<String> _topologyLines(CpuTopology topology) {
    final groups = <int, List<CpuLogicalInfo>>{};
    for (final cpu in topology.allowedCpus) {
      groups.putIfAbsent(cpu.performanceGroup, () => []).add(cpu);
    }
    final keys = groups.keys.toList()..sort();
    return keys.map((group) {
      final cpus = groups[group]!
        ..sort(
          (left, right) => left.logicalCpu.compareTo(right.logicalCpu),
        );
      final ids = cpus.map((cpu) => cpu.logicalCpu).join(',');
      final maxKhz = cpus
          .map((cpu) => cpu.maxFrequencyKhz)
          .fold<int>(0, (current, value) => value > current ? value : current);
      final capacity = cpus
          .map((cpu) => cpu.capacity)
          .fold<int>(0, (current, value) => value > current ? value : current);
      final frequency = maxKhz == 0
          ? 'freq n/a'
          : '${(maxKhz / 1000).toStringAsFixed(0)} MHz';
      final capacityText =
          capacity == 0 ? 'capacity n/a' : 'capacity $capacity';
      return 'G$group  CPU $ids  •  $frequency  •  $capacityText';
    }).toList(growable: false);
  }

  static bool _topologyIsInferred(CpuTopology topology) {
    return topology.performanceGroupsAreBestEffort;
  }

  static String _stateLabel(BenchmarkState state, int errorCode) {
    return switch (state) {
      BenchmarkState.idle => 'Idle',
      BenchmarkState.preparing => 'Preparing',
      BenchmarkState.warmingUp => 'Warming up',
      BenchmarkState.measuring => 'Measuring',
      BenchmarkState.completed => 'Completed',
      BenchmarkState.cancelled => 'Cancelled',
      BenchmarkState.error => 'Error ($errorCode)',
    };
  }

  static String _nativeErrorMessage(int errorCode) {
    final description = switch (errorCode) {
      BenchmarkErrorCode.invalidRequest => 'invalid benchmark request',
      BenchmarkErrorCode.busy => 'another benchmark is already running',
      BenchmarkErrorCode.topologyUnavailable =>
        'no online and allowed CPU was available',
      BenchmarkErrorCode.threadCreationFailed =>
        'one or more benchmark workers could not be created',
      BenchmarkErrorCode.affinityUnavailable =>
        'no benchmark worker could be pinned to its selected CPU',
      _ => 'unrecognized native failure',
    };
    return 'Native error $errorCode: $description.';
  }

  static String _testLabel(BenchmarkTest? test) {
    return switch (test) {
      BenchmarkTest.cpuSingle => 'Single Thread',
      BenchmarkTest.cpuMulti => 'Multi Thread',
      BenchmarkTest.phase1 => 'Native Link',
      null => '—',
    };
  }
}

class _ScoreBlock extends StatelessWidget {
  const _ScoreBlock({
    required this.label,
    required this.score,
    required this.live,
    required this.detail,
    this.metrics = const [],
    this.accentMetric,
    this.accentMetricWarning = false,
    this.peakScore,
    this.onTap,
    this.showPickerIndicator = false,
  });

  final String label;
  final double? score;
  final double? peakScore;
  final bool live;
  final String detail;
  final List<String> metrics;
  final String? accentMetric;
  final bool accentMetricWarning;
  final VoidCallback? onTap;
  final bool showPickerIndicator;

  @override
  Widget build(BuildContext context) {
    final scoreText = score == null ? '—' : (score! / 1000).toStringAsFixed(1);
    final peakScoreText = peakScore == null || peakScore! <= 0
        ? null
        : (peakScore! / 1000).toStringAsFixed(1);
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(4),
        child: Ink(
          padding: const EdgeInsets.fromLTRB(14, 12, 14, 13),
          decoration: BoxDecoration(
            color: const Color(0xFF1A1F24),
            border: Border.all(color: const Color(0xFF293139)),
            borderRadius: BorderRadius.circular(4),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Flexible(
                    child: Text(
                      label,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontWeight: FontWeight.w600),
                    ),
                  ),
                  if (live) ...[
                    const SizedBox(width: 8),
                    const Text(
                      'LIVE',
                      style: TextStyle(
                        color: Color(0xFF49B6A7),
                        fontSize: 10,
                        letterSpacing: 1.2,
                      ),
                    ),
                  ],
                  if (showPickerIndicator) ...[
                    const SizedBox(width: 5),
                    Icon(
                      Icons.expand_more,
                      size: 17,
                      color: onTap == null
                          ? const Color(0xFF59636A)
                          : const Color(0xFF8C989F),
                    ),
                  ],
                ],
              ),
              const SizedBox(height: 4),
              Text(
                detail,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  color: Color(0xFF8C989F),
                  fontSize: 12,
                ),
              ),
              const SizedBox(height: 8),
              SizedBox(
                height: 48,
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.end,
                  children: [
                    Expanded(
                      child: FittedBox(
                        fit: BoxFit.scaleDown,
                        alignment: Alignment.centerLeft,
                        child: Text(
                          scoreText,
                          maxLines: 1,
                          style: TextStyle(
                            color:
                                score == null ? const Color(0xFF49B6A7) : null,
                            fontSize: 40,
                            height: 1,
                            fontWeight: FontWeight.w600,
                            fontFeatures: const [
                              FontFeature.tabularFigures(),
                            ],
                          ),
                        ),
                      ),
                    ),
                    if (score != null) ...[
                      const SizedBox(width: 10),
                      const Padding(
                        padding: EdgeInsets.only(bottom: 4),
                        child: Text(
                          'M units/s',
                          style: TextStyle(
                            color: Color(0xFF8C989F),
                            fontSize: 10,
                          ),
                        ),
                      ),
                    ],
                  ],
                ),
              ),
              if (peakScoreText != null ||
                  metrics.isNotEmpty ||
                  accentMetric != null) ...[
                const SizedBox(height: 10),
                Wrap(
                  spacing: 6,
                  runSpacing: 6,
                  children: [
                    if (peakScoreText != null)
                      Tooltip(
                        message: 'Highest 3.3-second scoring window',
                        child: _metricChip(
                          'Peak score $peakScoreText M/s',
                          const Color(0xFF80CBC4),
                        ),
                      ),
                    for (final metric in metrics)
                      _metricChip(metric, const Color(0xFFB7C0C5)),
                    if (accentMetric != null)
                      _metricChip(
                        accentMetric!,
                        accentMetricWarning
                            ? const Color(0xFFFF8A80)
                            : const Color(0xFF49B6A7),
                      ),
                  ],
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }

  static Widget _metricChip(String text, Color color) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 5),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.08),
        border: Border.all(color: color.withValues(alpha: 0.22)),
        borderRadius: BorderRadius.circular(3),
      ),
      child: Text(
        text,
        style: TextStyle(
          color: color,
          fontSize: 11,
          height: 1.1,
          fontWeight: FontWeight.w500,
          fontFeatures: const [FontFeature.tabularFigures()],
        ),
      ),
    );
  }
}

class _SectionLabel extends StatelessWidget {
  const _SectionLabel(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: const TextStyle(
        color: Color(0xFF8C989F),
        fontSize: 12,
        letterSpacing: 1.3,
      ),
    );
  }
}

class _DataRow extends StatelessWidget {
  const _DataRow({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        children: [
          Expanded(
            child: Text(
              label,
              style: const TextStyle(color: Color(0xFF8C989F)),
            ),
          ),
          Text(
            value,
            style: const TextStyle(
              fontFeatures: [FontFeature.tabularFigures()],
            ),
          ),
        ],
      ),
    );
  }
}

class _Notice extends StatelessWidget {
  const _Notice({required this.text, this.isWarning = false});

  final String text;
  final bool isWarning;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(top: 10),
      child: Text(
        text,
        style: TextStyle(
          color: isWarning ? const Color(0xFFFF8A80) : const Color(0xFFFFB74D),
          fontSize: 12,
          height: 1.4,
        ),
      ),
    );
  }
}

class _LoadFailure extends StatelessWidget {
  const _LoadFailure({required this.error});

  final Object? error;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text(
            'Native library could not be loaded.',
            style: TextStyle(fontSize: 18, fontWeight: FontWeight.w600),
          ),
          const SizedBox(height: 12),
          Text(
            error?.toString() ?? 'Unknown initialization error',
            style: const TextStyle(color: Color(0xFFFF8A80)),
          ),
        ],
      ),
    );
  }
}
