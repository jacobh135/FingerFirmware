#!/usr/bin/env sh
set -eu

ST_CUBE_PROG_URL="https://www.st.com/en/development-tools/stm32cubeprog.html"
ST_CUBE_CLT_URL="https://www.st.com/en/development-tools/stm32cubeclt.html"
LOCAL_BIN="${HOME}/.local/bin"
ASSUME_YES=0
CHECK_ONLY=0

usage() {
    cat <<EOF
Usage: ./tools/setup.sh [--yes] [--check-only]

Checks and, when possible, installs the tools needed to build, flash, and debug this firmware:
  - cmake
  - ninja
  - arm-none-eabi-gcc, objcopy, and size
  - STM32CubeCLT for full flash/debug support
  - or standalone STM32CubeProgrammer for flash/erase only

Options:
  --yes         Run supported package-manager installs without prompting.
  --check-only  Only report missing tools and setup guidance.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --yes|-y)
            ASSUME_YES=1
            ;;
        --check-only)
            CHECK_ONLY=1
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf "Unknown option: %s\n\n" "$1"
            usage
            exit 2
            ;;
    esac
    shift
done

info() {
    printf "info: %s\n" "$1"
}

ok() {
    printf "ok: %s\n" "$1"
}

warn() {
    printf "warning: %s\n" "$1"
}

fail() {
    printf "error: %s\n" "$1"
}

have() {
    command -v "$1" >/dev/null 2>&1
}

run_or_warn() {
    if "$@"; then
        return 0
    fi

    warn "command failed: $*"
    return 0
}

confirm() {
    question="$1"

    if [ "$ASSUME_YES" -eq 1 ]; then
        return 0
    fi

    if [ ! -t 0 ]; then
        return 1
    fi

    printf "%s [y/N] " "$question"
    read answer
    case "$answer" in
        y|Y|yes|YES|Yes)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

detect_os() {
    case "$(uname -s 2>/dev/null || printf unknown)" in
        Darwin)
            printf "macos"
            ;;
        Linux)
            printf "linux"
            ;;
        MINGW*|MSYS*|CYGWIN*)
            printf "windows"
            ;;
        *)
            printf "unknown"
            ;;
    esac
}

missing_build_tools() {
    missing=""

    for tool in cmake ninja arm-none-eabi-gcc arm-none-eabi-objcopy arm-none-eabi-size; do
        if ! have "$tool"; then
            missing="${missing} ${tool}"
        fi
    done

    printf "%s" "$missing"
}

install_build_tools_macos() {
    if ! have brew; then
        warn "Homebrew is not installed, so this script cannot install build tools automatically."
        printf "\nInstall Homebrew from https://brew.sh, then rerun:\n"
        printf "  ./tools/setup.sh\n\n"
        printf "Or install manually:\n"
        printf "  brew install cmake ninja arm-none-eabi-gcc\n"
        return 1
    fi

    if [ "$CHECK_ONLY" -eq 1 ]; then
        printf "Would run:\n"
        printf "  brew install cmake ninja arm-none-eabi-gcc\n"
        return 0
    fi

    if confirm "Install missing build tools with Homebrew?"; then
        run_or_warn brew install cmake ninja arm-none-eabi-gcc
    else
        warn "Skipped Homebrew install."
        return 1
    fi
}

install_build_tools_linux() {
    if have apt-get; then
        if [ "$CHECK_ONLY" -eq 1 ]; then
            printf "Would run:\n"
            printf "  sudo apt-get update\n"
            printf "  sudo apt-get install -y cmake ninja-build gcc-arm-none-eabi binutils-arm-none-eabi\n"
            return 0
        fi

        if confirm "Install missing build tools with apt?"; then
            run_or_warn sudo apt-get update
            run_or_warn sudo apt-get install -y cmake ninja-build gcc-arm-none-eabi binutils-arm-none-eabi
        else
            warn "Skipped apt install."
            return 1
        fi
    elif have dnf; then
        if [ "$CHECK_ONLY" -eq 1 ]; then
            printf "Would try:\n"
            printf "  sudo dnf install -y cmake ninja-build arm-none-eabi-gcc-cs arm-none-eabi-binutils-cs\n"
            return 0
        fi

        if confirm "Try installing missing build tools with dnf?"; then
            run_or_warn sudo dnf install -y cmake ninja-build arm-none-eabi-gcc-cs arm-none-eabi-binutils-cs
        else
            warn "Skipped dnf install."
            return 1
        fi
    elif have pacman; then
        if [ "$CHECK_ONLY" -eq 1 ]; then
            printf "Would run:\n"
            printf "  sudo pacman -S --needed cmake ninja arm-none-eabi-gcc arm-none-eabi-binutils\n"
            return 0
        fi

        if confirm "Install missing build tools with pacman?"; then
            run_or_warn sudo pacman -S --needed cmake ninja arm-none-eabi-gcc arm-none-eabi-binutils
        else
            warn "Skipped pacman install."
            return 1
        fi
    else
        warn "No supported package manager found."
        printf "\nInstall these packages manually for your system:\n"
        printf "  cmake ninja arm-none-eabi-gcc arm-none-eabi-binutils\n"
        return 1
    fi
}

