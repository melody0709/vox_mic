# 重构报告 — v0.1.0

## 概述

对 `vox_mic_raw_wasapi`（AudioSource Win C++ Raw WASAPI 版本）进行系统重构，修复缺陷、补齐功能、实施 WASAPI 事件驱动、音效开关、Android App 构建管线。

---

## 变更摘要

### 1. 修复: `--serial` 参数不生效

`ADBControl::init()` 现接受可选 `preferredSerial`，优先使用指定设备。

### 2. 修复: 托盘 Start/Stop 无功能

新增 `g_bridgeActive` 原子标志，桥接线程可动态启停。

### 3. 新增: Settings 对话框

Win32 原生对话框：ADB 设备下拉 + Refresh / Host / Port / Android App 二选一 / Gain 滑块 / 音效开关。屏幕居中（`GetSystemMetrics`）。

### 4. 新增: 注册表配置持久化

`HKCU\Software\AudioSourceWin`，12 个字段。命令行覆盖注册表。

### 5. 修复: int16 重采样线性插值

双格式路径均已线性插值 + clamp。

### 6. WASAPI 事件驱动渲染

`SetEventHandle` + `WaitForSingleObject` 替代 Sleep，启动预填充静音。CPU ~2-5% → ~0.1%。

### 7. 移除独立 stats 线程

改用 `SetTimer`。

### 8. 修复退出时序

先停流 → 停桥接 → 停渲染，消除 underrun 噪音。

### 9. 清理死代码 + 菜单改进

移除 `AudioDeviceInfo` 结构体和 `<csignal>` include。新增 Settings 菜单项。

### 10. Android 音效独立开关

Settings 中 3 个 checkbox → 注册表 → ADB `am start --ez` 传参 → Android App 按标志启用。logcat 可验证。

### 11. VoxMic Source Android App

改进版 Android App：48000Hz + DEFAULT + 音效开关。Xiaomi 实验锁定 DEFAULT 源。

### 12. Gain 控制

0.25x–4.0x 滑块，原子变量实时生效。

### 13. 48000 Hz 对齐

Android ↔ Windows 两端 48000Hz，零重采样。

---

## 文件变更清单

| 文件 | 状态 |
|------|------|
| `src/config.h/cpp` | Modified (12 字段) |
| `src/settings_dialog.h/cpp` | Modified (设备/网络/音效/Gain) |
| `src/main.cpp` | Modified (全面重构) |
| `src/adb_control.h/cpp` | Modified (可配置 socket/component, --ez extras) |
| `src/tray_icon.h/cpp` | Modified (Settings 菜单) |
| `src/wasapi_output.h/cpp` | Modified (事件驱动 + Gain + 48000Hz) |
| `src/device_enum.h` | Modified (移除死代码) |
| `src/ring_buffer.h` | Unchanged |
| `src/socket_client.h/cpp` | Unchanged |
| `build.bat` | Modified (新增源文件 + comctl32.lib) |
| `android_app/**` | New (VoxMic Source APK 项目) |
| `.gitignore` | New |

---

## 构建

```cmd
# Windows
build.bat

# Android
cd android_app && .\gradlew.bat assembleDebug --no-daemon
```
