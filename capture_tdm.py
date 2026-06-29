#!/usr/bin/env python3
"""
capture_tdm.py — trigger a FingerFirmware TDM audio-accelerometer capture
over serial and save the drained samples to a file ready for tdm_analyze.py.

USAGE:
    python3 capture_tdm.py /dev/cu.usbmodem1103 capture.txt

WHAT IT DOES:
    1. Opens the serial port at 115200 baud (same as the firmware's UART).
    2. Sends 'c' to arm a capture.
    3. Reads and discards firmware status lines (the ones starting with '[')
       and any in-flight heartbeat line, until the drain actually starts.
    4. Collects "x y z" lines until the firmware prints "[capture] done".
    5. Writes only the data lines to the output file.

Close any other serial monitor (e.g. tools/serial_monitor.sh) before running
this -- only one program can hold the port at a time.

Requires pyserial: pip install pyserial --break-system-packages
"""

import argparse
import sys

try:
    import serial
except ImportError:
    sys.exit("[error] pyserial not installed. Run: pip install pyserial --break-system-packages")


def is_data_line(line):
    """A drained sample line is 3 whitespace-separated integers. Status lines
    start with '[' or 't=' (a heartbeat that slipped in before capture armed)
    or are blank -- anything that doesn't parse as 3 ints is not data."""
    parts = line.split()
    if len(parts) != 3:
        return False
    try:
        [int(p) for p in parts]
        return True
    except ValueError:
        return False


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port, e.g. /dev/cu.usbmodem1103")
    ap.add_argument("output", help="path to write the captured x y z lines")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=10.0,
                     help="seconds to wait for the capture+drain to finish "
                          "before giving up (default: 10)")
    args = ap.parse_args()

    print(f"[info] opening {args.port} at {args.baud} baud...", file=sys.stderr)
    with serial.Serial(args.port, args.baud, timeout=args.timeout) as ser:
        # Give the port a moment and clear out anything already buffered
        # (e.g. a heartbeat line mid-print) so we start from a clean state.
        ser.reset_input_buffer()

        print("[info] sending capture trigger 'c'...", file=sys.stderr)
        ser.write(b"c")

        data_lines = []
        saw_done = False

        while True:
            raw = ser.readline()
            if not raw:
                print("[warn] timed out waiting for data; saving what we have",
                      file=sys.stderr)
                break

            try:
                line = raw.decode("utf-8", errors="replace").strip()
            except Exception:
                continue

            if not line:
                continue

            if line.startswith("["):
                print(f"[fw] {line}", file=sys.stderr)
                if "[capture] done" in line:
                    saw_done = True
                    break
                continue

            if is_data_line(line):
                data_lines.append(line)
            # else: ignore stray heartbeat/log lines that slipped through

        if not saw_done:
            print("[warn] did not see a '[capture] done' line - capture may "
                  "be incomplete or the port had stale buffered output",
                  file=sys.stderr)

        with open(args.output, "w") as f:
            f.write("\n".join(data_lines) + "\n")

        print(f"[done] wrote {len(data_lines)} sample lines to {args.output}",
              file=sys.stderr)


if __name__ == "__main__":
    main()
