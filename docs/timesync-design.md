# Time Synchronization Design Document

## Project Overview

This document describes the design and implementation of a real-time clock system for the STM32L432KCU6 microcontroller that maintains accurate time both with and without internet connectivity.

## Requirements

### Functional Requirements

- FR1: System shall maintain accurate time when internet connectivity is available
- FR2: System shall maintain reasonable time accuracy when internet is unavailable
- FR3: System shall automatically synchronize with NTP time from ESP-01 module
- FR4: System shall provide current time in standard format (year, month, day, hour, minute, second)
- FR5: System shall continue keeping time across power cycles if backup battery is present
- FR6: System shall automatically detect available clock sources
- FR7: System shall report which clock source is in use for remote debugging

### Performance Requirements

- PR1: Clock drift shall be less than 20 ppm when using LSE oscillator (1.7 seconds per day)
- PR2: Clock drift shall be less than 5 percent when using LSI oscillator (acceptable with frequent NTP sync)
- PR3: Time synchronization from NTP shall occur every 1 hour when using LSE, every 1 minute when using LSI
- PR4: RTC updates shall not block main application loop
- PR5: Time queries shall execute in under 1 millisecond

### Design Requirements

- DR1: Implementation shall use STM32 HAL RTC driver
- DR2: System shall prefer LSE over LSI when available
- DR3: System shall handle LSE startup failure gracefully
- DR4: System shall maintain time continuity during clock source changes
- DR5: Code shall be modular and testable

## System Architecture

### Components

1. RTC Peripheral
   - Hardware calendar integrated in STM32L432KCU6
   - Maintains date and time independently of CPU
   - Powered by VDD or VBAT (backup battery)
   - Clocked by LSE or LSI oscillator

2. Clock Sources
   - LSE: External 32.768 kHz crystal oscillator (may or may not be present on board)
   - LSI: Internal 32 kHz RC oscillator (always available)

3. NTP Time Source
   - ESP-01 module provides NTP-synchronized time via UART
   - JSON messages arrive every 10 seconds
   - Format: epoch, year, month, day, hour, minute, second

4. Application Interface
   - Functions to query current time
   - Functions to set time manually if needed
   - Status reporting functions

### Data Flow

```
ESP-01 NTP Time (UART)
        |
        v
    Parse JSON
        |
        v
    Update RTC Calendar
        ^
        |
    LSE/LSI Clock
        |
        v
    RTC Peripheral
        |
        v
    Time Query Functions
        |
        v
    Application (Clock Display)
```

## Clock Source Selection Strategy

### Startup Sequence

1. Attempt to enable LSE oscillator with timeout
2. Wait for LSE ready flag (maximum 5 seconds)
3. If LSE ready: use LSE as RTC clock source
4. If LSE timeout: use LSI as RTC clock source
5. Report selected clock source via UART

### LSE Detection Algorithm

The LSE oscillator requires external 32.768 kHz crystal. Detection sequence:

1. Enable PWR clock and backup domain access
2. Reset backup domain to clear any previous configuration
3. Enable LSE oscillator with medium-high drive strength
4. Poll LSE ready flag with timeout (5000 ms)
5. If LSERDY flag sets: LSE is available
6. If timeout: LSE crystal not present or failed

### LSI Fallback

If LSE is not available:

1. Enable LSI oscillator
2. Wait for LSI ready flag (typically under 100 us)
3. Select LSI as RTC clock source
4. RTC will have approximately 5 percent drift but NTP sync every 1 minute keeps error under 3 seconds

### Clock Source Characteristics

LSE (when available):
- Frequency: 32.768 kHz
- Accuracy: 20 ppm typical (1.7 seconds per day)
- Startup time: 1-5 seconds
- Power: Very low (sub-uA range)
- Requires: External crystal on OSC32_IN and OSC32_OUT pins

LSI (always available):
- Frequency: 32 kHz nominal (actual: 26-56 kHz range)
- Accuracy: 5 percent typical
- Startup time: Under 100 microseconds
- Power: Low (approximately 1 uA)
- Requires: Nothing, internal to chip

## RTC Configuration

