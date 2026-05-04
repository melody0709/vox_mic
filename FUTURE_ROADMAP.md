# Future Roadmap

[简体中文](doc/zh-CN/FUTURE_ROADMAP.md) | **English**

## Completed

- WASAPI event-driven rendering (CPU ~0.1%)
- System tray + Settings dialog (17 fields)
- Configuration persistence (registry)
- 48000 Hz Android <-> Windows alignment (zero resampling)
- Gain control + independent audio effect toggles
- VoxMic Source Android App
- Xiaomi AudioSource experiment
- Latency optimization 400ms -> 83ms (v0.1.1)
- DSP pipeline: EQ + Compressor + Limiter (v0.2.0)
- **RNNoise Neural Network Denoising** (v0.3.0)
- **Phase 3: Microphone On-demand Activation** (v0.4.0)
  - MicUsageMonitor: IAudioSessionManager2 100ms polling detects CABLE Output capture state
  - Always Hot: bridge never actively disconnects socket, idle time recv + discard
  - ADB one-time init, socket reconnect only connect() ~200ms
  - Detection latency measured ~12ms, end-to-end latency ~40ms
  - Idle CPU ~0.25% (DSP all bypass + monitor 0.15% + WASAPI 0.1%)
- **Phase 5: Hide to Tray GUI** (v0.4.1)
  - Settings window promoted to main window, modeless persistent
  - Left-click tray icon opens window, close hides to tray
  - ADB no flicker: CreateProcess + CREATE_NO_WINDOW
  - /SUBSYSTEM:WINDOWS zero-console launch
  - Debug Console on-demand AllocConsole
  - Version v0.4.1 displayed in tray menu
- **Phase 8: CPU Optimization** (v0.4.2)
  - Event-driven Monitor: IAudioSessionNotification + IAudioSessionEvents COM callbacks
  - Zero COM polling: no GetSessionEnumerator/Activate/Release when idle
  - Demand Mode toggle: tray context menu, persisted to registry
  - DSP overhead measured only ~0.1% (far below estimated ~1.65%)
  - Idle CPU 0-0.1% (optimized from v0.4.1's 0.2-0.4%)
- **Phase 9: Socket On-demand Connection** (v0.5.0)
  - Always Hot toggle: tray context menu, persisted to registry, default OFF
  - Idle 5s disconnect socket -> Android AudioRecord stops -> zero power draw
  - On-demand reconnect: socket connect ~0.3-0.8ms (QPC measured)
  - Cold start latency ~200ms (Sleep 200ms polling dominant)
  - Android side no changes needed
- **Auto-recovery from ADB forward loss after scrcpy toggle** (v0.5.3)
  - scrcpy cleans up ADB forward on exit, causing bridge connect 10061
  - First connect failure + previously connected -> `refreshForward()` quick rebuild forward (~100ms) -> retry succeeds
  - 3 consecutive failures -> `setupAudioSource()` full rebuild (restart app + forward)
  - Recovery latency ~300ms (improved from v0.5.2's 3-5 seconds)

## Upcoming Phases

| Phase | Content | Effort | Priority |
|-------|---------|--------|----------|
| Phase 4 | Power management (WM_POWERBROADCAST) | 30min | Low |
| Phase 6C | DeepFilterNet3 upgrade (optional) | 4h | Low |
| -- | Custom tray icons (.ico three states) | 1h | Low |
| -- | VU level meter (RMS + tray tooltip) | 1h | Low |

---

## Change Timeline

| Order | Item | Effort | Version |
|-------|------|--------|---------|
| ~~1~~ | ~~WASAPI event-driven~~ | Done | v0.1.0 |
| ~~2~~ | ~~System tray + Settings~~ | Done | v0.1.0 |
| ~~3~~ | ~~48000Hz alignment~~ | Done | v0.1.0 |
| ~~4~~ | ~~Gain + audio effect toggles~~ | Done | v0.1.0 |
| ~~5~~ | ~~Latency optimization 400->83ms~~ | Done | v0.1.1 |
| ~~6~~ | ~~DSP pipeline (EQ+Comp+Lim)~~ | Done | v0.2.0 |
| ~~7~~ | ~~RNNoise denoising~~ | Done | v0.3.0 |
| ~~8~~ | ~~Microphone on-demand activation~~ | Done | v0.4.0 |
| ~~9~~ | ~~WiFi ADB~~ | Done | Already available |
| ~~10~~ | ~~Hide to tray GUI~~ | Done | v0.4.1 |
| ~~11~~ | ~~CPU optimization + event-driven Monitor~~ | Done | v0.4.2 |
| ~~12~~ | ~~Socket on-demand connection~~ | Done | v0.5.0 |
| ~~13~~ | ~~scrcpy forward auto-recovery~~ | Done | v0.5.3 |
| 14 | Custom icons | 1h | -- |
