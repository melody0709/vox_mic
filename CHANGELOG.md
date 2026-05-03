# Changelog

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
