# 设置界面规则化 + 浅色主题落地方案

> 状态：**S1–S5 已完整收口与深度优化（2026-09-17）** · 决策 D1=A、D2=A、D3=A 均已落地
> 验证证据：`settings_tab_order_test` 自动化遍历测试 100% PASS（验证两页共 28 步严格焦点拓扑）、`mic_session_state_test` 全过、构建 0 error / 0 warning、架构守卫 PASS（extern 0/0, u8 0/0, thread 3/3）、DPDFNet 4 个 smoke 全过
> **遗留收口与自审修复**：
> 1. **Tab 遍历顺序规范化**：解耦页脚按钮创建时序，改为由 `createFooter` 在页面字段创建后构建，消除 Tab 键由标签头直跳页脚的问题，形成 Tab 控件 -> 各字段顺序 -> Reset -> Cancel -> Apply -> OK -> Tab 的严格单向环形遍历。
> 2. **几何双倍内边距修复**：修正 `ColFieldX = ColLabelW + GapLabelField`，消除叠加多余 `PadPageX` 导致控件与标签间距达 26px 及 Refresh 按钮冲出右侧边框的问题。
> 3. **行内复选框灰色残留与溢出修复**：修正 `sameRowAsPrevious` 为 `TextRole::Opaque` 并细分列宽，彻底消除 AcousticEchoCanceler 的 `#F0F0F0` 系统灰底与右缘溢出。
> 4. **动态字段命令分发与键盘确认**：`WM_COMMAND` 改为动态字段表匹配，支持 `IDOK` 回车确认，新增字段零代码侵入即可获得脏标记与预览联动。
> 5. **实时增益（Gain）预览与禁用提示色绑定**：增益滑动条纳入音频实时预览与取消回滚链路；`WM_CTLCOLORSTATIC` 确保禁用状态下 Hint 文字正确呈现禁用语义色。
> 上游分析（对标证据）：`.plan/refactor/settings-ui-benchmark-vs-voxtype.html`
> 标注规则：`[已核实]` = 读过源码/二进制/全历史确认；`[推断]` = 待实测确认
> 本方案**不复制 VoxType 的外观**。VoxType 只证明"这三件事在 Win32 原生控件上够用"。

---

## 0. 结论

三件事，按性价比排序：

| 顺序 | 项 | 为什么排这里 |
|---|---|---|
| 1 | **补 manifest**（comctl32 v6 + PerMonitorV2） | 控制点最少（1 个新文件 + 2 处引用），收益最大：一次把 Tab / 滑杆 / 下拉框从 Win95 外观切成 Win10 原生，并顺带修掉一直缺失的 DPI 能力 |
| 2 | **布局规则化** | 本次的**核心**。把"每个控件手填坐标"换成"字段表 + 布局器"，这是唯一能真正解决"以后新增选项又乱"的动作 |
| 3 | **浅色主题 token** | 自研浅色板，由 token 统一驱动。放在最后是因为它风险最低（纯替换），且**规则化之后才替换得干净** |

配色已定：**浅色**。且**不是 VoxType 那张色板**——按本项目已有的设计语言（中性基色 + 青绿强调 + 4px 网格 + 11–17px 字号 + 状态色绑定引擎状态）翻成浅色版本，见 §3。

---

## 1. 为什么必须"规则化"，而不是"再修一遍坐标"

现状 `settings_dialog.cpp` 1458 行 `[已核实]`，其中大头是 `WM_CREATE` 里**逐个控件的 `CreateWindowEx` 手填 x/y/w/h**。

具体症状：

| 症状 | 证据 |
|---|---|
| 同一窗口内 **4 种控件高度混排** | 输入框 21（`:882`）/ 标签 22 / 滑杆 24（`:921`）/ 按钮 25（`:871`） |
| **两页视觉语言不同** | General 用"小节标题 + `SS_ETCHEDHORZ` 蚀刻线"（`:852-855`）；DSP 用 `BS_GROUPBOX`（`:972-978`） |
| 依赖态靠手写 setter | `updateDspControlStates` + `updateProcessingChainUi` + `updateDenoiseBackendUi`，**三个函数合计约 146 行**（`:211-390`） |
| 加载/保存四处逐项手写 | `loadDspUiFromConfig`(`:401-447`) / `loadGeneralUiFromConfig`(`:447-474`) / `saveDspUiToConfig`(`:480-512`) / `saveUiToConfig`(`:512-563`) |
| 无 DPI 缩放、无重排 | 全是字面量像素；无 `WM_SIZE` 处理 |

