# ESP-01 ↔ STM32 Serial Protocol

Serial protocol between the ESP-01 WiFi/NTP firmware (`esp01/src/main.cpp`, v2.0) and the STM32. Taken
from the former `ESP01_STM32_INTEGRATION_GUIDE.md` and checked against the firmware source on 2026-10-03.

| | |
|---|---|
| Link | ESP-01 TX → PB7 (USART1_RX), ESP-01 RX ← PB6 (USART1_TX) |
| Format | 115200 baud, 8N1 |
| JSON framing | One object per line, terminated by `\n` |
| Time source | `pool.ntp.org`, UTC (offset 0), re-synced every hour |

**What the STM32 firmware uses today:** only JSON mode. It parses `{"type":"time",...}` lines (see
`process_line()` in `src/main.c`) and ignores everything else. Binary packets are not parsed.

## 1. Transmission modes

| Mode | `set_mode` value | Sends |
|---|---|---|
| JSON (default) | `"json"` | `{"type":"time",...}` line every sync interval |
| Binary | `"binary"` | 20-byte binary packet every sync interval |
| Both | `"both"` | JSON line followed by the binary packet |

The mode and interval are stored in the ESP-01's EEPROM and survive reboots. The default interval is 10 s.
Time messages are only sent once NTP has synced.

## 2. Messages from the ESP-01

| Message | Example |
|---|---|
| Boot | `{"status":"boot","version":"2.0","tx_mode":"json","sync_interval":10}` |
| WiFi connected | `{"status":"connected","ssid":"<ssid>","ip":"192.168.x.x","rssi":-69}` |
| WiFi failed | `{"status":"connect_failed","ssid":"<ssid>"}` |
| Fallback access point | `{"status":"ap_mode","ssid":"ESP01-Config","ip":"192.168.4.1"}` |
| NTP synced | `{"status":"ntp_synced","server":"pool.ntp.org"}` |
| Time | `{"type":"time","epoch":1791040206,"utc":"2026-10-03T15:10:06","year":2026,"month":10,"day":3,"hour":15,"minute":10,"second":6}` |

### Binary time packet

```c
#pragma pack(push, 1)
struct BinaryTimePacket {          // 20 bytes, little-endian
  uint32_t sync_marker;            // 0xAA55AA55
  uint32_t epoch_seconds;          // Unix time, UTC
  uint32_t microseconds;           // micros() % 1000000, see note
  uint16_t year;                   // e.g. 2026
  uint8_t  month;                  // 1-12
  uint8_t  day;                    // 1-31
  uint8_t  hour;                   // 0-23
  uint8_t  minute;                 // 0-59
  uint8_t  second;                 // 0-59
  uint8_t  checksum;               // 8-bit sum of the preceding 19 bytes
};
#pragma pack(pop)
```

Note: `microseconds` is the ESP's free-running `micros()` counter modulo one second. It is **not aligned
to the start of the UTC second**, so it gives no sub-second time.

## 3. Commands to the ESP-01

Each command is one JSON line.

| Command | Response | Persists |
|---|---|---|
| `{"cmd":"get_time"}` | A time message | – |
| `{"cmd":"get_status"}` | `{"wifi_connected":true,"ap_mode":false,"time_synced":true,"ip":"...","rssi":-69}` | – |
| `{"cmd":"get_config"}` | `{"tx_mode":0,"tx_mode_name":"json","sync_interval":10}` | – |
| `{"cmd":"set_mode","mode":"json\|binary\|both"}` | `{"status":"mode_updated","mode":"binary"}` | EEPROM |
| `{"cmd":"set_interval","interval":60}` | `{"status":"interval_updated","interval":60}`, range 1–3600 s | EEPROM |
| `{"cmd":"set_wifi","ssid":"...","pass":"..."}` | `{"status":"credentials_saved","action":"rebooting"}`, then reboots | EEPROM |
| `{"cmd":"restart"}` | `{"status":"restarting"}` | – |

## 4. Errors

| Error | Cause |
|---|---|
| `{"error":"invalid_json","message":"..."}` | Line was not valid JSON |
| `{"error":"missing_cmd"}` | No `cmd` field |
| `{"error":"unknown_command","cmd":"..."}` | Unknown `cmd` |
| `{"error":"invalid_mode"}` / `{"error":"missing_mode"}` | Bad or missing `mode` in `set_mode` |
| `{"error":"invalid_interval"}` | `interval` outside 1–3600 |
| `{"error":"missing_ssid_or_pass"}` | `set_wifi` without both fields |

## 5. Testing by hand

Flash the USART bridge (`cmake --build build --target flash-esp-bridge`), open COM3 at 115200 and type
commands. Restore the application with `cmake --build build --target flash`. See `esp01-flashing.md`.
