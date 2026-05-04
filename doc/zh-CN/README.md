# VoxMic

**简体中文** | [English](../../README.md)

将 Android 手机麦克风用作 Windows 系统麦克风，通过 ADB + VB-CABLE + Raw WASAPI 实现。支持按需激活：有 Windows 应用使用 CABLE Output 时才推流，空闲时不走 DSP。

## 工作原理

```
Android 手机麦克风 → [VoxMic Source App] → ADB → 本程序 → VB-CABLE → Windows 应用
                                                              ↓
                                    [Phase 3] 事件驱动门控 ← MicUsageMonitor (COM 回调)
                                    [DSP] RNNoise → HPF → EQ → Comp → Limiter
```

## v0.5.0 核心特性

| 特性 | 说明 |
|------|------|
| **事件驱动 Monitor** | `IAudioSessionNotification` + `IAudioSessionEvents` 回调，零轮询零 COM 开销 |
| **Demand Mode 开关** | 右键托盘菜单控制按需激活，持久化到注册表 |
| **Socket 按需连接** | Always Hot OFF 时空闲 5s 断连，Android AudioRecord 停止，零耗电；重连仅 0.3-0.8ms |
| **Always Hot 开关** | 右键托盘菜单控制 socket 常连接，持久化到注册表 |
| **按需激活延迟** | ~200ms 冷启动，事件驱动即时检测 |
| **空闲 CPU 0-0.1%** | 事件驱动 + DSP 按需跳过，与 Python 版本持平 |
| **RNNoise 神经网络降噪** | 官方 xiph/rnnoise v0.2，3 层 GRU，22 Bark 频段独立降噪，BSD-3 |
| DSP 管线 | HPF 80Hz + 6-band EQ + RMS Compressor + Peak Limiter |
| 低延迟 | **~40ms** (实测) |

## 系统要求

- Windows 10/11
- Android 设备 (已开启 USB 调试)
- [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) 已安装
- ADB 已加入 PATH

## 使用

直接启动，程序自动隐藏到系统托盘：

```cmd
build\voxmic.exe
```

- **左键托盘图标** → 弹出设置窗口
- **右键托盘图标** → Demand Mode / Always Hot / Settings / Exit 菜单
- **关闭窗口 [X]** → 隐藏到托盘（不退出）
- 在 Windows 应用中选择 **CABLE Output** 作为麦克风。

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
adb -s <serial> install -r "app\build\outputs\apk\debug\VoxMic_Source-v0.5.3.apk"
```

## 性能

| 指标 | v0.5.0 |
|------|--------|
| CPU 空闲 | **0-0.1%** |
| CPU 激活 | ~0.1% (DSP) |
| 内存 | ~15 MB |
| 端到端延迟 | **~40ms** |
| 冷启动延迟 | **~200ms** |
| 二进制 | ~1.5 MB |

## 延迟预算

| 组件 | 延迟 |
|------|------|
| Android ADC + HAL | ~10ms |
| AudioRecord read (480fr) | ~10ms |
| ADB + Socket | ~2ms |
| 环形缓冲 | ~10ms (1-2 块) |
| RNNoise + EQ + Comp + Lim | ~50µs |
| WASAPI 缓冲 | ~11ms |
| VB-CABLE | ~3ms |
| **总计** | **~40ms** |

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

[ARCHITECTURE.md](ARCHITECTURE.md) | [AGENTS.md](AGENTS.md) | [CHANGELOG.md](CHANGELOG.md) | [FUTURE_ROADMAP.md](FUTURE_ROADMAP.md) | [plan/completed/](../../plan/completed/) (历史计划)