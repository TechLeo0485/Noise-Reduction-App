# Architecture

## 1. System overview

```
 ┌──────────────────────── UI thread (Qt / QML) ────────────────────────┐
 │  Main.qml + screens  ◄──►  EngineController (QObject)                │
 │                              │  atomics (params, meters, counters)   │
 │                              │  commands (config, restart, enable)   │
 └──────────────────────────────┼───────────────────────────────────────┘
                                ▼
 ┌──────────── Supervisor thread (non-real-time, MTA) ───────────┐
 │ EngineSupervisor: start/stop, device re-open, recovery with   │
 │ back-off, stall detection, diagnostics that need the devices  │
 └──────────────────────────────┬─────────────────────────────────┘
                                ▼ owns
 ┌───────────────────────────── AudioEngine ─────────────────────────────┐
 │                                                                         │
 │  Capture thread (MMCSS "Pro Audio")         Render thread(s) (MMCSS)    │
 │  WASAPI capture ─► mono float ─► resample   SPSC ring ─► adaptive       │
 │  to 48 kHz ─► 10 ms framing ─►              resampler (drift) ─►         │
 │  VoiceProcessingChain ─► SPSC ring(s) ────► device format ─► WASAPI      │
 │                                             (output = CABLE Input,       │
 │                                              optional monitor)           │
 └─────────────────────────────────────────────────────────────────────────┘
```

Source layout:

| Directory | Contents | Qt? |
|---|---|---|
| `src/core` | parameter registry, SPSC ring, DSP math, MMCSS/QPC helpers | no |
| `src/dsp` | biquads, high-pass, gate/expander, compressor, limiter, EQ, pitch, DSP suppressor | no |
| `src/ai` | `INoiseSuppressionProcessor`, RNNoise integration, VoxLock streaming port + ONNX Runtime loader (`voxlock/`) | no |
| `src/audio` | engine, supervisor, processing chain | no |
| `src/audio/wasapi` | WASAPI streams, client initialisation, COM helpers | no |
| `src/audio/buffers` | resampler, sample-format conversion, adaptive ring reader | no |
| `src/audio/isolation` | `VoiceIsolator`: fail-closed strict voice isolation stage, worker/loader threads, enrollment recording | no |
| `src/audio/monitoring` | lock-free metrics, percentile statistics | no |
| `src/devices` | endpoint enumeration and notifications | no |
| `src/hardware` | CPU (CPUID) / GPU (DXGI) detection, backend list | no |
| `src/diagnostics` | logger, process metrics, loopback latency tester | no |
| `src/settings` | presets/performance profiles (no Qt); JSON settings store (Qt Core) | partly |
| `src/app` | `main.cpp`, `EngineController`, crash handler | yes |
| `ui/` | QML theme, components, screens | yes |
| `tools/` | `vs_bench`, `vs_latency`, `vs_soak` | no |

The audio engine (`vs_core`) has no Qt dependency. It is linked unchanged by the app, the CLI
tools and the tests.

## 2. Threads and ownership

