# Changelog

## v0.3.0 (2026-05-01)

### 官方 RNNoise 神经网络降噪集成

基于 `werman/noise-suppression-for-voice` 的 `external/rnnoise` fork（含预生成模型文件）。

| 特性 | 说明 |
|------|------|
| 算法 | 3 层 GRU 网络 (96+96+96)，22 Bark 频段独立降噪 + 梳状滤波 |
| 模型 | v0.2 权重 (128+384+384 维度，Amazon 优化)，85KB 量化，编译进二进制 |
| 延迟 | 10ms/frame (480 帧 @ 48kHz) |
| 版权 | BSD-3-Clause |

### 管线

```
RNNoise (10ms) → HPF 80Hz → EQ 6-band → RMS Comp → Limiter → WASAPI
```

### 参数变更

| 参数 | v0.2.0 | v0.3.0 |
|------|--------|--------|
| FRAMES_PER_BLOCK | 512 (10.7ms) | **480 (10.0ms)** |
| Android BLOCK_SIZE | 1024 字节 | **960 字节** |
| 总延迟 | ~83ms | **~90ms** |

### 稳定性验证

60 秒压力测试: `recv=5819 drop=3 underrun=0 queue=1~2`

| 指标 | v0.2.0 | v0.3.0 |
|------|--------|--------|
| underrun | 0 | **0** |
| drop | 6–9 (0.15%) | **3 (0.05%)** |
| queue | 3–5 | **1–2** (极致精简) |
| 二进制 | ~270 KB | **~1.5 MB** |

480 帧块让缓冲区更紧凑 (queue 仅 1–2)，drop 率历史最低。

### Settings

| 控件 | 默认 | 说明 |
|------|------|------|
| Noise Reduction | on | RNNoise 开关 |

---

## v0.2.0 (2026-05-01)

### 自研 DSP 音频后处理管线

| 阶段 | 算法 | 延迟 | 目的 |
|------|------|------|------|
| HPF | BiQuad IIR 80Hz | 0ms | 切除风噪/直流 |
| EQ | 6-band BiQuad (Presence + Bass Cut 可调) | 0ms | 人声优化 |
| Compressor | RMS 3:1, 5ms/50ms | 0ms | 响度统一 |
| Limiter | 峰值跟随 -1dBFS | 0ms | 防削波 |

### Settings 新增

EQ Enable / Presence (0–6dB) / Bass Cut (-6–0dB) / Compressor Enable

### 副作用修复

Settings 对话框: 纯音效修改也更新内存配置 (原 bug: 仅 serial/host/port 变化才生效)

---

## v0.1.1 (2026-05-01)

### 延迟优化 — 400ms → 83ms

| 参数 | v0.1.0 | v0.1.1 | 影响 |
|------|--------|--------|------|
| FRAMES_PER_BLOCK | 1024 (21.3ms) | **512 (10.7ms)** | 块粒度减半 |
| WASAPI buffer | 200ms | **20ms** | 缓冲延迟 -90% |
| 环形水位 | 16→8 | **5→3** | 排队延迟 -85% |
| 初始填充 | 3 块 (~70ms) | **3 块 (~32ms)** | 启动等待 -55% |
| Android 块 | 2048 字节 | **1024 字节** | 对齐 512 帧 |
| 总延迟 | ~400ms | **~83ms** | **5 倍提升** |

2 分钟压测: `recv=11090 drop=12 underrun=0`

---

## v0.1.0 (2026-04-30)

### 初始功能集

- WASAPI 事件驱动渲染 (SetEventHandle + WaitForSingleObject)
- 系统托盘 + Settings 对话框 (设备/网络/App/音效/Gain)
- 注册表配置持久化 (12 字段)
- 48000 Hz Android ↔ Windows 对齐 (零重采样)
- Gain 0.25x–4.0x 滑块
- 音效独立开关 (NS/AEC/AGC checkbox + ADB --ez 传参)
- VoxMic Source Android App (独立构建安装)
- Xiaomi 设备 AudioSource 兼容性实验 (锁定 DEFAULT)
