# Raw WASAPI 优化计划 — v0.1.0+

## 目标

将程序改造成 24 小时后台运行的轻量级服务，需要麦克风时才激活。

---

## 当前状态

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 1 | WASAPI 事件驱动渲染 | ✅ 已完成 |
| Phase 2 | 系统托盘 + 设置对话框 + 配置持久化 + Gain 控制 | ✅ 已完成 |
| Phase 3 | 按需激活（麦克风监控） | ⬜ 待实施 |
| Phase 4 | 电源管理 | ⬜ 待实施 |
| Phase 5 | Android 端重构（AudioSource 实验 + 采样率对齐） | ✅ 已收敛 |
| Phase 6 | Windows 端音频后处理管线（Speex + 压缩器 + EQ） | ⬜ 待实施 |
| Phase 7 | WiFi ADB + 断连平滑 + 设备恢复 | ⬜ 待实施 |

---

## Phase 1-2: 已完成（摘要）

### Phase 1: WASAPI 事件驱动渲染

| 指标 | 改前 | 改后 |
|------|------|------|
| CPU（流式） | ~2-5% | ~0.1-0.3% |
| underruns | ~12% | ~1-2% |
| 渲染方式 | Sleep 轮询 | SetEventHandle + WaitForSingleObject |

**关键改动** (`wasapi_output.cpp`):
```cpp
Initialize(..., AUDCLNT_STREAMFLAGS_EVENTCALLBACK, ...);
CreateEventEx(...); SetEventHandle(m_hEvent);
WaitForSingleObject(m_hEvent, 2000);  // 替代 Sleep
```

### Phase 2: 系统托盘 + 设置 + Gain

- 右键菜单: Start Bridge / Stop Bridge / Settings... / Exit
- Settings 对话框: ADB 设备选择 / Host / Port / Android App / Gain 滑块
- 注册表持久化: `HKCU\Software\AudioSourceWin`
- Gain 范围 0.25x–4.0x（默认 1.5x），原子变量实时生效

---

## Phase 5: Android 端重构 — 实验记录与结论

### 背景

与 AudioRelay 对比音质差距明显，研发了改进版 Android App `VoxMic Source`（包名 `com.voxmic.source`，Socket `voxmicsource`），在 Xiaomi 设备 `42f159a4` 上进行了系统的 AudioSource + 采样率组合测试。

### 最终收敛配置（当前生效）

```
AudioSource:          DEFAULT
采样率:               48000 Hz
NoiseSuppressor:      启用
AcousticEchoCanceler: 启用
AutomaticGainControl: 禁用
块大小:               2048 字节 (与 Windows BLOCK_SIZE 对齐)
Socket:               voxmicsource
```

### 实验测试矩阵

| # | AudioSource | 采样率 | 结果 | 结论 |
|---|-------------|--------|------|------|
| 1 | `DEFAULT` (原版) | 44100 | ✅ 正常 | 基准对比 |
| 2 | `VOICE_COMMUNICATION` | 48000 | ❌ 几乎无声 | Xiaomi HAL 窄带路径冲突 |
| 3 | `VOICE_COMMUNICATION` | 44100 | ❌ 音量极低 (~20%) | 同 HAL 问题 |
| 4 | `VOICE_RECOGNITION` | 48000 | ❌ 不正常 | 厂商优化路径不可靠 |
| 5 | `DEFAULT` + NS + AEC | 44100 | ✅ 正常 | 音量 OK，清晰度无明显提升 |
| 6 | `DEFAULT` + NS + AEC | 48000 | ✅ 正常 | **当前配置** |
| 7 | `DEFAULT` + NS + AEC + AGC | 44100 | ❌ 音量极低 | AGC 过度压降 |

### 关键发现

#### Xiaomi 设备 AudioSource 兼容性

```
DEFAULT              ✅ 稳定，所有采样率正常
VOICE_COMMUNICATION  ❌ 48000Hz 几乎无声 / 44100Hz 音量极低
                      — Xiaomi HAL 将 VOICE_COMMUNICATION 固化为窄带通话路径
                      — 非标采样率请求时硬件层做糟糕的内部重采样
VOICE_RECOGNITION    ❌ 行为不可靠
                      — 路径在不同设备上表现不一致

结论: Xiaomi 设备只能用 DEFAULT 源。
      AudioRelay FAQ 也确认 "Confirmed on Xiaomi phones"。
```

#### Android 内置音效效果

