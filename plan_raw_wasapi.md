# 方案2：Raw WASAPI 实施计划

## 方案概述

直接调用 Windows WASAPI (Windows Audio Session API) COM 接口，不使用任何第三方音频库，实现最小内存占用。

## 架构设计

```
┌─────────────────────────────────────────────────────────────────┐
│                      C++ Win32 程序 (Raw WASAPI)                │
├─────────────────────────────────────────────────────────────────┤
│  [Socket 接收线程]                                               │
│    Winsock2 连接 127.0.0.1:27183                                │
│    接收 PCM 数据块                                               │
│    推入无锁环形缓冲区                                             │
├─────────────────────────────────────────────────────────────────┤
│  [音频渲染线程]                                                  │
│    WASAPI IAudioRenderClient::GetBuffer/ReleaseBuffer            │
│    从环形缓冲区读取 → mono 转 stereo → 写入 CABLE Input          │
│    使用 Sleep + 定时器控制节奏                                    │
├─────────────────────────────────────────────────────────────────┤
│  [设备枚举模块]                                                  │
│    IMMDeviceEnumerator 枚举音频端点                              │
│    查找名称包含 "CABLE Input" 的设备                              │
│    IPropertyStore 获取设备友好名称                                │
├─────────────────────────────────────────────────────────────────┤
│  [主线程]                                                       │
│    COM 初始化                                                   │
│    ADB 控制                                                     │
│    程序生命周期管理                                               │
└─────────────────────────────────────────────────────────────────┘
```

## 文件结构

