# Changelog

**简体中文** | [English](../../CHANGELOG.md)

## v0.6.7 (2026-09-04)

### Demand Mode 会话跟踪可靠性

| 修复 | 说明 |
|------|------|
| **逐会话状态** | 将无会话身份的共享 active 计数器替换为每个采集会话独立的 `IAudioSessionEvents` observer 和幂等状态跟踪，快速停止/重新开始不会重复计数或漏掉下一次激活。 |
| **回调校准** | 新增 200ms 会话枚举与状态校准；Core Audio 回调偶发遗漏或乱序时，会在整段录音静音前自动纠正。 |
| **静音语义** | 删除“端点连续三秒无峰值就视为无人使用”的规则；用户保持安静时仍然属于有效采集会话。 |
| **COM 生命周期** | Core Audio 初始化、校准和关闭全部放在同一个 monitor 线程，并明确持有/释放 session control。 |
| **安全回退** | 会话监控初始化失败时改为 fail-open 连续传音，避免输出永久数字静音。 |
| **诊断与测试** | Stats 新增源音频接收/丢弃/写入计数及校准次数；增加状态回归测试、真实 WASAPI 快速启停探针和 Android PCM RMS/peak/zero ratio 日志。 |
| **发布身份** | 桌面端/Android 升级为 `0.6.7` / Android `versionCode=13`。 |

### 托盘图标恢复

| 修复 | 说明 |
|------|------|
| **资源管理器重启** | 注册 `TaskbarCreated` 窗口消息，Windows 资源管理器重启时重新注册已有的 `NOTIFYICONDATA`，程序仍在运行时托盘图标不会再消失。 |
| **统一添加路径** | 将 `NIM_ADD` 调用抽取为 `TrayIcon::addIcon()`，首次创建与重新注册不会各自漂移；设置窗口在进入自身处理前先把消息转给托盘图标。 |

---

## v0.6.6 (2026-08-10)

### Settings 首次打开重绘

| 修复 | 说明 |
|------|------|
| **初始 Tab 可见性** | 显式初始化 General/DSP 子控件可见性，窗口显示时只同步当前 Tab。 |
| **Owner-draw 残影** | 隐藏 DPDFNet 状态控件时不再强制重绘，首次显示只重绘主窗口背景，避免状态文字出现在 General 控件上方。 |
| **发布身份** | 桌面端/Android 升级为 `0.6.6` / Android `versionCode=12`。 |

---

## v0.6.5 (2026-08-10)

### DPDFNet worker 加固

| 修复 | 说明 |
|------|------|
| **失败状态收敛** | worker 硬失败后清除 `ready` 状态、停止周期性空转；`setEpoch()` 不再操作失败 session，正常停止时快速退出。 |
| **Epoch 交接** | 新 pop 的 block 以自身 epoch 标签为准先 reset 后处理；被更晚 epoch 替代的输入和过期推理结果会被丢弃。 |
| **输出校验** | 超大、非法或包含 NaN/Inf 的 DPDFNet 输出在进入 FIFO 前触发硬失败并回退 RNNoise。 |
| **实际状态** | 增加 `off` effective backend，降噪关闭时 Settings/Stats 不再误报 DPDFNet 正在运行。 |
| **回归覆盖** | 新增 failure lifecycle smoke，覆盖失败状态收敛、epoch 短路和正常析构耗时。 |
| **发布身份** | 桌面端/Android 升级为 `0.6.5` / Android `versionCode=11`。 |

原生 DPDFNet API 理论上仍可能在 `Run()` 内永久阻塞；该情况作为已知限制记录，不通过 detach 或强制终止持有 DLL/session 资源的线程来处理。

---

## v0.6.4 (2026-08-09)

### DPDFNet 重试与状态修正