### Calendar Format

The STM32 RTC uses BCD format for date and time:

- Year: 0-99 (two digits, add 2000 for full year)
- Month: 1-12
- Day: 1-31
- Hour: 0-23
- Minute: 0-59
- Second: 0-59
- Subseconds: 0-255 (1/256 second resolution)

### Prescaler Configuration

For 32.768 kHz input (LSE):
- Asynchronous prescaler: 128
- Synchronous prescaler: 256
- Result: 32768 / 128 / 256 = 1 Hz tick

For 32 kHz input (LSI):
- Asynchronous prescaler: 128
- Synchronous prescaler: 250
- Result: 32000 / 128 / 250 = 1 Hz tick
- Note: LSI frequency varies, calibration may be needed

### RTC Initialization Sequence

1. Enable PWR clock
2. Enable backup domain access (PWR_CR1 DBP bit)
3. Reset backup domain if first boot
4. Enable and select clock source (LSE or LSI)
5. Enable RTC clock
6. Exit initialization mode
7. Configure prescalers for 1 Hz tick
8. Set calendar to default or last known time
9. Enable RTC peripheral

### Race Conditions and Concurrent Access

The RTC peripheral is updated by hardware at the LSE/LSI clock rate (32.768 kHz) while the CPU accesses it at the system clock rate (80 MHz). This creates potential race conditions that must be handled correctly.

#### Hardware Protection: Shadow Register Mechanism

The STM32L4 RTC includes built-in hardware protection against corrupted reads:

```
RTC Core Domain (32.768 kHz LSE)     APB1 Domain (CPU side, 80 MHz)
┌─────────────────────┐              ┌──────────────────┐
│  RTC_TR (Time)      │─────sync────>│ Shadow RTC_TR    │<── CPU reads
│  RTC_DR (Date)      │─────sync────>│ Shadow RTC_DR    │
│  RTC_SSR (Subsec)   │─────sync────>│ Shadow RTC_SSR   │
└─────────────────────┘              └──────────────────┘
    Updated by HW                      Locked during read
```

**Protection mechanism:**
1. Shadow registers are continuously synchronized from the RTC core domain
2. When CPU reads RTC_SSR or RTC_TR (time), shadow registers automatically lock
3. All shadow registers remain frozen until CPU reads RTC_DR (date)
4. This ensures a consistent snapshot even if hardware updates during the read sequence
5. Reading order is critical: always read date last to unlock the shadows

**HAL Implementation:**
The STM32 HAL correctly implements this sequence:
```c
HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BCD);  // Locks shadow registers
HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BCD);  // Unlocks shadow registers
```

#### Application-Level Race Conditions

**Scenario 1: Reading while RTC ticks**
- **Risk**: Hardware updates time during CPU read
- **Protection**: Shadow register mechanism (hardware)
- **Result**: Always get consistent snapshot, no action needed

**Scenario 2: Reading during NTP sync (software write)**
```
Main Loop (1/sec):              UART RX Callback (every 10 sec):
send_rtc_time_usart2()          process_line()
  └─> rtc_get_time()              └─> rtc_sync_from_esp()
                                        └─> rtc_set_time()
```

- **Risk**: Read occurs while NTP sync is writing new time
- **Duration**: HAL_RTC_SetTime takes ~10-50 µs
- **Frequency**: NTP sync every hour (LSE) or minute (LSI)
- **Impact**: Reader gets either old or new time, both valid
- **Corruption risk**: None - cannot get invalid time like "2025-99-99 88:88:88"

**Assessment for clock display application:**
- **No critical section needed**: Race is extremely rare and harmless
- **Worst case**: User sees one skipped or duplicate second on display
- **Visual impact**: Imperceptible on analog clock display
- **Safe for application**: Current implementation is adequate

**When protection would be needed:**
- Critical timing systems (aviation, medical, industrial control)
- Data logging with precise timestamps
- Time-based access control or authentication
- Financial transaction timestamping

**Protection options if needed:**
1. **Critical section** (disable interrupts during read)
2. **Read-verify-read** (read twice, retry if different)
3. **Sync flag** (set flag during write, check before read)