install_build_tools() {
    os="$1"

    case "$os" in
        macos)
            install_build_tools_macos
            ;;
        linux)
            install_build_tools_linux
            ;;
        windows)
            warn "Automatic Windows setup is not supported by this shell script."
            printf "\nUse WSL, MSYS2, or an IDE toolchain, then rerun ./tools/check_env.sh.\n"
            return 1
            ;;
        *)
            warn "Unknown OS. Install CMake, Ninja, and ARM GNU toolchain manually."
            return 1
            ;;
    esac
}

ensure_local_bin_on_path() {
    mkdir -p "$LOCAL_BIN"

    case ":$PATH:" in
        *":$LOCAL_BIN:"*)
            return 0
            ;;
    esac

    rc_file=""
    shell_name="$(basename "${SHELL:-sh}")"
    case "$shell_name" in
        zsh)
            rc_file="${HOME}/.zshrc"
            ;;
        bash)
            rc_file="${HOME}/.bashrc"
            ;;
    esac

    if [ -n "$rc_file" ]; then
        if [ "$CHECK_ONLY" -eq 1 ]; then
            printf "Would add this line to %s:\n" "$rc_file"
            printf "  export PATH=\"\$HOME/.local/bin:\$PATH\"\n"
            return 0
        fi

        if confirm "Add ${LOCAL_BIN} to PATH in ${rc_file}?"; then
            printf "\nexport PATH=\"\$HOME/.local/bin:\$PATH\"\n" >> "$rc_file"
            export PATH="$LOCAL_BIN:$PATH"
            ok "${LOCAL_BIN} added to PATH for future shells"
        else
            warn "${LOCAL_BIN} is not on PATH. Add it manually or call tools by full path."
            return 1
        fi
    else
        warn "${LOCAL_BIN} is not on PATH."
        printf "Add this to your shell startup file:\n"
        printf "  export PATH=\"\$HOME/.local/bin:\$PATH\"\n"
        return 1
    fi
}

find_st_cli() {
    os="$1"

    if [ "$os" = "macos" ]; then
        for candidate in \
            "/Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin/STM32_Programmer_CLI" \
            "/Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/MacOs/bin/STM32_Programmer_CLI"
        do
            if [ -x "$candidate" ]; then
                printf "%s\n" "$candidate"
                return 0
            fi
        done

        found="$(find /Applications -name STM32_Programmer_CLI -type f -perm -111 -print 2>/dev/null | head -n 1 || true)"
        if [ -n "$found" ]; then
            printf "%s\n" "$found"
            return 0
        fi
    elif [ "$os" = "linux" ]; then
        found="$(find \
            "${HOME}/STMicroelectronics" \
            "${HOME}/.local/share/stm32cube/bundles/programmer" \
            /opt/ST \
            /opt/st \
            /usr/local/STMicroelectronics \
            -name STM32_Programmer_CLI -type f -perm -111 -print 2>/dev/null | head -n 1 || true)"
        if [ -n "$found" ]; then
            printf "%s\n" "$found"
            return 0
        fi
    fi

    return 1
}

st_cli_mode() {
    cli_path="$1"
    os="$2"

    if "$cli_path" --version >/dev/null 2>&1; then
        printf "direct"
        return 0
    fi

    if [ "$os" = "macos" ] && [ "$(uname -m)" = "arm64" ]; then
        if arch -x86_64 "$cli_path" --version >/dev/null 2>&1; then
            printf "rosetta"
            return 0
        fi
    fi

    printf "broken"
    return 1
}

install_st_command() {
    cli_path="$1"
    mode="$2"

    ensure_local_bin_on_path || true

    if [ "$CHECK_ONLY" -eq 1 ]; then
        printf "Would install STM32_Programmer_CLI wrapper in %s\n" "$LOCAL_BIN"
        return 0
    fi

    if [ -e "${LOCAL_BIN}/STM32_Programmer_CLI" ] || [ -L "${LOCAL_BIN}/STM32_Programmer_CLI" ]; then
        if ! confirm "Replace existing ${LOCAL_BIN}/STM32_Programmer_CLI?"; then
            warn "Skipped STM32_Programmer_CLI wrapper install."
            return 1
        fi
    fi

    if [ "$mode" = "rosetta" ]; then
        cat > "${LOCAL_BIN}/STM32_Programmer_CLI" <<EOF
#!/usr/bin/env sh
exec arch -x86_64 "$cli_path" "\$@"
EOF
        chmod 755 "${LOCAL_BIN}/STM32_Programmer_CLI"
        ok "installed Rosetta wrapper: ${LOCAL_BIN}/STM32_Programmer_CLI"
    else
        rm -f "${LOCAL_BIN}/STM32_Programmer_CLI"
        ln -s "$cli_path" "${LOCAL_BIN}/STM32_Programmer_CLI"
        ok "linked STM32_Programmer_CLI into ${LOCAL_BIN}"
    fi
}

