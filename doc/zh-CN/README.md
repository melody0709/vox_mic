# VoxMic

**简体中文** | [English](../../README.md)

将 Android 手机麦克风用作 Windows 系统麦克风，通过 ADB + VB-CABLE + Raw WASAPI 实现。支持按需激活：有 Windows 应用使用 CABLE Output 时才推流，空闲时不走 DSP。降噪后端可在内置 RNNoise 与可选的 DPDFNet 48 kHz 模型之间切换。

## 工作原理

```
Android 手机麦克风 → [VoxMic Source App] → ADB → 本程序 → VB-CABLE → Windows 应用
                                                              ↓
                                    [Phase 3] 事件驱动门控 ← MicUsageMonitor (COM 回调)
                                    [DSP] RNNoise/DPDFNet → HPF → EQ → Comp → Limiter
```

## v0.6.4 DPDFNet 稳定性优化

修复 DPDFNet 流切换时 worker 可能清掉新 epoch 首块的问题，并增加输出存活监控：每次 reset 最多容忍 4 个 10ms 的模型预热静音块；稳态连续 3 个 block 无输出时自动降级到 RNNoise，避免 worker 卡住后持续整块静音。设置页和周期诊断会显示 degraded 状态，点击 OK 可重试仍可用但卡住的 worker；真正的 session 硬失败需要重新 prepare。

## v0.6.2 修复

修复切换 RNNoise / DPDFNet 后，降噪后端状态文字未清除旧内容而发生重叠的问题；状态控件改为不透明重绘，并为两行可用性提示预留高度。

## v0.6.1 重点变化

| 特性 | 说明 |
|------|------|
| **降噪后端可切换** | 默认继续使用 RNNoise，也可以在 DSP 设置中选择 DPDFNet。 |
| **流式 DPDFNet 适配** | 固定使用 48 kHz / 480-sample 模型，通过 worker + FIFO 运行，不阻塞 WASAPI 渲染线程。 |
| **安全回退** | DLL、模型、ABI 符号、初始化失败或 worker 卡住时，程序继续使用 RNNoise。 |
| **可重建运行载荷** | 固定版本的 DPDFNet runtime、模型、C API 头文件、metadata 和 notices 位于 `third_party/dpdfnet`；删除 `build/` 后仍可重新生成。 |

## v0.5.0 核心特性

| 特性 | 说明 |
|------|------|
| **事件驱动 Monitor** | `IAudioSessionNotification` + `IAudioSessionEvents` 回调，零轮询零 COM 开销 |
| **Demand Mode 开关** | 右键托盘菜单控制按需激活，持久化到注册表 |
| **Socket 按需连接** | Always Hot OFF 时空闲 5s 断连，Android AudioRecord 停止，零耗电；重连仅 0.3-0.8ms |
| **Always Hot 开关** | 右键托盘菜单控制 socket 常连接，持久化到注册表 |
| **按需激活延迟** | ~200ms 冷启动，事件驱动即时检测 |
| **空闲 CPU 0-0.1%** | 事件驱动 + DSP 按需跳过，与 Python 版本持平 |
| **RNNoise 神经网络降噪** | 官方 xiph/rnnoise v0.2，3 层 GRU，22 Bark 频段独立降噪，BSD-3 |
| DSP 管线 | HPF 80Hz + 6-band EQ + RMS Compressor + Peak Limiter |
| 低延迟 | **~40ms** (实测) |

## 系统要求

- Windows 10/11
- Android 设备 (已开启 USB 调试)
- [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) 已安装
- ADB 已加入 PATH

## 使用

直接启动，程序自动隐藏到系统托盘：

```cmd
build\run\x64-release\voxmic.exe
```

- **左键托盘图标** → 弹出设置窗口
- **右键托盘图标** → Demand Mode / Always Hot / Settings / Exit 菜单
- **关闭窗口 [X]** → 隐藏到托盘（不退出）
- 在 Windows 应用中选择 **CABLE Output** 作为麦克风。

## 构建

