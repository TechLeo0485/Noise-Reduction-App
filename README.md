# VoiceStudio

Local, low-latency microphone noise suppression and voice processing for Windows 10/11.

VoiceStudio captures your microphone with WASAPI, removes background noise (RNNoise or a
DSP-only mode), applies a gate, EQ, compression, optional pitch and a limiter, and plays the
result into a virtual audio cable (VB-CABLE). Discord, Zoom, Teams and games then use the
cable as their microphone. Everything runs on your machine: no cloud, no account, no telemetry.

> **Build status:** builds cleanly with MSVC 14.44 + Qt 6.8.3. All unit and hardware tests
> pass. The GUI, VB-CABLE output and long-run behaviour have not been verified interactively
> yet; see [docs/implementation-checklist.md](docs/implementation-checklist.md).

## Features (current build)

| Area | Status |
|---|---|
| WASAPI event-driven capture/render, IAudioClient3 low-latency periods | Implemented |
| Device enumeration, hot-plug, default-device following, stable IDs | Implemented |
| Automatic recovery (unplug, Bluetooth drop, driver reset, suspend, stalls) | Implemented |
| Clock-drift compensation between microphone and output device | Implemented (adaptive resampling) |
| RNNoise neural noise suppression (10 ms frames, 48 kHz) | Implemented |
| DSP-only noise control, high-pass, noise gate/expander with VAD hold | Implemented |
| 5-band EQ, compressor, zero-latency limiter, low-latency pitch shift | Implemented |
| Presets (Interview, Natural Voice, Gaming, Streaming, NR Maximum, custom) | Implemented |
| Performance modes (Ultra-Low Latency, Balanced, Maximum NR, Custom) | Implemented |
| CPU / GPU detection (CPUID, DXGI) | Implemented |
| Live metrics: P50/P95/P99 timings, dropouts, drift, latency breakdown, CPU, memory | Implemented |
| Virtual cable test and loopback latency measurement | Implemented |
| Advanced AI (ONNX Runtime, GPU/NPU) | **Not in this build**: UI shows it as unavailable |
| Strict voice isolation (removes other people's voices, VoxLock INT8 Full, CPU) | Implemented: record your voice once; fail-closed (silence, never the raw microphone) |
| Custom virtual audio driver | Not planned for the MVP (uses VB-CABLE) |

## Requirements

- Windows 10 21H2+ or Windows 11, x64
- Visual Studio 2022 (17.8+) with **Desktop development with C++** (MSVC v143, Windows 11 SDK)
- CMake 3.25+ and Ninja (both ship with Visual Studio)
- Qt 6.5 or newer (6.8 LTS recommended), MSVC 2022 64-bit kit, with Qt Quick and Qt Quick Controls 2
- Internet access the **first time you configure** (RNNoise and GoogleTest are downloaded by CMake)
- At runtime: [VB-CABLE](https://vb-audio.com/Cable/) for routing into other apps

## Setting up Visual Studio 2022

1. Install Visual Studio 2022 Community/Professional, or "Build Tools for Visual Studio 2022".
2. In the installer, select **Desktop development with C++** and make sure these are ticked:
   *MSVC v143 x64/x86 build tools*, *Windows 11 SDK (10.0.22621 or newer)*, *C++ CMake tools for Windows*.
3. Install Qt using one of:
   - the Qt Online Installer: choose *Qt 6.8.x > MSVC 2022 64-bit*, or
   - `scripts\setup-dev.ps1`, which installs Qt via `aqtinstall` into `C:\Qt` along with CMake and Ninja (via winget).
4. Set `QTDIR` to the kit, e.g. `setx QTDIR C:\Qt\6.8.3\msvc2022_64`, then open a new terminal.

## Building

From a **x64 Native Tools Command Prompt for VS 2022** (or Developer PowerShell):

```bat
cmake --preset ninja-release
cmake --build --preset ninja-release
ctest --preset ninja-release
```

Or with the Visual Studio generator (any terminal):

```bat
cmake --preset vs2022
cmake --build --preset vs2022-release
ctest --preset vs2022-release
```

`scripts\build.ps1` wraps the Ninja steps and imports the VS environment automatically.

Useful options:

| Option | Default | Meaning |
|---|---|---|
| `VOICESTUDIO_WITH_RNNOISE` | ON | Build RNNoise. If it fails to compile, set OFF: the app then offers DSP-only suppression |
| `RNNOISE_SOURCE_DIR` | (empty) | Use a local RNNoise tree (offline builds) |
| `VOICESTUDIO_BUILD_TESTS` | ON | GoogleTest suites |
| `VOICESTUDIO_BUILD_TOOLS` | ON | `vs_bench`, `vs_latency`, `vs_soak` (+ `vs_isolation` with VoxLock) |
| `VOICESTUDIO_WITH_VOXLOCK` | ON | Strict voice isolation. Configure downloads ONNX Runtime 1.31 (CPU) and the VoxLock INT8 Full models (~85 MB, SHA-256 pinned) once |
| `ONNXRUNTIME_ROOT` / `VOXLOCK_MODEL_DIR` | (empty) | Offline builds: an extracted `onnxruntime-win-x64-1.31.0` package / a folder with `enroll.onnx` and `chunk_int8_full.onnx` |

Outputs land in `build/<preset>/bin/`.

## Running

1. Install VB-CABLE (see the *Audio Devices* screen or [docs/troubleshooting.md](docs/troubleshooting.md)).
2. Start `VoiceStudio.exe`. On first start it picks the default microphone and, if present, **CABLE Input** as the output.
3. In Discord/Zoom/Teams, select **CABLE Output** as the microphone.
4. Press **Test virtual microphone** on the *Audio Devices* screen to confirm routing.

Settings, logs and crash dumps live in `%LOCALAPPDATA%\VoiceStudio\`.

## Testing

| Suite | Command | Needs hardware |
|---|---|---|
| Unit, DSP, chain, settings, performance budget | `ctest --preset vs2022-release` | No |
| Device enumeration, real capture, start/stop cycles | `ctest --preset hardware` | Yes (microphone) |
| Offline benchmark (P50/P95/P99 per mode) | `build\...\bin\vs_bench.exe` | No |
| Quality comparison WAVs from a recording | `vs_bench --wav speech_noisy.wav --out results` | No |
| Loopback latency | `vs_latency --list`, then `vs_latency --out N --in M` | Yes |
| 8-hour soak | `vs_soak --hours 8 --out N` | Yes |

Procedures and how to report results: [docs/latency-benchmarks.md](docs/latency-benchmarks.md).
No benchmark numbers are published in this repository until they have been measured on real hardware.

## Packaging

```powershell
scripts\package.ps1            # Release build, cmake --install (runs windeployqt), Inno Setup installer
```

Produces `dist\VoiceStudio-<version>-setup.exe` if [Inno Setup 6](https://jrsoftware.org/isinfo.php)
is installed, otherwise a portable folder in `build\install\`. VB-CABLE is **not** redistributed;
users install it separately. Uninstall through *Settings > Apps*; user data in
`%LOCALAPPDATA%\VoiceStudio` is removed only if you choose that option in the uninstaller.

## Documentation

- [docs/architecture.md](docs/architecture.md): components, threads, design decisions, risks
- [docs/audio-pipeline.md](docs/audio-pipeline.md): signal flow, buffering policy, real-time rules
- [docs/latency-benchmarks.md](docs/latency-benchmarks.md): latency model, measurement, soak test, quality tests
- [docs/troubleshooting.md](docs/troubleshooting.md): setup and common problems
- [docs/implementation-checklist.md](docs/implementation-checklist.md): phase status
- [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)

## Privacy

- Audio is processed in memory only and never sent anywhere. The one exception is strict voice
  isolation: when you record your voice, a 16 kHz sample is saved locally as
  `%LOCALAPPDATA%\VoiceStudio\voice\voice_sample.wav` so it survives restarts. "Delete my voice" removes it.
- No telemetry, no network access at runtime.
- Logs contain events and device names only. Crash dumps are minimal (stacks and module list, no audio buffers).