#### Current Implementation

The implementation uses shadow register protection only, which is sufficient for the clock display application. No additional critical sections or locking mechanisms are implemented.

## RTC Power Architecture

### Dual Power Domain

The STM32L432 RTC operates on a separate backup power domain that can be battery-powered independently from the main VDD supply.

Power pins:
- VDD: Main 3.3V supply (derived from 5V input via onboard regulator)
- VBAT: Backup battery input (pin 6 on STM32L432KC LQFP32 package)

Power switching:
- When VDD is present: RTC powered from VDD, VBAT pin is isolated by internal switch
- When VDD drops below threshold: Automatic switchover to VBAT within microseconds
- RTC, LSE oscillator, and backup registers remain powered from VBAT
- When VDD returns: Automatic switchback to VDD, VBAT charging circuit disabled

### Backup Domain Components

The following components are powered by the backup domain and survive VDD power loss:

- RTC peripheral (calendar, prescalers, alarms)
- LSE oscillator (32.768 kHz crystal)
- Backup SRAM registers (20 x 32-bit registers)
- Tamper detection circuitry
- Wakeup timer

### Nucleo-32 Board Configuration

Default configuration:
- VBAT pin connected to VDD via solder bridge SB9
- No backup battery installed
- RTC loses time when board is unpowered

Adding backup battery:
1. Locate solder bridge SB9 on bottom of Nucleo-32 board
2. Cut trace on SB9 to disconnect VBAT from VDD
3. Solder CR2032 coin cell holder to VBAT pin (pin 6)
4. Connect holder negative terminal to GND
5. Insert CR2032 battery (220 mAh, 3V)

### Power Consumption

RTC power consumption in different modes:

Mode 1: Normal operation (VDD powered)
- LSE oscillator: 0.5 uA
- RTC logic: 0.5 uA
- Total: approximately 1 uA

Mode 2: VBAT backup (VDD off, battery powered)
- LSE oscillator: 0.45 uA
- RTC logic: 0.05 uA
- Total: approximately 0.5 uA

Mode 3: Using LSI instead of LSE
- LSI oscillator: 0.7 uA
- RTC logic: 0.5 uA
- Total: approximately 1.2 uA

Battery lifetime calculation:
- CR2032 capacity: 220 mAh
- RTC consumption: 0.5 uA
- Lifetime: 220 mAh / 0.5 uA = 440,000 hours = 50 years (theoretical)
- Practical lifetime: 10-25 years accounting for self-discharge and temperature

### Implementation Notes

VBAT pin handling:
- Never connect VBAT to voltage higher than VDD + 0.6V
- Use Schottky diode if external supply might exceed VDD
- CR2032 is safe because battery voltage (3V) is always less than VDD (3.3V)

Backup domain reset:
- Occurs on first power-up or when VBAT drops below threshold
- Clears all RTC settings and backup registers
- Software must detect reset and reinitialize RTC
- Use RTC backup register as flag to detect if RTC was previously initialized

## Time Synchronization Strategy

### Initial Synchronization

On first boot or after backup domain reset:

1. RTC starts with default time (2025-01-01 00:00:00)
2. Wait for first NTP time message from ESP-01
3. Parse NTP time data
4. Set RTC calendar to NTP time
5. Mark system as synchronized

### Periodic Synchronization

ESP-01 behavior:
- ESP-01 broadcasts NTP time every 10 seconds via UART (unchanged)
- STM32 receives and parses all messages but does not always act on them

STM32 sync schedule (based on clock source):

When using LSE:
- Sync interval: 1 hour
- Maximum drift accumulation: 0.072 seconds (20 ppm x 3600 sec)
- STM32 ignores NTP messages between scheduled sync times
- Keeps clock accurate to sub-100ms

When using LSI:
- Sync interval: 1 minute
- Maximum drift accumulation: 3 seconds (5 percent x 60 sec)
- More frequent sync compensates for LSI instability
- Acceptable accuracy for clock display

