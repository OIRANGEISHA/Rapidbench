enum StorageBenchmarkTest {
  sequentialRead(1, 'SEQ READ', StorageMetric.throughput),
  sequentialWrite(2, 'SEQ WRITE', StorageMetric.throughput),
  random4KQ1T1Read(3, '4K Q1T1 READ', StorageMetric.random),
  random4KQ1T1Write(4, '4K Q1T1 WRITE', StorageMetric.random),
  random4KQ8T1Read(5, '4K Q8T1 READ', StorageMetric.random),
  random4KQ8T1Write(6, '4K Q8T1 WRITE', StorageMetric.random),
  random4KQ1T4Read(7, '4K Q1T4 READ', StorageMetric.random),
  random4KQ1T4Write(8, '4K Q1T4 WRITE', StorageMetric.random),
  sqliteInsert(9, 'SQLITE INSERT', StorageMetric.rows),
  sqliteDelete(10, 'SQLITE DELETE', StorageMetric.rows),
  sqliteUpdate(11, 'SQLITE UPDATE', StorageMetric.rows),
  all(12, 'BENCH ALL', StorageMetric.throughput);

  const StorageBenchmarkTest(this.nativeId, this.label, this.metric);

  final int nativeId;
  final String label;
  final StorageMetric metric;

  static const databaseTests = <StorageBenchmarkTest>[
    StorageBenchmarkTest.sqliteInsert,
    StorageBenchmarkTest.sqliteUpdate,
    StorageBenchmarkTest.sqliteDelete,
  ];

  bool get isRunnableCard => this != StorageBenchmarkTest.all;

  static StorageBenchmarkTest fromNative(int value) {
    return values.firstWhere(
      (test) => test.nativeId == value,
      orElse: () => StorageBenchmarkTest.all,
    );
  }
}

enum StorageMetric { throughput, random, rows }

enum StorageBenchmarkState {
  idle(0),
  preparing(1),
  warmingUp(2),
  measuring(3),
  flushing(4),
  stopping(5),
  completed(6),
  stopped(7),
  error(8);

  const StorageBenchmarkState(this.nativeId);

  final int nativeId;

  bool get isRunning => switch (this) {
    idle || completed || stopped || error => false,
    _ => true,
  };

  bool get isTerminal => !isRunning;

  static StorageBenchmarkState fromNative(int value) {
    return values.firstWhere(
      (state) => state.nativeId == value,
      orElse: () => StorageBenchmarkState.error,
    );
  }
}

enum StorageBenchmarkPhase {
  none(0),
  prepare(1),
  warmUp(2),
  measure(3),
  flush(4);

  const StorageBenchmarkPhase(this.nativeId);

  final int nativeId;

  static StorageBenchmarkPhase fromNative(int value) {
    return values.firstWhere(
      (phase) => phase.nativeId == value,
      orElse: () => StorageBenchmarkPhase.none,
    );
  }
}

enum StorageIoMode {
  unavailable(0),
  direct(1),
  bufferedCompatibility(2),
  sqlite(3);

  const StorageIoMode(this.nativeId);

  final int nativeId;

  static StorageIoMode fromNative(int value) {
    return values.firstWhere(
      (mode) => mode.nativeId == value,
      orElse: () => StorageIoMode.unavailable,
    );
  }
}

final class StorageBenchmarkResult {
  const StorageBenchmarkResult({
    required this.test,
    required this.valid,
    required this.stopped,
    required this.ioMode,
    required this.errorCode,
    required this.threadCount,
    required this.queueDepth,
    required this.maxOutstanding,
    required this.elapsed,
    required this.completedBytes,
    required this.completedIo,
    required this.completedRows,
    required this.mbps,
    required this.iops,
    required this.rowsPerSecond,
  });

  final StorageBenchmarkTest test;
  final bool valid;
  final bool stopped;
  final StorageIoMode ioMode;
  final int errorCode;
  final int threadCount;
  final int queueDepth;
  final int maxOutstanding;
  final Duration elapsed;
  final int completedBytes;
  final int completedIo;
  final int completedRows;
  final double mbps;
  final double iops;
  final double rowsPerSecond;
}

final class StorageBenchmarkSnapshot {
  const StorageBenchmarkSnapshot({
    this.runId = 0,
    this.state = StorageBenchmarkState.idle,
    this.phase = StorageBenchmarkPhase.none,
    this.activeTest = StorageBenchmarkTest.all,
    this.ioMode = StorageIoMode.unavailable,
    this.errorCode = 0,
    this.alignment = 0,
    this.testFileMiB = 0,
    this.blockSize = 0,
    this.queueDepth = 0,
    this.threadCount = 0,
    this.currentOutstanding = 0,
    this.maxOutstanding = 0,
    this.elapsed = Duration.zero,
    this.completedBytes = 0,
    this.completedIo = 0,
    this.completedRows = 0,
    this.currentMbps = 0,
    this.currentIops = 0,
    this.currentRowsPerSecond = 0,
    this.progress = 0,
    this.results = const <StorageBenchmarkTest, StorageBenchmarkResult>{},
  });

  final int runId;
  final StorageBenchmarkState state;
  final StorageBenchmarkPhase phase;
  final StorageBenchmarkTest activeTest;
  final StorageIoMode ioMode;
  final int errorCode;
  final int alignment;
  final int testFileMiB;
  final int blockSize;
  final int queueDepth;
  final int threadCount;
  final int currentOutstanding;
  final int maxOutstanding;
  final Duration elapsed;
  final int completedBytes;
  final int completedIo;
  final int completedRows;
  final double currentMbps;
  final double currentIops;
  final double currentRowsPerSecond;
  final double progress;
  final Map<StorageBenchmarkTest, StorageBenchmarkResult> results;
}
