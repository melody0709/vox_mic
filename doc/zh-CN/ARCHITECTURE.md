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
    ├── MicUsageMonitor 线程 (逐会话事件 + 200ms 状态校准)
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
    │   │  1. 可选降噪后端：RNNoise 或 DPDFNet     │
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
| `config.h/cpp` | config.ini 持久化 (**20 字段**) |
| `settings_dialog.h/cpp` | **主窗口** GUI (设备/网络/App/音效/DSP/Debug，非模态持久窗口) |
| **`mic_usage_monitor.h/cpp`** | 逐会话 `IAudioSessionEvents`、周期校准、Demand Mode 防抖与 fail-open 策略 |
| **`mic_session_state.h`** | 带会话身份的幂等活动状态跟踪器，供 monitor 与回归测试复用 |
| **`dsp/biquad.h`** | BiQuad IIR (HPF/LowShelf/Peak/HighShelf) |
| **`dsp/pipeline.h`** | DSP 链调度 (RNNoise/DPDFNet→HPF→EQ→Comp→Limiter) |
| **`dsp/dpdfnet_processor.h/cpp`** | 可选固定头文件的 sherpa-onnx C ABI 动态加载、worker、epoch reset/watchdog 与 480-sample FIFO 适配 |
| **`dsp/rnnoise/` (27 文件)** | 官方 RNNoise v0.2 源码 + 预生成模型 (来源: werman fork) |

## DSP 管线详解

```
480 帧 float32[] 输入
    ↓
[1] 可选降噪后端  ← g_nrEnabled + DenoiseBackend 控制
    • RNNoise: rnnoise_process_frame(st, out, in)，支持 NR Strength
    • DPDFNet: 48 kHz 在线 ONNX 模型运行于独立 worker，变长输出放入 FIFO，再整理为固定 480 帧
    • 新 epoch 输入按自身 tag 先 reset 后处理；旧 epoch 或已被更晚 epoch 替代的输入/结果按 tag 丢弃
    • DPDFNet DLL/模型/ABI 失败或 worker 卡住时自动回退 RNNoise，不中断音频链路
    • 非法、超大或包含 NaN/Inf 的模型输出在进入 FIFO 前触发硬失败
    • reset 后最多容忍 4 个 10ms 预热静音块；稳态连续 3 个空输出 block 触发降级
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
| Denoise Backend | `rnnoise` | `g_denoiseBackend` / `DenoiseBackend` | RNNoise / DPDFNet 下拉框 |
| EQ Enable | true | `g_eqEnabled` | checkbox |
| Presence | +3.0 dB | `g_eqPresence` | slider 0–6dB |
| Bass Cut | -3.0 dB | `g_eqBassCut` | slider -6–0dB |
| Comp Enable | true | `g_compressorEnabled` | checkbox |

`NR Strength` 保留在 `config.ini` 中以兼容 RNNoise；选择 DPDFNet 时设置界面会禁用该滑块。关闭 NR 时实际后端显示为 `off`，重新开启后再按请求值选择 RNNoise/DPDFNet。即使 DPDFNet 资源不可用，请求值仍会持久化，实际后端单独报告为 RNNoise；资源恢复后可在 fresh prepare 时重新尝试。`g_dpdfnetDegraded` 区分“资源可用但 worker 卡住”和“runtime 缺失”，并在下一次流 reset 时清除后重试。DPDFNet 在线 API 没有模型强度滑块；VoxMic 固定使用 48 kHz `dpdfnet2_48khz_hr`、CPU provider、1 个推理线程并关闭 debug。这些属于发布/开发配置，不作为普通 Settings 选项。

## Phase 3+8: 按需激活 + 事件驱动

| 参数 | 值 | 原子变量 | 线程 |
|------|-----|----------|------|
| Monitor 检测 | 逐会话 COM 事件 + 200ms 状态校准 | `g_micRequested` | monitor |
| 推流门控 | discard when `!g_micRequested` | `g_micStreaming` | bridge → tray |
| 检测延迟 | 事件即时；漏事件最多 200ms 修复 | `g_micOnTick` | monitor → bridge |
| 退出防抖 | 最后一个会话 inactive 后 400ms | `g_micRequested` | monitor |
| Demand Mode 开关 | 右键托盘，持久化到注册表 | `g_demandMode` | tray → monitor |
| 空闲 ring buffer reset | 5s (50 × 100ms) | — | bridge |
| Socket 空闲断连 | 5s (500 blocks) Always Hot OFF | `g_alwaysHot` | bridge |
| Socket stall 断连 | 9s (90 × 100ms) | — | bridge |

## 线程模型

```
main thread:         消息泵 + SetTimer(stats, 5s)
monitor thread:      独占 MTA COM apartment；逐会话回调 + 200ms enumerate/GetState 状态校准
bridge thread:       ADB 一次性初始化 (CreateProcess NO_WINDOW) + Socket 按需连接 (idle 5s 断连, connect ~0.4ms) → g_micRequested 门控 → ring buffer push/discard
render thread:       事件驱动 ring buffer pop → int16→float → DspPipeline → WASAPI write
DPDFNet worker:      带 epoch tag 的 SPSC 输入队列 → epoch-aware reset → sherpa-onnx Run() → 校验后的带 tag 输出 FIFO → 固定 480 帧；失败 worker 等待 stop；渲染线程不做 DLL/模型 I/O
```

watchdog 无法取消永久不返回的原生 `Run()` 调用；该情况仍是已知退出限制。当前实现不会在释放 denoiser/DLL 前 detach worker，后续若要硬取消需要进程隔离。

## 延迟预算

| 组件 | 延迟 |
|------|------|
| Android ADC + HAL | ~10ms |
| AudioRecord read (480fr) | ~10ms |
| ADB + Socket | ~2ms |
| 环形缓冲区 | ~10ms (1-2 块) |
| RNNoise + EQ + Comp + Limiter | ~50µs (实测) |
| DPDFNet worker 推理 | 参考 smoke test EMA 约 1.7ms；reset 预热有上限，worker 无输出时回退 RNNoise |
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

## 可选 DPDFNet 运行载荷

默认 `build.bat` 只编译内置 RNNoise。仓库内的 DPDFNet 载荷位于 `third_party/dpdfnet/`，大体积模型和 DLL 使用 Git LFS 管理：

```
third_party/dpdfnet/
├─ include/sherpa-onnx/c-api/c-api.h
├─ model/dpdfnet2_48khz_hr.onnx
├─ runtime/sherpa-onnx-c-api.dll
├─ runtime/onnxruntime.dll
├─ runtime/onnxruntime_providers_shared.dll
└─ metadata.json
```

clone 后先执行 `git lfs pull`。`build.bat --dpdfnet` 会校验仓库内文件，将其暂存到 `build/cmake/x64-release/_deps/dpdfnet`，再把 DLL 安装到 exe 目录、模型安装到 `models/`。CMake 编译时使用固定 C API 头文件，但可执行文件仍通过 `GetProcAddress` 动态解析符号，不产生 sherpa-onnx import library 依赖。runtime manifest 会记录所有安装文件，因此删除 `build/` 只会删除生成物，不会丢失依赖来源。仓库依赖目录不可用时，准备脚本仍保留固定 SHA-256 的下载/缓存回退；正常仓库构建不需要联网。任何可选文件或 C API 符号缺失时，程序仍可启动并使用 RNNoise。