Sync algorithm when sync is due:
1. Receive and parse NTP time from ESP-01
2. Read current RTC time
3. Calculate time difference (delta = NTP - RTC)
4. If absolute delta is greater than 2 seconds: hard set RTC to NTP time
5. If absolute delta is 1-2 seconds: hard set RTC to NTP time with warning log
6. If absolute delta is less than 1 second: no action needed, clock is accurate
7. Update last sync timestamp
8. Schedule next sync based on clock source (1 hour for LSE, 1 minute for LSI)

### Offline Operation

When ESP-01 is disconnected or NTP unavailable:

1. RTC continues running on LSE or LSI
2. Time accuracy depends on clock source
3. LSE drift: approximately 1.7 seconds per day
4. LSI drift: approximately 4000 seconds per day
5. When NTP returns, resynchronize immediately

### Sync Error Handling

- If NTP time goes backwards: log error, do not update RTC
- If NTP time jumps forward more than 1 hour: log warning, require confirmation
- If multiple sync failures: mark time as unreliable
- If RTC hardware error: attempt re-initialization

## Software Interface Design

### Public Functions

```
rtc_init()
  - Initialize RTC with clock source detection
  - Returns: status code indicating success or clock source used

rtc_get_time(rtc_time_t *time)
  - Read current time from RTC
  - Returns: 0 on success, -1 on error

rtc_set_time(rtc_time_t *time)
  - Set RTC calendar to specified time
  - Returns: 0 on success, -1 on error

rtc_sync_from_esp(esp_time_t *ntp_time)
  - Update RTC from NTP time
  - Returns: 0 on success, -1 on error

rtc_get_status(rtc_status_t *status)
  - Get clock source, sync status, last sync time
  - Returns: 0 on success, -1 on error
```

### Data Structures

```
typedef struct {
    uint16_t year;      // 2000-2099
    uint8_t month;      // 1-12
    uint8_t day;        // 1-31
    uint8_t hour;       // 0-23
    uint8_t minute;     // 0-59
    uint8_t second;     // 0-59
    uint8_t subsecond;  // 0-255 (not always used)
} rtc_time_t;

typedef struct {
    uint8_t clock_source;    // 0=LSI, 1=LSE
    uint8_t synced;          // 0=never synced, 1=synced
    uint32_t last_sync_time; // RTC timestamp of last NTP sync
    uint32_t next_sync_time; // RTC timestamp when next sync is due
    int8_t drift_estimate;   // estimated drift in seconds per day
    uint16_t sync_interval;  // seconds between syncs (3600 for LSE, 60 for LSI)
} rtc_status_t;
```

## Integration with Existing Code

### Changes to main.c

1. Add RTC initialization after HAL_Init
2. Add rtc_sync_from_esp call in process_line function when NTP time received
3. Change clock display to use rtc_get_time instead of esp_time directly
4. Add status reporting in main loop or on demand

### Changes to CMakeLists.txt

1. Add stm32l4xx_hal_rtc.c to HAL_SOURCES
2. Add stm32l4xx_hal_rtc_ex.c to HAL_SOURCES

### New Files

1. src/rtc_manager.c - RTC initialization and management
2. src/rtc_manager.h - Public interface and data structures

## Testing Strategy

### Unit Tests

Test 1: LSE detection on board with crystal
- Expected: LSE selected, LSERDY flag set, 32.768 kHz confirmed

Test 2: LSI fallback on board without crystal
- Expected: LSI selected after LSE timeout, RTC running

Test 3: Time setting and reading
- Expected: Set time matches read time within 1 second

Test 4: NTP sync with small difference
- Expected: RTC updated, time matches NTP within 1 second

Test 5: NTP sync with large difference
- Expected: Warning logged, time updated after confirmation logic

### Integration Tests

Test 6: Power cycle with VBAT
- Expected: Time preserved across reset

Test 7: Power cycle without VBAT
- Expected: Time resets, resynchronizes from NTP

Test 8: ESP-01 disconnect for 1 hour
- Expected: Time continues, drifts according to clock source

Test 9: ESP-01 reconnect after disconnect
- Expected: Time resyncs immediately

### Field Tests

Test 10: 24 hour drift measurement with LSE
- Expected: Less than 2 seconds drift per day

