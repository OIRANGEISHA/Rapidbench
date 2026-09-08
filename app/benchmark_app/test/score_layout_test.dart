import 'package:benchmark_app/src/cpu_bench_page.dart';
import 'package:benchmark_app/src/storage_bench_page.dart';
import 'package:benchmark_native/benchmark_native.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

Widget host(Widget child, double scale) => MaterialApp(
      home: MediaQuery(
        data: MediaQueryData(textScaler: TextScaler.linear(scale)),
        child: Scaffold(body: SingleChildScrollView(child: child)),
      ),
    );

StorageBenchmarkResult result(StorageBenchmarkTest test) =>
    StorageBenchmarkResult(
      test: test,
      valid: true,
      stopped: false,
      ioMode: StorageIoMode.direct,
      errorCode: 0,
      threadCount: 1,
      queueDepth: 8,
      maxOutstanding: 8,
      elapsed: const Duration(seconds: 3),
      completedBytes: 999999999,
      completedIo: 999999,
      completedRows: 999999,
      mbps: 12345.67,
      iops: 1234567.8,
      rowsPerSecond: 123456.7,
    );

void main() {
  for (final scale in [1.0, 2.0]) {
    testWidgets('CPU large scores and three frequency groups at 320px x$scale',
        (tester) async {
      await tester.binding.setSurfaceSize(const Size(320, 800));
      addTearDown(() => tester.binding.setSurfaceSize(null));
      await tester.pumpWidget(host(
          const CpuScoreBlock(
            label: 'CPU Multi Thread',
            score: 123456789,
            peakScore: 145678901,
            live: true,
            detail: 'All cores • 10 workers',
            metrics: ['G0 1800 MHz', 'G1 3000 MHz', 'G2 4600 MHz'],
            accentMetric: 'Scaling 12.34×',
            showPickerIndicator: true,
          ),
          scale));
      expect(tester.takeException(), isNull);
      expect(find.text('123456.8'), findsOneWidget);
      expect(find.text('G2 4600 MHz'), findsOneWidget);
    });

    testWidgets('Storage database and random results at 320px x$scale',
        (tester) async {
      await tester.binding.setSurfaceSize(const Size(320, 800));
      addTearDown(() => tester.binding.setSurfaceSize(null));
      await tester.pumpWidget(host(
          Padding(
            padding: const EdgeInsets.all(16),
            child: Column(children: [
              Row(children: [
                for (final test in StorageBenchmarkTest.databaseTests)
                  Expanded(
                      child: Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 3),
                    child: StorageResultCard(
                        test: test,
                        snapshot: const StorageBenchmarkSnapshot(),
                        result: result(test),
                        onTap: () {}),
                  )),
              ]),
              Row(children: [
                for (final test in [
                  StorageBenchmarkTest.random4KQ1T1Read,
                  StorageBenchmarkTest.random4KQ8T1Write
                ])
                  Expanded(
                      child: StorageResultCard(
                          test: test,
                          snapshot: const StorageBenchmarkSnapshot(),
                          result: result(test),
                          onTap: () {})),
              ]),
            ]),
          ),
          scale));
      expect(tester.takeException(), isNull);
      expect(find.text('SQLITE UPDATE'), findsOneWidget);
      expect(find.text('12346 MB/s'), findsNWidgets(2));
    });
  }
}
