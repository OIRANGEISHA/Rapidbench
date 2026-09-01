package dev.cpu_benchmark.benchmark_app

import android.app.ActivityManager
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.opengl.EGL14
import android.opengl.GLES20
import android.os.Build
import android.os.StatFs
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import java.io.File
import java.util.concurrent.Executors

class MainActivity : FlutterActivity() {
    private companion object {
        const val PROJECT_URL = "https://github.com/OIRANGEISHA/Rapidbench"
    }

    private val deviceExecutor = Executors.newSingleThreadExecutor()

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            "dev.cpu_benchmark.benchmark_app/device_info",
        ).setMethodCallHandler { call, result ->
            when (call.method) {
                "getDeviceInfo" -> {
                    deviceExecutor.execute {
                        try {
                            val info = collectDeviceInfo()
                            runOnUiThread { result.success(info) }
                        } catch (error: Throwable) {
                            runOnUiThread {
                                result.error(
                                    "DEVICE_INFO_FAILED",
                                    error.message ?: error.javaClass.simpleName,
                                    null,
                                )
                            }
                        }
                    }
                }
                "openProjectPage" -> openProjectPage(result)
                else -> result.notImplemented()
            }
        }
    }

    override fun onDestroy() {
        deviceExecutor.shutdownNow()
        super.onDestroy()
    }

    private fun collectDeviceInfo(): Map<String, Any> {
        val activityManager = getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val memoryInfo = ActivityManager.MemoryInfo()
        activityManager.getMemoryInfo(memoryInfo)
        val dataStats = StatFs(dataDir.absolutePath)
        val socModel = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            Build.SOC_MODEL.takeUnless { it.isBlank() } ?: Build.HARDWARE
        } else {
            Build.HARDWARE
        }
        val socManufacturer = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            Build.SOC_MANUFACTURER.takeUnless { it.isBlank() } ?: "Unavailable"
        } else {
            "Unavailable"
        }

        return mapOf(
            "app" to collectAppInfo(),
            "system" to mapOf(
                "manufacturer" to Build.MANUFACTURER,
                "brand" to Build.BRAND,
                "model" to Build.MODEL,
                "device" to Build.DEVICE,
                "androidVersion" to Build.VERSION.RELEASE,
                "sdkLevel" to Build.VERSION.SDK_INT,
                "securityPatch" to Build.VERSION.SECURITY_PATCH,
                "buildId" to Build.ID,
                "hardware" to Build.HARDWARE,
                "supportedAbis" to Build.SUPPORTED_ABIS.toList(),
                "architecture" to Build.SUPPORTED_ABIS.firstOrNull().orEmpty(),
            ),
            "soc" to mapOf(
                "model" to socModel,
                "manufacturer" to socManufacturer,
                "modelSource" to if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    "Android Build.SOC_MODEL"
                } else {
                    "Android Build.HARDWARE"
                },
            ),
            "memory" to mapOf(
                "totalBytes" to memoryInfo.totalMem,
                "availableBytes" to memoryInfo.availMem,
                "lowMemory" to memoryInfo.lowMemory,
                "type" to detectMemoryType(),
            ),
            "storage" to mapOf(
                "totalBytes" to dataStats.totalBytes,
                "availableBytes" to dataStats.availableBytes,
                "usedBytes" to dataStats.totalBytes - dataStats.availableBytes,
                "type" to detectStorageType(),
                "scope" to "Internal app data volume",
            ),
            "gpu" to collectOpenGlInfo(),
        )
    }

    @Suppress("DEPRECATION")
    private fun collectAppInfo(): Map<String, Any> {
        val packageInfo = packageManager.getPackageInfo(packageName, 0)
        val versionCode = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            packageInfo.longVersionCode
        } else {
            packageInfo.versionCode.toLong()
        }
        return mapOf(
            "name" to applicationInfo.loadLabel(packageManager).toString(),
            "versionName" to (packageInfo.versionName ?: "Unavailable"),
            "versionCode" to versionCode,
            "repository" to PROJECT_URL,
        )
    }

    private fun openProjectPage(result: MethodChannel.Result) {
        try {
            val intent = Intent(Intent.ACTION_VIEW, Uri.parse(PROJECT_URL)).apply {
                addCategory(Intent.CATEGORY_BROWSABLE)
            }
            startActivity(intent)
            result.success(true)
        } catch (error: ActivityNotFoundException) {
            result.error(
                "PROJECT_LINK_UNAVAILABLE",
                "No browser is available to open the RapidBench repository.",
                null,
            )
        } catch (error: Throwable) {
            result.error(
                "PROJECT_LINK_FAILED",
                error.message ?: error.javaClass.simpleName,
                null,
            )
        }
    }

    private fun detectMemoryType(): String {
        val paths = listOf(
            "/sys/class/misc/memory_type/name",
            "/sys/devices/system/soc/soc0/memory_type",
            "/proc/device-tree/memory-type",
        )
        for (path in paths) {
            readSystemText(path)?.let { value ->
                if (value.isNotBlank()) return value
            }
        }
        return "Unavailable"
    }

    private fun detectStorageType(): String {
        val ufsPaths = listOf(
            "/sys/class/ufs",
            "/sys/bus/platform/drivers/ufshcd",
            "/sys/bus/platform/drivers/ufshc",
        )
        if (ufsPaths.any { File(it).exists() }) {
            return "UFS (interface detected)"
        }
        val mmcType = readSystemText("/sys/class/block/mmcblk0/device/type")
        if (mmcType != null && mmcType.contains("MMC", ignoreCase = true)) {
            return "eMMC (interface detected)"
        }
        return "Unavailable"
    }

    private fun readSystemText(path: String): String? {
        return try {
            File(path).readText()
                .replace("\u0000", "")
                .trim()
                .takeIf { it.isNotBlank() }
        } catch (_: Throwable) {
            null
        }
    }

    private fun collectOpenGlInfo(): Map<String, Any> {
        val display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        if (display == EGL14.EGL_NO_DISPLAY) {
            return mapOf("status" to "Unavailable", "reason" to "No EGL display")
        }
        val versions = IntArray(2)
        if (!EGL14.eglInitialize(display, versions, 0, versions, 1)) {
            return mapOf("status" to "Unavailable", "reason" to "EGL initialize failed")
        }
        val attributes = intArrayOf(
            EGL14.EGL_RENDERABLE_TYPE,
            EGL14.EGL_OPENGL_ES2_BIT,
            EGL14.EGL_SURFACE_TYPE,
            EGL14.EGL_PBUFFER_BIT,
            EGL14.EGL_RED_SIZE,
            8,
            EGL14.EGL_GREEN_SIZE,
            8,
            EGL14.EGL_BLUE_SIZE,
            8,
            EGL14.EGL_NONE,
        )
        val configs = arrayOfNulls<android.opengl.EGLConfig>(1)
        val configCount = IntArray(1)
        if (!EGL14.eglChooseConfig(
                display,
                attributes,
                0,
                configs,
                0,
                configs.size,
                configCount,
                0,
            ) || configCount[0] == 0
        ) {
            EGL14.eglTerminate(display)
            return mapOf("status" to "Unavailable", "reason" to "No EGL config")
        }
        val contextAttributes = intArrayOf(
            EGL14.EGL_CONTEXT_CLIENT_VERSION,
            2,
            EGL14.EGL_NONE,
        )
        val eglContext = EGL14.eglCreateContext(
            display,
            configs[0],
            EGL14.EGL_NO_CONTEXT,
            contextAttributes,
            0,
        )
        val surfaceAttributes = intArrayOf(
            EGL14.EGL_WIDTH,
            1,
            EGL14.EGL_HEIGHT,
            1,
            EGL14.EGL_NONE,
        )
        val surface = EGL14.eglCreatePbufferSurface(display, configs[0], surfaceAttributes, 0)
        if (eglContext == EGL14.EGL_NO_CONTEXT || surface == EGL14.EGL_NO_SURFACE ||
            !EGL14.eglMakeCurrent(display, surface, surface, eglContext)
        ) {
            EGL14.eglTerminate(display)
            return mapOf("status" to "Unavailable", "reason" to "EGL context failed")
        }

        val result = mapOf(
            "status" to "Available",
            "vendor" to (GLES20.glGetString(GLES20.GL_VENDOR) ?: "Unavailable"),
            "renderer" to (GLES20.glGetString(GLES20.GL_RENDERER) ?: "Unavailable"),
            "openGlEsVersion" to (GLES20.glGetString(GLES20.GL_VERSION) ?: "Unavailable"),
            "glslVersion" to (
                GLES20.glGetString(GLES20.GL_SHADING_LANGUAGE_VERSION) ?: "Unavailable"
            ),
        )
        EGL14.eglMakeCurrent(
            display,
            EGL14.EGL_NO_SURFACE,
            EGL14.EGL_NO_SURFACE,
            EGL14.EGL_NO_CONTEXT,
        )
        EGL14.eglDestroySurface(display, surface)
        EGL14.eglDestroyContext(display, eglContext)
        EGL14.eglTerminate(display)
        return result
    }
}
