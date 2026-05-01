# 方案1：miniaudio 实施计划

## 方案概述

使用 miniaudio 库（单头文件）+ Winsock2 实现 Android 麦克风音频流到 Windows 虚拟麦克风的桥接。

## 架构设计

```
┌─────────────────────────────────────────────────────────────────┐
│                      C++ Win32 程序 (miniaudio)                 │
├─────────────────────────────────────────────────────────────────┤
│  [Socket 接收线程]                                               │
│    Winsock2 连接 127.0.0.1:27183                                │
│    接收 PCM 数据块 (1024 frames × 2 bytes = 2048 bytes)         │
│    推入无锁环形缓冲区 (SPSC Ring Buffer)                          │
├─────────────────────────────────────────────────────────────────┤
│  [音频回调线程] (miniaudio/WASAPI 回调)                           │
│    从环形缓冲区取出数据                                           │
│    mono int16 → stereo int16 (复制声道)                          │
│    应用增益                                                      │
│    写入 "CABLE Input" 输出设备                                   │
├─────────────────────────────────────────────────────────────────┤
│  [主线程]                                                       │
│    启动 Android AudioSource app (adb shell am start)            │
│    创建 ADB 端口转发 (adb forward tcp:27183 localabstract:...)  │
│    监控状态、处理退出                                             │
└─────────────────────────────────────────────────────────────────┘
```

## 文件结构

```
vox_mic_miniaudio/
├── build.bat                  # MSVC 编译脚本
├── src/
│   ├── main.cpp               # 主函数、程序入口
│   ├── audio_bridge.h         # 音频桥接类声明
│   ├── audio_bridge.cpp       # 音频桥接实现（miniaudio 初始化、回调）
│   ├── ring_buffer.h          # 无锁 SPSC 环形缓冲区
│   ├── socket_client.h        # Winsock2 客户端声明
│   ├── socket_client.cpp      # Winsock2 客户端实现
│   ├── adb_control.h          # ADB 控制声明
│   └── adb_control.cpp        # ADB 控制实现（启动 app、端口转发）
├── thirdparty/
│   └── miniaudio.h            # miniaudio 单头文件
└── README.md
```

## 依赖和工具

| 依赖 | 说明 | 获取方式 |
|------|------|----------|
| miniaudio | 单头文件音频库 | GitHub 下载 miniaudio.h |
| Winsock2 | Windows 原生 Socket | 系统自带，链接 ws2_32.lib |
| WASAPI | Windows 音频 API | 系统自带（miniaudio 自动使用） |
| VB-CABLE | 虚拟音频线缆驱动 | https://vb-audio.com/Cable/ |
| MSVC | Visual Studio 编译器 | VS 2022 Community |

## 核心组件

### 1. SPSCRingBuffer（无锁环形缓冲区）

- Single-Producer Single-Consumer 无锁队列
- 容量：64 个音频块 × 2048 bytes = 128KB
- 使用 std::atomic 保证线程安全
- 音频回调是实时线程，不能用 mutex

### 2. SocketClient（Winsock2 客户端）

- 连接 127.0.0.1:27183（ADB 端口转发）
- recv_exact 函数：确保接收完整的音频块
- 自动重连机制（断开后等待 1 秒重试）
- 统计：重连次数、接收块数

### 3. AudioBridge（音频桥接核心）

- miniaudio 设备初始化
- 查找 VB-CABLE 设备（名称匹配 "CABLE Input"）
- 音频回调：从环形缓冲区读取 → mono 转 stereo → 应用增益 → 输出
- 统计：丢块数、欠载数

### 4. ADBControl（ADB 命令封装）

- 检查 adb 是否在 PATH 中
- 检查设备连接状态（adb devices）
- 启动 Android AudioSource app（adb shell am start）
- 创建端口转发（adb forward tcp:27183 localabstract:audiosource）
- 清理端口转发（adb forward --remove）

## 实施步骤

### 步骤1：环境准备

1. 安装 Visual Studio 2022 Community（带 C++ 桌面开发工作负载）
2. 安装 VB-Audio Virtual Cable
3. 确保 adb 在 PATH 中（已有）

### 步骤2：下载 miniaudio

```cmd
mkdir thirdparty
curl -L https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h -o thirdparty\miniaudio.h
```

### 步骤3：创建源文件

按照文件结构创建所有源文件。

### 步骤4：编译

```cmd
build.bat
```

编译脚本内容：
```cmd
@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /O2 /EHsc /std:c++17 /I thirdparty src\*.cpp /Fe:audiosource.exe ws2_32.lib ole32.lib
```

### 步骤5：运行

```cmd
audiosource.exe
```

程序会自动：
1. 检查 ADB 连接
2. 启动 Android AudioSource app
3. 创建端口转发
4. 开始音频流传输

### 步骤6：在 Windows 应用中选择麦克风

在 Zoom/Teams/微信等应用中，选择 `CABLE Output` 作为麦克风输入。

## 优缺点

### 优点

- 单头文件，零外部依赖
- 代码简洁（~100 行核心代码）
- miniaudio 自动处理 WASAPI/COM 初始化
- 内置设备枚举、格式转换
- MIT 许可证，可商用

### 缺点

- miniaudio 库文件较大（~900KB 源码）
- 包含不需要的功能（捕获、重采样等）
- 编译后 exe 较大（~500KB-1MB）

## 预期结果

| 指标 | Python 版本 | miniaudio C++ 版本 |
|------|------------|-------------------|
| 内存占用 | 200+ MB | ~500KB - 1MB |
| 启动时间 | ~2-3 秒 | ~100ms |
| CPU 占用 | ~2-5% | ~0.5-1% |
| 依赖 | Python + numpy + sounddevice | 无（静态编译） |
| 分发 | 需要 Python 环境 | 单个 exe 文件 |

## 关键注意事项

1. **音频回调线程安全**：WASAPI/miniaudio 回调在实时线程运行，不能分配内存、不能用 mutex，必须用无锁队列
2. **mono → stereo 转换**：Android 发送 mono，VB-CABLE 通常需要 stereo，需要复制声道
3. **VB-CABLE 共享模式**：使用 AUDCLNT_SHAREMODE_SHARED，避免独占模式
4. **缓冲区欠载处理**：如果 socket 数据不及时，用静音填充而不是重放旧数据
5. **COM 初始化**：WASAPI 需要 COM，miniaudio 自动处理
