"""Quick analysis of an acid3 render: per-step peak/RMS level, spectral centroid and pitch.

usage: .venv/bin/python tools/analysis/analyze.py renders/x.wav --bpm 130 [--steps 16]
"""
import argparse

import numpy as np
from scipy.io import wavfile


def yin_pitch(x, sr, fmin=25, fmax=1000):
    """Very small YIN-style f0 estimate for one frame."""
    x = x - np.mean(x)
    n = len(x) // 2
    tau_min, tau_max = int(sr / fmax), min(int(sr / fmin), n - 1)
    d = np.array([np.sum((x[:n] - x[t:t + n]) ** 2) for t in range(tau_max + 1)])
    cmnd = np.ones_like(d)
    cmnd[1:] = d[1:] * np.arange(1, len(d)) / np.maximum(np.cumsum(d[1:]), 1e-12)
    for t in range(tau_min, tau_max):
        if cmnd[t] < 0.15 and cmnd[t] <= cmnd[t + 1]:
            return sr / t
    t = tau_min + int(np.argmin(cmnd[tau_min:tau_max]))
    return sr / t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--bpm", type=float, default=130)
    ap.add_argument("--steps", type=int, default=16)
    a = ap.parse_args()
    sr, x = wavfile.read(a.wav)
    x = x.astype(np.float64)
    step = int(sr * 60 / a.bpm / 4)
    print(f"sr={sr} len={len(x)/sr:.2f}s peak={np.max(np.abs(x)):.3f} rms={np.sqrt(np.mean(x**2)):.4f}")
    print("step  peak    rms_dB  centroid_Hz  f0_Hz")
    for i in range(a.steps):
        seg = x[i * step:(i + 1) * step]
        if len(seg) < step:
            break
        rms = np.sqrt(np.mean(seg ** 2))
        spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
        f = np.fft.rfftfreq(len(seg), 1 / sr)
        cen = np.sum(f * spec) / max(np.sum(spec), 1e-12)
        f0 = yin_pitch(seg[: min(len(seg), 4096)], sr) if rms > 1e-4 else 0
        print(f"{i:4d}  {np.max(np.abs(seg)):.3f}  {20*np.log10(max(rms,1e-9)):7.1f}  {cen:10.0f}  {f0:7.1f}")


if __name__ == "__main__":
    main()
