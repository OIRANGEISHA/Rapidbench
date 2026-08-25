import 'benchmark_models.dart';

enum MemoryBenchmarkTest {
  read(1, 'Read'),
  write(2, 'Write'),
  copy(3, 'Copy');

  const MemoryBenchmarkTest(this.nativeId, this.label);

  final int nativeId;
  final String label;

  static MemoryBenchmarkTest fromNative(int value) {
    return values.firstWhere(
      (test) => test.nativeId == value,
      orElse: () => MemoryBenchmarkTest.read,
    );
  }
}

final class MemoryBenchmarkSnapshot {
  const MemoryBenchmarkSnapshot({
    this.runId = 0,
    this.state = BenchmarkState.idle,
    this.test = MemoryBenchmarkTest.read,
    this.threadCount = 0,
    this.errorCode = 0,
    this.affinityFailures = 0,
    this.performanceRequestFailures = 0,
    this.bufferBytes = 0,
    this.elapsed = Duration.zero,
    this.processedBytes = 0,
    this.bandwidthGbps = 0,
    this.progress = 0,
  });

  final int runId;
  final BenchmarkState state;
  final MemoryBenchmarkTest test;
  final int threadCount;
  final int errorCode;
  final int affinityFailures;
  final int performanceRequestFailures;
  final int bufferBytes;
  final Duration elapsed;
  final int processedBytes;
  final double bandwidthGbps;
  final double progress;
}

final class MemoryFrequencyInfo {
  const MemoryFrequencyInfo({
    required this.available,
    required this.currentHz,
    required this.maximumHz,
  });

  final bool available;
  final int currentHz;
  final int maximumHz;

  bool get hasCurrent => currentHz > 0;
  bool get hasMaximum => maximumHz > 0;
}
