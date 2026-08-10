# Changelog

[简体中文](doc/zh-CN/CHANGELOG.md) | **English**

## v0.6.6 (2026-08-10)

### Settings first-open repaint

| Fix | Description |
|-----|-------------|
| **Initial tab visibility** | Explicitly initializes the General/DSP child visibility and refreshes only the active tab when the Settings window is shown. |
| **Owner-draw ghosting** | Avoids forcing a repaint on the hidden DPDFNet status control and repaints only the parent background during first display, preventing status text from appearing over General controls. |
| **Release identity** | Bumped desktop/Android to `0.6.6` / Android `versionCode=12`. |

---

## v0.6.5 (2026-08-10)

### DPDFNet worker hardening

| Fix | Description |
|-----|-------------|
| **Failure convergence** | A failed worker clears `ready`, stops its periodic empty-loop polling, and exits quickly when stopped; `setEpoch()` no longer operates on a failed session. |
| **Epoch hand-off** | A newly popped epoch block is reset and processed using its own tag; blocks superseded by a later requested epoch and stale inference results are discarded. |
| **Output validation** | Oversized, malformed, or non-finite DPDFNet output triggers hard fallback to RNNoise before entering the output FIFO. |
| **Effective status** | Added an explicit `off` effective backend state so Stats and Settings do not report DPDFNet when noise reduction is disabled. |
| **Regression coverage** | Added failure lifecycle smoke coverage, including failed state convergence, epoch short-circuiting, and normal destructor latency. |
| **Release identity** | Bumped desktop/Android to `0.6.5` / Android `versionCode=11`. |

The native DPDFNet API can still theoretically block forever inside `Run()`. This remains documented as a limitation; the implementation does not detach or forcibly terminate a thread holding DLL/session resources.

---

## v0.6.4 (2026-08-09)

### DPDFNet retry/status correctness

| Fix | Description |
|-----|-------------|
| **Explicit retry** | Clicking OK while DPDFNet is in the ready-but-stalled degraded state now sends a reset request, so the worker can be retried without restarting the application. Ordinary backend/NR changes still rely on the single render-thread change detection and do not cause a duplicate reset. |
| **Hard failure status** | A worker/session failure is reported as unavailable rather than as a retryable degraded stall; the render path remains on RNNoise until a fresh DPDFNet prepare. |
| **Release identity** | Bumped the final desktop/Android release to `0.6.4` / Android `versionCode=10` after the post-0.6.3 correctness fixes. |

---

## v0.6.3 (2026-08-09)

### DPDFNet stream resilience

| Fix | Description |
|-----|-------------|
| **Epoch hand-off** | The worker no longer clears its input queue during a reset, so a render-thread block already tagged with the new epoch is retained. Results that become stale while inference is running are discarded before they can occupy the output FIFO. |
| **Bounded silence** | DPDFNet warm-up permits at most four 10 ms empty-output blocks after a reset. A steady worker with three consecutive empty blocks is downgraded to RNNoise for the current stream instead of producing unlimited silence. |
| **Runtime status** | Added `g_dpdfnetDegraded`, a Settings status message, and periodic diagnostics for effective backend, availability, underflows, FIFO drops, and worker timing. A stream reset retries a ready DPDFNet worker. |
| **ABI maintenance** | The adapter now compiles against the pinned sherpa-onnx C API header while retaining dynamic DLL loading; no sherpa-onnx import-library dependency is added. |
| **Regression coverage** | Added a pipeline switch smoke test for RNNoise ↔ DPDFNet, repeated epoch resets, and a deliberately stalled worker; strengthened the processor smoke reset coverage. |

---

## v0.6.2 (2026-08-09)

### Backend status rendering

| Fix | Description |
|-----|-------------|
| **Overlapping status text** | The dynamic Denoise Backend status is now painted opaquely and explicitly erased before redraw. It also reserves two lines for the unavailable-runtime message. |

---

## v0.6.1 (2026-08-09)

### Selectable RNNoise / DPDFNet noise-reduction backend

