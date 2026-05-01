# 方案3：PortAudio 实施计划

## 方案概述

使用 PortAudio 成熟 C 库（2001 年至今）+ Winsock2 实现音频桥接。PortAudio 是跨平台音频库，支持多种后端。

## 架构设计

```
┌─────────────────────────────────────────────────────────────────┐
│                      C++ Win32 程序 (PortAudio)                 │
├─────────────────────────────────────────────────────────────────┤
│  [Socket 接收线程]                                               │
│    Winsock2 连接 127.0.0.1:27183                                │
│    接收 PCM 数据块                                               │
│    推入无锁环形缓冲区                                             │
├─────────────────────────────────────────────────────────────────┤
│  [PortAudio 回调线程]                                            │
│    Pa_OpenStream / Pa_StartStream                               │
│    回调函数：从环形缓冲区读取 → mono 转 stereo → 写入输出         │
│    PortAudio 内部使用 WASAPI 后端                                │
├─────────────────────────────────────────────────────────────────┤
│  [设备枚举模块]                                                  │
│    Pa_GetDeviceCount / Pa_GetDeviceInfo                         │
│    查找名称包含 "CABLE Input" 的设备                              │
├─────────────────────────────────────────────────────────────────┤
│  [主线程]                                                       │
│    Pa_Initialize / Pa_Terminate                                 │
│    ADB 控制                                                     │
│    程序生命周期管理                                               │
└─────────────────────────────────────────────────────────────────┘
```

## 文件结构

```
vox_mic_portaudio/
├── build.bat                  # MSVC 编译脚本
├── lib/
│   └── portaudio_x64.lib     # PortAudio 静态库（预编译或自己编译）
├── include/
│   └── portaudio.h            # PortAudio 头文件
├── src/
│   ├── main.cpp               # 主函数
│   ├── audio_bridge.h         # 音频桥接类声明
│   ├── audio_bridge.cpp       # PortAudio 初始化、回调
│   ├── ring_buffer.h          # 无锁环形缓冲区
│   ├── socket_client.h        # Winsock2 客户端
│   ├── socket_client.cpp
│   ├── adb_control.h          # ADB 控制
│   └── adb_control.cpp
└── README.md
```

## 依赖和工具

| 依赖 | 说明 | 获取方式 |
|------|------|----------|
| PortAudio | 跨平台音频 C 库 | http://portaudio.com/ |
| Winsock2 | Windows Socket | 系统自带，链接 ws2_32.lib |
| VB-CABLE | 虚拟音频线缆驱动 | https://vb-audio.com/Cable/ |
| MSVC | Visual Studio 编译器 | VS 2022 Community |

## 获取 PortAudio

### 方法1：下载预编译库

从 http://portaudio.com/ 下载预编译的 Windows 库。

### 方法2：自己编译

```cmd
git clone https://github.com/PortAudio/portaudio.git
cd portaudio
cmake -B build -A x64 -DPA_USE_WASAPI=ON -DPA_USE_DS=OFF -DPA_USE_WMME=OFF
cmake --build build --config Release
```

编译后得到：
- `build/Release/portaudio_x64.lib`（静态库）
- `include/portaudio.h`（头文件）

## 核心组件

### 1. AudioBridge（PortAudio 音频桥接）

**PortAudio 初始化流程**：
1. `Pa_Initialize()` - 初始化 PortAudio
2. `Pa_GetDeviceCount()` - 获取设备数量
3. `Pa_GetDeviceInfo()` - 遍历设备信息
4. 查找名称包含 "CABLE Input" 的设备
5. `Pa_OpenStream()` - 打开音频流（回调模式）
6. `Pa_StartStream()` - 开始播放

**回调函数**：
```cpp
static int audioCallback(
    const void* input,          // 未使用（播放模式）
    void* output,               // 输出缓冲区
    unsigned long frameCount,   // 帧数
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData              // 用户数据（AudioBridge 指针）
) {
    AudioBridge* bridge = (AudioBridge*)userData;
    int16_t* out = (int16_t*)output;
    
    // 从环形缓冲区读取 mono 数据
    if (bridge->m_ringBuffer.pop((uint8_t*)bridge->m_tempBuffer, frameCount * 2)) {
        // mono 转 stereo
        for (unsigned long i = 0; i < frameCount; i++) {
            out[i * 2] = bridge->m_tempBuffer[i];     // Left
            out[i * 2 + 1] = bridge->m_tempBuffer[i]; // Right
        }
    } else {
        // 欠载，填充静音
        memset(output, 0, frameCount * 2 * sizeof(int16_t));
        bridge->m_underruns++;
    }
    
    return paContinue;
}
```

**打开音频流**：
```cpp
PaStreamParameters outputParams;
outputParams.device = cableDeviceIndex;
outputParams.channelCount = 2;  // stereo
outputParams.sampleFormat = paInt16;
outputParams.suggestedLatency = Pa_GetDeviceInfo(cableDeviceIndex)->defaultLowOutputLatency;
outputParams.hostApiSpecificStreamInfo = nullptr;

PaStream* stream;
Pa_OpenStream(
    &stream,
    nullptr,           // 无输入
    &outputParams,     // 输出参数
    44100,             // 采样率
    1024,              // 每缓冲区帧数
    paClipOff,         // 不裁剪
    audioCallback,     // 回调函数
    this               // 用户数据
);
```

