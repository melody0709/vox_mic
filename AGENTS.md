# AGENTS.md — v0.4.0

## 项目概述

将 Android 手机麦克风用作 Windows 系统麦克风，通过 ADB + VB-CABLE + Raw WASAPI 实现。支持按需激活：有 Windows 应用使用 CABLE Output 时才推流，空闲时不走 DSP。

## 文档组织

```
├── README.md                    # 项目总览
├── CHANGELOG.md                 # 版本变更记录
├── ARCHITECTURE.md              # 架构说明
├── FUTURE_ROADMAP.md            # 未来路线图
├── AGENTS.md                    # 本文件 (AI Agent 约束)
├── Audio_Settings_Guide.md      # 音频设置指南
├── REFACTORING_REPORT.md        # 重构报告
├── plan/
│   ├── completed/               # 已完成的 plan (不可修改, 记录历史)
│   │   ├── PHASE3_DESIGN.md     #   Phase 3 按需激活设计文档
│   │   └── plan_raw_wasapi.md   #   初始 Raw WASAPI 计划
│   └── ongoing/                 # 进行中的 plan (新建 plan 放这里)
│       └── plan_optimize.md     #   整体优化计划 (Phase 4/6C 待实施)
```

### Plan 管理规则

- 用户要求新建 plan → 在 `plan/ongoing/` 下创建 `plan_<topic>.md`
- Plan 实施完成后 → 移到 `plan/completed/`
- **禁止**直接在项目根目录新建 `.md` plan 文件
- `plan/ongoing/` 下的文件被 AI 视为当前活跃的工作目标

## 构建

### Windows 桥接程序

```cmd
build.bat
```

需要 Visual Studio 2022（含 C++ 桌面开发工作负载）。

### VoxMic Source Android App

环境：SDK `D:\@APP\android-platform-sdk\android-sdk`，Gradle 8.7，JDK 17。

```powershell
cd android_app
.\gradlew.bat assembleDebug --no-daemon --console=plain
adb -s <serial> install -r app\build\outputs\apk\debug\app-debug.apk
```

## 运行

```cmd
build\audiosource.exe --serial <serial>
```

在 Windows 应用中选择 **CABLE Output** 作为麦克风。

## v0.4.0 关键参数

### 传输参数

| 参数 | 值 | 位置 |
|------|-----|------|
| FRAMES_PER_BLOCK | 480 | `wasapi_output.h:14` |
| BLOCK_SIZE | 960 字节 | `wasapi_output.h:16` |
| WASAPI 缓冲 | 22ms (VB-CABLE 共享模式引擎下限) | `wasapi_output.cpp:73` |
| 环形水位 | 3→2 | `main.cpp:126-127` |
| 初始填充 | 0 (直接启动, 无等待) | `main.cpp` |
| Android BLOCK_SIZE | 960 字节 | `RecordThread.java:37` |
| Android AudioRecord 缓冲 | `1 × minBufSize` (~10ms) | `RecordService.java:101` |
| 总延迟 | **~40ms** (实测) | |

### 按需激活参数 (Phase 3)

| 参数 | 值 | 位置 |
|------|-----|------|
| Monitor 轮询间隔 | 100ms | `main.cpp:72` |
| 检测延迟 (实测) | **~12ms** | monitor ON → bridge push |
| 空闲 ring buffer reset | 5s (50 × 100ms) | `main.cpp:170-173` |
| Socket stall 断连阈值 | 9s (90 × 100ms) | `main.cpp:137` |
| Bridge 重连延迟 | ~200ms (仅 socket, 不走 ADB) | `main.cpp` |
| 初始 ADB 启动 | 一次性 (~1.5s) | `main.cpp` |

### DSP 管线

| 阶段 | 参数 | 可开关 | 位置 |
|------|------|--------|------|
| RNNoise | 22-Bark GRU, 10ms | `g_nrEnabled` | `dsp/pipeline.h` |
| HPF | 80Hz, Q=0.707 | 始终 | `dsp/pipeline.h` |
| EQ | 6-band, Pres 0–6dB, Bass -6–0dB | `g_eqEnabled` | `dsp/pipeline.h` |
| Compressor | -18dBFS, 3:1, 5/50ms | `g_compressorEnabled` | `dsp/pipeline.h` |
| Limiter | -1dBFS | 始终 | `dsp/pipeline.h` |

### 配置字段 (17)

