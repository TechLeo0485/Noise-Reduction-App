# Lightweight filter (VoiceFilter-Lite design) - training kit

VoiceStudio's **Lightweight filter** isolation model follows Google's
[VoiceFilter-Lite](https://google.github.io/speaker-id/publications/VoiceFilter-Lite/) design
([paper](https://arxiv.org/abs/2009.04323)). Google did not publish code or a trained model, and the paper's model
outputs speech-recognition features rather than audio, so this folder builds and trains a version that produces
audio for VoiceStudio. Until you train it and place the file, the app lists the model as *needs model file*.

## What is the same as the paper, what is different

| | Paper | Here |
|---|---|---|
| Speaker input | d-vector of the enrolled user | the CAM++ voice print VoiceStudio already computes from your recording (512 values) |
| Network | log-mel features, 3 uni-directional LSTM layers, mask | same (128 mel bins, 3 x 256 LSTM, 1.8 M parameters, 2.4 MB in 8-bit) |
| Adaptive suppression | second head predicts interference; mask applied as `w*mask + (1-w)` | same |
| Loss | asymmetric L2 on compressed spectra (penalises cutting the user) | same, plus BCE for the suppression head |
| Output | enhanced filterbanks for an ASR model, no audio | mask on the 257 STFT bins, mixture phase, inverse STFT inside the graph |
| Framing | 25 ms / 10 ms | 32 ms / 8 ms periodic Hann, to match VoiceStudio's streaming contract exactly |
| Training data | speech + interferers + noise | the same, plus *other voice only* (user silent) and mouse/keyboard clicks, which VoiceStudio must remove |

## Steps

Requirements: Python 3.10+, a CUDA GPU for real training (a CPU only works for the smoke test), and disk space for
the data. From the repository root:

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install -r tools\voicefilter\requirements.txt
# for an NVIDIA GPU, install the CUDA build of PyTorch instead: https://pytorch.org/get-started/locally/
cd tools\voicefilter
```

1. **Data.** Clean read speech with many speakers, one folder per speaker, for example LibriSpeech
   `train-clean-360` (921 speakers, CC BY 4.0, <https://www.openslr.org/12>). Optional real noise, for example
   MUSAN `noise/` (<https://www.openslr.org/17>) or the DNS Challenge noise set. Without real noise, synthetic noise
   and clicks are used.

2. **Index and voice prints** (CAM++ from the app's model folder; a few minutes per 100 speakers):

   ```powershell
   ..\..\.venv\Scripts\python prepare.py --speech-dir D:\data\LibriSpeech\train-clean-360 `
       --campp ..\..\build\ninja-release\bin\models\voxlock --out work
   ```

3. **Train** (on one modern GPU roughly a day for 200k steps; quality keeps improving for longer):

   ```powershell
   ..\..\.venv\Scripts\python train.py --work work --noise-dir D:\data\musan\noise --out runs\vfl --device cuda
   ```

   Every 2000 steps it prints, on speakers it never trained on: `sdr_gain` (how much cleaner your voice gets when
   someone talks over you, dB, higher is better), `other_db` (how much quieter another voice gets while you are
   silent, lower is better; aim for -20 dB or less) and `target_db` (your voice alone; should stay within ~1 dB of 0).
   `--resume runs\vfl\last.pt` continues a run.

4. **Export** (8-bit, checked chunk by chunk against PyTorch, the way the app runs it):

   ```powershell
   ..\..\.venv\Scripts\python export.py --checkpoint runs\vfl\last.pt --out ..\..\models\voicefilter\voicefilter_lite.onnx --int8
   ```

5. **Use it.** Re-run CMake (it picks up `models/voicefilter/voicefilter_lite.onnx`, copies it next to the app and into
   the installer) or copy the file to `<app folder>\models\voicefilter\`. In the app, *Only my voice* ->
   *Isolation model* -> **Lightweight filter**. Compare it with Full isolation using
   `vs_isolation --model voicefilter` / `--model voxlock` on the same recording.

## Testing the app integration without training

`python export.py --identity --out <app>\models\voicefilter\voicefilter_lite.onnx` writes a pass-through model
(mask = 1). The app then runs the whole path - voice print, ONNX session, streaming, voice check - with audio passing
unchanged, which is how the integration was verified (0.4 ms per 16 ms chunk on an i7-12700KF). Delete the file
afterwards: it does not filter anything.

## The ONNX contract

```
inputs : wav (1, S) float, S = (K-1)*128 + 512   dvector (1, 512)   h, c (layers, 1, hidden)
outputs: frames (1, K, 512)  synthesis-windowed output frames (periodic Hann; overlap-added by the app)
         h_out, c_out        the LSTM state for the next chunk
```

Dimension names must be `S` and `K` (the app fixes them per chunk size). The app binds `dvector` as a constant, so
the speaker projection is folded once per voice. `layers * hidden` must not exceed 49152.