| 修复 | 说明 |
|------|------|
| **显式重试** | DPDFNet 处于“资源可用但 worker 卡住”的 degraded 状态时点击 OK 会发送 reset 请求，无需重启即可重试；普通后端/NR 变更仍由 render 的单次变更检测处理，不会重复 reset。 |
| **硬失败状态** | worker/session 真正失败时显示为 unavailable，而不是可重试的 stall degraded；音频保持 RNNoise，直到重新 prepare DPDFNet。 |
| **发布身份** | 由于 0.6.3 之后又修正了上述状态/重试逻辑，最终桌面/Android 版本升级为 `0.6.4` / Android `versionCode=10`。 |

---

## v0.6.3 (2026-08-09)

### DPDFNet 流式稳定性

| 修复 | 说明 |
|------|------|
| **Epoch 交接** | worker reset 不再清空输入队列，因此 render 已提交的新 epoch 首块会保留；推理期间变旧的结果也会在进入输出 FIFO 前丢弃。 |
| **静音有上限** | reset 后模型预热最多允许 4 个 10ms 无输出 block；稳态连续 3 个 block 无输出即降级到 RNNoise，不再无限静音。 |
| **运行状态** | 新增 `g_dpdfnetDegraded`、设置页状态和周期诊断，显示实际后端、可用性、underflow、FIFO drop 与 worker 耗时；下一次流 reset 会重试可用的 DPDFNet。 |
| **ABI 维护** | 编译时使用固定版本的 sherpa-onnx C API 头文件，同时保留 DLL 动态加载，不增加 sherpa-onnx import library 硬依赖。 |
| **回归测试** | 新增 RNNoise ↔ DPDFNet 切换、重复 epoch reset、worker 卡住降级的 pipeline smoke，并强化 processor reset 覆盖。 |

---

## v0.6.2 (2026-08-09)

### 后端状态文字绘制

| 修复 | 说明 |
|------|------|
| **状态文字重叠** | 动态降噪后端状态改为不透明绘制，重绘前明确擦除旧文字；同时为 runtime 不可用提示预留两行高度。 |

---

## v0.6.1 (2026-08-09)

### RNNoise / DPDFNet 降噪后端可切换

| 改动 | 说明 |
|------|------|
| **后端选择** | 新增持久化配置 `DenoiseBackend=rnnoise|dpdfnet` 与 DSP 设置下拉框，默认仍为 RNNoise。 |
| **流式 DPDFNet** | 新增动态加载 sherpa-onnx C API 的适配器，包含独立 worker、epoch reset 和固定 480-sample FIFO 输出。 |
| **安全回退** | runtime DLL、模型、API 符号、采样契约或 worker 失败时自动回退 RNNoise。 |
| **构建模式** | 新增 `build.bat --dpdfnet`；RNNoise-only 仍为单 exe，DPDFNet 构建额外包含经过校验的 runtime/model/notices。 |
| **可复现依赖** | 将固定版本的 DPDFNet 模型、sherpa-onnx runtime DLL 和 C API 头文件放入 `third_party/dpdfnet`，大文件使用 Git LFS；清空 `build/` 后仍可离线重新生成。 |
| **清理安全性** | `build.bat --clean` 遇到被占用的生成文件时会明确失败，不再误报清理成功。 |
| **验证** | 新增 DPDFNet streaming/fallback smoke test、runtime manifest 文件校验和固定 SHA-256 依赖准备脚本。 |

DPDFNet 只是可选后端。其主观音质和端到端延迟仍需在目标 Android 设备及实际噪声环境中做 A/B 测试；本版本不会将其设为默认后端。

---

## v0.5.3 (2026-05-03)

### scrcpy 开关后 ADB forward 丢失自动恢复

#### Bug 修复

