# AGENTS.md

[简体中文](doc/zh-CN/AGENTS.md) | **English**

Use your Android phone's microphone as a Windows system microphone via ADB + VB-CABLE + Raw WASAPI. On-demand activation: streaming only when an app is using CABLE Output, DSP bypassed when idle.

## Plan Management

- New plan -> `plan/ongoing/plan_<topic>.md`
- Implementation complete -> Move to `plan/completed/`
- **Do NOT** create `.md` plan files in the project root directory

## Version Bump

Version is unified in `src/version.h` (`APP_VERSION` macro). On each bump, modify this checklist item by item:

| # | File | Location/Line | Format |
|---|------|--------------|--------|
| 1 | `src/version.h` | `#define APP_VERSION` | `"x.y.z"` |
| 2 | `android_app/app/build.gradle` | `versionName` | Sync `APP_VERSION` string |
| 3 | `android_app/app/build.gradle` | `versionCode` | +1 (last=3) |

`src/tray_icon.cpp` auto-syncs via `#include "version.h"`, no manual change needed.

## Build & Run

```cmd
build.bat                        # Requires VS2022 C++
build\voxmic.exe                 # Launch (tray background)
build\voxmic.exe --list-devices  # List devices
```

Android App (SDK `D:\@APP\android-platform-sdk\android-sdk`, Gradle 8.7, JDK 17):
```powershell
cd android_app; .\gradlew.bat assembleDebug --no-daemon --console=plain
```

### Android Release Build

```powershell
cd android_app; .\gradlew.bat assembleRelease --no-daemon --console=plain
```

Output: `VoxMic_Source-v<versionName>.apk` (auto-named by `build.gradle`, no manual rename needed).

Requires `keystore.properties` + `voxmic.keystore` (both gitignored, new clone needs generation):

```powershell
& "C:\Program Files\Java\jdk-17\bin\keytool.exe" -genkey -v `
  -keystore android_app/voxmic.keystore -alias voxmic `
  -keyalg RSA -keysize 2048 -validity 10000 `
  -storepass voxmic123 -keypass voxmic123 `
  -dname "CN=VoxMic, OU=Dev, O=VoxMic, L=N/A, ST=N/A, C=CN"
```

Then create `android_app/keystore.properties`:
```
storePassword=voxmic123
keyPassword=voxmic123
keyAlias=voxmic
storeFile=../voxmic.keystore
```

Select **CABLE Output** as microphone in Windows apps. Tray icon: left-click=settings window, right-click=menu (Demand Mode / Always Hot / Exit), close window=hide.

## Source File Structure

```
src/
+-- main.cpp                     # Main thread + monitor/bridge threads
+-- wasapi_output.h/cpp          # WASAPI event-driven rendering (CABLE Input render endpoint)
+-- device_enum.h/cpp            # Device enumeration (findVBCableDevice etc.)
+-- ring_buffer.h                # SPSC ring buffer
+-- socket_client.h/cpp          # TCP socket client
+-- adb_control.h/cpp            # ADB (CreateProcess + CREATE_NO_WINDOW)
+-- tray_icon.h/cpp              # System tray + context menu
+-- config.h/cpp                 # config.ini persistence (21 fields)
+-- settings_dialog.h/cpp        # Modeless settings window
+-- mic_usage_monitor.h/cpp      # Event-driven COM + IAudioMeterInformation silence fallback
+-- dsp/
    +-- pipeline.h               # DSP chain (RNNoise->HPF->EQ->Comp->Limiter)
    +-- biquad.h                 # BiQuad IIR
    +-- rnnoise/ (27 files)      # Official RNNoise v0.2 (C compiled, no external deps)
```

## Thread Model

```
main:      Message pump + SetTimer(stats)
monitor:   idle Sleep(1000) / active every 1s GetPeakValue()
bridge:    ADB management + Socket recv -> g_micRequested gate -> ring buffer
render:    ring buffer pop -> int16->float -> DspPipeline -> WASAPI write
```

## Key Parameters

| Parameter | Value | Location |
|-----------|-------|----------|
| FRAMES_PER_BLOCK / BLOCK_SIZE | 480 / 960B | `wasapi_output.h:14-16` |
| SAMPLE_RATE | 48000 Hz | `wasapi_output.h:12` |
| RING_BUFFER_BLOCKS | 128 | `wasapi_output.h:17` |
| WASAPI buffer | ~22ms (shared mode lower limit) | `wasapi_output.cpp:73` |
| Ring buffer watermarks | 3->2 | `main.cpp` |
| Android AudioRecord | 1x minBufSize | `RecordService.java` |
| Total latency | ~40ms | |

### On-demand Activation

| Mechanism | Trigger | Latency | Overhead |
|-----------|---------|---------|----------|
| OnStateChanged (event) | COM callback | Instant | Zero |
| renderStallScore | render event 3x timeout | ~6s | Zero (existing) |
| IAudioMeterInformation | monitor thread (active state only) | ~3s | 1 COM/sec |
| **Idle state** | Sleep(1000) loop | -- | **Zero CPU / Zero COM** |

| Threshold | Value |
|-----------|-------|
| Socket stall disconnect | 9s (90x100ms) |
| Socket idle disconnect | 5s (500 blocks, AlwaysHot OFF) |
| Ring buffer reset | Every 50 blocks (0.5s) |
| Bridge reconnect | ~0.3-0.8ms QPC |

### DSP Pipeline & Configuration

DSP: RNNoise(22-Bark GRU) -> HPF(80Hz) -> EQ(6-band, Pres 0-6dB, Bass -6-0dB) -> Comp(-18dBFS, 3:1) -> Limiter(-1dBFS)

| Atomic Variable | Purpose | Thread |
|-----------------|---------|--------|
| `g_gain` | Gain multiplier | bridge -> render |
| `g_nrEnabled` / `g_eqEnabled` / `g_compressorEnabled` | DSP toggles | bridge -> render |
| `g_nrStrength` | NR denoising strength (0.3-0.95) | bridge -> render |
| `g_eqPresence` / `g_eqBassCut` | EQ parameters | bridge -> render |
| `g_micRequested` | Whether app is capturing | monitor -> bridge |
| `g_micStreaming` | Whether streaming | bridge -> tray |
| `g_micOnTick` | Detection latency timestamp | monitor -> bridge |
| `g_demandMode` | Demand Mode toggle | tray -> monitor/bridge |
| `g_alwaysHot` | Always Hot toggle | tray -> bridge |

Configuration 21 fields, config.ini persistence. `syncDspAtomsFromConfig()` in `main.cpp`, called on startup and reconnect.

### Monitor Three-layer Detection

1. **COM event callback** (`IAudioSessionNotification` + `IAudioSessionEvents`): Targets CABLE Output capture endpoint (`EnumAudioEndpoints(eCapture)` name match, fallback to default if not found)
2. **Render event timeout** (`wasapi_output.cpp`): 3 consecutive `WaitForSingleObject` timeouts -> `renderStallScore=3`, bridge uses `effectiveActive = demandOff || (micRequested && !renderStalled)` gate
3. **IAudioMeterInformation** (`mic_usage_monitor.cpp`): When active, every 1s `GetPeakValue()`, 3s consecutive peak below threshold -> force `g_micRequested=false`; not called when idle
