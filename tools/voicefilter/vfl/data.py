"""Training mixtures: a target speaker (with their voice print), other speakers, noise and clicks.

Scenarios follow how VoiceStudio is used, including the cases the paper's ASR setting did not care about:
  * overlap      - the user and someone else talk at the same time
  * target only  - the user alone (the filter must leave them alone: "do no harm")
  * other only   - the user is silent and someone else talks (must be removed completely)
  * turns        - one talks, then the other
Noise is added to half of the examples, and clicks (mouse, keyboard) to many: the target is always the clean voice,
so the filter also learns to drop transients that land on the user's speech.
"""

import json
import math
import random
from pathlib import Path

import numpy as np
import soundfile as sf
import torch
from scipy.signal import lfilter, resample_poly

SAMPLE_RATE = 16000
AUDIO_EXTENSIONS = {".wav", ".flac", ".ogg", ".mp3"}


def load_audio(path: Path) -> np.ndarray:
    audio, rate = sf.read(str(path), dtype="float32", always_2d=True)
    audio = audio.mean(axis=1)
    if rate != SAMPLE_RATE:
        g = math.gcd(rate, SAMPLE_RATE)
        audio = resample_poly(audio, SAMPLE_RATE // g, rate // g).astype(np.float32)
    return audio


def rms_db(x: np.ndarray) -> float:
    return 10 * math.log10(float(np.mean(x.astype(np.float64) ** 2)) + 1e-12)


def set_level(x: np.ndarray, db: float) -> np.ndarray:
    return x * (10 ** ((db - rms_db(x)) / 20)) if np.any(x) else x


def random_crop(x: np.ndarray, length: int, rng: random.Random) -> np.ndarray:
    if len(x) >= length:
        start = rng.randrange(0, len(x) - length + 1)
        return x[start: start + length].copy()
    out = np.zeros(length, np.float32)
    start = rng.randrange(0, length - len(x) + 1)
    out[start: start + len(x)] = x
    return out


def resonator(freq: float, q: float):
    """Biquad band-pass coefficients (RBJ)."""
    w = 2 * math.pi * freq / SAMPLE_RATE
    alpha = math.sin(w) / (2 * q)
    b = [alpha, 0.0, -alpha]
    a = [1 + alpha, -2 * math.cos(w), 1 - alpha]
    return np.array(b) / a[0], np.array(a) / a[0]


def click(rng: random.Random) -> np.ndarray:
    """One mouse/keyboard click: a decaying broadband burst with a plastic resonance and a soft thump."""
    n = int(SAMPLE_RATE * rng.uniform(0.008, 0.03))
    t = np.arange(n) / SAMPLE_RATE
    burst = np.random.default_rng(rng.randrange(1 << 30)).standard_normal(n) * np.exp(-t / rng.uniform(0.0008, 0.004))
    b, a = resonator(rng.uniform(1800, 6500), rng.uniform(1.5, 6))
    body = lfilter(b, a, burst)
    b2, a2 = resonator(rng.uniform(150, 500), 1.0)
    thump = lfilter(b2, a2, burst) * rng.uniform(0.0, 0.6)
    x = body + thump + burst * rng.uniform(0.05, 0.3)
    return (x / (np.abs(x).max() + 1e-9)).astype(np.float32)


def add_clicks(length: int, rng: random.Random) -> np.ndarray:
    """Mouse clicks (press + release) or typing, at a random peak level."""
    out = np.zeros(length, np.float32)
    typing = rng.random() < 0.5
    pos = rng.randrange(0, SAMPLE_RATE // 2)
    while pos < length:
        c = click(rng) * (10 ** (rng.uniform(-28, -6) / 20))
        end = min(length, pos + len(c))
        out[pos:end] += c[: end - pos]
        p2 = pos + int(rng.uniform(0.06, 0.14) * SAMPLE_RATE)  # mouse release ~60-140 ms later
        if not typing and p2 < length:
            r = click(rng) * (10 ** (rng.uniform(-32, -10) / 20))
            e2 = min(length, p2 + len(r))
            out[p2:e2] += r[: e2 - p2]
        pos += int(SAMPLE_RATE * (rng.uniform(0.08, 0.25) if typing else rng.uniform(0.4, 1.5)))
    return out


def colored_noise(length: int, rng: random.Random) -> np.ndarray:
    white = np.random.default_rng(rng.randrange(1 << 30)).standard_normal(length)
    alpha = rng.uniform(0.0, 0.98)  # white .. brown-ish (fans, hum-like rumble)
    return lfilter([1.0], [1.0, -alpha], white).astype(np.float32)


class MixtureDataset(torch.utils.data.IterableDataset):
    def __init__(self, index_path: Path, prints_path: Path, noise_dir: Path | None = None, seconds: float = 3.0,
                 split: str = "train", seed: int = 0):
        index = json.loads(Path(index_path).read_text(encoding="utf-8"))
        prints = np.load(prints_path)
        usable = [s for s in index["speakers"] if s["id"] in prints]
        # Targets come from this split; the other voices from any speaker (they are never the one being kept).
        self.speakers = [s for s in usable if s["split"] == split]
        self.others = usable
        if not self.speakers or len(usable) < 2:
            raise ValueError(f"need a speaker in split '{split}' and 2+ speakers in total, found "
                             f"{len(self.speakers)} and {len(usable)}")
        self.prints = {s["id"]: prints[s["id"]] for s in self.speakers}
        self.noise_files = sorted(p for p in Path(noise_dir).rglob("*") if p.suffix.lower() in AUDIO_EXTENSIONS) \
            if noise_dir else []
        self.length = int(seconds * SAMPLE_RATE)
        self.seed = seed
        self.cache: dict[str, np.ndarray] = {}

    def _audio(self, path: str) -> np.ndarray:
        if path not in self.cache:
            if len(self.cache) > 2000:
                self.cache.clear()
            self.cache[path] = load_audio(Path(path))
        return self.cache[path]

    def __iter__(self):
        info = torch.utils.data.get_worker_info()
        rng = random.Random(self.seed + (info.id if info else 0) * 7919 + random.randrange(1 << 20))
        while True:
            yield self.sample(rng)

    def sample(self, rng: random.Random):
        n = self.length
        target_speaker = rng.choice(self.speakers)
        other_speaker = rng.choice([s for s in self.others if s["id"] != target_speaker["id"]])
        target = random_crop(self._audio(rng.choice(target_speaker["targets"])), n, rng)
        other = random_crop(self._audio(rng.choice(other_speaker["targets"] + other_speaker["references"])), n, rng)
        target = set_level(target, rng.uniform(-30, -18))

        scenario = rng.choices(["overlap", "target", "other", "turns"], weights=[0.35, 0.2, 0.2, 0.25])[0]
        if scenario == "target":
            other[:] = 0
        elif scenario == "other":
            target[:] = 0
        elif scenario == "turns":
            cut = rng.randrange(n // 4, 3 * n // 4)
            if rng.random() < 0.5:
                target[cut:] = 0
                other[:cut] = 0
            else:
                other[cut:] = 0
                target[:cut] = 0
        if np.any(other):
            reference_db = rms_db(target) if np.any(target) else rng.uniform(-30, -18)
            other = set_level(other, reference_db - rng.uniform(-5, 12))

        mixture = target + other
        if rng.random() < 0.5:
            noise = random_crop(self._audio(str(rng.choice(self.noise_files))), n, rng) if self.noise_files \
                and rng.random() < 0.7 else colored_noise(n, rng)
            level = rms_db(mixture) if np.any(mixture) else -30.0
            mixture = mixture + set_level(noise, level - rng.uniform(0, 25))
        if rng.random() < 0.5:
            mixture = mixture + add_clicks(n, rng)

        peak = float(np.abs(mixture).max())
        if peak > 0.99:
            mixture, target = mixture * (0.99 / peak), target * (0.99 / peak)
        voice_print = self.prints[target_speaker["id"]]
        voice_print = voice_print[rng.randrange(len(voice_print))]
        return (torch.from_numpy(mixture.astype(np.float32)), torch.from_numpy(target.astype(np.float32)),
                torch.from_numpy(voice_print.astype(np.float32)))
