# VoxMic：RNNoise / DPDFNet 可切换降噪后端方案

状态：实施完成，0.6.1 依赖归档与发布验证通过（2026-08-09）  
创建日期：2026-08-09  
目标版本：`0.6.1`

## 1. 目标

保留现有内置 RNNoise 作为默认、低资源、零外部依赖的降噪后端；新增一个设置项，使用户可以在 RNNoise 与 DPDFNet 之间切换。

设计目标：

1. 老用户升级后默认行为不变，旧 `config.ini` 自动按 RNNoise 解释。
2. 切换后仍使用当前 Android → ADB → TCP → ring buffer → WASAPI 的音频链路，不改变 Android 传输协议和 480-sample 音频块协议。
3. DPDFNet 不可用、模型缺失或运行时初始化失败时，自动回退 RNNoise，不能导致 VoxMic 启动失败或 Windows 麦克风中断。
4. 降噪后端只替换当前 Pipeline 中的“语音降噪”阶段，现有 EQ、压缩器、Limiter、Demand Mode 和 WASAPI 逻辑保持不变。
5. 运行时切换不能在 render thread 中执行长时间模型加载、DLL 加载或析构操作。

## 2. 调研结论

### 2.1 DPDFNet 适合本项目的原因

DPDFNet 的官方模型/运行时生态已经提供在线流式语音增强路径，C++ 侧可通过 sherpa-onnx 的 C/C++ API 使用 ONNX 模型。当前项目的输入是 48 kHz、单声道、16-bit PCM，每次 480 samples（10 ms）；官方 48 kHz DPDFNet 模型 `dpdfnet2_48khz_hr.onnx` 的模型元数据为：

- sample rate：48,000 Hz
- `n_fft`：960
- `window_length`：960
- `hop_length`：480
- 输入/输出频谱形状：`[1, 1, 481, 2]`
- recurrent state：56,436 个 float
- 模型文件约 10.6 MB

因此 MVP 不需要修改 Android 端采样率，也不需要把当前 48 kHz 音频先重采样到 16 kHz。16 kHz 的 `dpdfnet_baseline/dpdfnet2/dpdfnet4/dpdfnet8` 不作为本项目的默认实时模型；它们面向 16 kHz 处理或下游语音识别，不能仅凭文件名直接替代 48 kHz 模型。

### 2.2 与分享调研的校正

分享内容的大方向正确，但“DPDFNet 有独立官方 C++ 版本、直接依赖 sherpa-onnx 即可完成实时麦克风集成”的表述过于简化：

- DPDFNet 本质上是 ONNX 模型；C++ 集成需要 ONNX Runtime 或 sherpa-onnx 这类运行时。
- sherpa-onnx 当前确实提供 OnlineSpeechDenoiser 的 DPDFNet 示例，但它还负责 STFT、overlap-add、模型 state 和输入采样率处理，不能只把 RNNoise 调用替换成一个函数。
- DPDFNet 的公开对比数据只能作为候选依据，不能直接当作 VoxMic 的效果保证；实际效果会受 Android 硬件 NS/AEC/AGC、手机麦克风、环境噪声、增益和后级压缩影响。
- DPDFNet 不是 AEC 模型。回声消除仍由 Android `AcousticEchoCanceler` 或后续专门的 AEC 方案负责。

### 2.3 当前 VoxMic 的约束

当前降噪实现位于 `src/dsp/pipeline.h`，render thread 将每个 480-sample 块转换为 float 后调用 `rnnoise_process_frame`，随后依次执行 EQ、压缩器和 Limiter。RNNoise 模型以 C 源码和内嵌权重编译进 exe；当前 CMake install 只发布 `voxmic.exe` 和 runtime manifest。

Android 端已经可能启用系统 `NoiseSuppressor`、`AcousticEchoCanceler` 和 `AutomaticGainControl`。DPDFNet 上线后这些开关仍独立保留，不自动替用户关闭；效果验证必须至少覆盖“Android NS 开/关 × Windows DPDFNet/RNNoise”组合。

### 2.4 官方 C API 的实施约束

