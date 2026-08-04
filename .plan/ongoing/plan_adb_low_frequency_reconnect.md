# Phase 12: ADB 低频健康检查与断线自恢复

**状态**: 已实施，待实机断线/重连验证

## 目标

USB 线松动导致 Android 设备从 ADB 掉线后，设备重新连回时，VoxMic 应在低频后台检查中自动恢复到可用状态。用户第一次调用麦克风时不应再承担 `adb init + start app + forward` 的恢复成本。

## 现象记录

模拟断线后日志表现：

```text
connect() failed: 10061
[Bridge] connect fail #1
connect() failed: 10061
[Bridge] connect fail #2
connect() failed: 10061
[Bridge] connect fail #3
[Bridge] 3 consecutive connect fails, full reset
Android app started (NS=0 AEC=1 AGC=0)
ADB forward ready: tcp:27183 -> localabstract:voxmicsource
```

这说明完整恢复逻辑存在，但它目前是由第一次 socket connect 失败触发的。Demand Mode + AlwaysHot OFF 时，空闲阶段不主动 connect，因此设备重新插上后不会提前发现 forward/app 状态已失效，第一次用麦会变成恢复触发器，导致第一次调用失败或明显延迟。

## 根因判断

当前恢复机制偏 socket reactive：

| 层级 | 当前行为 | 问题 |
|------|----------|------|
| Socket connect 失败 | 第 1 次且曾连接过时 refresh forward，3 次后 full reset | 只有用户请求麦克风后才触发 |
| USB/ADB 设备掉线 | 没有独立状态 | 无法区分 forward 丢失、app 未启动、设备不在线 |
| 设备重新上线 | 没有低频预恢复 | 第一次调用麦克风承担恢复成本 |
| Idle socket 已断开 | 没有任何 socket 活动 | 线在空闲时松掉/插回不会被发现 |
| setupAudioSource | 内部 hardcode `27183` | 非默认端口恢复路径不一致 |

## 设计原则

- 低频，不增加明显 CPU/ADB 开销。
- 不做常态高频轮询；只在 idle 断开或异常后做低频检查。
- 正常 idle 省电断连不算异常。
- 设备不在线时不反复 full reset，避免无意义 ADB 命令。
- 设备回来后主动预热：启动 app、重建 forward、验证 mapping。

## 方案

### 1. 增加 Bridge 恢复状态

在 bridge 线程内维护轻量状态：

| 状态 | 含义 |
|------|------|
| `Healthy` | ADB init、app、forward 最近确认正常 |
| `Suspect` | socket 异常发生，等待一次 ADB 健康检查 |
| `AdbLost` | 目标设备不在 `adb devices` 的 `device` 列表中 |
| `Recovering` | 设备已回来，正在重启 app + 重建 forward |

异常来源：

- `connect()` 返回 10061 或连续失败
- `recvExact()` 返回 `<= 0`
- socket stall 9 秒
- 3 秒内 quick disconnect

不进入异常状态：

- Demand Mode 空闲等待
- AlwaysHot OFF 的 idle 5 秒主动断开
- 程序退出时 cleanup

### 2. Idle 状态增加极低频 readiness check

仅靠“异常后轮询”不完整：如果 USB 线在 Demand Mode 空闲、socket 已主动断开的阶段松掉，Windows 端不会收到 socket 错误。设备重新插回后，也不会自动预恢复，直到用户第一次请求麦克风才会触发 connect fail。

因此需要在以下条件全部满足时做极低频检查：

```text
socket 未连接
Demand Mode ON
AlwaysHot OFF
当前无 micRequested
之前至少成功 ADB setup/恢复过一次
```

建议节奏：

```text
Healthy + idle socket disconnected:
  每 15 秒检查 1 次 adb devices
  如果目标设备在线且 forward mapping 缺失 -> 后台 refresh/setup
  如果目标设备不在线 -> 进入 AdbLost
```

15 秒一次 `adb devices` 的开销很低，并且只发生在 idle 断开状态。用户正在 streaming 时不检查，正常 AlwaysHot 长连接时也不需要检查。

可选更积极策略：

```text
刚 idle 断开后的前 30 秒:
  每 5 秒检查 1 次
之后:
  每 15-30 秒检查 1 次
```

如果目标是“设备插回后尽量快可用”，推荐先用固定 10-15 秒；如果目标是“极致低功耗/低开销”，用 30 秒。

### 3. 异常后开启低频 ADB 健康检查

触发 `Suspect` 后，不需要马上高频轮询。建议节奏：

```text
首次异常: 立即检查一次 adb devices
设备不在线: 进入 AdbLost
AdbLost 轮询: 1s -> 2s -> 3s -> 3s ... 最大 3 秒
设备在线: 进入 Recovering
```

检查内容：

1. `adb devices` 中目标 serial 是否为 `device`
2. 如果用户选择 Auto-detect，则选择第一个在线设备
3. 如果目标 serial 不在线但有其他设备在线，不自动切换，保持等待目标设备，避免串到别的手机

