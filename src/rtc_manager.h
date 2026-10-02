#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <stdint.h>

// RTC time structure
typedef struct {
    uint16_t year;      // 2000-2099
    uint8_t month;      // 1-12
    uint8_t day;        // 1-31
    uint8_t hour;       // 0-23
    uint8_t minute;     // 0-59
    uint8_t second;     // 0-59
    uint8_t subsecond;  // 0-255 (not always used)
} rtc_time_t;

// RTC status structure
typedef struct {
    uint8_t clock_source;    // 0=LSI, 1=LSE
    uint8_t synced;          // 0=never synced, 1=synced
    uint32_t last_sync_time; // RTC timestamp of last NTP sync
    uint32_t next_sync_time; // RTC timestamp when next sync is due
    int8_t drift_estimate;   // estimated drift in seconds per day
    uint16_t sync_interval;  // seconds between syncs (3600 for LSE, 60 for LSI)
} rtc_status_t;

// Clock source definitions
#define RTC_CLOCK_LSI 0
#define RTC_CLOCK_LSE 1

// Function prototypes

/**
 * @brief Initialize RTC with LSE clock source
 * @return 0 on success, -1 on error
 */
int rtc_init(void);

/**
 * @brief Get current time from RTC
 * @param time Pointer to rtc_time_t structure to fill
 * @return 0 on success, -1 on error
 */
int rtc_get_time(rtc_time_t *time);

/**
 * @brief Set RTC calendar to specified time
 * @param time Pointer to rtc_time_t structure with time to set
 * @return 0 on success, -1 on error
 */
int rtc_set_time(const rtc_time_t *time);

/**
 * @brief Synchronize RTC from ESP NTP time
 * @param ntp_year Year from NTP (2000-2099)
 * @param ntp_month Month from NTP (1-12)
 * @param ntp_day Day from NTP (1-31)
 * @param ntp_hour Hour from NTP (0-23)
 * @param ntp_minute Minute from NTP (0-59)
 * @param ntp_second Second from NTP (0-59)
 * @param ntp_epoch Unix epoch timestamp
 * @return 0 on success, -1 on error
 */
int rtc_sync_from_esp(uint16_t ntp_year, uint8_t ntp_month, uint8_t ntp_day,
                      uint8_t ntp_hour, uint8_t ntp_minute, uint8_t ntp_second,
                      uint32_t ntp_epoch);

/**
 * @brief Get RTC status information
 * @param status Pointer to rtc_status_t structure to fill
 * @return 0 on success, -1 on error
 */
int rtc_get_status(rtc_status_t *status);

#endif // RTC_MANAGER_H
