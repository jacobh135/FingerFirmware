#!/usr/bin/env python3
"""
stream_audio.py -- capture the live raw-audio stream from FingerFirmware and
save it to a WAV file (and report any dropped samples).

The firmware's 'a' command starts continuous single-axis audio: it prints a
text banner at 115200, then switches to 1 Mbaud and streams framed binary:

    [0xA5 0x5A][count_lo count_hi][int16 little-endian samples ...]

This script sends 'a', reads the banner at 115200 to learn the axis, then
reopens at 1 Mbaud, locks onto the sync word, and writes all samples to a WAV
at the ~15625 Hz sample rate.

USAGE:
    python3 stream_audio.py /dev/cu.usbmodem103 --seconds 10 --out finger.wav
    python3 stream_audio.py /dev/cu.usbmodem103 --axis x --seconds 5

Power-cycle the board first so audio streaming starts OFF (the 'a' toggle
assumes OFF->ON). Requires pyserial.
"""

import argparse
import struct
import sys
import time
import wave

try:
    import serial
except ImportError:
    sys.exit("[error] pyserial not installed. Run: pip3 install pyserial")

SAMPLE_RATE = 15625      # single-axis frame rate (WCLK ~15.625 kHz)
SYNC0 = 0xA5
SYNC1 = 0x5A
LOG_BAUD = 115200
AUDIO_BAUD = 1000000


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--out", default="finger_audio.wav")
    ap.add_argument("--axis", choices=["x", "y", "z"], default=None,
                     help="select axis before streaming (sends x/y/z)")
    args = ap.parse_args()

    # Phase 1: open at log baud, optionally set axis, send 'a', read the banner.
    print(f"[info] opening {args.port} at {LOG_BAUD} to start audio...",
          file=sys.stderr)
    ser = serial.Serial(args.port, LOG_BAUD, timeout=1.0)
    ser.reset_input_buffer()
    if args.axis:
        ser.write(args.axis.encode())
        time.sleep(0.1)
    ser.write(b"a")
    time.sleep(0.2)
    banner = ser.readline().decode("utf-8", errors="replace").strip()
    # may need one more read to catch the banner line
    if "audio" not in banner:
        banner2 = ser.readline().decode("utf-8", errors="replace").strip()
        banner = banner2 or banner
    print(f"[info] device says: {banner}", file=sys.stderr)
    ser.close()

    # Phase 2: reopen at the high baud and read the binary stream.
    time.sleep(0.2)
    print(f"[info] reopening at {AUDIO_BAUD} baud for binary stream...",
          file=sys.stderr)
    ser = serial.Serial(args.port, AUDIO_BAUD, timeout=1.0)
    ser.reset_input_buffer()

    samples = bytearray()
    total = 0
    t_start = time.time()

    def read_exact(n):
        buf = b""
        while len(buf) < n:
            chunk = ser.read(n - len(buf))
            if not chunk:
                return None
            buf += chunk
        return buf

    print(f"[info] capturing {args.seconds:.0f}s -- drag the finger now...",
          file=sys.stderr)
    try:
        while (time.time() - t_start) < args.seconds:
            # find sync
            b = ser.read(1)
            if not b or b[0] != SYNC0:
                continue
            b = ser.read(1)
            if not b or b[0] != SYNC1:
                continue
            cnt = read_exact(2)
            if cnt is None:
                break
            count = cnt[0] | (cnt[1] << 8)
            payload = read_exact(count * 2)
            if payload is None:
                break
            samples += payload
            total += count
    except KeyboardInterrupt:
        print("\n[info] stopped early", file=sys.stderr)

    # Turn streaming off: reopen at log baud (device is at high baud now, but
    # sending 'a' at the wrong baud won't parse -- so just send at high baud;
    # the device reads bytes regardless of value, and 'a' toggles off).
    try:
        ser.baudrate = AUDIO_BAUD
        ser.write(b"a")
    except Exception:
        pass
    ser.close()

    dur = time.time() - t_start
    # write WAV
    with wave.open(args.out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(bytes(samples))

    expected = int(dur * SAMPLE_RATE)
    print(f"[done] {total} samples in {dur:.1f}s ({total/dur:.0f}/s) -> {args.out}",
          file=sys.stderr)
    print(f"[info] expected ~{expected} at {SAMPLE_RATE} Hz; "
          f"{'OK' if total > expected*0.9 else 'LOW -- dropped samples?'}",
          file=sys.stderr)
    if total == 0:
        print("[warn] no samples. Was streaming already ON? Power-cycle and "
              "retry. Is the new firmware flashed?", file=sys.stderr)


if __name__ == "__main__":
    main()