实施以固定版本 sherpa-onnx 的官方 C API 为准，并在 Phase 0 锁定头文件、DLL 和模型的同一版本。当前 API 契约必须按以下方式适配：

DPDFNet 在线支持属于较新的 sherpa-onnx 能力；Phase 0 必须确认选定的发行包/提交确实导出了在线 DPDFNet C API。若固定包只有离线 DPDFNet 或没有对应 online symbols，直接判定依赖不合格，不把离线接口混入实时 adapter。

- `Run()` 是有状态、非线程安全的调用；同一个在线降噪器只能由一个执行线程调用。
- `Run()` 在内部尚未形成可输出帧时可能返回空结果；不能把“每个输入块必然立即得到 480 个输出样本”写成 API 假设。
- 非空结果是 API 分配的音频对象，必须在复制样本后调用官方销毁函数；不能跨 DLL 使用 VoxMic 自己的 `free/delete`。
- `Reset()` 用于实时流断点、后端切换和重新启用 NR；`Flush()` 产生尾部数据，只能用于明确的流结束测试，实时重置时应丢弃尾部并清空 FIFO，不能让尾部进入下一次会话。

因此，DPDFNet adapter 的正式接口不能只有无参数 `init()`，至少要区分主线程 `prepare(runtimePath, modelPath)` 与单执行线程 `processBlock()/reset()`，并明确“可变长 API 输出 → 固定 480-sample 输出 FIFO”的契约。

## 3. 推荐架构

### 3.1 后端抽象

在 DSP 层引入窄接口，建议包含以下能力：

```text
DenoiseProcessor
  prepare(runtimePath, modelPath)
  processBlock(input[480], output[480])
  reset()
  isReady()
  name()
```

实现两个后端：

```text
RnNoiseProcessor   -> 现有 rnnoise_create / rnnoise_process_frame
DpdfnetProcessor   -> sherpa-onnx OnlineSpeechDenoiser + 48 kHz模型
```

`DspPipeline` 继续负责完整后级处理，但不再把 RNNoise 作为唯一的降噪实现。后端切换只影响第一阶段：

```text
int16 -> float -> selected denoiser -> EQ -> Compressor -> Limiter -> gain -> WASAPI
```

RNNoise 的 `nrStrength` 只对 RNNoise 生效；DPDFNet 没有与当前 slider 等价的强度参数，切换到 DPDFNet 时该 slider 应禁用并保留其 RNNoise 值。

### 3.2 DPDFNet 流式适配

DPDFNet 在线实现以 480 samples 为 hop，但 STFT 窗长为 960 samples，因此首个输出块存在内部启动延迟。适配器必须：

1. 每次向 OnlineSpeechDenoiser 输入一个 480-sample 块。
2. 将 API 返回的变长 float 输出放入内部 FIFO。
3. 每次 render 调用严格取出 480 samples；启动阶段不足部分用静音填充，不能改变 WASAPI 的固定输出节奏。
4. 在切换后端、停止/重新连接、Demand Mode 从 idle 恢复时调用 `reset()`，清理 STFT overlap-add 和 DPDFNet recurrent state。
5. 处理 flush/tail，退出或重置时不能把尾部数据泄漏到下一次会话。
6. 记录初始化、推理失败和回退原因；失败路径不得在音频线程重复刷屏。
7. 初始化后必须通过 C API 的 `SherpaOnnxOnlineSpeechDenoiserGetSampleRate()` 和 `SherpaOnnxOnlineSpeechDenoiserGetFrameShiftInSamples()` 检查 `SAMPLE_RATE`/`FRAMES_PER_BLOCK`；第一版不在实时 adapter 中支持任意 chunk 重组，契约不匹配就禁用 DPDFNet 并回退 RNNoise。
8. 适配器必须在 `Run()` 返回对象仍有效时把样本复制进自有 FIFO，然后立即调用官方销毁函数；若使用独立 worker，所有 API 对象的创建、调用和销毁都留在该 worker/准备线程，render thread 只读预分配的 480-sample 数据。

### 3.3 切换时序

推荐采用“主线程预加载 + render 边界切换”：

