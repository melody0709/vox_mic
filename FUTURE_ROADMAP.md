# 未来路线图 — v0.5.0+

## 已完成 ✅

- ✅ WASAPI 事件驱动渲染 (CPU ~0.1%)
- ✅ 系统托盘 + Settings 对话框 (17 字段)
- ✅ 配置持久化 (注册表)
- ✅ 48000 Hz Android ↔ Windows 对齐 (零重采样)
- ✅ Gain 控制 + 音效独立开关
- ✅ VoxMic Source Android App
- ✅ Xiaomi AudioSource 实验
- ✅ 延迟优化 400ms → 83ms (v0.1.1)
- ✅ DSP 管线: EQ + Compressor + Limiter (v0.2.0)
- ✅ **RNNoise 神经网络降噪** (v0.3.0)
- ✅ **Phase 3: 麦克风按需激活** (v0.4.0)
  - MicUsageMonitor: IAudioSessionManager2 100ms 轮询检测 CABLE Output 捕获状态
  - Always Hot: bridge 永不主动断连 socket, 空闲时 recv + 丢弃
  - ADB 一次性初始化, socket 重连仅 connect() ~200ms
  - 检测延迟实测 ~12ms, 端到端延迟 ~40ms
  - 空闲 CPU ~0.25% (DSP 全跳 + monitor 0.15% + WASAPI 0.1%)
- ✅ **Phase 5: 隐藏到托盘 GUI** (v0.4.1)
  - 设置窗口提升为主窗口，非模态持久化
  - 左键托盘图标弹出窗口，关闭即隐藏到托盘
  - ADB 无闪烁: CreateProcess + CREATE_NO_WINDOW
  - /SUBSYSTEM:WINDOWS 零控制台启动
  - Debug Console 按需 AllocConsole
  - 版本号 v0.4.1 显示于托盘菜单
- ✅ **Phase 8: CPU 优化** (v0.4.2)
  - 事件驱动 Monitor: IAudioSessionNotification + IAudioSessionEvents COM 回调
  - 零 COM 轮询: 空闲时无 GetSessionEnumerator/Activate/Release
  - Demand Mode 开关: 右键托盘菜单，持久化到注册表
  - DSP 开销实测仅 ~0.1%（远低于预估 ~1.65%）
  - 空闲 CPU 0-0.1%（从 v0.4.1 的 0.2-0.4% 优化）
- ✅ **Phase 9: Socket 按需连接** (v0.5.0)
  - Always Hot 开关: 右键托盘菜单，持久化到注册表，默认 OFF
  - 空闲 5s 断连 socket → Android AudioRecord 停止 → 零耗电
  - 按需重连: socket connect ~0.3-0.8ms (QPC 实测)
  - 冷启动延迟 ~200ms (Sleep 200ms 轮询为主)
  - Android 端无需改动

## 后续 Phase

| Phase | 内容 | 工时 | 优先级 |
|-------|------|------|--------|
| Phase 4 | 电源管理 (WM_POWERBROADCAST) | 30min | 低 |
| Phase 6C | DeepFilterNet3 升级 (可选) | 4h | 低 |
| — | 自定义托盘图标 (.ico 三种状态) | 1h | 低 |
| — | VU 电平表 (RMS + 托盘提示) | 1h | 低 |

---

## 变更时间线

| 顺序 | 项目 | 工时 | 版本 |
|------|------|------|------|
| ~~1~~ | ~~WASAPI 事件驱动~~ | ✅ | v0.1.0 |
| ~~2~~ | ~~系统托盘 + Settings~~ | ✅ | v0.1.0 |
| ~~3~~ | ~~48000Hz 对齐~~ | ✅ | v0.1.0 |
| ~~4~~ | ~~Gain + 音效开关~~ | ✅ | v0.1.0 |
| ~~5~~ | ~~延迟优化 400→83ms~~ | ✅ | v0.1.1 |
| ~~6~~ | ~~DSP 管线 (EQ+Comp+Lim)~~ | ✅ | v0.2.0 |
| ~~7~~ | ~~RNNoise 降噪~~ | ✅ | v0.3.0 |
| ~~8~~ | ~~麦克风按需激活~~ | ✅ v0.4.0 |
| ~~9~~ | ~~WiFi ADB~~ | ✅ 已可用 |
| ~~10~~ | ~~隐藏到托盘 GUI~~ | ✅ v0.4.1 |
| ~~11~~ | ~~CPU 优化 + 事件驱动 Monitor~~ | ✅ v0.4.2 |
| ~~12~~ | ~~Socket 按需连接~~ | ✅ v0.5.0 |
| 13 | 自定义图标 | 1h | — |
