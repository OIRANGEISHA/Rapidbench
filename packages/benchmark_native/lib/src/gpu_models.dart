enum GpuBenchmarkTest {
  none(0, 'None'),
  fp32(1, 'FP32 Compute'),
  fp16(2, 'FP16 Compute'),
  int32(3, 'INT32 Compute'),
  mixed(4, 'Mixed Compute'),
  memoryBandwidth(5, 'GPU Memory Bandwidth'),
  all(6, 'All');

  const GpuBenchmarkTest(this.nativeId, this.label);

  final int nativeId;
  final String label;

  static GpuBenchmarkTest fromNative(int value) => values.firstWhere(
    (test) => test.nativeId == value,
    orElse: () => GpuBenchmarkTest.none,
  );
}

enum GpuBenchmarkState {
  idle(0),
  warmingUp(1),
  running(2),
  stopping(3),
  stopped(4),
  completed(5),
  error(6);

  const GpuBenchmarkState(this.nativeId);

  final int nativeId;

  bool get isRunning =>
      this == GpuBenchmarkState.warmingUp ||
      this == GpuBenchmarkState.running ||
      this == GpuBenchmarkState.stopping;

  bool get isTerminal =>
      this == GpuBenchmarkState.stopped ||
      this == GpuBenchmarkState.completed ||
      this == GpuBenchmarkState.error;

  static GpuBenchmarkState fromNative(int value) => values.firstWhere(
    (state) => state.nativeId == value,
    orElse: () => GpuBenchmarkState.error,
  );
}

enum GpuFp16Mode {
  emulated(0),
  native(1);

  const GpuFp16Mode(this.nativeId);

  final int nativeId;

  static GpuFp16Mode fromNative(int value) =>
      value == native.nativeId ? native : emulated;
}

enum GpuTimingMode {
  hostFallback(0, 'HOST FALLBACK'),
  gpuTimestamp(1, 'GPU TIMESTAMP');

  const GpuTimingMode(this.nativeId, this.label);

  final int nativeId;
  final String label;

  static GpuTimingMode fromNative(int value) =>
      value == gpuTimestamp.nativeId ? gpuTimestamp : hostFallback;
}

final class GpuCapabilities {
  const GpuCapabilities({
    required this.available,
    required this.deviceName,
    required this.apiVersion,
    required this.driverVersion,
    required this.computeQueueFamily,
    required this.timestampValidBits,
    required this.timestampPeriod,
    required this.shaderFloat16,
    required this.fp16Mode,
    required this.timingMode,
    required this.maxStorageBufferRange,
    required this.reason,
  });

  factory GpuCapabilities.fromJson(Map<String, dynamic> json) {
    final available = json['status'] == 'available';
    return GpuCapabilities(
      available: available,
      deviceName: json['deviceName']?.toString() ?? 'Unavailable',
      apiVersion: json['apiVersion']?.toString() ?? 'Unavailable',
      driverVersion: (json['driverVersion'] as num?)?.toInt() ?? 0,
      computeQueueFamily: (json['computeQueueFamily'] as num?)?.toInt() ?? -1,
      timestampValidBits: (json['timestampValidBits'] as num?)?.toInt() ?? 0,
      timestampPeriod: (json['timestampPeriod'] as num?)?.toDouble() ?? 0,
      shaderFloat16: json['shaderFloat16'] == true,
      fp16Mode: json['fp16Mode'] == 'NATIVE'
          ? GpuFp16Mode.native
          : GpuFp16Mode.emulated,
      timingMode: json['timingMode'] == 'GPU_TIMESTAMP'
          ? GpuTimingMode.gpuTimestamp
          : GpuTimingMode.hostFallback,
      maxStorageBufferRange:
          (json['maxStorageBufferRange'] as num?)?.toInt() ?? 0,
      reason: available
          ? ''
          : json['reason']?.toString() ?? 'Vulkan Compute Unavailable',
    );
  }

  final bool available;
  final String deviceName;
  final String apiVersion;
  final int driverVersion;
  final int computeQueueFamily;
  final int timestampValidBits;
  final double timestampPeriod;
  final bool shaderFloat16;
  final GpuFp16Mode fp16Mode;
  final GpuTimingMode timingMode;
  final int maxStorageBufferRange;
  final String reason;
}

final class GpuBenchmarkSnapshot {
  const GpuBenchmarkSnapshot({
    this.runId = 0,
    this.state = GpuBenchmarkState.idle,
    this.activeTest = GpuBenchmarkTest.none,
    this.fp16Mode = GpuFp16Mode.emulated,
    this.timingMode = GpuTimingMode.hostFallback,
    this.errorCode = 0,
    this.overallScoreValid = false,
    this.reducedWorkingSet = false,
    this.vulkanAvailable = false,
    this.elapsed = Duration.zero,
    this.bufferBytes = 0,
    this.fp32Gflops = 0,
    this.fp16Gflops = 0,
    this.fp16Scaling = 0,
    this.int32Gops = 0,
    this.mixedGwork = 0,
    this.memoryBandwidthGbps = 0,
    this.overallScore = 0,
    this.progress = 0,
    this.gpuBatchMs = 0,
    this.workgroupSize = 64,
    this.dispatchCount = 0,
    this.iterationCount = 64,
    this.lastError = '',
  });

  final int runId;
  final GpuBenchmarkState state;
  final GpuBenchmarkTest activeTest;
  final GpuFp16Mode fp16Mode;
  final GpuTimingMode timingMode;
  final int errorCode;
  final bool overallScoreValid;
  final bool reducedWorkingSet;
  final bool vulkanAvailable;
  final Duration elapsed;
  final int bufferBytes;
  final double fp32Gflops;
  final double fp16Gflops;
  final double fp16Scaling;
  final double int32Gops;
  final double mixedGwork;
  final double memoryBandwidthGbps;
  final double overallScore;
  final double progress;
  final double gpuBatchMs;
  final int workgroupSize;
  final int dispatchCount;
  final int iterationCount;
  final String lastError;

  double valueFor(GpuBenchmarkTest test) => switch (test) {
    GpuBenchmarkTest.fp32 => fp32Gflops,
    GpuBenchmarkTest.fp16 => fp16Gflops,
    GpuBenchmarkTest.int32 => int32Gops,
    GpuBenchmarkTest.mixed => mixedGwork,
    GpuBenchmarkTest.memoryBandwidth => memoryBandwidthGbps,
    _ => 0,
  };
}
