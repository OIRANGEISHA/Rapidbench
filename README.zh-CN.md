# RapidBench

[English](README.md)

当前开发通道：**1.0.2 Beta 3 / Preview 预览版**。

RapidBench 是一款原生 Android 性能测试工具，用于快速评估设备性能，并集中查看 CPU、GPU 的特性支持情况。它提供时间较短且可重复的 CPU、内存、存储和 Vulkan Compute 测试，同时展示 CPU 拓扑、Arm ISA 和 Vulkan 能力。

本项目适合快速验机、性能模式对比和版本回归检查，不用于替代严格控制环境下的实验室测量。

## RapidBench 测试什么

| 模块 | 测试和信息 |
| --- | --- |
| CPU | 可选核心的单核测试、动态识别 CPU 簇或全核心的多核测试、持续/峰值分数、亲和性检查、占用率和各簇峰值频率 |
| 内存 | 多线程读取、写入以及按双向流量统计的系统 `memcpy()` 带宽 |
| 存储 | 顺序读写、4 KiB Q1T1、4 KiB Q8T1、4 KiB Q1T4、SQLite Insert 和 SQLite Delete |
| GPU | Vulkan FP32、原生或模拟 FP16、INT32、Mixed Compute 和 GPU 内存带宽 |
| 设备 | CPU 拓扑、最大频率、容量分组、内核报告的 Arm ISA 等级、HWCAP/HWCAP2 指令特性、内存信息、Vulkan 特性/扩展，以及包含项目链接的 About App 信息 |

每张测试卡都可以单独运行。CPU 核心和多核簇选项来自当前设备的动态检测，不会假定设备固定只有两个或三个簇。

## 实现方式

界面使用 Flutter 编写，跑分核心使用 C++17，并通过稳定的 FFI 接口提供给 Dart。Storage 模块通过平台通道获取 Android 应用私有测试目录。计时工作循环全部保留在原生层，避免 UI 轮询进入测量路径。

```text
Flutter UI
  -> Dart Controller 与 FFI Binding
  -> 原生 C ABI
  -> C++ Benchmark Engine
  -> CPU 线程 / libc / Linux AIO / SQLite / Vulkan Compute
```

### CPU 算法

- RapidBench 在运行时读取 present/online CPU、亲和性掩码、capacity、cluster ID、全局 CPUFreq policy 和最大频率，并按照观测到的性能特征动态分组；旧内核未暴露逐 CPU CPUFreq 链接时也能识别簇和频率。
- 单核测试会尝试把一个 worker 绑定到所选逻辑 CPU；多核测试会为所选簇或全设备中的每个 present CPU 创建 worker。Android 拒绝亲和性请求时，worker 仍会持续施加负载并定期重试，同时显示 Affinity 警告，而不是直接让测试报错退出。
- Android 支持时，原生引擎会通过 Performance Hint 请求最高性能调度，并在测试期间持续报告实际工作时长。
- 每个 worker 使用包含 128 个 32 位整数和 128 个浮点数的确定性状态。每个 batch 进行 8 轮整数雪崩运算（加法、异或、乘法、移位和循环移位）、可向量化浮点递推以及 checksum 归约。Checksum 让计算结果保持可观察，防止编译器删掉负载。
- CPU 默认先预热 2.5 秒，再测量 10 秒。
- 原生层根据已完成的固定工作单元和耗时计算吞吐，界面显示“每秒百万固定工作单元”。测量期分为三个等长窗口，最终分数取三个窗口的中位数，最快窗口作为 Peak Score；窗口差异超过 7% 时会标记稳定性警告。

CPU 分数只适合在相同 RapidBench 工作负载版本之间比较，不是对其他跑分软件分数的换算。

### 内存算法

- 引擎使用当前允许使用的全部 CPU，并为每个 worker 分配互不重叠的对齐缓冲区区域。
- 工作集目标为 256 MiB，同时限制在可用内存的八分之一以内；内存压力较大时最低回退到 32 MiB。
- Read 使用展开的原生加载和归约内核；Write 使用展开的 128 字节写入模式；Copy 在各线程独立区域调用系统 `memcpy()`。
- Copy 按内存系统总流量统计：源读取和目标写入各计一次。因此实际复制 payload 为 20 GB/s 时，界面会显示约 40 GB/s 的双向流量。
- 所有 worker 会在预热和正式测量前同步。默认预热 1 秒，正式测量 3 秒。

### 存储算法

- 测试文件建立在 Android 应用私有目录中。RapidBench 会优先尝试使用对齐缓冲区和 `O_DIRECT`，不可用时明确回退到 Buffered I/O，并在系统支持时发出丢弃缓存建议。
- 顺序测试使用 1 MiB 块和 Q1T1；随机测试使用确定性打乱顺序的 4 KiB 块。
- Q1T1 使用同步 pread/pwrite；Q8T1 使用原生 Linux AIO，持续保持 8 个请求在途，并验证实际达到 QD8；Q1T4 使用 4 个原生线程在独立文件区域工作。
- 写入测试在计时阶段后执行 `fdatasync()`，Flush 时间与吞吐分开记录。
- SQLite 测试使用带索引的数据表和 512 字节 payload，Insert/Delete 均采用每 500 行一次的 Immediate Transaction；Delete 顺序使用固定种子打乱。
- Storage 默认预热 750 ms，正式测量 3 秒。