| 分类 | 字段 | 类型 | 默认 |
|------|------|------|------|
| 网络 | serial / host / port | string/string/int | /127.0.0.1/27183 |
| Android | androidSocket / androidComponent / androidAppPreset | string/string/int | audiosource/fr.dzx.../0 |
| 增益 | gain | float (0.25~4.0) | 1.5 |
| 音效 | nsEnabled / aecEnabled / agcEnabled | bool | true/true/true |
| DSP | eqEnabled / compressorEnabled / nrEnabled | bool | true/true/true |
| DSP | eqPresence / eqBassCut | float (0~6 / -6~0) | 3.0 / -3.0 |

### 全局原子变量

| 变量 | 用途 | 线程 |
|------|------|------|
| `g_gain` | 增益倍率 | bridge → render |
| `g_nrEnabled` | RNNoise 开关 | bridge → render |
| `g_eqEnabled` | EQ 开关 | bridge → render |
| `g_eqPresence` | Presence dB | bridge → render |
| `g_eqBassCut` | Bass Cut dB | bridge → render |
| `g_compressorEnabled` | 压缩器开关 | bridge → render |
| `g_micRequested` | Windows 应用是否在捕获 (Phase 3) | monitor → bridge |
| `g_micStreaming` | 当前是否在推流 (Phase 3) | bridge → tray |
| `g_micOnTick` | 检测延迟时间戳 (Phase 3) | monitor → bridge |

`syncDspAtomsFromConfig()` 在 `main.cpp`，启动和每次重连时调用。

## 源文件结构

```
src/
├── main.cpp
├── wasapi_output.h/cpp
├── device_enum.h/cpp
├── ring_buffer.h
├── socket_client.h/cpp
├── adb_control.h/cpp
├── tray_icon.h/cpp
├── config.h/cpp
├── settings_dialog.h/cpp
├── mic_usage_monitor.h/cpp   # Phase 3: IAudioSessionManager2 按需检测
└── dsp/
    ├── biquad.h              # BiQuad IIR 滤波器
    ├── pipeline.h            # DSP 链 (RNNoise→HPF→EQ→Comp→Limiter)
    └── rnnoise/ (27 files)   # 官方 RNNoise v0.2 源码
        ├── rnnoise.h          # 公共 API
        ├── denoise.c/h        # 降噪核心
        ├── rnn.c/h            # RNN 推理
        ├── rnnoise_data.c/h   # 预生成模型权重 (5MB)
        ├── nnet.c/h           # 神经网络层
        ├── pitch.c/h          # 音高检测
        ├── kiss_fft.c/h       # FFT 库
        └── ...                # 辅助文件
```

## 线程模型

```
main thread:         消息泵 + SetTimer(stats)
monitor thread:      100ms 轮询 IAudioSessionManager2 → g_micRequested (Phase 3)
bridge thread:       ADB 管理 + Socket recv → g_micRequested 门控 → ring buffer push/discard
render thread:       ring buffer pop → int16→float → DspPipeline → WASAPI write
```

## RNNoise 构建说明

来源: `werman/noise-suppression-for-voice` 的 `external/rnnoise/` (原始 `xiph/rnnoise` 缺少 autotools 生成的 `rnnoise_data.c/h`)。

构建: 所有 .c 文件作为 C 编译，`/I src\dsp\rnnoise` 加入 include 路径。**无外部依赖**。

## Phase 3 — 麦克风按需激活 (已完成 v0.4.0)

- **MicUsageMonitor**: `src/mic_usage_monitor.h/cpp` — IAudioSessionManager2 100ms 轮询检测 CABLE Output 捕获状态
- **Always Hot**: bridge 永不主动断连 socket，空闲时 recv + 丢弃，不 push ring buffer
- **ADB 一次性初始化**: `SetupAudioSource` 只在启动时调用，socket 重连仅 `connect()` (~200ms)
- **检测延迟实测 ~12ms** (monitor ON → bridge 首块 push)
- **端到端延迟 ~40ms** (实测, 比 v0.3.0 的 ~85ms 减半)
- **空闲 CPU ~0.25%** (DSP 全跳, monitor 0.15%, WASAPI 静音 0.1%)

## Next: DeepFilterNet3 (Phase 6C)

- libDF C 共享库，感知质量优于 RNNoise
- MIT/Apache-2.0 双协议
- 预编译 Windows .dll 可用
- 替换 RNNoise 为 DspPipeline 的 stage 0