**这就是"新增即混乱"的机制原因**：现在加一个设置项 = 改 **4 处**（坐标 + 字体 + 加载 + 保存），任何一处漏掉就是一个 bug，且坐标靠人眼对齐。

**规则化的目标口径**：新增一个设置项 = **只改 1 处**（字段表加一条记录）。

---

## 2. 设计：三层结构

```
① metrics        栅格常量，唯一来源，按 DPI 缩放
                 ↓
② field spec     字段声明：只描述"是什么"，不描述"在哪"
                 ↓
③ layout pass    纯计算 → rect；再由 widget factory 统一创建控件
```

### 2.1 metrics — 允许出现的全部尺寸

设计原则：**除本表外，代码里不得再出现表示尺寸/位置的像素字面量。**

| 类别 | 常量 | 值（逻辑 px） | 说明 |
|---|---|---|---|
| 边距 | `PadWinX` / `PadWinY` | 16 / 12 | 内容区到窗口 |
| 分组 | `SectionTitleH` / `SectionGap` | 20 / 20 | 小节标题行高 / 小节之间 |
| 行 | `RowH` | 28 | 标签 + 单行控件（含行间距） |
| 行 | `HintH` | 16 | 说明文字行 |
| 行 | `LabelW` / `GapLabelCtrl` | 96 / 8 | 左列标签宽 / 标签与控件间距 |
| 控件高 | `CtrlH` | 24 | **所有**输入类控件统一高度 |
| 控件宽 | `WNarrow` / `WMedium` / `WWide` | 72 / 150 / 220 | 数字 / 短列表 / 文本与长列表 |
| 滑杆 | `WSlider` / `WValue` / `GapValue` | 180 / 48 / 8 | 滑杆宽 / 右侧数值宽 / 间距 |
| 尾部槽 | `GapTrail` / `WTrailBtn` | 8 / 76 | 行内动作按钮（Refresh / Browse） |
| 页脚 | `FooterH` / `FooterBtnW` | 56 / 76 | |
| 字号 | `FontBody` / `FontHint` / `FontSection` | 13 / 12 / 13 (Semibold) | 单位 px，按 DPI 换算成 `-MulDiv` |
| 圆角/描边 | `RadiusCtrl` / `StrokeHair` | 4 / 1 | |

**行高只允许 3 档**（消除"一行一个高度"）：

| 档 | 组成 | 用在 |
|---|---|---|
| `RowSingle` | `RowH` | 标签 + 控件，无说明 |
| `RowWithHint` | `RowH + HintH` | 标签 + 控件 + 一行说明 |
| `RowTall` | `RowH + n*HintH` | 说明需要多行（当前仅"后端状态"需要 2 行）|

**控件高度只允许 2 档**：`CtrlH`（全部交互控件）与 `CtrlH` 的行内变体（同高，仅宽度不同）。
→ 现在 21/22/24/25 四值混排的问题从根上消失。

### 2.2 field spec — 只描述"是什么"

```cpp
enum class FieldKind {
    Text,     // 单行文本
    Number,   // 数字（ES_NUMBER + 范围校验）
    Choice,   // 下拉选择
    Toggle,   // 复选框
    Slider,   // 滑杆 + 右侧数值
    Status,   // 只读状态文本（可带语义色）
    Note,     // 纯说明行（不绑定配置）
};

enum class FieldState { Live, Disabled, Hidden };  // 依赖不满足时的表现

struct FieldSpec {
    std::string_view id;          // 绑定键，与 config.ini 的键同名
    std::string_view label;       // 左列标签
    FieldKind        kind;
    std::string_view hint;        // 控件下方说明，可空
    std::string_view dependsOn;   // 依赖的布尔字段 id，可空
    FieldState       whenUnmet;   // 依赖不满足时：禁用 还是 隐藏
    int              widthKind;   // WNarrow / WMedium / WWide / 自动
};
```

分组与页面同样是声明：

```cpp
struct SectionSpec { std::string_view title; std::span<const FieldSpec> fields; };
struct TabSpec     { std::string_view title; std::span<const SectionSpec> sections; };
```

### 2.3 layout pass — 唯一一处算坐标

- **横向**：`xLabel = PadWinX`；`xCtrl = xLabel + LabelW + GapLabelCtrl`；`xTrail = xCtrl + wCtrl + GapTrail`。
  `wCtrl` 由 `FieldKind` + `widthKind` 决定，不由调用方决定。