| Bug | 描述 | 修复 |
|-----|------|------|
| **scrcpy 关闭后无法恢复** | scrcpy 关闭时清理 ADB forward (`tcp:27183`)，导致 bridge connect 10061，需重启 VoxMic | 第 1 次 connect 失败且之前成功过 → `refreshForward()` 快速重建 forward（~100ms）→ 重试成功 |
| **3 次快速断开无恢复** | socket 连接后 <3 秒断开反复发生，无自动修复 | quick disconnect 计数 ≥ 3 → `setupAudioSource()` 完整重建（重启 app + forward） |

#### 改动文件

| 文件 | 改动 |
|------|------|
| `src/adb_control.h` | 新增 `refreshForward()` 声明 |
| `src/adb_control.cpp` | 实现 `refreshForward()` = `removeForward` + `createForward`（~100ms） |
| `src/main.cpp` | bridge 连接失败检测：`connectFailCount` + `wasPreviouslyConnected`；第 1 次失败调用 `refreshForward`；3 次失败调用 `setupAudioSource`；quick disconnect 检测 |
| `RecordService.java` | 提取 `createRecorder()` 工厂方法 + `releaseAudioEffects()` 辅助方法 |
| `RecordThread.java` | 诊断日志（accept/recorder state/blocks sent/connection closed） |

#### 恢复延迟对比

| 场景 | v0.5.2 | v0.5.3 |
|------|--------|--------|
| scrcpy 后第 1 次语音输入 | 3-5 秒（3 次失败 + 1.5s 重建） | ~300ms（1 次失败 + 100ms 刷新 + 重试） |
| 正常 idle 恢复 | 无变化 | 无变化 |

---

## v0.5.2 (2026-05-03)

### Phase 12: RNNoise 降噪强度可调 + Hint 样式优化

| 特性 | 说明 |
|------|------|
| **NR Strength 滑块** | 0.30–0.95，默认 0.60，调节 RNNoise alpha 增益平滑参数（降噪激进程度） |
| **滑块 Hint** | Presence / Bass Cut / NR Strength 下方灰色小字说明，16pt 字体 |
| **Build 零 Warning** | `/wd4305 /wd4244` 抑制 RNNoise 上游警告，项目代码 C4100/C4189 已修复 |

#### 改动文件

| 文件 | 改动 |
|------|------|
| `src/dsp/rnnoise/denoise.c` | `DenoiseState` 加 `strength` 字段；`alpha` 从字段读取；`rnnoise_set_strength()` 实现 |
| `src/dsp/rnnoise/rnnoise.h` | 新增 `rnnoise_set_strength(DenoiseState*, float)` |
| `src/config.h/cpp` | 新增 `nrStrength` 字段 (0.3–0.95, 默认 0.6), INI 键 `NrStrength`，配置字段 → 21 |
| `src/dsp/pipeline.h` | 新增 `g_nrStrength` 原子变量 + `updateSettings()` 调用 `rnnoise_set_strength` |
| `src/main.cpp` | `syncDspAtomsFromConfig()` 新增 `g_nrStrength` 同步 |
| `src/settings_dialog.cpp` | DSP 标签页新增 NR Strength 滑块 + 3 个 hint 标签（小字灰色，WM_CTLCOLORSTATIC） |
| `build.bat` | 链接 `gdi32.lib`，编译标志 `/wd4305 /wd4244` |
| `AGENTS.md` | "注册表持久化" → "config.ini"；配置字段 20→21；新增 release 构建说明 |

#### 清理归档

| 文件 | 操作 |
|------|------|
| `plan_nr_strength.md` | 新增 → `plan/ongoing/` |
| `plan_hint_style.md` | 新增 → `plan/ongoing/` |
| `plan_optimize.md` | `plan/ongoing/` → `plan/completed/` |

---

## v0.5.1 (2026-05-03)

### Phase 10: 按需激活检测修复 — Sound Recorder 停止后不 idle

#### Bug 修复

