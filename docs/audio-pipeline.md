# Audio pipeline

## Signal flow

```
Physical microphone
  → WASAPI capture (shared, event-driven, device mix format; IAudioClient3 min period in Low-latency mode)
  → interleaved → mono float (channel average; int16/24/32 and float32 supported)
  → resample to 48 kHz if the device rate differs (windowed-sinc, 32 taps)
  → 10 ms framing (480 samples)
  → VoiceProcessingChain:
        NaN/Inf guard
        input gain (ramped)
        high-pass, 4th-order Butterworth (default 80 Hz)
        strict voice isolation (optional): frame → SPSC ring → isolation worker thread
            [48 → 16 kHz, VoxLock INT8 Full chunk.onnx (ONNX Runtime CPU), 16 → 48 kHz]
            → SPSC ring → frame (isolated voice or silence; see below)
        noise suppression: Off | DSP-only | RNNoise    (one-frame crossfade on change)
        noise gate / downward expander, held open by RNNoise VAD
        voice stabilizer (distance): slow speech leveler to −24 dBFS RMS (−10..+12 dB) and a 200 Hz low shelf
            (−8..+4 dB) that undoes changes in the low/mid balance against the speaker's own ~2 min average
            (proximity effect when leaning in, thin voice further away); adapts only during speech, zero latency
        EQ → compressor → voice character   (user-configurable order; voice character =
            formant-preserving pitch (Signalsmith Stretch, 35 ms blocks) → 90 Hz high-pass → anti-boom
            low shelf at 150 Hz (−1 dB per semitone lower, −0.5 dB per semitone of body, max −7 dB) → presence peak)
        voice volume, master mute (ramped)
        limiter (zero latency) → NaN/Inf guard → hard clip ±1.0
  → SPSC ring (output) [+ SPSC ring (monitor, with monitor gain)]
  → render thread: adaptive resampler 48 kHz → device rate, PI drift control
  → mono → all device channels, device sample format
  → WASAPI render to "CABLE Input" (virtual cable playback endpoint)
  → apps record from "CABLE Output"
```

## Strict voice isolation

`VoiceIsolator` (src/audio/isolation) keeps only the enrolled voice. The model never runs on the
capture thread: the capture thread pushes each 10 ms frame into a lock-free ring, wakes the
isolation worker (MMCSS Pro Audio) and pops the isolated audio produced earlier. Inference is a
C++ port of the official `voxlock.core.Stream` (src/ai/voxlock/VoxLockStream.cpp): reflect start
padding, fixed K-hop chunks of `chunk_int8_full.onnx`, the six LSTM states carried from call to
call, weighted overlap-add with the squared Hann window and the -0.5 output gain.

- **Fail-closed.** While the switch is on, a frame is either isolated audio or silence. Silence is
  used while the model loads, without an enrolled voice, after an inference error (the stream then
  restarts), when the worker is late (one frame of silence, then the cushion grows) and after a
  ring overflow. The only microphone audio after switching on is a 2 ms fade-out in the very
  frame where the switch flips. Presets never change the switch.
- **Enrollment.** The UI records 10 s of the microphone after input gain and high-pass (the signal
  the model sees). The recording is resampled to 16 kHz, checked (level, clipping, speech-like
  dynamics), its loudest 3 s are run through `enroll.onnx` (`fit_sample`), and the result is bound
  into a new chunk session. The previous voice keeps running until the new one is ready.
- **Constant folding.** The chunk graph recomputes the enrollment-only branch (32 per-band
  key/value projections of the speaker context) on every call. `bindInputsAsConstants()` turns
  `enroll_mag` and `context` into initializers so ONNX Runtime folds that branch once per voice:
  per-call time drops by about 40 %, with output identical to the unmodified graph.
- **Calibration (any computer).** On every start and enrollment the loader measures inference on this
  computer and picks the smallest chunk K (8 ms hops) with p50 ≤ 75 % and p95 ≤ 100 % of the chunk's
  duration. It tries K = 2, 3, 4, 6, 8, 12, 16 - only sizes whose delay fits the budget (see below) - and on
  CPUs with 6+ logical cores 2 and 4 threads. Larger chunks cost less per second of audio (i7-12700KF:
  59 % of real time at K = 2, 32 % at 8, 27 % at 16), so a slower laptop keeps up with more delay (K = 8:
  about 130 ms). Each setting is measured for at most ~1.5 s. If nothing keeps up, it does not run the
  model anyway (constant gaps) but switches to **light isolation**: the stream passes the microphone
  (`PassThroughChunkModel`), the noise filter and the voice check still work, so steady noise is removed
  and other voices are muted while the user is silent (not while both talk). The card shows the mode.
