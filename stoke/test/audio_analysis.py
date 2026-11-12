#!/usr/bin/env python3
"""Spectra and loudness-matched A/B plots from StokeTest WAVs."""
from __future__ import annotations
import math, sys
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import wave, struct

def read_wav(p: Path):
    with wave.open(str(p), "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(n)
        if w.getsampwidth() == 4:
            x = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
        elif w.getsampwidth() == 3:
            b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
            v = (b[:,0].astype(np.int32) | (b[:,1].astype(np.int32) << 8) | (b[:,2].astype(np.int32) << 16))
            v = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float64) / 8388608.0
            x = v
        else:
            x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
        if ch > 1:
            x = x.reshape(-1, ch).mean(axis=1)
        return sr, x

def spectrum(x, sr, nfft=8192):
    n = min(len(x), nfft * 8)
    x = x[:n] * np.hanning(n)
    # average of hops
    hop = nfft // 2
    acc = None
    k = 0
    for i in range(0, n - nfft + 1, hop):
        X = np.fft.rfft(x[i:i+nfft] * np.hanning(nfft))
        p = (X.real ** 2 + X.imag ** 2)
        acc = p if acc is None else acc + p
        k += 1
    p = acc / max(k, 1)
    f = np.fft.rfftfreq(nfft, 1 / sr)
    db = 10 * np.log10(np.maximum(p, 1e-20))
    return f, db

def plot_pair(dry, wet, sr, title, out, lo=40, hi=16000):
    fd, dd = spectrum(dry, sr)
    fw, dw = spectrum(wet, sr)
    fig, ax = plt.subplots(figsize=(8.2, 4.4), facecolor="#0c0c0c")
    ax.set_facecolor("#101010")
    ax.semilogx(fd, dd, color="#8a8a8a", lw=1.4, label="input")
    ax.semilogx(fw, dw, color="#e8e8e8", lw=1.6, label="STOKE")
    ax.set_xlim(lo, hi)
    ax.set_ylim(-90, 0)
    ax.set_xlabel("Hz")
    ax.set_ylabel("dB")
    ax.set_title(title, color="#e8e8e8")
    ax.tick_params(colors="#8a8a8a")
    for s in ax.spines.values():
        s.set_color("#3a3a3a")
    ax.legend(facecolor="#161616", edgecolor="#3a3a3a", labelcolor="#e8e8e8")
    ax.grid(True, which="both", color="#222", lw=0.5)
    fig.tight_layout()
    fig.savefig(out, dpi=120, facecolor=fig.get_facecolor())
    plt.close()
    print("wrote", out)

def main(folder):
    folder = Path(folder)
    img = folder / "img"
    img.mkdir(exist_ok=True)
    pairs = [
        ("dry-pink.wav", "default-pink.wav", "Default, pink noise"),
        ("dry-drums.wav", "default-drums.wav", "Default, drums"),
        ("dry-vocal.wav", "default-vocal.wav", "Default, vocal"),
        ("ab-dry-vocal.wav", "ab-wet-vocal.wav", "Vocal, loudness-matched"),
        ("ab-dry-synth.wav", "ab-wet-synth.wav", "Synth, loudness-matched"),
        ("ab-dry-drums.wav", "ab-wet-drums.wav", "Drums, loudness-matched"),
        ("ab-dry-mix.wav", "ab-wet-mix.wav", "Full mix, loudness-matched"),
    ]
    for a, b, title in pairs:
        pa, pb = folder / a, folder / b
        if not pa.exists() or not pb.exists():
            print("skip", a, b)
            continue
        sra, da = read_wav(pa)
        srb, db_ = read_wav(pb)
        plot_pair(da, db_, sra, title, img / (b.replace(".wav", ".png").replace("default-", "STOKE-").replace("ab-wet-", "STOKE-ab-")))

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
