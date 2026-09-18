# VoxMic C++23 + Slint 界面重构计划

> ⚠️ **本计划已被取代（2026-09-17）**。Slint 路线已放弃：B4/B5 的实验工作保存在
> `slint-ui-experiment` 分支（`025ecec`、`5019d94`），main 已回退到 C++23 稳定化路线。
> 设置界面的现代化改由 `.plan/refactor/settings-ui-regularization-plan.md` 承接
> （布局规则化 + 浅色 token，均为 Win32 原生控件实现，不引入任何 UI 框架）。
> 本文件仅作历史记录：其中挂死点修复（B1/B2）、AppState 单一持有（B3）、架构守卫的
> 内容仍然有效且已落地；附录的**深色视觉规范已作废**（项目已定为浅色，见
> `src/settings_theme.h`），§9 落地步骤中 B4 及以后的 Slint 部分不再执行。
>
> 原状态行（历史）：准备流程已完成，关键环节已实机验证，可开工 · 2026-09-17。
> 原「开工前必读」（历史）：挂死点修复（B2）依赖 C++20，须排在语言标准升级（B1）之后。

---

## 0. 结论

**不换语言。C++17 → C++23**（对齐 VoxType），界面用 **Slint C++ 1.17.1** 重写。

立项理由是**降低后续迭代成本**——不是内存泄漏，那个已在 commit `921c140` 修完。

真正值得重写的是这两块（合计占自研代码近四成）：

| 痛点 | 现状 | 占比 | 重构后 |
|---|---|---|---|
| 设置界面 | `settings_dialog.cpp` **1458 行**手写 Win32 GDI 自绘 | 25% | Slint 声明式，约 400 行 `.slint` + 300 行绑定 |
| 降噪后端适配 | `dpdfnet_processor.cpp` **728 行**手写 `LoadLibrary` + `GetProcAddress` + FIFO | 13% | 抽象后端 + 独立降级策略层，约 300 行 |

已评估并否决的方案：**全量换语言重写**（降噪模型无官方原生绑定，收益不抵风险）。

---

## 1. 现状盘点 `[已核实]`

自研代码约 **5800 行**（不含 vendored RNNoise）：

| 模块 | 行数 | 职责 | 重构动作 |
|---|---|---|---|
| `settings_dialog.cpp` | 1458 | Win32 GDI 自绘设置窗口 | **重写为 Slint** |
| `main.cpp` | 650 | 入口 / 消息泵 / bridge+monitor 线程 / 20+ 全局 atomic | 拆分为 app 编排层 |
| `mic_usage_monitor.cpp` | 652 | CoreAudio 会话事件 + 200ms 对账 | 移入 platform |
| `dsp/dpdfnet_processor.cpp` | 728 | sherpa-onnx 动态加载 + worker/FIFO | 抽象后端 + 降级策略 |
| `wasapi_output.cpp` | 294 | WASAPI 事件驱动渲染 | 移入 platform |
| `adb_control.cpp` | 280 | ADB 子进程 | 移入 platform |
| `config.cpp` | 150 | `config.ini` 读写（20 字段） | 移入 core，改原子写 |
| 其余 12 个文件 | ~1690 | 托盘 / 注册表 / 路径 / socket / 设备枚举 | 按层归位 |
| `dsp/rnnoise/` | 276,919 | 官方 v0.2 C 源码 vendored（权重表占 270,198 行） | **不动**，继续以 C 编译进工程 |

第三方依赖：`sherpa-onnx-c-api.dll`(2.74MB) + `onnxruntime.dll`(15.4MB) + `dpdfnet2_48khz_hr.onnx`(10.6MB)，均 Git LFS 跟踪。

Android 端：**4 个 Java 文件**（`App/MainActivity/RecordService/RecordThread`），协议为 `localabstract:audiosource` 上的 960 字节 int16 块 —— **不重写**。

---

## 2. 现存两个挂死点（已拍板：单独一个提交，即 §9 的 B2）

这两个是**设计问题**，重构不会自动解决，必须先修（各约 10 行）：

| 缺陷 | 位置 | 现状 |
|---|---|---|
| `worker.join()` 永久阻塞 | `dpdfnet_processor.cpp:380-398` | 原生 `sherpa-onnx Run()` 不可取消，join 无超时 |
| `bridge.join()` 可能永久挂起 | `main.cpp:414` → `socket_client.cpp:65` | 阻塞 `recv` 未设 `SO_RCVTIMEO` |

**执行时机**：准备流程收尾后，作为 §9 的 **B2** —— 单独一个提交，紧跟语言标准升级（B1）之后。

**前置依赖（2026-09-17 补，实机核实）**：DPDFNet 那半条要改 `std::jthread` + `std::stop_token`，
二者是 **C++20** 特性；而 `CMakeLists.txt:20` 当前是 `set(CMAKE_CXX_STANDARD 17)`。
→ **本节的修复（B2）必须排在语言标准升级（B1，17 → 23）之后**，否则编译不过。
若坚持先行，该半条只能写成 C++17 可用形态（放弃 `join()` → `detach()` + 标记残留），
代价是"退不出"时泄漏一个线程、仍不可强杀。**建议先 B1、再 B2。**

