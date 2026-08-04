# 计划书：开机自启动 + MSI 安装器 + Portable 便携版

> 参考：`D:\GITHUB_melody0709\VoxType`（同作者的姊妹项目，已实现完整 CMake/WiX/Portable 流水线）
> 状态：待实施 | 创建日期：2026-08-04 | 当前版本：`APP_VERSION "0.5.3"`

## 目录

1. 目标与关键决策
2. 背景与现状（VoxMic 现状 / VoxType 参考摘要）
3. Phase 1 — 开机自启动
4. Phase 2 — portable.flag 路径分流
5. Phase 3 — CMake 迁移
6. Phase 4 — Portable + MSI 打包
7. build/ 目录布局
8. 版本管理
9. 工具链依赖
10. 验证策略
11. 文件清单
12. 风险与决策记录（影响级别定义见本节）
13. 实施顺序
- 附录 A — VoxType 关键参考文件路径

---

## 1. 目标

为 VoxMic 增加三项分发与驻留能力：

1. **开机自启动** — 用户可在托盘设置窗口勾选「开机启动」，写入 HKCU Run 键
2. **MSI 安装器** — perMachine 安装包，支持 MajorUpgrade、保留安装目录、开始菜单快捷方式
3. **Portable 便携版** — 7z 压缩包，EXE 旁 `portable.flag` 标记，数据随 EXE

三项中，开机自启动独立可先行交付；Portable 与 MSI 共享规范载荷，且 `portable.flag` 路径分流是 MSI 的前置条件（MSI 装到 ProgramFiles 后 config.ini 不可写）。因此采用 CMake 化构建系统作为打包基础。

### 关键决策（已确认）

| 决策项 | 选择 | 理由 |
|--------|------|------|
| 构建系统 | 完全迁移到 CMake，镜像 VoxType | cmake --install 产出规范载荷是 Portable/MSI 共享前置 |
| 计划结构 | 一份分阶段计划 | 三特性耦合（portable.flag 是 MSI 前置），分阶段可独立验证 |
| 代码签名 | 暂不签名，脚本预留 --require-signing 钩子 | 当前无证书，先打通流程，产物名带 -unsigned |

---

## 2. 背景与现状

### 2.1 VoxMic 现状

- **构建**：`build.bat` 直接 `cl` 编译到 `build/voxmic.exe`（单 exe，无 DLL/模型/外部依赖）
- **配置**：`config.ini` 硬编码 EXE 旁（`src/config.cpp:9-19`，`getModuleFileNameA` + 拼接）
- **版本**：`src/version.h` 单一源 `APP_VERSION "0.5.3"`，`tray_icon.cpp` 自动 include 同步
- **托盘/设置**：已有 `tray_icon.cpp` 右键菜单 + `settings_dialog.cpp` 无模式（modeless）窗口（`createSettingsWindow` 返回 HWND）
- **链接库**：ws2_32 ole32 mmdevapi shell32 advapi32 comctl32 gdi32（`build.bat:65`）
- **无开机自启、无安装器、无 portable 标记**

### 2.2 VoxType 参考实现摘要

| 特性 | VoxType 实现 | 可复用性 |
|------|-------------|---------|
| 开机自启 | `src/core/startup_registration.{h,cpp}` 写 HKCU Run，UI 复选框事务性保存 | 直接移植，业务无关 |
| Portable | EXE 旁 `portable.flag` 空文件检测，`MutableDataDir()` 分流 EXE 目录 vs `%LOCALAPPDATA%` | 模式直接复用，VoxMic 仅 config.ini 一项更简单 |
| MSI | `packaging/windows/` WiX v4 SDK 工程 + `scripts/` PowerShell 生成/校验/打包 | 脚本框架复用，GUID 为 VoxMic 重新生成 |
| 构建系统 | CMakeLists.txt + CMakePresets.json + cmake/ install component | 模式复用，VoxMic 单 exe 载荷更简单 |

VoxType 关键工程原则（直接采纳）：
- 注册表是开机自启唯一真相，**不**在 config 复制 bool
- MSI 不自动创建开机自启、不拥有 LocalAppData 数据、不递归删未知文件
- 数据路径迁出安装目录是 MSI 前置，不能后置
- UUID v5 派生 WiX GUID（可复现、可追溯），避免随机 GUID 导致升级残留
- 同版本资产保护：input digest + .input.sha256 sidecar，防「改代码不提版本号就重发」

---

## 3. Phase 1 — 开机自启动（独立，先交付）

