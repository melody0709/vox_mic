# AGENTS.md — v0.3.0

## 项目概述

将 Android 手机麦克风用作 Windows 系统麦克风，通过 ADB + VB-CABLE + Raw WASAPI 实现。

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

## v0.3.0 关键参数

### 传输参数

| 参数 | 值 | 位置 |
|------|-----|------|
| FRAMES_PER_BLOCK | 480 | `wasapi_output.h:14` |
| BLOCK_SIZE | 960 字节 | `wasapi_output.h:16` |
| WASAPI 缓冲 | 20ms | `wasapi_output.cpp` |
| 环形水位 | 5→3 | `main.cpp` |
| 初始填充 | 3 块 | `main.cpp` |
| Android BLOCK_SIZE | 960 字节 | `RecordThread.java:37` |
| 总延迟 | ~90ms | |

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
bridge thread:       ADB 管理 + Socket recv → ring buffer push
render thread:       ring buffer pop → int16→float → DspPipeline → WASAPI write
```

## RNNoise 构建说明

来源: `werman/noise-suppression-for-voice` 的 `external/rnnoise/` (原始 `xiph/rnnoise` 缺少 autotools 生成的 `rnnoise_data.c/h`)。

构建: 所有 .c 文件作为 C 编译，`/I src\dsp\rnnoise` 加入 include 路径。**无外部依赖**。

## Next: DeepFilterNet3 (Phase 6C)

- libDF C 共享库，感知质量优于 RNNoise
- MIT/Apache-2.0 双协议
- 预编译 Windows .dll 可用
- 替换 RNNoise 为 DspPipeline 的 stage 0
