# ESP-01 WiFi/NTP Bridge - STM32 Integration Guide

## Overview

This document describes how to integrate the ESP-01 WiFi/NTP module with an STM32 microcontroller. The ESP-01 is already programmed and running, providing NTP-synchronized time data via UART.

## Project Locations

- **ESP-01 Project:** `C:\Users\HAns\Documents\PlatformIO\Projects\esp-01\`
- **STM32 Project:** `C:\Users\HAns\Documents\devel\fei30-master\`

## ESP-01 Current Status

✓ **Firmware is complete and running**
- Connected to WiFi network "PikaFlat24"
- NTP synchronized with pool.ntp.org
- Sending time data every 10 seconds via UART
- Listening for commands on UART

## Hardware Connection

Connect ESP-01 to STM32 via UART:

```
ESP-01          STM32
------          -----
TX (GPIO1)  →   RX (UARTx_RX)
RX (GPIO3)  ←   TX (UARTx_TX)
GND         →   GND
VCC (3.3V)  ←   3.3V
```

**UART Configuration:**
- Baud Rate: 115200
- Data Bits: 8
- Parity: None
- Stop Bits: 1

## Communication Protocol

### JSON over UART

All communication is JSON-based, terminated with `\n` (newline).

### Messages FROM ESP-01 to STM32

**1. Boot Message:**
```json
{"status":"boot","version":"1.0"}
```

**2. WiFi Connected:**
```json
{"status":"connected","ssid":"PikaFlat24","ip":"192.168.178.91","rssi":-69}
```

**3. NTP Synchronized:**
```json
{"status":"ntp_synced","server":"pool.ntp.org"}
```

**4. Time Data (sent every 10 seconds automatically):**
```json
{
  "type":"time",
  "epoch":1764278455,
  "utc":"2025-11-27T21:20:55",
  "year":2025,
  "month":11,
  "day":27,
  "hour":21,
  "minute":20,
  "second":55
}
```

**5. AP Mode (fallback if WiFi fails):**
```json
{"status":"ap_mode","ssid":"ESP01-Config","ip":"192.168.4.1"}
```

**6. Connection Failed:**
```json
{"status":"connect_failed","ssid":"PikaFlat24"}
```

### Commands FROM STM32 to ESP-01

**1. Request Current Time:**
```json
{"cmd":"get_time"}
```
Response: Time data message (as shown above)

**2. Get WiFi/NTP Status:**
```json
{"cmd":"get_status"}
```
Response:
```json
{
  "wifi_connected":true,
  "ap_mode":false,
  "time_synced":true,
  "ip":"192.168.178.91",
  "rssi":-69
}
```

**3. Set WiFi Credentials (if needed):**
```json
{"cmd":"set_wifi","ssid":"NetworkName","pass":"Password123"}
```
Response:
```json
{"status":"credentials_saved","action":"rebooting"}
```
ESP-01 will reboot and connect to the new network.

**4. Restart ESP-01:**
```json
{"cmd":"restart"}
```
Response:
```json
{"status":"restarting"}
```

### Error Responses

**Invalid JSON:**
```json
{"error":"invalid_json","message":"error description"}
```

**Missing Command:**
```json
{"error":"missing_cmd"}
```

**Unknown Command:**
```json
{"error":"unknown_command","cmd":"bad_command"}
```

## STM32 Implementation Requirements

### 1. UART Configuration

Configure a UART peripheral with:
- 115200 baud
- 8N1 (8 data bits, no parity, 1 stop bit)
- Enable RX interrupt or use DMA for receiving

### 2. JSON Parsing

You'll need a JSON parser for STM32. Options:
- **cJSON** - Lightweight C library
- **jsmn** - Minimal JSON parser
- **Embedded Template Library (ETL)** - For C++

### 3. Receive Buffer

Implement line-buffered reception:
- Buffer incoming characters until `\n`
- Parse complete JSON line
- Maximum message length: ~256 bytes

### 4. Example STM32 Code Structure

```c
// Pseudo-code structure for STM32

#include "cJSON.h"  // or your chosen JSON library

#define ESP01_UART huart2  // Adjust to your UART
#define RX_BUFFER_SIZE 512

char rx_buffer[RX_BUFFER_SIZE];
uint16_t rx_index = 0;

typedef struct {
    uint32_t epoch;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool valid;
} TimeData_t;

TimeData_t current_time = {0};

// UART Receive Callback (called per character or line)
void UART_RxCallback(char received_char) {
    if (received_char == '\n') {
        // Null-terminate the buffer
        rx_buffer[rx_index] = '\0';

        // Parse JSON
        parseESP01Message(rx_buffer);

        // Reset buffer
        rx_index = 0;
    } else if (rx_index < RX_BUFFER_SIZE - 1) {
        rx_buffer[rx_index++] = received_char;
    }
}