- **纵向**：游标 `y` 从 `PadWinY` 起。
  - section：`y += SectionTitleH`（画标题 + 细分隔线）`+ GapLabelCtrl`
  - 每个字段：`y += RowH`；若 `hint` 非空再 `y += HintH`
  - section 之间：`y += SectionGap`
- 布局器输出 `std::vector<FieldWidget>`，内含 `id → HWND` 映射，供状态刷新与取值使用。

**由此得到的三个硬保证**：
1. 字段按声明顺序顺推，**不可能重叠或错位**；
2. 新增字段只影响它后面的 y，上面不动；
3. 同一个 `FieldKind` 在任何页面上长得完全一样。

### 2.4 依赖态：从"手写 setter"变成"统一求值"

现在是三个函数、约 146 行手写 `setControlEnabledIfChanged`。改为：

```cpp
// 唯一入口：读一次状态快照，遍历字段表求值
void applyFieldStates(const std::vector<FieldWidget>&, const StateSnapshot&);
```

`dependsOn` 指向一个布尔设置项；求值结果 `Live / Disabled / Hidden` 由字段表声明。
**新增的"X 关掉时 Y 失效"关系 = 字段表里写一个名字**，不再新增代码分支。

> 现状对照：`NR 关 → 后端/强度/状态失效`、`EQ 关 → Presence/Bass 失效`、
> `后端=DPDFNet 且未就绪 → 强度失效`，这三条现在散在 `:211-390`；规则化后是字段表里的 3 个 `dependsOn`。

### 2.5 新增一个设置项的成本（这是本方案要买的东西）

| 步骤 | 现在 | 规则化后 |
|---|---|---|
| 声明位置 | 手填 x/y/w/h + 选字体 | 字段表加 1 条 |
| 新建控件 | 手写 `CreateWindowEx` + 选样式 | 布局器按 `kind` 自动建 |
| 依赖态 | 改 1~2 个状态函数 | `dependsOn` 写个名字 |
| 加载 | `load*FromConfig` 加一段 | 由 `id` 自动与 `Config` 字段对齐 |
| 保存 | `saveToConfig` 加一段 | 同上 |
| **合计** | **约 4 处、需人眼对齐** | **1 处声明** |

---

## 3. 浅色主题 token（自研，不照抄 VoxType）

沿用本项目已定的设计语言结构（4px 基准网格 / 11–17px 字号阶梯 / 4-8-14px 圆角阶梯 /
状态色绑定引擎真实状态），**只把基色从深色翻成浅色**。

> 上一版为 Slint 界面设计的深色基色（`#131417` / `#1b1d21` / `#14b8a6`）**作废，不再保留深色分支**。
> 保留的是它的**结构**（网格、字号、圆角、状态语义），不是它的色值。

| 类别 | token | 值 | 深色版（作废，仅备注对应关系） |
|---|---|---|---|
| 窗口底 | `BgWindow` | `#F4F5F7` | ~~#131417~~ |
| 面板 / 输入 | `BgPanel` | `#FFFFFF` | ~~#1b1d21~~ |
| 嵌套区 / 表头 | `BgSubtle` | `#ECEEF1` | ~~#22252a~~ |
| 描边 / 分隔 | `Stroke` | `#D8DCE1` | ~~#2f3339~~ |
| 焦点环 | `FocusRing` | `#9AA3AE` | ~~#3d434b~~ |
| 主色 | `Accent` / hover / pressed | `#0F766E` / `#0D9488` / `#115E59` | ~~#14b8a6~~（浅底上对比不足，压深一档） |
| 主色浅底 | `AccentSoft` | `#E4F1EF` | — |
| 主文字 | `TextPrimary` | `#1B1F24` | ~~#e8eaed~~ |
| 次文字 | `TextSecondary` | `#4A5560` | ~~#b9bec6~~ |
| 辅助 / 提示 | `TextHint` | `#78828D` | ~~#8b919a~~ |
| 禁用 | `TextDisabled` | `#A4ACB5` | ~~#5f656e~~ |
| 字体 | `FontFamily` | `Segoe UI`（见 §6 D2） | ~~Segoe UI Variable Text~~ |

**状态色 = 引擎真实状态**（与三态托盘图标对齐）。浅底上需要压深才够对比度：

