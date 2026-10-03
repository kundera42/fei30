# fei30

Firmware for a NUCLEO-L432KC (STM32L432KCU6) that drives a bit-banged display. It keeps time in the RTC,
synced over NTP by an ESP-01, and reads temperature and pressure from a BMP180. Status goes out once a
second on the ST-LINK virtual COM port.

## Quick start

```
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake
cmake --build build --target flash
```

Then open the ST-LINK COM port at 115200 8N1.

For the ESP-01 firmware, first copy `esp01/include/secrets.example.h` to `esp01/include/secrets.h` and fill
in your WiFi credentials. Then run `cmake --build build --target flash-esp01`.

## Documentation

| Document | For |
|---|---|
| [docs/UM.md](docs/UM.md) | User manual: wiring, flashing, serial output, troubleshooting |
| [docs/SPM.md](docs/SPM.md) | Software programming manual: host setup, architecture, pin map, build targets, conventions, known issues |
| [docs/esp01-protocol.md](docs/esp01-protocol.md) | ESP-01 ↔ STM32 serial protocol |
| [docs/esp01-flashing.md](docs/esp01-flashing.md) | Reprogramming the ESP-01 in place |
| [docs/timesync-design.md](docs/timesync-design.md) | RTC / NTP time sync design |
| [docs/hardware/](docs/hardware/) | Carrier board issues and change requests |
| [docs/reference/](docs/reference/) | Vendor datasheets and manuals |
