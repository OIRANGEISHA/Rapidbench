import 'package:benchmark_native/benchmark_native.dart';
import 'package:benchmark_app/src/gpu_bench_page.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  for (final scale in [1.0, 2.0]) {
    testWidgets('GPU unavailable/error cards fit 320px at text scale $scale',
        (tester) async {
      tester.view.physicalSize = const Size(320, 800);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final snapshot = GpuBenchmarkSnapshot(
        fp32Gflops: 123456789,
        diagnostics: {
          2: GpuItemDiagnostics.fromJson({
            'state': 1,
            'reason':
                'Native FP16 pipeline unavailable; FP32 emulation also unavailable. '
                    'Driver returned an unsupported pipeline configuration.'
          }),
          3: GpuItemDiagnostics.fromJson({
            'state': 5,
            'reason':
                'Numerical validation failed; retry this item independently.'
          }),
        },
      );
      await tester.pumpWidget(MaterialApp(
          home: MediaQuery(
        data: MediaQueryData(textScaler: TextScaler.linear(scale)),
        child: Scaffold(
            body: SingleChildScrollView(
                child: Column(children: [
          for (final test in [
            GpuBenchmarkTest.fp32,
            GpuBenchmarkTest.fp16,
            GpuBenchmarkTest.int32
          ])
            GpuResultCard(
                test: test,
                snapshot: snapshot,
                enabled: false,
                live: false,
                onTap: () {}),
        ]))),
      )));
      expect(find.textContaining('Unavailable ·'), findsOneWidget);
      expect(find.textContaining('Failed ·'), findsOneWidget);
      expect(tester.takeException(), isNull);
    });
  }
  test('GPU capabilities isolate unavailable tests and expose FP16 fallback',
      () {
    final capabilities = GpuCapabilities.fromJson({
      'status': 'available',
      'availableTestMask': 0x3a,
      'shaderFloat16': true,
      'fp16Mode': 'EMULATED',
      'testReasons': {'2': 'Native and emulated pipeline rejected'},
    });
    expect(capabilities.supports(GpuBenchmarkTest.fp16), isFalse);
    expect(capabilities.supports(GpuBenchmarkTest.fp32), isTrue);
    expect(capabilities.supports(GpuBenchmarkTest.int32), isTrue);
    expect(capabilities.supports(GpuBenchmarkTest.all), isTrue);
    expect(capabilities.fp16Mode, GpuFp16Mode.emulated);
    expect(capabilities.testReasons[2], contains('rejected'));
    expect(
        GpuCapabilities.fromJson({'status': 'unavailable'})
            .supports(GpuBenchmarkTest.all),
        isFalse);
  });
  test('GPU aggregate timing never hides an earlier host fallback', () {
    final snapshot = GpuBenchmarkSnapshot(runId: 7, diagnostics: {
      1: GpuItemDiagnostics.fromJson(
          {'runId': 7, 'hostBatches': 2, 'state': 3}),
      2: GpuItemDiagnostics.fromJson(
          {'runId': 7, 'timestampBatches': 20, 'state': 3}),
      3: GpuItemDiagnostics.fromJson(
          {'runId': 6, 'hostBatches': 99, 'state': 5}),
    });
    expect(snapshot.timingLabel, 'GPU + HOST FALLBACK');
    expect(snapshot.diagnostics[3]!.state, GpuItemState.failed);
    expect(
        GpuBenchmarkSnapshot(runId: 8, diagnostics: snapshot.diagnostics)
            .timingLabel,
        'Awaiting samples');
    expect(const GpuBenchmarkSnapshot().timingLabel, 'Not measured');
  });
  test('memory distinguishes restricted and unstable CPU availability', () {
    expect(
        const MemoryBenchmarkSnapshot(
                threadCount: 8, presentCpus: 10, onlineCpus: 10, allowedCpus: 8)
            .topologyWarning,
        contains('8 of 10'));
    expect(
        const MemoryBenchmarkSnapshot(threadCount: 8, presentCpus: 8)
            .topologyWarning,
        isNull);
    expect(
        const MemoryBenchmarkSnapshot(threadCount: 8, topologyUnstable: true)
            .topologyWarning,
        contains('fixed set'));
  });
  test('automatic CPU selection distinguishes inferred and unknown targets',
      () {
    for (final flags in [0, 1 << 6, 1 << 7]) {
      final topology = CpuTopology(
        cpus: const [],
        onlineCount: 0,
        allowedCount: 0,
        performanceGroupCount: 0,
        preferredSingleCpu: -1,
        qualityFlags: flags,
      );
      expect(topology.selectionWarning == null, flags == 0);
      if (flags == 1 << 7) {
        expect(topology.selectionWarning, contains('unknown'));
      }
    }
    expect(const CpuApplicationSnapshot(flags: 8).selectionWarning,
        contains('estimated'));
    expect(const CpuApplicationSnapshot(flags: 16).selectionWarning,
        contains('unknown'));
    expect(const CpuApplicationSnapshot().selectionWarning, isNull);
  });
}