---

## 3. 为什么是 C++23（而不是停在 C++20）

对齐 VoxType（`CMakeLists.txt:7` 已 `CMAKE_CXX_STANDARD 23`），同一 MSVC 工具链已跑通，**升级零工具链风险**。且对本项目有实质收益，不是追新：

| 特性 | 标准 | 直接解决的现存问题 |
|---|---|---|
| `std::jthread` + `std::stop_token` | C++20 | **§2 两个挂死点的解药**：取消成为一等公民，`jthread` 析构自动 join |
| `std::atomic<T>::wait / notify_all` | C++20 | 替代手写事件唤醒（`m_wakeEvent`） |
| `std::span<const float>` | C++20 | DSP 切片签名，替代 `(const float*, size_t)` |
| `std::expected<T, AppError>` | C++23 | 配置 / ADB / 管道的错误返回（沿用 VoxType 双轨错误模型） |
| `std::format` | C++20 | 消灭 `snprintf` |

**两点必须说清楚**：

1. **C++23 不解决 UI 问题。** 加一行布局困难、配置项改动散落、界面不好看——这三条在 C++17/20/23 下完全一样。决定成本的是信息组织方式，不是语言标准（与 VoxType 同判断）。
2. **C++23 无法强杀已进入 native 的 `sherpa-onnx Run()`。** `stop_token` 只能让"放弃等待"变成显式可诊断状态，仍需配合超时设计。

---

## 4. 为什么是 Slint

### 4.1 与 VoxType 的关键差异：IME 权重不同

VoxType 把输入法（IME）定为"分族判据"，因为它的 Settings 要输入 LLM Prompt、敏感词表、热词表（大量中文）。**VoxMic 不是这个形态** `[已核实]`：

```
settings_dialog.cpp 全文文本输入控件只有 2 处：
  :880  EDIT  host   ES_AUTOHSCROLL   → IP / 主机名，ASCII
  :889  EDIT  port   ES_NUMBER        → 纯数字
另有 3 处 COMBOBOX（设备序列号 / App preset / 降噪后端）→ 选择，无文本输入
```

→ **中文输入需求 ≈ 0**，因此 VoxType 的"IME 一票否决"在此不成立。

**已核实（2026-09-17 补）——原"未能独立核实"的观察项可结案**：
Slint issue **#5982**（`Redundant display when input Chinese on LineEdit (or TextEdit)`，
Windows + winit 后端，中文输入时输入框下方重复绘制候选字）**已于 2026-03-02 关闭**，
`state_reason: completed`。维护者 tronical 的关闭说明：

> Upstream released a fix as part of winit 0.30.13 - call of `cargo update` away with any recent release of Slint :)

Slint 1.17.1 的 win64 预编译包启用了 `BACKEND_WINIT`（见 `SlintTargets.cmake` 的
`SLINT_ENABLED_FEATURES`），其发布（2026-07）晚于该修复 → **判定该缺陷对 1.17.1 不适用**。
叠加本项目实测确认的中文输入需求 ≈ 0，此项**不再列为观察项**。

### 4.2 体积实测：不会破坏现有量级 `[已核实]`

```
VoxMic 现状：voxmic.exe 6.0 MB
             onnxruntime.dll 15.4 MB   ← 单这一个就占大头
             sherpa-onnx-c-api.dll 2.7 MB + 模型 10.6 MB
             → MSI 19.9 MB / portable 7z 18.9 MB

Slint 增量（2026-09-17 实测，预编译包 1.17.1 win64-MSVC-AMD64）：
             slint_cpp.dll 26.3 MB     ← 会超过 onnxruntime.dll，成为最大单个依赖
             slint-compiler.exe 10.4 MB（构建期用，不进发布包）
```

**结论不变（发布包早已不是轻量量级），但量级要按实数记账**：原先"估 6–9MB"是 `[推断]`，
实测是 **26.3 MB**，MSI 预期从 19.9MB 升至约 30MB 量级（待打包后实测确认）。

体积偏大的原因：该预编译包把 `INTERPRETER`、`RENDERER_SKIA`、`RENDERER_FEMTOVG`、
`RENDERER_SOFTWARE`、`ACCESSIBILITY`、`TESTING` 全部编进同一个 DLL
（见 `SlintTargets.cmake` 的 `SLINT_ENABLED_FEATURES`）。若日后需要瘦身，只能自行从源码裁剪特性
——需要 Rust 工具链，属独立议题，不在本次重构范围。

### 4.3 内存约束的真正解法：UI 按需创建

「低内存」指**常驻 RAM**，不是磁盘体积。Slint 可以不常驻：

```
常驻态（托盘后台）：只跑音频引擎 + 托盘图标（现有 Win32 tray_icon.cpp，119 行）
                    → 不创建任何 Slint 窗口，零 UI 运行时开销
按需态（点开设置）  ：才 construct Slint 组件 → 关闭即 destroy
```

