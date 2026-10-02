# ESP-01 WiFi/NTP Bridge - STM32 Integration Guide

## Overview

This document describes how to integrate the ESP-01 WiFi/NTP module with an STM32 microcontroller for **real-time applications**. The ESP-01 is programmed to provide NTP-synchronized time data via UART, supporting both JSON and efficient binary formats optimized for DMA reception without interrupts.

## Project Locations

- **ESP-01 Project:** `C:\Users\HAns\Documents\PlatformIO\Projects\esp-01\`
- **STM32 Project:** `C:\Users\HAns\Documents\devel\fei30-master\`

## ESP-01 Current Status

✓ **Firmware Version 2.0 is complete and running**
- Connected to WiFi network "PikaFlat24"
- NTP synchronized with pool.ntp.org
- Configurable transmission modes: JSON, Binary, or Both
- Configurable sync interval (1-3600 seconds)
- Binary format optimized for DMA and real-time systems
- Listening for commands on UART

## Key Features for Real-Time Integration

✅ **Binary time packets with fixed structure** - Easy DMA parsing without string processing
✅ **No interrupts required on STM32** - Use DMA + polling for reception
✅ **Configurable sync intervals** - Reduce overhead (sync every 1-600 seconds)
✅ **Microsecond precision** - High-resolution timestamps
✅ **Checksum validation** - Ensure data integrity
✅ **Backward compatible** - Supports original JSON format

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

The ESP-01 supports **two transmission formats**:
1. **JSON Mode** - Human-readable, backward compatible with v1.0
2. **Binary Mode** - Optimized for DMA, fixed 20-byte packets, no parsing required
3. **Both Mode** - Sends both JSON and Binary (for debugging/transition)

### Binary Time Packet Format (Recommended for Real-Time)

Fixed 20-byte structure, ideal for DMA circular buffer:

```c
#pragma pack(push, 1)
struct BinaryTimePacket {
  uint32_t sync_marker;     // 0xAA55AA55 (sync marker for DMA)
  uint32_t epoch_seconds;   // Unix timestamp (seconds since 1970-01-01)
  uint32_t microseconds;    // Microseconds within current second (0-999999)
  uint16_t year;            // Full year (e.g., 2025)
  uint8_t  month;           // Month (1-12)
  uint8_t  day;             // Day of month (1-31)
  uint8_t  hour;            // Hour (0-23)
  uint8_t  minute;          // Minute (0-59)
  uint8_t  second;          // Second (0-59)
  uint8_t  checksum;        // Simple sum of all bytes (excluding this field)
};
#pragma pack(pop)
// Total size: 20 bytes
```

**Advantages for STM32:**
- Fixed size - perfect for DMA circular buffer
- Sync marker (0xAA55AA55) - easy to find packet boundaries
- No string parsing required
- Microsecond precision
- Minimal CPU overhead
- No interrupts needed (poll DMA buffer in main loop)

### JSON Format (Optional, Backward Compatible)

All JSON communication is terminated with `\n` (newline).

### Messages FROM ESP-01 to STM32

**1. Boot Message:**
```json
{"status":"boot","version":"2.0","tx_mode":"json","sync_interval":10}
```
The boot message now includes current transmission mode and sync interval.

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

**5. Set Transmission Mode (NEW in v2.0):**
```json
{"cmd":"set_mode","mode":"binary"}
```
Valid modes: `"json"`, `"binary"`, `"both"`

Response:
```json
{"status":"mode_updated","mode":"binary"}
```

**6. Set Sync Interval (NEW in v2.0):**
```json
{"cmd":"set_interval","interval":60}
```
Interval in seconds (1-3600). Longer intervals reduce CPU overhead on STM32.

Response:
```json
{"status":"interval_updated","interval":60}
```

**7. Get Current Configuration (NEW in v2.0):**
```json
{"cmd":"get_config"}
```
Response:
```json
{"tx_mode":1,"tx_mode_name":"binary","sync_interval":60}
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

## STM32 Implementation for Real-Time Systems

### Architecture Overview: DMA + RTC (No Interrupts Required)

For real-time applications where interrupts cannot be tolerated, use this architecture:

