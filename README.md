# AudioSource Win (Raw WASAPI) v0.3.0

将 Android 手机麦克风用作 Windows 系统麦克风，通过 ADB + VB-CABLE + Raw WASAPI 实现。

## 工作原理

```
Android 手机麦克风 → [VoxMic Source App] → ADB → 本程序 → VB-CABLE → Windows 应用
                                                              ↓
                                    [DSP] RNNoise → HPF → EQ → Comp → Limiter
```

## v0.3.0 核心特性

| 特性 | 说明 |
|------|------|
| **RNNoise 神经网络降噪** | 官方 xiph/rnnoise v0.2，3 层 GRU，22 Bark 频段独立降噪，BSD-3 |
| DSP 管线 | HPF 80Hz + 6-band EQ + RMS Compressor + Peak Limiter |
| 低延迟 | **~90ms** (RNNoise 10ms + 传输 80ms) |
| Settings 全可控 | NR / EQ / Presence / Bass Cut / Compressor / Gain / 音效 |

## 系统要求

- Windows 10/11
- Android 设备 (已开启 USB 调试)
- [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) 已安装
- ADB 已加入 PATH

## 使用

```cmd
build\audiosource.exe --serial <serial>
```

在 Windows 应用中选择 **CABLE Output** 作为麦克风。

## 构建

### Windows

```cmd
build.bat
```

需要 Visual Studio 2022 (C++ 桌面开发)。

### Android App

```cmd
cd android_app
.\gradlew.bat assembleDebug --no-daemon --console=plain
adb -s <serial> install -r app\build\outputs\apk\debug\app-debug.apk
```

## 性能

| 指标 | v0.3.0 |
|------|--------|
| CPU 空闲 | ~0% |
| CPU 流式 | ~0.5–1.0% |
| 内存 | ~15 MB |
| 延迟 | ~90ms |
| 二进制 | ~1.5 MB |

## 延迟预算

| 组件 | 延迟 |
|------|------|
| Android 采集 | ~10ms (48000Hz/480 帧) |
| ADB + Socket | ~10ms |
| RNNoise 处理 | ~10ms |
| 环形缓冲 | ~30ms (3 块) |
| EQ + Comp + Lim | 0ms |
| WASAPI 缓冲 | ~20ms |
| VB-CABLE | ~10ms |
| **总计** | **~90ms** |

## 管线效果

| 阶段 | 参数 | 目的 |
|------|------|------|
| RNNoise | 22 频段 GRU 神经网络 | 背景降噪 + 人声保留 |
| HPF 80Hz | 12dB/oct | 切除风噪/震动 |
| Bass Cut | 120Hz shelf + 250Hz 衰减 (可调 -6~0dB) | 减少浑浊 |
| Presence | 2.5kHz + 3.2kHz 提升 (可调 0~6dB) | 辅音清晰度 |
| Compressor | 3:1, 5ms attack, 50ms release | 响度均匀 |
| Limiter | -1dBFS ceiling | 防削波 |

## 文档

[ARCHITECTURE.md](ARCHITECTURE.md) | [AGENTS.md](AGENTS.md) | [CHANGELOG.md](CHANGELOG.md) | [plan_optimize.md](plan_optimize.md) | [FUTURE_ROADMAP.md](FUTURE_ROADMAP.md)
