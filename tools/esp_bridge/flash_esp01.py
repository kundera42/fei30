"""Flash the soldered ESP-01 through the STM32 UART bridge (ST-LINK VCP <-> STM32 <-> ESP-01).

Plain `esptool --port COMx` drops bytes on the ST-LINK virtual COM port during SYNC and never connects.
Draining the port from a background thread in large reads and handing esptool that port object works.

Prerequisites:
  - STM32 runs the bridge:  cmake --build build --target flash-esp-bridge
  - ESP-01 is in its ROM bootloader (GPIO0 held low during power-up / reset)
  - pip install esptool   (tested with esptool 5.4)

Usage:
  python tools/esp_bridge/flash_esp01.py --port COM3                 # just identify the chip
  python tools/esp_bridge/flash_esp01.py --port COM3 firmware.bin    # write + verify at 0x0
"""

import argparse
import threading
import time

import esptool
import serial


class BufferedPort:
    """Minimal pyserial-like port that buffers everything the VCP delivers in a reader thread."""

    def __init__(self, name, baud=115200):
        self._s = serial.Serial(name, baud, timeout=0.01)
        self._buf = bytearray()
        self._cv = threading.Condition()
        self._run = True
        self._baud = baud
        self.port = self.name = name
        self.timeout = 3.0
        self.write_timeout = 10
        self.dtr = self.rts = False
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while self._run:
            data = self._s.read(4096)
            if data:
                with self._cv:
                    self._buf += data
                    self._cv.notify_all()

    def read(self, n=1):
        end = time.time() + (self.timeout or 0)
        with self._cv:
            while len(self._buf) < n and time.time() < end:
                self._cv.wait(max(0.0, end - time.time()))
            out = bytes(self._buf[:n])
            del self._buf[:n]
            return out

    @property
    def in_waiting(self):
        with self._cv:
            return len(self._buf)

    def inWaiting(self):
        return self.in_waiting

    def write(self, data):
        return self._s.write(data)

    def flush(self):
        self._s.flush()

    def reset_input_buffer(self):
        with self._cv:
            self._buf.clear()

    flushInput = reset_input_buffer

    def reset_output_buffer(self):
        self._s.reset_output_buffer()

    flushOutput = reset_output_buffer

    @property
    def baudrate(self):
        return self._baud

    @baudrate.setter
    def baudrate(self, baud):
        self._baud = baud
        self._s.baudrate = baud

    # The bridge has no control lines to the ESP-01; reset is done by hand.
    def setDTR(self, _):
        pass

    def setRTS(self, _):
        pass

    def close(self):
        self._run = False
        time.sleep(0.05)
        self._s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="COM3", help="ST-LINK virtual COM port (default COM3)")
    ap.add_argument("firmware", nargs="?", help="ESP-01 image to write at 0x0 (e.g. .pio/build/esp01_1m/firmware.bin)")
    args = ap.parse_args()

    port = BufferedPort(args.port)
    try:
        esp = esptool.cmds.detect_chip(port, 115200, "no-reset", False, 4)
        esp = esp.run_stub()
        mac = ":".join(f"{b:02x}" for b in esp.read_mac())
        print(f"Chip: {esp.get_chip_description()}  MAC: {mac}")
        if args.firmware:
            # keep flash mode/size from the image header produced by PlatformIO
            esptool.cmds.write_flash(esp, [(0x0, args.firmware)])
            print("Done. Reset the ESP-01 with GPIO0 released to run the new firmware.")
    finally:
        port.close()


if __name__ == "__main__":
    main()