```
NoiseSuppressor.isAvailable()     ✅ 返回 true，但效果不可感知
AcousticEchoCanceler.isAvailable() ✅ 返回 true，但效果不可感知
AutomaticGainControl.isAvailable() ❌ 启用后音量被过度压降

结论: Android 内置音效在 Xiaomi 设备上形同虚设。
      提升清晰度必须靠 Windows 端后处理管线。
```

#### 48000 Hz 与重采样消除 ✅

当前两端都是 48000 Hz，`m_resampleRatio == 1.0`，零质量损失。

```
数据链: Android (48000Hz/DEFAULT) → ADB → Windows (48000Hz 直通) → WASAPI
           ↑                                                    ↑
       对齐 2048 字节块                                    无重采样
```

### Phase 5 最终状态矩阵

| 子步骤 | 内容 | 结果 |
|--------|------|------|
| 5A | AudioSource 实验 (VOICE_COMMUNICATION / VOICE_RECOGNITION) | ❌ 均失败，回退 DEFAULT |
| 5B | Android 音效 (NS + AEC + AGC) | ⚠️ NS/AEC 无明显提升，AGC 禁用 |
| 5C | 采样率 48000 + 块对齐 2048 | ✅ 成功，零重采样 |
| 5D | Android App 构建/安装流程 | ✅ 打通，`android_app/` 可独立构建 |
| 5E | Settings 对话框 App 切换 | ✅ 完工，注册表持久化 |
| 5F | Gain 控制（Windows 端） | ✅ 完工，滑块 + 原子变量 |

---

## Phase 6: Windows 端音频后处理管线 — 待实施

### 目标

在 Windows C++ 端引入专业音频后处理，弥补 Android 端 `DEFAULT` 源 + 无效内置音效的短板，达到接近 AudioRelay 的清晰度。

### 核心改造

全部在 `wasapi_output.cpp` 渲染线程中实现，不碰 Android APK。

#### 6A: Speex 高质量重采样器（基础升级）

**来源**: `libspeex/resample.c` — Public Domain，单 C 文件 (~500 行)，可内嵌。

**效果**: 重采样质量从"线性插值"跃升到"行业标准"级别。当前 48000Hz 直通可绕过，但为将来兼容 44100Hz 输入留底。

**工时**: 1 小时

#### 6B: 动态压缩器（"清晰感"最大来源）

**原理**: 
```
小声说话(低电平) → 自动放大     ← 听感更实
大声说话(高电平) → 限幅防爆     ← 不刺耳
背景噪音(阈值下) → 不处理       ← 不放大噪音
```

**实现**: ~50 行 C++ 的简单压缩器，关键参数：

| 参数 | 值 | 含义 |
|------|-----|------|
| threshold | -20 dB | 阈值以下不压缩 |
| ratio | 3:1 | 超出阈值每 3dB 压缩为 1dB |
| makeup_gain | +6 dB | 补偿整体音量 |

**工时**: 2 小时

#### 6C: 语音增强 EQ（锦上添花）

**原理**: 在 2–5kHz 频段轻微提升，增强人声清晰度。

**实现**: 简单的双二阶滤波器 (biquad peaking filter)。

```cpp
// 3kHz, +3dB, Q=1.0 的 peaking EQ
// 只需要 4 个 float 状态变量 + 5 个系数
```

**工时**: 1 小时

### Phase 6 实施顺序

```
6A (Speex) → 独立上线测试
        ↓
6B (压缩器) → 追求"清晰感"质的飞跃
        ↓
6C (EQ) → 微调人声频段
```

每一步独立实施，可单独验证效果。

### 预期效果

| 指标 | 当前 (Phase 5) | +Phase 6 |
|------|----------------|----------|
| 重采样质量 | 线性插值 (直通时无影响) | Speex (兼容 44100 输入) |
| 小声说话 | 听不清 | 压缩器自动提升 |
| 大声说话 | 不爆 | 限幅保护 |
| 人声清晰度 | 低频偏糊 | 2–5kHz 提升 |
| 对比 AudioRelay | 仍有差距 | **显著接近** |

---

## Phase 3-4, 7: 已设计，待实施

### Phase 3: 按需激活（麦克风监控）

- `IAudioSessionNotification` 事件驱动
- 检测到活跃捕获会话 → 自动启动桥接
- 全部 Inactive → 自动停止
- 新增文件: `src/mic_monitor.h/cpp`
- 预计工时: 2h

### Phase 4: 电源管理

- `WM_POWERBROADCAST` / `PBT_APMSUSPEND` 暂停/恢复
- 修改文件: `main.cpp`
- 预计工时: 30min

### Phase 7: 其他优化

