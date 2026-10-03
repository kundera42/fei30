# Request for PCB designer: ESP-01 programming buttons

Copy-paste the section below to the PCB designer.

---

**Context.** The carrier board has an ESP-01 (ESP8266) module soldered next to an STM32 Nucleo-32 (L432KC).
We reprogram the ESP-01 in place over its UART (through the STM32), which needs the ESP-01 to be reset
while GPIO0 is held low. Right now that requires jumper wires on the module pins, and RST sits next to
VCC: we have shorted the 3.3V rail twice by accident. Please add a safe, easy way to do this.

**Current circuit (keep as is):**
- ESP-01 EN/CH_PD: 10k pull-up (R1) to 3.3V
- ESP-01 RST: 10k pull-up (R2) to 3.3V
- ESP-01 GPIO0: pull-up to 3.3V
- ESP-01 TX -> STM32 PB7, ESP-01 RX <- STM32 PB6
- ESP-01 powered from the carrier's TLV1117LV33, not from the Nucleo

**Requested changes:**

1. **RESET button:** momentary tactile switch between ESP-01 **RST** and **GND**. Add a 100 nF capacitor from
   RST to GND for debounce and noise immunity. Label: `ESP RST`.
2. **PROG button:** momentary tactile switch between ESP-01 **GPIO0** and **GND**, with a series resistor of
   about 470 R so that pressing it while GPIO0 is driven as an output can't short the pin. Label: `ESP PROG`.
   Usage: hold PROG, tap RST, release PROG.
3. **Keep a 10k pull-up on GPIO0, and make sure GPIO2 is pulled high (10k)**, so the module always boots
   normally when no button is pressed.
4. **Optional but very welcome: let the STM32 do it automatically.** Connect two free STM32 pins to RST and
   GPIO0 through 1k series resistors, or better through small N-MOSFETs / open-drain, in parallel with
   the buttons. Candidate pins: PA0 (A0) and PA1 (A1); please confirm with me that they are free. Firmware
   will only ever pull these low, never drive them high.
5. **Place the buttons where they're easy to reach**, away from the 3.3V/5V pins, and add a 2-pin GND test
   point/header nearby for measurements.
6. **Optional:** a 3-pin header (GND / ESP-TX / ESP-RX) so a USB-serial adapter can talk to the ESP-01 directly
   when the STM32 is not fitted.

**Not needed:** auto-reset via DTR/RTS (the ST-LINK virtual COM port does not provide those lines).

---