| 状态 | 浅色版 | 深色版（作废） | 含义 |
|---|---|---|---|
| idle | `#6B7280` | ~~#6b7280~~ | 托盘常驻、未推流 |
| armed | `#0284C7` | ~~#38bdf8~~ | 链路已建立、尚无消费方 |
| streaming | `#0F766E` | ~~#14b8a6~~ | 与主色一致 = 信号在流动 |
| degraded | `#B45309` | ~~#f59e0b~~ | DPDFNet 不可用 / 空块降级 |
| fault | `#DC2626` | ~~#ef4444~~ | socket 断开、设备丢失 |

落地方式：一张 `UiToken` 常量表 + 一组 `HFONT`/`HBRUSH` 缓存（照现有 `pData->hHintFont` 的
缓存方式扩展），`WM_CTLCOLOR*` / `WM_PAINT` 全部只从 token 取色。
**不得再出现 `COLOR_BTNFACE` 之类的系统色**——否则浅色板会被系统灰污染。

---

## 4. 落地步骤

| 步骤 | 内容 | 退出条件 |
|---|---|---|
| **S1** | **补 manifest**：新增 `src/app.manifest`（PerMonitorV2 + Common-Controls v6 + supportedOS），`voxmic.rc` 加 `1 RT_MANIFEST`，CMake 加 `/MANIFEST:NO` | 改前/改后截图：Tab、滑杆、下拉框从经典外观变原生；滑杆无黑条 |
| **S2** | **DPI 缩放**：`DpiScaleForWindow()` + `S()` + metrics 表；补 `WM_DPICHANGED` | 100/125/150% 三档截图文字锐利 |
| **S3** | **布局器 + 字段表**：`settings_layout.*` + `settings_fields.h`；迁移全部 20 个配置项 | 功能对齐现有配置项；`settings_dialog.cpp` 行数回落 |
| **S4** | **浅色 token**：替换全部系统色与硬编码色 | 无 `COLOR_*` 系统色残留；截图确认 |
| **S5** | **键盘可达 + 页脚**：`WS_TABSTOP` / `WS_GROUP` / `WS_EX_CONTROLPARENT` + `IsDialogMessage`；页脚分隔线与状态位 | Tab 键可遍历全部字段 |

S1 与 S2 共用同一份 manifest，**必须一起做**（分两次 = 改两遍文件、测两遍）。

### 文件落点

| 文件 | 职责 | 备注 |
|---|---|---|
| `src/app.manifest` | 新增 | |
| `src/settings_layout.h/.cpp` | metrics + 布局器 + widget factory | 新增；纯 Win32，不进 core |
| `src/settings_fields.h` | 字段表（纯数据） | 新增；**不 include windows.h** |
| `src/settings_dialog.cpp` | 收缩为：窗口过程 + 提交/回滚 + 状态刷新 | 1458 → 目标 ≤ 900 |

---

## 5. 对架构守卫的影响 `[已核实]`

读了 `scripts/check_architecture.ps1`，本方案的约束与后果：

| 守卫项 | 现状 | 本方案的影响 |
|---|---|---|
| 行数 ratchet | `settings_dialog.cpp` 1458 | **只降不升**。拆分后应回落，符合守卫 FAIL 信息里的"UI refactor should shrink this" |
| 新增文件 | `$LineBaselines` 是白名单，新文件不受管 | **需要给 3 个新文件补基线条目**，否则新文件成为无人看守的膨胀点 |
| `extern` 总数 | 0 / 0（零余量） | 新文件**不得引入任何 `extern`**；字段表用 `inline constexpr` 数组 |
| 裸 `std::thread` | 3 / 3 | 本次不涉及线程 |
| C++23 红线 | `/utf-8`、`NOMINMAX`、无 `u8""` | 新文件天然满足 |

> ⚠️ 守卫的 `$Sources` 是全 `src/**` 扫描，所以新增文件**会**被计入 extern / u8 / thread 统计——
> 必须保持 extern 为 0。

---

### 实施时修正（与原文档的差异，均已验证）

1. **提交边界改为 4 次**（原计划的 S1+S2/S3/S4+S5）：S1（manifest）与 S2/S3（DPI+规则化）
   合并为一次提交 `85342aa`。原因：启用 DPI 感知但不缩放布局，在任何非 100% 屏上
   比改之前更糟（字体随系统放大、盒子不放大），实测截图证实了这一点，两者不能拆开。