配合这一条，Slint 对"常驻低内存"目标**无影响**。必须显式做到，不能默认获得（见步骤 B6 验证）。

### 4.4 选型对比

| 路线 | 附加依赖 | 常驻内存（配合按需创建） | 结论 |
|---|---|---|---|
| **Slint C++ 1.17.1** | Slint 运行时（DLL 或静态） | **0** | **选定** |
| 自研布局 + D2D/DWrite 自绘 | 0 | 0 | 退路（Slint 集成受阻时启用） |
| WinUI 3 / XAML Islands | Windows App SDK 运行时 | 中高 | 否决 |
| Qt 6 | +30–60MB，授权义务 | 高 | 否决 |
| WebView2 | +80–150MB | 很高 | 否决 |

---

## 5. 目标架构

```
                    ┌──────────────────────────────┐
  Windows 托盘/窗口  │  voxtype_ui（Slint 1.17.1）   │  ui/*.slint + theme.slint
                    └───────────┬──────────────────┘
                                │ Command / StateSnapshot
                    ┌───────────▼──────────────────┐
  线程编排/生命周期   │  voxtype_app                  │  jthread + stop_token 关闭序列
                    └───────────┬──────────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
        │                       │                       │
┌───────▼─────────┐   ┌─────────▼──────────┐   ┌────────▼───────┐
│ voxtype_core    │   │ voxtype_platform   │   │ android_app    │
│ 纯逻辑·可测      │   │ Windows 专属       │   │ Java·不动      │
│ config / dsp    │   │ WASAPI / CoreAudio │   └────────────────┘
│ ring / session  │   │ ADB / socket / 注册表│
└─────────────────┘   └────────────────────┘
```

**分层硬约束**：`voxtype_core` 不依赖任何 Windows API，可独立编译出离线测试可执行文件。
当前 C++ 版 DSP 与平台代码缠在一块（`pipeline.h` 依赖全局 atomic），这是可测性差的主因。

---

## 6. 架构重构点（不合理 → 新设计）

| # | 现状问题 | 新设计 |
|---|---|---|
| 1 | **20+ 全局 `std::atomic`** 做跨线程状态，`settings_dialog.cpp` 用 `extern` 直接读写引擎内部状态 | 单一 `AppState` 持有；UI 只发 `Command`、只收 `StateSnapshot`，**UI 与引擎解耦** |
| 2 | **关闭序列隐式**，`g_running=false` 后三个 `join()` 无超时 | 显式 `Shutdown` 协议：`stop_token` → join(timeout) → 强制降级 → 记录残留。让"退不出"变成**可诊断**而非**挂死** |
| 3 | **降噪后端硬编码 switch** | 抽象后端接口（`process` / `reset`），RNNoise 与 DPDFNet 可互换；降级策略（warm-up≤4 空块、稳态≤3 空块）独立成策略层，不再散落在 worker 里 |
| 4 | **`config.ini` + `PrivateProfile` API**，`save()` 非原子且要求调用方自行回滚 | 写入临时文件后原子 rename + schema 版本号；内置旧 ini 迁移器 |
| 5 | **SPSC ring 无 cacheline padding** | 读写位置各自 64 字节对齐（当前存在 false sharing，未被测出） |
| 6 | 版本散在多处 | 单一 `src/version.h` 为源头（现有机制保留），MSI/Android 版本继续派生 |
| 7 | 构建脚本分散 | **保留现有 CMake + WiX + PowerShell 体系**，Slint 通过 `find_package(Slint)` 接入，不另起构建系统 |

---

## 7. Slint C++ 集成要点 `[已核实]`

| 项 | 结论 |
|---|---|
| 版本 | **1.17.1**（已按此版本安装 `cpp-sdk` 并完成编译运行实测，见本表下方实测记录） |
| C++ SDK | 官方预编译包 `Slint-cpp-1.17.1-win64-MSVC-AMD64.exe`（NSIS 安装器，可直接用 7-Zip 解包，无需运行安装器） |
| **安装位置** | `~/.workbuddy/binaries/slint/1.17.1/cpp-sdk/`（与既有 `bin/`、`lsp/`、`viewer/` 同级），实测 37 MB |
| **环境变量** | 用户级 `CMAKE_PREFIX_PATH` = `C:\Users\kawae\.workbuddy\binaries\slint\1.17.1\cpp-sdk`（原为空，已设） |
| **安装包校验** | `sha256 = f5b537da448c1e3d72a24a774e19518ae412b9706b8ef49bdee64b62b878fe56` |
| 是否需要 Rust 环境 | **不需要**（官方：binary packages work without any Rust development environment） |
| CMake | `find_package(Slint)`；**链接目标 `Slint::Slint`**，`slint_target_sources()` 注册 `.slint` |
| 编译器要求 | `Slint::Slint` 声明 `INTERFACE_COMPILE_FEATURES cxx_std_20`；本项目定 C++23，满足。MSVC 会自动加 `/bigobj` |
| 运行时 | **必须随包带 `slint_cpp.dll`** —— 该预编译包**只提供共享库**（`lib/slint_cpp.dll` + `lib/slint_cpp.dll.lib`），**无静态库**，静态链接不可行。exe 找不到该 DLL 会以 `0xC0000135` 直接退出（已实测） |
| 构建期依赖 | `bin/slint-compiler.exe`（由 SDK 自带，`SlintConfig.cmake` 默认走本地副本，**不联网下载**） |
| 默认控件风格 | `SLINT_STYLE` 默认为 `fluent`。**其自带配色与 §附 的设计规范不一致**：需覆写风格 token 或自绘控件，最终统一落进 `ui/theme.slint` |
| 托盘 | 现有 `tray_icon.cpp` 仅 **119 行**且工作正常，**保持不动**；Slint `SystemTrayIcon` 列为可选 |
| 设计 token | **不继承任何外部项目**。按 §附 的视觉设计规范自行设计，落成 `ui/theme.slint` 的 `global` |
| **许可文件** | SDK 自带 `licenses/LICENSE.md` + `THIRDPARTY.md`，随包保留 |