### 2. DeviceEnum（设备枚举）

```cpp
int findVBCableDevice() {
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info->maxOutputChannels > 0) {
            if (strstr(info->name, "CABLE Input") != nullptr) {
                return i;
            }
        }
    }
    return Pa_GetDefaultOutputDevice(); // 回退到默认设备
}
```

### 3. SPSCRingBuffer（无锁环形缓冲区）

与方案1相同。

### 4. SocketClient（Winsock2 客户端）

与方案1相同。

### 5. ADBControl（ADB 控制）

与方案1相同。

## 实施步骤

### 步骤1：环境准备

1. 安装 Visual Studio 2022 Community
2. 安装 VB-Audio Virtual Cable

### 步骤2：获取 PortAudio

**方法A：下载预编译库**
```cmd
mkdir lib include
# 从 http://portaudio.com/ 下载预编译库
# 将 portaudio_x64.lib 放入 lib/
# 将 portaudio.h 放入 include/
```

**方法B：自己编译**
```cmd
git clone https://github.com/PortAudio/portaudio.git
cd portaudio
cmake -B build -A x64 -DPA_USE_WASAPI=ON
cmake --build build --config Release
copy build\Release\portaudio_x64.lib ..\lib\
copy include\portaudio.h ..\include\
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
cl /O2 /EHsc /std:c++17 /I include src\*.cpp /Fe:audiosource.exe /link /LIBPATH:lib portaudio_x64.lib ws2_32.lib ole32.lib
```

### 步骤5：运行

```cmd
audiosource.exe
```

### 步骤6：选择麦克风

在 Windows 应用中选择 `CABLE Output` 作为麦克风输入。

## 优缺点

### 优点

- 成熟稳定（2001 年至今，20+ 年历史）
- 文档完善，社区活跃
- 跨平台（Windows/macOS/Linux）
- 封装良好，API 简洁
- 支持多种后端（WASAPI、DirectSound、MME、ASIO）
- 内存占用适中（~200-400KB）

### 缺点

- 需要预编译库或自己编译
- 库文件较大（~200KB）
- 比 miniaudio 复杂（需要处理更多参数）
- 对于纯 Windows 项目，跨平台优势无意义

## 预期结果

| 指标 | Python 版本 | PortAudio C++ 版本 |
|------|------------|---------------------|
| 内存占用 | 200+ MB | ~200-400KB |
| 启动时间 | ~2-3 秒 | ~100-200ms |
| CPU 占用 | ~2-5% | ~0.5-1% |
| 依赖 | Python + numpy + sounddevice | PortAudio 静态库 |
| 分发 | 需要 Python 环境 | 单个 exe 文件 |

## 关键注意事项

1. **PortAudio 版本**：使用最新稳定版（v19.7+），确保 WASAPI 支持
2. **编译选项**：只启用 WASAPI 后端，禁用 DirectSound 和 MME，减小体积
3. **回调线程安全**：与 miniaudio 相同，回调在实时线程，不能用 mutex
4. **缓冲区大小**：PortAudio 的 `framesPerBuffer` 参数，建议 1024
5. **错误处理**：所有 PortAudio 函数返回 PaError，需要检查
6. **资源释放**：Pa_CloseStream / Pa_Terminate 必须配对调用

## PortAudio 关键概念

### Host API

PortAudio 支持多种后端（Host API）：
- `paWASAPI` (13) - 推荐，现代 Windows 音频
- `paDirectSound` (1) - 较旧
- `paMME` (2) - 最旧
- `paASIO` (3) - 专业音频

可以通过 `Pa_GetHostApiCount()` 和 `Pa_GetHostApiInfo()` 查询。

### 设备类型

- 输入设备：`maxInputChannels > 0`
- 输出设备：`maxOutputChannels > 0`

### 回调模式 vs 阻塞模式

- **回调模式**（推荐）：PortAudio 在独立线程调用回调函数
- **阻塞模式**：使用 Pa_ReadStream / Pa_WriteStream，需要自己管理线程

## 调试技巧

1. 使用 `Pa_GetErrorText()` 获取错误描述
2. 检查 `Pa_GetLastHostErrorInfo()` 获取主机错误
3. 使用 `--list-devices` 参数列出所有设备
4. 检查 PortAudio 版本：`Pa_GetVersion()`

## 与 miniaudio 的对比

| 特性 | PortAudio | miniaudio |
|------|-----------|-----------|
| 历史 | 20+ 年 | ~5 年 |
| 语言 | C | C |
| 文件数 | 多个（需要编译） | 单头文件 |
| 依赖 | 需要静态库 | 无 |
| API 风格 | C 函数式 | C 结构体式 |
| WASAPI 支持 | 通过 host API | 原生默认 |
| 内存占用 | ~200-400KB | ~500KB-1MB |
| 学习曲线 | 中等 | 低 |

## 总结

PortAudio 是一个成熟的选择，适合：
- 需要跨平台支持的项目
- 已有 PortAudio 经验的团队
- 需要多种音频后端的场景

对于纯 Windows 项目，miniaudio 更简单直接。
