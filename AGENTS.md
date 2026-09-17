# AGENTS.md

Use your Android phone's microphone as a Windows system microphone via ADB + VB-CABLE + Raw WASAPI. On-demand activation: streaming only when an app is using CABLE Output, DSP bypassed when idle.

This file is English-only and is loaded in full every session. Keep it short:
only rules that break something when violated belong here. Reference material
lives in the files below.

## Documentation Map

| Need | Read |
|------|------|
| Data flow, thread model, parameters, monitor detection | `ARCHITECTURE.md` |
| Build / package / sign / Android keystore | `BUILD.md` |
| User-facing guide | `README.md` |
| Upgrade contract, signing | `packaging/windows/UPGRADE_CONTRACT.md` |
| Long-term ideas | `FUTURE_ROADMAP.md` |
| C++23 + Slint refactor | `.plan/refactor/cxx23-slint-refactor-plan.md` |

Files under `doc/zh-CN/` are Chinese translations of older revisions. They may
lag behind the English originals — treat English as the source of truth.

## Plan Management

- 重构 / 架构类计划 → `.plan/refactor/`
- 功能类 → `.plan/feat/`，修复类 → `.plan/fix/`
- 实施完成 → 移入 `.plan/completed/`
- `plan/` 下的旧计划为历史遗留，新计划一律放 `.plan/`
- **Do NOT** create `.md` plan files in the project root directory

## Tech Stack

| 项 | 值 |
|---|---|
| 语言标准 | **C++23**（`CMakeLists.txt` → `CMAKE_CXX_STANDARD 23`）——⚠️ 当前实际仍为 17，B1 步骤升级到位 |
| 构建 | CMake + Ninja + MSVC（VS2022 C++ x64） |
| 界面 | 现状 Win32 GDI 自绘；**重构中 → Slint C++ 1.17.1** |
| 打包 | 7-Zip（Portable）+ .NET SDK + WiX v4 SDK via NuGet（MSI） |

## C++23 开发规则（强制）

### 编译红线

- 每个 target 必须带 **`/utf-8`**（sherpa-onnx 头文件含非 ASCII 字面量）
- 全局定义 **`NOMINMAX`**（否则 `std::min` / `std::max` 报 C2589）
- **`u8"..."` 字面量禁用**（C++20 起类型为 `char8_t`，会引入类型冲突）
- `.ps1` 脚本必须**纯 ASCII 或带 UTF-8 BOM**（PS 5.1 按 ANSI 解码无 BOM 的 UTF-8，会吞掉紧跟非 ASCII 字节后的换行，产生看不出与编码有关的解析错误）

### 并发

- 新线程一律 **`std::jthread` + `std::stop_token`**，禁止裸 `std::thread` + 无超时 `join()`
- 已有阻塞 `recv` 必须设 **`SO_RCVTIMEO`**
- 关闭序列：`stop_token` → `join(timeout)` → 强制降级 → **记录残留**。让"退不出"变成**可诊断**，而不是挂死
- 已知限制：`std::stop_token` **无法强杀已进入 native 的 `sherpa-onnx Run()`**，只能放弃等待并标记——需配合超时设计

### 接口

- DSP 切片签名用 **`std::span<const float>`**，替代 `(const float*, size_t)`
- 非实时路径（配置 / ADB / 管道）错误返回 **`std::expected<T, AppError>`**
- 事件唤醒用 **`std::atomic<T>::wait / notify_all`**，替代手写 Event 对象

### 实时路径红线（RT）

WASAPI 回调与音频渲染线程内：**禁止任何堆分配**、**禁止构造带 `std::string` 的错误对象**；错误一律用纯标量 `enum class` 或 trivially_copyable 定长结构。

## UI 规则

- `.slint` 文件统一放 **`ui/`**
- 改完 `.slint` 必须跑 **`slint-viewer --check`** 校验语法，并 **`--screenshot` 看渲染结果**——未看过渲染不得宣称完成
- **`AboutSlint` widget 不可删除**：已选定 Slint Royalty-free 2.0 许可，其义务是"披露使用了 Slint"。移除披露 → 免版税档不成立 → 需转商业许可
- UI 线程**不得** `extern` 引擎的全局 atomic，只通过 `Command` / `StateSnapshot` 交互
- Slint 窗口**按需创建、关闭即销毁**：托盘常驻时不加载任何 UI