**实测记录（2026-09-17）**：用与本项目一致的 MSVC 14.44.35207 x64 + Ninja + C++23 构建了一个最小
`find_package(Slint)` 工程，验证链全部通过——

```
CONFIGURE_EXIT=0    Slint_DIR = .../cpp-sdk/lib/cmake/Slint
BUILD_EXIT=0        slint-compiler 生成 slint_generated_app_1.cpp → main.cpp 编译 → 链接 slint_smoke.exe (118,784 B)
不拷 DLL 运行        exit -1073741515 (0xC0000135, STATUS_DLL_NOT_FOUND)
拷入 DLL 后运行      exit 0，输出 SMOKE_OK（窗口创建 + 事件循环 + Timer 退出均正常）
```

该冒烟工程同时用了 `AboutSlint` widget，**证明 §8 的许可披露义务在本版本有对应控件可用**。

---

## 8. 许可：已选定 Royalty-free 2.0 `[已核实]`

来源：Slint 1.17.1 随包 `licenses/LICENSE.md`（三档任选其一）：

| 档位 | 费用 | 覆盖范围 | 条件 |
|---|---|---|---|
| **Royalty-free 2.0** ← 已选 | 免费 | 专有/开源 **桌面 · 移动 · Web**（排除嵌入式） | **必须披露使用了 Slint** |
| GPLv3 | 免费 | 开源（GPL 兼容），含嵌入式 | 应用整体按 GPLv3 分发 |
| Commercial | 付费 | 专有全平台，含嵌入式 | 见 slint.dev/pricing |

**VoxMic 自身是 BSD 3-Clause**（`LICENSE` 首行）。选定免版税档的理由：桌面应用正好落在覆盖范围，免费，且不影响现有 BSD 授权。不选 GPLv3（会使项目整体转为 GPLv3，与 BSD-3 冲突）；不需要 Commercial（桌面应用已被覆盖）。

**落地义务（不可跳过）**：在设置窗口内放 **`AboutSlint` widget 或 Slint badge** 完成披露。
这是步骤 B4 的组成部分，且受架构守卫强制（见 §9 修正说明）——**没有披露则免版税档不成立，需转为商业许可。**
`AboutSlint` 在本版本中可用已由冒烟工程实测确认（见 §7 实测记录）。

---

## 9. 落地步骤

顺序已于 2026-09-17 修正（两处原顺序按字面执行会当场失败，见备注）。

| 步骤 | 内容 | 退出条件 |
|---|---|---|
| B0 | 准备落定：许可（免版税 + AboutSlint）、slint skill、CLI 工具、**C++ SDK 安装 + 编译实测** | ✅ 已完成 |
| **B1** | **语言标准升级**：`CMAKE_CXX_STANDARD` 17 → 23；沿用 VoxType 的 `/utf-8` + `NOMINMAX` 红线 | 全量编译通过 |
| **B2** | **§2 两个挂死点**（另开提交）：socket 设 `SO_RCVTIMEO`；DPDFNet worker 改 `jthread` + 超时放弃 | 退出不再挂死 |
| B3 | 解耦：抽出 `Command` / `StateSnapshot` 通道，UI 不再 `extern` 全局 | `settings_dialog.cpp` 无 `extern` atomic |
| B4 | 接入 Slint：`find_package(Slint)` 打通 + 空窗口跑起来，**同一步放入 `AboutSlint`** | 构建并显示出 Slint 窗口，且守卫通过 |
| B5 | 按 §附 的视觉设计规范落成 `ui/theme.slint`；逐项迁移 20 个配置项 | 功能对齐现有配置项 |
| B6 | 按需创建/销毁验证（§4.3）：托盘常驻时不加载 UI | 常驻内存与改造前持平 |
| B7 | 打包：**随包带 `slint_cpp.dll`**（静态链接不可行，见 §7），`validate_build_layout.ps1` 白名单适配 | 装得上、卸载干净 |