| Bug | 描述 | 修复 |
|-----|------|------|
| **Bug 1** | Monitor 监听默认采集设备（内置麦克风），而非 CABLE Output | 改用 `EnumAudioEndpoints(eCapture)` 按名称 "CABLE Output" 匹配 |
| **Bug 2** | Sound Recorder (UWP) 停止/关闭后不释放 `IAudioClient`，`AudioSessionState` 保持 Active | 新增 `IAudioMeterInformation` 静音兜底，连续 3s 峰值=0 则强制 idle |

#### 三层检测架构

| 层次 | 机制 | 触发方式 | 延迟 | CPU 开销 |
|------|------|---------|------|---------|
| 1 | `OnStateChanged(Active/Inactive)` | COM 事件回调 | 即时 | 零 |
| 2 | `renderStallScore` | render event 超时 (3×2000ms) | ~6s | 零 (已有) |
| 3 | `IAudioMeterInformation::GetPeakValue()` | monitor 线程 (仅活跃态) | ~3s | 1次 COM/秒 |

**idle 态**：`g_micRequested == false` 时 monitor 线程仅 `Sleep(1000)`，零 COM 调用，零 CPU。

#### 修改文件

| 文件 | 改动 |
|------|------|
| `src/mic_usage_monitor.h` | 新增 `IAudioMeterInformation* m_pMeter` + `getCapturePeak()` |
| `src/mic_usage_monitor.cpp` | `init()` 枚举 eCapture 找 CABLE Output + 激活 IAudioMeterInformation；`shutdown()` 释放 |
| `src/wasapi_output.h` | 新增 `std::atomic<int> renderStallScore` |
| `src/wasapi_output.cpp` | `renderThread()` 追踪 `WaitForSingleObject` 超时计数 |
| `src/main.cpp` | `micMonitorThread()` 活跃态 meter 轮询 + 静音强制 idle；bridge 线程 `effectiveActive` 逻辑整合 |

---

## v0.5.0 (2026-05-03)

### Phase 9: Socket 按需连接 — Android 空闲省电

| 特性 | 说明 |
|------|------|
| **Always Hot 开关** | 右键托盘菜单勾选控制，持久化到注册表，**默认 OFF** |
| **空闲断连** | Always Hot OFF 时，空闲 5s 后主动断连 socket → Android `AudioRecord` 停止 → 零耗电 |
| **按需重连** | 外层循环轮询 `g_micRequested` (Sleep 200ms)，有需求时 `connect()` **~0.3-0.8ms (QPC 实测)** |
| **冷启动延迟** | ~200ms (主要为 Sleep 200ms 轮询，socket connect 可忽略) |
| **Android 端** | 无需改动：`accept → startRecording → stop → accept` 循环原生匹配 |

#### 实测数据

| 指标 | 值 |
|------|-----|
| socket 重连耗时 | 0.33-0.81ms (QPC, 远低于预估 ~200ms) |
| 冷启动延迟 | ~200ms (Sleep 200ms 轮询为主) |
| 空闲 CPU | 0-0.1% (不变) |

#### 修改文件

| 文件 | 改动 |
|------|------|
| `src/main.cpp` | 新增 `g_alwaysHot` 原子变量；外层循环空闲等待；内层循环 idle 500 blocks 断连；QPC socket connect 计时 |
| `src/tray_icon.h/cpp` | 新增 `ID_MENU_ALWAYS_HOT` 菜单项 + `setAlwaysHot()` |
| `src/settings_dialog.cpp` | Always Hot 菜单切换逻辑 + 注册表保存 |
| `src/config.h/cpp` | 新增 `alwaysHot` 字段（20 字段），默认 false，注册表持久化 |

---

## v0.4.2 (2026-05-02)

### Phase 8: CPU 优化 + 事件驱动 Monitor

| 特性 | 说明 |
|------|------|
| **事件驱动 Monitor** | `IAudioSessionNotification` + `IAudioSessionEvents` 回调替代 100ms 轮询，零 COM 开销 |
| **Demand Mode 开关** | 右键托盘菜单 "Demand Mode" 勾选，控制按需激活开关 |
| **Demand Mode 持久化** | 设置保存到注册表，重启后保持状态 |
| **空闲 CPU** | Demand ON: **0-0.1%**（从 v0.4.1 的 0.2-0.4% 优化） |
| **DSP 开销实测** | 仅 ~0.1%（远低于设计文档预估的 ~1.65%） |