```
┌─────────────┐       UART       ┌──────────────────┐
│   ESP-01    │ ──────────────→  │   STM32 UART     │
│ (NTP Sync)  │    Binary/JSON   │   with DMA RX    │
└─────────────┘                  └──────────────────┘
                                          │
                                          ▼
                                  ┌──────────────────┐
                                  │  DMA Circular    │
                                  │     Buffer       │
                                  │   (No IRQ!)      │
                                  └──────────────────┘
                                          │
                                          ▼ (Poll in main loop)
                                  ┌──────────────────┐
                                  │  Parse Binary    │
                                  │  Time Packet     │
                                  └──────────────────┘
                                          │
                                          ▼ (Periodic sync)
                                  ┌──────────────────┐
                                  │   STM32 RTC or   │
                                  │  Hardware Timer  │
                                  │ (Local timekeeping)│
                                  └──────────────────┘
                                          │
                                          ▼ (Anytime, no latency)
                                  ┌──────────────────┐
                                  │  Read Current    │
                                  │      Time        │
                                  └──────────────────┘
```

**Key Benefits:**
- ✅ **No interrupts** - DMA handles UART reception in background
- ✅ **No blocking** - Poll for new data in main loop when convenient
- ✅ **Real-time compatible** - Doesn't disrupt critical tasks
- ✅ **Low latency** - RTC provides instant time access
- ✅ **Low drift** - Sync every 10-60 seconds is sufficient (RTC drift ~50ppm)

### 1. UART + DMA Configuration

Configure UART peripheral with DMA for reception (no interrupts!):

```c
// STM32 UART + DMA Configuration (STM32CubeMX or manual)
// UART: 115200 baud, 8N1, DMA RX enabled
// DMA: Circular mode, no interrupts

#define ESP01_UART huart2           // Adjust to your UART
#define DMA_BUFFER_SIZE 128         // Must be >= 20 bytes for binary packets

uint8_t uart_dma_buffer[DMA_BUFFER_SIZE];

// In your initialization code:
void ESP01_Init(void) {
  // Start UART DMA reception in circular mode (no interrupt!)
  HAL_UART_Receive_DMA(&ESP01_UART, uart_dma_buffer, DMA_BUFFER_SIZE);

  // Optional: Disable DMA interrupts if you truly need zero interrupts
  __HAL_DMA_DISABLE_IT(ESP01_UART.hdmarx, DMA_IT_TC);  // Transfer complete
  __HAL_DMA_DISABLE_IT(ESP01_UART.hdmarx, DMA_IT_HT);  // Half transfer
}
```

### 2. RTC Configuration

Use STM32's RTC peripheral for local timekeeping:

```c
// Configure RTC with external 32.768kHz crystal (LSE) for best accuracy
// Typical drift: 20-100 ppm (0.002-0.01%)
// At 100ppm: 8.64 seconds per day drift
// Syncing every 60 seconds keeps error < 0.006 seconds

RTC_TimeTypeDef rtc_time;
RTC_DateTypeDef rtc_date;
uint32_t local_epoch = 0;

// Sync RTC from ESP-01 time packet
void syncRTCFromEpoch(uint32_t epoch_seconds) {
  // Convert epoch to RTC date/time
  time_t t = (time_t)epoch_seconds;
  struct tm *timeinfo = gmtime(&t);

  rtc_time.Hours = timeinfo->tm_hour;
  rtc_time.Minutes = timeinfo->tm_min;
  rtc_time.Seconds = timeinfo->tm_sec;

  rtc_date.Year = timeinfo->tm_year - 100;  // Years since 2000
  rtc_date.Month = timeinfo->tm_mon + 1;
  rtc_date.Date = timeinfo->tm_mday;

  HAL_RTC_SetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN);
  HAL_RTC_SetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN);

  local_epoch = epoch_seconds;
}

// Get current epoch time from RTC (instant, no waiting for ESP-01!)
uint32_t getCurrentEpoch(void) {
  HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN);

  struct tm timeinfo = {0};
  timeinfo.tm_hour = rtc_time.Hours;
  timeinfo.tm_min = rtc_time.Minutes;
  timeinfo.tm_sec = rtc_time.Seconds;
  timeinfo.tm_year = rtc_date.Year + 100;  // Years since 1900
  timeinfo.tm_mon = rtc_date.Month - 1;
  timeinfo.tm_mday = rtc_date.Date;

  return (uint32_t)mktime(&timeinfo);
}
```

