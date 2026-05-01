# 重构报告 — v0.1.1

## v0.1.0

### 1-9. 功能修复与新增

- ✅ `--serial` 参数修复
- ✅ 托盘 Start/Stop 功能
- ✅ Settings 对话框 (Win32 原生)
- ✅ 注册表配置持久化 (12 字段)
- ✅ int16 线性插值修复
- ✅ WASAPI 事件驱动渲染
- ✅ stats 改用 SetTimer
- ✅ 退出时序修复
- ✅ 死代码清理 + 菜单改进

### 10-13. Android 端

- ✅ Android 音效独立开关 (checkbox + ADB --ez)
- ✅ VoxMic Source Android App (48000Hz / DEFAULT + 音效)
- ✅ Gain 控制 (0.25x–4.0x 滑块)
- ✅ 48000 Hz 零重采样对齐

---

## v0.1.1

### 14. 延迟优化 — 400ms → 83ms

| 参数 | v0.1.0 | v0.1.1 | 改动文件 |
|------|--------|--------|----------|
| FRAMES_PER_BLOCK | 1024 | **512** | `wasapi_output.h` |
| WASAPI buffer | 200ms | **20ms** | `wasapi_output.cpp` |
| 环形水位 | 16→8 | **5→3** | `main.cpp` |
| 初始填充 | 3 块 | **3 块** (块变小所以等待减半) | `main.cpp` |
| Android 块大小 | 2048 字节 | **1024 字节** | `RecordThread.java` |

**验证**: 2 分钟连续压测 `recv=11090 underrun=0`。

**测试历史**:
- 256 帧/块 + 10ms WASAPI → 失败 (ADB 抖动)
- 512 帧/块 + 10ms WASAPI + 4→2 水位 → 队列不稳定
- 512 帧/块 + 20ms WASAPI + 5→3 水位 → **收敛**

---

## 文件变更清单 (v0.1.1 最终)

| 文件 | 状态 |
|------|------|
| `src/wasapi_output.h` | Modified (FRAMES_PER_BLOCK 512) |
| `src/wasapi_output.cpp` | Modified (WASAPI 20ms buffer) |
| `src/main.cpp` | Modified (5→3 水位 + 3 块初始填充) |
| `src/config.h/cpp` | Modified (12 字段) |
| `src/settings_dialog.h/cpp` | Modified |
| `src/adb_control.h/cpp` | Modified |
| `src/tray_icon.h/cpp` | Modified |
| `src/device_enum.h` | Modified |
| `build.bat` | Modified |
| `android_app/.../RecordThread.java` | Modified (1024 字节块) |
| `.gitignore` | New |
| `CHANGELOG.md` | New |
