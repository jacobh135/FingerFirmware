#!/usr/bin/env python3
"""
plot_dataset.py — turn texture_dataset.csv (labeled feature windows from
log_texture.py) into readable, visual output:

    1. A printed per-surface summary table (mean / std of each feature).
    2. A scatter matrix of the most informative feature pairs, colored by
       surface label -- this is the "do the surfaces actually separate?" view.
    3. Per-feature histograms by surface.

If the colored clusters are visibly separated, Phase 1 time-domain features are
enough and an off-board classifier will work well. If they overlap heavily,
that's the empirical signal that Phase 2 (FFT / spectral features) is worth it.

USAGE:
    python3 plot_dataset.py                       # reads texture_dataset.csv
    python3 plot_dataset.py --csv mydata.csv --out plots/

Requires numpy + matplotlib.
"""

import argparse
import csv
import os
import sys
from collections import defaultdict

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

FEATURES = ["rms", "mad", "peak", "zcr", "crest"]


def load(csv_path):
    by_label = defaultdict(lambda: defaultdict(list))
    with open(csv_path, newline="") as f:
        reader = csv.DictReader(f)
        missing = [c for c in (["label"] + FEATURES) if c not in reader.fieldnames]
        if missing:
            sys.exit(f"[error] CSV missing columns: {missing}. "
                      f"Found: {reader.fieldnames}")
        n = 0
        for row in reader:
            label = row["label"]
            for feat in FEATURES:
                by_label[label][feat].append(float(row[feat]))
            n += 1
    if n == 0:
        sys.exit("[error] no rows in CSV. Did logging capture anything?")
    # convert to arrays
    for label in by_label:
        for feat in FEATURES:
            by_label[label][feat] = np.array(by_label[label][feat])
    return by_label


def print_summary(by_label):
    print()
    print("Per-surface feature summary (mean +/- std over all windows)")
    print("=" * 78)
    header = f"{'surface':<20} {'n':>5}  " + "  ".join(f"{f:>12}" for f in FEATURES)
    print(header)
    print("-" * 78)
    for label in sorted(by_label):
        d = by_label[label]
        n = len(d[FEATURES[0]])
        cells = []
        for feat in FEATURES:
            m = d[feat].mean()
            s = d[feat].std()
            cells.append(f"{m:6.0f}+/-{s:<4.0f}")
        print(f"{label:<20} {n:>5}  " + "  ".join(f"{c:>12}" for c in cells))
    print("=" * 78)
    print()


def scatter_pairs(by_label, out_dir):
    # The two most intuitive separators for texture: rms (intensity) vs zcr
    # (frequency proxy). Also rms vs crest (steady vs impulsive).
    pairs = [("rms", "zcr"), ("rms", "crest"), ("mad", "zcr")]
    labels = sorted(by_label)
    cmap = plt.get_cmap("tab10")
    colors = {lab: cmap(i % 10) for i, lab in enumerate(labels)}

    fig, axes = plt.subplots(1, len(pairs), figsize=(5 * len(pairs), 4.5))
    if len(pairs) == 1:
        axes = [axes]

    for ax, (fx, fy) in zip(axes, pairs):
        for lab in labels:
            d = by_label[lab]
            ax.scatter(d[fx], d[fy], s=8, alpha=0.45, color=colors[lab], label=lab)
        ax.set_xlabel(fx)
        ax.set_ylabel(fy)
        ax.set_title(f"{fx} vs {fy}")
    axes[0].legend(loc="best", fontsize=8, framealpha=0.9)
    fig.suptitle("Texture features by surface (clusters = separable surfaces)")
    fig.tight_layout()
    path = os.path.join(out_dir, "feature_scatter.png")
    fig.savefig(path, dpi=130)
    plt.close(fig)
    return path


def histograms(by_label, out_dir):
    labels = sorted(by_label)
    cmap = plt.get_cmap("tab10")
    colors = {lab: cmap(i % 10) for i, lab in enumerate(labels)}

    fig, axes = plt.subplots(1, len(FEATURES), figsize=(4 * len(FEATURES), 4))
    if len(FEATURES) == 1:
        axes = [axes]

    for ax, feat in zip(axes, FEATURES):
        # shared bins across surfaces for honest comparison
        allvals = np.concatenate([by_label[l][feat] for l in labels])
        lo, hi = np.percentile(allvals, [1, 99])
        bins = np.linspace(lo, max(hi, lo + 1), 30)
        for lab in labels:
            ax.hist(by_label[lab][feat], bins=bins, alpha=0.5,
                    color=colors[lab], label=lab)
        ax.set_title(feat)
        ax.set_xlabel("value")
    axes[0].set_ylabel("count")
    axes[-1].legend(loc="best", fontsize=8)
    fig.suptitle("Per-feature distribution by surface")
    fig.tight_layout()
    path = os.path.join(out_dir, "feature_histograms.png")
    fig.savefig(path, dpi=130)
    plt.close(fig)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", default="texture_dataset.csv")
    ap.add_argument("--out", default="plots")
    args = ap.parse_args()

    if not os.path.exists(args.csv):
        sys.exit(f"[error] {args.csv} not found. Run log_texture.py first.")

    os.makedirs(args.out, exist_ok=True)
    by_label = load(args.csv)

    print(f"[info] loaded {len(by_label)} surface(s): {', '.join(sorted(by_label))}",
          file=sys.stderr)

    print_summary(by_label)

    if len(by_label) < 2:
        print("[note] only one surface so far -- collect a few more with "
              "log_texture.py to see how they separate. Summary above still "
              "shows this surface's feature ranges.", file=sys.stderr)

    s = scatter_pairs(by_label, args.out)
    h = histograms(by_label, args.out)
    print(f"[done] wrote {s}", file=sys.stderr)
    print(f"[done] wrote {h}", file=sys.stderr)


if __name__ == "__main__":
    main()