### 4. AdbLost 必须是可长期停留的稳定状态

设备被真正拔走后，可能长时间不回来。`AdbLost` 不能被实现成错误重试循环，而应该是一个正常、可无限期停留的状态：

```text
AdbLost:
  不做 socket connect
  不做 setupAudioSource
  不做 refreshForward
  不累计触发 full reset
  只按低频节奏运行 adb devices
  设备没回来就继续停留
```

这样设备长期断联时不会崩溃、不会刷屏、不会持续启动 ADB 子进程，也不会影响 WASAPI 渲染线程。

额外要求：

- `adb devices` 调用必须有超时保护，避免 ADB server 卡住时拖死 bridge 线程。
- `AdbLost` 日志需要限流，例如进入状态时打印一次，之后每 30-60 秒打印一次等待中。
- 托盘状态应显示 disconnected/idle，不要显示 streaming。
- 设备回来后才进入 `Recovering`，恢复成功后再回 `Healthy`。

### 5. 设备回来后主动预恢复

`Recovering` 状态执行：

```text
adb.init(serial)
adb.setupAudioSource(androidComponent, androidSocket, port, ns, aec, agc)
adb forward --list 验证 tcp:port -> localabstract:androidSocket
状态置 Healthy
connectFailCount = 0
quickDisconnectCount = 0
```

这样恢复发生在后台低频检查里，而不是用户第一次开麦时。

### 6. setupAudioSource 改为端口感知

当前 `audioBridgeThread()` 读取 `g_config.port`，但 `ADBControl::setupAudioSource()` 内部 hardcode `27183`。需要把 port 传进去：

```cpp
setupAudioSource(androidComponent, androidSocket, port, ns, aec, agc)
cleanup(port)
```

同时保留默认参数 `27183`，避免影响旧调用。

### 7. ADB forward 结果校验

当前 `createForward()` / `refreshForward()` 基本总是返回 true。建议增强：

- `runCommandNoWindow()` 支持拿到命令输出和是否超时
- 检查输出中是否包含 `error`、`offline`、`unauthorized`、`no devices`
- 成功后运行 `adb forward --list`，确认包含：

```text
<serial> tcp:<port> localabstract:<androidSocket>
```

校验失败则保持 `AdbLost` 或 `Suspect`，等待下一轮低频检查。

## 推荐实现路径

| # | 任务 | 状态 | 文件 |
|---|------|------|------|
| 1 | 为 `ADBControl` 增加 `isDeviceOnline(serial)` 辅助 | 已完成 | `src/adb_control.h/cpp` |
| 2 | `setupAudioSource()` / `cleanup()` 支持 port 参数 | 已完成 | `src/adb_control.h/cpp`, `src/main.cpp` |
| 3 | 在 bridge 线程增加 `Healthy/AdbLost/Recovering` 状态和低频时间戳 | 已完成 | `src/main.cpp` |
| 4 | idle socket disconnected 状态下增加 15 秒一次 readiness check | 已完成 | `src/main.cpp` |
| 5 | `AdbLost` 支持长期停留：不 connect、不 reset、不启动 app，只低频检查 | 已完成 | `src/main.cpp` |
| 6 | connect fail 时先检查 ADB 在线状态，再决定 refresh/recover | 已完成 | `src/main.cpp` |
| 7 | AdbLost 状态下低频轮询，设备回来后后台预恢复 | 已完成 | `src/main.cpp` |
| 8 | ADB 命令增加超时保护，避免 adb 卡住拖死 bridge 线程 | 已完成 | `src/adb_control.cpp` |
| 9 | forward 创建/刷新后用 `adb forward --list` 验证 mapping | 已完成 | `src/adb_control.cpp` |
| 10 | 增加日志，区分 `ADB lost`、`Recovering ADB`、`ADB recovered and forward verified` | 已完成 | `src/main.cpp`, `src/adb_control.cpp` |

## 本次实际改动

### `src/adb_control.h/cpp`

- `runCommandNoWindow()` 增加默认 5 秒超时，超时后终止子进程并返回 `[TIMEOUT]` 标记。
- 新增 `isDeviceOnline(serial)`，用于恢复状态机判断目标设备是否已回来。
- 新增 `verifyForward(port, remoteSocket)`，用 `adb forward --list` 确认映射真实存在。
- `createForward()` / `refreshForward()` 不再无条件返回 true，会检查 ADB 错误输出并验证 forward。
- `setupAudioSource()` 和 `cleanup()` 改为接收 `port`，移除恢复路径里的 `27183` 硬编码。

### `src/main.cpp`

