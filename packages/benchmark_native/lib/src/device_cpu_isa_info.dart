import 'dart:convert';
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

typedef _CpuIsaJsonNative = Int32 Function(
  Pointer<Char>,
  Uint32,
  Pointer<Uint32>,
);
typedef _CpuIsaJsonDart = int Function(Pointer<Char>, int, Pointer<Uint32>);

abstract final class DeviceCpuIsaInfoReader {
  static Map<String, dynamic> read() {
    final library = _openLibrary();
    final query = library.lookupFunction<_CpuIsaJsonNative, _CpuIsaJsonDart>(
      'bm_get_cpu_isa_info_json',
    );
    final required = calloc<Uint32>();
    try {
      _check(query(nullptr, 0, required));
      final buffer = calloc<Uint8>(required.value);
      try {
        _check(query(buffer.cast<Char>(), required.value, required));
        final bytes = buffer.asTypedList(required.value - 1);
        final decoded = jsonDecode(utf8.decode(bytes));
        if (decoded is! Map) {
          throw const FormatException('Unexpected CPU ISA query response');
        }
        return Map<String, dynamic>.from(decoded);
      } finally {
        calloc.free(buffer);
      }
    } finally {
      calloc.free(required);
    }
  }

  static DynamicLibrary _openLibrary() {
    if (Platform.isAndroid || Platform.isLinux) {
      return DynamicLibrary.open('libbenchmark_ffi.so');
    }
    if (Platform.isWindows) {
      return DynamicLibrary.open('benchmark_ffi.dll');
    }
    if (Platform.isMacOS) {
      return DynamicLibrary.open('libbenchmark_ffi.dylib');
    }
    throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
  }

  static void _check(int status) {
    if (status != 0) {
      throw StateError('CPU ISA query failed with native status $status');
    }
  }
}