1. 在 `WASAPIOutput::init()` 完成、`wasapiOutput.start()` 之前，由主线程准备 `DspPipeline` 和两个后端实例；render thread 只接收已经 ready 的对象。
2. RNNoise 始终准备成功；如果 DPDFNet 运行时和模型存在，则主线程完成 DLL/ONNX session/model 初始化及一次 warm-up inference；资源缺失只记录 unavailable，不影响 RNNoise。
3. UI 保存配置时只更新目标后端原子值，不直接操作 DPDFNet 对象。
4. render thread 在 block 边界观察目标后端变化，先 reset 目标后端，再切换 active pointer。
5. 旧后端对象不在 render thread 中销毁；两个后端实例可在 `WASAPIOutput` 生命周期内复用，应用退出时再统一释放。
6. DPDFNet 初始化失败时把 effective backend 固定回 RNNoise，并在日志/设置界面提示。
7. 由 `g_denoiseResetEpoch`（或等价的无锁 reset 信号）统一通知 render thread：bridge 重连、idle→active、后端切换和 NR 重新启用都必须递增 epoch；render thread 发现 epoch 变化后 reset 当前/目标 denoiser 和输出 FIFO。

这样可以避免用户点击保存时发生 ONNX session 加载、DLL 加载或大块内存分配，也避免 render thread 在启动阶段承担初始化工作。代价是即使用户默认使用 RNNoise，只要 DPDFNet 资源可用，启动时也会额外加载 DPDFNet；若实测启动时间或内存不可接受，再单独设计后台懒加载和 ready 后切换，不能把懒加载直接放进音频线程。

#### 动态加载是硬约束

DPDFNet 路径不得把 sherpa-onnx 或 ONNX Runtime import library 直接链接进 `voxmic.exe`。否则可选 DLL 缺失时，Windows loader 可能在 `main()` 前就失败，RNNoise 回退逻辑无法执行。

实现应使用绝对路径 `LoadLibraryExW` 加受限 DLL 搜索目录（优先 `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` / `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`），再用 `GetProcAddress` 填充一个只包含所需 C API 的函数表；不得在音频线程加载 DLL。API 符号不完整、版本不匹配或任一依赖 DLL 无法加载时，整个 DPDFNet 能力标记为 unavailable，保持 RNNoise。

这也意味着 CMake 的 DPDFNet enabled 构建可以使用固定版本头文件/ABI 声明进行编译，但链接阶段不应产生 sherpa-onnx/ONNX Runtime 的硬导入依赖；Phase 0 必须用依赖缺失的运行目录验证“程序仍能启动并使用 RNNoise”。

## 4. 配置与设置界面

### 4.1 配置格式

新增一个持久化字段，建议使用可读字符串而不是数字：

```ini
DenoiseBackend=rnnoise
```

允许值：

- `rnnoise`：默认值，兼容旧配置。
- `dpdfnet`：使用 `dpdfnet2_48khz_hr.onnx`。

配置值表示用户请求的 backend；运行时另维护 effective backend/status。DPDFNet 资源不可用时不要静默把用户请求改写成 `rnnoise`，应保持请求值以便资源恢复后自动重试，同时在设置界面和日志显示“当前实际使用 RNNoise（DPDFNet unavailable）”。未知配置值才按 `rnnoise` 规范化。

未知值按 `rnnoise` 处理并回写规范值。第一版不把模型路径暴露为用户配置，统一从运行时目录解析：

```text
<exe-dir>\models\dpdfnet2_48khz_hr.onnx
```

后续如需替换模型，再增加模型版本/路径机制；不要让任意 config.ini 路径直接影响 MSI 安装目录权限或运行时加载安全。

### 4.2 UI

在 DSP 页的 Noise Reduction 区域增加：

```text
Noise Reduction Backend: [RNNoise | DPDFNet]
```

交互规则：