2. **新增 `src/settings_theme.h`**（计划未预见）：浅色 token 独立成表，由
   `settings_layout` 持有 window/panel/stroke 三把刷子的缓存。
3. **窗口会自动增高**：布局器按最高页的内容高度把窗口撑到刚好容纳（不滚动，
   符合 D1=A），本机 150% 下从 620 设计高增至约 653。
4. **页脚按钮改"从客户区底锚定"**：原写法把窗口高度当客户区高度，按钮画在客户区外。
5. **复选框必须 Opaque**：带视觉样式的 BUTTON 忽略 `NULL_BRUSH`（实测底带
   `#F0F0F0`），改成面板刷后 `#FFFFFF` 一致。

## 6. 待用户决策

> 2026-09-17 已定案：D1=A、D2=A、D3=A，均按推荐执行。以下保留原始选项备查。

### D1　窗口尺寸与溢出策略

规则化后内容不再被挤压，但窗口现为 **500×565**（Tab 区 465×465），按新 metrics 计算
General 页需要约 **640×600**。三条路：

| 选项 | 说明 | 代价 |
|---|---|---|
| **A（推荐）** | 窗口放大到约 **640×600**，两页都不滚动 | 窗口变大 |
| B | 保持 500×565，内容超出时**纵向滚动** | 需要滚动条 + 滚动时重排坐标 |
| C | 可自由 resize + 滚动 | 引入响应式列宽/换行复杂度，**不建议** |

**推荐 A**：本方案的所有收益都建立在"行有统一尺寸"上，滚动会把"可见区域计算"重新引入布局器，
抵消一部分规则化收益。若担心以后项数继续增长，可先用 A，等真的超了再补滚动（布局器已能算出内容总高，
补滚动是增量动作，不是返工）。

### D2　字体栈

| 选项 | 说明 |
|---|---|
| **A（推荐）** | 统一 `Segoe UI`：Win10 / Win11 都在，无回退分支。VoxType 用的也是它 |
| B | 优先 `Segoe UI Variable Text`（Win11），Win10 回退 `Segoe UI` |

**推荐 A**：本项目字号区间是 12–13px，"Variable" 的视觉差别在这一档几乎不可见，
却引入一个需要实测的回退分支。

### D3　执行范围与提交粒度

| 选项 | 说明 |
|---|---|
| **A（推荐）** | 分 3 次提交：`S1+S2`（manifest+DPI）→ `S3`（规则化）→ `S4+S5`（token+可达）。每步独立截图验证，任一步可回退 |
| B | 一次性做完再验证 | 出问题时难以定位是哪一层引起的 |

**推荐 A**：S1/S2 立刻能看到观感跳变且风险独立；S3 是纯结构重构（应做到**零视觉变化**，
即布局器摆出来的位置与现在肉眼一致），把结构和配色分开验证，出问题时能立刻分辨是布局错还是取色错。

---

## 7. 不做的事（明确排除）

| 不做 | 理由 |
|---|---|
| 引入 Slint / 任何 UI 框架 | 已放弃（B4/B5 存于 `slint-ui-experiment` 分支）。VoxType 证明原生控件 + 这三层就够 |
| 抄 VoxType 的 3448 行单文件结构 | 它的 `SettingsWndProc` 单 switch 跨 1636 行，比我们现在更糟 |
| 抄 VoxType 的 18 个全局 `std::vector<HWND>` | 我们 B3 刚把 `extern` 从 30 清到 0，抄回去是架构倒退 |
| 深色主题 / 跟随系统深色 | 已定浅色。做主题切换在 Win32 GDI 上成本不低，不在本次范围 |
| 保留深色 token 作为"备选" | 按约定作废就是作废，不留注释性残留 |
| 改 20 个配置项的键名或 ini 格式 | 规则化的第一原则是行为不变，兼容性不冒险 |

---

## 8. 顺手要修的文档缺陷

`.plan/refactor/cxx23-slint-refactor-plan.md` 的状态与事实不一致 `[已核实]`：

- 仍把 B4「接入 Slint」、B5「主题 + 迁移配置项」写成待做；
- 附录整段保留着为 Slint 写的深色 token 与 `.slint` 相关验证步骤；
- 该文件最后一次提交是 `0bc5b32`（B3），放弃 Slint 的决定**没有回写**。

**建议**：把该文件标记为已被本方案取代并移入 `.plan/completed/`，或就地记录"视觉规范结构沿用、
Slint 实现作废"。二选一，不要两处并存互相矛盾。