| Change | Description |
|--------|-------------|
| **Backend selection** | Added persistent `DenoiseBackend=rnnoise|dpdfnet` configuration and a DSP settings selector. RNNoise remains the default. |
| **Streaming DPDFNet** | Added a dynamically loaded sherpa-onnx C API adapter with a dedicated worker, epoch reset, and fixed 480-sample FIFO output. |
| **Fallback** | Missing runtime DLLs, model files, API symbols, contract mismatches, and worker failures fall back to RNNoise. |
| **Build modes** | Added `build.bat --dpdfnet`; RNNoise-only installs stay single-executable, while DPDFNet installs include the verified runtime/model payload and notices. |
| **Reproducible dependencies** | Vendored the pinned DPDFNet model, sherpa-onnx runtime DLLs, and C API header under `third_party/dpdfnet` with Git LFS; a clean `build/` can be regenerated without network access. |
| **Clean safety** | `build.bat --clean` now fails clearly when a generated runtime file is locked instead of reporting a false successful cleanup. |
| **Validation** | Added DPDFNet streaming and fallback smoke tests, runtime manifest coverage, and pinned SHA-256 dependency preparation. |

The DPDFNet backend is an optional alternative. Its subjective quality and end-to-end latency still require A/B testing on the target Android devices and noise environments; this change does not make DPDFNet the default.

---

## v0.5.3 (2026-05-03)

### Auto-recovery from ADB forward loss after scrcpy toggle

#### Bug Fixes

| Bug | Description | Fix |
|-----|-------------|-----|
| **No recovery after scrcpy close** | scrcpy cleans up ADB forward (`tcp:27183`) on exit, causing bridge connect 10061, requiring VoxMic restart | First connect failure + previously connected -> `refreshForward()` quick rebuild forward (~100ms) -> retry succeeds |
| **3 rapid disconnects with no recovery** | Socket disconnects within <3 seconds repeatedly, no auto-fix | Quick disconnect count >= 3 -> `setupAudioSource()` full rebuild (restart app + forward) |

#### Changed Files

| File | Change |
|------|--------|
| `src/adb_control.h` | Added `refreshForward()` declaration |
| `src/adb_control.cpp` | Implemented `refreshForward()` = `removeForward` + `createForward` (~100ms) |
| `src/main.cpp` | Bridge connect failure detection: `connectFailCount` + `wasPreviouslyConnected`; first failure calls `refreshForward`; 3 failures call `setupAudioSource`; quick disconnect detection |
| `RecordService.java` | Extracted `createRecorder()` factory method + `releaseAudioEffects()` helper |
| `RecordThread.java` | Diagnostic logging (accept/recorder state/blocks sent/connection closed) |

#### Recovery Latency Comparison

| Scenario | v0.5.2 | v0.5.3 |
|----------|--------|--------|
| First voice input after scrcpy | 3-5 seconds (3 failures + 1.5s rebuild) | ~300ms (1 failure + 100ms refresh + retry) |
| Normal idle recovery | No change | No change |

---

## v0.5.2 (2026-05-03)

### Phase 12: Adjustable RNNoise Denoising Strength + Hint Style Optimization

| Feature | Description |
|---------|-------------|
| **NR Strength slider** | 0.30-0.95, default 0.60, adjusts RNNoise alpha gain smoothing parameter (denoising aggressiveness) |
| **Slider Hints** | Gray hint text below Presence / Bass Cut / NR Strength, 16pt font |
| **Zero Build Warnings** | `/wd4305 /wd4244` suppress RNNoise upstream warnings, project code C4100/C4189 fixed |

#### Changed Files

