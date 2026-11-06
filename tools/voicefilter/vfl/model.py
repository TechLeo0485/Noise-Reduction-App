"""VoiceFilter-Lite-style target-speaker filter, adapted to produce audio for VoiceStudio.

Design (Wang et al., "VoiceFilter-Lite: Streaming Targeted Voice Separation for On-Device Speech Recognition",
Interspeech 2020, arXiv:2009.04323):

* conditioned on a speaker embedding of the enrolled user (the paper: d-vector; here: the CAM++ voice print that
  VoiceStudio already computes for its voice check, 512 values, unit length);
* log-mel filterbank input, uni-directional LSTMs only (streaming, no look-ahead beyond one STFT frame);
* a mask, and the paper's *adaptive suppression strength*: a second head predicts per frame whether interference
  is present (w); the applied mask is w * mask + (1 - w), so clean speech is left alone;
* trained with the paper's *asymmetric* L2 loss that penalises over-suppression of the user (see train.py).

Changes for listening instead of speech recognition: the paper enhances filterbank features fed to an ASR model and
never reconstructs audio. Here the mask is predicted on the 257 STFT bins and applied to the complex spectrum
(mixture phase); the inverse STFT is part of the graph. Framing matches VoiceStudio's streaming contract exactly
(voxlock::Stream): 16 kHz, 512-sample periodic Hann frames, 128-sample hop. The forward pass maps a chunk of
(K-1)*128 + 512 samples to K synthesis-windowed output frames and carries the LSTM state, so the C++ side runs it
like VoxLock's chunk.onnx (see Runtime::createVoiceFilterModel()).
"""

import math

import torch
import torch.nn as nn
import torch.nn.functional as F

SAMPLE_RATE = 16000
N_FFT = 512
HOP = 128
BINS = N_FFT // 2 + 1
EMBEDDING_DIM = 512  # CAM++ (3D-Speaker, VoxCeleb) as used by VoiceStudio


def hann_periodic(n: int) -> torch.Tensor:
    return 0.5 - 0.5 * torch.cos(2.0 * math.pi * torch.arange(n, dtype=torch.float64) / n)


def mel_filterbank(n_mels: int) -> torch.Tensor:
    """(BINS, n_mels) HTK mel filterbank from 20 Hz to 8 kHz (no dependency on torchaudio at export time)."""
    def hz_to_mel(f):
        return 2595.0 * math.log10(1.0 + f / 700.0)

    def mel_to_hz(m):
        return 700.0 * (10.0 ** (m / 2595.0) - 1.0)

    lo, hi = hz_to_mel(20.0), hz_to_mel(SAMPLE_RATE / 2)
    centers = [mel_to_hz(lo + (hi - lo) * i / (n_mels + 1)) for i in range(n_mels + 2)]
    freqs = torch.arange(BINS, dtype=torch.float64) * SAMPLE_RATE / N_FFT
    fb = torch.zeros(BINS, n_mels, dtype=torch.float64)
    for m in range(n_mels):
        left, center, right = centers[m], centers[m + 1], centers[m + 2]
        up = (freqs - left) / max(center - left, 1e-6)
        down = (right - freqs) / max(right - center, 1e-6)
        fb[:, m] = torch.clamp(torch.minimum(up, down), min=0.0)
    return fb


class StftFrontEnd(nn.Module):
    """Framing, analysis window and real DFT as one strided convolution (exports as a plain ONNX Conv)."""

    def __init__(self):
        super().__init__()
        n = torch.arange(N_FFT, dtype=torch.float64)
        k = torch.arange(BINS, dtype=torch.float64)
        angle = 2.0 * math.pi * k[:, None] * n[None, :] / N_FFT
        w = hann_periodic(N_FFT)
        weight = torch.cat([torch.cos(angle) * w, -torch.sin(angle) * w], dim=0)[:, None, :]  # (2*BINS, 1, N_FFT)
        self.register_buffer("weight", weight.float())

    def forward(self, wav: torch.Tensor):
        x = F.conv1d(wav[:, None, :], self.weight, stride=HOP).transpose(1, 2)  # (B, K, 2*BINS)
        return x[..., :BINS], x[..., BINS:]