## 分层规则

```
voxtype_core     纯逻辑，可独立编译出离线测试可执行文件；不依赖 Windows API
voxtype_platform Windows 专属：WASAPI / CoreAudio / ADB / socket / 注册表
voxtype_ui       Slint 界面
voxtype_app      线程编排与生命周期
android_app      Java，不重写
```

- **core → platform 反向依赖 = 0**（硬约束）
- **ui 不得 extern 全局**；**platform 不得反向依赖 app**
- 新增配置项必须同时落进字段表与配置结构，**不得**散落多处

## 守护边界（Guardrails）

| 脚本 | 覆盖 |
|---|---|
| `scripts/validate_build_layout.ps1` | `build/` 布局白名单 + `runtime-manifest.json` hash 校验 |
| `scripts/check_architecture.ps1` | 架构不变量：文件行数、`extern` 计数、C++23 红线、Slint 披露（ratchet 只减不增） |
| `.input.sha256` 伴生文件 | 同版本产物保护：改代码未升版本且未重跑 `--package*` → 打包失败 |
| `packaging/windows/ProductIdentity.wxi` | 冻结的永久 GUID（UpgradeCode）；改动会断裂升级链 |

**每次构建都会跑架构守卫**（挂接在 `build.bat` 主流程）。人工绕过需在提交信息中说明理由。

## Version Bump

Version is unified in `src/version.h` (`APP_VERSION` macro). On each bump, modify this checklist item by item:

| # | File | Location/Line | Format |
|---|------|--------------|--------|
| 1 | `src/version.h` | `#define APP_VERSION` | `"x.y.z"` |
| 2 | `android_app/app/build.gradle` | `versionName` | Sync `APP_VERSION` string |
| 3 | `android_app/app/build.gradle` | `versionCode` | +1 (last=14) |

`src/tray_icon.cpp` auto-syncs via `#include "version.h"`, no manual change needed.
CMake/MSI versions are derived automatically (`CMakeLists.txt` → `runtime-manifest.json` → MSI `ProductVersion`).

## Build & Run

```cmd
build.bat                        # Incremental build + install (DPDFNet by default)
build.bat --rebuild              # Clean rebuild
build.bat --rnnoise-only         # Single-exe payload without DPDFNet
build.bat --package              # Build + Portable (.7z) + MSI
build.bat --dpdfnet --test-dpdfnet  # Build + run DPDFNet smoke tests
build\run\x64-release\voxmic.exe                 # Launch (tray background)
build\run\x64-release\voxmic.exe --list-devices  # List devices
```

完整命令、签名、Android 构建与 keystore、DPDFNet smoke → **`BUILD.md`**。

构建/重启前先检查是否有 `voxmic.exe` 正在运行；若有，直接结束该 VoxMic 进程，不要等用户手动退出，也不要结束无关进程。

## Source File Structure

> 完整职责表见 **`ARCHITECTURE.md`**，此处只列重构相关要点。

```
src/
+-- main.cpp                    入口 / 消息泵 / bridge+monitor 线程（20+ 全局 atomic → 待解耦）
+-- settings_dialog.cpp         1458 行 Win32 GDI 自绘 → 重构为 Slint
+-- dsp/dpdfnet_processor.cpp   728 行手写 LoadLibrary/GetProcAddress/FIFO → 抽象后端
+-- dsp/rnnoise/                官方 RNNoise v0.2 C 源码 vendored（27 文件，不动）

ui/                             【新增】Slint 界面文件（重构引入）。**在仓库根目录，不在 src/ 下** —— `check_architecture.ps1` 只扫根目录 `ui/`，放错位置会让许可披露检查静默失效
scripts/
+-- validate_build_layout.ps1   build/ 白名单 + runtime-manifest hash 校验
+-- check_architecture.ps1      架构不变量守卫（ratchet）
packaging/windows/ProductIdentity.wxi  冻结的永久 GUID，禁止改动
```
