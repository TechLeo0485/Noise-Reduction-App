# Troubleshooting

## Setup: VB-CABLE (virtual microphone)

VoiceStudio does not install a driver. It sends processed audio to a virtual cable:

| Endpoint | Windows type | Who uses it |
|---|---|---|
| **CABLE Input** | Playback (speaker) device | VoiceStudio *Processed output* |
| **CABLE Output** | Recording (microphone) device | Discord / Zoom / Teams / games |

Install: download from https://vb-audio.com/Cable/, extract, run `VBCABLE_Setup_x64.exe` as
administrator, choose *Install Driver*, then reboot. VB-CABLE is donationware from VB-Audio;
it is not bundled with VoiceStudio.

Recommended: in *Sound settings > More sound settings*, set both CABLE endpoints and your
microphone to **48000 Hz** (*Properties > Advanced*). This avoids resampling.

Do **not** make CABLE Input the Windows default playback device. System sounds would go into
your microphone.

## Common problems

| Symptom | Cause | Fix |
|---|---|---|
| "No virtual audio cable found" | VB-CABLE not installed or not rebooted | Install, reboot, press *Refresh devices* |
| Others hear nothing | App uses the wrong mic, or VoiceStudio output is not CABLE Input | Select CABLE Output in the app; check the *Processed output* level meter; run *Test virtual microphone* |
| Echo / feedback loop | Microphone input is a virtual cable, or monitoring through speakers | Select your physical mic (VoiceStudio warns about this); monitor only on headphones |
| "Microphone access denied" | Windows privacy setting | *Settings > Privacy & security > Microphone*: allow desktop apps |
| "Device is in exclusive use" | Another app holds the mic in exclusive mode | Close it, or untick *Allow applications to take exclusive control* in the device's Advanced properties |
| Crackles / dropouts (Performance shows underruns) | Buffers too small for the system/driver | Use *Balanced* mode or *Safe* buffers; raise the jitter target; check DPC latency (LatencyMon) |
| Delay feels long | Default buffers, 44.1 kHz devices, Bluetooth | *Ultra-Low Latency* mode; set devices to 48 kHz; prefer wired mics |
| Voice sounds thin or choppy | Gate too aggressive or suppression strength too high | Lower the gate threshold/range, enable *Keep gate open on detected voice*, reduce strength to 80-90 % |
| Bluetooth headset mic sounds bad | Hands-free profile (narrowband) | A Bluetooth limitation; use a wired or USB microphone for calls |
| "Recovering" repeats | Device keeps failing (driver/USB power) | Check the log; try another USB port; update the driver |
| Advanced AI / GPU / NPU unavailable | Not included in this build | Expected. RNNoise runs on the CPU with very low load |
| Other people in the room still audible | Noise suppression is not speaker separation | Expected limitation; use a close-talking or directional microphone and the gate |

## Diagnostics

- **Performance** screen: live P50/P95/P99 processing time, dropouts, drift, latency breakdown.
- **Advanced > Copy diagnostics**: configuration, hardware and counters (no audio) for bug reports.
- Logs: `%LOCALAPPDATA%\VoiceStudio\logs\voicestudio.log` (one rotated copy kept).
- Crash dumps: `%LOCALAPPDATA%\VoiceStudio\crashdumps\` (minidumps: stacks and modules only).
- Settings: `%LOCALAPPDATA%\VoiceStudio\settings.json`. If corrupt, it is renamed to
  `settings.json.bad` and defaults are used.

## Build problems

| Problem | Fix |
|---|---|
| `Could not find a package configuration file provided by "Qt6"` | Set `QTDIR` to the MSVC 2022 64-bit kit, or pass `-DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64` |
| RNNoise download fails (offline) | Download `rnnoise-0.2.tar.gz` once, extract, configure with `-DRNNOISE_SOURCE_DIR=<path>` |
| RNNoise fails to compile with MSVC | Configure with `-DVOICESTUDIO_WITH_RNNOISE=OFF` (DSP-only fallback) and report the error |
| `cl` not found with the Ninja presets | Use the *x64 Native Tools Command Prompt for VS 2022*, `scripts\build.ps1`, or the `vs2022` preset |
| "Qt6Gui.dll / Qt6Qml.dll was not found" | The build runs `windeployqt` after linking VoiceStudio; rebuild. If it reported "windeployqt not found", run `%QTDIR%\bin\windeployqt --release --qmldir ui build\ninja-release\bin\VoiceStudio.exe` |
| App starts but QML fails to load | Run from the build or install folder (`windeployqt` deploys the QML modules at install time) |
