# 未来路线图 — v0.1.0+

当前版本 v0.1.0 里程碑已达成。以下为后续推进方向。

---

## 已完成 ✅

- ✅ WASAPI 事件驱动渲染 (CPU ~0.1%)
- ✅ 系统托盘 + Settings 对话框 (设备/网络/App/音效/Gain 全可控)
- ✅ 配置持久化 (注册表 12 字段)
- ✅ 48000 Hz Android ↔ Windows 对齐 (零重采样)
- ✅ Gain 滑块 0.25x–4.0x
- ✅ 音效独立开关 (NS/AEC/AGC checkbox + ADB --ez 传参)
- ✅ VoxMic Source Android App (可独立构建安装)
- ✅ Xiaomi 设备 AudioSource 实验记录 (锁定 DEFAULT)

---

## 下一步: Phase 6 — Windows 端音频后处理管线

| 子步骤 | 内容 | 工时 |
|--------|------|------|
| 6A | Speex 高质量重采样器 (Public Domain 单文件) | 1h |
| 6B | 动态压缩器 (清晰感核心提升) | 2h |
| 6C | 语音增强 EQ (2-5kHz peaking) | 1h |

全部在 `wasapi_output.cpp` 实现，不碰 Android APK。

---

## 后续 Phase

| Phase | 内容 | 工时 |
|-------|------|------|
| Phase 3 | 麦克风按需激活 (IAudioSessionNotification) | 2h |
| Phase 4 | 电源管理 (WM_POWERBROADCAST) | 30min |
| Phase 7 | WiFi ADB + 断连平滑 + 设备恢复 | 5h |
| — | 自定义托盘图标 (.ico 三种状态) | 1h |
| — | VU 电平表 (RMS + 托盘提示) | 1h |
| — | 多设备支持 | 4h+ |
| — | 跨平台 (Linux/macOS) | 20h+ |

---

## 变更时间线

| 顺序 | 项目 | 工时 | 影响 |
|------|------|------|------|
| ~~1~~ | ~~WASAPI 事件驱动~~ | ✅ | CPU -95% |
| ~~2~~ | ~~系统托盘 + Settings~~ | ✅ | 全功能 UI |
| ~~3~~ | ~~48000Hz 对齐 + 零重采样~~ | ✅ | 音频质量 |
| ~~4~~ | ~~Gain + 音效开关~~ | ✅ | 音量/效果可控 |
| **5** | **Speex 重采样器** | 1h | 质量底线 |
| **6** | **动态压缩器** | 2h | 清晰感飞跃 |
| **7** | **语音增强 EQ** | 1h | 人声优化 |
| 8 | WiFi ADB | 3h | 无线连接 |
| 9 | 断连平滑 | 1h | 体验优化 |
| 10 | 麦克风自动检测 | 2h | 全自动 |
| 11 | 自定义图标 | 1h | 专业外观 |
