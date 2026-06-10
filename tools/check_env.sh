#!/usr/bin/env sh
set -eu

missing=0

check_tool() {
    if command -v "$1" >/dev/null 2>&1; then
        printf "ok: %s\n" "$1"
    else
        printf "missing: %s\n" "$1"
        missing=1
    fi
}

check_optional_tool() {
    if command -v "$1" >/dev/null 2>&1; then
        printf "ok: %s\n" "$1"
    else
        printf "optional missing: %s, needed for %s\n" "$1" "$2"
    fi
}

check_optional_command() {
    label="$1"
    purpose="$2"
    shift 2

    if path="$("$@" 2>/dev/null)"; then
        printf "ok: %s: %s\n" "$label" "$path"
    else
        printf "optional missing: %s, needed for %s\n" "$label" "$purpose"
    fi
}

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

check_tool cmake
check_tool ninja
check_tool arm-none-eabi-gcc
check_tool arm-none-eabi-objcopy
check_tool arm-none-eabi-size
check_optional_tool arm-none-eabi-gdb "VS Code debugging"

check_optional_tool STM32_Programmer_CLI "flash/erase targets and ST-LINK debugging"
check_optional_command "ST-LINK_gdbserver" "VS Code debugging" "${script_dir}/ST-LINK_gdbserver" --print-path
check_optional_command "STM32CubeProgrammer path" "ST-LINK debugging" "${script_dir}/ST-LINK_gdbserver" --print-programmer-path

if [ "$missing" -ne 0 ]; then
    printf "\nInstall the missing required tools, then rerun this script.\n"
    exit 1
fi

printf "\nEnvironment looks ready for building.\n"
