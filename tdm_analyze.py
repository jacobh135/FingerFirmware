#!/usr/bin/env python3
"""
tdm_analyze.py — turn a raw TDM audio-accelerometer capture into something you
can listen to AND look at:

    1. capture.wav        — the audio (same conversion as tdm_to_wav.py)
    2. capture_plot.png   — waveform (top) + spectrogram (bottom)
    3. capture_summary.txt — peak amplitude, RMS, dominant frequency, duration

INPUT FORMAT: one sample per line, "x y z" (whitespace or comma separated),
same as tdm_to_wav.py expects — this is what the firmware's streaming dump
mode should emit.

USAGE:
    python3 tdm_analyze.py capture.txt --axis z --rate 15625 --out results/

Requires numpy + matplotlib (pip install numpy matplotlib --break-system-packages).
"""

import argparse
import os
import struct
import sys
import wave

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def parse_line(line):
    parts = line.replace(",", " ").split()
    if len(parts) < 3:
        return None
    try:
        return tuple(int(p) for p in parts[:3])
    except ValueError:
        return None


def load_samples(path):
    xs, ys, zs = [], [], []
    skipped = 0
    with open(path, "r", errors="ignore") as f:
        for line in f:
            parsed = parse_line(line)
            if parsed is None:
                skipped += 1
                continue
            x, y, z = parsed
            xs.append(x)
            ys.append(y)
            zs.append(z)
    if skipped:
        print(f"[info] skipped {skipped} non-data line(s)", file=sys.stderr)
    if not xs:
        sys.exit("[error] no parseable samples found - check input format")
    return np.array(xs, dtype=np.float64), np.array(ys, dtype=np.float64), np.array(zs, dtype=np.float64)


def select_channel(xs, ys, zs, axis):
    if axis == "x":
        return xs
    if axis == "y":
        return ys
    if axis == "z":
        return zs
    if axis == "avg":
        return (xs + ys + zs) / 3.0
    if axis == "mag":
        return np.sqrt(xs ** 2 + ys ** 2 + zs ** 2)
    raise ValueError(axis)


def high_pass(samples, alpha=0.995):
    """One-pole DC blocker: removes the large constant gravity/offset term so
    what's left is the AC vibration content."""
    out = np.zeros_like(samples)
    prev_x = samples[0]
    prev_y = 0.0
    for i, x in enumerate(samples):
        y = x - prev_x + alpha * prev_y
        out[i] = y
        prev_x = x
        prev_y = y
    return out


def normalize_to_int16(samples, headroom=0.95):
    peak = np.max(np.abs(samples)) or 1.0
    scale = (32767 * headroom) / peak
    return np.clip(np.round(samples * scale), -32768, 32767).astype(np.int16)


def write_wav(path, samples_i16, sample_rate):
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(struct.pack("<%dh" % len(samples_i16), *samples_i16))


def make_plot(path, raw, filtered, sample_rate, axis_label):
    t = np.arange(len(filtered)) / sample_rate

    fig, (ax_wave, ax_spec) = plt.subplots(2, 1, figsize=(11, 7), height_ratios=[1, 1.4])

    ax_wave.plot(t, filtered, linewidth=0.5, color="#2b6cb0")
    ax_wave.set_title(f"TDM audio accelerometer - axis {axis_label} (DC-removed)")
    ax_wave.set_xlabel("time (s)")
    ax_wave.set_ylabel("amplitude (counts)")
    ax_wave.set_xlim(t[0], t[-1])

    nfft = 512
    noverlap = nfft - 64
    if len(filtered) > nfft:
        ax_spec.specgram(filtered, NFFT=nfft, Fs=sample_rate, noverlap=noverlap,
                          cmap="magma")
        ax_spec.set_title("Spectrogram")
        ax_spec.set_xlabel("time (s)")
        ax_spec.set_ylabel("frequency (Hz)")
        ax_spec.set_ylim(0, sample_rate / 2)
    else:
        ax_spec.text(0.5, 0.5, "capture too short for a spectrogram",
                     ha="center", va="center")

    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def write_summary(path, raw, filtered, sample_rate, axis_label, n_samples):
    duration = n_samples / sample_rate
    peak = np.max(np.abs(filtered))
    rms = np.sqrt(np.mean(filtered ** 2))
    dc_offset = np.mean(raw)

    # Dominant frequency via FFT magnitude peak (skip the DC bin).
    spectrum = np.abs(np.fft.rfft(filtered))
    freqs = np.fft.rfftfreq(len(filtered), d=1.0 / sample_rate)
    spectrum[0] = 0.0
    dominant_idx = np.argmax(spectrum)
    dominant_freq = freqs[dominant_idx]

    lines = [
        f"TDM audio accelerometer capture summary",
        f"-----------------------------------------",
        f"axis analyzed       : {axis_label}",
        f"sample rate         : {sample_rate} Hz",
        f"samples             : {n_samples}",
        f"duration            : {duration:.3f} s",
        f"raw DC offset       : {dc_offset:.1f} counts",
        f"peak amplitude (AC) : {peak:.1f} counts",
        f"RMS amplitude (AC)  : {rms:.1f} counts",
        f"dominant frequency  : {dominant_freq:.1f} Hz",
        f"",
        f"Notes:",
        f"- 'raw DC offset' is mostly gravity + sensor bias on this axis, removed",
        f"  before computing peak/RMS/dominant frequency.",
        f"- dominant frequency is the single strongest FFT bin; for tap/voice",
        f"  content look at the spectrogram image for the full picture, a single",
        f"  number won't capture a changing signal.",
    ]
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="path to the raw-stream capture (text file)")
    ap.add_argument("--axis", choices=["x", "y", "z", "avg", "mag"], default="z")
    ap.add_argument("--rate", type=int, default=15625)
    ap.add_argument("--hpf-alpha", type=float, default=0.995)
    ap.add_argument("--out", default=".", help="output directory (default: current dir)")
    ap.add_argument("--basename", default="capture",
                     help="prefix for output files (default: capture)")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    xs, ys, zs = load_samples(args.input)
    n = len(xs)
    print(f"[info] loaded {n} samples ({n / args.rate:.2f} s at {args.rate} Hz)",
          file=sys.stderr)

    raw = select_channel(xs, ys, zs, args.axis)
    filtered = high_pass(raw, alpha=args.hpf_alpha)

    wav_path = os.path.join(args.out, f"{args.basename}.wav")
    plot_path = os.path.join(args.out, f"{args.basename}_plot.png")
    summary_path = os.path.join(args.out, f"{args.basename}_summary.txt")

    write_wav(wav_path, normalize_to_int16(filtered), args.rate)
    make_plot(plot_path, raw, filtered, args.rate, args.axis)
    summary_text = write_summary(summary_path, raw, filtered, args.rate, args.axis, n)

    print(f"[done] wrote {wav_path}", file=sys.stderr)
    print(f"[done] wrote {plot_path}", file=sys.stderr)
    print(f"[done] wrote {summary_path}", file=sys.stderr)
    print(file=sys.stderr)
    print(summary_text, file=sys.stderr)


if __name__ == "__main__":
    main()
