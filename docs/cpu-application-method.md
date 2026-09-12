# CPU 应用型负载 v1：方法与开发验证

状态：纳入 1.1.0 Beta 6 / 四阶段计划中的阶段 2-A。日期：2026-09-12。
**不是阶段 2 全部完成，也不是整体 SoC 性能评分。** 发布验收见 [Beta 6 记录](releases/1.1.0-beta.6.md)。

## UI 与旧分数的关系

CPU 页面原分数、峰值、核心/簇选择以及 BENCH CPU 自动顺序不变。
在原控制按钮下加入可折叠 `APPLICATION WORKLOADS`。Single 自动使用检测到的最高性能核心；
Multi 自动为全部 present 核心各创建一个 worker。新增子项没有核心/簇选择，也不读取上方旧卡片的选择；
点击对应卡片运行单项。三项结果独立显示数值和单位，不相加，不加入旧主分数或 Scaling。

Multi 为独立 worker 吞吐；每个 worker 拥有完整独立输入。共享同一任务的协作测试尚未实现。
停止后只保留已验证的部分结果，显示 `STOPPED · PARTIAL`；重新开始清空当前子项。
方法/输入大小及实际 worker 数随结果保存；单核结果记录实际目标 CPU，切换 Single/Multi 清空旧子项结果。

## 固定方法

| 项目 | 输入 | 计时内操作 | 单位 | 独立参考 |
| --- | --- | --- | --- | --- |
| SORT | 65,536 个 uint32，含重复与 0/UINT32_MAX | 输入复制重置 + `std::sort` | 百万键/秒 | 4 轮 8-bit radix，逐键比较 |
| JSON RECORDS | 8,192 条，共 573,048 B | 顺序解析字段，汇总数值、行数、布尔与字符串哈希 | 十进制 MB/s | 构造 corpus 时直接计算预期摘要，不通过被测解析器生成 |
| IMAGE FILTER | 1024×1024 uint8 灰度图 | 整数 Sobel；梯度绝对值之和截断到 255，边框清零 | 百万输入像素/秒，含边框 | 独立 3×3 权重循环卷积，逐像素比较 |

JSON 只覆盖固定有序的 `id/value/active/name` schema，允许 JSON 空白，整数非负，字符串为
无转义 ASCII；不代表任意 JSON、Unicode、DOM 分配或浏览器性能。图像项目不是解码、相机算法
或 AI 推理。排序为重复固定输入，工作集落入缓存是该应用型子项的特征，不应宣称它测量 DRAM 峰值。

每 worker 显示输入字节数；实际还需输出与参考缓冲，不能把显示输入量称为总内存占用。
输入与参考生成、分配、初始正确性检查在预热之前完成。

## 计时与生命周期

1. 每次开始重新检测拓扑。Single 按 capacity 优先、最大频率次优选出目标；capacity 全部缺失时退回最大频率，观测值并不能保证现实中每种负载的最佳核心。Multi 为全部 present CPU 建立 worker，不按当前 allowed/online 掩码缩减，无固定 8-worker 上限；超过支持的 64 核时明确失败，不静默截断。
2. 请求可用性能提示，尝试亲和性；测量期间周期检查与重试，任何降级保留告警。并不保证最高频率。
3. 700 ms 预热后，共同开始 3 秒测量；各 worker 连续完成整个 batch，截止后不再启动下一批。
4. 最终时间为共同开始至最后一个已计数 batch 实际完成，不截断到请求时长，不计入轮询或 join。
5. 全部 worker 退出计时后再进行完整结果校验及摘要读取，避免快核的校验干扰仍运行的慢核。
6. 校验/资源释放完成后才发布终态。Dart 持有全局互斥直到该终态；后台请求停止，不自动续跑。

吞吐公式：`sum(completed input units) × 1e9 / last_completion_elapsed_ns`。
使用 `cpu-application-v1`；新增接口是 additive，原 CPU 的请求/快照结构和测试 ID 不变。
没有 GPU 型号判断、分数放大系数或为了贴近理论值的修正。

## 验证

- 原生 `cpu_application_test --full` 在 PJZ110 / Snapdragon 8 Elite 上通过；三种负载各运行
  3 秒单核和全核，单核目标全部为 CPU 6，全核全部为 8 workers，affinity_failures 全部为 0。
  检查校验值、计量公式、忙碌拒绝、取消/重新开始、拒绝手工 worker 数和旧开发版请求布局、FFI。
- 自动选核的模拟拓扑覆盖 1/8/10/12/64/65 核、稀疏与重复 CPU 编号、allowed/online 不完整、
  capacity 排序及最大频率回退；模拟测试不等于这些真实机型通过。
- 初始输出未写入会被拒绝；JSON 畸形输入、越界整数、错误 schema、Sobel 小矩阵 golden 与 alias 拒绝通过。
- Flutter 静态检查通过；完整 22 项测试通过，新测试覆盖独立接口尺寸、单位、全局互斥、后台取消、
  清理先于解锁、单项点击和 320px 下 1/2/3 倍字体、大数字布局。
- 自动选核更新后的原生真机日志：`artifacts/cpu-auto-native-device.log`；Flutter：
  `artifacts/cpu-auto-flutter-analyze.log`、`artifacts/cpu-auto-flutter-tests.log`。
- 新 Debug 已同签名覆盖安装到样机：`0.0.0-dev` / code `4019`；构建和安装日志分别为
  `artifacts/cpu-auto-debug-build.log`、`artifacts/cpu-auto-device-install.log`，未发布 GitHub。
- 安装后 App 实跑三项 Single/Multi、停止/重开、后台停止，以及原选项独立性：原多核选 G0（6 核）
  后新 Multi 仍为 8 workers；原单核选 CPU 0 后新 Single 仍为 CPU 6。全程无进程重启、未处理异常
  或 Flutter overflow，日志与截图前缀为 `artifacts/cpu-auto-app-*`。
- 完整开发包与验收范围见 [真机验证记录](benchmark-improvement-device-validation.md)。

这些是功能/正确性回归，不是控制热状态后的正式排名。尚无旧麒麟、Exynos 或十核设备的真实测试；
没有 ThreadSanitizer 或跨 SoC 校准。已完成上述 App 操作和截图检查，但不等于所有界面状态或设备的全面验收。

## 下一阶段剩余工作

内存 pointer-chasing 延迟、工作集扫描、1/2/4/全核线程扫描；共享任务协作；结果导出、热状态及计时
回退元数据；持续测试与多轮统计。各项独立验证，公开版本在发布时按规范确定，不复用旧 Tag/资产。
