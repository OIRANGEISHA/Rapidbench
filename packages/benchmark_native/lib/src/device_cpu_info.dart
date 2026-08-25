import 'benchmark_bindings.dart';
import 'benchmark_models.dart';

abstract final class DeviceCpuInfoReader {
  static CpuTopology read() => BenchmarkBindings.open().readTopology();
}