### Windows

```cmd
build.bat

git lfs pull
build.bat --dpdfnet
build.bat --dpdfnet --test-dpdfnet
```

需要 Visual Studio 2022 (C++ 桌面开发)。

`build.bat` 生成 RNNoise-only 开发载荷。可选 DPDFNet 载荷保存在 `third_party/dpdfnet/`，并使用 Git LFS 管理；clone 后先执行一次 `git lfs pull`，将模型和 DLL 实体化。`build.bat --dpdfnet` 会校验仓库内文件，并把它们暂存到 `build/cmake/x64-release/_deps/dpdfnet` 后再配置 CMake，因此清空 `build/` 后也可以离线重新生成。仅当仓库依赖目录不可用时，准备脚本才保留经过 SHA-256 校验的下载/缓存回退路径。打包可选后端使用 `build.bat --dpdfnet --package`。

仓库内载荷包括 sherpa-onnx C API 头文件、三个 Windows x64 runtime DLL 和 `dpdfnet2_48khz_hr.onnx`；对应 SHA-256 记录在 `third_party/dpdfnet/metadata.json`。第三方许可证说明位于 `third_party/DPDFNET_THIRD_PARTY_NOTICES.txt`。

### Android App

```cmd
cd android_app
.\gradlew.bat assembleDebug --no-daemon --console=plain
adb -s <serial> install -r "app\build\outputs\apk\debug\VoxMic_Source-v0.6.4.apk"
```

## 性能

| 指标 | v0.6.4 |
|------|--------|
| CPU 空闲 | **0-0.1%** |
| CPU 激活 | ~0.1% (DSP) |
| 内存 | ~15 MB |
| 端到端延迟 | **~40ms** |
| 冷启动延迟 | **~200ms** |
| 二进制 | ~1.5 MB |

参考机器上的 DPDFNet streaming smoke test 显示 worker 推理 EMA 约 1.7ms/480-sample block；这是适配器测量值，不代表所有 Windows 机器的端到端延迟承诺。pipeline switch smoke 还覆盖 RNNoise ↔ DPDFNet 切换、重复 epoch reset 和 worker 卡住时的降级路径。

## 延迟预算

| 组件 | 延迟 |
|------|------|
| Android ADC + HAL | ~10ms |
| AudioRecord read (480fr) | ~10ms |
| ADB + Socket | ~2ms |
| 环形缓冲 | ~10ms (1-2 块) |
| RNNoise + EQ + Comp + Lim | ~50µs |
| DPDFNet worker 推理 | smoke test EMA 约 1.7ms；reset 预热最多保留 4 个 10ms 静音块，超时安全回退 |
| WASAPI 缓冲 | ~11ms |
| VB-CABLE | ~3ms |
| **总计** | **~40ms** |

## 管线效果

| 阶段 | 参数 | 目的 |
|------|------|------|
| RNNoise | 22 频段 GRU 神经网络 | 背景降噪 + 人声保留 |
| DPDFNet | 48 kHz 在线 ONNX 模型，worker + FIFO + watchdog | 可选降噪后端；`NR Strength` 仅对 RNNoise 生效；worker 卡住时安全使用 RNNoise |
| HPF 80Hz | 12dB/oct | 切除风噪/震动 |
| Bass Cut | 120Hz shelf + 250Hz 衰减 (可调 -6~0dB) | 减少浑浊 |
| Presence | 2.5kHz + 3.2kHz 提升 (可调 0~6dB) | 辅音清晰度 |
| Compressor | 3:1, 5ms attack, 50ms release | 响度均匀 |
| Limiter | -1dBFS ceiling | 防削波 |

## 文档

[ARCHITECTURE.md](ARCHITECTURE.md) | [AGENTS.md](AGENTS.md) | [CHANGELOG.md](CHANGELOG.md) | [FUTURE_ROADMAP.md](FUTURE_ROADMAP.md) | [plan/completed/](../../plan/completed/) / [.plan/completed/](../../.plan/completed/) (历史计划)
