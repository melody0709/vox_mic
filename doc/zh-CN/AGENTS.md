# AGENTS.md

**简体中文** | [English](../../AGENTS.md)

将 Android 手机麦克风用作 Windows 系统麦克风，ADB + VB-CABLE + Raw WASAPI。按需激活：有应用使用 CABLE Output 时才推流，空闲不走 DSP。

## Plan 管理

- 新建 plan → `plan/ongoing/plan_<topic>.md`
- 实施完成 → 移到 `plan/completed/`
- **禁止**在项目根目录新建 `.md` plan 文件

## 版本号更新 (Bump Version)

版本号统一在 `src/version.h` (`APP_VERSION` 宏)。每次 bump 按此清单逐项修改：

| # | 文件 | 位置/行 | 格式 |
|---|------|---------|------|
| 1 | `src/version.h` | `#define APP_VERSION` | `"x.y.z"` |
| 2 | `android_app/app/build.gradle` | `versionName` | 同步 `APP_VERSION` 字符串 |
| 3 | `android_app/app/build.gradle` | `versionCode` | +1 (上次=12) |

`src/tray_icon.cpp` 已通过 `#include "version.h"` 自动同步，无需手动修改。

## 构建 & 运行

```cmd
build.bat                        # RNNoise-only 构建 + 安装
build.bat --dpdfnet              # 准备校验依赖并启用 DPDFNet
build.bat --dpdfnet --package    # 构建并打包可选 DPDFNet 载荷
build.bat --dpdfnet --test-dpdfnet # 构建并运行 DPDFNet smoke
build\run\x64-release\voxmic.exe                 # 启动 (托盘后台)
build\run\x64-release\voxmic.exe --list-devices  # 列出设备
```

可选 DPDFNet 依赖位于 `third_party/dpdfnet/`，模型和 DLL 使用 Git LFS 管理。clone 后先执行 `git lfs pull`，再运行 `build.bat --dpdfnet`；脚本会校验仓库内文件并将其暂存到 `build/cmake/x64-release/_deps/dpdfnet`。`build/` 是可清理的生成目录，清空后不会丢失依赖来源；只有仓库依赖目录缺失时才使用固定 SHA-256 的下载/缓存回退。

DPDFNet 验证目标（执行 DPDFNet 构建后）：
```cmd
build\cmake\x64-release\dpdfnet_smoke.exe build\run\x64-release build\run\x64-release\models\dpdfnet2_48khz_hr.onnx
build\cmake\x64-release\dpdfnet_fallback_smoke.exe build\run\x64-release build\run\x64-release\models\missing-for-test.onnx
build\cmake\x64-release\dpdfnet_pipeline_switch_smoke.exe build\run\x64-release build\run\x64-release\models\dpdfnet2_48khz_hr.onnx
build\cmake\x64-release\dpdfnet_failure_smoke.exe build\run\x64-release build\run\x64-release\models\dpdfnet2_48khz_hr.onnx
```

watchdog 无法取消永久不返回的原生 sherpa-onnx `Run()` 调用。当前实现不会在释放 denoiser/DLL 资源前 detach worker；在未来完成进程隔离适配器前，这仍是已知的退出限制。

Android App (SDK `D:\@APP\android-platform-sdk\android-sdk`, Gradle 8.7, JDK 17):
```powershell
cd android_app; .\gradlew.bat assembleDebug --no-daemon --console=plain
```

### Android Release 构建

```powershell
cd android_app; .\gradlew.bat assembleRelease --no-daemon --console=plain
```

输出: `VoxMic_Source-v<versionName>.apk`（由 `build.gradle` 自动命名，无需手动改名）。

需要 `keystore.properties` + `voxmic.keystore`（均为 gitignore，新 clone 缺失时需生成）：

```powershell
& "C:\Program Files\Java\jdk-17\bin\keytool.exe" -genkey -v `
  -keystore android_app/voxmic.keystore -alias voxmic `
  -keyalg RSA -keysize 2048 -validity 10000 `
  -storepass voxmic123 -keypass voxmic123 `
  -dname "CN=VoxMic, OU=Dev, O=VoxMic, L=N/A, ST=N/A, C=CN"
```

然后创建 `android_app/keystore.properties`:
```
storePassword=voxmic123
keyPassword=voxmic123
keyAlias=voxmic
storeFile=../voxmic.keystore
```

Windows 应用中选 **CABLE Output** 作为麦克风。托盘图标：左键=设置窗口，右键=菜单(Demand Mode / Always Hot / Exit)，关闭窗口=隐藏。

## 源文件结构

```
src/
├── main.cpp                     # 主线程 + monitor/bridge 线程
├── wasapi_output.h/cpp          # WASAPI 事件驱动渲染 (CABLE Input render 端点)
├── device_enum.h/cpp            # 设备枚举 (findVBCableDevice 等)
├── ring_buffer.h                # SPS 环形缓冲
├── socket_client.h/cpp          # TCP socket 客户端
├── adb_control.h/cpp            # ADB (CreateProcess + CREATE_NO_WINDOW)
├── tray_icon.h/cpp              # 系统托盘 + 右键菜单
├── config.h/cpp                 # config.ini 持久化 (20 字段)
├── settings_dialog.h/cpp        # 非模态设置窗口
├── mic_usage_monitor.h/cpp      # 逐会话 COM 事件 + 校准/防抖/fail-open 策略
├── mic_session_state.h          # 带身份的幂等会话状态跟踪器
└── dsp/
    ├── pipeline.h               # DSP 链 (RNNoise/DPDFNet→HPF→EQ→Comp→Limiter)
    ├── dpdfnet_processor.h/cpp  # 可选 DPDFNet worker/FIFO 适配器
    ├── biquad.h                 # BiQuad IIR
    └── rnnoise/ (27 files)      # 官方 RNNoise v0.2 (C 编译, 无外部依赖)
```

