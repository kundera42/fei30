# Software Programming Manual (SPM)

Firmware for the fei30 clock / display controller on a NUCLEO-L432KC (STM32L432KCU6).
This manual is for whoever builds, changes or debugs the firmware. For operating the device, see
[UM.md](UM.md).

| | |
|---|---|
| Target | STM32L432KCU6, NUCLEO-L432KC (MB1180), Cortex-M4F, 256 KB flash, 64 KB SRAM |
| Toolchain | Arm GNU Toolchain (`arm-none-eabi-gcc` 14.3), CMake ≥ 3.20, Ninja |
| HAL | STM32L4xx HAL driver v1.13.6, vendored in `extern/stm32_hal` |
| Programmer | STM32CubeProgrammer CLI (`STM32_Programmer_CLI`, tested with 2.21.0) over the on-board ST-LINK |

## 1. Document map

| Document | Contents |
|---|---|
| `docs/SPM.md` (this) | Architecture, pin map, build and flash, conventions, known issues |
| `docs/UM.md` | Wiring, serial console output, troubleshooting for users |
| `docs/timesync-design.md` | RTC / LSE / NTP synchronisation design |
| `docs/esp01-protocol.md` | ESP-01 ↔ STM32 serial protocol (JSON and binary) |
| `docs/esp01-flashing.md` | Reprogramming the soldered ESP-01 through the STM32 |
| `docs/hardware/board-v1.md` | Issues with the v1 carrier board to fix in the next revision |
| `docs/hardware/pcb-esp01-programming-request.md` | Request to the PCB designer: ESP-01 PROG/RESET buttons |
| `docs/reference/` | Vendor documents: RM0394 (reference manual), UM1956 (Nucleo-32), STM32L432KC datasheet, ST oscillator design guide (LSE), Ferranti FE130 display notes |

## 2. Hardware platform

### 2.1 Clocks

| Clock | Source | Frequency | Notes |
|---|---|---|---|
| SYSCLK / HCLK | PLL from MSI 4 MHz (×40 ÷2) | 80 MHz | MSI is trimmed by LSE (MSI PLL mode) |
| PCLK1, PCLK2 | HCLK ÷1 | 80 MHz | I2C3 and the USARTs are clocked from PCLK |
| RTC | LSE | 32.768 kHz | See `docs/timesync-design.md` |
| Flash latency | | 4 WS | |

### 2.2 Pin map

| MCU pin | Nucleo pin | Function | Peripheral | Module |
|---|---|---|---|---|
| PA2 | A7 | VCP TX → ST-LINK (COM port) | USART2_TX, DMA1 Ch7 | `main.c` status output |
| PA15 | – | VCP RX (not used) | USART2_RX | |
| PB6 | D5 | → ESP-01 RX | USART1_TX | ESP link |
| PB7 | D4 | ← ESP-01 TX | USART1_RX, DMA1 Ch5 | ESP link |
| PA7 | A6 | BMP180 SCL | I2C3_SCL (AF4, open-drain) | `bmp180.c` |
| PB4 | D12 | BMP180 SDA | I2C3_SDA (AF4, open-drain) | `bmp180.c` |
| PA10 | D0 | Display DATA | GPIO bit-bang | `spi_bitbang_master.h` |
| PA11 | D10 | Display CLOCK | GPIO bit-bang | `spi_bitbang_master.h` |
| PA8 | D9 | Debug output, toggles on each ESP-01 message | GPIO | `main.c` |
| PH3 | – | BOOT0, 10 kΩ pull-down on the Nucleo | | see §7.1 |

Pin constraints worth knowing before moving anything:
- On the Nucleo-32, solder bridges SB16/SB18 tie PB6↔PA6 (A5) and PB7↔PA5 (A4) by default. PA5 and PA6
  therefore carry the ESP-01 UART signals and must not be used as outputs.
- I2C1 cannot be used: its pins are PB6/PB7 (ESP-01 UART) or PA9/PA10 (PA10 is the display). That is why
  the BMP180 is on I2C3.
- The user LED LD3 is on PB3. `main.c` still has a `LED_PIN` define for PA5, which is unused.

### 2.3 Peripherals and interrupts

