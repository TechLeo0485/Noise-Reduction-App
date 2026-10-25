# Latency, benchmarks and long-run testing

**No measured results are recorded here yet.** The code has not been run on hardware. Fill in
the tables below from real runs, and record the machine, devices, buffer mode and build type.

## 1. Latency model

Three different quantities, reported separately:

| Quantity | What it is | Where it comes from |
|---|---|---|
| **Algorithmic latency** | Delay inherent in the algorithms, independent of CPU speed | RNNoise: 10 ms (one 480-sample frame). Pitch: about 12.7 ms when enabled. Resamplers: 16 input samples each (0.33 ms at 48 kHz). DSP-only, gate, EQ, compressor, limiter: 0 |
| **Processing execution time** | CPU time to process one 10 ms frame | Measured per frame (QPC); shown as P50/P95/P99 |
| **End-to-end latency** | Sound at the mic → sound available to the receiving app | Measured with a loopback (section 3) |

Components visible to the application (*Performance* screen, *Latency breakdown*):

```
capture device period          (negotiated; e.g. 10 ms default, 2.67-5 ms with IAudioClient3)
+ framing to 10 ms             (0 when the capture period is 10 ms; worst case 10 - period)
+ algorithmic                  (RNNoise 10 ms, pitch ~12.7 ms)
+ processing execution         (P50 measured)
+ resampler filters            (0.33 ms per resampling stage)
+ jitter buffer                (measured mean ring fill)
+ render queue                 (measured WASAPI padding)
= estimated total, excluding driver/converter latency and the virtual cable
```

Not visible to the application: USB/driver buffering, ADC/DAC converter latency and the
virtual cable's internal buffer. Only a loopback measurement captures these.

## 2. Processing benchmark (hardware independent)

```bat
build\ninja-release\bin\vs_bench.exe --seconds 120
```

Prints avg/P50/P95/P99/max microseconds per 10 ms frame for passthrough, gate, DSP, RNNoise,
the full chain, and the full chain with pitch. Also prints noise reduction during pauses and
level change during "speech" on a synthetic signal. The synthetic signal is not speech, so use
`--wav` with real recordings for quality conclusions.

Measured 2026-10-24, `vs_bench --seconds 30`, MSVC 14.44 Release, Qt 6.8.3, RNNoise 0.2 (portable C path, no SIMD dispatch):

| Machine / CPU | Mode | avg µs | P50 µs | P95 µs | P99 µs | max µs | RT load |
|---|---|---|---|---|---|---|---|
| i7-12700KF | passthrough | 6.8 | 6.6 | 7.3 | 8.3 | 178.1 | 0.07 % |
| i7-12700KF | gate | 6.8 | 6.5 | 7.1 | 8.2 | 125.5 | 0.07 % |
| i7-12700KF | dsp | 8.0 | 7.5 | 8.6 | 9.7 | 570.6 | 0.08 % |
| i7-12700KF | rnnoise | 310.2 | 299.6 | 359.2 | 491.0 | 1005.9 | 3.10 % |
| i7-12700KF | combined (RNNoise + gate + EQ + comp + limiter) | 328.5 | 315.7 | 432.1 | 495.3 | 1033.1 | 3.29 % |
| i7-12700KF | combined + pitch | 330.8 | 318.2 | 435.3 | 485.6 | 911.1 | 3.31 % |

On the synthetic signal, RNNoise reduced energy in the pauses by 25.8 dB and DSP-only by 10.2 dB. The gate showed 0 dB there because the synthetic noise (−34 dBFS) sits above the default −50 dBFS threshold. These are not speech-quality results.
RNNoise is compiled without its x86 SIMD kernels (MSVC takes the generic path), so there is headroom for optimisation.

The unit test `ChainPerformance.RnnoiseFullChainMeetsDeadline` asserts P99 < 5 ms as a
sanity bound only.

## 3. Loopback latency measurement

`vs_latency` (and the in-app *Test virtual microphone* button) play 6 ms, 1 kHz tone bursts
on a render endpoint and detect them on a capture endpoint. Both sides are timed with
WASAPI's QPC-stamped positions (`IAudioClock::GetPosition`, `IAudioCaptureClient::GetBuffer`).
Results are therefore independent of polling jitter in the tool.

