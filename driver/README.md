# VoiceStudio Virtual Microphone (driver)

A free virtual microphone for VoiceStudio, so no VB-CABLE or other third-party audio driver is needed.
It is a Windows kernel audio driver based on Microsoft's
[SysVAD sample](https://github.com/microsoft/Windows-driver-samples/tree/main/audio/sysvad)
(MIT license, `LICENSE.sysvad.txt`; upstream commit `bb68ed5e`).

## What it does

The driver creates one device, **VoiceStudio Virtual Audio**, with two endpoints:

| Endpoint | Kind | Used by |
|---|---|---|
| **VoiceStudio Mic Feed** | playback (render) | VoiceStudio sends your processed voice here (selected automatically) |
| **VoiceStudio Virtual Microphone** | recording (capture) | Discord, Zoom, Teams, OBS, games and Windows use it as the microphone |

Whatever is played into the feed comes out of the microphone (`sysvad/VsLoopback.cpp`): a 2-second ring
buffer of 48 kHz mono 16-bit samples, read about 20 ms behind the writer. Both sides run on the same clock
(the WaveRT position timers use `KeQueryPerformanceCounter`), so there is no drift. While nothing is played,
the microphone delivers silence; after a gap it waits for 20 ms of new audio before playing again, so it
never replays old audio. If the reader falls more than 50 ms behind (e.g. the recording app paused), it skips
back to 20 ms, so the virtual microphone never adds more than that to VoiceStudio's 300 ms delay budget.

Formats are fixed to 48 kHz 16-bit (feed: stereo, mixed down to mono; microphone: mono). Windows' audio
engine converts for apps that use other formats.

### Changes from SysVAD

- `TabletAudioSample/minipairs.h`: only two endpoints (the HDMI template for the feed - it has no offload pins -
  and the MicIn template for the microphone) instead of eight.
- `TabletAudioSample/hdmiwavtable.h`, `micinwavtable.h`: 48 kHz 16-bit PCM only (no bitstream formats, no
  16/24 kHz speech modes).
- `TabletAudioSample/hdmitoptable.h`, `micintoptable.h`: new endpoint name GUIDs (names in the INF).
- `EndpointsCommon/minwavertstream.*`: capture reads the loopback buffer instead of a sine tone; render writes
  to it instead of a file.
- `adapter.cpp`: allocates/frees the buffer. `VsLoopback.*`: new.
- `TabletAudioSample/VsVirtualMic.inx`: new INF (hardware ID `Root\VoiceStudioVirtualMic`, service `vsvirtualmic`).
- The Bluetooth/USB sideband and keyword-detector code of the sample is still compiled but inactive
  (no endpoints use it).

## Build

Needs Visual Studio 2022 Build Tools with **Windows Driver Kit (build tools)** and **MSVC Spectre-mitigated
libraries** (VS Installer components `Component.Microsoft.Windows.DriverKit.BuildTools` and
`Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre`). The WDK and SDK themselves come from NuGet.

```powershell
scripts\build-driver.ps1 -NuGet <path\to\nuget.exe>
```

Output: `build\driver-package\` - `vsvirtualmic.sys`, `vsvirtualmic.inf`, `vsvirtualmic.cat` (signed) and
`vsvirtualmic-test.cer` (the public test certificate). The next CMake install/`scripts\package.ps1` includes it
in the installer, which then offers the **Install the VoiceStudio Virtual Microphone** task.

### Test signing

`build-driver.ps1` creates a self-signed code-signing certificate **once**, in your own certificate store
(`Cert:\CurrentUser\My`, "CN=VoiceStudio Virtual Microphone Test Signing", private key **not exportable**), and signs
with it. Keep it: every build for your friends must be signed with the same certificate, or they would have to
trust a new one. If you lose it (new PC, reinstall), the script makes a new one; reinstalling VoiceStudio then
trusts the new one.

**Security note:** the installer adds this certificate to the PC's *Trusted Root* and *Trusted Publishers* stores
(so Windows installs the driver without a warning) and removes it on uninstall. While it is there, that PC trusts
anything signed with your key - so never share or export the private key, and only give the installer to people
who trust you.

## Install (you and your friends)

Test-signed drivers only load in **Windows Test Mode**, and Windows only allows Test Mode with **Secure Boot off**.
No program can turn Secure Boot off - it is a firmware (UEFI/BIOS) setting that only a person at the PC can change.
Setup therefore guides the user through it and **asks before every change** (`vs_vmic finish`):

1. Run the VoiceStudio installer and tick **Install the VoiceStudio Virtual Microphone**. The driver is installed.
2. **If Secure Boot is on**, setup explains why it has to be turned off (and what that means) and asks whether to
   restart straight into the firmware settings. If the user agrees, it pauses BitLocker for the next two restarts
   (so the recovery key is not needed), shows the steps (find *Secure Boot*, set it to *Disabled*, save and exit -
   often F10) and restarts into the firmware screen.
3. **After Windows starts again**, setup continues by itself: it asks whether to turn on Test Mode
   (`bcdedit /set testsigning on`), pauses BitLocker again and restarts.
4. **After that restart** it confirms that the microphone works. VoiceStudio switches its output to
   **VoiceStudio Mic Feed** by itself; in Discord, OBS, games or Windows choose **VoiceStudio Virtual Microphone**.

Saying no at any step changes nothing; the Start menu entry **Finish VoiceStudio Virtual Microphone setup** picks it
up again later. If the PC cannot restart into its firmware automatically, setup says so: restart and press the
firmware key (often F2, F10, F12, Del or Esc).

Some games' anti-cheat software (e.g. Vanguard, FACEIT, some EAC titles) refuses to run in Test Mode. Without Test
Mode the device installs but shows code 52 and the microphone stays inactive; VB-CABLE remains an option.

Uninstalling VoiceStudio removes the device, the driver package and the certificate, and - only if setup turned
Test Mode on - asks whether to turn it off again.

### By hand

```bat
certutil -addstore Root vsvirtualmic-test.cer
certutil -addstore TrustedPublisher vsvirtualmic-test.cer
vs_vmic install vsvirtualmic.inf     :: elevated; creates Root\VoiceStudioVirtualMic and installs the driver
vs_vmic status                       :: device state, Test Mode, Secure Boot
vs_vmic uninstall                    :: removes the device and the driver package
```

## Testing a kernel driver

A bug in a kernel driver can crash Windows (blue screen) instead of just one app. Try new builds on a spare PC or
a virtual machine (Hyper-V: Generation 2 VM with Secure Boot turned off) before giving them to friends.
