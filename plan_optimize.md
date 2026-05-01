# Raw WASAPI 后台优化计划

## 目标
将程序改造成24小时后台运行的轻量级服务，需要麦克风时才激活。

## 当前问题

| 问题 | 原因 | 影响 |
|------|------|------|
| CPU 占用高 (~2-5%) | Sleep 精度不够 | 浪费资源 |
| underruns 高 (~12%) | 缓冲区太小 (1024帧) | 音频卡顿 |
| 无系统托盘 | 缺少UI交互 | 不方便管理 |
| 持续连接 | 没有按需激活 | 浪费资源 |

---

## 优化方案

### Phase 1: 事件驱动 + 缓冲区优化

**改动文件**: `wasapi_output.h`, `wasapi_output.cpp`

**核心改动**:
```cpp
// 1. 使用 WASAPI 事件通知
HANDLE m_hEvent;
pAudioClient->SetEventHandle(m_hEvent);
WaitForSingleObject(m_hEvent, INFINITE);  // 替代 Sleep

// 2. 增大缓冲区
#define FRAMES_PER_BLOCK 2048  // 23ms → 46ms
#define RING_BUFFER_BLOCKS 128 // 64 → 128
```

**预期效果**:
- CPU: ~2-5% → ~0.1-0.3%
- underruns: ~12% → ~1-2%

---

### Phase 2: 系统托盘

**新增文件**: `tray_icon.h`, `tray_icon.cpp`

**功能**:
- 托盘图标显示状态（空闲/连接中/传输中）
- 右键菜单：开始/停止/设置/退出
- 双击打开/关闭音频桥接
- 音频电平显示（ToolTip）

**UI 结构**:
```
┌─────────────────────────────────┐
│ 🎤 AudioSource Win              │
├─────────────────────────────────┤
│ ▶ 开始桥接 (Ctrl+Enter)        │
│ ⏹ 停止桥接 (Ctrl+S)           │
├─────────────────────────────────┤
│ ⚙ 设置...                      │
├─────────────────────────────────┤
│ ❌ 退出 (Ctrl+Q)               │
└─────────────────────────────────┘
```

---

### Phase 3: 按需激活

**策略**:
1. **监控模式**: 持续监控是否有应用在使用麦克风
2. **自动激活**: 检测到麦克风请求时自动启动桥接
3. **自动停止**: 无麦克风请求时自动停止（节省资源）

**实现方式**:
```cpp
// 方案A: 轮询检查（简单）
bool isAnyAppUsingMic() {
    // 检查音频会话
    IAudioSessionEnumerator* pEnumerator;
    pSessionManager->GetSessionEnumerator(&pEnumerator);
    // 遍历会话，检查状态
}

// 方案B: 监听音频会话事件（高效）
class AudioSessionNotification : public IAudioSessionNotification {
    // 当有新会话创建时回调
};
```

**推荐方案B**: 事件驱动，CPU 占用更低

---

### Phase 4: 电源管理

**功能**:
- 系统休眠时暂停桥接
- 系统唤醒时恢复桥接
- 低电量模式自动停止

**实现**:
```cpp
// 监听电源事件
RegisterPowerSettingNotification(hwnd, &GUID_MONITOR_POWER_ON, DEVICE_NOTIFY_WINDOW_HANDLE);
```

---

## 文件结构

```
vox_mic_raw_wasapi/
├── build.bat
├── src/
│   ├── main.cpp               # 程序入口（托盘窗口）
│   ├── wasapi_output.h/cpp    # WASAPI 输出（事件驱动）
│   ├── device_enum.h/cpp      # 设备枚举
│   ├── ring_buffer.h          # 无锁环形缓冲区
│   ├── socket_client.h/cpp    # Winsock2 客户端
│   ├── adb_control.h/cpp      # ADB 控制
│   ├── tray_icon.h/cpp        # 系统托盘（新增）
│   ├── mic_monitor.h/cpp      # 麦克风监控（新增）
│   └── resource.h             # 资源定义（图标等）
└── assets/
    └── icon.ico               # 托盘图标
```

---

## 实施步骤

### Step 1: 事件驱动优化 (30min)
- [ ] 添加事件句柄到 WASAPIOutput
- [ ] 修改渲染线程使用 WaitForSingleObject
- [ ] 调整缓冲区大小
- [ ] 测试 CPU 占用和 underruns

### Step 2: 系统托盘 (1h)
- [ ] 创建窗口类和消息循环
- [ ] 添加托盘图标
- [ ] 实现右键菜单
- [ ] 添加状态切换动画

### Step 3: 按需激活 (45min)
- [ ] 实现麦克风监控
- [ ] 添加自动启动/停止逻辑
- [ ] 测试边界情况

### Step 4: 电源管理 (30min)
- [ ] 监听电源事件
- [ ] 实现休眠/唤醒处理

---

## 预期最终效果

| 指标 | 当前 | 优化后 |
|------|------|--------|
| CPU 空闲 | ~2-5% | ~0% |
| CPU 工作 | ~2-5% | ~0.3% |
| 内存 | ~500KB | ~1MB |
| underruns | ~12% | ~1% |
| 交互方式 | 命令行 | 系统托盘 |
| 启动方式 | 手动 | 自动检测 |

---

## 技术细节

### WASAPI 事件驱动原理
```
┌─────────────────────────────────────────┐
│  WASAPI 渲染缓冲区 (环形)               │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┐     │
│  │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │     │
│  └───┴───┴───┴───┴───┴───┴───┴───┘     │
│       ↑                               │
│       └── 事件触发：一个块播放完成      │
└─────────────────────────────────────────┘
        │
        ▼
  WaitForSingleObject(hEvent)
        │
        ▼
  填充新数据到缓冲区
```

### 麦克风监控原理
```
Windows 音频引擎
        │
        ▼
  IAudioSessionEnumerator
        │
        ├── Session 1: Chrome (Active)
        ├── Session 2: Zoom (Active)
        └── Session 3: Discord (Inactive)
        
检测到 Active 会话 → 启动桥接
全部 Inactive → 停止桥接
```

---

## 风险与对策

| 风险 | 对策 |
|------|------|
| 事件驱动延迟 | 调整缓冲区大小平衡 |
| 托盘图标闪烁 | 使用定时器刷新 |
| 误检测麦克风 | 添加白名单/黑名单 |
| ADB 断连 | 自动重连机制 |

---

## 后续扩展

1. **多设备支持**: 同时桥接多个手机
2. **网络传输**: 支持 WiFi 连接（ADB over TCP）
3. **音频处理**: 降噪、增益、均衡器
4. **配置持久化**: 保存用户设置到注册表/配置文件
5. **开机自启**: 注册为 Windows 服务或启动项
