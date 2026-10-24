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
        noise suppression: Off | DSP-only | RNNoise    (one-frame crossfade on change)
        noise gate / downward expander, held open by RNNoise VAD
        EQ → compressor → pitch   (user-configurable order)
        voice volume, master mute (ramped)
        limiter (zero latency) → NaN/Inf guard → hard clip ±1.0
  → SPSC ring (output) [+ SPSC ring (monitor, with monitor gain)]
  → render thread: adaptive resampler 48 kHz → device rate, PI drift control
  → mono → all device channels, device sample format
  → WASAPI render to "CABLE Input" (virtual cable playback endpoint)
  → apps record from "CABLE Output"
```

## Real-time rules (enforced by construction)

Inside the capture and render threads:

- No allocation: every buffer is sized in `open()`/`configure()`/constructors. Rings,
  resampler buffers, conversion buffers and RNNoise state exist before the thread starts.
- No locks: parameters and metrics are atomics; timing samples go through lock-free SPSC
  rings; failures are flagged with an atomic compare-exchange.
- No I/O or logging: the supervisor logs on the threads' behalf from polled state.
- Bounded work: each wake-up processes what the device delivered; each frame runs a fixed
  pipeline. The capture wait has a 200 ms timeout, so a stop request is never missed.
- No model initialisation: suppressors are created before streaming.
- MMCSS "Pro Audio" registration and FTZ/DAZ are set per thread (RAII).

## Buffering and the overflow/underflow policy

| Buffer | Size | Policy |
|---|---|---|
| WASAPI capture buffer | device-negotiated | packets drained on every event; `DATA_DISCONTINUITY` counted as a capture glitch |
| 10 ms frame accumulator | 480 samples | always drained when full |
| Output / monitor ring | 16384 samples (~340 ms) | producer never blocks; if full, the frame is dropped and an overrun counted (should not happen because of the backlog limit) |
| Jitter target | `max(user target, 10 ms frame + render period + 2 ms)` | PI controller steers mean fill to the target. A full frame is needed because, with drifting clocks, a render pass periodically sees no new frame |
| Backlog limit | target + 40 ms | excess discarded immediately; counted as a latency reset |
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
would otherwise click. Enabling/disabling the compressor and pitch shifter is ramped inside
each effect. Gain, volume and mute changes are ramped per frame.

## Fallback order

`Advanced AI → RNNoise → DSP-only`. The microphone always keeps producing audio. If the
requested engine is unavailable, the chain reports `requestedModeUnavailable`, which the UI
shows. A processor that emits NaN/Inf is neutralised by the output guard (and counted)
rather than allowed to reach the device.

## Device recovery

| Event | Detection | Response |
|---|---|---|
| Device unplugged / disabled / invalidated | stream thread gets `AUDCLNT_E_DEVICE_INVALIDATED` etc. | supervisor stops all streams, retries with back-off 250 ms → 5 s |
| Device stops delivering (driver hang, suspend) | no capture packet for 2 s | same as above |
| Device re-added | `IMMNotificationClient::OnDeviceAdded/StateChanged` | immediate retry (400 ms debounce) if waiting |
| Windows default changed | `OnDefaultDeviceChanged` (console role) | restart only if following the default |
| Output fails, input works | start result | runs in *Degraded* state: meters work, user sees an actionable message |
| Automatic recovery disabled | config | *Error* state with a restart button |