### 3.1 新增文件

**`src/startup_registration.h`** — 忠实移植自 VoxType `src/core/startup_registration.h`，仅将 `VoxType` 前缀改为 `VoxMic`、Run 键值名改为 `VoxMic`，API 签名保持一致：

```cpp
#pragma once

#include <windows.h>

#include <cstddef>
#include <string>

// Windows Run-key values are command lines and are limited to 260 characters.
constexpr size_t kStartupRegistrationCommandLineMaxChars = 260;

struct StartupRegistrationState {
    bool registered = false;
    bool pointsToCurrentExecutable = false;
    std::wstring commandLine;
    DWORD error = ERROR_SUCCESS;

    bool Succeeded() const { return error == ERROR_SUCCESS; }
};

// 注册表是唯一真相。不复制到 config.ini，避免与 Windows 实际 Run 注册漂移。
StartupRegistrationState QueryVoxMicStartupRegistration();

// 添加/更新/删除当前用户的 VoxMic Run 值。
// Portable 目录被移动后需更新（而非跳过）已有值。
DWORD SetVoxMicStartupRegistration(bool enable);

std::wstring BuildStartupRegistrationCommandLine(const std::wstring& executablePath);
bool IsStartupRegistrationCommandLineValid(const std::wstring& commandLine);
```

**`src/startup_registration.cpp`** — 实现细节同 VoxType，核心常量：

```cpp
constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"VoxMic";
```

内部函数 + 公开 API（与 VoxType 一一对应）：
- `CurrentExecutablePath()` — 动态扩容 `GetModuleFileNameW`（从 MAX_PATH 翻倍到 32768，不假设 MAX_PATH）
- `BuildStartupCommandLine(exePath)` — 返回 `"\" + exePath + "\""`（公开）
- `IsStartupRegistrationCommandLineValid(cmd)` — 长度 ≤ `kStartupRegistrationCommandLineMaxChars`（公开）
- `WriteStartupRegistration()` — `RegCreateKeyExW(HKEY_CURRENT_USER, ..., KEY_SET_VALUE)` + `RegSetValueExW(... REG_SZ ...)`
- `RemoveStartupRegistration()` — `RegOpenKeyExW` + `RegDeleteValueW`，值不存在视为成功
- `QueryVoxMicStartupRegistration()` — 读 HKCU Run 值，`CompareStringOrdinal(... TRUE)` 大小写不敏感比较，识别 Portable 移动后旧值失效；返回 `StartupRegistrationState`（含 `DWORD error`）
- `SetVoxMicStartupRegistration(bool)` — 返回 `DWORD`（`ERROR_SUCCESS` 表成功）；开启时若指向旧路径则更新；关闭时只删 VoxMic 值