- 新增 `BridgeRecoveryState`：`Healthy`、`AdbLost`、`Recovering`。
- Demand Mode + AlwaysHot OFF + socket idle disconnected 时，已成功 ADB setup/恢复过的情况下每 15 秒做一次 readiness check。
- `AdbLost` 状态可长期停留：不 socket connect、不 setup app、不 refresh forward，只低频 `adb devices`。
- `AdbLost` 轮询节奏为 1 秒、2 秒、3 秒，之后固定 3 秒。
- 设备回来后后台执行 `adb.init()`、`setupAudioSource(..., port, ...)`、`verifyForward()`，成功后回到 `Healthy`。
- connect fail 和 quick disconnect 路径会先检查 ADB 在线状态，避免设备不在线时反复 full reset。
- 即使尚未成功建立过 socket，只要初始 ADB setup 成功过，第一次 connect fail 也会先快速 refresh forward。

## 构建验证

已运行：

```text
build.bat
```

结果：

```text
Build successful: build\voxmic.exe
```

## 附加可观测性优化

为避免 idle 时 `underrun` 计数持续上涨造成误判，已调整 Debug Console 的统计口径：

| 指标 | 含义 |
|------|------|
| `underrun` | 只有当前期望有音频时 ring buffer 为空才累计，代表真正需要关注的缺数据 |
| `idleSilence` | 当前不期望有音频时输出静音块，代表正常 idle 行为 |
| `state` | bridge 当前状态，方便区分 streaming、idle、adb-lost、recovering |

新的 Stats 示例：

```text
[Stats] state=idle-sleep recv=1548 drop=0 underrun=0 idleSilence=1500 queue=0 proc=1us lat=11.0ms
```

状态值：

| state | 含义 |
|-------|------|
| `starting` | bridge 初始化中 |
| `idle-sleep` | Demand Mode 空闲，socket 已断开省电 |
| `idle-hot` | socket 连接中但当前没有麦克风请求，输出/丢弃静音 |
| `streaming` | 正在向 VB-CABLE 推送手机音频 |
| `adb-lost` | ADB 设备断联，低频等待设备回来 |
| `recovering` | 设备已回来，正在恢复 app 和 forward |

实现文件：

| 文件 | 改动 |
|------|------|
| `src/wasapi_output.h/cpp` | 新增 `idleSilenceBlocks`，idle 时空 buffer 计入 `idleSilence`，active 时空 buffer 才计入 `underrun` |
| `src/main.cpp` | 新增 `bridgeStatus`，Stats 输出增加 `state=` 和 `idleSilence=` |

## 行为预期

| 场景 | 预期 |
|------|------|
| 正常空闲 | socket 已断开时每 10-15 秒做一次 readiness check |
| 线松导致 ADB 掉线 | socket 异常后进入 `AdbLost`，托盘显示 disconnected |
| idle 时线松后自动重连 | 先由 idle check 发现掉线，设备回来后最多约 1-3 秒内后台恢复 app + forward |
| streaming 时线松后自动重连 | socket 异常立即发现，设备回来后最多约 1-3 秒内后台恢复 app + forward |
| 设备长期拔开 | 长期停留在 `AdbLost`，只低频检查，不崩溃、不刷屏、不 full reset |
| 用户第一次调用麦 | socket connect 直接成功，不再触发 full reset |
| 目标设备没回来 | 不切换到其他设备，不刷屏，不反复启动 app |
| 非默认端口 | forward 和 socket 使用同一个配置端口 |

## 风险与控制

| 风险 | 控制 |
|------|------|
| ADB 轮询增加开销 | idle 已断开时 10-15 秒一次；异常掉线后最大 3 秒一次 |
| 多设备误选 | 指定 serial 时只等该 serial；Auto-detect 才选第一个 |
| 设备 unauthorized | 明确日志提示用户手机上确认 USB 调试授权 |
| full reset 过早 | 先检查 ADB 在线状态，再决定是否 setupAudioSource |
| 恢复期间用户请求麦 | 状态机保持 Recovering，完成后立即 connect |
| ADB server 卡住 | ADB 命令增加超时保护，超时后保持 AdbLost 并等待下一轮 |

## 验证计划

1. 启动 VoxMic，确认初始连接正常。
2. Demand Mode ON、AlwaysHot OFF，空闲 5 秒后 socket 断开。
3. 在 idle socket 已断开时拔掉 USB，确认下一次 idle health check 进入 `AdbLost`。
4. 保持设备拔开 5-10 分钟，确认只低频检查，不崩溃、不刷屏、不 full reset。
5. 重新插回设备，观察 1-3 秒内出现 `AdbRecovered` / `ForwardVerified`。
6. 打开 Windows 录音应用请求 CABLE Output，第一次调用应直接进入 streaming。
7. streaming 期间拔掉 USB，确认 socket 异常路径进入 `AdbLost`。
8. 重复 3 次快速拔插，确认不会卡死或需要重启 VoxMic。
9. 将 port 改为非 27183，确认 forward/listen/connect 都使用同一端口。

## 非目标

- 不改变 Android `RecordThread` 的 accept/record/stop 模型。
- 不引入高频常驻 ADB 轮询；idle 检查必须是低频且可配置。
- 不改变 Demand Mode 的省电策略。
