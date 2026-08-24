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
    +-- MicUsageMonitor Thread (per-session events + 200ms reconciliation)
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
    |   |  1. Selected denoiser: RNNoise or DPDFNet   |
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
| `config.h/cpp` | config.ini persistence (**20 fields**) |
| `settings_dialog.h/cpp` | **Main window** GUI (device/network/app/audio/DSP/Debug, modeless persistent window) |
| **`mic_usage_monitor.h/cpp`** | Per-session `IAudioSessionEvents`, periodic reconciliation, Demand Mode debounce/fail-open policy |
| **`mic_session_state.h`** | Identity-aware, idempotent capture-session activity tracker used by the monitor and regression test |
| **`dsp/biquad.h`** | BiQuad IIR (HPF/LowShelf/Peak/HighShelf) |
| **`dsp/pipeline.h`** | DSP chain scheduling (RNNoise/DPDFNet->HPF->EQ->Comp->Limiter) |
| **`dsp/dpdfnet_processor.h/cpp`** | Optional pinned-header sherpa-onnx C ABI loader, worker thread, epoch reset/watchdog, and 480-sample FIFO adapter |
| **`dsp/rnnoise/` (27 files)** | Official RNNoise v0.2 source + pre-generated model (from werman fork) |

## DSP Pipeline Details

```
480 frames float32[] input
    |
[1] Selected denoiser <- g_nrEnabled + DenoiseBackend control
    - RNNoise: rnnoise_process_frame(st, out, in), adjustable `NR Strength`
    - DPDFNet: 48 kHz online ONNX model on a dedicated worker; variable API output is buffered into fixed 480-sample blocks
    - New-epoch input is reset and processed using its own tag; old-tagged or superseded input/result blocks are discarded without entering the active FIFO
    - DPDFNet DLL/model/ABI failure or worker stall falls back to RNNoise without stopping the audio path
    - Malformed, oversized, or non-finite model output hard-fails the session before FIFO insertion
    - At reset, up to four empty 10ms blocks are allowed for model context; three consecutive steady-state empty blocks mark the worker degraded and use RNNoise
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
| Denoise Backend | `rnnoise` | `g_denoiseBackend` / `DenoiseBackend` | RNNoise or DPDFNet combo |
| EQ Enable | true | `g_eqEnabled` | checkbox |
| Presence | +3.0 dB | `g_eqPresence` | slider 0-6dB |
| Bass Cut | -3.0 dB | `g_eqBassCut` | slider -6-0dB |
| Comp Enable | true | `g_compressorEnabled` | checkbox |

`NR Strength` is preserved in `config.ini` for RNNoise compatibility and is disabled in the UI when DPDFNet is selected. When NR is disabled, the effective backend is `off`; the requested backend remains persisted and is applied again when NR is enabled. The requested backend also remains persisted when DPDFNet resources are unavailable; the effective backend is reported separately and remains RNNoise until a fresh prepare succeeds. `g_dpdfnetDegraded` distinguishes a ready-but-stalled worker from a missing runtime and is cleared for a retry at the next stream reset. DPDFNet's online API does not expose a model-strength slider; VoxMic pins the 48 kHz `dpdfnet2_48khz_hr` model, CPU provider, one inference thread, and debug off. Those are packaging/developer choices, not Settings controls.

## Phase 3+8: On-demand Activation + Event-driven

| Parameter | Value | Atomic Variable | Thread |
|-----------|-------|-----------------|--------|
| Monitor detection | Per-session COM events + 200ms reconciliation | `g_micRequested` | monitor |
| Streaming gate | discard when `!g_micRequested` | `g_micStreaming` | bridge -> tray |
| Detection latency | Immediate event; <=200ms missed-event repair | `g_micOnTick` | monitor -> bridge |
| Deactivation grace | 400ms after the final session becomes inactive | `g_micRequested` | monitor |
| Demand Mode toggle | Tray context menu, persisted to registry | `g_demandMode` | tray -> monitor |
| Idle ring buffer reset | 5s (50 x 100ms) | -- | bridge |
| Socket idle disconnect | 5s (500 blocks) Always Hot OFF | `g_alwaysHot` | bridge |
| Socket stall disconnect | 9s (90 x 100ms) | -- | bridge |

## Thread Model

```
main thread:         Message pump + SetTimer(stats, 5s)
monitor thread:      Owns the MTA COM apartment; per-session callbacks + 200ms enumerate/GetState reconciliation
bridge thread:       ADB one-time init (CreateProcess NO_WINDOW) + Socket on-demand connection (idle 5s disconnect, connect ~0.4ms) -> g_micRequested gate -> ring buffer push/discard
render thread:       Event-driven ring buffer pop -> int16->float -> DspPipeline -> WASAPI write
DPDFNet worker:      tagged SPSC input queue -> epoch-aware reset -> sherpa-onnx Run() -> validated tagged output FIFO -> fixed 480-sample blocks; failed worker blocks until stop; no DLL/model I/O on render
```

The watchdog cannot cancel a native `Run()` call that never returns. Such a call remains a known shutdown limitation; the current implementation never detaches the worker before releasing its denoiser/DLL resources. A future process-isolated adapter is required for hard cancellation.

## Latency Budget

| Component | Latency |
|-----------|---------|
| Android ADC + HAL | ~10ms |
| AudioRecord read (480fr) | ~10ms |
| ADB + Socket | ~2ms |
| Ring Buffer | ~10ms (1-2 blocks) |
| RNNoise + EQ + Comp + Limiter | ~50us (measured) |
| DPDFNet worker inference | ~1.7ms EMA in the reference smoke test; reset warm-up is bounded and stalled output downgrades to RNNoise |
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

## Optional DPDFNet Runtime

The default build (`build.bat`) embeds RNNoise only. The source-controlled DPDFNet payload is kept under `third_party/dpdfnet/` (large model/DLL files use Git LFS):

```
third_party/dpdfnet/
├─ include/sherpa-onnx/c-api/c-api.h
├─ model/dpdfnet2_48khz_hr.onnx
├─ runtime/sherpa-onnx-c-api.dll
├─ runtime/onnxruntime.dll
├─ runtime/onnxruntime_providers_shared.dll
└─ metadata.json
```

After cloning, run `git lfs pull`. `build.bat --dpdfnet` verifies the vendored files, stages them under `build/cmake/x64-release/_deps/dpdfnet`, and installs the DLLs under the executable directory plus the model under `models/`. CMake compiles the adapter against that pinned C API header, but the executable still resolves every DLL symbol dynamically and has no sherpa-onnx import-library dependency. The runtime manifest includes every installed file, so deleting `build/` only removes generated output. If any optional file or required C API symbol is missing at runtime, the process still starts with RNNoise.
