#!/usr/bin/env python3
"""
detect_surface.py — train a surface classifier from texture_dataset.csv and
run it live: as the finger drags, print the detected surface in real time.

Two modes:
  --check         Train, then report cross-validated accuracy and per-surface
                  confusion on your existing data. Run this FIRST to see whether
                  the surfaces are actually distinguishable before going live.
  (default/live)  Train, then open the serial stream and print the predicted
                  surface ~per window, smoothed over a short vote window.

The classifier is k-nearest-neighbors on the per-window features
[rms, mad, peak, zcr, crest], with each feature standardized (z-scored) so no
single large-valued feature dominates the distance. KNN is a good fit here:
zero training time, and it naturally matches the cluster structure you saw in
plot_dataset.py.

USAGE:
    python3 detect_surface.py --check
    python3 detect_surface.py /dev/cu.usbmodem103
    python3 detect_surface.py /dev/cu.usbmodem103 --axis z --vote 6 --k 7

Requires numpy + pyserial. (No scikit-learn needed; KNN is implemented here so
there's nothing extra to install and the logic is fully visible.)
"""

import argparse
import csv
import sys
import time
from collections import Counter, deque

import numpy as np

FEATURES = ["rms", "mad", "peak", "zcr", "crest",
            "band0", "band1", "band2", "band3", "centroid", "peakfreq"]
ALL_FIELDS = ["seq", "rms", "mad", "peak", "zcr", "crest", "mean",
              "band0", "band1", "band2", "band3", "centroid", "peakfreq"]
# Set by load_dataset() to whichever FEATURES are actually present in the CSV;
# the live query must use the same set the model was trained on.
ACTIVE_FEATURES = list(FEATURES)


# --------------------------- data + model --------------------------- #

def load_dataset(csv_path):
    X, y = [], []
    with open(csv_path, newline="") as f:
        reader = csv.DictReader(f)
        if "label" not in reader.fieldnames:
            sys.exit(f"[error] CSV has no 'label' column; found {reader.fieldnames}")
        # Use whichever of our features are actually present in this CSV, so a
        # Phase-1 dataset (time-domain only) and a Phase-2 dataset (with spectral
        # columns) both load. The active feature set is intersected here.
        present = [c for c in FEATURES if c in reader.fieldnames]
        if not present:
            sys.exit(f"[error] CSV has none of the expected feature columns "
                      f"{FEATURES}; found {reader.fieldnames}")
        global ACTIVE_FEATURES
        ACTIVE_FEATURES = present
        for row in reader:
            try:
                X.append([float(row[k]) for k in present])
                y.append(row["label"])
            except (ValueError, KeyError):
                continue
    if not X:
        sys.exit("[error] no usable rows in CSV")
    if len(present) < len(FEATURES):
        print(f"[note] CSV has {len(present)}/{len(FEATURES)} features "
              f"(missing: {set(FEATURES)-set(present)}). Using what's present. "
              f"Re-collect with Phase-2 firmware to get spectral features.",
              file=sys.stderr)
    return np.array(X, dtype=np.float64), np.array(y)


def standardizer(X):
    """Return (mean, std) for z-scoring; guard against zero-variance features."""
    mu = X.mean(axis=0)
    sd = X.std(axis=0)
    sd[sd == 0] = 1.0
    return mu, sd


def knn_predict(Xtrain, ytrain, Xquery, k):
    """Plain KNN. Xtrain/Xquery already standardized. Returns array of labels."""
    preds = []
    for q in Xquery:
        d = np.sqrt(((Xtrain - q) ** 2).sum(axis=1))
        idx = np.argpartition(d, min(k, len(d) - 1))[:k]
        vote = Counter(ytrain[idx])
        preds.append(vote.most_common(1)[0][0])
    return np.array(preds)


# ------------------------------ check ------------------------------ #

