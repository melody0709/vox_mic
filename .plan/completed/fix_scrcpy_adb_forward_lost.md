# scrcpy 开关后 ADB forward 丢失修复

**日期**: 2026-05-03
**状态**: 已完成

## 问题描述

运行 scrcpy 后关闭 scrcpy，vox_mic 无法进入 streaming 状态，必须重启 vox_mic 才能恢复。

## 根因分析

### 问题链

```
scrcpy 关闭
  → scrcpy 清理 ADB 端口转发（adb forward --remove tcp:27183）
  → vox_mic 的 ADB forward 消失
  → bridge 尝试 socket connect → 10061 (Connection Refused)
  → 连续 3 次失败后才通过 setupAudioSource() 完整重建（~5 秒延迟）
  → 用户需要按 3 次语音输入才能恢复
```

### 关键发现

1. **Android RecordThread 从未崩溃** — logcat 证实每次都能正常 accept、录音、发送、处理 Broken pipe 并回到 accept
2. **问题是 Windows 端的 ADB forward 被 scrcpy 删除** — 不是 Android 端的问题
3. **bridge 在 demand mode idle 时不做任何连接尝试** — 无法提前发现 forward 已坏

## 修复方案

### 方式 B：连接失败时被动刷新 forward（Reactive）

connect 失败 10061 时，如果之前成功连接过，立即调用 `refreshForward()`（~100ms）重建 ADB forward，然后重试连接。

```
connect 失败 10061
  ├─ 第 1 次 + 之前成功过 → refreshForward (~100ms) → retry → 成功
  └─ 连续 3 次都失败 → setupAudioSource（完整重建，~1.5s）
```

**恢复延迟**：~300ms（connect 失败 + refreshForward + retry），比之前的 3-5 秒大幅改善。

## 尝试过但移除的方案

| 方案 | 问题 | 状态 |
|------|------|------|
| SO_RCVTIMEO 超时 + auto-exit | 对 scrcpy 问题无帮助，Android RecordThread 本身不崩溃 | 已移除 |
| AudioRecord 重建 | 同上，read() 不失败 | 已移除 |
| 外层 catch(Throwable) | 防御性代码，RecordThread 不会崩溃 | 已移除 |
| 主动健康检查（定时轮询） | 用户拒绝：增加 CPU 开销 | 未实施 |
| idle→active 转换时主动刷新 | 用户拒绝：每次语音输入增加 1.5s 延迟 | 未实施 |

## 最终改动

### 文件清单

| 文件 | 改动 | 行数 |
|------|------|------|
| `src/adb_control.h` | 添加 `refreshForward()` 声明 | +1 |
| `src/adb_control.cpp` | 实现 `refreshForward()` = removeForward + createForward | +13 |
| `src/main.cpp` | connect 失败检测 + `refreshForward` 调用 + quick disconnect 检测 + 日志 | +44 |
| `RecordService.java` | 提取 `createRecorder()` 工厂方法 + `releaseAudioEffects()` 辅助方法 | 重构 |
| `RecordThread.java` | 诊断日志（accept/recorder state/blocks sent/connection closed） | +20 |

### 核心逻辑（main.cpp）

```cpp
if (!socketClient.connect(host, port)) {
    connectFailCount++;
    if (connectFailCount == 1 && wasPreviouslyConnected) {
        // 第 1 次失败且之前成功过 → 可能是 forward 被删，快速刷新
        adb.refreshForward(port, "localabstract:" + androidSocket);
    }
    if (connectFailCount >= 3) {
        // 3 次都失败 → forward 刷新不够，app 可能也崩了，完整重建
        adb.setupAudioSource(androidComponent, androidSocket, ns, aec, agc);
        connectFailCount = 0;
    }
    Sleep(200);
    continue;
}
```

### refreshForward() 实现

```cpp
bool ADBControl::refreshForward(int port, const std::string& remoteSocket) {
    removeForward(port);   // adb forward --remove tcp:27183  (~50ms)
    // adb forward tcp:27183 localabstract:voxmicsource       (~50ms)
    createForward(port, remoteSocket);
    return true;
}
```

## 验证结果

scrcpy 开关后，控制台输出：
```
connect() failed: 10061
[Bridge] connect fail #1
[Bridge] first fail after connection, refreshing ADB forward
ADB forward refreshed: tcp:27183 -> localabstract:voxmicsource
Socket connected (0.33ms)    ← 恢复成功
```

## 功耗影响

| 场景 | 功耗变化 |
|------|---------|
| 正常使用 | 无变化 |
| 待机（电脑关机） | 无变化 — RecordThread 阻塞在 accept()，零 CPU |
| scrcpy 恢复 | 多 ~100ms 的 ADB 命令执行，一次性开销 |

## 后续可优化

- **idle→active 转换时主动刷新**：如果用户反馈被动刷新的 ~300ms 延迟仍可感知，可在 idle > 3 秒后恢复时主动刷新 forward
- **SO_RCVTIMEO + auto-exit**：如果需要电脑关机后 Android app 自动退出，可重新加入
