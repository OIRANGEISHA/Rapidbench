import 'package:benchmark_app/src/device_info_service.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  const channel = MethodChannel(
    'dev.cpu_benchmark.benchmark_app/device_info',
  );

  tearDown(() async {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });

  test('openProjectPage delegates to the Android device-info channel',
      () async {
    MethodCall? receivedCall;
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
      receivedCall = call;
      return true;
    });

    final opened = await DeviceInfoService.openProjectPage();

    expect(opened, isTrue);
    expect(receivedCall?.method, 'openProjectPage');
    expect(receivedCall?.arguments, isNull);
  });

  test('openProjectPage treats a missing Android result as not opened',
      () async {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (_) async => null);

    expect(await DeviceInfoService.openProjectPage(), isFalse);
  });
}