#### 优化历程

| 阶段 | 做法 | CPU |
|------|------|-----|
| v0.4.0 monitor 轮询 | 每 100ms `Activate(IAudioSessionManager2)` + 枚举 + `Release` | 0.2-0.4% |
| 方案 A COM 缓存 | `init()` 时创建并缓存 `IAudioSessionManager2`，每 100ms 仅创建枚举器 | 0.15-0.25% |
| 方案 B 事件驱动 | `IAudioSessionNotification` + `IAudioSessionEvents` 回调，零轮询 | **0-0.1%** |

#### 修改文件

| 文件 | 改动 |
|------|------|
| `src/mic_usage_monitor.h` | 重写：实现 `IAudioSessionNotification` + `IAudioSessionEvents` COM 接口 |
| `src/mic_usage_monitor.cpp` | 重写：事件驱动 `OnStateChanged` 更新 `g_micRequested`，`OnSessionCreated` 注册新会话 |
| `src/main.cpp` | `g_micRequested` 非 static 可 extern；monitor 线程仅 `Sleep(1000)` 保持 COM 公寓 |
| `src/main.cpp` | 新增 `g_demandMode` 原子变量，从 Config 初始化 |
| `src/tray_icon.h/cpp` | 新增 `ID_MENU_DEMAND_MODE` 菜单项 + `setDemandMode()` |
| `src/settings_dialog.cpp` | Demand Mode 菜单切换逻辑 + 注册表保存 |
| `src/config.h/cpp` | 新增 `demandMode` 字段（19 字段），注册表持久化 |

---

## v0.4.1 (2026-05-02)

### Phase 5: 隐藏到托盘 GUI + ADB 无闪烁

| 特性 | 说明 |
|------|------|
| **隐藏到托盘** | 设置窗口提升为主窗口，程序启动即隐藏到托盘，右键 Exit 才退出 |
| **托盘交互** | 左键托盘图标弹出设置窗口，关闭窗口即隐藏（不退出） |
| **ADB 无闪烁** | `CreateProcess` + `CREATE_NO_WINDOW` 替换所有 `_popen("adb ...")`，后台调用零黑窗 |
| **/SUBSYSTEM:WINDOWS** | 无控制台子系统启动，无任何窗口闪烁 |
| **Debug Console 按需** | 勾选才 `AllocConsole()`，默认隐藏，复选框持久化到注册表 |
| **启动信息日志** | 启动时打印 Gain/DSP/Android HW 设置摘要 |
| **版本号** | 托盘右键菜单底部显示灰色 `v0.4.1` |

#### 修改文件

| 文件 | 改动 |
|------|------|
| `build.bat` | 链接器 + `/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup` |
| `src/adb_control.h/cpp` | `runCommand` → 公开 `runCommandNoWindow()` (CreateProcess + CREATE_NO_WINDOW) |
| `src/settings_dialog.h/cpp` | 模态 `showSettingsDialog` → 非模态持久窗口 `createSettingsWindow` + `loadSettingsWindow` |
| `src/main.cpp` | 删除 `WindowProc`/`HWND_MESSAGE`，改为 `createSettingsWindow`，`setConsoleVisible()` 用 FreeConsole/AllocConsole |
| `src/tray_icon.cpp` | "Settings..." → "Settings"，新增灰色版本号菜单项 |
| `src/config.h/cpp` | 新增 `debugConsole` 字段 (18 字段) |

#### 构建变更

```
# v0.4.0
/link ws2_32.lib ole32.lib mmdevapi.lib shell32.lib advapi32.lib comctl32.lib

# v0.4.1
/link ... /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
```