**两处顺序修正的原因（原编号 → 新编号）**

1. **原"B-fix"（现 B2）原排在原 B2（现 B1）之前 → 编译不过。**
   原因见 §2 前置依赖：`std::jthread` / `std::stop_token` 是 C++20，而 CMake 仍是 C++17。
2. **原 B3（现 B4）会触发架构守卫 FAIL。**
   `scripts/check_architecture.ps1` 第 5 节规定：`ui/` 下只要存在任一 `.slint` 而无 `AboutSlint` 即 FAIL，
   且 `build.bat:37` 在构建前强制执行。所以首个 `.slint` 落地时必须同步带 `AboutSlint`，
   否则 `build.bat` 直接失败。原 B5（AboutSlint）已并入现 B4。

   对应的守卫代码（`scripts/check_architecture.ps1:126-141`，扫描的是**仓库根目录的 `ui/`**）：

   ```powershell
   $slintFiles = Get-ChildItem -Path (Join-Path $Root 'ui') -Recurse -Include *.slint -File
   if ($slintFiles.Count -gt 0) {
       ... if (-not $hasDisclosure) { Add-Fail 'Slint files present but no AboutSlint widget ...' }
   }
   ```

   ⚠️ **注意**：`.slint` 必须放**根目录 `ui/`**。若按 `AGENTS.md` 中"Source File Structure"一节的写法放进
   `src/ui/`，守卫会**扫不到任何 `.slint`，许可披露检查静默失效**——免版税档即不成立。
   （该文档不一致已于 2026-09-17 修正。）

**额外的操作约束**

- `extern` 计数与 `std::thread` 计数当前**正好卡在守卫上限**（30/30、4/4，见 `check_architecture.ps1:40-45`），
  零余量。B3 解耦过程中任何**临时**新增 `extern` 都会让 `build.bat` 失败 → 需按小步提交、每步使计数只降不升。

---

## 10. 准备流程状态（2026-09-17 已完成）

| 项 | 位置 | 状态 |
|---|---|---|
| 项目备份 | `D:\GITHUB_melody0709\vox_mic\vox_mic.bak` | ✅ 61MB（排除可重建的 `build/` 与有远程的 `.git/`） |
| Slint 官方 AI skill（skill 名 `slint`） | `~/.workbuddy/skills/slint/` | ✅ 已装，已过安全审计 |
| `slint-lsp` 1.17.1 | `~/.workbuddy/binaries/slint/1.17.1/bin/` | ✅ 已装并验证，PATH 已配置 |
| `slint-viewer` 1.17.1 | 同上 | ✅ 已装并验证；用户级 PATH 已含该目录（已核实） |
| **Slint C++ SDK 1.17.1** | `~/.workbuddy/binaries/slint/1.17.1/cpp-sdk/` | ✅ **已装（37MB）并完成编译运行实测**（2026-09-17，见 §7） |
| **`CMAKE_PREFIX_PATH`** | 用户级环境变量 | ✅ 已指向 `cpp-sdk`（原为空值） |
| **架构守卫** | `scripts/check_architecture.ps1`，挂在 `build.bat:37` | ✅ 已落地并接入构建主流程；ratchet 基线按真实代码校准 |
| Slint docs MCP（远程） | 插件声明 `https://docs.slint.dev/mcp` | ⏸ 未启用（保守默认） |
| `slint-ui-layout-optimization` | 用户级 | 本机早前已装 |
| `windows-crash-dump-triage` | 用户级 | 本机早前已装 |

**环境变量事项（2026-09-17 记录）**：此前用 `setx PATH "%PATH%;<slint bin>"` 配置 PATH。
该写法会把**机器级 PATH 整体并进用户级**（`%PATH%` 取的是合并后的进程 PATH）。
实测后果：用户级 PATH 由 19 条膨胀至 74 条、878 → 3091 字符（**未触发 1024 截断，无条目丢失**）。
已清理还原为 20 条 / 975 字符，改动前原值备份在
`~/.workbuddy/backups/user-path-before-cleanup-20260917-102801.txt`。
**后续约定**：不再用 `%PATH%` 拼接；改 PATH 用
`[Environment]::SetEnvironmentVariable(<完整目标值>, 'User')`，或走系统属性 GUI。

**skill 安全审计结论**（人工逐文件审计；本机无 `skills-security-check` skill）：

- **P0（严重）：无。** 包内无任何可执行脚本，全部为 md / json / yaml。
- **P1-a**：`mcp_config.json` 声明远程 MCP（外部网络服务）→ 保守默认**未启用**。
- **P1-b**：原 `SKILL.md` 有一段会建议执行 `gh api --method PUT /user/starred/...`（对用户 GitHub 账号的写操作），是包内唯一外部副作用指令 → **安装时已删除该段**（grep 计数已验证为 0）。
- **P2**：其余为纯文档，安装说明均指向官方源。

