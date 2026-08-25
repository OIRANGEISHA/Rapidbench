package dev.cpu_benchmark.benchmark_native

import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.io.File

class BenchmarkNativePlugin : FlutterPlugin, MethodChannel.MethodCallHandler {
    private var channel: MethodChannel? = null
    private var benchmarkDirectory: File? = null

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        benchmarkDirectory = File(
            binding.applicationContext.noBackupFilesDir,
            "storage_bench",
        )
        channel = MethodChannel(
            binding.binaryMessenger,
            "dev.cpu_benchmark.benchmark_app/storage_benchmark",
        ).also { it.setMethodCallHandler(this) }
    }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        if (call.method != "getBenchmarkDirectory") {
            result.notImplemented()
            return
        }
        val directory = benchmarkDirectory
        if (directory == null) {
            result.error("STORAGE_DIRECTORY_UNAVAILABLE", "Plugin is detached", null)
            return
        }
        try {
            if (!directory.exists() && !directory.mkdirs()) {
                result.error(
                    "STORAGE_DIRECTORY_CREATE_FAILED",
                    "Unable to create the private benchmark directory",
                    null,
                )
                return
            }
            result.success(directory.canonicalPath)
        } catch (error: Throwable) {
            result.error(
                "STORAGE_DIRECTORY_FAILED",
                error.message ?: error.javaClass.simpleName,
                null,
            )
        }
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        channel?.setMethodCallHandler(null)
        channel = null
        benchmarkDirectory = null
    }
}