### 3. Binary Packet Reception (No Interrupts!)

Poll DMA buffer in your main loop to find and parse packets:

```c
#define SYNC_MARKER 0xAA55AA55

// Binary time packet structure (must match ESP-01!)
#pragma pack(push, 1)
typedef struct {
  uint32_t sync_marker;     // 0xAA55AA55
  uint32_t epoch_seconds;   // Unix timestamp
  uint32_t microseconds;    // Microseconds within second
  uint16_t year;
  uint8_t  month;
  uint8_t  day;
  uint8_t  hour;
  uint8_t  minute;
  uint8_t  second;
  uint8_t  checksum;
} BinaryTimePacket;
#pragma pack(pop)

static uint32_t last_dma_pos = 0;

// Call this in your main loop (no interrupt!)
bool ESP01_PollForTimePacket(BinaryTimePacket *packet) {
  // Get current DMA write position
  uint32_t dma_pos = DMA_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(ESP01_UART.hdmarx);

  // Calculate available bytes
  uint32_t available = (dma_pos >= last_dma_pos)
                       ? (dma_pos - last_dma_pos)
                       : (DMA_BUFFER_SIZE - last_dma_pos + dma_pos);

  // Need at least 20 bytes for a packet
  if (available < sizeof(BinaryTimePacket)) {
    return false;
  }

  // Search for sync marker in buffer
  for (uint32_t i = 0; i < available - 3; i++) {
    uint32_t pos = (last_dma_pos + i) % DMA_BUFFER_SIZE;

    // Check for sync marker (handle wraparound)
    uint32_t marker = 0;
    for (int j = 0; j < 4; j++) {
      marker |= ((uint32_t)uart_dma_buffer[(pos + j) % DMA_BUFFER_SIZE]) << (j * 8);
    }

    if (marker == SYNC_MARKER) {
      // Found sync marker! Copy packet
      for (uint32_t j = 0; j < sizeof(BinaryTimePacket); j++) {
        ((uint8_t*)packet)[j] = uart_dma_buffer[(pos + j) % DMA_BUFFER_SIZE];
      }

      // Verify checksum
      uint8_t sum = 0;
      for (uint32_t j = 0; j < sizeof(BinaryTimePacket) - 1; j++) {
        sum += ((uint8_t*)packet)[j];
      }

      if (sum == packet->checksum) {
        // Valid packet! Update read position
        last_dma_pos = (pos + sizeof(BinaryTimePacket)) % DMA_BUFFER_SIZE;
        return true;
      }

      // Checksum failed, keep searching
    }
  }

  // No valid packet found, advance position to avoid re-checking same data
  last_dma_pos = (last_dma_pos + (available / 2)) % DMA_BUFFER_SIZE;
  return false;
}
```

### 4. Complete STM32 Example (Real-Time Compatible)

