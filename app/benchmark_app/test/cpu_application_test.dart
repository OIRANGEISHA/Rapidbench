import 'dart:ffi' show sizeOf;
import 'package:benchmark_app/src/cpu_application_section.dart';
import 'package:benchmark_native/benchmark_native.dart';
import 'package:benchmark_native/src/cpu_application_bindings.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

class FakeApplicationEngine implements CpuApplicationEngine {
  CpuApplicationSnapshot snapshot = const CpuApplicationSnapshot();
  bool failRead = false, disposed = false, multi = false;
  int stopCalls = 0, id = 0;
  VoidCallback? onDispose;
  @override
  int start(CpuApplicationTest test, {required bool multi}) {
    this.multi = multi;
    snapshot = CpuApplicationSnapshot(
        runId: ++id, test: test, state: BenchmarkState.preparing);
    return id;
  }

  @override
  CpuApplicationSnapshot readSnapshot() {
    if (failRead) throw StateError('simulated read failure');
    return snapshot;
  }

  @override
  void requestStop(int id) {
    stopCalls++;
  }

  @override
  void dispose() {
    if (!disposed) onDispose?.call();
    disposed = true;
  }
}

CpuApplicationSnapshot result(
        {BenchmarkState state = BenchmarkState.completed,
        CpuApplicationTest test = CpuApplicationTest.sort}) =>
    CpuApplicationSnapshot(
      runId: 1,
      test: test,
      state: state,
      flags: 3,
      threadCount: 10,
      inputBytes: 1048576,
      completedUnits: 100000000,
      elapsedNs: 3000000000,
      unitsPerSecond: 1234567890123456,
      affinityFailures: 2,
    );

void main() {
  test('additive FFI layouts and result units', () {
    expect(
        const CpuApplicationSnapshot(flags: 2, threadCount: 1).executionLabel,
        'All cores · 1 independent worker');
    expect(
        const CpuApplicationSnapshot(selectedCpu: 9, threadCount: 1)
            .executionLabel,
        'CPU 9 · 1 worker');
    expect(sizeOf<BmCpuApplicationRequest>(), 24);
    expect(sizeOf<BmCpuApplicationSnapshot>(), 104);
    expect(result().displayValue, closeTo(1234567890.123456, 0.00001));
    expect(result(state: BenchmarkState.error).hasResult, isFalse);
    expect(
        const CpuApplicationSnapshot(
                state: BenchmarkState.completed,
                flags: 1,
                unitsPerSecond: double.nan)
            .hasResult,
        isFalse);
  });
  testWidgets('ownership, background stop, partial result and restart',
      (tester) async {
    final coordinator = BenchmarkRunCoordinator();
    final engine = FakeApplicationEngine();
    final controller =
        CpuApplicationController(engine: engine, coordinator: coordinator);
    controller.start(CpuApplicationTest.sort);
    expect(coordinator.acquire(BenchmarkModule.memory, () {}), isNull);
    coordinator.setForeground(false);
    expect(engine.stopCalls, 1);
    expect(coordinator.canStart, isFalse);
    engine.snapshot = result(state: BenchmarkState.cancelled);
    await tester.pump(const Duration(milliseconds: 130));
    expect(controller.resultFor(CpuApplicationTest.sort)?.hasResult, isTrue);
    expect(coordinator.activeModule, isNull);
    expect(controller.canStart, isFalse);
    coordinator.setForeground(true);
    controller.start(CpuApplicationTest.sort);
    expect(controller.resultFor(CpuApplicationTest.sort), isNull);
    expect(controller.snapshot.unitsPerSecond, 0);
    expect(engine.multi, isFalse);
    controller.dispose();
    expect(engine.disposed, isTrue);
    expect(coordinator.canStart, isTrue);
    coordinator.dispose();
  });
  testWidgets('automatic mode selection, error cleanup before unlocking',
      (tester) async {
    final coordinator = BenchmarkRunCoordinator();
    final engine = FakeApplicationEngine();
    final controller =
        CpuApplicationController(engine: engine, coordinator: coordinator);
    controller.selectMulti(true);
    controller.start(CpuApplicationTest.json);
    expect(engine.multi, isTrue);
    controller.selectMulti(false);
    expect(controller.multi, isTrue);
    bool? availabilityDuringDispose;
    engine.onDispose = () => availabilityDuringDispose = coordinator.canStart;
    engine.failRead = true;
    await tester.pump(const Duration(milliseconds: 130));
    expect(controller.engineUnavailable, isTrue);
    expect(availabilityDuringDispose, isFalse);
    expect(controller.canStart, isFalse);
    expect(coordinator.canStart, isTrue);
    expect(controller.resultFor(CpuApplicationTest.json), isNull);
    controller.dispose();
    coordinator.dispose();
  });
  for (final scale in [1.0, 2.0, 3.0]) {
    testWidgets('application cards large numbers at 320px x$scale',
        (tester) async {
      await tester.binding.setSurfaceSize(const Size(320, 800));
      addTearDown(() => tester.binding.setSurfaceSize(null));
      await tester.pumpWidget(MaterialApp(
          home: MediaQuery(
        data: MediaQueryData(textScaler: TextScaler.linear(scale)),
        child: Scaffold(
            body: SingleChildScrollView(
                child: Padding(
                    padding: const EdgeInsets.all(20),
                    child: Column(children: [
                      for (final test in CpuApplicationTest.values)
                        CpuApplicationResultCard(
                            test: test,
                            result: result(test: test),
                            onTap: () {}),
                    ])))),
      )));
      expect(tester.takeException(), isNull);
      expect(find.text('1234567890.12'), findsNWidgets(3));
      expect(find.text('Mkeys/s'), findsOneWidget);
      expect(find.text('MB/s'), findsOneWidget);
      expect(find.text('MPix/s'), findsOneWidget);
    });
  }
  testWidgets('expand and tap a single item, automatic core policy only',
      (tester) async {
    final coordinator = BenchmarkRunCoordinator();
    final engine = FakeApplicationEngine();
    final controller =
        CpuApplicationController(engine: engine, coordinator: coordinator);
    await tester.pumpWidget(MaterialApp(
        home: Scaffold(
            body: SingleChildScrollView(
                child: CpuApplicationSection(controller: controller)))));
    await tester.tap(find.text('APPLICATION WORKLOADS'));
    await tester.pumpAndSettle();
    expect(find.text('Auto · highest-performance core'), findsOneWidget);
    expect(find.textContaining('CPU / cluster selection'), findsNothing);
    await tester.tap(find.text('SORT'));
    expect(engine.multi, isFalse);
    expect(controller.isRunning, isTrue);
    engine.snapshot = result();
    await tester.pump(const Duration(milliseconds: 150));
    expect(controller.resultFor(CpuApplicationTest.sort)?.hasResult, isTrue);
    await tester.tap(find.text('Multi'));
    await tester.pumpAndSettle();
    expect(find.text('All cores · one worker per core'), findsOneWidget);
    expect(controller.resultFor(CpuApplicationTest.sort), isNull);
    await tester.tap(find.text('SORT'));
    expect(engine.multi, isTrue);
    await tester.pumpWidget(const SizedBox());
    controller.dispose();
    coordinator.dispose();
  });
}
