# Architecture

## Data Flow

```
Android Mic (44100Hz/mono/int16)
    ↓
AudioSource App (localabstract:audiosource)
    ↓ USB
ADB Forward (tcp:27183)
    ↓ TCP
audiosource.exe
    ├── Socket Receiver Thread (Winsock2 recvExact)
    ├── SPSC Ring Buffer (128 blocks × 2KB, lock-free)
    └── WASAPI Render Thread (resample + mono→stereo)
            ↓
    VB-CABLE Input (48000Hz/stereo/float32)
            ↓
    VB-CABLE Output
            ↓
    Windows Apps (Zoom, Teams, etc.)
```

## Source Files

| File | Role |
|------|------|
| `main.cpp` | Entry, tray icon, bridge thread, latency control |
| `wasapi_output.*` | WASAPI init, render loop, resample |
| `device_enum.*` | Find VB-CABLE by name |
| `ring_buffer.h` | Lock-free SPSC queue |
| `socket_client.*` | Winsock2 TCP client |
| `adb_control.*` | ADB commands via _popen |
| `tray_icon.*` | System tray with context menu |

## Audio Conversion

```
44100Hz mono int16
    → resample (linear interp) → 48000Hz
    → duplicate channel → stereo
    → /32768.0f → float32
```

## Latency Budget

| Component | Latency |
|-----------|---------|
| Android capture | ~23ms |
| ADB + socket | ~10ms |
| Ring buffer | ~186ms (8 blocks) |
| WASAPI buffer | ~200ms |
| VB-CABLE | ~10ms |
| **Total** | **~400ms** |

## Optimization Ideas

- [ ] WASAPI event-driven (reduce CPU, failed attempt)
- [ ] Better resampler (libsamplerate / miniaudio)
- [ ] Auto start/stop on mic usage
- [ ] WiFi ADB support
- [ ] Windows service mode