---

## v0.4.0 (2026-05-01)

### Phase 3: 麦克风按需激活 + 延迟压缩

| 特性 | 说明 |
|------|------|
| **MicUsageMonitor** | IAudioSessionManager2 100ms 轮询, 检测 CABLE Output 是否被 Windows 应用占用 |
| **Always Hot** | Bridge 永不主动断连 socket, 空闲时 recv + 丢弃, 不 push ring buffer |
| **按需推流** | `g_micRequested` 控制 bridge push/discard, 空闲 CPU ~0.25% |
| **ADB 一次性启动** | `setupAudioSource` 仅首次调用, socket 重连 ~200ms (不走 ADB) |
| **延迟压缩 85→40ms** | Android AudioRecord 2×→1× minBufSize, 水位 5→3→3→2, 初始填充 3→0 |

#### 新增文件

| 文件 | 说明 |
|------|------|
| `src/mic_usage_monitor.h/cpp` | Capture endpoint session 轮询检测 |

#### 修改文件

| 文件 | 改动 |
|------|------|
| `src/main.cpp` | Monitor 线程 + bridge Always Hot discard 逻辑 + detect latency 计时 |
| `src/socket_client.h/cpp` | 新增 `waitForData(timeoutMs)` |
| `src/wasapi_output.h/cpp` | `procUsEma`/`estLatencyMs` QPC 计时 |
| `src/wasapi_output.cpp:73` | WASAPI 缓冲 200000→100000 hns (实际下限 22ms) |
| `RecordService.java:101` | AudioRecord 缓冲 `2×→1× minBufSize` |
| `RecordThread.java` | 新增 `elapsedRealtimeNanos` read 计时日志 |
| `build.bat` | 新增 `mic_usage_monitor.cpp` 编译 |

#### 参数变更

| 参数 | v0.3.0 | v0.4.0 |
|------|--------|--------|
| Android AudioRecord 缓冲 | 2× minBufSize (~20ms) | **1× minBufSize (~10ms)** |
| 环形水位 | 5→3 | **3→2** |
| 初始填充 | 3 块 (30ms) | **0 (直接启动)** |
| WASAPI 缓冲 | 20ms (请求) | 22ms (VB-CABLE 引擎下限) |
| 总延迟 | ~90ms | **~40ms** (实测) |
| 检测延迟 | N/A | **~12ms** (实测) |
| 空闲 CPU | ~2% | **~0.25%** |

#### 新的全局原子变量

| 变量 | 用途 | 线程 |
|------|------|------|
| `g_micRequested` | Windows 应用是否在捕获 | monitor → bridge |
| `g_micStreaming` | 是否正在推流 | bridge → tray icon |
| `g_micOnTick` | 检测延迟测量时间戳 | monitor → bridge |
| `procUsEma` / `estLatencyMs` | 单块处理时间 EMA / 估算延迟 | render threads |

#### 实测数据

- 语音输入法 CapsLock 长按 300ms 激活: `[Monitor] mic=ON/OFF` 精准跟随
- 检测延迟: 0-16ms, 均值 ~12ms
- 端到端延迟: ~36-46ms (Phone: read ~20ms + HAL ~10ms, PC: queue 1-2 + WASAPI 11ms + DSP 50us)
- 60s 连续测试: `drop=0 underrun=3049 queue=0~1 proc=47us lat=11~21ms`
- 空闲 CPU: monitor 0.15% + WASAPI 静音 0.1% = 0.25%

### 线程模型变更

```
v0.3.0                          v0.4.0
main                             main
bridge                           monitor (NEW: 100ms IAudioSessionManager2 轮询)
render                           bridge (g_micRequested 门控 push/discard)
                                 render
```

---

## v0.3.0 (2026-05-01)

### 官方 RNNoise 神经网络降噪集成

基于 `werman/noise-suppression-for-voice` 的 `external/rnnoise` fork（含预生成模型文件）。