```c
// Complete example for STM32 with DMA + RTC (no interrupts!)

#include "stm32xxxx_hal.h"  // Adjust for your STM32 family
#include <time.h>

extern UART_HandleTypeDef huart2;  // Your UART handle
extern RTC_HandleTypeDef hrtc;     // Your RTC handle

#define ESP01_UART huart2
#define DMA_BUFFER_SIZE 128
#define SYNC_MARKER 0xAA55AA55

// Binary packet structure
#pragma pack(push, 1)
typedef struct {
  uint32_t sync_marker;
  uint32_t epoch_seconds;
  uint32_t microseconds;
  uint16_t year;
  uint8_t  month;
  uint8_t  day;
  uint8_t  hour;
  uint8_t  minute;
  uint8_t  second;
  uint8_t  checksum;
} BinaryTimePacket;
#pragma pack(pop)

static uint8_t uart_dma_buffer[DMA_BUFFER_SIZE];
static uint32_t last_dma_pos = 0;
static bool time_synced = false;

// Initialize ESP-01 interface
void ESP01_Init(void) {
  // Start DMA reception in circular mode
  HAL_UART_Receive_DMA(&ESP01_UART, uart_dma_buffer, DMA_BUFFER_SIZE);

  // Optional: Disable DMA interrupts for true zero-interrupt operation
  __HAL_DMA_DISABLE_IT(ESP01_UART.hdmarx, DMA_IT_TC);
  __HAL_DMA_DISABLE_IT(ESP01_UART.hdmarx, DMA_IT_HT);
}

// Poll for new time packet (call from main loop)
bool ESP01_PollForTimePacket(BinaryTimePacket *packet) {
  uint32_t dma_pos = DMA_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(ESP01_UART.hdmarx);
  uint32_t available = (dma_pos >= last_dma_pos)
                       ? (dma_pos - last_dma_pos)
                       : (DMA_BUFFER_SIZE - last_dma_pos + dma_pos);

  if (available < sizeof(BinaryTimePacket)) return false;

  // Search for sync marker
  for (uint32_t i = 0; i < available - 3; i++) {
    uint32_t pos = (last_dma_pos + i) % DMA_BUFFER_SIZE;
    uint32_t marker = 0;

    for (int j = 0; j < 4; j++) {
      marker |= ((uint32_t)uart_dma_buffer[(pos + j) % DMA_BUFFER_SIZE]) << (j * 8);
    }

    if (marker == SYNC_MARKER) {
      // Copy packet
      for (uint32_t j = 0; j < sizeof(BinaryTimePacket); j++) {
        ((uint8_t*)packet)[j] = uart_dma_buffer[(pos + j) % DMA_BUFFER_SIZE];
      }

      // Verify checksum
      uint8_t sum = 0;
      for (uint32_t j = 0; j < sizeof(BinaryTimePacket) - 1; j++) {
        sum += ((uint8_t*)packet)[j];
      }

      if (sum == packet->checksum) {
        last_dma_pos = (pos + sizeof(BinaryTimePacket)) % DMA_BUFFER_SIZE;
        return true;
      }
    }
  }

  last_dma_pos = (last_dma_pos + (available / 2)) % DMA_BUFFER_SIZE;
  return false;
}

// Sync RTC from time packet
void ESP01_SyncRTC(BinaryTimePacket *packet) {
  RTC_TimeTypeDef rtc_time = {0};
  RTC_DateTypeDef rtc_date = {0};

  rtc_time.Hours = packet->hour;
  rtc_time.Minutes = packet->minute;
  rtc_time.Seconds = packet->second;

  rtc_date.Year = packet->year - 2000;
  rtc_date.Month = packet->month;
  rtc_date.Date = packet->day;

  HAL_RTC_SetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN);
  HAL_RTC_SetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN);

  time_synced = true;
}

// Get current time from RTC (instant, no latency!)
uint32_t getCurrentEpoch(void) {
  RTC_TimeTypeDef rtc_time;
  RTC_DateTypeDef rtc_date;

  HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN);

  struct tm timeinfo = {0};
  timeinfo.tm_hour = rtc_time.Hours;
  timeinfo.tm_min = rtc_time.Minutes;
  timeinfo.tm_sec = rtc_time.Seconds;
  timeinfo.tm_year = rtc_date.Year + 100;  // Years since 1900
  timeinfo.tm_mon = rtc_date.Month - 1;
  timeinfo.tm_mday = rtc_date.Date;

  return (uint32_t)mktime(&timeinfo);
}

// Main loop integration
int main(void) {
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_RTC_Init();

  ESP01_Init();

  BinaryTimePacket time_packet;

  while (1) {
    // Your real-time critical task runs here WITHOUT interruption!
    doRealtimeWork();

    // Poll for time updates when convenient (no interrupt!)
    if (ESP01_PollForTimePacket(&time_packet)) {
      ESP01_SyncRTC(&time_packet);
    }

    // Use time anytime with zero latency
    uint32_t current_time = getCurrentEpoch();

    // ... rest of your application
  }
}

// Optional: Send commands to ESP-01
void ESP01_SendCommand(const char* json_cmd) {
  HAL_UART_Transmit(&ESP01_UART, (uint8_t*)json_cmd, strlen(json_cmd), 1000);
  HAL_UART_Transmit(&ESP01_UART, (uint8_t*)"\n", 1, 100);
}

void ESP01_SetBinaryMode(void) {
  ESP01_SendCommand("{\"cmd\":\"set_mode\",\"mode\":\"binary\"}");
}

void ESP01_SetSyncInterval(uint16_t seconds) {
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "{\"cmd\":\"set_interval\",\"interval\":%u}", seconds);
  ESP01_SendCommand(cmd);
}
```

## Integration Steps for STM32 (DMA + RTC Approach)