- `Noise Reduction` checkbox 仍是总开关。
- 选择 RNNoise：启用 NR Strength slider。
- 选择 DPDFNet：禁用 NR Strength slider，并显示“DPDFNet uses its own model strength”。
- DPDFNet runtime/model 不存在时显示明确提示，但不阻塞设置窗口；保存后请求值仍可保持 `dpdfnet`，实际后端回退 RNNoise。
- Reset to Defaults 恢复 RNNoise。
- 保存后立即生效，但切换在下一个完整 10 ms block 边界完成。

## 5. 外部依赖与发布方式

### 5.1 依赖选择

实现边界明确使用 sherpa-onnx **C API**，不使用跨 DLL 返回 `std::vector` 的 C++ API，避免 MSVC STL/CRT ABI 和内存释放边界问题。VoxMic 内部用一个很薄的 C++ adapter 持有 opaque `SherpaOnnxOnlineSpeechDenoiser*`，并严格调用官方 `DestroyDenoisedAudio`/`DestroyOnlineSpeechDenoiser` 释放对象。

建议使用 sherpa-onnx 的 Windows x64 shared Release no-tts 发行包作为 C API/ONNX Runtime 依赖。当前 VoxMic CMake 没有显式改变 MSVC runtime，Phase 0 应以实际 `dumpbin`/运行结果确认并固定 MD 变体；若改用 MT，必须同时把 CMake runtime 选择写死，不能保留 MD/MT 模糊状态。版本、下载地址和 SHA-256 全部固定，对应的模型固定为 DPDFNet 官方 48 kHz 模型，并单独固定 SHA-256。

发布决策：固定的模型、runtime DLL、C API 头文件、依赖 metadata 和许可证说明放入 `third_party/dpdfnet/`；模型和 DLL 使用 Git LFS，普通 Git 只保存指针。这样清空 `build/` 后仍能从仓库重新生成载荷，不依赖开发机上的历史缓存。准备脚本仍保留“仓库依赖目录缺失时”的固定 SHA-256 下载/缓存回退，用于旧工作树或特殊 CI 环境，但正常 clone + `git lfs pull` 路径优先使用仓库内依赖。

依赖组成：

```text
sherpa-onnx runtime DLL / pinned ABI headers（import library 仅供离线 smoke test 或符号核对，不进入最终 payload）
ONNX Runtime runtime DLL（由 sherpa-onnx 包提供或明确列为依赖）
models/dpdfnet2_48khz_hr.onnx
third-party license notices
```

Phase 0 还必须用 `dumpbin /dependents` 或等价工具列出 sherpa-onnx 的全部传递 DLL 依赖，并把实际需要的文件逐项加入 payload 白名单；不能只凭文件名假定“sherpa-onnx DLL 已经包含 ONNX Runtime”。

不采用静态链接作为第一版：官方静态包体积明显大于 shared 方案，会破坏当前 exe 小、可快速启动的产品特征。使用 shared runtime 后，当前“单 exe”描述需要更新为“主程序 + 可选 DPDFNet runtime/model payload”。RNNoise 路径仍可在没有这些外部资源时工作。

### 5.2 依赖准备

新增依赖准备脚本（名称可定为 `scripts/prepare_dpdfnet_deps.ps1`），职责：

1. 将固定版本 sherpa-onnx Windows x64 shared MD Release no-tts 包中实际需要的 runtime DLL 和 C API 头文件，以及固定版本 DPDFNet 48 kHz ONNX 模型，归档到 `third_party/dpdfnet/`；若实际选 MT，必须同步修改 CMake runtime 契约和本计划中的所有说明。
2. 对仓库内模型、DLL 和头文件逐项校验 SHA-256；失败立即停止，禁止使用未校验文件。
3. 由 `build.bat` 在 CMake configure 前调用，将经过校验的仓库依赖复制到 `build/cmake/x64-release/_deps/dpdfnet`；该目录是可删除的生成缓存。
4. 输出 CMake 可消费的 include/runtime/model 路径，并写入来源模式 metadata。
5. 支持 `VOXMIC_DPDFNET_DEPS_DIR` 或等价 CMake cache 覆盖，用于离线构建和 CI 缓存。
6. 当仓库依赖目录缺失时，才允许使用固定 URL、固定 SHA-256 的下载/缓存回退；音频运行阶段绝不下载依赖。