| 特性 | 说明 |
|------|------|
| 算法 | 3 层 GRU 网络 (96+96+96)，22 Bark 频段独立降噪 + 梳状滤波 |
| 模型 | v0.2 权重 (128+384+384 维度，Amazon 优化)，85KB 量化，编译进二进制 |
| 延迟 | 10ms/frame (480 帧 @ 48kHz) |
| 版权 | BSD-3-Clause |

### 管线

```
RNNoise (10ms) → HPF 80Hz → EQ 6-band → RMS Comp → Limiter → WASAPI
```

### 参数变更

| 参数 | v0.2.0 | v0.3.0 |
|------|--------|--------|
| FRAMES_PER_BLOCK | 512 (10.7ms) | **480 (10.0ms)** |
| Android BLOCK_SIZE | 1024 字节 | **960 字节** |
| 总延迟 | ~83ms | **~90ms** |

### 稳定性验证

60 秒压力测试: `recv=5819 drop=3 underrun=0 queue=1~2`

| 指标 | v0.2.0 | v0.3.0 |
|------|--------|--------|
| underrun | 0 | **0** |
| drop | 6–9 (0.15%) | **3 (0.05%)** |
| queue | 3–5 | **1–2** (极致精简) |
| 二进制 | ~270 KB | **~1.5 MB** |

480 帧块让缓冲区更紧凑 (queue 仅 1–2)，drop 率历史最低。

### Settings

| 控件 | 默认 | 说明 |
|------|------|------|
| Noise Reduction | on | RNNoise 开关 |

---

## v0.2.0 (2026-05-01)

### 自研 DSP 音频后处理管线

| 阶段 | 算法 | 延迟 | 目的 |
|------|------|------|------|
| HPF | BiQuad IIR 80Hz | 0ms | 切除风噪/直流 |
| EQ | 6-band BiQuad (Presence + Bass Cut 可调) | 0ms | 人声优化 |
| Compressor | RMS 3:1, 5ms/50ms | 0ms | 响度统一 |
| Limiter | 峰值跟随 -1dBFS | 0ms | 防削波 |

### Settings 新增

EQ Enable / Presence (0–6dB) / Bass Cut (-6–0dB) / Compressor Enable

### 副作用修复

Settings 对话框: 纯音效修改也更新内存配置 (原 bug: 仅 serial/host/port 变化才生效)

---

## v0.1.1 (2026-05-01)

### 延迟优化 — 400ms → 83ms

| 参数 | v0.1.0 | v0.1.1 | 影响 |
|------|--------|--------|------|
| FRAMES_PER_BLOCK | 1024 (21.3ms) | **512 (10.7ms)** | 块粒度减半 |
| WASAPI buffer | 200ms | **20ms** | 缓冲延迟 -90% |
| 环形水位 | 16→8 | **5→3** | 排队延迟 -85% |
| 初始填充 | 3 块 (~70ms) | **3 块 (~32ms)** | 启动等待 -55% |
| Android 块 | 2048 字节 | **1024 字节** | 对齐 512 帧 |
| 总延迟 | ~400ms | **~83ms** | **5 倍提升** |

2 分钟压测: `recv=11090 drop=12 underrun=0`

---

## v0.1.0 (2026-04-30)

### 初始功能集

- WASAPI 事件驱动渲染 (SetEventHandle + WaitForSingleObject)
- 系统托盘 + Settings 对话框 (设备/网络/App/音效/Gain)
- 注册表配置持久化 (12 字段)
- 48000 Hz Android ↔ Windows 对齐 (零重采样)
- Gain 0.25x–4.0x 滑块
- 音效独立开关 (NS/AEC/AGC checkbox + ADB --ez 传参)
- VoxMic Source Android App (独立构建安装)
- Xiaomi 设备 AudioSource 兼容性实验 (锁定 DEFAULT)