| File | Change |
|------|--------|
| `src/dsp/rnnoise/denoise.c` | `DenoiseState` added `strength` field; `alpha` reads from field; `rnnoise_set_strength()` implemented |
| `src/dsp/rnnoise/rnnoise.h` | Added `rnnoise_set_strength(DenoiseState*, float)` |
| `src/config.h/cpp` | Added `nrStrength` field (0.3-0.95, default 0.6), INI key `NrStrength`, config fields -> 21 |
| `src/dsp/pipeline.h` | Added `g_nrStrength` atomic variable + `updateSettings()` calls `rnnoise_set_strength` |
| `src/main.cpp` | `syncDspAtomsFromConfig()` added `g_nrStrength` sync |
| `src/settings_dialog.cpp` | DSP tab added NR Strength slider + 3 hint labels (small gray text, WM_CTLCOLORSTATIC) |
| `build.bat` | Link `gdi32.lib`, compile flags `/wd4305 /wd4244` |
| `AGENTS.md` | "Registry persistence" -> "config.ini"; config fields 20->21; added release build instructions |

#### Cleanup & Archive

| File | Action |
|------|--------|
| `plan_nr_strength.md` | Added -> `plan/ongoing/` |
| `plan_hint_style.md` | Added -> `plan/ongoing/` |
| `plan_optimize.md` | `plan/ongoing/` -> `plan/completed/` |

---

## v0.5.1 (2026-05-03)

### Phase 10: On-demand Activation Detection Fix -- No Idle After Sound Recorder Stops

#### Bug Fixes

| Bug | Description | Fix |
|-----|-------------|-----|
| **Bug 1** | Monitor listens to default capture device (built-in mic) instead of CABLE Output | Changed to `EnumAudioEndpoints(eCapture)` matching by name "CABLE Output" |
| **Bug 2** | Sound Recorder (UWP) doesn't release `IAudioClient` after stop/close, `AudioSessionState` stays Active | Added `IAudioMeterInformation` silence fallback, force idle after 3s consecutive peak=0 |

#### Three-layer Detection Architecture

| Layer | Mechanism | Trigger | Latency | CPU Overhead |
|-------|-----------|---------|---------|-------------|
| 1 | `OnStateChanged(Active/Inactive)` | COM event callback | Instant | Zero |
| 2 | `renderStallScore` | render event timeout (3x2000ms) | ~6s | Zero (existing) |
| 3 | `IAudioMeterInformation::GetPeakValue()` | monitor thread (active state only) | ~3s | 1 COM/sec |

**Idle state**: When `g_micRequested == false`, monitor thread only `Sleep(1000)`, zero COM calls, zero CPU.

#### Modified Files

| File | Change |
|------|--------|
| `src/mic_usage_monitor.h` | Added `IAudioMeterInformation* m_pMeter` + `getCapturePeak()` |
| `src/mic_usage_monitor.cpp` | `init()` enumerates eCapture to find CABLE Output + activate IAudioMeterInformation; `shutdown()` releases |
| `src/wasapi_output.h` | Added `std::atomic<int> renderStallScore` |
| `src/wasapi_output.cpp` | `renderThread()` tracks `WaitForSingleObject` timeout count |
| `src/main.cpp` | `micMonitorThread()` active meter polling + silence force idle; bridge thread `effectiveActive` logic integration |

---

## v0.5.0 (2026-05-03)

### Phase 9: Socket On-demand Connection -- Android Idle Power Saving

| Feature | Description |
|---------|-------------|
| **Always Hot toggle** | Tray context menu checkbox control, persisted to registry, **default OFF** |
| **Idle disconnect** | When Always Hot OFF, idle 5s then actively disconnect socket -> Android `AudioRecord` stops -> zero power draw |
| **On-demand reconnect** | Outer loop polls `g_micRequested` (Sleep 200ms), on demand `connect()` **~0.3-0.8ms (QPC measured)** |
| **Cold start latency** | ~200ms (mainly Sleep 200ms polling, socket connect negligible) |
| **Android side** | No changes needed: `accept -> startRecording -> stop -> accept` loop natively matches |

#### Measured Data

| Metric | Value |
|--------|-------|
| Socket reconnect time | 0.33-0.81ms (QPC, far below estimated ~200ms) |
| Cold start latency | ~200ms (Sleep 200ms polling dominant) |
| Idle CPU | 0-0.1% (unchanged) |

