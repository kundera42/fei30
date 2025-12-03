# Building on Windows (PowerShell)

These steps let you build and flash the firmware entirely from Windows without WSL. Commands are shown for PowerShell, but work in any terminal once the tools are on your `PATH`.

## 1. Install prerequisites
1. **Arm GNU Toolchain (`arm-none-eabi-`)**  
   Download the Windows installer (`gcc-arm-none-eabi`) from [Arm Developer](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) and install it to a path without spaces such as `C:\ArmGnuToolchain`. Add the `...\bin` folder (for example `C:\ArmGnuToolchain\13.2.Rel1\bin`) to your user or system `PATH`.
2. **CMake and Ninja**  
   Install using your preferred package manager, e.g.:
   ```powershell
   winget install --id Kitware.CMake
   winget install --id Ninja-build.Ninja
   ```
   Alternatively download the ZIP from Kitware and add `cmake.exe` + `ninja.exe` to `PATH`.
3. **ST-LINK / STM32CubeProgrammer**  
   Install [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html). The installer also offers the latest ST-LINK/V2-1 USB drivers; accept that prompt so Windows claims the debugger. Add `C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin` to `PATH` (or note the full path to `STM32_Programmer_CLI.exe` for later).

After installing the tools, open a new PowerShell window and verify that `arm-none-eabi-gcc --version`, `cmake --version`, `ninja --version`, and `STM32_Programmer_CLI --version` all succeed.

## 2. Configure and build
From PowerShell:
```powershell
cd C:\Users\HAns\Documents\devel\fei30-master
cmake -S . -B build -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake `
  -DCMAKE_BUILD_TYPE=Release `
  -DSTM32_FLASH_TOOL=stlink    # optional: keep default STM32_Programmer_CLI
cmake --build build
```

The build produces `.elf`, `.bin`, `.hex`, and `.map` files inside `build\`.

> If CMake cannot find `arm-none-eabi-gcc`, reopen PowerShell after editing your `PATH` or export it for the current session:  
> ` $Env:Path = "C:\ArmGnuToolchain\13.2.Rel1\bin;$Env:Path"`

## 3. Flashing and debugging
The default `flash` target expects `STM32_Programmer_CLI` on `PATH`:
```powershell
cmake --build build --target flash
```

If the executable lives in a directory with spaces (e.g., `C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin`), add that folder to your `PATH` so that calling `STM32_Programmer_CLI` works from any terminal. Other flashing tools (e.g., the open-source `st-flash`) still work by overriding `STM32_FLASH_TOOL` exactly as you would on Linux.

For interactive debug sessions, launch STM32CubeProgrammer or STLink Server to hold the probe open, then connect with `arm-none-eabi-gdb build\stm32l432-blink.elf` and `target remote localhost:61234` (or whichever port the server exposes).

## 4. Helpful extras
- Print image size: `cmake --build build --target stm32l432-blink_size`
- Reconfigure for Debug builds: change `-DCMAKE_BUILD_TYPE=Debug`
- Clean build artifacts: `cmake --build build --target clean` or remove the `build\` folder
- Update the HAL submodules when ST releases updates:  
  `git submodule update --init --depth 1 extern/STM32CubeL4/...` (same command works on Windows)
