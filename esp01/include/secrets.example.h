#pragma once
// WiFi credentials for the ESP-01 firmware. Template: copy to secrets.h and fill in.

// Station mode (fallback if nothing is stored in EEPROM)
#define DEFAULT_SSID "your-ssid"
#define DEFAULT_PASS "your-wifi-password"

// Config access point (used when station connect fails)
#define AP_SSID "ESP01-Config"
#define AP_PASS "choose-an-ap-password"
