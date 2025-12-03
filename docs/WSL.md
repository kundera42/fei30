# Building inside WSL2

These are the recommended steps to configure, build, and flash the firmware entirely from WSL2. They assume your repository remains on the Windows file system at `/mnt/c/Users/HAns/Documents/devel/fei30-master`.

## 1. Install packages
```bash
sudo apt update
sudo apt install -y build-essential ninja-build cmake gcc-arm-none-eabi gdb-multiarch stlink-tools
```

> **Note:** If you prefer ST's official CLI, download and install the Linux version of `STM32_Programmer_CLI` from ST, then place the binary on your `$PATH`. Otherwise the bundled `st-flash` utility from `stlink-tools` works well with the generated `.bin` file.

## 2. Configure and build
```bash
cd /mnt/c/Users/HAns/Documents/devel/fei30-master
cmake -S . -B build-wsl -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-wsl
```

This produces `stm32l432-blink.elf/bin/hex/map` under `build-wsl/`.

## 3. Flashing options
1. **Using ST's CLI** (after installing it and optionally passing the USB device through with `usbipd wsl attach --busid <bus-id>` on Windows):
   ```bash
   cmake --build build-wsl --target flash
   ```

2. **Using stlink-tools**:
   ```bash
   sudo st-flash --reset write build-wsl/stm32l432-blink.bin 0x08000000
   ```

If you cannot or do not want to expose USB devices to WSL, build inside WSL and flash from Windows by pointing `STM32_Programmer_CLI` at `build-wsl/stm32l432-blink.elf`.

## 4. Helpful extras
- Inspect binary size: `cmake --build build-wsl --target stm32l432-blink_size`
- Debugging from WSL: `gdb-multiarch build-wsl/stm32l432-blink.elf` with `target remote :3333` if you launch an OpenOCD/stlink server.
- Update submodules if needed: `git submodule update --init --depth 1 extern/STM32CubeL4/...` (same command works in WSL).
