cmake_minimum_required(VERSION 3.22)

project(FingerFirmware C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(TARGET_NAME finger_firmware)
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/boards/nucleo_g431kb/STM32G431KB_FLASH.ld)

add_executable(${TARGET_NAME}
    core/src/main.c
    core/src/startup.c
    core/src/system_stm32g431.c
    core/src/system_time.c
    core/src/uart_log.c
    app/app.c
    app/diagnostics.c
    comms/can.c
    comms/i2c.c
    comms/tdm.c
    sensors/lsm6dsv16b.c
)

target_include_directories(${TARGET_NAME} PRIVATE
    core/inc
    app
    comms
    sensors
)

target_compile_definitions(${TARGET_NAME} PRIVATE
    STM32G431xx
    BOARD_NUCLEO_G431KB
    FIRMWARE_VERSION="0.1.0"
)

target_compile_options(${TARGET_NAME} PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)

target_link_options(${TARGET_NAME} PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -T${LINKER_SCRIPT}
    -nostartfiles
    -Wl,--gc-sections
    -Wl,-Map=${TARGET_NAME}.map
)

set(ELF_FILE $<TARGET_FILE:${TARGET_NAME}>)
set(BIN_FILE ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin)
set(HEX_FILE ${CMAKE_BINARY_DIR}/${TARGET_NAME}.hex)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${ELF_FILE} ${BIN_FILE}
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${ELF_FILE} ${HEX_FILE}
    COMMENT "Generating .bin and .hex files"
)

add_custom_target(size
    COMMAND ${CMAKE_SIZE} ${ELF_FILE}
    DEPENDS ${TARGET_NAME}
)

find_program(STM32_PROGRAMMER_CLI STM32_Programmer_CLI)

if(STM32_PROGRAMMER_CLI)
    add_custom_target(flash
        COMMAND ${STM32_PROGRAMMER_CLI} -c port=SWD -w ${HEX_FILE} -v -rst
        DEPENDS ${TARGET_NAME}
        USES_TERMINAL
    )

    add_custom_target(erase
        COMMAND ${STM32_PROGRAMMER_CLI} -c port=SWD -e all
        USES_TERMINAL
    )
else()
    add_custom_target(flash
        COMMAND ${CMAKE_COMMAND} -E echo "STM32_Programmer_CLI was not found. Install STM32CubeProgrammer to flash the board."
        COMMAND ${CMAKE_COMMAND} -E false
        DEPENDS ${TARGET_NAME}
    )

    add_custom_target(erase
        COMMAND ${CMAKE_COMMAND} -E echo "STM32_Programmer_CLI was not found. Install STM32CubeProgrammer to erase the board."
        COMMAND ${CMAKE_COMMAND} -E false
    )
endif()
