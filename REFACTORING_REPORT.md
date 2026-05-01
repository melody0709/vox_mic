# 重构报告 — v0.3.0

## v0.1.0

### 1-13. 基础功能 + Android 端

- ✅ WASAPI 事件驱动渲染
- ✅ 系统托盘 + Settings 对话框 + 注册表持久化 (12 字段)
- ✅ Gain 控制 + 音效独立开关
- ✅ VoxMic Source Android App (48000Hz / DEFAULT)
- ✅ 48000 Hz 零重采样对齐

---

## v0.1.1

### 14. 延迟优化 — 400ms → 83ms

| 参数 | v0.1.0 | v0.1.1 |
|------|--------|--------|
| FRAMES_PER_BLOCK | 1024 | **512** |
| WASAPI buffer | 200ms | **20ms** |
| 环形水位 | 16→8 | **5→3** |
| Android 块 | 2048 | **1024** |

验证: 2 分钟压测 `recv=11090 underrun=0`

---

## v0.2.0

### 15. 自研 DSP 后处理管线

| 阶段 | 算法 | 文件 | 行数 |
|------|------|------|------|
| HPF | BiQuad IIR 80Hz | `dsp/biquad.h` | |
| EQ | 6-band (Presence + Bass Cut) | `dsp/biquad.h` | |
| Compressor | RMS 3:1 | `dsp/pipeline.h` | |
| Limiter | Peak follower -1dBFS | `dsp/pipeline.h` | |

配置 12→16 字段 (EQ/Comp 控件)
验证: 2×60s 压测 `drop=6 underrun=0`

---

## v0.3.0

### 16. 官方 RNNoise 神经网络降噪集成

| 项目 | 说明 |
|------|------|
| 来源 | `werman/noise-suppression-for-voice` 的 `external/rnnoise/` |
| 模型 | v0.2 权重 (128+384+384 GRU, Amazon 优化) |
| 文件 | 27 个 C/H 文件 → `src/dsp/rnnoise/` |
| 许可 | BSD-3-Clause |
| 精度 | 22 Bark 频段, 85KB 8-bit 量化权重 |

### 参数变更

| 参数 | v0.2.0 | v0.3.0 |
|------|--------|--------|
| FRAMES_PER_BLOCK | 512 | **480** |
| BLOCK_SIZE | 1024 | **960** |
| Android BLOCK_SIZE | 1024 | **960** |
| 总延迟 | ~83ms | **~90ms** |

### 管线

```
RNNoise (10ms) → HPF 80Hz → EQ 6-band → RMS Comp (3:1) → Limiter (-1dBFS)
```

### 验证

60s 压测: `recv=5819 drop=3 underrun=0 queue=1~2`
- 历史最佳: drop 0.05%, queue 极致精简

---

## 文件变更清单 (v0.3.0 最终)

| 文件 | 状态 |
|------|------|
| `src/dsp/rnnoise/*` (27 files) | **New** — 官方 RNNoise v0.2 源码 |
| `src/dsp/noise_gate.h` | **Deleted** — 替换为 RNNoise |
| `src/dsp/biquad.h` | Modified — 移除 bandpass/LPF (自研 NR 专用) |
| `src/dsp/pipeline.h` | Modified — RNNoise 作为 stage 0 |
| `src/wasapi_output.h` | Modified — FRAMES_PER_BLOCK 480, 移除 DspPipeline member |
| `src/wasapi_output.cpp` | Modified — DspPipeline 局部变量, 包含 rnnoise.h |
| `src/config.h/cpp` | Modified — nrEnabled (17 字段) |
| `src/main.cpp` | Modified — g_nrEnabled atomic |
| `src/settings_dialog.cpp` | Modified — NR checkbox |
| `build.bat` | Modified — +10 rnnoise .c + /I include |
| `android_app/.../RecordThread.java` | Modified — BLOCK_SIZE 960 |
| `README.md` | v0.3.0 |
| `ARCHITECTURE.md` | RNNoise pipeline diagram |
| `AGENTS.md` | v0.3.0 params, rnnoise build notes |
| `CHANGELOG.md` | +v0.3.0 entry |
| `plan_optimize.md` | Phase 6B done |
| `FUTURE_ROADMAP.md` | Phase 6B done, Phase 6C planned |
| `REFACTORING_REPORT.md` | +v0.3.0 section |