- **Latency.** Fixed delay = VoxLock's 24 ms look-ahead + chunking + 2 ms of resampling + one frame
  of hand-over + a 20 ms safety cushion: about 80 ms on an i7-12700KF. The start-up cushion assumes
  the worst frame/hop phase; slack above the cushion is trimmed in 5 ms steps during silence
  (1 s observation windows). A late chunk costs one 10 ms frame of silence and grows the cushion by
  5 ms; after 20 s without a late chunk the cushion shrinks by 5 ms again (down to 20 ms), so one bad
  moment does not cost delay for the rest of the call. Several late chunks within 10 s (a busy or
  throttled laptop) step up to the next chunk size that fits the budget; at the largest, light isolation.
- **Delay limit.** The isolated voice is never further behind than the engine's limit for it (what
  the 300 ms budget leaves, see below; 50..250 ms). Backlog above it is dropped at once (with a fade),
  and the cushion never grows past it.
- **Processor.** When a graphics card is chosen, the CPU is measured too; if the CPU keeps up where
  the card does not, or needs smaller chunks (less delay), the CPU is used. Older GPUs (e.g. a GTX 1050 Ti:
  ~16 ms per 16 ms chunk vs ~8 ms on the CPU) are often slower for this small model.
- **Scheduling.** The worker, ONNX Runtime's pool thread and the loader opt out of power
  throttling and, on hybrid CPUs, run on the performance cores only (`preferPerformanceCores()`),
  because VoiceStudio usually runs in the background during a call.
- **Voice check (other voices while the user is silent).** VoxLock follows "the voice most like the
  sample". With only someone else talking it has no right answer, and a similar voice (same gender)
  passed at full level; its LSTM state then stayed on that voice, so the user was cut by ~10 dB when
  they spoke again. A checker thread (src/ai/voxlock/SpeakerCheck.cpp) runs the CAM++
  speaker-verification network on the isolated 16 kHz output, before the gate: every 0.3 s with new
  speech it compares the last 1.5 s of speech (pauses cut out) with a reference built from the user's
  whole recording. Below a similarity of 0.15 the worker fades the output to silence in 20 ms and
  resets the LSTM state (again every 0.5 s while muted: VoxLock locks onto the first voice it hears, so
  the user must be heard after a fresh reset). A pause of 0.3 s ends the other voice's turn: the state
  is reset, and the worker opens the gate itself (5 ms fade-in) on the first chunk of new speech, so
  the user's first syllable passes. The checker judges that new speech after 0.4 s and mutes again
  below 0.15; the other voice therefore passes for ~0.4 s at the start of each phrase after a pause.
  Without a pause (talking over the other voice), the gate reopens above 0.22 over 0.8 s of speech.
  The checker thread runs at below-normal priority on the E-cores. Measured with the user's recording
  and TTS voices: the other voice went from 0 dB (fully passed) to about -10 dB overall (muted after
  ~1.5 s, short leaks at phrase starts); the user's replies after pauses of 0.35-1.5 s keep their start
  (0 dB from the first 250 ms instead of ~0.8 s muted); talking over the other voice keeps the user at
  -4 dB instead of -10 dB. When another, louder voice keeps talking over the user in a noisy room the
  model itself cannot separate them, so the output can stay muted.
  CAM++ needs a small model fix for ONNX Runtime 1.24 (`AveragePool count_include_pad`, see
  `Runtime::createSpeakerModel()`); without it every voice looked alike. Without the CAM++ file the
  check is off and isolation works as before. `vs_isolation --similarity` prints the scores.
- **Noise under the voice.** VoxLock silences noise in pauses but passes steady room noise under
  the voice at almost full level. `NoiseFloorFilter` runs on each 32 ms output frame inside the
  stream (no extra delay): per frequency it tracks the noise floor of the microphone input
  (minimum of the smoothed spectrum, rising 3 dB/s) and subtracts it from the output frame
  (x2, floor -18 dB, smoothed across bins). Fan + pink noise: noise between words 10-20 dB lower;
  steady noise passed under the voice ~11 dB lower; the voice's SI-SDR about 1 dB lower.

## Real-time rules (enforced by construction)

Inside the capture and render threads:

