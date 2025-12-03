cmake_minimum_required(VERSION 3.20)
project(stm32l432-blink C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -x assembler-with-cpp")

set(STM32CUBE_L4_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/extern/STM32CubeL4 CACHE PATH "Path to STM32CubeL4")
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32L432KCUx_FLASH.ld CACHE FILEPATH "Linker script")

if(NOT EXISTS ${STM32CUBE_L4_ROOT})
    message(FATAL_ERROR "STM32CubeL4 repository not found. Did you clone submodules?")
endif()

set(HAL_DRIVER_DIR ${STM32CUBE_L4_ROOT}/Drivers/STM32L4xx_HAL_Driver)
set(CMSIS_DEVICE_DIR ${STM32CUBE_L4_ROOT}/Drivers/CMSIS/Device/ST/STM32L4xx)
set(CMSIS_CORE_DIR ${STM32CUBE_L4_ROOT}/Drivers/CMSIS/Include)

if(NOT EXISTS ${HAL_DRIVER_DIR}/Inc/stm32l4xx_hal.h OR NOT EXISTS ${CMSIS_DEVICE_DIR}/Include/stm32l432xx.h)
    message(FATAL_ERROR "Required HAL/CMSIS submodules are missing. Run 'git submodule update --init --depth 1 extern/STM32CubeL4/Drivers/STM32L4xx_HAL_Driver extern/STM32CubeL4/Drivers/CMSIS/Device/ST/STM32L4xx'")
endif()

set(HAL_SOURCES
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_cortex.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_rcc.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_rcc_ex.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_flash.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_flash_ex.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_flash_ramfunc.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_pwr.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_pwr_ex.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_gpio.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_uart.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_uart_ex.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_dma.c
    ${HAL_DRIVER_DIR}/Src/stm32l4xx_hal_dma_ex.c
)

set(PROJECT_SOURCES
    src/main.c
    src/stm32l4xx_it.c
    src/system_stm32l4xx.c
    startup/startup_stm32l432xx.s
)

add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCES}
    ${HAL_SOURCES}
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        config
        src
        ${HAL_DRIVER_DIR}/Inc
        ${HAL_DRIVER_DIR}/Inc/Legacy
        ${CMSIS_DEVICE_DIR}/Include
        ${CMSIS_CORE_DIR}
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        USE_HAL_DRIVER
        STM32L432xx
)

target_compile_options(${PROJECT_NAME}
    PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -Wall -Wextra -Wundef -Wshadow -O3 -g3>
        $<$<COMPILE_LANGUAGE:ASM>:-mcpu=cortex-m4 -mthumb>
)

target_link_options(${PROJECT_NAME}
    PRIVATE
        -T${LINKER_SCRIPT}
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
        -Wl,-Map=$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.map
        -Wl,--gc-sections
        -Wl,--start-group
        -lc
        -lm
        -lnosys
        -Wl,--end-group
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.hex
    COMMENT "Generating binary and ihex images"
)

if(CMAKE_SIZE)
    add_custom_target(${PROJECT_NAME}_size
        COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${PROJECT_NAME}>
        DEPENDS ${PROJECT_NAME}
    )
endif()

set(STM32_FLASH_TOOL STM32_Programmer_CLI CACHE STRING "Utility used for flashing")
set_property(CACHE STM32_FLASH_TOOL PROPERTY STRINGS STM32_Programmer_CLI stlink)
set(STM32_FLASH_ADDRESS 0x08000000 CACHE STRING "Flash base address")

if(STM32_FLASH_TOOL STREQUAL "stlink")
    set(_flash_command st-flash --reset write $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.bin ${STM32_FLASH_ADDRESS})
elseif(STM32_FLASH_TOOL STREQUAL "STM32_Programmer_CLI")
    set(_flash_command ${STM32_FLASH_TOOL} -c port=SWD -w $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.elf ${STM32_FLASH_ADDRESS} -rst)
else()
    message(FATAL_ERROR "Unsupported STM32_FLASH_TOOL: ${STM32_FLASH_TOOL}")
endif()

add_custom_target(flash
    COMMAND ${_flash_command}
    DEPENDS ${PROJECT_NAME}
    COMMENT "Flashing target using ${STM32_FLASH_TOOL}"
    USES_TERMINAL
)
