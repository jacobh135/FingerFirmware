#!/usr/bin/env python3
"""
log_texture.py — record the continuous texture-feature stream from
FingerFirmware into a labeled CSV, for building an offline training dataset.

The firmware emits one line per ~16 ms window when texture streaming is on:

    feat <seq> <rms> <mad> <peak> <zcr> <crest> <mean>

This script enables streaming (sends 't'), tags every feature row with a
surface label you provide, and writes a CSV. Run it once per surface, dragging
the finger over that surface for the duration.

USAGE:
    python3 log_texture.py /dev/cu.usbmodem103 sandpaper_120grit --seconds 10
    python3 log_texture.py /dev/cu.usbmodem103 smooth_acrylic   --seconds 10 --axis z

Appends to texture_dataset.csv by default (so multiple runs accumulate into one
dataset); use --out to change the file. Columns:
    label, t_host_s, seq, rms, mad, peak, zcr, crest, mean, axis

Close any other serial monitor first. Requires pyserial.
"""

import argparse
import csv
import os
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("[error] pyserial not installed. Run: pip3 install pyserial")

FEATURE_FIELDS = ["seq", "rms", "mad", "peak", "zcr", "crest", "mean",
                  "band0", "band1", "band2", "band3", "centroid", "peakfreq"]


def parse_feature_line(line):
    """Parse a 'feat seq rms mad peak zcr crest mean band0..3 centroid peakfreq'
    line into a dict, or None. Accepts both the Phase 1 (8-token) and Phase 2
    (14-token) formats for backward compatibility."""
    parts = line.split()
    if len(parts) < 8 or parts[0] != "feat":
        return None
    try:
        vals = [int(p) for p in parts[1:]]
    except ValueError:
        return None
    # pad older 7-value lines so the CSV columns stay consistent
    while len(vals) < len(FEATURE_FIELDS):
        vals.append(0)
    return dict(zip(FEATURE_FIELDS, vals[:len(FEATURE_FIELDS)]))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port, e.g. /dev/cu.usbmodem103")
    ap.add_argument("label", help="surface label for this run, e.g. sandpaper_120grit")
    ap.add_argument("--seconds", type=float, default=10.0,
                     help="how long to record (default: 10 s)")
    ap.add_argument("--axis", choices=["x", "y", "z"], default=None,
                     help="select texture axis on the device before recording "
                          "(sends the axis key). Default: leave as-is.")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default="texture_dataset.csv",
                     help="CSV to append to (default: texture_dataset.csv)")
    args = ap.parse_args()

    print(f"[info] opening {args.port} at {args.baud} baud...", file=sys.stderr)
    with serial.Serial(args.port, args.baud, timeout=1.0) as ser:
        ser.reset_input_buffer()

        # Optionally select the axis on the device.
        if args.axis is not None:
            ser.write(args.axis.encode())
            time.sleep(0.1)

        # Enable texture streaming.
        print("[info] enabling texture streaming ('t')...", file=sys.stderr)
        ser.write(b"t")
        ser.reset_input_buffer()   # drop the toggle banner + any partial line

        new_file = not os.path.exists(args.out)
        rows = 0
        t_start = time.time()

        with open(args.out, "a", newline="") as f:
            writer = csv.writer(f)
            if new_file:
                writer.writerow(["label", "t_host_s"] + FEATURE_FIELDS + ["axis"])

            print(f"[info] recording '{args.label}' for {args.seconds:.0f}s -- "
                  f"drag the finger over the surface now...", file=sys.stderr)

            while (time.time() - t_start) < args.seconds:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                feat = parse_feature_line(line)
                if feat is None:
                    continue   # skip banners, heartbeats, partials
                t_host = time.time() - t_start
                writer.writerow(
                    [args.label, f"{t_host:.4f}"]
                    + [feat[k] for k in FEATURE_FIELDS]
                    + [args.axis or ""]
                )
                rows += 1

        # Turn streaming back off so the device returns to heartbeat mode.
        ser.write(b"t")

        dur = time.time() - t_start
        rate = rows / dur if dur > 0 else 0.0
        print(f"[done] logged {rows} feature windows for '{args.label}' "
              f"in {dur:.1f}s ({rate:.0f}/s) -> {args.out}", file=sys.stderr)
        if rows == 0:
            print("[warn] no feature lines captured. Is the new firmware "
                  "flashed? Did streaming actually turn on? Try the serial "
                  "monitor and press 't' manually to check.", file=sys.stderr)


if __name__ == "__main__":
    main()
