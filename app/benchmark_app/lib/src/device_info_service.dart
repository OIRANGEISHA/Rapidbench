import 'package:benchmark_native/benchmark_native.dart';
import 'package:flutter/services.dart';

final class DeviceInfoBundle {
  const DeviceInfoBundle({
    required this.platform,
    required this.topology,
    required this.cpuIsa,
    required this.vulkan,
    required this.memoryFrequency,
  });

  final Map<String, dynamic> platform;
  final CpuTopology topology;
  final Map<String, dynamic> cpuIsa;
  final Map<String, dynamic> vulkan;
  final MemoryFrequencyInfo memoryFrequency;

  Map<String, dynamic> section(String name) {
    final value = platform[name];
    return value is Map ? Map<String, dynamic>.from(value) : const {};
  }
}

abstract final class DeviceInfoService {
  static const _channel = MethodChannel(
    'dev.cpu_benchmark.benchmark_app/device_info',
  );

  static Future<DeviceInfoBundle> load() async {
    final raw = await _channel.invokeMethod<Object?>('getDeviceInfo');
    final converted = _deepConvert(raw);
    if (converted is! Map<String, dynamic>) {
      throw const FormatException('Unexpected Android device info response');
    }

    final topology = DeviceCpuInfoReader.read();
    Map<String, dynamic> cpuIsa;
    try {
      cpuIsa = DeviceCpuIsaInfoReader.read();
    } catch (error) {
      final platformSystem = converted['system'];
      cpuIsa = {
        'status': 'unavailable',
        'architecture': platformSystem is Map
            ? platformSystem['architecture']?.toString() ?? 'Unavailable'
            : 'Unavailable',
        'architectureLevel': 'Unavailable',
        'features': const <String>[],
        'reason': error.toString(),
      };
    }
    Map<String, dynamic> vulkan;
    try {
      vulkan = DeviceVulkanInfoReader.read();
    } catch (error) {
      vulkan = {
        'status': 'unavailable',
        'reason': error.toString(),
      };
    }
    MemoryFrequencyInfo memoryFrequency;
    try {
      memoryFrequency = DeviceMemoryFrequencyReader.read();
    } catch (_) {
      memoryFrequency = const MemoryFrequencyInfo(
        available: false,
        currentHz: 0,
        maximumHz: 0,
      );
    }
    return DeviceInfoBundle(
      platform: converted,
      topology: topology,
      cpuIsa: cpuIsa,
      vulkan: vulkan,
      memoryFrequency: memoryFrequency,
    );
  }

  static Object? _deepConvert(Object? value) {
    if (value is Map) {
      return <String, dynamic>{
        for (final entry in value.entries)
          entry.key.toString(): _deepConvert(entry.value),
      };
    }
    if (value is List) {
      return value.map(_deepConvert).toList(growable: false);
    }
    return value;
  }
}