- No allocation: every buffer is sized in `open()`/`configure()`/constructors. Rings,
  resampler buffers, conversion buffers and RNNoise state exist before the thread starts.
- No locks: parameters and metrics are atomics; timing samples go through lock-free SPSC
  rings; failures are flagged with an atomic compare-exchange.
- No I/O or logging: the supervisor logs on the threads' behalf from polled state.
- Bounded work: each wake-up processes what the device delivered; each frame runs a fixed
  pipeline. The capture wait has a 200 ms timeout, so a stop request is never missed.
- No model initialisation: suppressors are created before streaming. VoxLock sessions are built
  on the isolation loader thread and handed to the isolation worker; the capture thread only
  sees atomics and rings.
- MMCSS "Pro Audio" registration and FTZ/DAZ are set per thread (RAII).

## Delay budget: 300 ms from microphone to app

`kMaxTotalLatencyMs` (EngineConfig.h) caps the whole path, including the virtual microphone. When the
streams start, the engine adds up the worst case of every fixed part — capture period, 10 ms framing,
resamplers, noise suppressor, voice character (35 ms), jitter target plus its 60 ms reset limit, the
render buffer and 25 ms for the virtual microphone — and gives strict isolation what is left
(`VoiceIsolator::setMaxLatencyMs`). Settings that would break the budget are clamped: jitter target and
backlog limit ≤ 60 ms, Safe-mode buffer ≤ 100 ms. VoiceStudio's driver skips back to 20 ms whenever its
reader falls more than 50 ms behind. Setup → Delay shows the measured total and its parts.

## Buffering and the overflow/underflow policy

| Buffer | Size | Policy |
|---|---|---|
| WASAPI capture buffer | device-negotiated | packets drained on every event; `DATA_DISCONTINUITY` counted as a capture glitch |
| 10 ms frame accumulator | 480 samples | always drained when full |
| Output / monitor ring | 16384 samples (~340 ms) | producer never blocks; if full, the frame is dropped and an overrun counted (should not happen because of the backlog limit) |
| Jitter target | `max(user target, 10 ms frame + render period + 2 ms)` | PI controller steers mean fill to the target. A full frame is needed because, with drifting clocks, a render pass periodically sees no new frame |
| Backlog limit | target + 40 ms (at most 60 ms) | excess discarded immediately; counted as a latency reset |
| Render device queue | ≤ 2 device periods (whole buffer in Safe mode) | keeps render latency bounded |

**Underflow:** the render side fades out what it has, pads with silence, counts an underrun,
and waits until the ring is back at the target before resuming with a 2 ms fade-in.

**Why latency cannot creep:** with independent clocks, the ring would slowly fill or drain.
The drift controller adjusts the resampling ratio by up to ±0.5 % (a 300 ppm drift needs
0.03 %), and the backlog limit catches anything the controller cannot, such as a stall
followed by a burst.

## Mode switching

Changing suppression engine processes one frame with both engines and crossfades with a
raised-cosine curve. RNNoise output lags its input by one frame, so the crossfade blends
signals 10 ms apart for 10 ms. That is inaudible in practice and avoids the hard step that
would otherwise click. Enabling/disabling the compressor is ramped inside the effect. The voice character keeps the
dry signal until its shifter has valid output (~35 ms after a start), then crossfades over one
frame; turning it off crossfades back. Gain, volume and mute changes are ramped per frame.

## Fallback order

`Advanced AI → RNNoise → DSP-only`. The microphone always keeps producing audio. If the
requested engine is unavailable, the chain reports `requestedModeUnavailable`, which the UI
shows. A processor that emits NaN/Inf is neutralised by the output guard (and counted)
rather than allowed to reach the device.

Strict voice isolation has no fallback by design: if it cannot run, the output is silent until
the user switches it off.

## Device recovery

| Event | Detection | Response |
|---|---|---|
| Device unplugged / disabled / invalidated | stream thread gets `AUDCLNT_E_DEVICE_INVALIDATED` etc. | supervisor stops all streams, retries with back-off 250 ms → 5 s |
| Device stops delivering (driver hang, suspend) | no capture packet for 2 s | same as above |
| Device re-added | `IMMNotificationClient::OnDeviceAdded/StateChanged` | immediate retry (400 ms debounce) if waiting |
| Windows default changed | `OnDefaultDeviceChanged` (console role) | restart only if following the default |
| Output fails, input works | start result | runs in *Degraded* state: meters work, user sees an actionable message |
| Automatic recovery disabled | config | *Error* state with a restart button |