链接 `advapi32.lib`（已在 [build.bat:65](file:///d:/GITHUB_melody0709/vox_mic/vox_mic/build.bat#L65)，CMake 阶段同样链接）。不要求管理员权限（HKCU 而非 HKLM）。

### 3.2 设置窗口 UI

修改 [settings_dialog.cpp](file:///d:/GITHUB_melody0709/vox_mic/vox_mic/src/settings_dialog.cpp)：

- 新增控件 ID 于 [settings_dialog.cpp](file:///d:/GITHUB_melody0709/vox_mic/vox_mic/src/settings_dialog.cpp) 文件头 `#define` 区（遵循 VoxMic 现有约定 — 现有 24 个控件 ID 2001-2024 均在此定义，[resource.h](file:///d:/GITHUB_melody0709/vox_mic/vox_mic/src/resource.h) 仅含图标 ID 101-104）：`#define IDC_START_WITH_WINDOWS 2025`
- 创建 `BS_AUTOCHECKBOX`：「Start VoxMic when I sign in to Windows」
- 下方提示标签：「Uses your Windows account startup list. Moving a Portable copy is corrected when you save.」
- `RefreshStartupRegistrationControl(hwnd, reportError)` — WM_INIT_DIALOG 末尾 + 每次打开设置都调用，重新查注册表；检测到「已注册但指向旧路径」显示状态提示
- `SaveStartupRegistrationControl(hwnd)` — 调用 `SetVoxMicStartupRegistration(enable)`；失败弹 MessageBox、保留当前页面状态、返回 false
- **事务性保存**：保存流程第一行调用 `SaveStartupRegistrationControl`，注册表变更成功后才提交 config.ini 和 DSP 原子量；失败则不提交任何 UI 变更

### 3.3 编译集成

- Phase 1 仍用 `build.bat`，在 cl 命令行加入 `src\startup_registration.cpp`
- **过渡说明**：此 build.bat 改动是临时的。Phase 3 重写 build.bat 为 CMake 入口时，该源文件转入 `CMakeLists.txt` 的 `add_executable` 源列表（见第 5.2 节），cl 直编路径随之移除

### 3.4 验收标准

- [ ] 设置窗口勾选→保存→重启 Windows→VoxMic 自动启动到托盘
- [ ] 取消勾选→保存→重启→不启动
- [ ] Portable 移动目录后打开设置，显示路径不匹配提示，再次保存→Run 值更新为新路径
- [ ] config.ini 中**无**开机自启相关字段（注册表为唯一真相）
- [ ] 不要求管理员权限（HKCU）

---

## 4. Phase 2 — portable.flag 路径分流（MSI 前置）

### 4.1 问题

VoxMic `config.ini` 硬编码 EXE 旁（`config.cpp:9-19`）。MSI 装到 `ProgramFiles\VoxMic` 后该目录普通用户不可写，`WritePrivateProfileStringA` 会静默失败。必须在引入 MSI 前完成路径分流。

### 4.2 新增文件

**`src/runtime_paths.h`**

```cpp
#pragma once
#include <string>

namespace runtime_paths {
    // EXE 所在目录（不可变运行时资产目录）
    std::wstring ExecutableDir();

    // 检测 EXE 旁是否存在 portable.flag 空文件
    bool IsPortableMode();

    // 可变数据目录：Portable→EXE 目录；Installed→%LOCALAPPDATA%\VoxMic
    std::wstring MutableDataDir();

    // config.ini 完整路径 = MutableDataDir() + "\config.ini"
    std::wstring ConfigPath();
}
```

**`src/runtime_paths.cpp`**

```cpp
constexpr wchar_t kPortableFlagName[] = L"portable.flag";
constexpr wchar_t kAppName[] = L"VoxMic";

std::wstring ExecutableDir() {
    // 动态扩容 GetModuleFileNameW，取目录部分
}

bool IsPortableMode() {
    return PathExistsAsFile(ExecutableDir() + L"\\" + kPortableFlagName);
}

std::wstring MutableDataDir() {
    if (IsPortableMode()) {
        return ExecutableDir();          // Portable: 数据随 EXE
    }
    // 安装版: %LOCALAPPDATA%\VoxMic
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        result = path;
        CoTaskMemFree(path);
    }
    if (result.empty()) return ExecutableDir();   // 兜底
    result += L"\\"; result += kAppName;
    EnsureDirectory(result);
    return result;
}
```

链接 `shell32.lib`（`SHGetKnownFolderPath`，已在 `build.bat:65`）。

### 4.3 修改 config.cpp

[config.cpp:9-19](file:///d:/GITHUB_melody0709/vox_mic/vox_mic/src/config.cpp#L9-L19) `getIniPath()` 改用 `runtime_paths::ConfigPath()`（char/wchar 转换）。其余读写函数不变。

### 4.4 旧配置迁移（一次性）

新增 `MigrateLegacyConfigIfNeeded()`，仅非 Portable 模式执行：
- 若 `MutableDataDir()\config.ini` 不存在，且 EXE 旁有旧 `config.ini`，则一次性复制到 LocalAppData
- 保留源文件（不删除、不覆盖已有目标）
- **边界**：EXE 旁无 `config.ini`（全新安装）时直接跳过，不报错；目标已存在时不覆盖

在 `main.cpp` 启动早期、`Config::load()` 之前调用。

### 4.5 路径对照

| 路径 | Portable | 安装版 |
|------|---------|--------|
| config.ini | EXE 旁 | `%LOCALAPPDATA%\VoxMic\config.ini` |
| portable.flag | EXE 旁（空文件） | 不存在 |

VoxMic 无日志目录、无模型目录，仅 config.ini 一项，比 VoxType 简单。

### 4.6 验收标准

- [ ] EXE 旁放空 `portable.flag` → config.ini 读写于 EXE 旁（旧行为）
- [ ] 无 `portable.flag` → config.ini 读写于 `%LOCALAPPDATA%\VoxMic\config.ini`
- [ ] 首次以安装版运行，EXE 旁旧 config.ini 被复制到 LocalAppData，原文件保留
- [ ] 删除 `portable.flag` 后运行，使用 LocalAppData 配置，不影响已有设置

---

## 5. Phase 3 — CMake 迁移（打包基础）

### 5.1 目标

完全镜像 VoxType 构建系统：CMake/Ninja 编译 → `cmake --install` 产出规范载荷 → 打包脚本消费规范载荷产出 .7z / .msi。`build.bat` 退化为构建总入口。

### 5.2 新增文件

**`CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.24)
project(VoxMic VERSION <从 version.h 解析> LANGUAGES C CXX)

# 从 src/version.h 解析 APP_VERSION "x.y.z" → major/minor/patch
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" _version_h)
if(_version_h MATCHES "APP_VERSION \"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"")
    set(VoxMic_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(VoxMic_VERSION_MINOR ${CMAKE_MATCH_2})
    set(VoxMic_VERSION_PATCH ${CMAKE_MATCH_3})
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(voxmic WIN32
    src/main.cpp
    src/wasapi_output.cpp
    src/device_enum.cpp
    src/socket_client.cpp
    src/adb_control.cpp
    src/tray_icon.cpp
    src/config.cpp
    src/settings_dialog.cpp
    src/mic_usage_monitor.cpp
    src/startup_registration.cpp        # Phase 1
    src/runtime_paths.cpp               # Phase 2
    src/dsp/rnnoise/celt_lpc.c
    src/dsp/rnnoise/denoise.c
    src/dsp/rnnoise/kiss_fft.c
    src/dsp/rnnoise/nnet.c
    src/dsp/rnnoise/nnet_default.c
    src/dsp/rnnoise/parse_lpcnet_weights.c
    src/dsp/rnnoise/pitch.c
    src/dsp/rnnoise/rnn.c
    src/dsp/rnnoise/rnnoise_tables.c
    src/dsp/rnnoise/rnnoise_data.c
    src/voxmic.rc
)

target_include_directories(voxmic PRIVATE src/dsp/rnnoise)
target_compile_options(voxmic PRIVATE /O2 /EHsc /W4 /WX- /wd4305 /wd4244)

# /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
set_target_properties(voxmic PROPERTIES
    LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")

target_link_libraries(voxmic PRIVATE
    ws2_32 ole32 mmdevapi shell32 advapi32 comctl32 gdi32)

include(cmake/VoxMicRuntime.cmake)
```

**`CMakePresets.json`**

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "x64-release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/cmake/x64-release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_INSTALL_PREFIX": "${sourceDir}/build/run/x64-release"
      }
    }
  ],
  "buildPresets": [{ "name": "x64-release", "configurePreset": "x64-release" }]
}
```

**`cmake/VoxMicRuntime.cmake`** — 定义 Runtime install component（载荷清单 = voxmic.exe）。VoxMic 单 exe 无 DLL，比 VoxType 简单。

**`cmake/WriteRuntimeManifest.cmake.in`** — 生成 `runtime-manifest.json`（path/size/sha256）。**用途**：作为 canonical payload 的权威清单，供 Phase 4 打包脚本计算 input digest 时参考（digest 计算以实际文件枚举为准，manifest 本身是构建产物校验凭据，不参与 digest）。

### 5.3 重写 build.bat

```
build.bat                          增量编译 + 运行载荷安装
build.bat --rebuild                清理后重建
build.bat --clean                  清理 cmake/run/artifacts/logs，保留 packages
build.bat --package                同时生成 Portable + MSI
build.bat --package-portable       仅 Portable
build.bat --package-msi            仅 MSI
build.bat --require-signing ...    签名发布（预留钩子，当前无证书）
```

流程：解析参数 → `vswhere` 定位 VS2022 → `cmake --preset x64-release` → `cmake --build` → `cmake --install` → `validate_build_layout.ps1` → 按需 `package_voxmic.ps1`。

### 5.4 build/ 目录布局（见第 7 节）

### 5.5 验收标准

- [ ] `build.bat`（无参数）产出 `build/run/x64-release/voxmic.exe`，行为与旧 cl 构建一致
- [ ] `build/run/x64-release/runtime-manifest.json` 生成，含 voxmic.exe 的 size/sha256
- [ ] `build.bat --rebuild` 清理后完整重建
- [ ] 旧 `build.bat` cl 直编逻辑移除（CMake 为唯一编译权威）

---

## 6. Phase 4 — Portable + MSI 打包（共享规范载荷）

### 6.1 Portable 打包

**`scripts/package_voxmic.ps1 -Mode Portable`**（适配自 VoxType，单 exe 载荷简化）

流程：
1. 复制 canonical payload（`build/run/x64-release/`）到 `staging/portable/VoxMic-v<ver>-win-x64-portable[-unsigned]/`
2. 写入空 `portable.flag` 文件（0 字节）
3. 7-Zip solid 压缩：`7z a -t7z -mx=9`
4. `Verify-PortableArtifact`：`7z t` 测试 → 解压到独立目录 → 校验 `portable.flag` 存在且 0 字节 → 逐文件 sha256 与 canonical payload 比对（唯一允许差异 = portable.flag）
5. 计算 input digest（canonical payload 每文件 size+sha256，排序后整体 sha256）
6. 同版本资产保护：若 `build/packages/` 已有同名资产，digest 相同则只验证不重产，不同则报错要求提升版本号
7. 原子移动到 `build/packages/`，写 `.sha256` + `.input.sha256` sidecar

产物：`VoxMic-v<ver>-win-x64-portable-unsigned.7z`

依赖：7-Zip（PATH 中需有 `7z.exe`）。

### 6.2 MSI 安装器

**`packaging/windows/ProductIdentity.wxi`** — 为 VoxMic 冻结全新永久身份（**不复用 VoxType 的 GUID**）

```xml
<?define VoxMicProductLineKey = "stable|x64|perMachine" ?>
<?define VoxMicUpgradeCode = "<新生成 GUID，永久不变>" ?>
<?define VoxMicProductCodeNamespace = "<新生成 GUID>" ?>
<?define VoxMicComponentNamespace = "<新生成 GUID>" ?>
```

注释明确：首发前一次性冻结，后续版本不得重新生成或替换。

**`packaging/windows/Package.wxs`**（WiX v4 schema）
- `<Package Name="VoxMic" Manufacturer="melody0709" Scope="perMachine" InstallerVersion="500" Compressed="yes">`
- `<MajorUpgrade Schedule="afterInstallInitialize" AllowSameVersionUpgrades="no" DowngradeErrorMessage="A newer version of VoxMic is already installed." />`
- `<MediaTemplate EmbedCab="yes" CompressionLevel="high" />`
- `INSTALLFOLDER` 默认 `ProgramFiles64Folder\VoxMic`
- AppSearch + RegistrySearch：升级前从 `HKLM\Software\VoxMic\InstallFolder` 恢复上次安装目录
- 独立 Component 写 `HKLM\Software\VoxMic\InstallFolder`
- 独立 Component 创建开始菜单快捷方式，目标 `[INSTALLFOLDER]voxmic.exe`
- `<Feature>` 引用 `VoxMicRuntimeFiles` ComponentGroup（运行时文件，自动生成）+ 上述两个 Component

**`packaging/windows/InstallerUi.wxs`** — 自定义欢迎对话框 `VoxMicWelcomeDlg`，含 Advanced 按钮（打开 `InstallDirDlg`）、带提升盾牌的 Install 按钮。引用 `WixUI_Common`。

**`packaging/windows/VoxMic.Installer.wixproj`** — WiX v4 SDK 风格项目，无需机器级 WiX 安装

```xml
<Project Sdk="WixToolset.Sdk/7.0.0">
  <PropertyGroup>
    <OutputType>Package</OutputType>
    <InstallerPlatform>x64</InstallerPlatform>
    <Cultures>en-US</Cultures>
    <SuppressIces>ICE60</SuppressIces>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="WixToolset.UI.wixext" Version="7.0.0" />
    <Compile Include="Package.wxs" />
    <Compile Include="InstallerUi.wxs" />
    <Compile Include="$(GeneratedWixDirectory)\RuntimeFiles.generated.wxs" />
  </ItemGroup>
