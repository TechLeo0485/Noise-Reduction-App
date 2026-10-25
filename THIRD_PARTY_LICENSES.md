# Third-party software

| Component | Version | License | Use | Distributed with the app |
|---|---|---|---|---|
| Qt (Core, Gui, Qml, Quick, QuickControls2) | 6.5+ | LGPL-3.0 (or commercial) | UI | Yes, as dynamically linked DLLs (LGPL requirement: users can replace them) |
| RNNoise (Xiph.Org Foundation, Jean-Marc Valin) | 0.2 | BSD-3-Clause, including the bundled model weights | Noise suppression | Yes, statically linked |
| Signalsmith Stretch + Signalsmith Linear (Signalsmith Audio) | 1.4.0 / 0.6.4 | MIT | Voice character (formant-preserving pitch shift) | Yes, header-only, compiled in |
| GoogleTest | 1.15.2 | BSD-3-Clause | Tests only | No |
| ONNX Runtime (Microsoft) | 1.31.0, CPU package | MIT | Strict voice isolation inference | Yes, `onnxruntime.dll` (loaded at runtime) |
| VoxLock (Arbaz Siddiqui), models `enroll.onnx` + `chunk_int8_full.onnx`, revision a50f4e1d | 1.x | Apache-2.0 (weights and code); model code derived from WeSep (Apache-2.0) | Strict voice isolation | Yes, `models/voxlock/`. `src/ai/voxlock/VoxLockStream.cpp` and `VoxLockEnrollment.cpp` port its streaming code |
| SysVAD virtual audio driver sample (Microsoft, Windows-driver-samples, commit bb68ed5e) | 2026-10 | MIT | VoiceStudio Virtual Microphone driver (`driver/`, modified) | Yes, `driver/vsvirtualmic.sys` (optional installer task) |
| Windows Driver Kit (Microsoft), NuGet `Microsoft.Windows.WDK.x64` | 10.0.28000.2526 | WDK license | Building the driver | No (build tool only) |
| Microsoft Windows SDK (WASAPI, Core Audio, DXGI, AVRT, DbgHelp) | system | Windows SDK license | Platform APIs | System components |
| VB-CABLE (VB-Audio Software), www.vb-cable.com | Driver Pack 45 | Donationware, proprietary | Virtual microphone | **Yes, inside the setup** (optional task, silent install). VB-Audio allows embedding VB-CABLE with silent installation when users can identify it as a VB-Audio product and are able to donate ([licensing](https://vb-audio.com/Services/licensing.htm)); setup names it, credits www.vb-cable.com and links to the author. Company / professional deployments where users cannot donate need licenses from VB-Audio's webshop. Only the standard VB-CABLE may be bundled (not A+B / C+D). |

Before release, copy the full license texts into `licenses/` in the installer:

- Qt: `LICENSE.LGPL3` and `LICENSE.GPL3` from the Qt installation, plus a written offer or a
  link to the Qt source code used.
- RNNoise: `COPYING` from the RNNoise source archive.
- ONNX Runtime: `LICENSE` and `ThirdPartyNotices.txt` from the ONNX Runtime package.
- Signalsmith Stretch / Linear: `LICENSE.txt` from each source archive.
- SysVAD: `driver/LICENSE.sysvad.txt` (MIT, Microsoft Corporation).
- VoxLock: `LICENSE` (Apache-2.0) and `NOTICE` from https://huggingface.co/arbazsiddiqui/voxlock
  ("VoxLock, Copyright 2026 Arbaz Siddiqui"; wesep/ from the REAL-TSE WeSep baseline, Apache-2.0).

No other models or datasets are included. Any further ONNX model must have its license
reviewed and listed here before it is added (see Phase 3 in `docs/implementation-checklist.md`).