| 项目 | 内容 | 预计工时 |
|------|------|----------|
| WiFi ADB | `adb connect <ip>:5555` | 3h |
| 断连平滑 | 重连期间淡入淡出 | 1h |
| 设备恢复 | `IMMNotificationClient` 检测 VB-CABLE 热插拔 | 1h |
| 托盘图标 | 自定义 .ico 三种状态区分 | 1h |
| VU 电平表 | RMS 计算 + 托盘提示 | 1h |

---

## 当前文件结构

```
vox_mic_raw_wasapi/
├── build.bat
├── README.md                   # 项目说明
├── AGENTS.md                   # 开发者速查
├── ARCHITECTURE.md             # 架构说明
├── plan_optimize.md            # 本计划文档

├── src/
│   ├── main.cpp                # 程序入口（托盘窗口、桥接线程、定时器）
│   ├── wasapi_output.h/cpp     # WASAPI 事件驱动渲染 + Gain
│   ├── device_enum.h/cpp       # 设备枚举
│   ├── ring_buffer.h           # 无锁 SPSC 环形缓冲区
│   ├── socket_client.h/cpp     # Winsock2 TCP 客户端
│   ├── adb_control.h/cpp       # ADB 控制（可配置 socket/component）
│   ├── tray_icon.h/cpp         # 系统托盘（右键菜单）
│   ├── config.h/cpp            # 注册表配置持久化
│   └── settings_dialog.h/cpp   # 纯 Win32 设置对话框（含 Gain 滑块）
├── android_app/                # VoxMic Source Android App
│   ├── settings.gradle
│   ├── build.gradle
│   ├── gradle.properties
│   └── app/
│       ├── build.gradle
│       └── src/main/
│           ├── AndroidManifest.xml
│           ├── java/com/voxmic/source/
│           │   ├── App.java
│           │   ├── MainActivity.java
│           │   ├── RecordService.java
│           │   └── RecordThread.java
│           └── res/
└── doc/
    ├── plan_miniaudio.md       # miniaudio 方案参考
    └── plan_portaudio.md       # PortAudio 方案参考
```

---

## 截止当前效果总览

| 指标 | 原始 Python | 初版 C++ | 当前 (Phase 2+5) | 目标 (Phase 6) |
|------|------------|----------|-------------------|----------------|
| CPU 空闲 | ~2-5% | ~2-5% | ~0% | ~0% |
| CPU 流式 | ~2-5% | ~0.5% | ~0.1-0.3% | ~0.1% |
| 内存 | 200+ MB | ~10 MB | ~10 MB | ~10 MB |
| underruns | 高 | ~12% | ~1-2% | <1% |
| 音频源 | DEFAULT | DEFAULT | DEFAULT (无其他可选项) | DEFAULT |
| 采样率 | 44100 | 44100 | 48000 (Android ↔ Windows 对齐) | 48000 |
| 重采样 | 44100→48000 | 44100→48000 线性插值 | 无 (ratio=1.0) | Speex 备选 |
| 音量控制 | 无 | 无 | Gain 0.25x–4.0x 可调 | 保持不变 |
| 动态处理 | 无 | 无 | 无 | 压缩器 (Phase 6) |
| 语音 EQ | 无 | 无 | 无 | 2-5kHz peaking (Phase 6) |
| 交互方式 | 命令行 | 命令行+托盘 | 全功能托盘菜单 | 全功能托盘菜单 |
| Android App 管理 | 无 | 无 | Settings 切换 | 保持不变 |

---

## 经验教训与风险

### Xiaomi 设备适配（已验证）

| 尝试 | 结果 | 避免重试 |
|------|------|----------|
| `VOICE_COMMUNICATION` + 任何采样率 | ❌ | **永远不试** — Xiaomi HAL 固话窄带路径 |
| `VOICE_RECOGNITION` | ❌ | 行为不可靠，不要用 |
| `AutomaticGainControl` 启用 | ❌ | 音量被过度压降 |
| 48000 Hz + `DEFAULT` | ✅ | **唯一稳定配置** |
| Android 内置 NS/AEC | ⚠️ | `isAvailable()`=true 但效果不可感知 |

### 通用风险

| 风险 | 对策 |
|------|------|
| WASAPI 事件丢失 | `WaitForSingleObject(hEvent, 2000)` 2 秒超时 |
| ADB 断连 | bridge 线程外循环自动重连 |
| Xiaomi HAL 不兼容非 DEFAULT 源 | **硬约束**: 锁定 DEFAULT，不浪费实验时间 |
| 压缩器参数不当致失真 | 提供 Gain 滑块 + 预留 bypass 开关 |
| Gain 过高导致削波 | 渲染循环中 float32→int16 已 clamp |
