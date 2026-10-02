#include "rtc_manager.h"
#include "stm32l4xx_hal.h"
#include <stdlib.h>

// RTC handle
static RTC_HandleTypeDef hrtc;

// Status tracking
static rtc_status_t rtc_status = {
    .clock_source = RTC_CLOCK_LSE,
    .synced = 0,
    .last_sync_time = 0,
    .next_sync_time = 0,
    .drift_estimate = 0,
    .sync_interval = 3600  // 1 hour for LSE
};

// Backup register to detect if RTC was already initialized
#define RTC_INIT_FLAG_REGISTER RTC_BKP_DR0
#define RTC_INIT_MAGIC_VALUE   0x32F2  // Random magic value

// Helper function to convert decimal to BCD
static uint8_t dec_to_bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

// Helper function to convert BCD to decimal
static uint8_t bcd_to_dec(uint8_t val)
{
    return ((val >> 4) * 10) + (val & 0x0F);
}

// Helper function to calculate Unix epoch from RTC time
static uint32_t rtc_time_to_epoch(const rtc_time_t *time)
{
    // Simplified epoch calculation (ignores leap seconds)
    // Days since Jan 1, 2000
    uint32_t days = 0;

    // Add years
    for (uint16_t y = 2000; y < time->year; y++) {
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
            days += 366;  // Leap year
        } else {
            days += 365;
        }
    }

    // Add months
    const uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint8_t is_leap = ((time->year % 4 == 0 && time->year % 100 != 0) || (time->year % 400 == 0));

    for (uint8_t m = 1; m < time->month; m++) {
        days += month_days[m - 1];
        if (m == 2 && is_leap) {
            days += 1;  // Leap day
        }
    }

    // Add days
    days += time->day - 1;

    // Convert to seconds and add time
    uint32_t epoch = (days * 86400UL) + (time->hour * 3600UL) + (time->minute * 60UL) + time->second;

    // Adjust to Unix epoch (Jan 1, 1970)
    epoch += 946684800UL;  // Seconds between 1970 and 2000

    return epoch;
}

int rtc_init(void)
{
    // Check if RTC was already initialized (survives reset with VBAT)
    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_INIT_FLAG_REGISTER) == RTC_INIT_MAGIC_VALUE) {
        // RTC already initialized, just reinit the handle
        hrtc.Instance = RTC;
        hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
        hrtc.Init.AsynchPrediv = 127;  // 128 - 1
        hrtc.Init.SynchPrediv = 255;   // 256 - 1
        hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
        hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
        hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
        hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;

        if (HAL_RTC_Init(&hrtc) != HAL_OK) {
            return -1;
        }

        return 0;  // RTC was previously initialized, time preserved
    }

    // First-time initialization
    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127;  // (128 - 1) for LSE 32.768 kHz
    hrtc.Init.SynchPrediv = 255;   // (256 - 1) for LSE 32.768 kHz
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;

    if (HAL_RTC_Init(&hrtc) != HAL_OK) {
        return -1;
    }

    // Set default time: 2025-01-01 00:00:00
    rtc_time_t default_time = {
        .year = 2025,
        .month = 1,
        .day = 1,
        .hour = 0,
        .minute = 0,
        .second = 0,
        .subsecond = 0
    };

    if (rtc_set_time(&default_time) != 0) {
        return -1;
    }

    // Mark RTC as initialized in backup register
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_INIT_FLAG_REGISTER, RTC_INIT_MAGIC_VALUE);

    return 0;
}

int rtc_get_time(rtc_time_t *time)
{
    if (time == NULL) {
        return -1;
    }

    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    // Read time and date from RTC
    if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK) {
        return -1;
    }

    if (HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK) {
        return -1;
    }

    // Convert BCD to decimal
    time->year = 2000 + bcd_to_dec(sDate.Year);
    time->month = bcd_to_dec(sDate.Month);
    time->day = bcd_to_dec(sDate.Date);
    time->hour = bcd_to_dec(sTime.Hours);
    time->minute = bcd_to_dec(sTime.Minutes);
    time->second = bcd_to_dec(sTime.Seconds);
    time->subsecond = sTime.SubSeconds;

    return 0;
}

