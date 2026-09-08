import 'package:benchmark_native/benchmark_native.dart';
import 'package:flutter_test/flutter_test.dart';

class TestController extends ExclusiveBenchmarkController {
  TestController(BenchmarkRunCoordinator coordinator, this.module)
      : super(coordinator: coordinator);
  final BenchmarkModule module;
  int stopRequests = 0;
  bool start() => beginBenchmark(module, () => stopRequests++);
  void complete() => finishBenchmark();
  void fail(void Function() destroy) => abortBenchmark(destroy);
}

void main() {
  late BenchmarkRunCoordinator coordinator;
  setUp(() => coordinator = BenchmarkRunCoordinator());
  tearDown(() => coordinator.dispose());

  test('every module excludes all others until its whole sequence completes',
      () {
    for (final module in BenchmarkModule.values) {
      final active = TestController(coordinator, module);
      expect(active.start(), isTrue);
      for (final other in BenchmarkModule.values) {
        final contender = TestController(coordinator, other);
        expect(contender.canStart, isFalse);
        expect(contender.start(), isFalse);
        contender.dispose();
        expect(coordinator.activeModule, module);
      }
      active.complete();
      expect(coordinator.canStart, isTrue);
      active.dispose();
    }
  });

  test('background stops owner but keeps exclusion until native completion',
      () {
    final cpu = TestController(coordinator, BenchmarkModule.cpu);
    final memory = TestController(coordinator, BenchmarkModule.memory);
    cpu.start();
    coordinator.setForeground(false);
    expect(cpu.stopRequests, 1);
    expect(memory.start(), isFalse);
    coordinator.setForeground(true);
    expect(memory.start(), isFalse);
    cpu.complete();
    expect(memory.start(), isTrue);
    cpu.dispose();
    expect(coordinator.activeModule, BenchmarkModule.memory);
    memory.dispose();
  });

  test('completed background run cannot start again before resume', () {
    final cpu = TestController(coordinator, BenchmarkModule.cpu);
    cpu.start();
    coordinator.setForeground(false);
    cpu.complete();
    expect(cpu.start(), isFalse);
    coordinator.setForeground(true);
    expect(cpu.start(), isTrue);
    cpu.dispose();
  });

  test('stale completion cannot release a newer run', () {
    final old = coordinator.acquire(BenchmarkModule.cpu, () {})!;
    coordinator.release(old);
    final current = coordinator.acquire(BenchmarkModule.storage, () {})!;
    coordinator.release(old);
    expect(coordinator.activeModule, BenchmarkModule.storage);
    coordinator.release(current);
    expect(coordinator.canStart, isTrue);
  });

  test('bridge failure joins engine before allowing a different module', () {
    final cpu = TestController(coordinator, BenchmarkModule.cpu);
    final gpu = TestController(coordinator, BenchmarkModule.gpu);
    cpu.start();
    cpu.fail(() {
      expect(gpu.start(), isFalse);
    });
    expect(cpu.engineUnavailable, isTrue);
    expect(cpu.start(), isFalse);
    expect(gpu.start(), isTrue);
    cpu.dispose();
    gpu.dispose();
  });

  test('other controllers receive availability changes', () {
    final cpu = TestController(coordinator, BenchmarkModule.cpu);
    final memory = TestController(coordinator, BenchmarkModule.memory);
    final states = <bool>[];
    memory.addListener(() => states.add(memory.canStart));
    cpu.start();
    cpu.complete();
    expect(states, [false, true]);
    memory.dispose();
    cpu.dispose();
  });
}
