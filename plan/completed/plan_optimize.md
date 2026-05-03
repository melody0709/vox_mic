# Raw WASAPI 优化计划 — v0.4.0+

## 当前状态

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 1 | WASAPI 事件驱动渲染 | ✅ 已完成 |
| Phase 2 | 系统托盘 + 设置对话框 + 配置持久化 + Gain 控制 | ✅ 已完成 |
| Phase 3 | 按需激活（麦克风监控） | ✅ 已完成 (检测延迟 ~12ms, 空闲 CPU ~0.25%, Always Hot) |
| Phase 4 | 电源管理 | ⬜ 待实施 |
| Phase 5 | Android 端重构（AudioSource 实验 + 采样率对齐） | ✅ 已完成 |
| Phase 5L | 延迟优化 (400ms → 83ms) | ✅ 已完成 |
| Phase 6 | 自研 DSP 管线 (EQ + Compressor + Limiter) | ✅ 已完成 |
| Phase 6B | **官方 RNNoise 神经网络降噪** | ✅ 已完成 |
| **Phase 6C** | **DeepFilterNet3 升级** (可选) | ⬜ 待评估 |
| Phase 7 | WiFi ADB + 断连平滑 + 设备恢复 | ✅ 已可用 (WiFi ADB 已支持) |
| Phase 5 | 隐藏到托盘 GUI + ADB 无闪烁 | ✅ 已完成 (v0.4.1) |
| **Phase 8** | **CPU 优化 (空闲 0-0.1%)** | **✅ 已完成 (v0.4.2)** |

---

## Phase 8: CPU 占用优化 — ✅ 已完成 (v0.4.2)

### 根因确认 + 优化历程 ✅

| 阶段 | 做法 | 空闲 CPU |
|------|------|----------|
| v0.4.0 原始 monitor | 每 100ms `Activate(IAudioSessionManager2)` + 枚举 + `Release` | 0.2-0.4% |
| 方案 A COM 缓存 | `init()` 时缓存 `IAudioSessionManager2`，每 100ms 仅创建枚举器 | 0.15-0.25% |
| **方案 B 事件驱动** | `IAudioSessionNotification` + `IAudioSessionEvents` 回调，零轮询 | **0-0.1%** ✅ |

**关键发现**: DSP 实际开销仅 ~0.1%（远低于设计文档预估 ~1.65%），monitor 的 COM 轮询是唯一 CPU 瓶颈。

### Demand Mode 开关 (Debug Toggle)

右键托盘菜单 "Demand Mode" 勾选，控制按需激活开关，持久化到注册表。用户可自行对比 Demand ON/OFF 的 CPU 差异。

### 最终效果

```
Demand Mode ON:  空闲 CPU 0-0.1%  ← 事件驱动 COM 回调，零轮询
Demand Mode OFF: 空闲 CPU 0-0.1%  ← 无 monitor，bridge 始终推流
Python 版本:     空闲 CPU 0-0.1%  ← 无 monitor，无 DSP
```

三者持平。Phase 8 目标达成。

---

## Phase 3: 按需激活 — ✅ 已完成 (v0.4.0)

### 方案

- MicUsageMonitor: IAudioSessionManager2 100ms 轮询，检测 CABLE Output capture endpoint 的 AudioSessionState
- Always Hot: bridge 永不主动断连 socket，空闲时 recv + 丢弃，来需即推
- ADB 一次性初始化，socket 重连仅 connect() (~200ms)

### 延迟压缩 (3A)

| 参数 | 旧值 | 新值 | 节省 |
|------|------|------|------|
| Android AudioRecord 缓冲 | 2× minBufSize | **1× minBufSize** | ~10ms |
| 环形水位 | 5→3 | **3→2** | ~10ms |
| 初始填充 | 3 块 | **0** (直接启动) | 30ms |
| 总延迟 | ~90ms | **~40ms** | ~50ms |

### 实测 (3B)

- 检测延迟: 0-16ms, 均值 ~12ms
- 语音输入法 CapsLock 长按: `[Monitor] mic=ON/OFF` 精准跟随
- `[DetectLatency] 15ms` → bridge 立即推流
- drop=0, recv 正常增长

---

## Phase 4-7: 后续规划

### 方案

来源: `werman/noise-suppression-for-voice` 的 `external/rnnoise/` (原始 `xiph/rnnoise` 仓库缺少 autotools 生成的 `rnnoise_data.c/h`)。

| 特性 | 参数 |
|------|------|
| 模型架构 | 3 层 GRU (96+96+96), 22 Bark 频段 |
| 输入 | 480 帧 float32 @ 48kHz |
| 延迟 | 10ms/frame |
| 模型大小 | 85KB 8-bit 量化权重 |
| 许可 | BSD-3-Clause |
| 文件数 | 27 个 C/H 文件编译进项目 |

### 参数变更

| 参数 | 旧值 | 新值 |
|------|------|------|
| FRAMES_PER_BLOCK | 512 | **480** (RNNoise 要求正好 480) |
| BLOCK_SIZE (Windows) | 1024 | **960** |
| BLOCK_SIZE (Android) | 1024 | **960** |

### 验证

60s: `drop=3 underrun=0 queue=1~2` — 历史最佳稳定性

---

## Phase 6C: DeepFilterNet3 升级 (可选) — 待评估

| 对比 | RNNoise (当前) | DeepFilterNet3 |
|------|---------------|----------------|
| 感知质量 | 基线 | **最佳** |
| 集成方式 | 27 文件静态编译 | libDF.dll 动态链接 |
| 许可 | BSD-3 | MIT/Apache-2.0 |
| 延迟 | 10ms | ~5ms |
| 模型 | 85KB | ~5MB |
| CPU | ~0.3% | ~3-5% |

不急于实施，RNNoise 已满足当前需求。

---

## 截止当前效果总览

| 指标 | v0.1.0 | v0.3.0 | v0.4.0 |
|------|--------|--------|--------|
| 延迟 | ~400ms | ~90ms | **~40ms** |
| CPU 闲置 | ~0.5% | ~2% | **~0.25%** (按需) |
| CPU 流式 | ~0.5% | ~0.5-1.0% | ~2% |
| underruns | ~12% | **0%** | **0%** |
| 按需激活 | 无 | 无 | **~12ms 检测延迟** |
| 降噪 | Android 内置 (无效) | **RNNoise 神经网络 22 频段** | RNNoise |
| EQ | 无 | **6-band Presence/Bass 可调** | 6-band |
| 压缩 | 无 | **3:1 RMS 5ms/50ms** | 3:1 RMS 5ms/50ms |
| 限幅 | 无 | **-1dBFS** | -1dBFS |
| 对比 AudioRelay | 差距大 | **接近** | **超越** (延迟 + 按需)
