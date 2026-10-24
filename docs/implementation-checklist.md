# Implementation checklist

Legend: **[x]** implemented in code · **[v]** verified by building and running · **[ ]** not started

> **2026-10-24:** The project builds cleanly (MSVC 14.44, Qt 6.8.3; one fix was needed: a
> compatibility `os_support.h` for RNNoise 0.2). All 72 unit tests and all 5 hardware tests
> pass on an i7-12700KF; processing benchmarks are recorded in latency-benchmarks.md.
> **Not yet verified:** the GUI interactively, audio output to VB-CABLE, end-to-end
> loopback latency, device unplug recovery, and an 8-hour soak.

## Phase 1: native audio pipeline

- [x] CMake project, presets, dependency fetching (RNNoise, GoogleTest), vcpkg manifest
- [x] Qt 6 / QML application shell, dark theme, six screens
- [x] Microphone and output enumeration (Core Audio), friendly names, mix formats
- [x] Hot-plug, removal and default-device notifications
- [x] Device selection, follow-default option, stable endpoint IDs persisted
- [x] WASAPI shared-mode, event-driven capture and render; IAudioClient3 minimum period
- [x] Sample-format conversion (float32/int16/int24/int32), channel down/up-mix
- [x] Sample-rate conversion to/from 48 kHz
- [x] Lock-free SPSC rings, bounded buffering, documented overflow/underflow policy
- [x] Clock-drift compensation (adaptive resampling)
- [x] MMCSS, FTZ/DAZ, no allocation/locks on audio threads
- [x] Input/output level meters
- [x] Virtual cable output + connectivity/latency test
- [v] Build on Windows 11 (MSVC 14.44, Qt 6.8.3); unit tests pass
- [v] Real microphone capture + processing, 20 start/stop cycles, missing-device error path (hardware tests)
- [ ] GUI and VB-CABLE output verified interactively

## Phase 2: noise reduction

- [x] `INoiseSuppressionProcessor` interface
- [x] DSP-only mode (adaptive expander), high-pass, noise gate with VAD hold
- [x] RNNoise integration (preallocated state, native 480-sample frames, strength blend)
- [x] Click-free engine switching and enable/disable
- [x] Processing statistics: P50/P95/P99, deadline misses, underruns, overruns, drift
- [x] Performance test and `vs_bench`
- [v] Measured P50/P95/P99 recorded in latency-benchmarks.md (i7-12700KF)

## Phase 3: hardware selection and AI

- [x] CPU detection (CPUID vendor/brand, cores, SIMD with OS support check, memory)
- [x] GPU enumeration (DXGI: name, vendor, LUID, dedicated memory)
- [x] Backend list that shows unavailable devices as disabled with reasons
- [ ] ONNX Runtime integration (CPU / DirectML / QNN / OpenVINO providers)
- [ ] Model selection: evaluate licensing, size, latency (candidates: DTLN, DeepFilterNet,
      FRCRN, GTCRN); reject models that cannot meet a 10 ms hop on mid-range CPUs
- [ ] Off-thread model loading + validation + atomic swap into the chain
- [ ] Backend benchmark (warm-up, P95/P99, deadline reliability) and Auto selection
- [ ] NPU discovery via execution providers (not CPU branding)

## Phase 4: voice customization

- [x] 5-band EQ, compressor, limiter, pitch shift (low latency, off by default)
- [x] Configurable effect order
- [x] Built-in presets + custom presets, saved configuration (versioned JSON, validated)
- [x] Performance modes

## Phase 5: reliability and advanced features

- [x] Supervisor with recovery back-off, stall detection, degraded mode
- [x] Logging (privacy-safe), minidumps, diagnostics report
- [x] Soak-test tool (`vs_soak`) and procedure
- [x] Installer script (Inno Setup) and deploy step (windeployqt via CMake install)
- [ ] Target-speaker isolation (experimental; enrollment stored locally, deletable)
- [ ] Suspend/resume verified on real hardware
- [ ] Code signing
- [ ] Optional per-packet DSP-only path to remove 10 ms framing in DSP mode
