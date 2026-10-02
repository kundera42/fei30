# ESP-01 Flashing and Bring-up Notes

How to (re)program the ESP-01 that is soldered on the carrier board, without removing it, by using the
STM32L432 as a USB-serial bridge. Written after the first successful flash on 2026-10-02.

## Where things live

| What | Where |
|---|---|
| ESP-01 firmware (WiFi + NTP, Arduino/PlatformIO) | `C:\Users\HAns\Documents\PlatformIO\Projects\esp-01` (separate repo) |
| ESP-01 build output | `.pio/build/esp01_1m/firmware.bin` in that project (`pio run`) |
| STM32 bridge firmware | `tools/esp_bridge/esp_bridge.c` (CMake target `esp-bridge`) |
| Upload script | `tools/esp_bridge/flash_esp01.py` |
| ESP-01 <-> STM32 protocol | `ESP01_STM32_INTEGRATION_GUIDE.md` |

## Hardware connections (carrier board)

```
ESP-01 TX  ->  PB7  (USART1_RX, Nucleo D4)
ESP-01 RX  <-  PB6  (USART1_TX, Nucleo D5)
ESP-01 EN/CH_PD -> 3.3V via R1 10k
ESP-01 RST      -> 3.3V via R2 10k     (no STM32 connection)
ESP-01 GPIO0    -> 3.3V via pull-up    (no STM32 connection)
ESP-01 VCC      <- carrier TLV1117LV33 (not the Nucleo 3V3)
```

The ST-LINK virtual COM port (COM3 on this PC) is wired to the STM32's USART2 (PA2 TX / PA15 RX), not to
the ESP-01. That is why a bridge is needed.

ESP-01 header (standard ESP-01/ESP-01S layout, antenna up, component side towards you; check the
silkscreen of your module, clones differ):

```
GND   GPIO2  GPIO0  RX
TX    EN     RST    VCC
```

RST sits right next to VCC: twice the ST-LINK USB dropped out while jumpering RST, most likely from
touching 3.3V. Prefer the power-cycle method below.

## Procedure

1. Build the ESP firmware: `pio run` in the esp-01 project.
2. Put the bridge on the STM32: `cmake --build build --target flash-esp-bridge`
3. Put the ESP-01 in its ROM bootloader:
   - **Preferred:** with power off, connect GPIO0 to GND, power up the carrier, then remove the jumper.
   - Alternative: hold GPIO0 to GND, tap RST to GND, release GPIO0.
4. Upload (needs `pip install esptool`, tested with 5.4):
   ```
   python tools/esp_bridge/flash_esp01.py --port COM3 <esp-01 project>/.pio/build/esp01_1m/firmware.bin
   ```
   Takes about 20 s and ends with `Hash of data verified.`
5. Reset the ESP-01 (power-cycle, GPIO0 left alone). Optional: watch COM3 at 115200 baud, the bridge is still
   running, so you should see `{"type":"time",...}` every 10 s.
6. Restore the clock application: `cmake --build build --target flash`

Quick health checks through the bridge:
- Factory AT firmware answers `AT` with `OK` (the module shipped with AT v1.7.4).
- Our firmware is silent on input but prints a `{"type":"time",...}` line every 10 s.

## Pitfalls found during bring-up

### STM32 boots the ROM bootloader after flashing
STM32CubeProgrammer's erase leaves `FLASH_SR.PEMPTY` set; the reset after programming then starts the
system-memory bootloader (PC = `0x1FFF2Dxx`, no UART output) instead of the new image.
`cmake/clear_pempty.cmake` clears the flag and resets; both `flash` and `flash-esp-bridge` run it.
The CLI prints a verify error for that register write; that is expected.

### esptool cannot connect directly on the ST-LINK COM port
`esptool --port COM3` (both 3.0 bundled with PlatformIO and 5.4) loses bytes in the middle of the eight
SYNC replies and fails with `Invalid head of packet` / `Serial data stream stopped`. The bridge is not the
cause: its USART error counters (`err_usart1/2`, readable over SWD) stay at zero and a hand-written SYNC
gets eight clean replies. Handing esptool a port object that drains the VCP in a background thread
(`flash_esp01.py`) works reliably. `pio run -t upload` therefore does not work through the bridge.

### Clock accuracy of the bridge
The bridge runs at 48 MHz from MSI with LSE PLL-mode trimming, so both UARTs are within ~0.1 % of
115200 baud. The ESP-01 ROM loader auto-bauds on the SYNC pattern, so it follows whatever the bridge sends.

## Making this easier on the next board revision
See [pcb-esp01-programming-request.md](pcb-esp01-programming-request.md): a PROG button on GPIO0 and a
RESET button on RST, plus optional STM32 control of both, so the procedure becomes "hold PROG, tap RESET".
