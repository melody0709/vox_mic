# AudioSource Win (Raw WASAPI) v0.1.0

将 Android 手机麦克风用作 Windows 系统麦克风，通过 ADB + VB-CABLE + Raw WASAPI 实现。

## 工作原理

```
Android 手机麦克风 → [VoxMic Source App] → ADB 转发 → 本程序 → VB-CABLE → Windows 应用
```

## 版本特性 (v0.1.0)

| 特性 | 说明 |
|------|------|
| WASAPI 事件驱动渲染 | SetEventHandle + WaitForSingleObject，CPU ~0.1% |
| 系统托盘 | 右键菜单：Start/Stop/Settings/Exit |
| Settings 对话框 | ADB 设备选择 / Host / Port / Gain 滑块 / 音效开关 |
| 配置持久化 | 注册表 HKCU\Software\AudioSourceWin |
| Android 48000Hz 对齐 | 零重采样损失 |
| VoxMic Source Android App | DEFAULT 源 + NS/AEC/AGC 可选开关 |
| 音频效果独立开关 | Settings 中 ☑ 勾选 NoiseSuppressor/AEC/AGC |
| Gain 控制 | 0.25x–4.0x 滑块 |

## 系统要求

- Windows 10/11
- Android 设备（已开启 USB 调试）
- [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) 已安装
- ADB 已加入 PATH

## 使用方式

```cmd
# 启动音频桥接
build\audiosource.exe

# 指定设备序列号
build\audiosource.exe --serial <serial>

# 列出音频设备
build\audiosource.exe --list-devices

# 帮助
build\audiosource.exe --help
```

启动后，在 Windows 应用中选择 **CABLE Output** 作为麦克风输入。

## 构建

### Windows 桥接程序

需要 Visual Studio 2022（含 C++ 桌面开发工作负载）。

```cmd
build.bat
```

### VoxMic Source Android App

```cmd
cd android_app
.\gradlew.bat assembleDebug --no-daemon --console=plain
adb -s <serial> install -r app\build\outputs\apk\debug\app-debug.apk
```

## 性能指标

| 指标 | 数值 |
|------|------|
| CPU 空闲 | ~0% |
| CPU 流式传输 | ~0.1-0.3% |
| 内存 | ~10 MB |
| 延迟 | ~400ms |
| 二进制大小 | ~270 KB |

## 技术架构

详见 [ARCHITECTURE.md](ARCHITECTURE.md) 和 [plan_optimize.md](plan_optimize.md)。
