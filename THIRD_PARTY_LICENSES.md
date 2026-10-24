# Third-party software

| Component | Version | License | Use | Distributed with the app |
|---|---|---|---|---|
| Qt (Core, Gui, Qml, Quick, QuickControls2) | 6.5+ | LGPL-3.0 (or commercial) | UI | Yes, as dynamically linked DLLs (LGPL requirement: users can replace them) |
| RNNoise (Xiph.Org Foundation, Jean-Marc Valin) | 0.2 | BSD-3-Clause, including the bundled model weights | Noise suppression | Yes, statically linked |
| GoogleTest | 1.15.2 | BSD-3-Clause | Tests only | No |
| Microsoft Windows SDK (WASAPI, Core Audio, DXGI, AVRT, DbgHelp) | system | Windows SDK license | Platform APIs | System components |
| VB-CABLE (VB-Audio Software) | user-installed | Donationware, proprietary | Virtual microphone | **No.** Users install it themselves; it must not be redistributed without VB-Audio's permission |

Before release, copy the full license texts into `licenses/` in the installer:

- Qt: `LICENSE.LGPL3` and `LICENSE.GPL3` from the Qt installation, plus a written offer or a
  link to the Qt source code used.
- RNNoise: `COPYING` from the RNNoise source archive.

No other models or datasets are included. Any future ONNX model must have its license
reviewed and listed here before it is added (see Phase 3 in `docs/implementation-checklist.md`).
