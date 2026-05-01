# VoxMic Source — Android App

基于 [gdzx/audiosource](https://github.com/gdzx/audiosource) 修改的高音质版本。

## 改动

| 项 | 原始 | VoxMic |
|----|------|--------|
| 包名 | `fr.dzx.audiosource` | `com.voxmic.source` |
| 应用名 | Audio Source | VoxMic Source |
| Socket 名 | `audiosource` | `voxmicsource` |
| AudioSource | `DEFAULT` | `VOICE_COMMUNICATION` (硬件降噪+AEC+AGC) |
| NoiseSuppressor | 无 | 启用 |
| AcousticEchoCanceler | 无 | 启用 |
| AutomaticGainControl | 无 | 启用 |
| 采样率 | 44100 Hz | 48000 Hz |
| 块大小 | ~661 字节 | 2048 字节 (与 Windows BLOCK_SIZE 对齐) |

## 构建

需要 Android SDK (API 34) 和 Gradle。

### 方式 1: Android Studio

1. 用 Android Studio 打开 `android_app/` 目录
2. Build → Build APK
3. 安装到手机

### 方式 2: 命令行

```bash
cd android_app
./gradlew assembleDebug
# APK 输出: app/build/outputs/apk/debug/app-debug.apk
```

### 方式 3: 生成 Gradle Wrapper

```bash
cd android_app
gradle wrapper --gradle-version 8.2
```

## 图标

`ic_launcher` 和 `ic_launcher_round` 需要放在 `app/src/main/res/mipmap-*/` 下。
可从原始 audiosource 项目复制，或使用 Android Studio 的 Image Asset 工具生成。

## 与原版共存

本 App 的包名和应用名完全不同，可与原版 `gdzx/audiosource` 同时安装在同一设备上。
Windows 端可通过 Settings 对话框中切换 App 来 A/B 对比音质。