</Project>
```

通过 `DefineConstants` 注入 `GeneratedWixDirectory`、`CanonicalPayloadRoot`、`VoxMicInstallerInputSha256`。

### 6.3 MSI 生成脚本

**`scripts/generate_wix_product_instance.ps1`**（适配自 VoxType）
- 输入 `ProductIdentity.wxi` + 三段版本，输出 `ProductInstance.generated.wxi`
- 校验 `ProductIdentity.wxi` 四个值与脚本内硬编码期望值一致（任何漂移抛错）
- UUID v5（RFC 4122）从 `VoxMicProductCodeNamespace` + `"stable|x64|perMachine|<version>"` 派生 `ProductCode`
- UUID v5 派生 `InstallFolderRegistryComponentGuid` 和 `StartMenuShortcutComponentGuid`

**`scripts/generate_wix_runtime_fragment.ps1`**（适配自 VoxType）
- 输入 canonical payload 目录 + ComponentNamespace，输出 `RuntimeFiles.generated.wxs`
- 递归枚举 payload，每文件用 sha256(相对路径) 派生稳定 `fil_`/`cmp_` ID，UUID v5 派生 Component GUID
- 每文件独占一个 Component（MSI 升级最小单位）
- 拒绝 reparse point、绝对路径、`..`、控制字符、大小写碰撞

**`scripts/package_voxmic.ps1 -Mode Msi`**（与 Portable 共用同一脚本）

MSI 流程：
1. 生成 `ProductInstance.generated.wxi` + `RuntimeFiles.generated.wxs`
2. 计算 installer input digest（canonical payload 每文件 size+sha256 + WiX 源文件 + 两个生成脚本 sha256，排序后整体 sha256）
3. 同版本资产保护（同 Portable）
4. `Build-Msi`：`dotnet restore` + `dotnet build` VoxMic.Installer.wixproj
5. `Assert-MsiContract`：WindowsInstaller COM 打开 MSI 数据库，校验 ProductVersion、ALLUSERS=1、WIXUI_INSTALLDIR、必需对话框存在、ControlEvent 链、INSTALLFOLDER 父项、AppSearch + RegLocator 64-bit HKLM、InstallExecuteSequence 顺序、Upgrade 表拒绝同版本升级
6. `msiexec /a` 管理安装解包 → 找到 voxmic.exe 所在目录
7. `Assert-SamePayload`：逐路径、size、sha256 与 canonical payload 比较
8. 可选 `Sign-AndVerify`（预留钩子，当前跳过）
9. 原子发布到 `build/packages/`，写 sidecar

产物：`VoxMic-v<ver>-win-x64-unsigned.msi`

### 6.4 边界约束（沿用 VoxType）

- MSI **不**自动创建开机自启 HKCU 值（perMachine 安装器不替不同用户擅自启用登录启动）
- MSI 升级保持 INSTALLFOLDER（AppSearch 恢复目录），已注册的 Run 命令在 N→N+1 后仍指向正确 EXE
- MSI **不**拥有 `%LOCALAPPDATA%\VoxMic` 数据，卸载不删该目录
- MSI **不**递归删除 INSTALLFOLDER 下未知文件（仅移除 MSI 拥有的 Component）

### 6.5 验收标准

- [ ] `build.bat --package` 产出 `.7z` 和 `.msi` + 各自 `.sha256` / `.input.sha256` sidecar
- [ ] Portable .7z 解压后含空 `portable.flag`，运行时 config.ini 读写于 EXE 旁
- [ ] MSI 安装到 `ProgramFiles\VoxMic`，开始菜单快捷方式可启动
- [ ] MSI 安装版运行时 config.ini 读写于 `%LOCALAPPDATA%\VoxMic\config.ini`
- [ ] MSI N→N+1 升级保留安装目录和 LocalAppData 数据
- [ ] MSI 卸载仅移除 MSI 拥有的文件，保留 LocalAppData
- [ ] 同版本号重打包（代码有改动）报错，要求提升版本号
- [ ] `msiexec /a` 解包内容与 canonical payload 逐文件 sha256 一致

---

## 7. build/ 目录布局

CMake 化后的规范布局（全部 gitignored，顶层白名单受 `validate_build_layout.ps1` 强制）：

```
build/
├─ cmake/x64-release/   CMake/Ninja 缓存、obj、install_manifest、临时 package-staging
├─ run/x64-release/     唯一可直接运行的规范运行载荷（cmake --install 产出）
│                       含 voxmic.exe + runtime-manifest.json
├─ packages/            已验证的 .7z / .msi + .sha256 sidecar；--clean 不删除
├─ artifacts/           验证/测试报告
├─ logs/
└─ README.txt           build.bat 写出的布局说明
```

**.gitignore 调整**：当前 `build/` 整目录忽略，保持不变。WiX generated 文件（`ProductInstance.generated.wxi`、`RuntimeFiles.generated.wxs`）实际生成在 `build/cmake/x64-release/package-staging/generated/`，已被 `build/` 规则覆盖，**无需新增 .gitignore 条目**。`packaging/windows/` 仅收纳手写源文件（`ProductIdentity.wxi`、`Package.wxs`、`InstallerUi.wxs`、`.wixproj`），不含生成物。

---

## 8. 版本管理

| # | 文件 | 位置 | 格式 | 是否需手动改 |
|---|------|------|------|--------------|
| 1 | [src/version.h](file:///d:/GITHUB_melody0709/vox_mic/vox_mic/src/version.h) | `#define APP_VERSION` | `"x.y.z"` — 唯一源 | 是 |
| 2 | `android_app/app/build.gradle` | `versionName` | 同步 APP_VERSION | 是 |
| 3 | `android_app/app/build.gradle` | `versionCode` | +1 | 是 |
| 4 | CMakeLists.txt | 从 version.h 解析 | major/minor/patch → project VERSION | 否（自动读取） |
| 5 | MSI ProductVersion | x.y.z | build 号不入 MSI 版本 | 否（由 CMake project VERSION 注入） |

