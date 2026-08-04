# Phase 12 附录: Presence 滑块优化 — Q 值收窄 + 范围扩大

## 动机

Presence 滑块调节范围 0–6dB 感觉不够明显。根因不是范围小，而是 Q 值太宽（0.5），boost 分散在 1.2kHz–6.2kHz 宽频段，效果被稀释。

## 方案

| 参数 | 当前 | 修改后 | 效果 |
|------|------|--------|------|
| 主峰 Q (2500Hz) | 0.5 | **1.0** | 带宽 ~5000→~2500Hz，聚焦 1.7–3.7kHz |
| 范围 | 0–6 dB | **0–8 dB** | Q 收窄后可以安全加大增益 |
| 8k 搁架 | 固定 +1.5dB | **presence × 0.25** | 跟随主增益，高 presence 也适度提空气感 |

## 改动文件

| 文件 | 改动 |
|------|------|
| `src/dsp/pipeline.h:47` | `setPeak(2500, presence, 1.0)` Q 0.5→1.0 |
| `src/dsp/pipeline.h:48` | `setHighShelf(8000, 1.5)` → `setHighShelf(8000, presence*0.25f)` |
| `src/settings_dialog.cpp` | TBM_SETRANGE (0,60)→(0,80)；clamping 6.0→8.0；hint 文字更新 |
| `src/config.cpp` | clamping `>6.0f` → `>8.0f` |