Test 11: 24 hour drift measurement with LSI
- Expected: Measurable drift, corrected by periodic NTP sync

Test 12: Remote status reporting
- Expected: UART output shows clock source and sync status

## Implementation Status

### Completed Features (Version 1.2 - 2025-12-26)

#### System Clock Configuration (main.c)
- LSE oscillator enabled: 32.768 kHz external crystal configured
- MSI with LSE calibration: MSI auto-trim using LSE for better accuracy
- RTC clock source: LSE routed to RTC peripheral
- Backup domain access: Enabled for RTC/LSE configuration
- System clock: 80 MHz from MSI → PLL maintained

Configuration:
```
LSE (32.768 kHz) ──┬──> RTC Peripheral (1 Hz tick)
                   │
                   └──> MSI Auto-Calibration
                        │
MSI (4 MHz) ───────────┴──> PLL (×40 ÷2) ──> 80 MHz SYSCLK
```

#### RTC Manager Module
- rtc_manager.h: Public API with data structures
- rtc_manager.c: Full RTC implementation
- rtc_init(): Initialize RTC with LSE, set default time, backup register support
- rtc_get_time(): Read current time with BCD conversion
- rtc_set_time(): Set RTC calendar
- rtc_sync_from_esp(): NTP synchronization with drift detection
- rtc_get_status(): Query clock source and sync status

Key Features:
- BCD ↔ decimal conversion for time/date
- Unix epoch calculation for drift tracking
- Backup register magic value (RTC_BKP_DR0) to detect initialization state
- Time survives resets when VBAT is present
- Hourly sync interval for LSE (3600 seconds)
- Smart sync logic: only updates if drift ≥ 2 seconds

#### Main Application Integration
- RTC initialization: Called after SystemClock_Config() in main()
- NTP sync integration: rtc_sync_from_esp() called in process_line() when NTP message received
- UART time reporting: RTC time transmitted every 1 second over USART2 (USB VCP)
- DMA-based transmission: Non-blocking RTC time output with busy flag management
- Backward compatibility: esp_time structure still populated for existing display code

UART Output Format:
```
RTC: 2025-12-26 14:30:45
RTC: 2025-12-26 14:30:46
RTC: 2025-12-26 14:30:47
{"type":"time","epoch":1735228848,...}  (ESP-01 NTP message every 10 sec)
```

#### Build System
- CMakeLists.txt: Added stm32l4xx_hal_rtc.c and stm32l4xx_hal_rtc_ex.c to HAL_SOURCES
- Project sources: Added rtc_manager.c to PROJECT_SOURCES
- HAL configuration: HAL_RTC_MODULE_ENABLED confirmed in stm32l4xx_hal_conf.h

#### Race Condition Protection
- Shadow register mechanism: Hardware protection for read consistency
- Implementation decision: No critical sections needed for clock display application
- Documentation: Race condition analysis added to design document

### Not Yet Implemented

#### LSI Fallback
- LSI detection: Fallback to LSI if LSE fails to start
- Dynamic prescaler: Adjust prescalers for 32 kHz LSI vs 32.768 kHz LSE
- Adaptive sync interval: Switch to 60-second sync when using LSI
- Note: LSI fallback left empty since LSE is confirmed present on hardware

#### Advanced Features
- Status reporting: Detailed status output via UART (see Debug and Diagnostics section)
- Drift calibration: Multi-sample drift estimation and compensation
- Gradual time adjustment: Smooth time correction instead of hard set
- Error handling improvements: Detailed error codes and recovery logic
- Temperature compensation: LSI calibration using on-chip temp sensor

#### Testing
- Unit tests: Individual function testing
- Integration tests: Power cycle, VBAT backup, ESP-01 disconnect scenarios
- Field tests: 24-hour drift measurement, long-term accuracy verification

### Files Modified/Created

New Files:
- `src/rtc_manager.h` - RTC manager public interface
- `src/rtc_manager.c` - RTC manager implementation

Modified Files:
- `src/main.c` - Added RTC initialization, NTP sync integration, UART time reporting
- `CMakeLists.txt` - Added RTC HAL sources and rtc_manager.c
- `docs/timesync-design.md` - Added race condition analysis and implementation status