`src/tray_icon.cpp` 已自动 include version.h，无需手动改。AGENTS.md 现有版本检查清单需补充 CMake/MSI 两项。

---

## 9. 工具链依赖

| 工具 | 用途 | 当前状态 |
|------|------|---------|
| VS2022 C++ x64 | 编译 | 已有（build.bat vswhere 定位） |
| CMake + Ninja | 构建系统 | VS2022 自带 |
| .NET SDK | `dotnet build` WiX 工程 | 需确认已装 |
| WiX v4 SDK (NuGet) | MSI 生成 | `WixToolset.Sdk/7.0.0`，NuGet 还原，无需机器级安装 |
| 7-Zip | Portable 压缩 | 需 PATH 中有 7z.exe |
| signtool（可选） | 代码签名 | 预留钩子，当前无证书 |

---

## 10. 验证策略

### 10.1 阶段验证（每 Phase 完成时）

| Phase | 验证手段 |
|-------|---------|
| 1 | 手动：勾选/取消→重启；Portable 移动后路径更新 |
| 2 | 手动：放/删 portable.flag → 检查 config.ini 路径；旧配置迁移 |
| 3 | `build.bat` 无参产出可运行 exe；`--rebuild` 完整重建 |
| 4 | `build.bat --package` 产出双包；Portable 解压验证；MSI 安装/升级/卸载验证 |

