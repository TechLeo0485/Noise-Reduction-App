"""The user's voice print exactly as VoiceStudio computes it (src/ai/voxlock/SpeakerCheck.cpp): CAM++ embeddings of
1.5 s pieces of speech (pauses cut out), averaged and normalised. The filter is trained on these, so it must see the
same numbers the app gives it at run time."""

import math
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort

SAMPLE_RATE = 16000
FBANK_FRAME = 400
FBANK_SHIFT = 160
FBANK_BINS = 80
PADDED = 512
SPECTRUM_BINS = PADDED // 2
PIECE = 3 * SAMPLE_RATE // 2
REFERENCE_PIECES = 40
CAMPP_FILE = "3dspeaker_speech_campplus_sv_en_voxceleb_16k.onnx"


def _mel(hz):
    return 1127.0 * np.log(1.0 + hz / 700.0)


def _tables():
    i = np.arange(FBANK_FRAME)
    window = (0.5 - 0.5 * np.cos(2 * math.pi * i / (FBANK_FRAME - 1))) ** 0.85
    lo, hi = _mel(20.0), _mel(SAMPLE_RATE / 2)
    step = (hi - lo) / (FBANK_BINS + 1)
    m = _mel(np.arange(SPECTRUM_BINS) * SAMPLE_RATE / PADDED)
    banks = np.zeros((FBANK_BINS, SPECTRUM_BINS))
    for b in range(FBANK_BINS):
        left = lo + b * step
        centre, right = left + step, left + 2 * step
        banks[b] = np.maximum(0.0, np.minimum((m - left) / (centre - left), (right - m) / (right - centre)))
    return window, banks


_WINDOW, _BANKS = _tables()


def fbank(audio: np.ndarray) -> np.ndarray:
    """Kaldi-compatible log mel filterbank, mean-normalised over time: (frames, 80). Audio in [-1, 1]."""
    if len(audio) < FBANK_FRAME:
        return np.zeros((0, FBANK_BINS), np.float32)
    frames = 1 + (len(audio) - FBANK_FRAME) // FBANK_SHIFT
    idx = np.arange(FBANK_FRAME)[None, :] + FBANK_SHIFT * np.arange(frames)[:, None]
    x = audio.astype(np.float64)[idx]
    x = x - x.mean(axis=1, keepdims=True)
    pre = x.copy()
    pre[:, 1:] -= 0.97 * x[:, :-1]
    pre[:, 0] -= 0.97 * x[:, 0]
    spec = np.fft.rfft(pre * _WINDOW, n=PADDED)[:, :SPECTRUM_BINS]
    power = np.abs(spec) ** 2
    out = np.log(np.maximum(power @ _BANKS.T, np.finfo(np.float32).eps))
    return (out - out.mean(axis=0, keepdims=True)).astype(np.float32)


def speech_only(audio: np.ndarray) -> np.ndarray:
    """10 ms blocks within 30 dB of the loud passages (the app's speechOnly())."""
    block = 160
    blocks = len(audio) // block
    if blocks == 0:
        return audio[:0]
    x = audio[: blocks * block].reshape(blocks, block).astype(np.float64)
    energy_db = 10 * np.log10((x * x).mean(axis=1) + 1e-12)
    threshold = max(np.partition(energy_db, blocks * 95 // 100)[blocks * 95 // 100] - 30.0, -50.0)
    return x[energy_db > threshold].reshape(-1).astype(np.float32)


class CamPlusPlus:
    """CAM++ with the app's ONNX Runtime fix (AveragePool count_include_pad = 0, see Runtime::createSpeakerModel())."""

    def __init__(self, model_path: Path, threads: int = 1):
        model = onnx.load(str(model_path))
        for node in model.graph.node:
            if node.op_type == "AveragePool":
                for attr in node.attribute:
                    if attr.name == "count_include_pad":
                        attr.i = 0
        options = ort.SessionOptions()
        options.intra_op_num_threads = threads
        self.session = ort.InferenceSession(model.SerializeToString(), options, providers=["CPUExecutionProvider"])
        self.input = self.session.get_inputs()[0].name

    def embed(self, audio: np.ndarray):
        features = fbank(audio)
        if len(features) < 10:
            return None
        e = self.session.run(None, {self.input: features[None]})[0].reshape(-1).astype(np.float64)
        n = np.linalg.norm(e)
        return (e / n).astype(np.float32) if n > 0 and np.all(np.isfinite(e)) else None

    def reference(self, recording: np.ndarray):
        """The voice print of a recording (the app's referenceEmbedding()), or None if it has < 1.5 s of speech."""
        speech = speech_only(recording)
        pieces = len(speech) // PIECE
        if pieces == 0:
            return None
        used = min(pieces, REFERENCE_PIECES)
        total, n = None, 0
        for i in range(used):
            p = i * pieces // used
            e = self.embed(speech[p * PIECE: (p + 1) * PIECE])
            if e is None:
                continue
            total = e.astype(np.float64) if total is None else total + e
            n += 1
        if n == 0:
            return None
        return (total / np.linalg.norm(total)).astype(np.float32)
