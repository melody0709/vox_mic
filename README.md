# AudioSource Win (Raw WASAPI)

Use Android phone microphone as Windows microphone via ADB + VB-CABLE + Raw WASAPI.

## How It Works

```
Android Mic → AudioSource App → ADB Forward → This Program → VB-CABLE → Windows Apps
```

## Requirements

- Windows 10/11
- Android device with USB debugging enabled
- [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) installed
- [AudioSource Android app](https://github.com/gdzx/audiosource/releases) installed on phone
- ADB in PATH

## Usage

```cmd
# Start audio bridge
audiosource.exe --serial <device-serial>

# List audio devices
audiosource.exe --list-devices

# Show help
audiosource.exe --help
```

After starting, select **CABLE Output** as microphone input in Windows apps (Zoom, Teams, etc.).

## Build

Requires Visual Studio 2022 with C++ Desktop Development workload.

```cmd
build.bat
```

## Performance

| Metric | Value |
|--------|-------|
| CPU idle | ~0% |
| CPU streaming | ~0.5% |
| Memory | ~10MB |
| Latency | ~250-400ms |
| Binary size | ~270KB |

## Architecture

See [AGENTS.md](AGENTS.md) for technical details.
