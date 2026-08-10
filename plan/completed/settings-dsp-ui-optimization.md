# DSP 设置界面优化方案

## 状态

Implemented。已完成代码修改并通过增量构建验证。

实现文件：

- `src/settings_dialog.cpp`
- `src/main.cpp`

窗口整体高度仍保持 500×565，未调整。

## 范围

本方案针对 `VoxMic - Settings` 的 DSP 页面，重点处理信息层级、控件对齐、功能依赖关系、处理链说明和设置保存交互。

明确非目标：

- 第一阶段不调整窗口整体高度。
- 不新增 DSP 算法。
- 不改变当前 EQ、Compressor、RNNoise、DPDFNet 的核心处理参数。
- 不在本阶段增加复杂的高级参数面板。

## 当前实现要点

实际处理链为：

```text
输入音频 → Noise Reduction → EQ/低切 → Compressor → Limiter → 输出
```

EQ 和 Compressor 是项目内实现的标准 DSP 模块；RNNoise 和 DPDFNet 是降噪后端。三者可以同时启用，不是互斥选项。

EQ 当前还包含固定的 80 Hz 高通滤波器，因此 `EQ Enable` 实际上同时控制音色调整和低频隆隆声过滤。

## 设计目标

1. 让用户理解各功能是串联处理阶段，而不是互相竞争的算法。
2. 让控件之间的父子关系清晰，避免用户修改无效参数。
3. 让 DPDFNet 的“请求后端”和“实际生效后端”状态明确。
4. 在不改变窗口高度的前提下，改善左右空白和底部按钮区域的视觉平衡。
5. 让 `Apply`、`OK`、`Cancel` 具有明确且可预期的行为。

## 界面结构方案

建议按实际处理顺序重排为：

```text
[Noise Reduction]
  Noise Reduction
  Backend
  Status
  NR Strength

[Tone / EQ]
  EQ Enable
  Presence
  Bass Cut / Low Cut

[Dynamics]
  Compressor Enable
  Fixed voice compressor

[Processing chain]
  Noise Reduction → EQ → Compressor → Limiter
```

### Noise Reduction 区域

- 保留 `Noise Reduction` 主开关。
- 主开关关闭时，Backend、状态说明和 NR Strength 全部置灰。
- Backend 下拉框保留 `RNNoise (built-in)` 和 `DPDFNet (48 kHz model)`。
- 状态文字区分“选择的后端”和“实际生效的后端”：
  - `RNNoise active`
  - `DPDFNet active`
  - `DPDFNet unavailable — using RNNoise fallback`
  - `DPDFNet degraded — using RNNoise`
- 绿色只用于表示 Ready/Active 状态；Unavailable、Fallback、Degraded 使用黄色或红色提示色。

### Tone / EQ 区域

- `EQ Enable` 作为 Presence 和 Bass Cut 的父开关。
- EQ 关闭时，两个滑块和数值标签置灰，并保持当前数值不丢失。
- 建议把 `Bass Cut` 的说明改成更准确的表述，例如“减少 250 Hz 以下的低频轰鸣”。
- 由于 EQ 同时包含固定 80 Hz 高通滤波器，建议增加短说明：`Includes fixed 80 Hz low-cut filter`，避免用户误以为关闭 EQ 后低切仍然保留。
- 暂不拆分独立 Low Cut 控件，避免扩大本阶段范围。

### Dynamics 区域

- 保留 `Compressor Enable`。
- 增加简短说明：`Stabilizes voice volume with a fixed voice preset.`
- 当前不增加 Threshold、Ratio、Attack、Release 等高级参数。
- 说明其处理位置在 EQ 之后，帮助用户理解 Presence/Bass Cut 会影响压缩器的输入能量。

### Processing chain 区域

- 使用右侧现有空白区域展示小型处理链状态，避免继续扩大纵向内容。
- 处理链只作为说明，不提供额外操作：
  `NR → EQ → Compressor → Limiter`
- 可在链路下方显示当前状态，例如 `DPDFNet active / EQ on / Compressor on`。
- 如果实现成本过高，第一阶段至少保留一行静态说明。

## 对齐与视觉优化

在保持当前窗口高度不变的前提下：

- 统一标签左边缘。
- 统一所有滑块的起点和终点。
- 统一数值标签的右边缘，避免数值随位数变化产生跳动。
- 将说明文字统一缩进到对应控件下方。
- 使用固定的区域间距和分隔线，避免 Compressor 看起来像孤立控件。
- 利用右侧空白放置处理链/状态摘要；不再单纯增加左侧控件长度。
- 底部按钮区域改为：左侧 `Reset to Defaults`，右侧对齐 `Cancel`、`Apply`、`OK`。
- `OK` 作为默认按钮；`Apply` 在没有未保存修改时置灰。
- 保持当前窗口高度，暂不通过缩短窗口来解决底部空白问题。

## Apply 交互方案

推荐采用“实时预览 + 显式保存”的模式：

1. 打开设置窗口时保存一份原始配置快照。
2. 用户调整滑块或开关时，立即预览 DSP 效果，但标记为未提交修改。
3. `Apply`：保存当前配置，窗口保持打开，清除未保存状态。
4. `OK`：执行 Apply，然后关闭窗口。
5. `Cancel`：恢复打开窗口时的配置快照，同时恢复原来的 DSP 状态，然后关闭窗口。
6. `Reset to Defaults`：只重置当前编辑内容并立即预览，仍然可以通过 Cancel 撤销。

如果后续决定不支持实时预览和 Cancel 回滚，则不应加入 Apply；否则 Apply 会与现有 OK/Cancel 产生歧义。

## 建议的按钮布局

```text
[Reset to Defaults]                         [Cancel] [Apply] [OK]
```

其中：

- Reset 只作用于当前设置草稿。
- Cancel 不保存并恢复快照。
- Apply 保存但不关闭。
- OK 保存并关闭。

## 后续实现范围

预计涉及：

- `src/settings_dialog.cpp`：控件分组、布局、状态文字、父子控件启用状态、按钮行为。
- `src/settings_dialog.h`：如需要增加编辑快照、dirty 状态或 Apply 状态字段。
- `src/config.cpp`：仅在确认配置提交/回滚机制需要时调整，不改变配置字段格式。
- `src/dsp/pipeline.h`：本方案不要求修改 DSP 算法；只有在 UI 状态语义需要同步时才检查接口。

## 验收标准

- 窗口整体高度与当前版本保持一致。
- DSP 页面能清楚表达 `NR → EQ → Compressor → Limiter` 的处理顺序。
- EQ 关闭后 Presence/Bass Cut 不可编辑；降噪关闭后 Backend/NR Strength 不可编辑。
- DPDFNet 不可用或降级时，界面显示实际使用 RNNoise，而不是只显示“已选择 DPDFNet”。
- Apply 保存后窗口不关闭；OK 保存并关闭；Cancel 能恢复打开前状态。
- Reset 后仍可通过 Cancel 恢复原配置。
- 滑块、标签、数值和底部按钮在现有窗口高度下保持一致对齐。
