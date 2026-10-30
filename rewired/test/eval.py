#!/usr/bin/env python3
"""Score RewiredCli against the synthetic songs (test/make_songs.py) with mir_eval.
usage: eval.py CLI MODELDIR SONGSDIR [extra cli args...]
Pitched parts: mir_eval.transcription onset-only P/R/F1 (50 ms onset tolerance, pitch within 50 cents).
Drum parts: mir_eval.onset P/R/F1 (50 ms window). Prints per-part tables for song mode and stems mode."""
import sys, os, subprocess, json, numpy as np, pretty_midi, mir_eval
cli, models, songs = sys.argv[1:4]; extra = sys.argv[4:]
PARTS = ["Bass", "Melody", "Chords", "Kick", "Snare", "Hats", "Cymbals"]
def tracks(path):
    pm = pretty_midi.PrettyMIDI(path); out = {}
    for ins in pm.instruments:
        n = ins.name.strip().split(" (")[0].split(" - ")[0]   # 1.0.1: "Chords - Strings" / "Melody (Horns)" are instrument splits of one part
        a = np.array([[x.start, x.end, x.pitch] for x in ins.notes]).reshape(-1, 3)
        out[n] = np.concatenate([out[n], a]) if n in out else a
    return out
def score(ref, est, drum):
    if len(ref) == 0 and len(est) == 0: return (1, 1, 1)
    if len(ref) == 0 or len(est) == 0: return (0, 0, 0)
    if drum:
        f, p, r = mir_eval.onset.f_measure(np.unique(ref[:, 0]), np.unique(est[:, 0]), window=0.05); return (p, r, f)
    ri = np.maximum(ref[:, :2], 0); ri[:, 1] = np.maximum(ri[:, 1], ri[:, 0] + 1e-3)
    ei = est[:, :2].copy(); ei[:, 1] = np.maximum(ei[:, 1], ei[:, 0] + 1e-3)
    p, r, f, _ = mir_eval.transcription.precision_recall_f1_overlap(ri, pretty_midi.note_number_to_hz(ref[:, 2]), ei,
        pretty_midi.note_number_to_hz(est[:, 2]), onset_tolerance=0.05, offset_ratio=None)
    return (p, r, f)
res = {}
for mode in ["song", "stems"]:
    tot = {p: [] for p in PARTS}
    for s in sorted(os.listdir(songs)):
        d = os.path.join(songs, s); out = f"/tmp/rw-eval-{s}-{mode}.mid"
        args = [cli, "--models", models, "--out", out] + extra
        args += ["--song", f"{d}/mix.wav"] if mode == "song" else sum([[f"--{k}", f"{d}/stems/{k}.wav"] for k in ["bass", "melody", "chords", "drums"]], [])
        subprocess.run(args, check=True, capture_output=True)
        ref, est = tracks(f"{d}/truth.mid"), tracks(out)
        for p in PARTS: tot[p].append(score(ref.get(p, np.zeros((0, 3))), est.get(p, np.zeros((0, 3))), p in PARTS[3:]))
    print(f"\n{mode} mode  (mean over {len(tot['Bass'])} songs)\n  part       P     R     F1")
    res[mode] = {}
    for p in PARTS:
        m = np.mean(tot[p], axis=0); res[mode][p] = [round(float(x), 3) for x in m]
        print(f"  {p:8s} {m[0]:.3f} {m[1]:.3f} {m[2]:.3f}")
    fs = [res[mode][p][2] for p in PARTS]; print(f"  {'mean':8s}             {np.mean(fs):.3f}"); res[mode]["mean_f1"] = round(float(np.mean(fs)), 3)
json.dump(res, open("/tmp/rw-eval.json", "w"), indent=1)
