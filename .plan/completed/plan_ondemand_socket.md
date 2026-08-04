# Phase 9: Socket 按需连接 — Android 空闲省电

## 动机

当前 "Always Hot" 模式下，socket 永不主动断连，导致 Android 端 `RecordThread` 始终处于**录音+发送**状态：

```
Windows 空闲时:  socket 连接中 → Android AudioRecord 持续录音 → 持续耗电
```

但 Android 端代码本身就支持"等连接→录音→断连→再等"的循环：

```java
// RecordThread.run() — 已具备, 无需修改
while (!isInterrupted()) {
    serverSocket.accept();         // 阻塞, AudioRecord 未启动
    recorder.startRecording();     // 仅在连接后启动
    while (...) { ... write(); }   // 发送数据
    // 断连 → finally { recorder.stop(); }  // 录音停止, 回到 accept()
}
```

只需 Windows 端空闲时主动断连，Android 自动回到 `accept()` 等待 → AudioRecord 停止 → **零耗电**。

## 方案

| 对比 | Always Hot (当前) | Socket 按需 (目标) |
|------|-------------------|---------------------|
| 空闲行为 | socket 连接中, 收数据但丢弃 | **主动断连**, Android 停止录音 |
| 空闲耗电 | Android 持续录音 | **零耗电** |
| 激活延迟 | ~12ms | ~200ms (Sleep 200ms 轮询为主, socket connect ~0.4ms) |
| 重连耗时 | 仅 socket (~0.4ms 实测) | 仅 socket (~0.4ms 实测), 不走 ADB |
| Android 改动 | — | **无需改动** |
| 空闲后 ring buffer | 50 blocks (0.5s) reset | 自然排空 (无新数据推入) |

## 实现

### 改动文件: `src/main.cpp` — `audioBridgeThread()`

#### 1. 外层循环: 空闲等待重连

在 socket `connect()` 之前插入等待逻辑，当 Demand Mode 开启且无 Windows 应用使用麦克风时，不建立连接：

```cpp
// 现行:
if (!socketClient.isConnected()) {
    if (!socketClient.connect(host, port)) {
        Sleep(200);
        continue;
    }
    // ...
}

// 改为:
if (!socketClient.isConnected()) {
    if (g_demandMode.load() && !g_micRequested.load()) {
        Sleep(200);        // 空闲等待, 不建连
        continue;
    }
    if (!socketClient.connect(host, port)) {
        Sleep(200);
        continue;
    }
    // ...
}
```

#### 2. 内层循环: 空闲超时主动断连

在数据丢弃路径 (Demand Mode ON + 无请求) 中，当 `idleCount` 达到阈值时主动断开 socket：

```cpp
// 现行 (丢弃路径):
if (!micRequested && !demandOff) {
    g_micStreaming.store(false);
    idleCount++;
    wasIdle = true;
    if (idleCount > 50) {                    // 0.5s reset ring buffer
        g_wasapiOutput->getRingBuffer()->reset();
        idleCount = 0;
    }
    continue;                                // 继续收数据, 不 push
}

// 改为:
if (!micRequested && !demandOff) {
    g_micStreaming.store(false);
    idleCount++;
    wasIdle = true;
    if (idleCount > 50) {                    // 0.5s reset ring buffer (不变)
        g_wasapiOutput->getRingBuffer()->reset();
        idleCount = 0;
    }
    if (idleCount > 500) {                   // ~5s 空闲 → 主动断连
        printf("Idle 5s, disconnecting socket\n");
        fflush(stdout);
        socketClient.disconnect();
        g_streaming.store(false);
        g_micStreaming.store(false);
        if (g_trayIcon) g_trayIcon->updateIcon(false, false);
        break;                               // 跳出内层 → 外层等待重连
    }
    continue;
}
```

### 时间计算

- 每 block = 480 帧 @ 48kHz = **10ms**
- `idleCount = 500` × 10ms = **5 秒**
- 断连后外层 `Sleep(200)` 轮询 `g_micRequested` → 最坏 200ms 检测延迟
- socket connect 实测: **0.33-0.81ms** (QPC 微秒级, 不走 ADB)
- 总冷启动延迟: **~200ms** (轮询为主, connect 可忽略)

### 与现有 stall 检测的关系

| 机制 | 阈值 | 动作 | 场景 |
|------|------|------|------|
| stall 检测 | 90 × 100ms = 9s | disconnect + reconnect | socket 无数据 (异常) |
| 空闲断连 | 500 × 10ms = 5s | disconnect → 等待请求 | 空闲省电 (正常) |

两者独立，互不冲突：
- stall 检测在**网线不通**时触发
- 空闲断连在**有数据但丢弃**时触发

## 不改 Android 端

Android 端 `RecordThread.run()` 已有的 `accept → startRecording → 发送 → 断连 → stop → accept` 循环完美匹配此模型。断连后自动回到 `accept()` 等待，`AudioRecord` 停止。

## 风险

| 风险 | 评估 |
|------|------|
| 冷启动 ~200ms 延迟 | 可接受（主要为 Sleep(200) 轮询，socket connect 仅 ~0.4ms） |
| 频繁断连/重连 | 空闲断连阈值 5s，正常使用场景不会频繁切换 |
| ring buffer 残留 | 断连后 WASAPI 继续消耗，自然排空；下次 idle 50 blocks 也会 reset |
| Demand Mode OFF | 不影响 — 外层等待逻辑仅对 Demand ON 生效，OFF 时行为不变 |

## 后续可选增强

- 空闲断连时间可配置化（注册表/INI 字段 `idleDisconnectSec`）
- 托盘菜单开关 "Idle Sleep" 独立于 Demand Mode
- 断连前发送空块填充 ring buffer 平滑过渡 (当前已有 underrun 处理)
