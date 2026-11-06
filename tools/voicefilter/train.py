"""Step 2: train the filter.

    python train.py --work work --noise-dir D:/data/noise --out runs/vfl --steps 200000 --device cuda

Loss (VoiceFilter-Lite, section 3.3): L2 between power-law compressed magnitudes (exponent 0.3), made *asymmetric*:
where the filter removed more of the user than it should (estimate below the clean target), the error counts
`--alpha` times. Plus a binary cross-entropy on the adaptive-suppression head: "is there interference in this frame?".

Validation every --eval-every steps on held-out speakers prints
  sdr_gain  - SI-SDR improvement on mixtures with the user speaking (dB, higher is better)
  other_db  - level of the output relative to the input when only someone else talks (dB, lower is better)
  target_db - level change of the user alone (dB, should stay near 0: "do no harm")
"""

import argparse
import math
import time
from pathlib import Path

import torch
import torch.nn.functional as F

from vfl.data import MixtureDataset
from vfl.model import HOP, N_FFT, VoiceFilterLite, overlap_add


def si_sdr(estimate: torch.Tensor, reference: torch.Tensor) -> torch.Tensor:
    estimate = estimate - estimate.mean(dim=-1, keepdim=True)
    reference = reference - reference.mean(dim=-1, keepdim=True)
    scale = (estimate * reference).sum(-1, keepdim=True) / (reference.pow(2).sum(-1, keepdim=True) + 1e-8)
    projection = scale * reference
    return 10 * torch.log10(projection.pow(2).sum(-1) / ((estimate - projection).pow(2).sum(-1) + 1e-8) + 1e-8)


def framed_length(samples: int) -> int:
    return ((samples - N_FFT) // HOP) * HOP + N_FFT


def losses(model, mixture, target, voice_print, alpha):
    h, c = model.initial_state(mixture.shape[0], mixture.device)
    frames, _, _, parts = model(mixture, voice_print, h, c, return_parts=True)
    mix_mag = torch.sqrt(parts["re"] ** 2 + parts["im"] ** 2 + 1e-10)
    t_re, t_im = model.stft(target)
    target_mag = torch.sqrt(t_re ** 2 + t_im ** 2 + 1e-10)
    estimate_c = (parts["mask"] * mix_mag).pow(0.3)
    target_c = target_mag.pow(0.3)
    diff = target_c - estimate_c  # > 0: the user was suppressed
    magnitude = torch.where(diff > 0, alpha * diff, diff).pow(2).mean()

    i_re, i_im = model.stft(mixture - target)
    interference = (i_re ** 2 + i_im ** 2).sum(-1)
    speech = (t_re ** 2 + t_im ** 2).sum(-1)
    label = (interference > 0.05 * speech + 1e-7).float()
    strength = F.binary_cross_entropy(parts["strength"].squeeze(-1).clamp(1e-6, 1 - 1e-6), label)
    return magnitude, strength, frames


@torch.no_grad()
def evaluate(model, dataset, batches, batch_size, device):
    model.eval()
    gains, other, alone = [], [], []
    for _ in range(batches):
        mixture, target, voice_print = (torch.stack(x).to(device) for x in zip(*[dataset.sample(dataset.rng) for _ in range(batch_size)]))
        h, c = model.initial_state(mixture.shape[0], device)
        frames, _, _ = model(mixture, voice_print, h, c)
        output = overlap_add(frames)
        n = output.shape[-1]
        mix, ref = mixture[:, :n], target[:, :n]
        for i in range(mixture.shape[0]):
            t_energy = ref[i].pow(2).mean()
            interference = (mix[i] - ref[i]).pow(2).mean()
            if t_energy < 1e-9:  # only someone else
                other.append(10 * math.log10((output[i].pow(2).mean() + 1e-12) / (mix[i].pow(2).mean() + 1e-12)))
            elif interference < 1e-6 * t_energy:  # the user alone
                alone.append(10 * math.log10((output[i].pow(2).mean() + 1e-12) / (t_energy + 1e-12)))
            else:
                gains.append(float(si_sdr(output[i], ref[i]) - si_sdr(mix[i], ref[i])))
    model.train()
    mean = lambda v: sum(v) / len(v) if v else float("nan")
    return mean(gains), mean(other), mean(alone)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", default=Path("work"), type=Path, help="folder written by prepare.py")
    ap.add_argument("--noise-dir", type=Path, help="optional real noise recordings (e.g. MUSAN noise, DNS noise)")
    ap.add_argument("--out", default=Path("runs/vfl"), type=Path)
    ap.add_argument("--steps", default=200000, type=int)
    ap.add_argument("--batch", default=32, type=int)
    ap.add_argument("--lr", default=1e-3, type=float)
    ap.add_argument("--alpha", default=4.0, type=float, help="weight of over-suppression in the asymmetric loss")
    ap.add_argument("--seconds", default=3.0, type=float)
    ap.add_argument("--hidden", default=256, type=int, help="LSTM size (the paper's 2.2 MB model: 256)")
    ap.add_argument("--workers", default=4, type=int)
    ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    ap.add_argument("--eval-every", default=2000, type=int)
    ap.add_argument("--resume", type=Path)
    args = ap.parse_args()

    device = torch.device(args.device)
    model = VoiceFilterLite(hidden=args.hidden).to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.steps, eta_min=args.lr * 0.05)
    step = 0
    if args.resume:
        state = torch.load(args.resume, map_location=device)
        model.load_state_dict(state["model"])
        optimizer.load_state_dict(state["optimizer"])
        scheduler.load_state_dict(state["scheduler"])
        step = state["step"]

    train_set = MixtureDataset(args.work / "index.json", args.work / "prints.npz", args.noise_dir, args.seconds, "train")
    valid_set = MixtureDataset(args.work / "index.json", args.work / "prints.npz", args.noise_dir, args.seconds, "validation",
                               seed=99)
    import random
    valid_set.rng = random.Random(99)
    loader = torch.utils.data.DataLoader(train_set, batch_size=args.batch, num_workers=args.workers,
                                         persistent_workers=args.workers > 0)
    args.out.mkdir(parents=True, exist_ok=True)
    print(f"training on {device}, {sum(p.numel() for p in model.parameters()) / 1e6:.2f} M parameters")

    started = time.time()
    for mixture, target, voice_print in loader:
        if step >= args.steps:
            break
        mixture, target, voice_print = mixture.to(device), target.to(device), voice_print.to(device)
        magnitude, strength, _ = losses(model, mixture, target, voice_print, args.alpha)
        loss = magnitude + 0.1 * strength
        optimizer.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
        optimizer.step()
        scheduler.step()
        step += 1
        if step % 100 == 0:
            print(f"step {step}  loss {magnitude.item():.4f}  strength {strength.item():.3f}  "
                  f"{(time.time() - started) / step:.2f} s/step", flush=True)
        if step % args.eval_every == 0 or step == args.steps:
            gain, other, alone = evaluate(model, valid_set, 8, min(args.batch, 16), device)
            print(f"eval step {step}: sdr_gain {gain:+.2f} dB  other_db {other:+.1f}  target_db {alone:+.2f}", flush=True)
            torch.save({"model": model.state_dict(), "optimizer": optimizer.state_dict(),
                        "scheduler": scheduler.state_dict(), "step": step, "config": {"hidden": args.hidden}},
                       args.out / "last.pt")
    print(f"done: {args.out / 'last.pt'}")


if __name__ == "__main__":
    main()