---

## 11. 守护边界 `[已核实]`（2026-09-17 补全）

> 原文写"尚无架构不变量守卫、待审计补全"——**该描述已过期**。守卫已落地并接入构建主流程。

`scripts/check_architecture.ps1`（171 行）挂在 `build.bat:37`：**每次构建前强制执行，违规 `exit /b 1`**，
不会构建到一半才暴露问题（`--clean` 模式跳过）。ratchet 规则：基线只减不增，
加基线必须在提交信息里写明理由。

| 守卫节 | 检查项 | 当前基线（实测吻合） |
|---|---|---|
| 1 | 文件行数 ratchet | `settings_dialog.cpp` 1458 · `main.cpp` 650 · `mic_usage_monitor.cpp` 652 · `dsp/dpdfnet_processor.cpp` 728 · `AGENTS.md` 145 |
| 2 | `extern` 声明总数（排除 vendored `rnnoise/` 与 `extern "C"`） | **30 / 30（零余量）** |
| 3 | C++23 红线 | `u8"` 字面量 0 / 0 · 裸 `std::thread` **4 / 4（零余量）** |
| 4 | 构建红线 | 必须有 `/utf-8`；PCH 含 `windows.h` 时必须定义 `NOMINMAX` |
| 5 | Slint 许可披露 | 根目录 `ui/` 下存在 `.slint` 时必须出现 `AboutSlint` |
| 6 | 单一 `AGENTS.md` | `doc/zh-CN/AGENTS.md` 不得存在（英文版为唯一来源） |

其余守护按原机制不变：

| 脚本 / 文件 | 覆盖 |
|---|---|
| `scripts/validate_build_layout.ps1` | `build/` 布局白名单 + `runtime-manifest.json` hash 校验 |
| `.input.sha256` 伴生文件 | 同版本产物保护：改代码未升版本且未重跑 `--package*` → 打包失败 |
| `packaging/windows/ProductIdentity.wxi` | 冻结的永久 GUID（UpgradeCode）；改动会断裂升级链 |

**B7 时的待办 `[推断]`**：新增的 `slint_cpp.dll` 需同步纳入 `validate_build_layout.ps1` 的白名单与
`runtime-manifest.json`，否则按现机制打包会被布局校验拦下。落地时以实跑结果为准。

---

## 12. 已拍板（2026-09-17）

1. **Slint 运行时形态 → 只能随包带 `slint_cpp.dll`，静态链接不可行。**
   原设想"随包带 DLL / 改为静态链接，待实测二选一"经实测**不成立**：官方预编译包只提供共享库
   （`lib/slint_cpp.dll` + `lib/slint_cpp.dll.lib`），`SlintTargets.cmake` 中只有一个
   IMPORTED SHARED 目标 `slint_cpp-shared`，**不含任何静态库**。
   要静态链接只能自行从源码构建（需 Rust 工具链），不在本次重构范围。
   已实测：不拷贝该 DLL 时 exe 以 `0xC0000135`（STATUS_DLL_NOT_FOUND）退出。
   → B7 打包动作据此确定：把 `slint_cpp.dll`（26.3 MB）放进安装目录。

---

## 13. 实施进度

| 步骤 | 状态 | 提交 | 验证证据 |
|---|---|---|---|
| B0 准备落定 | ✅ | `cb43ae2` `30183eb` `40a992f` `bfca2d7` | SDK 编译运行实测；守卫落地并校准 |
| B1 C++23 | ✅ | `89faf1d` | 7 个 target 全绿（0 error / 0 warning）；守卫 PASS；托盘启动后常驻存活 |
| B2 两个挂死点 | ✅ | `f0ad2ad` | 单元测试 + 4 个冒烟全过；放弃路径实测 2437 ms 返回（预算 2 s） |
| B3 解耦（单一 `AppState`） | ✅ | 本轮 | **`extern` 30 → 0**；守卫 PASS；全部测试通过；托盘常驻存活 |
| B4 接入 Slint（含 AboutSlint） | ⏳ | | |
| B5 主题 + 迁移 20 个配置项 | ⏳ | | |
| B6 按需创建/销毁验证 | ⏳ | | |
| B7 打包（随包带 `slint_cpp.dll`） | ⏳ | | |

### B6 的内存基准（已实测，供 B6 对照）

托盘常驻、未推流时的稳态内存（B1 之后的构建，4 次启动结果一致）：

```
t = 5s / 10s / 15s / 20s    RSS 稳定 76.6 MB    private 稳定 51 MB
```

早前一次测到 14.2 MB，是进程尚未完成初始化的采样，**该数字作废，勿再引用**。
B6 的判定口径：**托盘常驻时不加载任何 Slint UI，稳态不得高于上表。**

### B3 实施细节

