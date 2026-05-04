# VoxMic

[简体中文](doc/zh-CN/README.md) | **English**

Use your Android phone's microphone as a Windows system microphone via ADB + VB-CABLE + Raw WASAPI. Supports on-demand activation: streaming only when a Windows app is using CABLE Output, DSP bypassed when idle.

## How It Works

```
Android Phone Mic -> [VoxMic Source App] -> ADB -> This Program -> VB-CABLE -> Windows App
                                                              |
                                    [Phase 3] Event-driven gate <- MicUsageMonitor (COM callback)
                                    [DSP] RNNoise -> HPF -> EQ -> Comp -> Limiter
```

## v0.5.0 Core Features

| Feature | Description |
|---------|-------------|
| **Event-driven Monitor** | `IAudioSessionNotification` + `IAudioSessionEvents` callbacks, zero polling, zero COM overhead |
| **Demand Mode toggle** | Tray context menu controls on-demand activation, persisted to registry |
| **Socket on-demand connection** | When Always Hot OFF, idle 5s disconnects socket, Android AudioRecord stops, zero power draw; reconnect in 0.3-0.8ms |
| **Always Hot toggle** | Tray context menu controls socket keep-alive, persisted to registry |
| **On-demand activation latency** | ~200ms cold start, event-driven instant detection |
| **Idle CPU 0-0.1%** | Event-driven + DSP on-demand bypass, on par with Python version |
| **RNNoise Neural Network Denoising** | Official xiph/rnnoise v0.2, 3-layer GRU, 22 Bark band independent denoising, BSD-3 |
| DSP Pipeline | HPF 80Hz + 6-band EQ + RMS Compressor + Peak Limiter |
| Low Latency | **~40ms** (measured) |

## System Requirements

- Windows 10/11
- Android device (USB debugging enabled)
- [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) installed
- ADB in PATH

## Usage

Launch directly, the program auto-hides to system tray:

```cmd
build\voxmic.exe
```

- **Left-click tray icon** -> Open settings window
- **Right-click tray icon** -> Demand Mode / Always Hot / Settings / Exit menu
- **Close window [X]** -> Hide to tray (does not exit)
- Select **CABLE Output** as microphone in Windows apps.

## Build

### Windows

```cmd
build.bat
```

Requires Visual Studio 2022 (C++ Desktop Development).

### Android App

```cmd
cd android_app
.\gradlew.bat assembleDebug --no-daemon --console=plain
adb -s <serial> install -r "app\build\outputs\apk\debug\VoxMic_Source-v0.5.3.apk"
```

## Performance

| Metric | v0.5.0 |
|--------|--------|
| CPU Idle | **0-0.1%** |
| CPU Active | ~0.1% (DSP) |
| Memory | ~15 MB |
| End-to-end Latency | **~40ms** |
| Cold Start Latency | **~200ms** |
| Binary Size | ~1.5 MB |

## Latency Budget

| Component | Latency |
|-----------|---------|
| Android ADC + HAL | ~10ms |
| AudioRecord read (480fr) | ~10ms |
| ADB + Socket | ~2ms |
| Ring Buffer | ~10ms (1-2 blocks) |
| RNNoise + EQ + Comp + Lim | ~50us |
| WASAPI Buffer | ~11ms |
| VB-CABLE | ~3ms |
| **Total** | **~40ms** |

## Pipeline Effects

| Stage | Parameters | Purpose |
|-------|-----------|---------|
| RNNoise | 22-band GRU neural network | Background noise reduction + voice preservation |
| HPF 80Hz | 12dB/oct | Cut wind noise/vibration |
| Bass Cut | 120Hz shelf + 250Hz attenuation (adjustable -6~0dB) | Reduce muddiness |
| Presence | 2.5kHz + 3.2kHz boost (adjustable 0~6dB) | Consonant clarity |
| Compressor | 3:1, 5ms attack, 50ms release | Even loudness |
| Limiter | -1dBFS ceiling | Prevent clipping |

## Documentation

[ARCHITECTURE.md](ARCHITECTURE.md) | [AGENTS.md](AGENTS.md) | [CHANGELOG.md](CHANGELOG.md) | [FUTURE_ROADMAP.md](FUTURE_ROADMAP.md) | [plan/completed/](plan/completed/) (historical plans)