def run_check(X, y, k):
    mu, sd = standardizer(X)
    Xs = (X - mu) / sd

    # Stratified-ish k-fold by simple shuffling (no sklearn). 5 folds.
    rng = np.random.default_rng(0)
    order = rng.permutation(len(Xs))
    Xs, yy = Xs[order], y[order]
    folds = np.array_split(np.arange(len(Xs)), 5)

    labels = sorted(set(y))
    conf = {a: Counter() for a in labels}
    correct = 0
    total = 0
    for i in range(5):
        test_idx = folds[i]
        train_idx = np.concatenate([folds[j] for j in range(5) if j != i])
        pred = knn_predict(Xs[train_idx], yy[train_idx], Xs[test_idx], k)
        for t, p in zip(yy[test_idx], pred):
            conf[t][p] += 1
            correct += (t == p)
            total += 1

    acc = correct / total if total else 0.0
    print()
    print(f"5-fold cross-validated accuracy: {acc*100:.1f}%  (k={k}, n={total})")
    print()
    print("Confusion (rows = true surface, cols = predicted):")
    w = max(len(l) for l in labels) + 2
    print(" " * w + "".join(f"{l[:10]:>12}" for l in labels))
    for t in labels:
        row = "".join(f"{conf[t][p]:>12}" for p in labels)
        print(f"{t:<{w}}{row}")
    print()
    if acc > 0.9:
        print("[great] surfaces are cleanly separable with these features.")
    elif acc > 0.75:
        print("[ok] decent separation; more data or Phase-2 spectral features "
              "would tighten it.")
    else:
        print("[weak] surfaces overlap a lot in these features. Consider "
              "collecting more/cleaner data, or moving to Phase-2 FFT features.")
    print()


# ------------------------------ live ------------------------------- #

def parse_feature_line(line):
    parts = line.split()
    if len(parts) < 8 or parts[0] != "feat":
        return None
    try:
        vals = [int(p) for p in parts[1:]]
    except ValueError:
        return None
    # firmware order: seq rms mad peak zcr crest mean [band0..3 centroid peakfreq]
    while len(vals) < len(ALL_FIELDS):
        vals.append(0)
    return dict(zip(ALL_FIELDS, vals[:len(ALL_FIELDS)]))


def run_live(X, y, k, port, baud, axis, vote_n):
    try:
        import serial
    except ImportError:
        sys.exit("[error] pyserial not installed. Run: pip3 install pyserial")

    mu, sd = standardizer(X)
    Xs = (X - mu) / sd

    print(f"[info] trained on {len(X)} windows, "
          f"{len(set(y))} surfaces: {', '.join(sorted(set(y)))}", file=sys.stderr)
    print(f"[info] opening {port}...", file=sys.stderr)

    with serial.Serial(port, baud, timeout=1.0) as ser:
        ser.reset_input_buffer()
        if axis is not None:
            ser.write(axis.encode()); time.sleep(0.1)
        ser.write(b"t")              # streaming ON
        ser.reset_input_buffer()

        votes = deque(maxlen=vote_n)
        last_print = ""
        print("[info] streaming. Drag the finger over a surface. Ctrl-C to stop.\n",
              file=sys.stderr)
        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                d = parse_feature_line(raw.decode("utf-8", errors="replace").strip())
                if d is None:
                    continue

                q = np.array([[float(d[f]) for f in ACTIVE_FEATURES]])
                qs = (q - mu) / sd
                pred = knn_predict(Xs, y, qs, k)[0]
                votes.append(pred)

                # smoothed decision: majority over the last vote_n windows
                smooth = Counter(votes).most_common(1)[0][0]
                conf = Counter(votes)[smooth] / len(votes)

                # live one-line readout (overwrites in place)
                bar = f"{smooth:<16} ({conf*100:3.0f}%)  rms={d['rms']:>4} zcr={d['zcr']:>3} crest={d['crest']:>4}"
                if bar != last_print:
                    print("\r" + bar + " " * 8, end="", flush=True)
                    last_print = bar
        except KeyboardInterrupt:
            print("\n[info] stopping; streaming OFF", file=sys.stderr)
            ser.write(b"t")           # streaming OFF


# ------------------------------ main ------------------------------- #

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", default=None,
                     help="serial port for live mode (omit with --check)")
    ap.add_argument("--csv", default="texture_dataset.csv")
    ap.add_argument("--check", action="store_true",
                     help="evaluate separability on existing data and exit")
    ap.add_argument("--k", type=int, default=7, help="KNN neighbors (default 7)")
    ap.add_argument("--vote", type=int, default=6,
                     help="windows to smooth the live label over (default 6 ~100ms)")
    ap.add_argument("--axis", choices=["x", "y", "z"], default=None,
                     help="set device texture axis before live detection")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    X, y = load_dataset(args.csv)

    if args.check or args.port is None:
        if args.port is None and not args.check:
            print("[note] no port given -> running --check. Pass a port for live "
                  "detection.", file=sys.stderr)
        run_check(X, y, args.k)
        return

    run_live(X, y, args.k, args.port, args.baud, args.axis, args.vote)


if __name__ == "__main__":
    main()
