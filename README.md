# Finger Firmware

Firmware scaffold for the HAND ERC robotic finger electronics controller.

This project is intentionally simple for fast onboarding:

- CMake + Ninja build
- ARM GCC toolchain
- One NUCLEO-G431KB target
- Flat module folders for app, CAN, I2C sensors, and TDM audio
- No IDE-specific build requirement

The first firmware image is a small self-contained NUCLEO blink program. It is meant to prove that the build and flash flow works before adding CubeMX-generated HAL setup for FDCAN, I2C, and SAI/TDM.

## Quick Start

Run the setup helper first:

```sh
./tools/setup.sh
```

The script detects your OS, checks for CMake, Ninja, the ARM GCC toolchain, and
`STM32_Programmer_CLI`, then installs what it can through a supported package
manager. If a tool needs a manual installer, such as
[STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html),
the script prints the exact next steps and reruns the final environment check.

Useful setup variants:

```sh
./tools/setup.sh --check-only
./tools/setup.sh --yes
```

Manual requirements, if you prefer not to use the script:

- CMake
- Ninja
- `arm-none-eabi-gcc`
- [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html), optional but needed for `flash`

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
