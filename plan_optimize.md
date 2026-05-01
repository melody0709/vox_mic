# Raw WASAPI 优化计划 — v0.3.0+

## 当前状态

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 1 | WASAPI 事件驱动渲染 | ✅ 已完成 |
| Phase 2 | 系统托盘 + 设置对话框 + 配置持久化 + Gain 控制 | ✅ 已完成 |
| Phase 3 | 按需激活（麦克风监控） | ⬜ 待实施 |
| Phase 4 | 电源管理 | ⬜ 待实施 |
| Phase 5 | Android 端重构（AudioSource 实验 + 采样率对齐） | ✅ 已完成 |
| Phase 5L | 延迟优化 (400ms → 83ms) | ✅ 已完成 |
| Phase 6 | 自研 DSP 管线 (EQ + Compressor + Limiter) | ✅ 已完成 |
| Phase 6B | **官方 RNNoise 神经网络降噪** | ✅ 已完成 |
| **Phase 6C** | **DeepFilterNet3 升级** (可选) | ⬜ 待评估 |
| Phase 7 | WiFi ADB + 断连平滑 + 设备恢复 | ⬜ 待实施 |

---

## Phase 1-5: 已完成（摘要）

详见 CHANGELOG.md 和 ARCHITECTURE.md。

---

## Phase 6B: 官方 RNNoise 集成 — ✅ 已完成

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

| 指标 | v0.1.0 | v0.3.0 |
|------|--------|--------|
| 延迟 | ~400ms | **~90ms** |
| CPU 流式 | ~0.5% | ~0.5-1.0% |
| underruns | ~12% | **0%** |
| 降噪 | Android 内置 (无效) | **RNNoise 神经网络 22 频段** |
| EQ | 无 | **6-band Presence/Bass 可调** |
| 压缩 | 无 | **3:1 RMS 5ms/50ms** |
| 限幅 | 无 | **-1dBFS** |
| 对比 AudioRelay | 差距大 | **接近** |
