# AGENTS.md — v0.1.1

## 项目概述

将 Android 手机麦克风用作 Windows 系统麦克风，通过 ADB + VB-CABLE + Raw WASAPI 实现。

## 参考

`../audiosource-win` 是原始 Python 参考项目。本项目基于它用 C++ 重写，加入系统托盘、Settings 对话框、事件驱动渲染、Gain 控制、音效开关、延迟优化。如非必要，请勿参考原项目，以免混淆。

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

Gradle wrapper 从 `Punch-in Reminder` 项目复制。

## 运行

```cmd
build\audiosource.exe --serial <serial>
```

在 Windows 应用中选择 **CABLE Output** 作为麦克风。

## v0.1.1 延迟优化参数

| 参数 | 值 | 位置 |
|------|-----|------|
| FRAMES_PER_BLOCK | 512 | `wasapi_output.h:13` |
| WASAPI 缓冲 | 20ms | `wasapi_output.cpp:70` |
| 环形水位 | 5→3 | `main.cpp:87-88` |
| 初始填充 | 3 块 | `main.cpp:246` |
| Android BLOCK_SIZE | 1024 字节 | `RecordThread.java` |

总延迟 ~83ms（已验证 2 分钟零 underrun）。

## 关键技术点

- **输入**: 48000Hz 单声道 int16，通过 ADB TCP 转发从 Android 接收
- **输出**: 48000Hz 立体声 float32/int16，WASAPI 共享模式写入 VB-CABLE
- **重采样**: ratio==1.0 直通，零损失
- **线程**: Socket 接收 → 无锁环形缓冲 (128 块 × 1KB) → WASAPI 事件驱动渲染
- **stats**: SetTimer 定时器，无独立线程
- **配置**: 注册表 `HKCU\Software\AudioSourceWin` 持久化
- **Settings 对话框**: 设备/网络/App/Gain/音效 全可控
- **音效开关**: checkbox → 注册表 → ADB `am start --ez` 传参 → Android App 读取
- **双 App 共存**:
  - `fr.dzx.audiosource` — 原版 gdzx (44100Hz / DEFAULT)
  - `com.voxmic.source` — VoxMic Source (48000Hz / DEFAULT / 音效可开关)
- **延迟控制**: 队列 > 5 块时修剪到 3 块，保持低水位
