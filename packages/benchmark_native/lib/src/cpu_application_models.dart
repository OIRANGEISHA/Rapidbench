import 'benchmark_models.dart';

enum CpuApplicationTest {
  sort(1, 'SORT', 'Mkeys/s', '65,536 uint32 keys · reset + std::sort'),
  json(2, 'JSON RECORDS', 'MB/s', '8,192 fixed-schema ASCII records'),
  sobel(3, 'IMAGE FILTER', 'MPix/s', '1024 × 1024 grayscale · Sobel filter');

  const CpuApplicationTest(
    this.nativeId,
    this.label,
    this.unit,
    this.description,
  );
  final int nativeId;
  final String label, unit, description;
  static CpuApplicationTest fromNative(int id) =>
      values.firstWhere((test) => test.nativeId == id);
}

final class CpuApplicationSnapshot {
  const CpuApplicationSnapshot({
    this.runId = 0,
    this.state = BenchmarkState.idle,
    this.test = CpuApplicationTest.sort,
    this.threadCount = 0,
    this.flags = 0,
    this.errorCode = 0,
    this.affinityFailures = 0,
    this.inputBytes = 0,
    this.completedUnits = 0,
    this.elapsedNs = 0,
    this.unitsPerSecond = 0,
    this.progress = 0,
    this.methodVersion = 1,
    this.selectedCpu = -1,
  });
  final int runId, threadCount, flags, errorCode, affinityFailures;
  final int inputBytes, completedUnits, elapsedNs, methodVersion;
  final int selectedCpu;
  final BenchmarkState state;
  final CpuApplicationTest test;
  final double unitsPerSecond, progress;
  bool get validated => (flags & 1) != 0;
  bool get hasResult =>
      validated &&
      state.isTerminal &&
      state != BenchmarkState.error &&
      unitsPerSecond.isFinite &&
      unitsPerSecond > 0;
  double get displayValue => unitsPerSecond / 1000000;
  String get executionLabel => (flags & 2) != 0
      ? 'All cores · $threadCount independent ${threadCount == 1 ? 'worker' : 'workers'}'
      : 'CPU $selectedCpu · 1 worker';
}
