# Architecture

[简体中文](doc/zh-CN/ARCHITECTURE.md) | **English**

## Data Flow

```
Android Phone Microphone (48000Hz/Mono/int16)
    |
VoxMic Source App (DEFAULT source + optional NS/AEC/AGC)
    | USB ADB
ADB Forward (tcp:27183)
    | TCP (960 bytes/block = 480 frames)
voxmic.exe
    +-- MicUsageMonitor Thread (Event-driven: IAudioSessionNotification + IAudioSessionEvents)
    |       +-> g_micRequested (atomic<bool>)
    |
    +-- Socket Receive Thread (On-demand connection: idle 5s disconnect, connect ~0.4ms measured)
    |       |   recvExact(960) -> g_micRequested ? push : discard
    |       |
    |   SPSC Lock-free Ring Buffer (128 blocks x 960 bytes)
    |       int16 -> float32 (480 frames)
    |           |
    |   +---------------------------------------------+
    |   |  DSP Pipeline (src/dsp/)                    |
    |   |  1. RNNoise 22-Bark GRU denoise + comb      |
    |   |  2. HPF 80Hz BiQuad IIR                     |
    |   |  3. EQ 6-band (Presence/Bass Cut adj.)      |
    |   |  4. RMS Compressor (3:1, 5ms/50ms)          |
    |   |  5. Peak Limiter (-1dBFS)                   |
    |   +---------------------------------------------+
    |           |
    +-- WASAPI Event-driven Render Thread (ratio=1.0 passthrough + Gain)
            |
    VB-CABLE Input (48000Hz/Stereo)
            |
    VB-CABLE Output
            |
    Windows Applications (Zoom, Teams, Sound Recorder, Voice Input)
```

## Source File Overview

| File | Responsibility |
|------|---------------|
| `main.cpp` | Entry point, main window creation (modeless), bridge thread (Always Hot), monitor thread, stats timer, DSP atomic variable sync |
| `wasapi_output.h/cpp` | WASAPI event-driven init, render loop, Gain + DSP pipeline injection, QPC timing |
| `device_enum.h/cpp` | WASAPI device enumeration, VB-CABLE lookup |
| `ring_buffer.h` | Lock-free SPSC ring buffer |
| `socket_client.h/cpp` | Winsock2 TCP client (includes waitForData) |
| `adb_control.h/cpp` | ADB commands, device detection, App launch, port forwarding, **forward quick refresh** (**CreateProcess + CREATE_NO_WINDOW**, no flicker) |
| `tray_icon.h/cpp` | System tray + context menu (includes gray version number) |
| `config.h/cpp` | config.ini persistence (**21 fields**) |
| `settings_dialog.h/cpp` | **Main window** GUI (device/network/app/audio/DSP/Debug, modeless persistent window) |
| **`mic_usage_monitor.h/cpp`** | Phase 3+8: Event-driven (IAudioSessionNotification + IAudioSessionEvents) |
| **`dsp/biquad.h`** | BiQuad IIR (HPF/LowShelf/Peak/HighShelf) |
| **`dsp/pipeline.h`** | DSP chain scheduling (RNNoise->HPF->EQ->Comp->Limiter) |
| **`dsp/rnnoise/` (27 files)** | Official RNNoise v0.2 source + pre-generated model (from werman fork) |

## DSP Pipeline Details

```
480 frames float32[] input
    |
[1] RNNoise: rnnoise_process_frame(st, out, in)  <- g_nrEnabled control
    - 3-layer GRU (96+96+96), 22 Bark bands, 85KB quantized weights
    - Per-band independent gain + comb filtering + VAD probability
    - Latency: 10ms (one frame)
    |
[2] HPF: Biquad 80Hz 12dB/oct  <- always active
    |
[3] EQ 6-band:  <- g_eqEnabled control
    - 120Hz LowShelf (eqBassCut)
    - 250Hz Peaking (0.5xBassCut)
    - 2.5kHz Peaking (eqPresence)
    - 3.2kHz Peaking (0.6xPresence)
    - 8kHz HighShelf (+1.5dB)
    |
[4] RMS Compressor:  <- g_compressorEnabled control
    - -18dBFS threshold, 3:1 ratio, 6dB soft knee
    - 5ms attack, 50ms release, +5dB makeup
    |
[5] Peak Limiter: -1dBFS  <- always active
    |
480 frames float32[] output -> Gain -> Interpolation -> Stereo -> WASAPI
```

## DSP Parameter Configuration

| Parameter | Default | Atomic Variable | Settings |
|-----------|---------|-----------------|----------|
| NR Enable | true | `g_nrEnabled` | checkbox |
| EQ Enable | true | `g_eqEnabled` | checkbox |
| Presence | +3.0 dB | `g_eqPresence` | slider 0-6dB |
| Bass Cut | -3.0 dB | `g_eqBassCut` | slider -6-0dB |
| Comp Enable | true | `g_compressorEnabled` | checkbox |

## Phase 3+8: On-demand Activation + Event-driven

| Parameter | Value | Atomic Variable | Thread |
|-----------|-------|-----------------|--------|
| Monitor detection | Event-driven (COM callback) | `g_micRequested` | monitor |
| Streaming gate | discard when `!g_micRequested` | `g_micStreaming` | bridge -> tray |
| Detection latency | Instant (COM callback) | `g_micOnTick` | monitor -> bridge |
| Demand Mode toggle | Tray context menu, persisted to registry | `g_demandMode` | tray -> monitor |
| Idle ring buffer reset | 5s (50 x 100ms) | -- | bridge |
| Socket idle disconnect | 5s (500 blocks) Always Hot OFF | `g_alwaysHot` | bridge |
| Socket stall disconnect | 9s (90 x 100ms) | -- | bridge |

## Thread Model

```
main thread:         Message pump + SetTimer(stats, 5s)
monitor thread:      Sleep(1000) only keeps COM apartment alive (Phase 8 event-driven)
bridge thread:       ADB one-time init (CreateProcess NO_WINDOW) + Socket on-demand connection (idle 5s disconnect, connect ~0.4ms) -> g_micRequested gate -> ring buffer push/discard
render thread:       Event-driven ring buffer pop -> int16->float -> DspPipeline (47us/block) -> WASAPI write
```

## Latency Budget

| Component | Latency |
|-----------|---------|
| Android ADC + HAL | ~10ms |
| AudioRecord read (480fr) | ~10ms |
| ADB + Socket | ~2ms |
| Ring Buffer | ~10ms (1-2 blocks) |
| RNNoise + EQ + Comp + Limiter | ~50us (measured) |
| WASAPI Buffer | ~11ms (VB-CABLE 22ms half-buffer) |
| VB-CABLE | ~3ms |
| **Total** | **~40ms** (measured) |

## WASAPI Event-driven

```
CreateEventEx -> SetEventHandle -> Pre-fill silence -> Start
  |
WaitForSingleObject(hEvent, 2000) -> GetCurrentPadding -> Calculate available frames
  |
Inner while: pop ring buffer -> int16->float -> DspPipeline.process() -> Gain -> ReleaseBuffer
  |
Stop
```

## Android App Communication

DSP parameters (NR/EQ/Presence/BassCut/Comp) are processed on the Windows side only, no communication with Android.

Android side only passes audio effect flags (NS/AEC/AGC) to `RecordService`.