**做法**：把跨线程共享状态收进单一 `AppState`（`src/app_state.h`），用 C++17 **`inline` 变量**
实现——命名空间作用域的单一定义，因此**连 `extern` 声明都不需要**。
选 `inline` 变量而非函数内 `static` 懒初始化单例，是因为 WASAPI 渲染线程每个音频块都要读这些原子，
懒初始化会引入 guard 变量检查。此处无堆分配、无锁，符合实时路径红线。

调度进来的成员（原先是散在各 TU 的 `extern`）：

- DSP 设置 12 项：`gain` / `eqEnabled` / `eqPresence` / `eqBassCut` / `compressorEnabled` /
  `nrEnabled` / `nrStrength` / `denoiseBackend` / `denoiseResetEpoch` /
  `dpdfnetAvailable` / `dpdfnetDegraded` / `denoiseEffectiveBackend`
- 运行标志 5 项：`running` / `micRequested` / `demandMode` / `alwaysHot` / `micOnTick`
- 非实时状态：`config`、`trayIcon`
- `DenoiseBackendKind` 枚举一并移入（它属于状态词汇表），`pipeline.h` 经 `app_state.h` 转出

**同时消除的 3 个函数 `extern`**：`syncDspAtomsFromConfig` / `requestDenoiseReset` /
`setDemandModeRuntime` —— 函数声明上的 `extern` 关键字本就冗余，改在 `app_state.h` 中声明
（概念上它们正是 UI 发给引擎的**命令**），守卫的 `^\s*extern\s+` 因此不再命中。

**调用点处理**：19 个符号用 `\b` 词边界做全局机械替换（`g_gain` → `g_appState.gain`，共 237 行），
再由编译器验证。两个冒烟测试原本各自重复定义了一份 DSP 全局，现已删除——头文件统一提供。

**守卫基线**：`extern` 30 → **0**（已收死）。`CMakeLists.txt` 为 voxmic 增加 `src` 到 include 路径，
使共享头文件从 `src/dsp/` 下也能按同一方式解析。

⚠️ **B3 只完成了解耦的一半**：状态已单一持有，但"UI 只发 `Command`、只收 `StateSnapshot`"
这一层契约尚未建立。它要等 B5 的 Slint UI 一起做——在即将被替换的 1458 行 Win32 对话
上先实现一遍是纯浪费。**该契约的落地位置改为 B5。**

### B2 实施细节

**socket 侧**

- `SocketClient::connect()` 设置 `SO_RCVTIMEO = 500 ms`（`RECV_TIMEOUT_MS`）。
- `recvExact()` 改为区分三种结果：`>0` 成功 / `0` 对端关闭 / `RECV_TIMEOUT` / `RECV_ERROR`，
  `main.cpp` 据此打印 "stalled mid-block" 与 "lost" 两种不同日志——让"为什么重连"可从日志回答。

**DPDFNet worker 侧**

- `std::thread` → **`std::jthread` + `std::stop_token`**（裸线程基线 4 → 3）。
- `stopWorker()` 改为**有界等待**：`request_stop` → 轮询 `workerExited` 最多
  `WORKER_STOP_TIMEOUT_MS`（2 s，公开常量，测试与调用方共用同一真相源）。
- 超时则：`detach()` + 置 `workerAbandoned` + **跳过全部资源释放**（事件 / denoiser / DLL 全部故意泄漏，
  因为它们可能仍被卡住的 worker 使用），并打印诊断。
- `~DpdfnetProcessor` 在已放弃时 `m_impl.release()`，不释放 Impl（否则 use-after-free）。
- `isReady()` / `hasFailed()` 纳入放弃状态 → 管线自动降级回 RNNoise；
  `prepare()` 拒绝复用被污染的会话，要求重启。

**途中发现并修掉的两个自身缺陷**（先追代码才暴露，值得记录）：

1. `stopWorker()` 在**已放弃**的 Impl 上被二次调用时，会跳过 join 分支直接走到资源释放
   → 会在 worker 仍持有时关掉事件、销毁 denoiser、卸载 DLL。→ 已加顶部早退。
2. `prepare()` 只在**进入时**检查放弃标记，而 `stopWorker()` 可能恰在**本次调用中**才放弃
   → 会在被污染的 Impl 上重建 worker。→ 已在 `stopWorker()` 之后加复查。

**回归测试**：`tests/dpdfnet_failure_smoke.cpp` 新增 `runAbandonedWorkerCase()`——
用 `setWorkerDelayForTest(10000)` 把 worker 停在 10 秒 sleep 里（与"卡在 native Run()"同形），
断言析构仍在预算内返回（不许死等满 10 秒），实测 `abandoned=2437 ms`。

**守卫基线临时抬高**（按守卫自身规则，理由须在提交信息中说明）：
`src/main.cpp` 650 → 655、`src/dsp/dpdfnet_processor.cpp` 728 → 814。
⚠️ **B3 拆解 `main.cpp`、B5 重写 `dpdfnet_processor.cpp` 之后，两者必须回落到 650 / 728 以下。**

### 本机构建方式（与 `build.bat` 的差异，仅为绕开环境限制）

