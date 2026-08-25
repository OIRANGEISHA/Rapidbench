import 'memory_bindings.dart';
import 'memory_models.dart';

abstract final class DeviceMemoryFrequencyReader {
  static MemoryFrequencyInfo read() {
    final engine = NativeMemoryEngine();
    try {
      return engine.readFrequency();
    } finally {
      engine.dispose();
    }
  }
}