### A. Virtual cable latency

```bat
vs_latency --list
vs_latency --out <CABLE Input index> --in <CABLE Output index>
```

### B. Device round trip (hardware baseline)

Connect an audio interface output to its input with a cable (or place a speaker against the
microphone in a quiet room).

```bat
vs_latency --out <interface output> --in <interface input>      -> L_device
```

### C. VoiceStudio end-to-end

1. Use the physical loop from B, and set VoiceStudio's microphone to the interface input and
   its output to **CABLE Input**.
2. Measure from the interface output to **CABLE Output**:
   `vs_latency --out <interface output> --in <CABLE Output>`   -> L_total
3. `L_total` contains the same interface render + capture path as `L_device`, plus everything
   VoiceStudio adds (framing, processing, ring, its render queue) plus the cable transit:

   ```
   L_total − L_device ≈ VoiceStudio added latency + virtual cable transit
   ```

   Report **L_total, L_device, L_cable (from A)** and the app's own estimate from the
   Performance screen together. `L_cable` from A is an upper bound for the cable transit
   (it also includes the test tool's own shared-mode buffering on the cable endpoints).

Repeat for each buffer mode (Low latency / Default / Safe) and suppression mode.

| Machine | Mic / interface | Buffer mode | Mode | L_device | L_cable | L_total | App estimate |
|---|---|---|---|---|---|---|---|
| *(not yet measured)* | | | | | | | |

## 4. 8-hour soak test procedure

Goal: no dropouts, no latency growth, no memory growth, and correct recovery over a long session.

Preparation:

1. Release build. Close other audio software. Disable sleep for the duration
   (`powercfg /change standby-timeout-ac 0`).
2. Connect the real microphone you want to qualify and install VB-CABLE.
3. Optional load: run a game or `vs_bench --seconds 28800` in parallel to add CPU pressure.

Run:

```bat
vs_soak --list
vs_soak --hours 8 --out <CABLE Input index> --csv soak_default.csv
vs_soak --hours 8 --out <CABLE Input index> --buffer low --csv soak_low.csv
```

For the restart/recovery variant, also run 1 h with `--restart-every 30`. During that hour,
unplug and replug the USB microphone a few times and confirm the CSV shows recovery
(`state` returns to `Running`).

Pass criteria (per run without forced restarts):

| Metric (CSV column) | Pass |
|---|---|
| `underruns` | 0 under normal load (document any under artificial load) |
| `latency_resets` | 0 |
| `buffered_ms` | stays within ±3 ms of the target for the whole run |
| `drift_ppm` | stable (a constant value, typically < ±300) |
| `private_mb` | growth < 5 MB after the first 2 minutes |
| `p99_us` | stable; no upward trend |
| `deadline_misses` | 0, or explained (e.g. by a system-wide stall in the event log) |

Also confirm the in-app counters after a full working day with Discord/Teams open.

## 5. Audio quality tests

1. Record test material at 48 kHz (mono, ≥ 30 s each) with the target microphones:
   - clean speech in a quiet room (reference)
   - speech + PC fan / air conditioning
   - speech + mechanical keyboard and mouse clicks
   - speech + traffic / household noise
   - speech + **another person talking in the background**
   - speech + **overlapping speech** (two people talking at once)
2. Process each file: `vs_bench --wav input.wav --out results\<name>`. That writes
   `original.wav`, `passthrough.wav`, `gate.wav`, `dsp.wav`, `rnnoise.wav`, `combined.wav`,
   `combined+pitch.wav`.
3. Objective metrics, where a clean reference exists (record clean speech and noise
   separately, mix digitally): PESQ (ITU-T P.862) and STOI via an offline Python script, and
   segmental SNR. Python is used only offline for evaluation, never in the app.
4. Controlled listening: blind A/B (or MUSHRA-style) comparisons by at least 3 listeners on
   closed headphones, rating intelligibility, naturalness, artifacts and residual noise.
5. Expected limitation to verify, not hide: RNNoise reduces diffuse background chatter but
   will not reliably remove a nearby second speaker or overlapping speech. Record what you hear.

| Material | Mode | PESQ | STOI | Listening notes |
|---|---|---|---|---|
| *(not yet measured)* | | | | |

## 6. Strict voice isolation (VoxLock INT8 Full)

Measured 2026-10-24 on an i7-12700KF (8P+4E, 20 threads), 32 GB, GTX 1050 Ti 4 GB, Windows 11,
ONNX Runtime 1.31.0. Reproduce with `vs_isolation` (see the header of `tools/vs_isolation.cpp`).

**Inference per `chunk_int8_full.onnx` call** (2 intra-op threads):

| Variant | 8 ms chunk (K=1) | 16 ms chunk (K=2) | 24 ms chunk (K=3) |
|---|---|---|---|
| Unmodified graph, dynamic shapes (Python ORT) | p50 12.8 ms | p50 15.3 ms | p50 16.4 ms |
| Enrollment folded + static shapes (what the app runs) | p50 6.6 ms, p99 9.8 ms | **p50 8.7–9.2 ms, p99 10.5–12.5 ms** | p50 11.1 ms |
| GPU, DirectML, INT8 Full (quantised LSTMs fall back to CPU) | | p50 12.7 ms | |
| GPU, DirectML, fp32 model | p50 21.1 ms | p50 21.8 ms | |

K=1 cannot keep up (p99 above the 8 ms budget); calibration picks K=2 on this machine. The GPU is
slower in every configuration, so the app uses the CPU only. CUDA was not tried: `DynamicQuantizeLSTM`
and `MatMulInteger` of the INT8 Full graph exist only in the CPU provider.

**Real-time runs** (`vs_isolation`, full chain, one frame every 10 ms of wall clock). Final configuration:
2 intra-op threads without spinning, worker / ONNX Runtime / loader threads on the P-cores with power
throttling (EcoQoS) disabled, 20 ms safety cushion:

| Condition | Isolation latency | Late chunks | Inference p50 / p99 | CPU |
|---|---|---|---|---|
| 3 × 90 s, desktop apps open (Discord, Chrome, Voicemod, ...) | 80 ms steady | 0, 0, 0 | 9.4–10.2 / 12.5–13.2 ms | 0.25 core (1.3 % of the CPU) |
| All 20 logical CPUs saturated by 20 busy processes, 30 s | 97–100 ms (calibration chose K=4) | 0 | 13.0 / 19.7 ms per 32 ms chunk | 0.4 core |

What did not work, for the record: without P-core selection, the same 90 s runs had 2–16 late chunks
(Windows moved the inference threads of the background process onto E-cores; inference p99 rose to 17 ms).
A 10 ms cushion reached 61–65 ms but produced occasional 10 ms gaps. ORT thread spinning cost ~1 core and
did not remove the gaps.

Capture-thread cost of `VoiceIsolator::process()`: p50 8 µs, max 35 µs per frame.

**Quality** (8 LibriSpeech dev-clean speaker pairs, other voice at 0 dB fully overlapping, enrolled from a
different utterance; Python offline scoring):

| | Mixture | Isolated (offline 16 kHz) | Isolated (real-time 48 kHz path) |
|---|---|---|---|
| SI-SDR | 0.0 dB | 12.0 dB | 12.2 dB |
| STOI | 0.73 | 0.92 | 0.92 |
| PESQ (wide-band) | 1.20 | 2.28 | 2.23 |
| Own voice alone: SI-SDR / PESQ | — | 35.8 dB / 4.44 | 26.1 dB / 4.28 |

The C++ port matches the official `voxlock.core.Stream` (same model, K=2): residual difference 41–102 dB
below the signal (INT8 rounding with static vs dynamic shapes); all metrics agree to 3 digits.

**Known limitation (model behaviour, also present in the official implementation):** other voices are
removed reliably while you talk, but not always while you are silent:

- Other person alone for the first 2 s of a stream: suppressed by 36–47 dB in 4 of 8 pairs; in the other
  4 their voice passed at full level for most of those 2 s (two pairs) or after 0.5–1 s (two pairs).
- Other person alone for 3 s after you have spoken: suppressed by 27–47 dB in 3 of 6 pairs; in the
  other 3 their first 0.5–1.5 s passed, after which they were suppressed by 25–50 dB.

A speaker-verification gate cannot catch these onsets within the latency budget (it needs about a second
of audio to decide), so none is added. Enroll in the same room and with the same microphone as the call,
and prefer a headset or a directional microphone when others speak nearby.
