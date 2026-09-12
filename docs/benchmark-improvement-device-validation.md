# Benchmark 改进开发版：真机验证记录

日期：2026-09-12。范围：阶段 1 正确性修复、阶段 2-A CPU 应用型子项，以及新增子项自动选核调整。
这是本地 Debug 开发阶段的历史快照，不是公开发布验收、跨机型性能排名或四阶段计划全部完成。
后续 1.1.0 Beta 6 的 Release 构建与验收见 [发布记录](releases/1.1.0-beta.6.md)。下述“当前”均指本次 Debug 验证时刻。

## 设备与安装包

- 实际设备：OnePlus PJZ110，Snapdragon 8 Elite / Adreno 830，Android 16。
- 当前安装：`dev.cpu_benchmark.benchmark_app`，`0.0.0-dev` / versionCode `4019`，Debug / arm64-v8a。
- 本次覆盖安装前已通知用户手机可能需要确认；安装结果 `Success`，没有卸载或清除 App 数据。
- 当前包归档：`artifacts/RapidBench-unreleased-cpu-auto-arm64-debug.apk`，95,364,949 B。
- SHA-256：`21d5729f3ee01178dffc8c0510e46e66e248ee3e617cf89a9439004a2edf20c3`。
- 签名证书 SHA-256：`5e922b7fd56e3772dc26b0ef7da10843da824304e77deaca21d67562ca9c903e`，与此前安装包一致。
- 最初手机上的公开版本包保存在本地 `artifacts/device-before-improvement.apk`；旧公开 Tag、Release 与 GitHub 资产没有修改。
- 源文件公开版本仍为 `1.0.4-beta.5+4019`，开发构建通过命令参数覆盖 versionName。不要把本开发包当作新的公开 Beta 5 资产上传。

## 本次自动选核更新后执行

| 检查 | 结果 / 证据 |
| --- | --- |
| 原生 Android 编译 | 通过，`artifacts/cpu-auto-native-build.log` |
| Flutter analyze | 无问题，`artifacts/cpu-auto-flutter-analyze.log` |
| Flutter 全部测试 | 22 项通过，含 320px、1/2/3 倍字体与大数字，`artifacts/cpu-auto-flutter-tests.log` |
| 原生样机实跑 | 三项各 3 秒 Single/Multi；单核 CPU 6，全核 8 workers，全部完整校验且 affinity_failures=0；`artifacts/cpu-auto-native-device.log` |
| 自动策略模拟测试 | 1/8/10/12/64/65 核、稀疏/重复编号、不可用掩码、capacity 排序与频率回退；同一原生测试程序执行 |
| Debug 构建 / 安装 | 成功，`artifacts/cpu-auto-debug-build.log`、`artifacts/cpu-auto-device-install.log` |
| 安装后真实 App 操作 | 下述项目全部通过，`artifacts/cpu-auto-app-ui-test.log`，截图与 hierarchy 使用 `cpu-auto-app-*` 前缀 |

实际 App 操作由 `tools/test-android-cpu-applications.ps1` 执行，依据每次新读取的界面节点定位并点击：

1. 展开 APPLICATION WORKLOADS，分别完成 SORT、JSON RECORDS、IMAGE FILTER 的 Single/Multi 六次测试。
2. Multi 图像项目停止后保留 `STOPPED · PARTIAL`；重新开始后完成新一轮。
3. 测量中返回桌面，恢复 App 后结果为停止的部分结果，不自动续跑。
4. 在原 CPU 卡片中选 G0（6 workers），新 JSON Multi 仍显示 `All cores · 8 independent workers`。
5. 在原 CPU 卡片中选 CPU 0，新 JSON Single 仍显示 `CPU 6 · 1 worker`。
6. 恢复原 CPU 6 / All cores 选项；新子项只保留 Single/Multi 两种模式，没有核心/簇选择参数。
7. 核对进程未重启、无未处理异常、无 Flutter overflow、无 affinity fallback；查看实际截图中的数值与单位布局。

截图例子：`artifacts/cpu-auto-app-multi-sort.png`、`artifacts/cpu-auto-app-auto-multi-ignores-cluster.png`、
`artifacts/cpu-auto-app-auto-single-ignores-core.png`。原生日志还覆盖校验、计量分母、忙碌拒绝、取消/重启和 FFI。

## 同一开发周期此前已执行的整体回归

以下在自动选核调整之前的合并开发 APK 上执行，不能误写为对当前 APK 重复执行过全部项目。
自动选核调整未改 Memory、Storage、GPU 算法；当前 APK 重新执行的项目以上节为准。

- `tools/test-android-benchmark-ui.ps1` 实际 App 回归通过：CPU 运行排斥其他模块、页面可切换、后台取消；
  Memory Stop/Restart/Read/Write/Copy；Storage 全部 11 项，包括顺序 QD8、随机 Q1T1/QD8/Q1T4、SQL Insert/Update/Delete；GPU 五项完整流程。
- 证据：`artifacts/improvement-app-ui-test.log`、`artifacts/improvement-app-app-process.log` 与相关 XML。
- 较早新 CPU 子项界面回归：`artifacts/cpu-applications-ui-test.log`；新 CPU 运行排斥 Memory 的界面证据：
  `artifacts/cpu-applications-excludes-memory.xml`。该版当时仍接受原选择器，已被当前自动策略替代。
- 11 项原生回归与 12 项 Shader 结构检查：`artifacts/improvement-final-*`。
  GPU 全流程、测量中停止保留校验后部分分数、准备时取消清零均通过。
- GPU 末次原生功能样本约为 FP32 3155.239 GFLOPS、FP16 2976.072 GFLOPS；仅为功能样本，不是正式排名。

## 验证限制

没有 Exynos、旧麒麟、十核或其他 GPU 的真机；模拟拓扑不等于对应机型实测。
没有运行 ThreadSanitizer、Vulkan Validation Layers 或正式跨 SoC 校准；没有控制热状态后的多轮统计。
系统限制亲和性时仍可能回退并显示警告；应用没有权限保证核心始终处于最高频率。
Memory 延迟/工作集扫描、共享任务协作、完整结果导出及持续性统计仍在计划中，未宣称完成。
