# Phase 12: NR Strength — 可调降噪强度

## 动机

RNNoise 当前只有 on/off 开关，`denoise.c:480` 的 `alpha = 0.6` 硬编码。用户希望可调降噪强度。

## 方案

暴露 `alpha` 参数为 `nrStrength` (0.3–0.95)，滑块调整。

| 值 | 效果 |
|----|------|
| 0.3 | 柔和降噪，保留自然音质 |
| 0.6 | 默认（等价当前行为） |
| 0.95 | 激进降噪，背景噪声几乎全消除 |

## 改动文件

| # | 文件 | 改动 |
|---|------|------|
| 1 | `src/dsp/rnnoise/rnnoise.h` | 新增 `rnnoise_set_strength(DenoiseState*, float)` |
| 2 | `src/dsp/rnnoise/denoise.c` | `DenoiseState` 加 `strength` 字段；`alpha` 改为从字段读取 |
| 3 | `src/config.h` | 新增 `float nrStrength = 0.6f` |
| 4 | `src/config.cpp` | 新增 `NrStrength` INI 键读写 |
| 5 | `src/dsp/pipeline.h` | 新增 `g_nrStrength` 原子变量 + `updateSettings` 调用 `rnnoise_set_strength` |
| 6 | `src/main.cpp` | `syncDspAtomsFromConfig` 新增 `g_nrStrength` 同步 |
| 7 | `src/settings_dialog.cpp` | 新增 "NR Strength" 滑块 |
| 8 | `AGENTS.md` | 修正"注册表持久化"→"config.ini"；配置字段数 20→21 |