Configuration Files:
- `config/stm32l4xx_hal_conf.h` - HAL_RTC_MODULE_ENABLED (already present)

### Current System Capabilities

Time Keeping:
- RTC runs continuously on LSE (32.768 kHz) with ~20 ppm accuracy
- Default time: 2025-01-01 00:00:00 on first boot
- Time preserved across resets if VBAT connected
- NTP synchronization every hour (when ESP-01 connected)
- Local timekeeping continues when NTP unavailable

Output:
- RTC time transmitted over USART2 every 1 second
- ESP-01 NTP messages echoed every 10 seconds
- Visual clock display on aircraft display (using simulated time for testing)

Power:
- LSE oscillator: ~0.5 µA
- RTC peripheral: ~0.5 µA
- Total RTC subsystem: ~1 µA
- VBAT backup ready (CR2032 coin cell support)

### Next Steps (Recommended Priority)

1. ~~Field testing: Flash to hardware, verify RTC initialization and LSE startup~~ Done 2026-10-02:
   RCC_BDCR = 0x8103 (LSE on/ready, RTC on LSE), RTC counts, backup-register magic set
2. NTP sync verification: initial sync verified 2026-10-02 (RTC jumped from 2025-01-01 00:00:01 to
   NTP time on the first ESP-01 message and stays on the same second). Hourly re-sync still to confirm.
   Note: ESP-01 firmware sends UTC (NTP_OFFSET 0); local time/DST is not handled yet.
3. Drift measurement: Run 24-hour test to measure actual LSE accuracy
4. Clock display integration: Update main loop to use rtc_get_time() instead of esp_time
5. VBAT testing: Add CR2032 battery, verify time retention across power cycles
6. Status reporting: Implement detailed status output for debugging

## Debug and Diagnostics

### Status Reporting Format

Output via USART2 on demand or periodic:

```
RTC Status:
  Clock Source: LSE (32.768 kHz)
  Sync Interval: 3600 seconds (1 hour)
  Synchronized: Yes
  Last NTP Sync: 5 seconds ago
  Next NTP Sync: 3595 seconds
  Current Time: 2025-12-24 15:30:45
  Drift Estimate: +1.2 seconds/day
```

### Error Conditions

- LSE_TIMEOUT: LSE crystal not detected, using LSI
- RTC_INIT_FAIL: RTC peripheral initialization failed
- NTP_TIME_BACKWARDS: Received NTP time is earlier than current RTC time
- NTP_TIME_JUMP: Received NTP time differs by more than 1 hour
- CLOCK_STOPPED: RTC not counting (hardware fault)

## Power Consumption Considerations

RTC power in different modes:

- LSE + RTC active: approximately 1 uA
- LSI + RTC active: approximately 1-2 uA
- RTC in VBAT mode (main power off): 0.5 uA with LSE

Backup battery:
- Recommended: CR2032 coin cell (220 mAh)
- Expected lifetime: 10-25 years depending on temperature and self-discharge
- Connection: VBAT pin on STM32 (pin 6)

## Future Enhancements

1. Gradual time adjustment instead of hard set for small differences
2. Drift calibration using multiple NTP samples
3. Temperature compensation for LSI if using on-chip temperature sensor
4. Timezone support and daylight saving time
5. Alarm functionality for periodic events
6. Timestamp for events in application
7. Log time synchronization events to external storage

## References

- STM32L4 Reference Manual RM0394 Section 38 (RTC)
- STM32L432KC Datasheet DS10198
- AN4759 Using the hardware RTC with the STM32 HAL
- esp01-protocol.md (NTP data format)

## Revision History

- Version 1.0 (2025-12-24): Initial design document
- Version 1.1 (2025-12-24): Added RTC Power Architecture section with VBAT details, updated sync intervals to hourly for LSE and minutely for LSI
- Version 1.3 (2026-10-02): First hardware run with flashed ESP-01; see esp01-flashing.md for the ESP-01 setup

## User notes related to timing

See "Real-time clock (RTC) and backup registers" in `reference/stm32l432kc.pdf`