`third_party/dpdfnet/` 保存可选 DPDFNet 的固定依赖来源：模型和 Windows x64 runtime DLL 使用 Git LFS，C API 头文件、`metadata.json` 与许可证说明使用普通文本文件管理。`build/` 中的 `_deps`、运行载荷和测试产物均可删除后重新生成。

## 线程模型

```
main:      消息泵 + SetTimer(stats)
monitor:   独占 MTA COM + 逐会话回调 + 200ms 状态校准
bridge:    ADB 管理 + Socket recv → g_micRequested 门控 → ring buffer
render:    ring buffer pop → int16→float → DspPipeline → WASAPI write
```

## 关键参数

| 参数 | 值 | 位置 |
|------|-----|------|
| FRAMES_PER_BLOCK / BLOCK_SIZE | 480 / 960B | `wasapi_output.h:14-16` |
| SAMPLE_RATE | 48000 Hz | `wasapi_output.h:12` |
| RING_BUFFER_BLOCKS | 128 | `wasapi_output.h:17` |
| WASAPI 缓冲 | ~22ms (共享模式下限) | `wasapi_output.cpp:73` |
| 环形水位 | 3→2 | `main.cpp` |
| Android AudioRecord | 1× minBufSize | `RecordService.java` |
| 总延迟 | ~40ms | |

### 按需激活

| 机制 | 触发 | 延迟 | 开销 |
|------|------|------|------|
| 逐会话 `OnStateChanged` | COM 回调 | 即时 | 事件驱动 |
| 会话状态校准 | 枚举 + `GetState()` | 漏事件最多 200ms 修复 | 每秒 5 次 |
| 最后会话退出 | 无活跃会话 | 400ms 防抖 | 校准循环计时 |
| renderStallScore | render event 3×超时 | ~6s | 既有 render 保护 |
| Monitor 初始化失败 | COM/设备/session manager 失败 | 立即 fail-open | 重启前连续传音 |

| 阈值 | 值 |
|------|-----|
| Socket stall 断连 | 9s (90×100ms) |
| Socket 空闲断连 | 5s (500 blocks, AlwaysHot OFF) |
| Ring buffer reset | 每 50 blocks (0.5s) |
| Bridge 重连 | ~0.3-0.8ms QPC |

### DSP 管线 & 配置

DSP: 可选 RNNoise/DPDFNet 降噪 → HPF(80Hz) → EQ(6-band, Pres 0-6dB, Bass -6-0dB) → Comp(-18dBFS, 3:1) → Limiter(-1dBFS)

| 原子变量 | 用途 | 线程 |
|------|------|------|
| `g_gain` | 增益倍率 | bridge → render |
| `g_nrEnabled` / `g_eqEnabled` / `g_compressorEnabled` | DSP 开关 | bridge → render |
| `g_nrStrength` | NR 降噪强度 (0.3-0.95) | bridge → render |
| `g_denoiseBackend` | 请求后端 (`rnnoise`/`dpdfnet`) | settings → render |
| `g_denoiseEffectiveBackend` | 回退后的实际后端 | render → settings |
| `g_dpdfnetAvailable` | DPDFNet runtime/model/session 可用性 | render → settings |
| `g_dpdfnetDegraded` | DPDFNet worker 卡住并暂时降级到 RNNoise | render → settings |
| `g_denoiseResetEpoch` | 流/后端 reset 通知 | bridge/settings → render |
| `g_eqPresence` / `g_eqBassCut` | EQ 参数 | bridge → render |
| `g_micRequested` | 应用是否在捕获 | monitor → bridge |
| `g_micStreaming` | 是否在推流 | bridge → tray |
| `g_micOnTick` | 检测延迟时间戳 | monitor → bridge |
| `g_demandMode` | Demand Mode 开关 | tray → monitor/bridge |
| `g_alwaysHot` | Always Hot 开关 | tray → bridge |

配置 20 字段，config.ini 持久化。`syncDspAtomsFromConfig()` 在 `main.cpp`，启动和重连时调用。

### Monitor 三层检测

1. **逐会话 COM 事件** (`IAudioSessionNotification` + 每个采集会话一个 `IAudioSessionEvents` observer)：靶向 CABLE Output 采集端点（`EnumAudioEndpoints(eCapture)` 按名匹配，找不到时 fallback 默认端点）；回调路径只发布原子状态并唤醒 monitor 线程。
2. **200ms 状态校准** (`mic_usage_monitor.cpp`)：重新枚举会话并调用 `GetState()`，修复遗漏或乱序回调；最后一个会话 inactive 后有 400ms 退出防抖。信号音量不再用于判断会话活动。
3. **Render event 超时** (`wasapi_output.cpp`)：连续 3 次 `WaitForSingleObject` 超时 → `renderStallScore=3`，bridge 用 `effectiveActive = demandOff || (micRequested && !renderStalled)` 门控。Monitor 初始化失败时 fail-open，避免永久静音。
