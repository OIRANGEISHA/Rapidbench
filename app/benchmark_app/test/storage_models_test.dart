import 'package:benchmark_native/benchmark_native.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('storage database test IDs preserve the published ABI', () {
    expect(StorageBenchmarkTest.sqliteInsert.nativeId, 9);
    expect(StorageBenchmarkTest.sqliteDelete.nativeId, 10);
    expect(StorageBenchmarkTest.sqliteUpdate.nativeId, 11);
    expect(StorageBenchmarkTest.all.nativeId, 12);
  });

  test('database cards use insert, update, delete display order', () {
    expect(
      StorageBenchmarkTest.databaseTests,
      const <StorageBenchmarkTest>[
        StorageBenchmarkTest.sqliteInsert,
        StorageBenchmarkTest.sqliteUpdate,
        StorageBenchmarkTest.sqliteDelete,
      ],
    );
  });

  test('native storage IDs map to update and all tests', () {
    expect(
      StorageBenchmarkTest.fromNative(11),
      StorageBenchmarkTest.sqliteUpdate,
    );
    expect(StorageBenchmarkTest.fromNative(12), StorageBenchmarkTest.all);
    expect(StorageBenchmarkTest.sqliteUpdate.metric, StorageMetric.rows);
    expect(StorageBenchmarkTest.sqliteUpdate.isRunnableCard, isTrue);
  });
}
