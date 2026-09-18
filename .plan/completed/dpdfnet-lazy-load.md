# DPDFNet 按需加载 / 释放

**目标**：没选 DPDFNet 就不加载它；选了才加载；切走能释放。当前是**无条件加载**，实测白付约 43MB。

---

## 1. 问题（实测，非估算）

`DspPipeline::init()` → `pipeline.h:41` **无条件**调用 `m_dpdfnet.prepare(...)`，不看 `g_appState.denoiseBackend`。

用户配置 `%LOCALAPPDATA%\VoxMic\config.ini` 里 `DenoiseBackend=rnnoise`，但进程照旧加载
`onnxruntime.dll`(14.71MB 镜像) + `sherpa-onnx-c-api.dll`(2.64MB) + 模型(10.11MB)。

**对照实验**（同一 exe，把 DPDFNet 负载改名移开再启动；任务管理器同源计数器
`Win32_PerfFormattedData_PerfProc_Process`）：

| 状态 | Working Set - Private | Working Set |
|---|---|---|
| 带 DPDFNet 负载（现状） | **46.6 – 47.9 MB** | 72 – 78 MB |
| 移除 DPDFNet 负载 | **3.6 – 3.7 MB** | 24.3 MB |
| 差值 | **≈ 43 MB** | ≈ 48 MB |

移除负载时进程正常存活（13 线程 / 64 模块，`onnx|sherpa` 模块数为 0）。
**即 RNNoise 用户 93% 的常驻内存花在一个没被选中的后端上。**

---

## 2. 约束（决定方案形状，不可绕过）

| # | 约束 | 出处 |
|---|---|---|
| 1 | **渲染线程禁止加载 DLL / 建 session**（分配 + 阻塞，违 RT 红线） | AGENTS.md 实时路径红线 |
| 2 | 释放前**必须停 worker**，且卡住的 worker 只能放弃不能释放 | B2 的 `stopWorker()` + `abandoned` 语义 |
| 3 | `isReady()==false` 时管线**已能自动降级到 RNNoise** | `pipeline.h:123`，加载期不会静音 |
| 4 | 后端**只能从设置对话框改**（托盘菜单无此开关） | `tray_icon.cpp` 菜单项 |
| 5 | `runtimeDirectory` / `modelPath` 目前是 `init()` 的**入参、未保存** | `pipeline.h:31-32`；惰性加载需要留存 |

---

## 3. 方案

### 3.1 状态机

```
                 ┌──────────────┐
   启动时后端≠dpdfnet │  NotLoaded   │◄──────── 释放完成
                 └──────┬───────┘
                        │ 需求：后端切到 dpdfnet（或启动时就是）
                        ▼
                 ┌──────────────┐
                 │   Loading    │   loader 线程：LoadLibrary + 建 session + warm-up
                 └──────┬───────┘
                 成功 ◄─┴─► 失败
                  │           │
                  ▼           ▼
           ┌──────────┐  ┌──────────┐
           │  Ready   │  │  Failed  │
           └────┬─────┘  └──────────┘
                │ 需求：已提交配置的后端≠dpdfnet
                ▼
            释放（停 worker → destroy → unload → drop model）
```

加载期间 `isReady()==false` → 管线沿用 RNNoise（约束 3），**不会静音也不会崩**。

### 3.2 触发策略（避免来回抖动）

预览是实时的（改下拉即生效），来回拨会反复加载/释放。建议**加载与释放不对称**：

| 事件 | 动作 |
|---|---|
| 启动，committed = dpdfnet | 立即加载（现状行为，不受影响） |
| 启动，committed ≠ dpdfnet | **不加载** |
| 预览切到 dpdfnet | 加载（让预览诚实，能听到） |
| 预览切走 | **不释放**（防抖动） |
| Apply，committed ≠ dpdfnet | **释放** |
| Apply，committed = dpdfnet | 确保已加载 |

即"**按需加载、按提交释放**"。若仍嫌抖动，再加最小驻留时间（如 30s 内不重复释放），建议先不加。

---

## 4. 落点清单

| 文件 | 改动 |
|---|---|
| `src/dsp/pipeline.h:31-52` | `init()` 存下 `runtimeDirectory`/`modelPath`；改条件加载 |
| `src/dsp/pipeline.h` | 新增 `requestDpdfnetLoad()` / `releaseDpdfnet()`（**均非 RT 调用**） |
| `src/dsp/pipeline.h:214-230` | `degradeDpdfnet()` 里 `dpdfnetAvailable=false` 的语义要区分"未加载"与"失败" |
| `src/dsp/dpdfnet_processor.cpp` | 复用 `stopWorker()` 做释放前置；新增 `shutdownSession()` |
| `src/app_state.h` | 新增加载态（`enum class DpdfnetLoadState { NotLoaded, Loading, Ready, Failed }`），替换 `dpdfnetAvailable` 的布尔语义 |
| `src/settings_dialog.cpp:263,305,345-351,625` | 状态文案要能显示"Not loaded（选中后加载）"，不能误报 unavailable |
| `src/main.cpp:139-140` | 统计输出跟随新状态 |
| `src/dsp/sherpa_onnx_api.h` | 无需改（`unload()` 已具备 FreeLibrary + 清函数指针） |

---

## 5. 风险

1. **切换时的音频间隙**：加载约数百毫秒（估算见下），期间降级到 RNNoise。可接受，但 UI 要有 Loading 提示。
   - 估算依据：`dpdfnet_failure_smoke` 总 3.3s，其中放弃路径占 2.4s，其余（含 `prepare()` + 5 个用例）≈0.9s。
   - **实施时须实测 prepare() 本身耗时**，不要用这个估算当结论。
2. **释放时 worker 卡住**：复用 B2 语义——`stopWorker()` 超时则**放弃并泄漏**，绝不释放（否则 use-after-free）。
3. **加载与渲染并发**：加载完成后才置 `Ready`；RT 侧只经 `ready` 原子与队列访问 session，不直接碰 FFI。
4. **加载失败要可重试**：`Failed` 态允许再次请求（例如用户重新 Apply），不要卡死。

---

## 6. 验收（用 §1 同一套实测方法）

- **RNNoise-only**：`privateWS ≈ 3.7MB`、totalWS ≈ 24MB，进程正常存活（这是核心指标）
- **DPDFNet 用户**：行为不变（启动即加载），`privateWS` 与现状持平
- **切换**：对话框里切到 DPDFNet → 状态变 Ready → `denoiseEffectiveBackend` 变 DPDFNet；切走并 Apply → 内存回落
- **渲染线程零分配**：加载/释放在 loader 线程，`process()` 路径不新增任何调用
- 守卫：`extern 0/0`、`u8 0/0`、行数基线不破（`dpdfnet_processor.cpp` 现 791/791 **已无余量**，本方案的增长必须靠抽取抵消或先还债）

---

## 7. 实施顺序（建议分 3 步，每步独立可验证）

1. **状态与条件加载**：引入 `DpdfnetLoadState`，`init()` 改成按 committed 后端条件加载。
   → 验收：RNNoise-only 内存回落；DPDFNet 用户不受影响。
2. **按需加载**：loader 线程 + `requestDpdfnetLoad()`，对话框预览/Apply 触发；UI 显示 Loading。
   → 验收：能切到 DPDFNet 并正常出声。
3. **释放**：`releaseDpdfnet()`，Apply 且 committed≠dpdfnet 时触发；复用 B2 的放弃语义。
   → 验收：切走后内存回到 ~3.7MB；反复切换不崩、不泄漏（泄漏额度 = 放弃路径，与 B2 同）。