guide_st_install() {
    os="$1"

    warn "STM32CubeProgrammer was not found."
    printf "\nRecommended for full flash/debug support:\n"
    printf "  Download and install STM32CubeCLT from ST:\n"
    printf "  %s\n\n" "$ST_CUBE_CLT_URL"
    printf "  STM32CubeCLT includes STM32CubeProgrammer, ST-LINK_gdbserver, GDB, and SVD files.\n\n"
    printf "Flash-only fallback:\n"
    printf "  Download and install STM32CubeProgrammer from ST:\n"
    printf "  %s\n\n" "$ST_CUBE_PROG_URL"

    if [ "$os" = "macos" ]; then
        printf "macOS notes:\n"
        printf "  - Use the Mac package matching your CPU when ST offers separate downloads.\n"
        printf "  - STM32CubeCLT is preferred because it covers both flashing and debugging.\n"
        printf "  - STM32CubeProgrammer is commonly under:\n"
        printf "    /Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin/STM32_Programmer_CLI\n"
        printf "  - If the app is blocked by Gatekeeper, right-click the installer and choose Open.\n"
    elif [ "$os" = "linux" ]; then
        printf "Linux notes:\n"
        printf "  - Install ST's Linux package for STM32CubeCLT when you want debugging.\n"
        printf "  - Install STM32CubeProgrammer if you only need flash/erase targets.\n"
        printf "  - If needed, symlink the CLI into ~/.local/bin.\n"
    fi
}

setup_st_cli() {
    os="$1"

    if have STM32_Programmer_CLI; then
        if STM32_Programmer_CLI --version >/dev/null 2>&1; then
            ok "STM32_Programmer_CLI is installed and runnable"
            return 0
        fi

        warn "STM32_Programmer_CLI is on PATH but did not run successfully."
        info "looking for another STM32CubeProgrammer install to repair the command"
    fi

    if cli_path="$(find_st_cli "$os")"; then
        mode="$(st_cli_mode "$cli_path" "$os" || true)"
        case "$mode" in
            direct|rosetta)
                install_st_command "$cli_path" "$mode" || true
                ;;
            *)
                warn "Found STM32_Programmer_CLI, but it did not run successfully:"
                printf "  %s\n\n" "$cli_path"
                if [ "$os" = "macos" ] && [ "$(uname -m)" = "arm64" ]; then
                    printf "If Rosetta is not installed, run:\n"
                    printf "  softwareupdate --install-rosetta --agree-to-license\n"
                fi
                return 1
                ;;
        esac
    else
        guide_st_install "$os"
        return 1
    fi
}

print_summary() {
    missing="$1"

    if [ -n "$missing" ]; then
        warn "Still missing:${missing}"
    else
        ok "required build tools are installed"
    fi

    if have STM32_Programmer_CLI; then
        ok "STM32_Programmer_CLI is on PATH"
    else
        warn "STM32_Programmer_CLI is not on PATH; flash/erase targets will not work yet"
    fi

    if have arm-none-eabi-gdb; then
        ok "arm-none-eabi-gdb is on PATH"
    else
        warn "arm-none-eabi-gdb is not on PATH; VS Code debugging will not work yet"
        printf "  Install STM32CubeCLT or another ARM GDB package:\n"
        printf "  %s\n" "$ST_CUBE_CLT_URL"
    fi

    script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
    if "${script_dir}/ST-LINK_gdbserver" --print-path >/dev/null 2>&1; then
        ok "ST-LINK_gdbserver was found"
    else
        warn "ST-LINK_gdbserver was not found; install STM32CubeCLT for VS Code debugging"
        printf "  %s\n" "$ST_CUBE_CLT_URL"
    fi
}

main() {
    os="$(detect_os)"
    info "detected OS: ${os}"

    missing="$(missing_build_tools)"
    if [ -n "$missing" ]; then
        warn "missing build tools:${missing}"
        install_build_tools "$os" || true
    else
        ok "build tools found"
    fi

    setup_st_cli "$os" || true

    missing="$(missing_build_tools)"
    printf "\n"
    print_summary "$missing"

    printf "\nRunning final environment check:\n"
    script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
    "${script_dir}/check_env.sh"
}

main "$@"