### 10.2 MSI 生命周期测试（Phase 4）

参考 VoxType `test_msi_lifecycle.ps1`（可选，需隔离 VM）：
- 安装 previous MSI → 校验 `HKLM:\Software\VoxMic\InstallFolder` → 在 `%LOCALAPPDATA%\VoxMic` 放保留标记 → 升级到 current MSI → 校验标记仍在、安装目录保留 → 卸载 → 校验仅 MSI 拥有文件被移除。

### 10.3 同版本资产保护

input digest + `.input.sha256` sidecar：代码改了但版本号没提就重发同版本包 → 报错。

---

## 11. 文件清单

### 11.1 新增

| 文件 | Phase | 说明 |
|------|-------|------|
| `src/startup_registration.h` | 1 | 开机自启 API |
| `src/startup_registration.cpp` | 1 | HKCU Run 实现 |
| `src/runtime_paths.h` | 2 | 路径分流 API |
| `src/runtime_paths.cpp` | 2 | portable.flag 检测 + MutableDataDir |
| `CMakeLists.txt` | 3 | CMake 构建 |
| `CMakePresets.json` | 3 | x64-release 预设 |
| `cmake/VoxMicRuntime.cmake` | 3 | Runtime install component |
| `cmake/WriteRuntimeManifest.cmake.in` | 3 | runtime-manifest.json 生成模板 |
| `scripts/package_voxmic.ps1` | 4 | Portable + MSI 共用打包 |
| `scripts/generate_wix_product_instance.ps1` | 4 | UUID v5 派生 ProductCode/GUID |
| `scripts/generate_wix_runtime_fragment.ps1` | 4 | 生成 RuntimeFiles.generated.wxs |
| `scripts/validate_build_layout.ps1` | 4 | build/ 布局校验 |
| `packaging/windows/ProductIdentity.wxi` | 4 | VoxMic 永久身份 GUID |
| `packaging/windows/Package.wxs` | 4 | MSI 主包定义 |
| `packaging/windows/InstallerUi.wxs` | 4 | 自定义欢迎对话框 |
| `packaging/windows/VoxMic.Installer.wixproj` | 4 | WiX v4 SDK 工程 |
| `packaging/windows/UPGRADE_CONTRACT.md` | 4 | 升级契约文档 |
| `build/README.txt` | 3 | build/ 布局说明（生成） |

