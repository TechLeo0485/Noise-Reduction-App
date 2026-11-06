"""Step 3: export the trained filter for VoiceStudio and check it.

    python export.py --checkpoint runs/vfl/last.pt --out ../../models/voicefilter/voicefilter_lite.onnx [--int8]
    python export.py --identity --out <path>      # pass-through model, to test the app integration without training

The ONNX graph follows VoiceStudio's streaming contract (Runtime::createVoiceFilterModel()):
    wav (1, S), dvector (1, 512), h (L, 1, H), c (L, 1, H)  ->  frames (1, K, 512), h_out, c_out
with S = (K-1)*128 + 512 and the dimension names S and K (the app fixes them per chunk size). The checks run the
graph the way the app does - chunk by chunk with the state carried - and compare with PyTorch on the whole signal.
"""

import argparse
from pathlib import Path

import numpy as np
import onnxruntime as ort
import torch

from vfl.model import EMBEDDING_DIM, HOP, N_FFT, VoiceFilterLite, overlap_add


def export(model: VoiceFilterLite, out: Path):
    model.eval()
    k = 4
    wav = torch.randn(1, (k - 1) * HOP + N_FFT) * 0.05
    dvector = torch.nn.functional.normalize(torch.randn(1, EMBEDDING_DIM), dim=-1)
    h, c = model.initial_state(1)
    out.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(model, (wav, dvector, h, c), str(out), input_names=["wav", "dvector", "h", "c"],
                      output_names=["frames", "h_out", "c_out"], dynamic_axes={"wav": {1: "S"}, "frames": {1: "K"}},
                      opset_version=17, dynamo=False)


def check(model: VoiceFilterLite, path: Path, identity: bool, tolerance: float):
    torch.manual_seed(0)
    k_total = 200  # 1.6 s
    wav = torch.randn(1, (k_total - 1) * HOP + N_FFT) * 0.05
    dvector = torch.nn.functional.normalize(torch.randn(1, EMBEDDING_DIM), dim=-1)
    with torch.no_grad():
        h, c = model.initial_state(1)
        reference, _, _ = model(wav, dvector, h, c)
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    for k in (2, 3, 8):
        h_np, c_np = (x.numpy() for x in model.initial_state(1))
        frames = []
        for start in range(0, k_total - k + 1, k):
            chunk = wav[:, start * HOP: start * HOP + (k - 1) * HOP + N_FFT].numpy()
            f, h_np, c_np = session.run(None, {"wav": chunk, "dvector": dvector.numpy(), "h": h_np, "c": c_np})
            frames.append(f)
        frames = np.concatenate(frames, axis=1)
        diff = float(np.abs(frames - reference[:, : frames.shape[1]].numpy()).max())
        print(f"  streaming K={k}: max difference to PyTorch {diff:.2e}")
        if diff > tolerance:
            raise SystemExit(f"streaming check failed (> {tolerance})")
    if identity:
        rebuilt = overlap_add(reference)[0, N_FFT:-N_FFT]
        diff = float((rebuilt - wav[0, N_FFT: N_FFT + rebuilt.shape[0]]).abs().max())
        print(f"  identity: reconstruction error {diff:.2e}")
        if diff > 1e-4:
            raise SystemExit("identity check failed")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--checkpoint", type=Path)
    ap.add_argument("--identity", action="store_true", help="export a pass-through model (testing only)")
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--int8", action="store_true", help="also quantize the weights to 8-bit (as in the paper)")
    args = ap.parse_args()
    if not args.identity and not args.checkpoint:
        raise SystemExit("give --checkpoint or --identity")

    if args.identity:
        model = VoiceFilterLite().make_identity()
    else:
        state = torch.load(args.checkpoint, map_location="cpu")
        model = VoiceFilterLite(hidden=state.get("config", {}).get("hidden", 256))
        model.load_state_dict(state["model"])
    model.eval()

    export(model, args.out)
    print(f"exported {args.out} ({args.out.stat().st_size / 1e6:.1f} MB)")
    check(model, args.out, args.identity, 1e-3)
    if args.int8:
        from onnxruntime.quantization import QuantType, quantize_dynamic
        float_path = args.out.with_suffix(".fp32.onnx")
        args.out.replace(float_path)
        quantize_dynamic(str(float_path), str(args.out), weight_type=QuantType.QInt8)
        float_path.unlink()
        print(f"quantized {args.out} ({args.out.stat().st_size / 1e6:.1f} MB)")
        check(model, args.out, False, 0.05)
    print("ok - copy it to models/voicefilter/voicefilter_lite.onnx and rebuild (or next to the app: "
          "<app>/models/voicefilter/voicefilter_lite.onnx)")


if __name__ == "__main__":
    main()
