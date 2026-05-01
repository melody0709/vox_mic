# 架构说明 — v0.1.0

## 数据流

```
Android 手机麦克风 (48000Hz/单声道/int16)
    ↓
VoxMic Source App (DEFAULT 源 + 可选 NS/AEC/AGC)
    ↓ USB ADB
ADB 转发 (tcp:27183)
    ↓ TCP
audiosource.exe
    ├── Socket 接收线程 (Winsock2 recvExact, 2048 字节块)
    ├── SPSC 无锁环形缓冲区 (128 块 × 2KB)
    └── WASAPI 事件驱动渲染线程 (ratio=1.0 直通 + Gain)
            ↓
    VB-CABLE Input (48000Hz/立体声)
            ↓
    VB-CABLE Output
            ↓
    Windows 应用 (Zoom, Teams, 录音机)
```

## 源文件说明

| 文件 | 职责 |
|------|------|
| `main.cpp` | 入口、托盘窗口、桥接线程（动态读 g_config）、stats 定时器 |
| `wasapi_output.h/cpp` | WASAPI 事件驱动初始化、渲染循环、Gain 应用、双格式重采样 |
| `device_enum.h/cpp` | WASAPI 设备枚举，按名称查找 VB-CABLE，回退默认设备 |
| `ring_buffer.h` | 无锁 SPSC 环形缓冲区，原子读写指针 |
| `socket_client.h/cpp` | Winsock2 TCP 客户端，recvExact 保证大小读取 |
| `adb_control.h/cpp` | ADB 命令执行、设备检测、App 启动（含 --ez 音效参数）、端口转发 |
| `tray_icon.h/cpp` | 系统托盘图标，右键菜单 (Start/Stop/Settings/Exit) |
| `config.h/cpp` | 注册表配置持久化 (12 个字段) |
| `settings_dialog.h/cpp` | 纯 Win32 设置对话框 (设备/网络/App/音效/Gain) |

## 音频处理管线

```
Android 48000Hz int16 mono
    ↓ (m_resampleRatio == 1.0 → 直通)
通道复制: mono → stereo
    ↓
格式转换: int16 → float32 (或 int16→int16)
    ↓
Gain 应用: sample *= g_gain (原子变量，支持 0.25x–4.0x)
    ↓
WASAPI 写入 VB-CABLE
```

## 延迟预算

| 组件 | 延迟 |
|------|------|
| Android 采集 | ~21ms (48000Hz/1024 帧) |
| ADB + Socket | ~10ms |
| 环形缓冲区 | ~171ms (8 块) |
| WASAPI 缓冲区 | ~200ms |
| VB-CABLE | ~10ms |
| **总计** | **~400ms** |

## WASAPI 事件驱动流程

```
CreateEventEx → SetEventHandle → 预填充静音 → Start
  ↓
循环: WaitForSingleObject(hEvent, 2000)
        ↓
      GetCurrentPadding → 算可用帧数
        ↓
      内层 while: 只要有空间 → pop 环形缓冲 → 处理 → ReleaseBuffer
  ↓
Stop
```

## Android App 通信协议

```
Windows Settings → 注册表
     ↓ (重连时读取)
g_config.nsEnabled / aecEnabled / agcEnabled
     ↓
adb shell am start -n com.voxmic.source/.MainActivity
     --ez ns_enabled true --ez aec_enabled true --ez agc_enabled false
     ↓
MainActivity.onCreate → 读 extras → RecordService.start(ns, aec, agc)
     ↓
RecordService.onStartCommand → 按标志启用/禁用对应音效
```
