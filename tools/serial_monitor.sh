#!/usr/bin/env sh
set -eu

baud=115200
port=""
list_only=0
kill_busy=0
use_screen=0

usage() {
    printf "Usage: %s [--list] [--port DEVICE] [--baud BAUD] [--kill-busy] [--screen]\n" "$0"
    printf "\n"
    printf "Find and open a USB serial monitor for firmware UART logs.\n"
    printf "\n"
    printf "Options:\n"
    printf "  --list        List likely serial ports and exit.\n"
    printf "  --port DEV    Use a specific serial device.\n"
    printf "  --baud BAUD   Baud rate. Default: 115200.\n"
    printf "  --kill-busy   Kill processes currently holding the selected port.\n"
    printf "  --screen      Attach directly with screen instead of wrapping it.\n"
    printf "  -h, --help    Show this help.\n"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --list)
            list_only=1
            ;;
        --port)
            if [ "$#" -lt 2 ]; then
                printf "error: --port requires a device path\n" >&2
                exit 2
            fi
            port="$2"
            shift
            ;;
        --baud)
            if [ "$#" -lt 2 ]; then
                printf "error: --baud requires a value\n" >&2
                exit 2
            fi
            baud="$2"
            shift
            ;;
        --kill-busy)
            kill_busy=1
            ;;
        --screen)
            use_screen=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf "error: unknown argument: %s\n\n" "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

find_ports() {
    for candidate in \
        /dev/cu.usbmodem* \
        /dev/cu.usbserial* \
        /dev/ttyACM* \
        /dev/ttyUSB*
    do
        if [ -e "$candidate" ]; then
            printf "%s\n" "$candidate"
        fi
    done
}

describe_busy_port() {
    busy_port="$1"

    if command -v lsof >/dev/null 2>&1; then
        if lsof "$busy_port" >/tmp/finger_firmware_lsof.$$ 2>/dev/null; then
            printf "Port is busy: %s\n\n" "$busy_port" >&2
            cat /tmp/finger_firmware_lsof.$$ >&2
            rm -f /tmp/finger_firmware_lsof.$$
            return 0
        fi

        rm -f /tmp/finger_firmware_lsof.$$
    fi

    return 1
}

kill_busy_port_processes() {
    busy_port="$1"

    if ! command -v lsof >/dev/null 2>&1; then
        printf "error: lsof is required for --kill-busy\n" >&2
        exit 1
    fi

    pids="$(lsof -t "$busy_port" 2>/dev/null || true)"
    if [ -z "$pids" ]; then
        return 0
    fi

    printf "Killing process(es) using %s: %s\n" "$busy_port" "$pids" >&2
    # shellcheck disable=SC2086
    kill $pids
}

run_screen_logger() {
    serial_port="$1"
    serial_baud="$2"
    temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/finger-firmware-serial.XXXXXX")"
    logfile="${temp_dir}/serial.log"
    screenrc="${temp_dir}/screenrc"
    session_name="finger-firmware-serial-$$"

    cleanup() {
        screen -S "$session_name" -X quit >/dev/null 2>&1 || true
        rm -rf "$temp_dir"
    }

    trap cleanup INT TERM EXIT

    : >"$logfile"
    {
        printf "logfile %s\n" "$logfile"
        printf "logfile flush 1\n"
        printf "deflog on\n"
    } >"$screenrc"

    screen -c "$screenrc" -dmS "$session_name" "$serial_port" "$serial_baud"
    sleep 1

    if ! screen -ls | grep "$session_name" >/dev/null 2>&1; then
        printf "error: failed to start screen serial session for %s\n" "$serial_port" >&2
        exit 1
    fi

    printf "Opening %s at %s baud.\n" "$serial_port" "$serial_baud" >&2
    printf "Press Ctrl-C to quit.\n\n" >&2

    tail -n +1 -f "$logfile" &
    tail_pid="$!"

    while screen -ls | grep "$session_name" >/dev/null 2>&1; do
        sleep 1
    done

    kill "$tail_pid" >/dev/null 2>&1 || true
    wait "$tail_pid" 2>/dev/null || true
    printf "\nSerial monitor closed.\n" >&2
}

if [ "$list_only" -eq 1 ]; then
    found="$(find_ports)"
    if [ -z "$found" ]; then
        printf "No likely USB serial ports found.\n"
        exit 1
    fi

    printf "%s\n" "$found"
    exit 0
fi

if [ -z "$port" ]; then
    ports="$(find_ports)"
    if [ -z "$ports" ]; then
        printf "error: no likely USB serial ports found\n" >&2
        printf "Connect the board, then rerun %s --list.\n" "$0" >&2
        exit 1
    fi

    port="$(printf "%s\n" "$ports" | sed -n '1p')"
    extra_port="$(printf "%s\n" "$ports" | sed -n '2p')"

    if [ -n "$extra_port" ]; then
        printf "Multiple likely serial ports found; using %s.\n" "$port" >&2
        printf "Use --list to inspect them or --port DEVICE to choose one.\n\n" >&2
    fi
fi

if [ ! -e "$port" ]; then
    printf "error: serial port does not exist: %s\n" "$port" >&2
    exit 1
fi

if describe_busy_port "$port"; then
    if [ "$kill_busy" -eq 1 ]; then
        kill_busy_port_processes "$port"
        sleep 1
    else
        printf "\nClose that program or rerun with --kill-busy.\n" >&2
        exit 1
    fi
fi

if [ "$use_screen" -eq 1 ]; then
    if ! command -v screen >/dev/null 2>&1; then
        printf "error: screen is not installed or is not on PATH\n" >&2
        exit 1
    fi

    printf "Opening %s at %s baud with screen.\n" "$port" "$baud" >&2
    printf "Exit screen with Ctrl-A, then K, then Y.\n\n" >&2
    exec screen "$port" "$baud"
fi

if ! command -v screen >/dev/null 2>&1; then
    printf "error: screen is required for the default Ctrl-C serial monitor\n" >&2
    printf "Install screen or use a VS Code serial monitor extension.\n" >&2
    exit 1
fi

run_screen_logger "$port" "$baud"