| Thread | Priority | May block? | Responsibilities |
|---|---|---|---|
| UI | normal | yes | QML, timers polling metrics (30 Hz meters, 2 Hz stats), settings save |
| Supervisor | normal | yes | WASAPI init, restarts, recovery, health checks every 100 ms |
| Capture | MMCSS Pro Audio | **no** | packet read, conversion, resampling, framing, processing, ring push |
| Render (×1–2) | MMCSS Pro Audio | **no** | ring pop, drift-compensated resampling, conversion, device write |
| Isolation worker | MMCSS Pro Audio | not on audio threads | 48↔16 kHz resampling, VoxLock inference (calls ONNX Runtime), overlap-add |
| ONNX Runtime pool (1 thread) | MMCSS Pro Audio | — | second intra-op thread of each inference (created through ORT's custom thread hooks) |
| Isolation loader | normal | yes | loads `onnxruntime.dll` + models, runs `enroll.onnx`, builds and calibrates per-voice sessions |
| Windows notification | system | must not | device events, forwarded with a queued Qt invocation |

Communication:

- **UI → audio:** `EngineParameters` is an array of `std::atomic<float>`, read with relaxed
  loads once per 10 ms frame. No locks, no queues, no allocation.
- **Audio → UI:** `EngineMetrics` atomics (meters, counters) plus two SPSC rings of timing
  samples consumed by `StatsCollector`.
- **Audio → supervisor:** `StreamFailure` atomic flag (set on `AUDCLNT_E_DEVICE_INVALIDATED`
  etc.) and the capture heartbeat (`lastCaptureTick`), both polled.
- **UI → supervisor:** mutex-protected command queue (non-real-time on both sides).

Lifetime: `EngineController` owns `EngineSupervisor`, which owns `AudioEngine`, which owns
the streams, rings and chain. Stream threads are always joined before the objects they use
are reconfigured or destroyed (`AudioEngine::stop()` stops capture first, then render).

## 3. Key design decisions

1. **Processing runs on the capture thread.** That avoids a third hand-off and its buffer.
   Processing finishes well inside the capture period (see `vs_bench`), and the render side
   is decoupled by the ring.
2. **Fixed 48 kHz mono, 10 ms frames internally.** This is RNNoise's native format, so the
   suppressor needs no extra reframing or resampling. All modes use the same framing, which
   makes mode switches seamless (one-frame crossfade). The cost is up to 10 ms of framing
   delay when the capture period is shorter than 10 ms.
3. **Shared mode with IAudioClient3 rather than exclusive mode.** Exclusive mode would lock
   the microphone away from other apps and fails on many consumer devices. IAudioClient3
   gives small periods (often 2.67–5 ms on inbox drivers) without that.
4. **Adaptive resampling for clock drift** (PI controller on ring fill, ±0.5 % max) instead
   of sample dropping/duplication. It is inaudible and keeps latency constant over hours.
5. **Hard bounds instead of growing queues.** Fixed-capacity rings, a backlog limit that
   resets latency, and re-priming after underruns. See [audio-pipeline.md](audio-pipeline.md).
6. **Pre-allocated, always-present suppressors.** DSP and RNNoise instances are created
   with the chain, before streams start. Switching modes is an atomic parameter change; no
   object is created or destroyed on the audio thread. The future ONNX engine will be built
   and validated on the supervisor thread, then published with an atomic pointer swap; the
   retired instance is reclaimed off-thread.
7. **Single parameter registry** (`core/Parameters.*`). UI ranges, presets, settings
   validation and the engine all use the same table, so they cannot disagree.
8. **Honest reporting.** Unavailable backends are listed but disabled with a reason.
   Latency components that cannot be measured are labelled as estimates.

## 4. Extension points

- **New noise suppressor / AI model:** implement `INoiseSuppressionProcessor` (480-sample
  frames at 48 kHz, RT-safe `processFrame`). Load/validate it outside the audio threads and
  register it in `VoiceProcessingChain`. WASAPI code is untouched.
- **New effect:** implement `IAudioProcessor`, add parameters to the registry, add an
  `EffectSlot`.
- **New hardware backend:** add a `ProcessingBackend` entry produced by real capability
  detection (e.g. ONNX Runtime provider enumeration) and mark it available only when a
  session can actually be created.

## 5. Risks and limitations

| Risk | Impact | Mitigation / status |
|---|---|---|
| Code not yet compiled or run | Unknown compile/runtime defects | First build + test pass on a real machine is the next step |
| RNNoise 0.2 C sources may need small fixes for MSVC | Build failure | `VOICESTUDIO_WITH_RNNOISE=OFF` fallback; `RNNOISE_SOURCE_DIR` for a patched tree |
| VB-CABLE adds its own latency (driver buffer) | Higher end-to-end delay | Measured by the cable test; documented |
| Shared-mode device formats at 44.1 kHz or other rates | Resampling adds ~0.3 ms and CPU | Recommend 48 kHz in Windows sound settings |
| 10 ms framing | Up to 10 ms added delay with short capture periods | Accepted for RNNoise; a DSP-only per-packet path could remove it later |
| Bluetooth HFP microphones (16 kHz, high latency) | Quality and delay limited by the device | Safe buffer mode; documented |
| Background-speaker suppression | RNNoise cannot separate voices | Feature marked experimental and unavailable; never claimed |
| Zero-latency limiter | Can distort on extreme transients | Compressor in front; ceiling configurable |
| Voice character (Signalsmith Stretch, formant-preserving) | ~35 ms delay while active | Off by default; bypassed (0 ms) when Natural |
