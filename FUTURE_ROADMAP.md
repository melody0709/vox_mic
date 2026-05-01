# 未来路线图 — v0.3.0+

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

---

## 下一步: Phase 3 — 麦克风按需激活

| 任务 | 工时 |
|------|------|
| IAudioSessionNotification 检测活跃捕获 | 2h |

自动检测 Windows 应用是否在使用 CABLE Output 麦克风，仅在需要时启动桥接。

---

## 后续 Phase

| Phase | 内容 | 工时 | 优先级 |
|-------|------|------|--------|
| Phase 4 | 电源管理 (WM_POWERBROADCAST) | 30min | 低 |
| Phase 6C | DeepFilterNet3 升级 (可选) | 4h | 低 |
| Phase 7 | WiFi ADB + 断连平滑 + 设备恢复 | 5h | 中 |
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
| 8 | 麦克风按需激活 | 2h | v0.4.0 |
| 9 | WiFi ADB | 3h | |
| 10 | 断连平滑 | 1h | |
| 11 | 自定义图标 | 1h | |