```
vox_mic_raw_wasapi/
├── build.bat                  # MSVC 编译脚本
├── src/
│   ├── main.cpp               # 主函数
│   ├── wasapi_output.h        # WASAPI 输出封装
│   ├── wasapi_output.cpp      # WASAPI 实现（COM 调用）
│   ├── device_enum.h          # 音频设备枚举
│   ├── device_enum.cpp        # 设备枚举实现
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
| WASAPI | Windows 音频 API | 系统自带，链接 ole32.lib mmdevapi.lib |
| Winsock2 | Windows Socket | 系统自带，链接 ws2_32.lib |
| VB-CABLE | 虚拟音频线缆驱动 | https://vb-audio.com/Cable/ |
| MSVC | Visual Studio 编译器 | VS 2022 Community |

## 核心组件

### 1. WASAPIOutput（WASAPI 输出封装）

**关键接口**：
- `IMMDeviceEnumerator` - 枚举音频端点
- `IMMDevice` - 表示音频端点
- `IAudioClient` - 创建和初始化音频流
- `IAudioRenderClient` - 写入输出数据

**初始化流程**：
1. `CoInitializeEx(NULL, COINIT_MULTITHREADED)` - 初始化 COM
2. `CoCreateInstance(MMDeviceEnumerator)` - 创建设备枚举器
3. `EnumAudioEndpoints(eRender)` - 枚举渲染设备
4. 查找 "CABLE Input" 设备
5. `Activate(IAudioClient)` - 激活音频客户端
6. `GetMixFormat()` - 获取混合格式
7. `Initialize(AUDCLNT_SHAREMODE_SHARED)` - 初始化共享模式
8. `GetService(IAudioRenderClient)` - 获取渲染客户端

**渲染循环**：
```cpp
while (running) {
    UINT32 padding;
    pAudioClient->GetCurrentPadding(&padding);
    UINT32 framesAvailable = bufferFrameCount - padding;
    
    BYTE* pData;
    pRenderClient->GetBuffer(framesAvailable, &pData);
    
    // 从环形缓冲区读取数据，填充到 pData
    // mono 转 stereo
    
    pRenderClient->ReleaseBuffer(framesAvailable, 0);
    
    // 等待半个缓冲区时间
    Sleep(hnsActualDuration / REFTIMES_PER_MILLISEC / 2);
}
```

### 2. DeviceEnum（设备枚举）

**设备查找逻辑**：
1. 枚举所有活跃的渲染端点
2. 打开每个设备的属性存储
3. 读取 `PKEY_Device_FriendlyName`
4. 检查是否包含 "CABLE Input" 字符串
5. 如果找到，返回该设备；否则使用默认设备

**关键代码**：
```cpp
PROPVARIANT varName;
pProps->GetValue(PKEY_Device_FriendlyName, &varName);
if (wcsstr(varName.pwszVal, L"CABLE Input") != nullptr) {
    // 找到 VB-CABLE
}
```

### 3. SPSCRingBuffer（无锁环形缓冲区）

与方案1相同，容量 128KB。

### 4. SocketClient（Winsock2 客户端）

与方案1相同。

### 5. ADBControl（ADB 控制）

与方案1相同。

## 实施步骤

### 步骤1：环境准备

1. 安装 Visual Studio 2022 Community
2. 安装 VB-Audio Virtual Cable

### 步骤2：创建源文件

按照文件结构创建所有源文件。

### 步骤3：编译

```cmd
build.bat
```

编译脚本内容：
```cmd
@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /O2 /EHsc /std:c++17 src\*.cpp /Fe:audiosource.exe ws2_32.lib ole32.lib mmdevapi.lib
```

### 步骤4：运行

```cmd
audiosource.exe
```

### 步骤5：选择麦克风

在 Windows 应用中选择 `CABLE Output` 作为麦克风输入。

## 优缺点

### 优点

- 最小内存占用（~100-300KB）
- 无任何外部依赖
- 完全控制每个细节
- 编译后 exe 最小（~50-100KB）
- 深入理解 Windows 音频架构

### 缺点

- 代码复杂（~300-500 行核心代码）
- 需要手动处理 COM 初始化/释放
- 需要手动枚举设备、处理属性
- 需要手动处理 WAVEFORMATEX 格式协商
- 需要处理各种 HRESULT 错误码
- 调试困难（COM 错误信息不直观）

## 预期结果

| 指标 | Python 版本 | Raw WASAPI C++ 版本 |
|------|------------|---------------------|
| 内存占用 | 200+ MB | ~100-300KB |
| 启动时间 | ~2-3 秒 | ~50-100ms |
| CPU 占用 | ~2-5% | ~0.3-0.8% |
| 依赖 | Python + numpy + sounddevice | 无 |
| 分发 | 需要 Python 环境 | 单个 exe 文件 |

## 关键注意事项

1. **COM 线程模型**：必须使用 `COINIT_MULTITHREADED`，与 miniaudio 相同
2. **COM 引用计数**：每个 COM 接口都需要 Release()，否则内存泄漏
3. **HRESULT 检查**：每个 COM 调用都需要检查返回值
4. **WAVEFORMATEX 格式协商**：可能需要尝试不同的格式参数
5. **缓冲区大小**：1 秒缓冲区 vs 更小的缓冲区（延迟 vs 稳定性权衡）
6. **Sleep 精度**：Windows Sleep 精度有限（~15ms），可能需要使用更精确的定时器
7. **线程优先级**：音频线程可能需要提升优先级（THREAD_PRIORITY_TIME_CRITICAL）

## WASAPI 关键概念

### 时间单位

WASAPI 使用 100 纳秒为单位（REFERENCE_TIME）：
- 1 秒 = 10,000,000 (10^7)
- 10ms = 100,000

### 共享模式 vs 独占模式

- **共享模式**（推荐）：多个应用共享音频设备，延迟 ~10ms
- **独占模式**：独占设备，延迟 ~3ms，但会阻止其他应用

### 缓冲区大小

- 较大缓冲区（1 秒）：稳定，延迟高
- 较小缓冲区（10ms）：延迟低，可能欠载

## 调试技巧

1. 使用 `printf` 输出 HRESULT 错误码
2. 使用 `_com_error` 获取错误描述
3. 检查设备是否被其他应用独占
4. 使用 Windows 音频疑难解答
