# 架构说明

**简体中文** | [English](../../ARCHITECTURE.md)

## 数据流

```
Android 手机麦克风 (48000Hz/单声道/int16)
    ↓
VoxMic Source App (DEFAULT 源 + 可选 NS/AEC/AGC)
    ↓ USB ADB
ADB 转发 (tcp:27183)
    ↓ TCP (960 字节/块 = 480 帧)
voxmic.exe
    ├── MicUsageMonitor 线程 (事件驱动: IAudioSessionNotification + IAudioSessionEvents)
    │       └→ g_micRequested (atomic<bool>)
    │
    ├── Socket 接收线程 (按需连接: idl 5s 断连, connect ~0.4ms 实测)
    │       │   recvExact(960) → g_micRequested ? push : discard
    │       │
    │   SPSC 无锁环形缓冲区 (128 块 × 960 字节)
    │       int16 → float32 (480 帧)
    │           ↓
    │   ┌─────────────────────────────────────────┐
    │   │  DSP 管线 (src/dsp/)                    │
    │   │  1. RNNoise 22-Bark GRU 降噪 + 梳状滤波  │
    │   │  2. HPF 80Hz BiQuad IIR                │
    │   │  3. EQ 6-band (Presence/Bass Cut 可调)  │
    │   │  4. RMS Compressor (3:1, 5ms/50ms)      │
    │   │  5. Peak Limiter (-1dBFS)               │
    │   └─────────────────────────────────────────┘
    │          ↓
    └── WASAPI 事件驱动渲染线程 (ratio=1.0 直通 + Gain)
            ↓
    VB-CABLE Input (48000Hz/立体声)
            ↓
    VB-CABLE Output
            ↓
    Windows 应用 (Zoom, Teams, 录音机, 语音输入法)
```

## 源文件说明

| 文件 | 职责 |
|------|------|
| `main.cpp` | 入口、主窗口创建 (非模态)、bridge 线程 (Always Hot)、monitor 线程、stats 定时器、DSP 原子变量 sync |
| `wasapi_output.h/cpp` | WASAPI 事件驱动初始化、渲染循环、Gain + DSP 管线注入、QPC 计时 |
| `device_enum.h/cpp` | WASAPI 设备枚举，VB-CABLE 查找 |
| `ring_buffer.h` | 无锁 SPSC 环形缓冲区 |
| `socket_client.h/cpp` | Winsock2 TCP 客户端 (含 waitForData) |
| `adb_control.h/cpp` | ADB 命令、设备检测、App 启动、端口转发、**forward 快速刷新** (**CreateProcess + CREATE_NO_WINDOW**，无闪烁) |
| `tray_icon.h/cpp` | 系统托盘 + 右键菜单 (含灰度版本号) |
| `config.h/cpp` | config.ini 持久化 (**21 字段**) |
| `settings_dialog.h/cpp` | **主窗口** GUI (设备/网络/App/音效/DSP/Debug，非模态持久窗口) |
| **`mic_usage_monitor.h/cpp`** | Phase 3+8: 事件驱动 (IAudioSessionNotification + IAudioSessionEvents) |
| **`dsp/biquad.h`** | BiQuad IIR (HPF/LowShelf/Peak/HighShelf) |
| **`dsp/pipeline.h`** | DSP 链调度 (RNNoise→HPF→EQ→Comp→Limiter) |
| **`dsp/rnnoise/` (27 文件)** | 官方 RNNoise v0.2 源码 + 预生成模型 (来源: werman fork) |

## DSP 管线详解

```
480 帧 float32[] 输入
    ↓
[1] RNNoise: rnnoise_process_frame(st, out, in)  ← g_nrEnabled 控制
    • 3 层 GRU (96+96+96), 22 Bark 频段, 85KB 量化权重
    • 每频段独立增益 + 梳状滤波 + VAD 概率
    • 延迟: 10ms (一帧)
    ↓
[2] HPF: Biquad 80Hz 12dB/oct  ← 始终激活
    ↓
[3] EQ 6-band:  ← g_eqEnabled 控制
    • 120Hz LowShelf (eqBassCut)
    • 250Hz Peaking (0.5×BassCut)
    • 2.5kHz Peaking (eqPresence)
    • 3.2kHz Peaking (0.6×Presence)
    • 8kHz HighShelf (+1.5dB)
    ↓
[4] RMS Compressor:  ← g_compressorEnabled 控制
    • -18dBFS threshold, 3:1 ratio, 6dB soft knee
    • 5ms attack, 50ms release, +5dB makeup
    ↓
[5] Peak Limiter: -1dBFS  ← 始终激活
    ↓
480 帧 float32[] 输出 → Gain → 插值 → 立体声 → WASAPI
```

## DSP 参数配置

| 参数 | 默认 | 原子变量 | Settings |
|------|------|----------|----------|
| NR Enable | true | `g_nrEnabled` | checkbox |
| EQ Enable | true | `g_eqEnabled` | checkbox |
| Presence | +3.0 dB | `g_eqPresence` | slider 0–6dB |
| Bass Cut | -3.0 dB | `g_eqBassCut` | slider -6–0dB |
| Comp Enable | true | `g_compressorEnabled` | checkbox |

## Phase 3+8: 按需激活 + 事件驱动

| 参数 | 值 | 原子变量 | 线程 |
|------|-----|----------|------|
| Monitor 检测 | 事件驱动 (COM 回调) | `g_micRequested` | monitor |
| 推流门控 | discard when `!g_micRequested` | `g_micStreaming` | bridge → tray |
| 检测延迟 | 即时 (COM 回调) | `g_micOnTick` | monitor → bridge |
| Demand Mode 开关 | 右键托盘，持久化到注册表 | `g_demandMode` | tray → monitor |
| 空闲 ring buffer reset | 5s (50 × 100ms) | — | bridge |
| Socket 空闲断连 | 5s (500 blocks) Always Hot OFF | `g_alwaysHot` | bridge |
| Socket stall 断连 | 9s (90 × 100ms) | — | bridge |

## 线程模型

```
main thread:         消息泵 + SetTimer(stats, 5s)
monitor thread:      Sleep(1000) 仅保持 COM 公寓存活 (Phase 8 事件驱动)
bridge thread:       ADB 一次性初始化 (CreateProcess NO_WINDOW) + Socket 按需连接 (idle 5s 断连, connect ~0.4ms) → g_micRequested 门控 → ring buffer push/discard
render thread:       事件驱动 ring buffer pop → int16→float → DspPipeline (47µs/块) → WASAPI write
```

## 延迟预算

| 组件 | 延迟 |
|------|------|
| Android ADC + HAL | ~10ms |
| AudioRecord read (480fr) | ~10ms |
| ADB + Socket | ~2ms |
| 环形缓冲区 | ~10ms (1-2 块) |
| RNNoise + EQ + Comp + Limiter | ~50µs (实测) |
| WASAPI 缓冲区 | ~11ms (VB-CABLE 22ms 半缓冲) |
| VB-CABLE | ~3ms |
| **总计** | **~40ms** (实测) |

## WASAPI 事件驱动

```
CreateEventEx → SetEventHandle → 预填充静音 → Start
  ↓
WaitForSingleObject(hEvent, 2000) → GetCurrentPadding → 算可用帧数
  ↓
内层 while: pop 环缓冲 → int16→float → DspPipeline.process() → Gain → ReleaseBuffer
  ↓
Stop
```

## Android App 通信

DSP 参数 (NR/EQ/Presence/BassCut/Comp) 仅在 Windows 端处理，不与 Android 通信。

Android 端仅传递音效标志 (NS/AEC/AGC) 给 `RecordService`。