CMake 增加明确的能力开关和路径契约，例如 `VOXMIC_ENABLE_DPDFNET`、`VOXMIC_DPDFNET_DEPS_DIR`、`VOXMIC_DPDFNET_MODEL`。RNNoise-only 开发构建关闭能力开关时不应包含 DPDFNet 头文件、链接库或 UI 的可用状态；发布构建打开能力开关时必须在 configure/install 阶段验证依赖完整。

`build.bat` 必须提供明确的 DPDFNet enabled 入口（例如 `--dpdfnet`，或等价环境变量），并区分以下两种构建契约：

- RNNoise-only：无外部依赖也能 configure/build/run，设置界面不把 DPDFNet 显示为可用。
- DPDFNet payload：显式启用后必须先准备并校验依赖，configure/install/package 失败即停止，不能生成“看起来支持 DPDFNet 但运行时必然回退”的发布包。

构建默认仍应能在“依赖未准备”时编译 RNNoise-only 开发版本；发布包或显式启用 DPDFNet 时必须要求依赖完整，避免用户看到可选项却拿到必然失败的包。

### 5.3 Runtime / MSI / Portable

修改 CMake install 规则，将 DPDFNet 运行所需 DLL 和 `models/` 下模型安装到 `build/run/x64-release`。runtime manifest 必须包含这些文件及 hash；WiX runtime fragment 自动从 payload 生成，不手写每个文件的 GUID。

安装刷新必须处理能力切换后的旧 payload：当前 `VoxMicRuntime.cmake` 只删除 exe 和 manifest，实施时要清理由 VoxMic install 自己拥有的 DPDFNet DLL/model 路径，避免先构建 DPDFNet、再构建 RNNoise-only 时旧模型仍留在 `build/run` 并被 manifest/打包误收录。清理范围必须是明确的 runtime 子路径，不得扩大到用户配置目录。

Portable 与 MSI 使用同一个 runtime payload：

- Portable：exe、DLL、models 和 `portable.flag` 同目录/子目录分发。
- MSI：DLL 和 models 安装到 INSTALLFOLDER，模型使用只读资源路径。
- 卸载/升级：依赖文件由生成的 runtime fragment 管理，不能遗留旧模型或旧 DLL。
- 发行文档增加第三方许可证与模型许可证说明。

## 6. 线程、性能与故障处理

### 6.1 线程原则

- UI/main thread：只写 Config 和目标后端原子值。
- render thread：只处理当前已初始化的 backend；只做无锁 block 边界切换和音频处理。
- 不在 render thread 执行 LoadLibrary、ONNX session 创建、模型读取、文件 I/O 或旧 session 析构。
- DPDFNet 每个实例只由一个固定执行线程调用（实验性同步 MVP 可是 render thread，正式 worker 方案则只能是 DPDFNet worker），禁止多个线程复用同一个有状态 denoiser。

补充：官方 C API 的每次非空 `Run()` 结果需要单独销毁，通常意味着每个音频块存在 API 内部的对象分配。这个分配不能因为一次机器上的 p99 达标就视为实时安全。Phase 0 可以在独立 smoke test 线程测量 API；正式发布的 DPDFNet 路径应优先采用独立 DPDFNet worker + 有界 SPSC FIFO，让 render thread 不承担 API 对象分配/释放。若第一版暂时采用 render-thread 同步调用，只能作为明确标注的实验性阶段，不能直接用作最终验收架构。

### 6.2 资源/错误策略

启动时检查：

1. DPDFNet model 是否存在且可读。
2. sherpa-onnx / ONNX Runtime DLL 是否可加载。
3. 模型 profile、sample rate、hop length 是否符合 48 kHz/480-sample 契约。
4. session 是否能完成一次 warm-up inference。

任何一步失败：

- 保持 RNNoise effective backend。
- tray/console 输出一次可诊断错误（缺哪个文件、哪个 DLL、哪个模型校验失败）。
- 不改变 Android 连接、WASAPI 初始化和默认 NR 行为。

