# Changelog

## v0.1.1 (2026-05-01)

### 延迟优化 — 400ms → 83ms

| 参数 | v0.1.0 | v0.1.1 | 影响 |
|------|--------|--------|------|
| FRAMES_PER_BLOCK | 1024 (21.3ms) | **512 (10.7ms)** | 块粒度减半 |
| WASAPI buffer | 200ms | **20ms** | 缓冲延迟 -90% |
| 环形水位 | 16→8 | **5→3** | 排队延迟 -85% |
| 初始填充 | 3 块 (~70ms) | **3 块 (~32ms)** | 启动等待 -55% |
| Android 块 | 2048 字节 | **1024 字节** | 对齐 512 帧 |
| 总延迟 | ~400ms | **~83ms** | **5 倍提升** |

### 稳定性验证

2 分钟连续压力测试：`recv=11090 drop=12 underrun=0 queue=2~4`

- underrun=0 零断音
- drop 源自延迟控制主动修剪，非数据丢失

### 测试记录

- 256 帧/块 + 10ms WASAPI → 失败 (ADB 抖动超出窗口)
- 512 帧/块 + 10ms WASAPI + 4→2 水位 → 队列不稳定
- 512 帧/块 + 20ms WASAPI + 5→3 水位 → **收敛** (0 underrun)

---

## v0.1.0 (2026-04-30)

### 初始功能集

- WASAPI 事件驱动渲染 (SetEventHandle + WaitForSingleObject)
- 系统托盘 + Settings 对话框 (设备/网络/App/音效/Gain)
- 注册表配置持久化 (12 字段)
- 48000 Hz Android ↔ Windows 对齐 (零重采样)
- Gain 0.25x–4.0x 滑块
- 音效独立开关 (NS/AEC/AGC checkbox + ADB --ez 传参)
- VoxMic Source Android App (独立构建安装)
- Xiaomi 设备 AudioSource 兼容性实验 (锁定 DEFAULT)