### 11.2 修改

| 文件 | Phase | 改动 |
|------|-------|------|
| [src/config.cpp](file:///d:/GITHUB_melody0709/vox_mic/vox_mic/src/config.cpp) | 2 | `getIniPath()` 改用 `runtime_paths::ConfigPath()` |
| [src/settings_dialog.cpp](file:///d:/GITHUB_melody0709/vox_mic/vox_mic/src/settings_dialog.cpp) | 1 | 新增开机自启复选框 + 事务性保存 |
| [src/main.cpp](file:///d:/GITHUB_melody0709/vox_mic/vox_mic/src/main.cpp) | 2 | 启动早期调用 `MigrateLegacyConfigIfNeeded()` |
| [build.bat](file:///d:/GITHUB_melody0709/vox_mic/vox_mic/build.bat) | 3 | 重写为 CMake 总入口 |
| [AGENTS.md](file:///d:/GITHUB_melody0709/vox_mic/vox_mic/AGENTS.md) | 全 | 版本检查清单补充 CMake/MSI；Build & Run 更新 |

> [.gitignore](file:///d:/GITHUB_melody0709/vox_mic/vox_mic/.gitignore) 无需改动 — WiX generated 文件生成在 `build/` 下，已被现有 `build/` 规则覆盖（见第 7 节）。

### 11.3 不动

- `src/version.h` 模式不变（仍是唯一版本源）
- `android_app/` 不受影响（仅 versionName/versionCode 同步规则）
- DSP/rnnoise 源不动
- 现有托盘菜单行为不动（开机自启入口在设置窗口，不在托盘菜单）

---

## 12. 风险与决策记录

**影响级别定义**：高 = 阻塞交付或破坏现有功能；中 = 需额外处理但不阻塞；低 = 已有缓解，残余风险小。

| 风险 | 影响 | 缓解 |
|------|------|------|
| CMake 迁移破坏现有可用构建 | 高 | Phase 3 先验证无参 build 产出与旧 cl 一致，再删旧逻辑；可保留 build.bat.legacy 临时备份直到验证通过；验证不通过则回滚至 build.bat.legacy |
| WiX v4 SDK NuGet 还原失败 | 中 | wixproj 用 SDK 风格，`dotnet restore` 即可；无需机器级 WiX 安装 |
| 7-Zip 不在 PATH | 中 | `Find-SevenZip` 明确报错，提示安装 |
| Portable 移动后开机自启失效 | 低 | Phase 1 已设计路径不匹配检测 + 更新逻辑 |
| MSI perMachine 安装到 ProgramFiles，config.ini 不可写 | 高 | Phase 2 路径分流是 Phase 4 前置，阶段顺序保证 |
| 同名资产被覆盖 | 中 | input digest + 同版本资产保护机制 |

---

## 13. 实施顺序

```
Phase 1 (开机自启动)        ─── 独立，可立即交付验证
   │
Phase 2 (portable.flag 分流) ─── MSI 前置，独立可验证
   │
Phase 3 (CMake 迁移)         ─── 打包基础，需验证旧构建等价
   │
Phase 4 (Portable + MSI)     ─── 共享规范载荷，依赖 Phase 2+3
```

每个 Phase 完成后独立验收，验收通过再进入下一 Phase。Phase 1-2 不依赖 CMake，可在当前 build.bat 上先行；Phase 3-4 是打包体系搭建。

---

## 附录 A — VoxType 关键参考文件路径

| 特性 | 参考文件 |
|------|---------|
| 开机自启 | `D:\GITHUB_melody0709\VoxType\src\core\startup_registration.{h,cpp}` |
| 开机自启 UI | `D:\GITHUB_melody0709\VoxType\src\ui\settings.cpp` (L213-243, L901-904, L1642-1648, L2485) |
| Portable 检测 | `D:\GITHUB_melody0709\VoxType\src\audio\engine.cpp` (L30, L143-185) |
| WiX 工程 | `D:\GITHUB_melody0709\VoxType\packaging\windows\` (5 文件) |
| 打包脚本 | `D:\GITHUB_melody0709\VoxType\scripts\package_voxtype.ps1` |
| GUID 派生 | `D:\GITHUB_melody0709\VoxType\scripts\generate_wix_product_instance.ps1` |
| 构建总入口 | `D:\GITHUB_melody0709\VoxType\build.bat` |
| CMake | `D:\GITHUB_melody0709\VoxType\CMakeLists.txt` + `CMakePresets.json` + `cmake\` |
| 设计文档 | `D:\GITHUB_melody0709\VoxType\.plan\feat\build-msi-portable-autostart.md` |