本机沙箱有两条硬限制：`reg.exe` 在程序黑名单里（`vcvars64.bat` 内部要调它，因此跑不起来），
且本会话进程环境同时存在 `Path` 与 `PATH`，会让 MSBuild 抛 `MSB6001`。
因此本轮验证走的是**手工喂 MSVC 环境 + Ninja 直连 `cl.exe`**——
用同一个已配置好的 `build/cmake/x64-release` 构建树，工具链与 `build.bat` 完全一致。
**`build.bat` 仍是标准入口**，在正常终端里用它没有这些问题。
另有已知偶发：并行编译时个别 `.obj` 创建被拒（`C1083 Permission denied`），重试即过。

---

## 附：视觉设计规范（自行设计，不继承参考项目）

**已否决：不继承参考项目 `stock_new` 的视觉规范**（2026-09-17 决定）。

`stock_new` 是深蓝底、亮蓝主色的交易看板，信息以卡片网格组织；而 VoxMic 是
**托盘常驻的音频工具**：窗口小、配置项密集、核心信息是"链路状态 + 设备 + 流状态"，不是数据卡片。
套用看板的深蓝配色只会得到一个"看起来像别的软件"的设置窗——**其视觉规范对本项目不具参考价值**。
→ 视觉规范重新设计；`.slint` 的语法与控件用法以 **Slint 官方文档**为准，不以任何现有项目为模板。

### 设计定位

一句话：**原生工具感的深色音频控制台**。

- **结构**：单列分区（section）+ 顶部常驻状态头（实时状态与电平），不是卡片网格
- **密度**：4px 基准网格，比看板式布局紧凑——设置项多、窗口小
- **强调色**：青绿（信号 / 音频语义），刻意避开通用的"科技蓝"
- **语义色**：直接映射引擎真实状态，让窗口与托盘图标说同一种语言

### 设计 token（B5 落成 `ui/theme.slint` 的 `global`）

| 类别 | 值 | 说明 |
|---|---|---|
| 窗口底 | `#131417` | 中性石墨，不偏蓝 |
| 主面板 | `#1b1d21` | |
| 嵌套区 | `#22252a` | |
| 描边 / 分隔 | `#2f3339` | |
| 焦点环 | `#3d434b` | 保证键盘焦点可见 |
| 主色 accent | `#14b8a6` · hover `#0d9488` · pressed `#0f766e` | 青绿 |
| 主文字 | `#e8eaed` | |
| 次文字 | `#b9bec6` | |
| 辅助文字 | `#8b919a` | |
| 禁用 / 提示 | `#5f656e` | |
| 圆角阶梯 | 4px 控件 / 8px 分组 / 14px 窗口 | |
| 字号阶梯 | 11 / 12 / 13 / 15 / 17 px | 工具类应用，不设展示级大字号 |
| 间距阶梯 | 4 / 8 / 12 / 16 / 24 px | 4px 基准网格 |
| 字体 | `"Segoe UI Variable Text"`，中文由系统字体回退 | 偏系统原生观感，非 Web 感 |

### 状态色 = 引擎状态（与托盘图标对齐）

| 状态 | 色 | 含义 | 现有托盘图标 |
|---|---|---|---|
| 待机 idle | `#6b7280` | 托盘常驻、未推流 | `voxmic_idle.ico` |
| 就绪 armed | `#38bdf8` | 链路已建立、尚无消费方 | `voxmic_connected.ico` |
| 推流中 streaming | `#14b8a6` | 与主色一致 = "信号在流动" | `voxmic_streaming.ico` |
| 降级 degraded | `#f59e0b` | DPDFNet 不可用 / 空块降级 | — |
| 故障 fault | `#ef4444` | socket 断开、设备丢失 | — |

状态色须与 `tray_icon.cpp` 的图标状态语义一致，B5 落地时确认不冲突。

### 落地时必须验证（`AGENTS.md` 硬要求，不得跳过）

`.slint` 改完必须跑 `slint-viewer --check` 校验语法 **并** `--screenshot` 看渲染结果，
**未看过渲染不得宣称完成**。重点确认两点：

1. `"Segoe UI Variable Text"` 在本机可用、中文回退渲染正常；
2. `SLINT_STYLE` 默认 `fluent` 的控件自带底色与上表的冲突程度 → 据此决定是覆写风格 token 还是自绘控件。

> 注：本次准备**不预先创建 `ui/theme.slint`**。守卫第 5 节规定 `ui/` 下存在任一 `.slint`
> 而无 `AboutSlint` 即令构建失败，因此 `theme.slint` 必须与带 `AboutSlint` 的首个窗口同批落地（见 §9 的 B4 / B5）。

### VoxType 的**非视觉**基建仍可对齐 `[已核实]`

`D:\GITHUB_melody0709\VoxType` 的工程基建继续对齐：分层契约、双轨错误模型、
`/utf-8` + `NOMINMAX` 构建红线、`.ps1` 脚本纯 ASCII 要求。**仅限工程基建，不涉视觉。**