### 6.3 性能门槛

不能直接套用 DPDFNet 论文或第三方 DNSMOS 数字作为本项目验收标准。首轮实现需以实际 Windows x64 机器测量：

- render block 仍为 10 ms，DPDFNet 推理 p95 < 5 ms、p99 < 8 ms。
- 连续运行至少 30 分钟，`underruns=0`，无 socket stall/reconnect。
- 默认 RNNoise 的 CPU、延迟和启动行为没有回归。
- DPDFNet 模式端到端延迟记录在 stats/log 中；目标控制在可接受范围（初始预期比 RNNoise 增加约 10–20 ms，最终以实测为准）。
- 切换后不出现明显 click、重复块、丢块或长时间静音。

## 7. 验证计划

### 7.1 构建验证

1. 无 DPDFNet 依赖且 `VOXMIC_ENABLE_DPDFNET=OFF`：RNNoise-only 构建成功，默认运行正常。
2. 有完整依赖：CMake configure/build/install 成功，运行 payload 包含 DLL、模型和 manifest。
3. `--list-devices`、Demand Mode、Always Hot、Android 重连路径不受影响。
4. `scripts/validate_build_layout.ps1` 中当前“单 exe”注释/能力假设必须改为按构建能力校验：RNNoise-only 仍是单 exe；DPDFNet payload 允许经过白名单的 DLL、license notice 和 `models/` 文件，并逐项校验 manifest。Portable/MSI 运行时目录验证通过；同版本输入 digest 保护仍有效。

### 7.2 功能验证

1. 旧 config.ini 启动后选择 RNNoise。
2. UI 选择 DPDFNet、保存、重新打开设置，选项保持。
3. DPDFNet 模型存在时切换生效；切换回 RNNoise 后 RNNoise strength 恢复可调。
4. 删除/改名模型或 DLL：程序仍启动，明确提示并回退 RNNoise。
5. Demand Mode idle → active、socket disconnect → reconnect 后，DPDFNet state 正确 reset。
6. Windows 端 44.1/48/96 kHz CABLE mix format 下输出不变速、不爆音；模型输入仍保持 48 kHz，现有输出重采样逻辑继续生效。

7. 后端切换、NR 开关切换和流断点的 reset 必须同时清理 denoiser state、DPDFNet 输出 FIFO、EQ/压缩器/limiter 的连续状态；切换产生的启动静音或延迟要量化，目标是有界且不出现 click/重复块，而不是只验证配置值变化。

### 7.3 音质 A/B 验证

使用同一批原始录音和同一 Android 硬件设置，对比：

- RNNoise / DPDFNet
- Android NS 开 / 关
- 风扇/空调低频、键盘敲击、持续人声、多人背景声、风噪、静音段

记录：语音可懂度、自然度、音乐/语音失真、残留噪声、语音停顿时的抽吸感、CPU/延迟。必要时再补充 DNSMOS 或 ASR WER；第三方对比表只作为参考，不作为唯一结论。

## 8. 实施顺序

### Phase 0：依赖与契约确认

- 固定 sherpa-onnx/ONNX Runtime 版本、模型版本和 SHA-256。
- 确认 shared 包实际 DLL、import lib、头文件布局及许可证。
- 写一个不接入 VoxMic 的最小 48 kHz/480-sample streaming smoke test，验证模型每块输出、首块延迟、reset 行为和 CPU 时间。
- smoke test 同时验证：缺失任一 DLL 时主程序仍可启动、C API 符号表完整性检查、`Run()` 空结果/变长结果、结果对象销毁和 reset 后无尾部泄漏。

### Phase 1：DSP 后端抽象

- 保留 RNNoise 原实现和默认参数。
- 抽出 denoiser interface、后端枚举、reset/ready 状态。
- 加入 DPDFNet block FIFO 和严格 480-sample 输出适配。
- 在正式 render 集成前完成 DPDFNet worker 方案或给出书面性能/实时性例外；不能仅以平均推理耗时通过来放行 render-thread 分配。

### Phase 2：配置/UI/运行时切换

