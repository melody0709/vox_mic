# VoxMic

[简体中文](doc/zh-CN/README.md) | **English**

Use your Android phone's microphone as a Windows system microphone via ADB + VB-CABLE + Raw WASAPI. Supports on-demand activation: streaming only when a Windows app is using CABLE Output, DSP bypassed when idle. Noise reduction can use the built-in RNNoise backend or the optional DPDFNet 48 kHz backend.

## How It Works

```
Android Phone Mic -> [VoxMic Source App] -> ADB -> This Program -> VB-CABLE -> Windows App
                                                              |
                                    [Phase 3] Event-driven gate <- MicUsageMonitor (COM callback)
                                    [DSP] RNNoise/DPDFNet -> HPF -> EQ -> Comp -> Limiter
```

## v0.6.5 DPDFNet worker hardening

The DPDFNet worker now converges cleanly after a hard failure: it clears its ready state, stops polling, and exits quickly when the processor is destroyed. Epoch hand-off now uses the input block's epoch tag so a new-epoch first block is reset and processed instead of being discarded; superseded epochs and stale inference results remain filtered. Invalid, oversized, or non-finite model output causes a hard fallback to RNNoise. Diagnostics and Settings distinguish disabled noise reduction (`off`) from RNNoise and DPDFNet.

The failure smoke test covers failed-worker lifecycle, `setEpoch()` short-circuiting, and the sub-100 ms normal destructor path. A native DLL call that never returns remains a documented limitation; it is not handled by unsafe thread detachment.

## v0.6.4 DPDFNet resilience

DPDFNet stream resets now preserve the first block submitted for the new epoch. Its worker output is also watched: up to four 10 ms warm-up blocks may be silent after a reset to keep the delayed stream aligned, but a steady worker that produces no output for three consecutive blocks automatically falls back to RNNoise instead of muting the microphone indefinitely. The Settings status and periodic diagnostics report the degraded state; clicking OK retries a ready-but-stalled worker, while a hard session failure requires a fresh prepare.

## v0.6.2 Fix

Fixed the Denoise Backend status label so switching between RNNoise and DPDFNet cannot leave overlapping text. The status control now repaints opaquely and reserves room for two-line availability messages.

## v0.6.1 Highlights

| Feature | Description |
|---------|-------------|
| **Selectable denoiser** | Keep RNNoise as the default, or select DPDFNet from the DSP settings. |
| **Streaming DPDFNet adapter** | Uses the pinned 48 kHz / 480-sample model through a worker and FIFO, without blocking the WASAPI render thread. |
| **Safe fallback** | Missing DLLs, model files, ABI symbols, initialization failures, or a stalled worker keep the application on RNNoise. |
| **Rebuildable payload** | The pinned DPDFNet runtime, model, C API header, metadata, and notices live under `third_party/dpdfnet`; `build/` can be deleted and regenerated. |

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
build\run\x64-release\voxmic.exe
```

- **Left-click tray icon** -> Open settings window
- **Right-click tray icon** -> Demand Mode / Always Hot / Settings / Exit menu
- **Close window [X]** -> Hide to tray (does not exit)
- Select **CABLE Output** as microphone in Windows apps.

## Build

### Windows

```cmd
build.bat

git lfs pull
build.bat --dpdfnet
build.bat --dpdfnet --test-dpdfnet
```

Requires Visual Studio 2022 (C++ Desktop Development).

`build.bat` produces the RNNoise-only development payload. The optional payload is vendored in `third_party/dpdfnet/` and managed with Git LFS; after cloning, run `git lfs pull` once so the model and DLLs are materialized. `build.bat --dpdfnet` verifies those files and stages them into `build/cmake/x64-release/_deps/dpdfnet` before configuring CMake, so a clean `build/` can be rebuilt without network access. The preparation script retains a verified download/cache fallback only when the vendored directory is unavailable. To package the optional payload, use `build.bat --dpdfnet --package`.

The vendored payload contains the sherpa-onnx C API header, three Windows x64 runtime DLLs, and `dpdfnet2_48khz_hr.onnx`; its SHA-256 values are recorded in `third_party/dpdfnet/metadata.json`. Third-party license notices are in `third_party/DPDFNET_THIRD_PARTY_NOTICES.txt`.

### Android App

```cmd
cd android_app
.\gradlew.bat assembleDebug --no-daemon --console=plain
adb -s <serial> install -r "app\build\outputs\apk\debug\VoxMic_Source-v0.6.5.apk"
```

## Performance

| Metric | v0.6.5 |
|--------|--------|
| CPU Idle | **0-0.1%** |
| CPU Active | ~0.1% (DSP) |
| Memory | ~15 MB |
| End-to-end Latency | **~40ms** |
| Cold Start Latency | **~200ms** |
| Binary Size | ~1.5 MB |

The DPDFNet worker smoke test on the reference machine reports about 1.7 ms inference EMA per 480-sample block. This is an adapter measurement, not a promise of identical end-to-end latency on every Windows system. The pipeline-switch smoke also verifies RNNoise ↔ DPDFNet switching, repeated epoch resets, and the stalled-worker downgrade path.

## Latency Budget

| Component | Latency |
|-----------|---------|
| Android ADC + HAL | ~10ms |
| AudioRecord read (480fr) | ~10ms |
| ADB + Socket | ~2ms |
| Ring Buffer | ~10ms (1-2 blocks) |
| RNNoise + EQ + Comp + Lim | ~50us |
| DPDFNet worker inference | ~1.7ms EMA in smoke test; reset warm-up is bounded to four 10ms silent blocks before safe fallback |
| WASAPI Buffer | ~11ms |
| VB-CABLE | ~3ms |
| **Total** | **~40ms** |

## Pipeline Effects

| Stage | Parameters | Purpose |
|-------|-----------|---------|
| RNNoise | 22-band GRU neural network | Background noise reduction + voice preservation |
| DPDFNet | 48 kHz online ONNX model, worker + FIFO + watchdog | Optional alternative denoiser; `NR Strength` applies only to RNNoise; stalled worker safely uses RNNoise |
| HPF 80Hz | 12dB/oct | Cut wind noise/vibration |
| Bass Cut | 120Hz shelf + 250Hz attenuation (adjustable -6~0dB) | Reduce muddiness |
| Presence | 2.5kHz + 3.2kHz boost (adjustable 0~6dB) | Consonant clarity |
| Compressor | 3:1, 5ms attack, 50ms release | Even loudness |
| Limiter | -1dBFS ceiling | Prevent clipping |

## Documentation

[ARCHITECTURE.md](ARCHITECTURE.md) | [AGENTS.md](AGENTS.md) | [CHANGELOG.md](CHANGELOG.md) | [FUTURE_ROADMAP.md](FUTURE_ROADMAP.md) | [plan/completed/](plan/completed/) / [.plan/completed/](.plan/completed/) (historical plans)