### Step 1: Configure ESP-01 for Binary Mode

Before integrating with STM32, configure ESP-01 to use binary transmission:

```bash
# Connect serial terminal to ESP-01 at 115200 baud and send:
{"cmd":"set_mode","mode":"binary"}
{"cmd":"set_interval","interval":60}
```

Configuration is saved in EEPROM and persists across reboots.

### Step 2: STM32CubeMX Configuration

1. **Enable UART with DMA:**
   - UART2 (or your choice): 115200 baud, 8N1
   - Add DMA for UART2_RX
   - Mode: Circular
   - **Important:** Do NOT enable "Use FIFO" option
   - Do NOT enable any UART interrupts (for true non-interrupt operation)

2. **Enable RTC:**
   - Clock source: LSE (external 32.768kHz crystal recommended)
   - Format: Binary
   - Enable calendar

3. **Generate Code**

### Step 3: Implement Time Synchronization

Copy the complete STM32 example code from Section 4 above into your project.

Key functions:
- `ESP01_Init()` - Initialize DMA reception
- `ESP01_PollForTimePacket()` - Poll for packets (call in main loop)
- `ESP01_SyncRTC()` - Sync RTC from packet
- `getCurrentEpoch()` - Get current time

### Step 4: Integrate into Main Loop

```c
while (1) {
  // Your real-time tasks run without interruption
  performCriticalTask();

  // Check for time updates when convenient
  BinaryTimePacket packet;
  if (ESP01_PollForTimePacket(&packet)) {
    ESP01_SyncRTC(&packet);
  }

  // Use time anytime with zero latency
  uint32_t now = getCurrentEpoch();
}
```

### Step 5: Tune Sync Interval

Balance accuracy vs overhead:

| Sync Interval | RTC Drift @ 100ppm | CPU Overhead | Recommendation |
|---------------|-------------------|--------------|----------------|
| 10 seconds | ±0.001 sec | Higher | High precision needed |
| 60 seconds | ±0.006 sec | Low | **Recommended for most** |
| 300 seconds | ±0.03 sec | Very low | Non-critical applications |

Adjust ESP-01 interval:
```c
ESP01_SetSyncInterval(60);  // Sync every 60 seconds
```

## Example Usage Flow (Binary Mode)

```
1. STM32 boots up, initializes UART DMA
2. ESP-01 sends: {"status":"boot","version":"2.0","tx_mode":"binary",...}
3. ESP-01 sends: {"status":"connected",...}
4. ESP-01 sends: {"status":"ntp_synced",...}
5. ESP-01 sends binary time packets (20 bytes) every configured interval
6. STM32 DMA receives packets in background (no CPU involvement)
7. STM32 main loop polls DMA buffer when convenient
8. STM32 syncs RTC from packet
9. STM32 reads time from RTC anytime (instant, no latency)
10. Optional: STM32 can adjust sync interval via {"cmd":"set_interval",interval":X}
```

## Alternative Approaches

### Option 1: DMA + RTC (Recommended for Real-Time) ✅

**Pros:**
- No interrupts needed
- Real-time task compatible
- Instant time access via RTC
- Low CPU overhead

**Cons:**
- Requires RTC peripheral
- Slight complexity

**Use when:** You have real-time tasks that cannot be interrupted.

### Option 2: DMA + Software Timer

If you don't have RTC available, use a hardware timer:

```c
// Use TIM2 (32-bit timer) running at 1MHz (1µs resolution)
uint32_t timer_offset_us = 0;  // Offset to convert timer to epoch

void ESP01_SyncTimer(BinaryTimePacket *packet) {
  uint32_t current_timer = __HAL_TIM_GET_COUNTER(&htim2);
  timer_offset_us = (packet->epoch_seconds * 1000000UL + packet->microseconds) - current_timer;
}

uint32_t getCurrentEpoch(void) {
  uint32_t current_timer = __HAL_TIM_GET_COUNTER(&htim2);
  uint32_t total_us = current_timer + timer_offset_us;
  return total_us / 1000000;
}
```

**Note:** Timer will overflow after ~71 minutes (32-bit @ 1MHz), so you need overflow handling.

### Option 3: Interrupt-Based (If Interrupts Acceptable)

