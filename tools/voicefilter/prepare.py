"""Step 1: index a speech corpus and compute each speaker's voice prints (as VoiceStudio does).

    python prepare.py --speech-dir D:/data/LibriSpeech/train-clean-360 --campp ../../build/ninja-release/bin/models/voxlock --out work

Speakers are the first-level folders of --speech-dir (LibriSpeech, VCTK, VoxCeleb layouts all work). Per speaker,
some utterances build the voice prints (references) and the others are the training targets, so the filter never
sees a voice print made from the very utterance it is cleaning. A few speakers are held out for validation.
Writes work/index.json and work/prints.npz (several voice prints per speaker, from different utterances).
"""

import argparse
import json
import random
from pathlib import Path

import numpy as np

from vfl.data import AUDIO_EXTENSIONS, load_audio
from vfl.speaker import CAMPP_FILE, CamPlusPlus


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--speech-dir", required=True, type=Path)
    ap.add_argument("--campp", required=True, type=Path, help="folder with " + CAMPP_FILE + " (the app's models/voxlock)")
    ap.add_argument("--out", default=Path("work"), type=Path)
    ap.add_argument("--prints-per-speaker", default=4, type=int)
    ap.add_argument("--reference-seconds", default=20.0, type=float, help="speech per voice print (the app: the user's recording)")
    ap.add_argument("--validation-speakers", default=20, type=int)
    ap.add_argument("--max-speakers", default=0, type=int, help="0 = all")
    args = ap.parse_args()

    rng = random.Random(1234)
    speakers = []
    for folder in sorted(p for p in args.speech_dir.iterdir() if p.is_dir()):
        files = sorted(str(p) for p in folder.rglob("*") if p.suffix.lower() in AUDIO_EXTENSIONS)
        if len(files) >= 4:
            speakers.append((folder.name, files))
    if args.max_speakers:
        speakers = speakers[: args.max_speakers]
    if len(speakers) < 3:
        raise SystemExit(f"found only {len(speakers)} speakers with 4+ files in {args.speech_dir}")
    print(f"{len(speakers)} speakers")

    campp = CamPlusPlus(args.campp / CAMPP_FILE, threads=4)
    validation = set(name for name, _ in rng.sample(speakers, min(args.validation_speakers, len(speakers) // 5 or 1)))
    index, prints = [], {}
    for i, (name, files) in enumerate(speakers):
        rng.shuffle(files)
        split_at = max(1, len(files) // 3)
        references, targets = files[:split_at], files[split_at:]
        vectors = []
        for p in range(args.prints_per_speaker):
            pool = references[:]
            rng.shuffle(pool)
            audio, seconds = [], 0.0
            for f in pool:
                x = load_audio(Path(f))
                audio.append(x)
                seconds += len(x) / 16000
                if seconds >= args.reference_seconds:
                    break
            v = campp.reference(np.concatenate(audio))
            if v is not None:
                vectors.append(v)
        if not vectors:
            print(f"  skipped {name}: no usable speech")
            continue
        prints[name] = np.stack(vectors)
        index.append({"id": name, "split": "validation" if name in validation else "train",
                      "references": references, "targets": targets})
        if (i + 1) % 20 == 0:
            print(f"  {i + 1}/{len(speakers)}")

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "index.json").write_text(json.dumps({"speakers": index}, indent=1), encoding="utf-8")
    np.savez(args.out / "prints.npz", **prints)
    print(f"wrote {args.out / 'index.json'} and {args.out / 'prints.npz'} ({len(index)} speakers)")


if __name__ == "__main__":
    main()
