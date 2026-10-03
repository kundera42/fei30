# User Manual (UM)

How to wire up, power, flash and read out the fei30 controller: a NUCLEO-L432KC with an ESP-01 WiFi
time source, a BMP180 temperature/pressure sensor and the bit-banged display.
For firmware internals, see [SPM.md](SPM.md).

## 1. What the device does

- Keeps time in the STM32 real-time clock (32.768 kHz crystal) and corrects it from internet time (NTP)
  that the ESP-01 sends.
- Measures temperature and air pressure once a second with a BMP180 sensor.
- Draws a clock face on the display.
- Reports time and sensor readings once a second over USB (the ST-LINK virtual COM port).

## 2. What you need

| Item | Notes |
|---|---|
| NUCLEO-L432KC | With the carrier board holding the ESP-01 |
| ESP-01 / ESP-01S | Programmed with the firmware from `esp01/` and your WiFi credentials |
| HW-706 BMP180 breakout | Has its own pull-ups and regulator |
| Display | Connected to the DATA/CLOCK lines |
| USB cable | Powers the board and provides programming and the COM port |
| PC tools | STM32CubeProgrammer, Arm GNU Toolchain, CMake, Ninja (see SPM §4.1) |

## 3. Wiring

Nucleo pin names are the labels printed next to the header pins.

### 3.1 BMP180 (HW-706)

| HW-706 | Nucleo | MCU pin |
|---|---|---|
| VCC | 3V3 | – |
| GND | GND | – |
| SCL | A6 | PA7 |
| SDA | D12 | PB4 |

### 3.2 ESP-01 (on the carrier board)

| ESP-01 | Nucleo | MCU pin |
|---|---|---|
| TX | D4 | PB7 |
| RX | D5 | PB6 |
| VCC | carrier 3.3 V regulator (not the Nucleo 3V3) | – |

### 3.3 Display

| Signal | Nucleo | MCU pin |
|---|---|---|
| DATA | D0 | PA10 |
| CLOCK | D10 | PA11 |

### 3.4 Pins you must leave free

A4 and A5 are bridged on the Nucleo to D4/D5, the ESP-01 serial lines. Don't connect anything else to
them.

## 4. Flashing the firmware

From the project folder:

```
cmake --build build --target flash
```

Afterwards the command prints one of these lines:

- `FLASH_SR.PEMPTY not set, nothing to do`: normal.
- `FLASH_SR.PEMPTY was set: cleared it and reset target`: the board would otherwise have started ST's
  built-in bootloader instead of the firmware; the script fixed that. See SPM §7.1.

To update the ESP-01 firmware, follow `docs/esp01-flashing.md`.

## 5. Reading the output

Open the ST-LINK virtual COM port (COM3 on the development PC; check Device Manager under "STMicroelectronics
STLink Virtual COM Port") at **115200 baud, 8 data bits, no parity, 1 stop bit**. Any terminal works:
PuTTY, Tera Term, the VS Code serial monitor.

Once a second:

```
RTC: 2026-10-03 15:10:06
BMP180: T=30.0 C P=1031.04 hPa Alt=-147.1 m
```

Every 10 s or so the ESP-01's time message is also passed through:

```
{"type":"time","epoch":1791040206,"utc":"2026-10-03T15:10:06","year":2026,"month":10,"day":3,"hour":15,"minute":10,"second":6}
```

| Field | Meaning |
|---|---|
| `RTC:` | Board time in UTC. |
| `T=` | Temperature in °C, 0.1 °C resolution, measured by the sensor chip itself. |
| `P=` | Absolute air pressure in hPa (not corrected to sea level). |
| `Alt=` | Altitude estimated from pressure against the standard 1013.25 hPa. Only meaningful relative to that reference; on a high-pressure day it reads below zero. |
| `BMP180: no sensor` | The sensor did not answer. The firmware retries every second, so you can connect it while the board runs. |

The time is UTC: the ESP-01 uses a UTC offset of 0.

## 6. Interpreting sensor readings

- **Temperature reads high:** the BMP180 measures its own temperature. Next to the Nucleo, the regulator or
  in a closed box it reads a few degrees above the room.
- **Pressure noise:** ±0.1 hPa from one reading to the next is normal.
- **Comparing with weather reports:** weather stations report pressure reduced to sea level (QNH). Your
  reading is lower by about 0.12 hPa per metre of elevation.

## 7. Troubleshooting

| Symptom | Likely cause | What to do |
|---|---|---|
| No output on the COM port after flashing | Board started ST's built-in bootloader (PEMPTY) | Unplug and replug USB, or see SPM §7.1 |
| No output at all | Wrong COM port or baud rate | Check Device Manager; use 115200 8N1 |
| `BMP180: no sensor` | Wiring, SDA/SCL swapped, or no power to the breakout | Check §3.1; SCL is A6, SDA is D12 |
| RTC shows `2025-01-01` | Board was just reflashed or reset and hasn't heard from the ESP-01 yet | Wait up to 10 s for the next ESP-01 time message |
| RTC never updates | ESP-01 not on WiFi, or wrong credentials | Check `esp01/include/secrets.h`; watch for `{"type":"time"...}` lines |
| Altitude strongly negative | High air pressure | Expected, see §5 |