- 新增 `DenoiseBackend` 配置字段及兼容读取。
- DSP 设置页增加 combo、DPDFNet 可用性提示和 strength slider 状态联动。
- 在 render block 边界完成切换和 state reset。

### Phase 3：构建与发布

- 接入依赖准备脚本、CMake 能力开关/ABI 声明和 install 规则；不创建 sherpa-onnx/ONNX Runtime 的硬链接依赖。
- 在 `third_party/dpdfnet/` 固定归档可重建所需的模型、runtime DLL、C API 头文件、metadata 和许可证说明；模型/DLL 使用 Git LFS，`build/` 只保存可删除的 staged/install 产物。
- 更新 runtime manifest、Portable/MSI payload、第三方 notices。
- 更新 README/架构文档中的“单 exe”与 DSP pipeline 描述。

### Phase 4：验证与版本发布

- 完成功能、故障、性能、音质 A/B 测试。
- 按 `AGENTS.md` 同步 `src/version.h`、Android `versionName` 和 `versionCode`；本次依赖归档修复使用 `0.6.1` / `versionCode=7`。
- 将计划移入 `.plan/completed/`，保留测试报告和依赖 hash 记录。

## 9. 风险与取舍

| 风险 | 影响 | 控制措施 |
|---|---|---|
| ONNX Runtime/sherpa-onnx 增大安装包 | 中 | 采用 shared MD Release no-tts；不静态链接；RNNoise 仍作为默认轻量后端 |
| 模型首次初始化造成音频卡顿 | 高 | 预加载或后台构造，render 只做 ready backend 的切换 |
| DPDFNet 与 Android NS 叠加导致过度处理 | 中 | 保持开关独立，测试矩阵覆盖 NS 开/关；不自动改用户配置 |
| 模型流式输出有启动延迟 | 中 | FIFO + 固定 480 输出；切换时 reset；测量并记录额外 latency |
| 外部 DLL/模型缺失 | 高 | 启动检查、一次性日志、自动回退 RNNoise |
| 论文/第三方 benchmark 与实际环境不一致 | 中 | 以 VoxMic 固定录音的 A/B 主观/客观测试验收 |
| 外部依赖升级引入 ABI/模型兼容性变化 | 中 | 版本、下载地址、SHA-256 全部 pin；升级单独走依赖变更计划；仓库内大文件通过 Git LFS 固定版本 |
| 许可证遗漏 | 中 | 发布包附带第三方 notices，审查 sherpa-onnx、ONNX Runtime 和 DPDFNet 模型许可 |

## 10. 明确不在本计划内

- 不删除或替换 RNNoise。
- 不修改 Android 采样率、块大小或 socket 协议。
- 不把 DPDFNet 推理放到 Android 端。
- 不在第一版同时引入 GTCRN、DeepFilterNet、AEC 或去混响模型。
- 不承诺 DPDFNet 在所有噪声和所有手机上必然优于 RNNoise；必须以 VoxMic 实测结果决定默认推荐。
- 本计划创建阶段不修改源码、构建脚本或依赖；实施阶段的固定依赖归档结果记录如下。

## 实施结果（2026-08-09）

- 已保留 RNNoise 为默认后端，新增 `DenoiseBackend=rnnoise|dpdfnet` 配置和设置页切换。
- 已完成 DPDFNet 动态 C API 加载、worker/FIFO、epoch reset、资源/ABI 检查和 RNNoise fallback。
- 已升级版本：`APP_VERSION/versionName=0.6.1`，Android `versionCode=7`。
- 已将固定 DPDFNet 依赖归档到 `third_party/dpdfnet/`，模型和 DLL 使用 Git LFS；`build/` 清理后可由脚本重新生成。
- 已通过 RNNoise-only 与 DPDFNet 构建；runtime manifest、Portable、MSI 和 Android debug 构建均通过（最终载荷版本 `0.6.1`）。
- DPDFNet smoke：`118/120` 输出、reset 后 `37` 块、无 input/output drops，worker EMA 约 `1.6ms`。
- fallback smoke 已覆盖缺失 model 和缺失 sherpa DLL 两种情况。
