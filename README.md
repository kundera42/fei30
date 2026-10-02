# STM32L432KCU6 Blink Project

This repository contains a minimal STM32Cube HAL based firmware for the STM32L432KCU6 (Nucleo-32/L432KC) that can be built and flashed with CMake.

## Prerequisites
- `arm-none-eabi-gcc`, `arm-none-eabi-binutils`, and `arm-none-eabi-gdb`  
  > Windows users: install the Arm GNU Toolchain for Windows and add its `bin` directory to `PATH` (see `docs/WINDOWS.md`).
- CMake 3.20+
- Ninja or Make (examples below assume Ninja)
- ST-LINK CLI (`STM32_Programmer_CLI`) or change the `STM32_FLASH_TOOL` cache variable to match your preferred programmer

All command examples below assume a Linux shell (WSL or native). For native Windows setup and PowerShell commands see `docs/WINDOWS.md`.

## One-time setup
```bash
git submodule update --init --depth 1 extern/STM32CubeL4 \
    extern/STM32CubeL4/Drivers/STM32L4xx_HAL_Driver \
    extern/STM32CubeL4/Drivers/CMSIS/Device/ST/STM32L4xx
```

## Configure and build
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The build generates `.elf`, `.bin`, `.hex`, and `.map` inside the `build` directory. Run `cmake --build build --target stm32l432-blink_size` to inspect the image size.

> Prefer building inside WSL2? Follow `docs/WSL.md` for package installation and Linux command examples.
>
> Prefer staying on Windows? Follow `docs/WINDOWS.md` to install the Arm GNU Toolchain, STM32CubeProgrammer/ST-LINK drivers, and run the same CMake/Ninja workflow from PowerShell.

## Flashing and debugging
Set `STM32_FLASH_TOOL` when configuring if you use a flashing tool other than `STM32_Programmer_CLI`. For example, to use the open-source `st-flash` utility from the `stlink` project:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DSTM32_FLASH_TOOL=stlink
cmake --build build --target flash
```

Flash the board via:

```bash
cmake --build build --target flash
```

The default `flash` target issues `STM32_Programmer_CLI -c port=SWD -w ... 0x08000000 -rst`.

## Customization
- Change `LED_PIN` / `LED_GPIO_PORT` inside `src/main.c` to match the LED you want to toggle.
- Edit `linker/STM32L432KCUx_FLASH.ld` if you shoehorn a different memory map.
- `stm32l4xx_hal_conf.h` enables the full HAL by default; disable modules you do not use to trim build time.

## ESP-01 (WiFi/NTP time source)
The ESP-01 on the carrier board runs the PlatformIO firmware in `esp01/` and sends NTP time over USART1
(PB6/PB7). Before the first build, copy `esp01/include/secrets.example.h` to `esp01/include/secrets.h` and fill
in your WiFi credentials. `cmake --build build --target esp01` builds it, and `--target flash-esp01` reprograms
the soldered module through the STM32 (see `docs/esp01-flashing.md`). The `flash` targets also clear `FLASH_SR.PEMPTY` after programming;
without that the L432 starts the ROM bootloader instead of the new image.
