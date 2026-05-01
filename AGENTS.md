# AGENTS.md — v0.1.0

## 项目概述

将 Android 手机麦克风用作 Windows 系统麦克风，通过 ADB + VB-CABLE + Raw WASAPI 实现。

## 参考

`../audiosource-win` 是原始 Python 参考项目。本项目基于它用 C++ 重写，加入系统托盘、Settings 对话框、事件驱动渲染、Gain 控制、音效开关。如非必要，请勿参考原项目，以免混淆。

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

Gradle wrapper（`gradle-wrapper.jar` + `gradlew.bat`）从 `Punch-in Reminder` 项目复制。

## 运行

```cmd
build\audiosource.exe --serial <serial>
```

在 Windows 应用中选择 **CABLE Output** 作为麦克风。

## 关键技术点

- **输入**: 48000Hz 单声道 int16，通过 ADB TCP 转发从 Android 接收
- **输出**: 48000Hz 立体声 float32/int16，通过 WASAPI 共享模式写入 VB-CABLE
- **重采样**: 两端 48000Hz 对齐，`ratio==1.0` 零损失直通；线性插值兼容 44100Hz 旧 App
- **线程**:
  - Socket 接收线程 → 无锁环形缓冲区 (128 块 × 2KB) → WASAPI 事件驱动渲染线程
  - stats 使用 `SetTimer` 定时器，无独立线程
- **延迟控制**: 队列超过 16 块时丢弃最旧块，维持 ~8 块目标水位
- **配置**: 注册表 `HKCU\Software\AudioSourceWin` 持久化所有设置
- **系统托盘**: Start/Stop/Settings/Exit 右键菜单
- **WASAPI**: 事件驱动（`SetEventHandle` + `WaitForSingleObject`），启动预填充缓冲
- **Settings 对话框**:
  - ADB 设备下拉 + Refresh
  - Host / Port 编辑框
  - Android App 二选一 (原版 gdzx / VoxMic Source)
  - Gain 滑块 0.25x–4.0x
  - ☑ NoiseSuppressor / ☑ AEC / ☑ AGC 独立开关
- **音效开关机制**: Settings 勾选 → 注册表 → ADB `am start --ez` 传参 → Android App 读取启用
- **两个 Android App 共存**:
  - `fr.dzx.audiosource` — 原版 gdzx (44100Hz / DEFAULT / 无音效)
  - `com.voxmic.source` — VoxMic Source (48000Hz / DEFAULT / 音效可开关)