class IstftBackEnd(nn.Module):
    """Inverse real DFT per frame, then the synthesis window. voxlock::Stream overlap-adds the frames and divides by
    the summed squared window, so an all-ones mask reproduces the input exactly (24 ms later)."""

    def __init__(self):
        super().__init__()
        n = torch.arange(N_FFT, dtype=torch.float64)
        k = torch.arange(BINS, dtype=torch.float64)
        angle = 2.0 * math.pi * k[:, None] * n[None, :] / N_FFT
        scale = torch.full((BINS, 1), 2.0, dtype=torch.float64)
        scale[0] = scale[-1] = 1.0
        w = hann_periodic(N_FFT)[None, :]
        self.register_buffer("inv_re", (scale * torch.cos(angle) / N_FFT * w).float())   # (BINS, N_FFT)
        self.register_buffer("inv_im", (-scale * torch.sin(angle) / N_FFT * w).float())

    def forward(self, re: torch.Tensor, im: torch.Tensor) -> torch.Tensor:
        return re @ self.inv_re + im @ self.inv_im  # (B, K, N_FFT), synthesis-windowed


class VoiceFilterLite(nn.Module):
    def __init__(self, n_mels: int = 128, hidden: int = 256, layers: int = 3, embedding_dim: int = EMBEDDING_DIM):
        super().__init__()
        self.layers = layers
        self.hidden = hidden
        self.stft = StftFrontEnd()
        self.istft = IstftBackEnd()
        self.register_buffer("mel", mel_filterbank(n_mels).float())
        self.feature_norm = nn.LayerNorm(n_mels)
        # Two projections instead of one on [features, voice print]: the voice print is a constant in the app, so
        # ONNX Runtime folds speaker_proj once per voice.
        self.feature_proj = nn.Linear(n_mels, hidden)
        self.speaker_proj = nn.Linear(embedding_dim, hidden)
        self.lstm = nn.LSTM(hidden, hidden, num_layers=layers, batch_first=True)
        self.mask_head = nn.Linear(hidden, BINS)
        self.strength_head = nn.Linear(hidden, 1)  # adaptive suppression strength (interference present?)

    def initial_state(self, batch: int = 1, device=None):
        z = torch.zeros(self.layers, batch, self.hidden, device=device)
        return z, z.clone()

    def forward(self, wav, dvector, h, c, return_parts: bool = False):
        re, im = self.stft(wav)
        power = re * re + im * im
        features = self.feature_norm(torch.log(power @ self.mel + 1e-6))
        x = F.relu(self.feature_proj(features) + self.speaker_proj(dvector)[:, None, :])
        y, (h_out, c_out) = self.lstm(x, (h, c))
        mask = torch.sigmoid(self.mask_head(y))
        strength = torch.sigmoid(self.strength_head(y))
        applied = strength * mask + (1.0 - strength)
        frames = self.istft(applied * re, applied * im)
        if return_parts:
            return frames, h_out, c_out, {"re": re, "im": im, "mask": applied, "strength": strength}
        return frames, h_out, c_out

    def make_identity(self):
        """Passes audio unchanged (mask = 1): for testing the app integration before a model is trained."""
        with torch.no_grad():
            self.mask_head.weight.zero_()
            self.mask_head.bias.fill_(12.0)
            self.strength_head.weight.zero_()
            self.strength_head.bias.fill_(-12.0)
        return self


def overlap_add(frames: torch.Tensor) -> torch.Tensor:
    """Reference reconstruction like voxlock::Stream (without its start padding): (B, K, N_FFT) -> (B, samples)."""
    batch, count, _ = frames.shape
    length = (count - 1) * HOP + N_FFT
    w2 = (hann_periodic(N_FFT) ** 2).float().to(frames.device)
    out = torch.zeros(batch, length, device=frames.device)
    env = torch.zeros(length, device=frames.device)
    for j in range(count):
        out[:, j * HOP: j * HOP + N_FFT] += frames[:, j]
        env[j * HOP: j * HOP + N_FFT] += w2
    return out / env.clamp(min=1e-8)