If interrupts are acceptable, use traditional UART interrupt approach:

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart == &ESP01_UART) {
    // Process received byte
    // Parse JSON or binary packet
  }
}
```

**Pros:** Simpler code
**Cons:** Interrupts your real-time tasks

## Testing

### Test ESP-01 with Serial Terminal

Before integrating with STM32, verify ESP-01 is working:

1. **Connect USB-to-Serial adapter:**
   - ESP-01 TX → USB Serial RX
   - ESP-01 RX → USB Serial TX
   - 3.3V and GND

2. **Test JSON mode (default):**
   ```bash
   # Open terminal at 115200 baud
   # Watch for automatic messages:
   {"status":"boot","version":"2.0",...}
   {"status":"connected",...}
   {"type":"time","epoch":...}

   # Send test commands:
   {"cmd":"get_status"}
   {"cmd":"get_config"}
   ```

3. **Test Binary mode:**
   ```bash
   {"cmd":"set_mode","mode":"binary"}
   # You'll now see 20-byte binary packets
   # Use hex view to verify: AA 55 AA 55 ...
   ```

4. **Test Configuration:**
   ```bash
   {"cmd":"set_interval","interval":5}
   {"cmd":"get_config"}
   ```

### STM32 Integration Testing

1. **Verify Hardware Connection:**
   - Connect ESP-01 to STM32 UART
   - Check 3.3V power (ESP-01 needs 200mA+)
   - Verify TX/RX not swapped

2. **Test DMA Reception:**
   ```c
   // Add debug code to verify DMA is receiving data
   uint32_t dma_pos = DMA_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(ESP01_UART.hdmarx);
   // This should increment as ESP-01 sends data
   ```

3. **Verify Packet Detection:**
   ```c
   // Add debug output when packet found
   if (ESP01_PollForTimePacket(&packet)) {
     printf("Sync marker: 0x%08X\n", packet.sync_marker);
     printf("Epoch: %u\n", packet.epoch_seconds);
     printf("Checksum OK\n");
   }
   ```

4. **Test RTC Sync:**
   ```c
   // After sync, verify RTC is running
   uint32_t t1 = getCurrentEpoch();
   HAL_Delay(5000);
   uint32_t t2 = getCurrentEpoch();
   // Should be ~5 seconds difference
   ```

5. **Performance Test:**
   ```c
   // Measure overhead of polling
   uint32_t start = DWT->CYCCNT;
   ESP01_PollForTimePacket(&packet);
   uint32_t cycles = DWT->CYCCNT - start;
   // Should be very low (< 1000 cycles typically)
   ```

## Troubleshooting

### ESP-01 Issues

**No data from ESP-01:**
- ✓ Check 3.3V power supply (ESP-01 needs 200mA+, use proper regulator)
- ✓ Verify RX/TX are not swapped (ESP-01 TX → STM32 RX)
- ✓ Check baud rate is 115200
- ✓ Verify GND connection
- ✓ Try toggling ESP-01 reset pin

**Garbled/corrupted data:**
- ESP-01 sends boot ROM messages at 74880 baud initially (ignore, wait for JSON)
- Check UART configuration (8N1, no flow control, no parity)
- Verify stable 3.3V power (voltage drops cause corruption)

**ESP-01 in AP mode instead of connecting:**
- WiFi credentials incorrect or network unavailable
- Send new credentials: `{"cmd":"set_wifi","ssid":"...","pass":"..."}`
- ESP-01 will reboot and try new credentials

**Time not syncing:**
- Check ESP-01 WiFi connection: `{"cmd":"get_status"}`
- Verify NTP sync message received
- Check router allows NTP traffic (UDP port 123)
- Test with different NTP server: Modify ESP-01 firmware

### STM32 DMA Issues

**DMA not receiving data:**
- Verify DMA is configured for UART RX (not TX!)
- Check DMA is in Circular mode
- Ensure DMA was started: `HAL_UART_Receive_DMA()`
- Use debugger to check `__HAL_DMA_GET_COUNTER()` - should change as data arrives

**Packets not detected:**
- Check sync marker search logic
- Verify buffer size >= 20 bytes
- Print raw DMA buffer in hex to inspect actual data
- Try setting ESP-01 to "both" mode to see JSON alongside binary

**Checksum failures:**
- Verify packet structure alignment (use `#pragma pack(push, 1)`)
- Ensure little-endian byte order (STM32 and ESP8266 are both little-endian)
- Check for buffer corruption or overflow

