# AudioSource Win (Raw WASAPI) v0.4.2

将 Android 手机麦克风用作 Windows 系统麦克风，通过 ADB + VB-CABLE + Raw WASAPI 实现。支持按需激活：有 Windows 应用使用 CABLE Output 时才推流，空闲时不走 DSP。

## 工作原理

```
Android 手机麦克风 → [VoxMic Source App] → ADB → 本程序 → VB-CABLE → Windows 应用
                                                              ↓
                                    [Phase 3] 事件驱动门控 ← MicUsageMonitor (COM 回调)
                                    [DSP] RNNoise → HPF → EQ → Comp → Limiter
```

## v0.4.2 核心特性

| 特性 | 说明 |
|------|------|
| **事件驱动 Monitor** | `IAudioSessionNotification` + `IAudioSessionEvents` 回调，零轮询零 COM 开销 |
| **Demand Mode 开关** | 右键托盘菜单控制按需激活，持久化到注册表 |
| **空闲 CPU 0-0.1%** | 事件驱动 + DSP 按需跳过，与 Python 版本持平 |
| **隐藏到托盘 GUI** | 无 CMD 窗口，左键托盘图标弹出设置窗口，关闭窗口即隐藏到托盘 |
| **ADB 无闪烁** | CreateProcess + CREATE_NO_WINDOW 替换 _popen，后台调用完全不闪 |
| **Debug Console 按需** | 勾选才弹出控制台日志，默认隐藏，零干扰 |
| **Always Hot** | socket 永不主动断连，空闲时丢弃数据，激活延迟 ~12ms |
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
build\audiosource.exe
```

- **左键托盘图标** → 弹出设置窗口
- **右键托盘图标** → Start/Stop/Settings/Exit 菜单
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
adb -s <serial> install -r app\build\outputs\apk\debug\app-debug.apk
```

## 性能

| 指标 | v0.4.2 |
|------|--------|
| CPU 空闲 | **0-0.1%** |
| CPU 激活 | ~0.1% (DSP) |
| 内存 | ~15 MB |
| 端到端延迟 | **~40ms** |
| 按需激活延迟 | **事件驱动，即时响应** |
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

[ARCHITECTURE.md](ARCHITECTURE.md) | [AGENTS.md](AGENTS.md) | [CHANGELOG.md](CHANGELOG.md) | [FUTURE_ROADMAP.md](FUTURE_ROADMAP.md) | [plan/completed/](plan/completed/) (历史计划)
