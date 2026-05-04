# VoxMic Source — Android App

[简体中文](../doc/zh-CN/android_app/README.md) | **English**

A high-quality version modified from [gdzx/audiosource](https://github.com/gdzx/audiosource).

## Changes

| Item | Original | VoxMic |
|------|----------|--------|
| Package name | `fr.dzx.audiosource` | `com.voxmic.source` |
| App name | Audio Source | VoxMic Source |
| Socket name | `audiosource` | `voxmicsource` |
| AudioSource | `DEFAULT` | `VOICE_COMMUNICATION` (hardware NR+AEC+AGC) |
| NoiseSuppressor | None | Enabled |
| AcousticEchoCanceler | None | Enabled |
| AutomaticGainControl | None | Enabled |
| Sample rate | 44100 Hz | 48000 Hz |
| Block size | ~661 bytes | 2048 bytes (aligned with Windows BLOCK_SIZE) |

## Build

Requires Android SDK (API 34) and Gradle.

### Option 1: Android Studio

1. Open the `android_app/` directory in Android Studio
2. Build → Build APK
3. Install on phone

### Option 2: Command Line

```bash
cd android_app
./gradlew assembleDebug
# APK output: app/build/outputs/apk/debug/app-debug.apk
```

### Option 3: Generate Gradle Wrapper

```bash
cd android_app
gradle wrapper --gradle-version 8.2
```

## Icons

`ic_launcher` and `ic_launcher_round` need to be placed in `app/src/main/res/mipmap-*/`.
You can copy them from the original audiosource project, or generate them using Android Studio's Image Asset tool.

## Coexistence with Original

This app has a completely different package name and app name, and can coexist with the original `gdzx/audiosource` on the same device.
The Windows side can switch between apps in the Settings dialog for A/B audio quality comparison.