void parseESP01Message(const char* json_string) {
    cJSON *json = cJSON_Parse(json_string);
    if (json == NULL) {
        return; // Invalid JSON
    }

    // Check if this is a time message
    cJSON *type = cJSON_GetObjectItem(json, "type");
    if (type != NULL && strcmp(type->valuestring, "time") == 0) {
        // Extract time data
        cJSON *epoch = cJSON_GetObjectItem(json, "epoch");
        cJSON *year = cJSON_GetObjectItem(json, "year");
        cJSON *month = cJSON_GetObjectItem(json, "month");
        cJSON *day = cJSON_GetObjectItem(json, "day");
        cJSON *hour = cJSON_GetObjectItem(json, "hour");
        cJSON *minute = cJSON_GetObjectItem(json, "minute");
        cJSON *second = cJSON_GetObjectItem(json, "second");

        if (epoch && year && month && day && hour && minute && second) {
            current_time.epoch = epoch->valueint;
            current_time.year = year->valueint;
            current_time.month = month->valueint;
            current_time.day = day->valueint;
            current_time.hour = hour->valueint;
            current_time.minute = minute->valueint;
            current_time.second = second->valueint;
            current_time.valid = true;

            // Optional: Set RTC or use time data
            updateSystemTime(&current_time);
        }
    }

    // Check for status messages
    cJSON *status = cJSON_GetObjectItem(json, "status");
    if (status != NULL) {
        if (strcmp(status->valuestring, "boot") == 0) {
            // ESP-01 just booted
        } else if (strcmp(status->valuestring, "connected") == 0) {
            // ESP-01 connected to WiFi
        } else if (strcmp(status->valuestring, "ntp_synced") == 0) {
            // NTP sync successful
        }
    }

    cJSON_Delete(json);
}

// Send command to ESP-01
void ESP01_SendCommand(const char* json_cmd) {
    HAL_UART_Transmit(&ESP01_UART, (uint8_t*)json_cmd, strlen(json_cmd), 1000);
    HAL_UART_Transmit(&ESP01_UART, (uint8_t*)"\n", 1, 100);
}

// Example: Request time on demand
void ESP01_RequestTime(void) {
    ESP01_SendCommand("{\"cmd\":\"get_time\"}");
}

// Example: Check ESP-01 status
void ESP01_GetStatus(void) {
    ESP01_SendCommand("{\"cmd\":\"get_status\"}");
}
```

## Integration Steps for STM32

1. **Configure UART**
   - Use STM32CubeMX or direct register configuration
   - Set to 115200 baud, 8N1
   - Enable RX interrupt or DMA

2. **Add JSON Library**
   - Download cJSON from: https://github.com/DaveGamble/cJSON
   - Add `cJSON.c` and `cJSON.h` to your project

3. **Implement RX Buffer Handler**
   - Collect characters until `\n`
   - Call JSON parser on complete lines

4. **Parse Time Messages**
   - Extract time fields from JSON
   - Update your RTC or time variables

5. **Optional: Send Commands**
   - Implement command functions if you need to request time on-demand

## Example Usage Flow

```
1. STM32 boots up
2. ESP-01 sends: {"status":"boot","version":"1.0"}
3. ESP-01 sends: {"status":"connected",...}
4. ESP-01 sends: {"status":"ntp_synced",...}
5. ESP-01 sends time data every 10 seconds
6. STM32 parses and uses time data
7. Optional: STM32 can send {"cmd":"get_time"} to request immediate update
```

## Testing

### Test with Serial Monitor First

Before integrating with STM32, verify ESP-01 is working:
1. Connect USB-to-Serial adapter to ESP-01 UART
2. Open serial monitor at 115200 baud
3. Watch for automatic time messages
4. Send test command: `{"cmd":"get_status"}`

### STM32 Integration Testing

1. Connect ESP-01 to STM32 UART
2. Implement basic UART echo first to verify connection
3. Add JSON parser and test with known messages
4. Verify time data reception
5. Test command sending (optional)

## Troubleshooting

**No data from ESP-01:**
- Check 3.3V power supply (needs 200mA+)
- Verify RX/TX are not swapped
- Check baud rate is 115200
- Verify GND connection

**Garbled data:**
- ESP-01 sends boot messages at different baud rate initially (normal)
- Wait for JSON messages (they start with `{`)
- Check UART configuration (8N1, no flow control)

**ESP-01 in AP mode:**
- WiFi credentials not configured or connection failed
- Can send new credentials via: `{"cmd":"set_wifi","ssid":"...","pass":"..."}`

**Time not updating:**
- Check ESP-01 is connected to WiFi (watch for status messages)
- Verify NTP sync message received
- Check internet connectivity on WiFi network

## ESP-01 Configuration

The ESP-01 firmware has these hardcoded defaults:
- **Default WiFi:** "PikaFlat24" / "***REMOVED***"
- **AP Mode SSID:** "ESP01-Config" (if WiFi fails)
- **NTP Server:** pool.ntp.org
- **Time Broadcast:** Every 10 seconds
- **NTP Update:** Every hour

## Advanced Features

### Changing WiFi Credentials

From STM32, send:
```json
{"cmd":"set_wifi","ssid":"NewNetwork","pass":"NewPassword"}
```

ESP-01 will save to EEPROM and reboot.

### Getting Immediate Time Update

Instead of waiting for automatic broadcast:
```json
{"cmd":"get_time"}
```

### Checking Connection Status

To verify WiFi and NTP status:
```json
{"cmd":"get_status"}
```

## Summary

The ESP-01 is a complete, autonomous WiFi/NTP module. Your STM32 just needs to:
1. Configure UART at 115200 baud
2. Receive line-buffered JSON messages
3. Parse the `"type":"time"` messages
4. Use the time data (epoch, year, month, day, hour, minute, second)

The ESP-01 handles all WiFi and NTP complexity automatically.

---

**Document Version:** 1.0
**Date:** 2025-11-27
**ESP-01 Firmware Version:** 1.0
