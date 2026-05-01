# AudioSource Win (Raw WASAPI) v0.1.1

将 Android 手机麦克风用作 Windows 系统麦克风，通过 ADB + VB-CABLE + Raw WASAPI 实现。

## 工作原理

```
Android 手机麦克风 → [VoxMic Source App] → ADB 转发 → 本程序 → VB-CABLE → Windows 应用
```

## 版本特性

- **低延迟 ~83ms** (v0.1.1 从 400ms 优化 5 倍)
- WASAPI 事件驱动渲染 (CPU ~0.1%)
- 系统托盘右键菜单 (Start/Stop/Settings/Exit)
- Settings 对话框: ADB 设备 / Host / Port / Gain 滑块 / 音效开关
- 配置持久化到注册表 HKCU\Software\AudioSourceWin
- 48000 Hz 零重采样直通
- VoxMic Source Android App (独立构建安装)
- 音效独立开关 (NoiseSuppressor / AEC / AGC)

## 系统要求

- Windows 10/11
- Android 设备 (已开启 USB 调试)
- [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) 已安装
- ADB 已加入 PATH

## 使用

```cmd
build\audiosource.exe --serial <serial>
```

启动后在 Windows 应用中选择 **CABLE Output** 作为麦克风输入。

## 构建

### Windows 桥接程序

需要 Visual Studio 2022 (C++ 桌面开发工作负载)。

```cmd
build.bat
```

### VoxMic Source Android App

```cmd
cd android_app
.\gradlew.bat assembleDebug --no-daemon --console=plain
adb -s <serial> install -r app\build\outputs\apk\debug\app-debug.apk
```

## 性能

| 指标 | 数值 |
|------|------|
| CPU 空闲 | ~0% |
| CPU 流式 | ~0.1-0.3% |
| 内存 | ~10 MB |
| 延迟 | ~83ms |
| 二进制大小 | ~270 KB |

## 文档

[ARCHITECTURE.md](ARCHITECTURE.md) | [AGENTS.md](AGENTS.md) | [CHANGELOG.md](CHANGELOG.md) | [plan_optimize.md](plan_optimize.md)