### GPU 算法

- RapidBench 动态加载 Vulkan，选择支持 Compute 的队列；存在多个候选时，优先选择支持 Timestamp 且不承担 Graphics 的队列。
- Compute 使用 1,024 个 workgroup，每组 64 个 invocation。FP Shader 对多条独立的 `vec4` 累加链执行 64 次迭代。
- FP32、原生 FP16 和模拟 FP16 都提供 8、12、16 条累加链变体。预热期间，RapidBench 按正序和逆序实测可用变体，并选择当前 GPU 上实际吞吐最高的版本。代码不包含 Adreno、Mali、Xclipse、PowerVR 或其他 GPU 品牌白名单。
- 12/16 路管线属于可选优化；驱动拒绝创建时会退回必需的 8 路管线。只有 Vulkan 报告 `shaderFloat16` 后才运行原生 FP16，否则使用兼容模拟路径。
- 16 区输出环形缓冲让连续 dispatch 写入不同区域，仅在区域即将复用时插入 Compute Barrier，从而减少不必要的串行等待，同时维持正确性。
- FLOPS/GOPS 按所选 Shader 的实际操作数计算。GPU Timestamp 只有在与独立测得的 Host Fence 时间保持合理一致时才会采用；无效或明显异常的样本会回退到已标记的 Host Timing。
- 每个 GPU 项目预热约 700 ms，正式测量约 6 秒。

### 设备特性检测

- Arm ISA 等级直接来自内核 `/proc/cpuinfo` 的 `CPU architecture`。RapidBench 不会根据无关 feature bit 猜测 Armv8.7-A 等 minor ISA；不同核心报告不一致或内核未暴露时会如实显示 Unknown。
- 指令特性独立读取 Linux `AT_HWCAP` 和 `AT_HWCAP2`，包括设备实际暴露的 NEON/ASIMD、AES、SHA、CRC32、LSE、FP16、DotProd、SVE/SVE2、BF16、I8MM 等。
- Vulkan 页面枚举 API/Driver 版本、设备扩展，以及驱动实际报告的 Shader FP16/int8、Descriptor Indexing、Timeline Semaphore、Buffer Device Address、Dynamic Rendering、Synchronization2、Fragment Shading Rate、Ray Query/Tracing、Acceleration Structure 和 Mesh Shader 等能力。

## 兼容性

- Android 7.0 / API 24 或更高版本。
- 当前发布 APK 仅包含 arm64-v8a。
- 没有 Vulkan 时，CPU、内存、存储和设备信息仍可使用；GPU 测试需要支持 Vulkan Compute 的设备。
- 实现只依赖标准 Android/Linux 和 Vulkan 接口，不按照 GPU 型号写厂商专用分支。本版本已在搭载 Adreno 830 的 PJZ110 上进行真机验证；其他 GPU 家族仍建议在对应真实驱动上复测。

## 构建

依赖：

- Flutter stable
- JDK 17
- Android SDK
- 带 `glslc` 的 Android NDK
- CMake 和 Ninja

```powershell
cd app/benchmark_app
flutter pub get
flutter analyze
flutter build apk --release --target-platform android-arm64
```

APK 输出位置：

```text
app/benchmark_app/build/app/outputs/flutter-apk/app-release.apk
```

### 签名说明

Release 构建会启用优化、混淆和资源压缩。只有构建环境同时提供
`RAPIDBENCH_RELEASE_STORE_FILE`、`RAPIDBENCH_RELEASE_STORE_PASSWORD`、
`RAPIDBENCH_RELEASE_KEY_ALIAS` 和 `RAPIDBENCH_RELEASE_KEY_PASSWORD` 时才会签名，
项目不再回退到任意 Debug Key。Beta 使用保留的 Beta 签名以保持升级兼容；正式版或商店发布前必须采用受保护的生产密钥并制定迁移方案。不得提交签名密码或私钥。

## 如何理解结果

- 保持设备温度较低、电量充足，测试时屏幕状态一致。
- 关闭高负载后台任务，并使用相同 RapidBench 版本进行对比。
- 温控、性能模式、固件调度、文件系统状态和厂商驱动都会影响结果。
- Peak 表示最快测量窗口；持续值更适合用于稳定对比。
- 内存和存储单位会显示在界面中；Copy 带宽有意同时统计 Read 与 Write 流量。

## 隐私

RapidBench 不申请网络权限。Storage 测试文件和 SQLite 数据库仅创建在应用私有目录中，并由测试引擎清理。除非用户自行导出或分享，否则跑分结果保留在设备本地。