#### Modified Files

| File | Change |
|------|--------|
| `src/main.cpp` | Added `g_alwaysHot` atomic variable; outer loop idle wait; inner loop idle 500 blocks disconnect; QPC socket connect timing |
| `src/tray_icon.h/cpp` | Added `ID_MENU_ALWAYS_HOT` menu item + `setAlwaysHot()` |
| `src/settings_dialog.cpp` | Always Hot menu toggle logic + registry save |
| `src/config.h/cpp` | Added `alwaysHot` field (20 fields), default false, registry persistence |

---

## v0.4.2 (2026-05-02)

### Phase 8: CPU Optimization + Event-driven Monitor

| Feature | Description |
|---------|-------------|
| **Event-driven Monitor** | `IAudioSessionNotification` + `IAudioSessionEvents` callbacks replace 100ms polling, zero COM overhead |
| **Demand Mode toggle** | Tray context menu "Demand Mode" checkbox, controls on-demand activation switch |
| **Demand Mode persistence** | Settings saved to registry, state preserved across restarts |
| **Idle CPU** | Demand ON: **0-0.1%** (optimized from v0.4.1's 0.2-0.4%) |
| **DSP overhead measured** | Only ~0.1% (far below design doc estimate of ~1.65%) |

#### Optimization History

| Stage | Approach | CPU |
|-------|----------|-----|
| v0.4.0 monitor polling | Every 100ms `Activate(IAudioSessionManager2)` + enumerate + `Release` | 0.2-0.4% |
| Plan A COM caching | `init()` creates and caches `IAudioSessionManager2`, every 100ms only creates enumerator | 0.15-0.25% |
| Plan B Event-driven | `IAudioSessionNotification` + `IAudioSessionEvents` callbacks, zero polling | **0-0.1%** |

#### Modified Files

| File | Change |
|------|--------|
| `src/mic_usage_monitor.h` | Rewritten: implements `IAudioSessionNotification` + `IAudioSessionEvents` COM interfaces |
| `src/mic_usage_monitor.cpp` | Rewritten: event-driven `OnStateChanged` updates `g_micRequested`, `OnSessionCreated` registers new sessions |
| `src/main.cpp` | `g_micRequested` non-static extern; monitor thread only `Sleep(1000)` to keep COM apartment alive |
| `src/main.cpp` | Added `g_demandMode` atomic variable, initialized from Config |
| `src/tray_icon.h/cpp` | Added `ID_MENU_DEMAND_MODE` menu item + `setDemandMode()` |
| `src/settings_dialog.cpp` | Demand Mode menu toggle logic + registry save |
| `src/config.h/cpp` | Added `demandMode` field (19 fields), registry persistence |

---

## v0.4.1 (2026-05-02)

### Phase 5: Hide to Tray GUI + ADB No Flicker

| Feature | Description |
|---------|-------------|
| **Hide to tray** | Settings window promoted to main window, program auto-hides to tray on launch, right-click Exit to quit |
| **Tray interaction** | Left-click tray icon opens settings window, close window hides (doesn't exit) |
| **ADB no flicker** | `CreateProcess` + `CREATE_NO_WINDOW` replaces all `_popen("adb ...")`, zero console windows |
| **/SUBSYSTEM:WINDOWS** | No console subsystem, no window flicker at all |
| **Debug Console on-demand** | Only `AllocConsole()` when checked, hidden by default, checkbox persisted to registry |
| **Startup info log** | Prints Gain/DSP/Android HW settings summary on startup |
| **Version number** | Gray `v0.4.1` displayed at bottom of tray context menu |

#### Modified Files

| File | Change |
|------|--------|
| `build.bat` | Linker + `/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup` |
| `src/adb_control.h/cpp` | `runCommand` -> public `runCommandNoWindow()` (CreateProcess + CREATE_NO_WINDOW) |
| `src/settings_dialog.h/cpp` | Modal `showSettingsDialog` -> modeless persistent window `createSettingsWindow` + `loadSettingsWindow` |
| `src/main.cpp` | Removed `WindowProc`/`HWND_MESSAGE`, replaced with `createSettingsWindow`, `setConsoleVisible()` uses FreeConsole/AllocConsole |
| `src/tray_icon.cpp` | "Settings..." -> "Settings", added gray version number menu item |
| `src/config.h/cpp` | Added `debugConsole` field (18 fields) |

#### Build Changes

```
# v0.4.0
/link ws2_32.lib ole32.lib mmdevapi.lib shell32.lib advapi32.lib comctl32.lib

# v0.4.1
/link ... /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
```

---

## v0.4.0 (2026-05-01)

### Phase 3: Microphone On-demand Activation + Latency Compression

| Feature | Description |
|---------|-------------|
| **MicUsageMonitor** | IAudioSessionManager2 100ms polling, detects if CABLE Output is being used by Windows apps |
| **Always Hot** | Bridge never actively disconnects socket, idle time recv + discard, don't push ring buffer |
| **On-demand streaming** | `g_micRequested` controls bridge push/discard, idle CPU ~0.25% |
| **ADB one-time launch** | `setupAudioSource` only called first time, socket reconnect ~200ms (no ADB) |
| **Latency compression 85->40ms** | Android AudioRecord 2x->1x minBufSize, watermarks 5->3->3->2, initial fill 3->0 |

#### New Files

| File | Description |
|------|-------------|
| `src/mic_usage_monitor.h/cpp` | Capture endpoint session polling detection |

#### Modified Files

| File | Change |
|------|--------|
| `src/main.cpp` | Monitor thread + bridge Always Hot discard logic + detect latency timing |
| `src/socket_client.h/cpp` | Added `waitForData(timeoutMs)` |
| `src/wasapi_output.h/cpp` | `procUsEma`/`estLatencyMs` QPC timing |
| `src/wasapi_output.cpp:73` | WASAPI buffer 200000->100000 hns (actual lower limit 22ms) |
| `RecordService.java:101` | AudioRecord buffer `2x->1x minBufSize` |
| `RecordThread.java` | Added `elapsedRealtimeNanos` read timing log |
| `build.bat` | Added `mic_usage_monitor.cpp` compilation |

#### Parameter Changes

| Parameter | v0.3.0 | v0.4.0 |
|-----------|--------|--------|
| Android AudioRecord buffer | 2x minBufSize (~20ms) | **1x minBufSize (~10ms)** |
| Ring buffer watermarks | 5->3 | **3->2** |
| Initial fill | 3 blocks (30ms) | **0 (direct start)** |
| WASAPI buffer | 20ms (requested) | 22ms (VB-CABLE engine lower limit) |
| Total latency | ~90ms | **~40ms** (measured) |
| Detection latency | N/A | **~12ms** (measured) |
| Idle CPU | ~2% | **~0.25%** |

#### New Global Atomic Variables

| Variable | Purpose | Thread |
|----------|---------|--------|
| `g_micRequested` | Whether Windows app is capturing | monitor -> bridge |
| `g_micStreaming` | Whether streaming is active | bridge -> tray icon |
| `g_micOnTick` | Detection latency measurement timestamp | monitor -> bridge |
| `procUsEma` / `estLatencyMs` | Per-block processing time EMA / estimated latency | render threads |

#### Measured Data

- Voice input method CapsLock hold 300ms activation: `[Monitor] mic=ON/OFF` precisely follows
- Detection latency: 0-16ms, average ~12ms
- End-to-end latency: ~36-46ms (Phone: read ~20ms + HAL ~10ms, PC: queue 1-2 + WASAPI 11ms + DSP 50us)
- 60s continuous test: `drop=0 underrun=3049 queue=0~1 proc=47us lat=11~21ms`
- Idle CPU: monitor 0.15% + WASAPI silence 0.1% = 0.25%

### Thread Model Changes

```
v0.3.0                          v0.4.0
main                             main
bridge                           monitor (NEW: 100ms IAudioSessionManager2 polling)
render                           bridge (g_micRequested gate push/discard)
                                 render
```

---

## v0.3.0 (2026-05-01)

### Official RNNoise Neural Network Denoising Integration

Based on `werman/noise-suppression-for-voice` `external/rnnoise` fork (includes pre-generated model files).

| Feature | Description |
|---------|-------------|
| Algorithm | 3-layer GRU network (96+96+96), 22 Bark band independent denoising + comb filtering |
| Model | v0.2 weights (128+384+384 dimensions, Amazon optimized), 85KB quantized, compiled into binary |
| Latency | 10ms/frame (480 frames @ 48kHz) |
| License | BSD-3-Clause |

### Pipeline

```
RNNoise (10ms) -> HPF 80Hz -> EQ 6-band -> RMS Comp -> Limiter -> WASAPI
```

### Parameter Changes

| Parameter | v0.2.0 | v0.3.0 |
|-----------|--------|--------|
| FRAMES_PER_BLOCK | 512 (10.7ms) | **480 (10.0ms)** |
| Android BLOCK_SIZE | 1024 bytes | **960 bytes** |
| Total latency | ~83ms | **~90ms** |

### Stability Verification

60-second stress test: `recv=5819 drop=3 underrun=0 queue=1~2`

| Metric | v0.2.0 | v0.3.0 |
|--------|--------|--------|
| underrun | 0 | **0** |
| drop | 6-9 (0.15%) | **3 (0.05%)** |
| queue | 3-5 | **1-2** (extremely compact) |
| Binary | ~270 KB | **~1.5 MB** |

480-frame blocks make the buffer more compact (queue only 1-2), lowest drop rate ever.

### Settings

| Control | Default | Description |
|---------|---------|-------------|
| Noise Reduction | on | RNNoise toggle |

---

## v0.2.0 (2026-05-01)

### Custom DSP Audio Post-processing Pipeline

| Stage | Algorithm | Latency | Purpose |
|-------|-----------|---------|---------|
| HPF | BiQuad IIR 80Hz | 0ms | Cut wind noise/DC |
| EQ | 6-band BiQuad (Presence + Bass Cut adjustable) | 0ms | Voice optimization |
| Compressor | RMS 3:1, 5ms/50ms | 0ms | Loudness uniformity |
| Limiter | Peak follower -1dBFS | 0ms | Prevent clipping |

### New Settings

EQ Enable / Presence (0-6dB) / Bass Cut (-6-0dB) / Compressor Enable

### Side-effect Fix

Settings dialog: pure audio effect changes also update in-memory config (original bug: only serial/host/port changes took effect)

---

## v0.1.1 (2026-05-01)

### Latency Optimization -- 400ms -> 83ms

| Parameter | v0.1.0 | v0.1.1 | Impact |
|-----------|--------|--------|--------|
| FRAMES_PER_BLOCK | 1024 (21.3ms) | **512 (10.7ms)** | Block granularity halved |
| WASAPI buffer | 200ms | **20ms** | Buffer latency -90% |
| Ring buffer watermarks | 16->8 | **5->3** | Queue latency -85% |
| Initial fill | 3 blocks (~70ms) | **3 blocks (~32ms)** | Startup wait -55% |
| Android block | 2048 bytes | **1024 bytes** | Align to 512 frames |
| Total latency | ~400ms | **~83ms** | **5x improvement** |

2-minute stress test: `recv=11090 drop=12 underrun=0`

---

## v0.1.0 (2026-04-30)

### Initial Feature Set

- WASAPI event-driven rendering (SetEventHandle + WaitForSingleObject)
- System tray + Settings dialog (device/network/app/audio effects/Gain)
- Registry config persistence (12 fields)
- 48000 Hz Android <-> Windows alignment (zero resampling)
- Gain 0.25x-4.0x slider
- Independent audio effect toggles (NS/AEC/AGC checkbox + ADB --ez params)
- VoxMic Source Android App (independent build & install)
- Xiaomi device AudioSource compatibility experiment (locked to DEFAULT)
