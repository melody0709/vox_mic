# AGENTS.md

## Project

Use Android phone microphone as Windows microphone via ADB + VB-CABLE + Raw WASAPI.

## Build

```cmd
build.bat
```

Requires Visual Studio 2022 with C++ Desktop Development workload.

## Run

```cmd
audiosource.exe --serial <device-serial>
```

Select **CABLE Output** as microphone in Windows apps.

## Key Points

- Input: 44100Hz mono int16 from Android via ADB TCP forward
- Output: 48000Hz stereo float32 to VB-CABLE via WASAPI shared mode
- Resample: linear interpolation in render thread
- Threading: socket receiver → lock-free ring buffer → WASAPI render
- Latency control: drop oldest blocks when queue > 16