**RTC not syncing:**
- Verify RTC is enabled and LSE oscillator is running
- Check RTC register writes are completing (may need backup domain access)
- Enable RTC write protection after sync for safety

### Real-Time Performance Issues

**DMA polling causes delays:**
- Reduce polling frequency (only poll once per expected packet interval)
- Optimize sync marker search (use memchr or hardware pattern matching)
- Increase ESP-01 sync interval to reduce packet rate

**RTC accuracy insufficient:**
- Use external 32.768kHz LSE crystal (not internal LSI)
- Calibrate RTC using smooth calibration feature
- Reduce sync interval for tighter accuracy
- Consider temperature-compensated crystal (TCXO)

## ESP-01 Configuration

The ESP-01 firmware v2.0 has these defaults:

**WiFi Settings (Hardcoded Fallback):**
- **Default WiFi:** "PikaFlat24" / (see esp01/include/secrets.h)
- **AP Mode SSID:** "ESP01-Config" (if WiFi connection fails)
- **NTP Server:** pool.ntp.org
- **NTP Update:** Every hour

**Transmission Settings (Configurable via EEPROM):**
- **Default Mode:** JSON (compatible with v1.0)
- **Default Sync Interval:** 10 seconds
- **Settings persist across reboots**

### Configurable Commands Summary

| Command | Purpose | Persists? |
|---------|---------|-----------|
| `set_mode` | Switch between JSON/Binary/Both | ✓ Yes (EEPROM) |
| `set_interval` | Set sync interval (1-3600s) | ✓ Yes (EEPROM) |
| `get_config` | Query current configuration | - |
| `set_wifi` | Change WiFi credentials | ✓ Yes (EEPROM) |
| `get_status` | Query WiFi/NTP status | - |
| `get_time` | Request immediate time update | - |
| `restart` | Reboot ESP-01 | - |

## Quick Start Guide

**For Real-Time STM32 Applications:**

1. **Flash ESP-01 firmware v2.0** (already done)

2. **Configure ESP-01 for binary mode:**
   ```bash
   {"cmd":"set_mode","mode":"binary"}
   {"cmd":"set_interval","interval":60}
   ```

3. **Configure STM32 (STM32CubeMX):**
   - UART: 115200 baud, 8N1
   - DMA: Circular mode for UART RX, no interrupts
   - RTC: LSE clock, binary format

4. **Copy STM32 example code** from Section 4

5. **Integrate into main loop:**
   ```c
   while (1) {
     doRealtimeWork();  // No interruptions!
     if (ESP01_PollForTimePacket(&packet)) {
       ESP01_SyncRTC(&packet);
     }
     uint32_t now = getCurrentEpoch();  // Instant access
   }
   ```

6. **Done!** Your STM32 now has NTP-synchronized time without interrupts.

## Summary

### ESP-01 Side (This Codebase)
The ESP-01 is an autonomous WiFi/NTP bridge that:
- ✅ Connects to WiFi automatically
- ✅ Syncs with NTP servers hourly
- ✅ Sends time via UART in JSON or efficient binary format
- ✅ Supports configuration via JSON commands
- ✅ Handles all WiFi/NTP complexity

### STM32 Side (Your Colleague's Work)
For real-time applications, the STM32 should:
- ✅ Use UART + DMA in circular mode (no interrupts!)
- ✅ Poll DMA buffer in main loop when convenient
- ✅ Use RTC or hardware timer for local timekeeping
- ✅ Sync periodically from ESP-01 packets (every 10-60 seconds)
- ✅ Read time from RTC anytime with zero latency

**Result:** NTP-synchronized time with zero interrupts, perfect for real-time systems!

---

## Document Information

**Document Version:** 2.0
**Last Updated:** 2025-12-03
**ESP-01 Firmware Version:** 2.0
**Author:** Claude Code
**Target:** Real-time STM32 applications requiring interrupt-free time synchronization

**Changelog:**
- **v2.0 (2025-12-03):**
  - Added binary time packet format for DMA efficiency
  - Added configurable transmission mode and sync interval
  - Added comprehensive DMA + RTC implementation guide
  - Added real-time system architecture documentation
  - Updated all examples for interrupt-free operation

- **v1.0 (2025-11-27):**
  - Initial JSON-only implementation