| Peripheral | Use | Mode | IRQ priority |
|---|---|---|---|
| USART1 | ESP-01 link, 115200 8N1 | DMA RX with idle-line detection (`HAL_UARTEx_ReceiveToIdle_DMA`) | USART1 5, DMA1_Ch5 5 |
| USART2 | ST-LINK VCP, 115200 8N1 | DMA TX | USART2 6, DMA1_Ch7 6 |
| I2C3 | BMP180, 100 kHz (`Timing = 0x10909CEC`) | Polled, 10 ms timeout per transfer | – |
| RTC | Calendar | LSE, sync from ESP-01 NTP | – |

## 3. Source layout

```
src/main.c                 init, super-loop, ESP-01 line parser, status output, clock face drawing
src/rtc_manager.[ch]       RTC on LSE, NTP sync from ESP-01
src/bmp180.[ch]            BMP180 temperature/pressure driver (non-blocking)
src/spi_bitbang_master.h   display protocol on PA10/PA11
src/stm32l4xx_it.c         interrupt handlers
config/                    stm32l4xx_hal_conf.h, stm32l4xx_it.h
startup/, linker/          startup code and linker script
extern/stm32_hal/          vendored HAL + CMSIS (see §6)
esp01/                     ESP-01 firmware (PlatformIO)
tools/esp_bridge/          USART bridge firmware + upload script for the ESP-01
cmake/                     toolchain file, clear_pempty.cmake
```

## 4. Build and flash

### 4.1 Host setup (Windows)

