# Phase 10: 按需激活检测修复 — Sound Recorder 停止后不 idle

## 动机

Win11 Sound Recorder 停止录音后，DSP 不关闭、socket 不断连，无法进入 idle。PTT 输入法正常。

## 根因

### Bug 1：监听目标设备错误

`MicUsageMonitor::init()` 使用 `GetDefaultAudioEndpoint(eCapture, eConsole, ...)` 监听的是**系统默认采集设备**（通常是内置麦克风），而不是 VB-CABLE 的 CABLE Output 采集端点。若用户未将 CABLE Output 设为系统默认麦克风，monitor 完全看不到 CABLE Output 上的 session 变化。

### Bug 2：AudioSessionState 不反映实际录音状态

即使 Bug 1 修复，Sound Recorder (UWP) 停止录音后**不释放 IAudioClient**，session 保持 `AudioSessionStateActive`，`OnStateChanged(Inactive)` 永远不会触发。实测连**关闭** Sound Recorder 后仍不能触发 Inactive。PTT 输入法则每次按键 open/close 设备，session 状态正确转换。

## 方案

### 修改 1：Monitor 靶向 CABLE Output 采集端点

`mic_usage_monitor.cpp:init()` 改为用 `EnumAudioEndpoints(eCapture, ...)` 遍历采集设备，按 friendly name 包含 "CABLE Output" 匹配，替代 `GetDefaultAudioEndpoint`（找不到时 fallback 到默认设备）。

### 修改 2：Render 缓冲积压检测（零额外开销）

`wasapi_output.h/cpp`: render 线程内连续3次 `WaitForSingleObject` 超时（6s）→ `renderStallScore = 3`；event 正常触发 → reset 0。

### 修改 3：IAudioMeterInformation 静音验证（核心兜底）

`mic_usage_monitor.h/cpp`: 在 CABLE Output 采集端点上激活 `IAudioMeterInformation`。`micMonitorThread()` 每 1s 调用 `GetPeakValue()`，若峰值连续3s < 阈值则强制 `g_micRequested = false`，峰值恢复则立即设 true。

| 信号 | 触发方式 | 延迟 | 用途 |
|------|---------|------|------|
| `OnStateChanged(Active/Inactive)` | COM 事件回调 | 即时 | 快速激活/停用（PTT） |
| `renderStallScore >= 3` | render event 超时 | ~6s | 检测无人消费 render 音频 |
| `IAudioMeterInformation` peak | monitor 线程 1s 间隔 | ~3s | 捕获端静音兜底检测 |

### 决策逻辑

`g_micRequested` 由三个信号并行控制：
- **事件驱动 OnStateChanged(Active)**: 瞬间 → true
- **事件驱动 OnStateChanged(Inactive)**: count==0 时 → false
- **Meter 轮询**: peak < 阈值 3s → false（覆盖 Sound Recorder 假阳性）；peak > 阈值 → true（恢复）
- **Bridge 线程**: `effectiveActive = demandOff \|\| (micRequested && !renderStalled)`

## 改动文件

| 文件 | 改动 |
|------|------|
| `src/mic_usage_monitor.h` | 新增 `IAudioMeterInformation* m_pMeter` + `getCapturePeak()` |
| `src/mic_usage_monitor.cpp` | `init()` 枚举 eCapture 找 CABLE Output + 激活 IAudioMeterInformation；`shutdown()` 释放；`getCapturePeak()` 实现 |
| `src/wasapi_output.h` | 新增 `std::atomic<int> renderStallScore` |
| `src/wasapi_output.cpp` | `renderThread()` 追踪 event 超时计数 |
| `src/main.cpp` | `micMonitorThread()` 增加 meter 轮询逻辑；bridge 线程整合 `renderStallScore` |