int rtc_set_time(const rtc_time_t *time)
{
    if (time == NULL) {
        return -1;
    }

    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    // Convert decimal to BCD and set time
    sTime.Hours = dec_to_bcd(time->hour);
    sTime.Minutes = dec_to_bcd(time->minute);
    sTime.Seconds = dec_to_bcd(time->second);
    sTime.SubSeconds = 0;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;

    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK) {
        return -1;
    }

    // Convert decimal to BCD and set date
    sDate.Year = dec_to_bcd(time->year - 2000);
    sDate.Month = dec_to_bcd(time->month);
    sDate.Date = dec_to_bcd(time->day);
    sDate.WeekDay = RTC_WEEKDAY_MONDAY;  // Not used in our application

    if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK) {
        return -1;
    }

    return 0;
}

int rtc_sync_from_esp(uint16_t ntp_year, uint8_t ntp_month, uint8_t ntp_day,
                      uint8_t ntp_hour, uint8_t ntp_minute, uint8_t ntp_second,
                      uint32_t ntp_epoch)
{
    // Get current RTC time
    rtc_time_t current_time = {0};
    if (rtc_get_time(&current_time) != 0) {
        return -1;
    }

    // Calculate current epoch
    uint32_t current_epoch = rtc_time_to_epoch(&current_time);

    // Check if sync is due
    if (rtc_status.synced && current_epoch < rtc_status.next_sync_time) {
        // Not time to sync yet, ignore this NTP message
        return 0;
    }

    // Calculate time difference
    int32_t delta = (int32_t)(ntp_epoch - current_epoch);

    // Error check: time going backwards
    if (delta < -10) {
        // NTP time is more than 10 seconds behind RTC, suspicious
        return -1;
    }

    // Sync logic based on delta
    if (abs(delta) >= 2) {
        // Significant difference, update RTC
        rtc_time_t ntp_time = {
            .year = ntp_year,
            .month = ntp_month,
            .day = ntp_day,
            .hour = ntp_hour,
            .minute = ntp_minute,
            .second = ntp_second,
            .subsecond = 0
        };

        if (rtc_set_time(&ntp_time) != 0) {
            return -1;
        }

        // Update status
        rtc_status.synced = 1;
        rtc_status.last_sync_time = ntp_epoch;
        rtc_status.next_sync_time = ntp_epoch + rtc_status.sync_interval;

        // Estimate drift if we have previous sync data
        if (rtc_status.last_sync_time > 0) {
            uint32_t time_since_sync = ntp_epoch - rtc_status.last_sync_time;
            if (time_since_sync > 0) {
                // Calculate drift in seconds per day
                rtc_status.drift_estimate = (int8_t)((delta * 86400L) / time_since_sync);
            }
        }
    } else if (abs(delta) < 1) {
        // Clock is accurate, just update sync status
        rtc_status.synced = 1;
        rtc_status.last_sync_time = ntp_epoch;
        rtc_status.next_sync_time = ntp_epoch + rtc_status.sync_interval;
    } else {
        // Delta is 1-2 seconds, update with warning
        rtc_time_t ntp_time = {
            .year = ntp_year,
            .month = ntp_month,
            .day = ntp_day,
            .hour = ntp_hour,
            .minute = ntp_minute,
            .second = ntp_second,
            .subsecond = 0
        };

        if (rtc_set_time(&ntp_time) != 0) {
            return -1;
        }

        rtc_status.synced = 1;
        rtc_status.last_sync_time = ntp_epoch;
        rtc_status.next_sync_time = ntp_epoch + rtc_status.sync_interval;
    }

    return 0;
}

int rtc_get_status(rtc_status_t *status)
{
    if (status == NULL) {
        return -1;
    }

    *status = rtc_status;
    return 0;
}
