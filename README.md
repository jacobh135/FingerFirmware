# Finger Firmware

Firmware scaffold for the HAND ERC robotic finger electronics controller.

This project is intentionally simple for fast onboarding:

- CMake + Ninja build
- ARM GCC toolchain
- One NUCLEO-G431KB target
- Flat module folders for app, CAN, I2C sensors, and TDM audio
- No IDE-specific build requirement

## Quick Start

Run the setup helper first:

```sh
./tools/setup.sh
```

The script detects your OS, checks for CMake, Ninja, the ARM GCC toolchain, and
ST command-line tools, then installs what it can through a supported package
manager. If an ST tool needs a manual installer, the script prints the next
steps and reruns the final environment check.

Useful setup variants:

```sh
./tools/setup.sh --check-only
./tools/setup.sh --yes
```

Manual build requirements, if you prefer not to use the script:

- CMake
- Ninja
- `arm-none-eabi-gcc`
- `arm-none-eabi-objcopy`
- `arm-none-eabi-size`

Additional ST tools:

- [STM32CubeCLT](https://www.st.com/en/development-tools/stm32cubeclt.html), recommended for full flash/debug support because it includes STM32CubeProgrammer, the ST-LINK GDB server, GDB, and SVD files
- [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html), smaller flash-only alternative if you do not need VS Code debugging

Then run:

```sh
./tools/check_env.sh
cmake --preset nucleo-debug
cmake --build --preset nucleo-debug
```

To flash a connected NUCLEO-G431KB:

```sh
cmake --build --preset nucleo-debug --target flash
```

## VS Code

Install the recommended extensions from `.vscode/extensions.json`, then run:

```sh
./tools/setup.sh
cmake --preset nucleo-debug
```

For debugging, install
[STM32CubeCLT](https://www.st.com/en/development-tools/stm32cubeclt.html) or
an equivalent setup that provides both `arm-none-eabi-gdb` and
`ST-LINK_gdbserver`. STM32CubeCLT includes STM32CubeProgrammer, so it is enough
for both flashing and debugging. The standalone STM32CubeProgrammer install is
only enough for flashing.

Check the debugger tools with:

```sh
./tools/check_env.sh
tools/ST-LINK_gdbserver --print-path
tools/ST-LINK_gdbserver --print-programmer-path
```

Use the **Debug NUCLEO-G431KB via ST-LINK** launch configuration to build and
debug from VS Code.

The committed VS Code configuration avoids machine-specific absolute paths. If
Cortex-Debug cannot find the ST-LINK GDB server on your machine, the
`tools/ST-LINK_gdbserver` wrapper checks common install locations. You can also
set `STLINK_GDB_SERVER=/absolute/path/to/ST-LINK_gdbserver` in your shell or VS
Code environment.

If Cortex-Debug cannot find `arm-none-eabi-gdb` or STM32CubeProgrammer after
installing your ST tools, set those paths in your VS Code user settings instead
of editing `.vscode/launch.json`.

## Useful Commands

```sh
cmake --preset nucleo-debug
cmake --build --preset nucleo-debug
cmake --build --preset nucleo-debug --target size
cmake --build --preset nucleo-debug --target flash
cmake --build --preset nucleo-debug --target erase
```

## Project Layout

```text
app/                Main firmware behavior
audio/              TDM/SAI audio capture module
comms/              CAN/FDCAN protocol and transport module
core/               Startup and main entry point
sensors/            I2C sensor module
boards/             Board-specific linker script and board notes
cmake/              Toolchain and helper CMake files
docs/               Bring-up notes
tools/              Newcomer-friendly scripts
```

## Current Milestones

- [x] Build and flash NUCLEO-G431KB.
- [x] Blink LD2 on PB8.
- [ ] Test setup.sh scripts on new machines
- [ ] Add UART logging.
- [ ] Add CubeMX `.ioc` for final STM32G431KBU6 pinout.
- [ ] Bring up I2C sensor bus.
- [ ] Bring up FDCAN loopback and external CAN transceiver.
- [ ] Bring up SAI/TDM audio capture with DMA.
- [ ] Add CI with GitHub Actions.

## Notes For New Contributors

Start in `core/src/main.c`, then read `app/app.c`.

Application code should live in `app/`, `comms/`, `sensors/`, and `audio/`. Keep chip startup and board-specific setup small and isolated so future CubeMX-generated code can replace it cleanly.