1. **Arm GNU Toolchain:** install from [Arm Developer](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
   into a path without spaces, and add its `bin` folder to `PATH`.
2. **CMake and Ninja:** `winget install --id Kitware.CMake` and `winget install --id Ninja-build.Ninja`.
3. **STM32CubeProgrammer:** install from [st.com](https://www.st.com/en/development-tools/stm32cubeprog.html)
   and accept the ST-LINK driver prompt. Add
   `C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin` to `PATH`.
4. **Optional, for the ESP-01:** PlatformIO (found automatically under `%USERPROFILE%\.platformio`) and
   Python 3 (the `esptool-venv` target creates its own virtualenv). Copy
   `esp01/include/secrets.example.h` to `esp01/include/secrets.h` (gitignored) and fill in the WiFi
   credentials.

Open a new terminal and check that `arm-none-eabi-gcc --version`, `cmake --version`, `ninja --version` and
`STM32_Programmer_CLI --version` all work. No submodules or other one-time steps are needed: the HAL is in
the repo.

WSL or Linux works the same way with `gcc-arm-none-eabi`, `cmake` and `ninja-build` from the package
manager. To flash from WSL, either pass the ST-LINK through with `usbipd`, or configure with
`-DSTM32_FLASH_TOOL=stlink` and use `st-flash` from `stlink-tools`. The PEMPTY fix (§7.1) needs
`STM32_Programmer_CLI` either way.

### 4.2 Configure, build and flash

Configure once:

```
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake
```

| Target | What it does |
|---|---|
| `cmake --build build` | Build `stm32l432-blink.elf/.bin/.hex/.map` |
| `--target stm32l432-blink_size` | Print section sizes |
| `--target flash` | Program over SWD, reset, then run `cmake/clear_pempty.cmake` (§7.1) |
| `--target esp01` | Build the ESP-01 firmware with PlatformIO |
| `--target flash-esp01` | Bridge → upload ESP-01 → restore application (`docs/esp01-flashing.md`) |
| `--target flash-esp-bridge` | Put only the USART bridge on the STM32 |

The compile flags include `-Wall -Wextra -Wundef -Wshadow`. The HAL headers produce many `-Wundef` warnings
about `USE_HAL_*_REGISTER_CALLBACKS`; they are harmless. New code should compile without warnings.

`build/`, `.vscode/` and `.claude/settings.local.json` are local and not tracked by git.

The vendored HAL must keep its CRLF line endings. If `git status` shows HAL files as modified while
`git diff` is empty, an editor has rewritten their line endings: restore them with `git checkout -- <file>`.

## 5. Firmware architecture

### 5.1 Start-up

`main()` runs `HAL_Init` → `SystemClock_Config` → `rtc_init` → GPIO → DMA → USART1 → USART2 → I2C3 →
`bmp180_init` → start USART1 DMA reception, then enters the super-loop.

### 5.2 Super-loop

The loop never blocks, because the display has to be refreshed continuously. Each pass:

1. `bmp180_poll()`: advances the sensor state machine, at most one short I2C transfer.
2. Every 1000 ms: `send_status_usart2()` sends the RTC time and the latest BMP180 reading to the VCP in one
   DMA transfer.
3. Draws the clock face (hour markers and hands) through `bitbang_character()`.

Rule for new code: **no `HAL_Delay()` or busy-waits in the loop.** Wait with `HAL_GetTick()` and a state
machine, the way `bmp180.c` does.

### 5.3 Interrupt context

- `HAL_UARTEx_RxEventCallback` (USART1): terminates the received line, parses `{"type":"time",...}`,
  updates `esp_time`, calls `rtc_sync_from_esp()`, echoes the line to USART2 and restarts reception.
- `HAL_UART_TxCpltCallback` / `HAL_UART_ErrorCallback` (USART2): clear `echo_busy` and `status_tx_busy`.

USART2 has two writers (the echo from the ISR and the status line from the loop). The busy flags keep a
new transfer from starting while one is running. A status line is skipped, not queued, if the port is
busy.

### 5.4 Modules

**RTC manager** (`rtc_manager.h`): `rtc_init`, `rtc_get_time`, `rtc_set_time`, `rtc_sync_from_esp`,
`rtc_get_status`. Design in `docs/timesync-design.md`.

**BMP180** (`bmp180.h`):

| Function | Description |
|---|---|
| `bmp180_init(&hi2c3, oss, interval_ms)` | Checks the chip ID (0x55) and reads the calibration EEPROM. Returns -1 if the sensor is missing, but polling then keeps retrying. |
| `bmp180_poll()` | State machine: IDLE → temperature conversion (5 ms) → pressure conversion (5–26 ms by oversampling) → compensate. Re-probes every interval after any I2C error. |
| `bmp180_get(&r)` | Copies the latest reading; returns 0 if valid |
| `bmp180_latest` | Global copy of the latest reading, for debugger live-watch |

The application uses oversampling setting 3 and a 1 Hz interval. The compensation is the datasheet integer
algorithm; it reproduces the datasheet's worked example (T = 150 → 15.0 °C, p = 69964 Pa). Altitude is
based on `BMP180_SEA_LEVEL_PA` (101325 Pa). I2C address 0x77.

**Display** (`spi_bitbang_master.h`): `bitbang_character(ch, y, x)` sends preamble, sync byte 0x02,
character, Y, X and postamble on PA10/PA11. Coordinates are signed 8-bit with (0,0) at the centre.

**ESP-01 link**: the ESP-01 sends a JSON time line every 10 s by default. The protocol and commands are in
`docs/esp01-protocol.md`.

### 5.5 Formatting output

`--specs=nano.specs` is used, so `printf`/`snprintf` have **no `%f` support**. Format fixed-point integers
instead (see the BMP180 line in `send_status_usart2()`), or link with `-u _printf_float` if the extra flash
is acceptable.

## 6. HAL vendoring convention

`extern/stm32_hal` holds only the HAL sources the firmware uses, plus all HAL headers and CMSIS. To add a
HAL module:

1. Fetch the `.c` files from GitHub `STMicroelectronics/stm32l4xx-hal-driver`, tag **v1.13.6**:
   `https://raw.githubusercontent.com/STMicroelectronics/stm32l4xx-hal-driver/v1.13.6/Src/<file>`.
   (Downloading through the STM32CubeL4 repo fails: its driver folder is only a submodule link.)
2. Convert to CRLF line endings to match the vendored tree.
3. Add the files to `HAL_SOURCES` in `CMakeLists.txt`.
4. Check that the module's `HAL_*_MODULE_ENABLED` is defined in `config/stm32l4xx_hal_conf.h`.

Do not copy from a local STM32Cube repository: `STM32Cube_FW_L4_V1.18.1` contains HAL v1.13.5.

## 7. Known issues and errata

### 7.1 Board boots the ROM bootloader after flashing (FLASH_SR.PEMPTY)

**Symptom:** after flashing there is no output on the COM port. Over SWD the PC reads `0x1FFFxxxx`
(system memory) instead of `0x080xxxxx`.

**Root cause** (RM0394 §3.3.1 "Empty check" and the FLASH_SR register description):
- At power-on reset, and at option-byte load (OBL_LAUNCH) only, the chip checks address 0x08000000. If it
  reads 0xFFFFFFFF, it sets `FLASH_SR.PEMPTY` (0x40022010, bit 17).
- While PEMPTY is set and boot from main flash is selected (BOOT0 = 0, the case on this board), **every
  reset boots the ROM bootloader**, even after the flash has been programmed. A system reset, including the
  programmer's `-rst`, does not re-check the flash.
- Programming or erasing the flash does not change PEMPTY. The flag gets set when the board is powered up
  while the flash is erased, for example after a mass erase or an interrupted flash, followed by a power
  cycle.
- **Writing 1 to PEMPTY toggles it; it does not clear it.** A blind "clear" sets the flag when it was
  already 0. Until 2026-10-03 the flash script did exactly that, so flashes alternated between booting the
  app and booting the bootloader.
- ST's flash loaders (CubeProgrammer, CubeIDE, IAR, Keil) are known not to clear PEMPTY after programming;
  see the references below.

**Not caused by:** jumpers or boot pins. PH3/BOOT0 has a 10 kΩ pull-down on the Nucleo (UM1956), and the
option bytes are at factory defaults (`nBOOT1=1`, `nSWBOOT0=1`, `nBOOT0=1`). USB vs. external power and USB
enumeration only matter in that any power-up repeats the empty check; with programmed flash, a power cycle
clears the flag.

**Handling in this repo:** `cmake/clear_pempty.cmake` runs after `flash`, `flash-esp-bridge` and
`flash-esp01`. It reads FLASH_SR and, only if PEMPTY is set, writes 1, checks that the flag cleared (it
fails the build otherwise) and resets the target. The CLI reports a write-verify error on that register;
that is expected.

**Manual recovery:** either power-cycle the board (unplug USB), or:

```
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 0x40022010 4      # bit 17 set?
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -w32 0x40022010 0x00020000   # only if it is set
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -rst
```

**Permanent alternative (not applied):** option bytes `nSWBOOT0=0`, `nBOOT0=1` force boot from main flash
and disable the empty check. The cost is that BOOT0 can no longer start the ROM bootloader. The change can
be reverted over SWD.

References:
- RM0394 Rev 5 §3.3.1 "Empty check" and the FLASH_SR register description (in `docs/reference/`)
- [ST Community: Intermittent break at 0x1fff51f4 (flash loaders not clearing PEMPTY)](https://community.st.com/stm32cubeide-mcus-28/intermittent-break-at-address-0x1fff51f4-with-no-debug-34748)
- [ST Community: Bootloader invocation every other debug session (STM32L431)](https://community.st.com/t5/stm32-mcus-products/bizarre-bootloader-invocation-every-other-debug-session/td-p/829749)

### 7.2 RTC restarts at 2025-01-01 after flashing

After a reflash the RTC shows `2025-01-01 00:00:00` until the next ESP-01 time message, up to 10 s later.
The cause has not been investigated yet. `rtc_init()` writes the default time when the init flag in
`RTC_BKP_DR0` is missing.

### 7.3 esptool cannot talk to the ESP-01 directly on the VCP

esptool loses bytes on the ST-LINK VCP; `tools/esp_bridge/flash_esp01.py` works around it. Details in
`docs/esp01-flashing.md`.

## 8. Change log

| Date | Change |
|---|---|
| 2026-10-03 | BMP180 (HW-706 breakout) on I2C3, readings in the 1 Hz VCP status line; I2C HAL v1.13.6 vendored |
| 2026-10-03 | PEMPTY root cause documented; `clear_pempty.cmake` reads before writing |
| 2026-10-03 | ESP-01 firmware brought into the repo with one-command flashing |
| 2026-10-02 | LSE-based RTC with NTP sync; in-place ESP-01 flashing |
