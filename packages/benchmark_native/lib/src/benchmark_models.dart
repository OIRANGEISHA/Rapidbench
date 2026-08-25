enum BenchmarkState {
  idle,
  preparing,
  warmingUp,
  measuring,
  completed,
  cancelled,
  error;

  static BenchmarkState fromNative(int value) {
    if (value < 0 || value >= values.length) {
      return BenchmarkState.error;
    }
    return values[value];
  }

  bool get isRunning =>
      this == preparing || this == warmingUp || this == measuring;

  bool get isTerminal =>
      this == completed || this == cancelled || this == error;
}

abstract final class BenchmarkErrorCode {
  static const int invalidRequest = -1;
  static const int busy = -2;
  static const int topologyUnavailable = -10;
  static const int threadCreationFailed = -11;
  static const int affinityUnavailable = -12;
}

enum BenchmarkTest {
  phase1(1),
  cpuSingle(2),
  cpuMulti(3);

  const BenchmarkTest(this.nativeId);

  final int nativeId;

  static BenchmarkTest fromNative(int value) {
    return values.firstWhere(
      (test) => test.nativeId == value,
      orElse: () => BenchmarkTest.cpuSingle,
    );
  }
}

final class BenchmarkSnapshot {
  const BenchmarkSnapshot({
    this.runId = 0,
    this.state = BenchmarkState.idle,
    this.test = BenchmarkTest.cpuSingle,
    this.threadCount = 0,
    this.qualityFlags = 0,
    this.elapsed = Duration.zero,
    this.completedWork = 0,
    this.currentValue = 0,
    this.peakScore = 0,
    this.progress = 0,
    this.scoreVariation = 0,
    this.errorCode = 0,
    this.selectedCpu = -1,
    this.affinityFailures = 0,
    this.peakFrequencyMhz = 0,
    this.peakFrequenciesMhzByGroup = const [],
    this.workerUtilization = 0,
    this.performanceHintActive = false,
    this.performanceRequestActive = false,
    this.affinityChecked = false,
  });

  final int runId;
  final BenchmarkState state;
  final BenchmarkTest test;
  final int threadCount;
  final int qualityFlags;
  final Duration elapsed;
  final int completedWork;
  final double currentValue;
  final double peakScore;
  final double progress;
  final double scoreVariation;
  final int errorCode;
  final int selectedCpu;
  final int affinityFailures;
  final int peakFrequencyMhz;
  final List<int> peakFrequenciesMhzByGroup;
  final double workerUtilization;
  final bool performanceHintActive;
  final bool performanceRequestActive;
  final bool affinityChecked;
}

final class CpuLogicalInfo {
  const CpuLogicalInfo({
    required this.logicalCpu,
    required this.online,
    required this.allowed,
    required this.clusterId,
    required this.coreId,
    required this.maxFrequencyKhz,
    required this.capacity,
    required this.performanceGroup,
  });

  final int logicalCpu;
  final bool online;
  final bool allowed;
  final int clusterId;
  final int coreId;
  final int maxFrequencyKhz;
  final int capacity;
  final int performanceGroup;
}

final class CpuTopology {
  const CpuTopology({
    required this.cpus,
    required this.onlineCount,
    required this.allowedCount,
    required this.performanceGroupCount,
    required this.preferredSingleCpu,
    required this.qualityFlags,
  });

  final List<CpuLogicalInfo> cpus;
  final int onlineCount;
  final int allowedCount;
  final int performanceGroupCount;
  final int preferredSingleCpu;
  final int qualityFlags;

  Iterable<CpuLogicalInfo> get allowedCpus => cpus.where((cpu) => cpu.allowed);

  bool get performanceGroupsAreBestEffort =>
      (qualityFlags & ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 5))) != 0;
}
