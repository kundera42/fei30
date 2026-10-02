#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <time.h>

// Configuration
#define EEPROM_SIZE 512
#define SSID_ADDR 0
#define PASS_ADDR 64
#define MAGIC_ADDR 128
#define MAGIC_VALUE 0xAA55
#define CONFIG_ADDR 256

// WiFi / config-AP credentials: DEFAULT_SSID, DEFAULT_PASS, AP_SSID, AP_PASS
// Copy include/secrets.example.h to include/secrets.h (gitignored) and fill in.
#include "secrets.h"

#define NTP_SERVER "pool.ntp.org"
#define NTP_OFFSET 0  // UTC offset in seconds
#define NTP_UPDATE_INTERVAL 3600000  // Update every hour (ms)

#define UART_BAUD 115200
#define STATUS_INTERVAL 10000  // Send status every 10 seconds (default)

// Binary time packet format for DMA
#define SYNC_MARKER 0xAA55AA55
#pragma pack(push, 1)
struct BinaryTimePacket {
  uint32_t sync_marker;     // 0xAA55AA55
  uint32_t epoch_seconds;   // Unix timestamp
  uint32_t microseconds;    // Microseconds within current second
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t checksum;         // Simple 8-bit sum of all bytes
};
#pragma pack(pop)

// Transmission modes
enum TransmissionMode {
  MODE_JSON = 0,
  MODE_BINARY = 1,
  MODE_BOTH = 2
};

// Configuration structure
struct Config {
  uint8_t tx_mode;          // TransmissionMode
  uint16_t sync_interval;   // Sync interval in seconds
};

Config default_config = {MODE_JSON, 10};

// Global variables
String stored_ssid = "";
String stored_pass = "";
bool wifi_configured = false;
bool wifi_connected = false;
bool ap_mode = false;
unsigned long last_ntp_update = 0;
unsigned long last_status_send = 0;
bool time_synced = false;
Config current_config = {MODE_JSON, 10};

WiFiUDP udp;

void saveWiFiCredentials(const String& ssid, const String& pass) {
  EEPROM.write(MAGIC_ADDR, MAGIC_VALUE & 0xFF);
  EEPROM.write(MAGIC_ADDR + 1, (MAGIC_VALUE >> 8) & 0xFF);

  for (int i = 0; i < 64; i++) {
    EEPROM.write(SSID_ADDR + i, i < ssid.length() ? ssid[i] : 0);
    EEPROM.write(PASS_ADDR + i, i < pass.length() ? pass[i] : 0);
  }
  EEPROM.commit();
}

bool loadWiFiCredentials() {
  int magic = EEPROM.read(MAGIC_ADDR) | (EEPROM.read(MAGIC_ADDR + 1) << 8);
  if (magic != MAGIC_VALUE) {
    return false;
  }

  char ssid[64] = {0};
  char pass[64] = {0};

  for (int i = 0; i < 64; i++) {
    ssid[i] = EEPROM.read(SSID_ADDR + i);
    pass[i] = EEPROM.read(PASS_ADDR + i);
  }

  stored_ssid = String(ssid);
  stored_pass = String(pass);

  return (stored_ssid.length() > 0);
}

void saveConfig() {
  EEPROM.write(CONFIG_ADDR, current_config.tx_mode);
  EEPROM.write(CONFIG_ADDR + 1, current_config.sync_interval & 0xFF);
  EEPROM.write(CONFIG_ADDR + 2, (current_config.sync_interval >> 8) & 0xFF);
  EEPROM.commit();
}

void loadConfig() {
  uint8_t mode = EEPROM.read(CONFIG_ADDR);
  uint16_t interval = EEPROM.read(CONFIG_ADDR + 1) | (EEPROM.read(CONFIG_ADDR + 2) << 8);

  // Validate loaded values
  if (mode <= MODE_BOTH && interval > 0 && interval <= 3600) {
    current_config.tx_mode = mode;
    current_config.sync_interval = interval;
  } else {
    current_config = default_config;
  }
}

void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  ap_mode = true;

  JsonDocument doc;
  doc["status"] = "ap_mode";
  doc["ssid"] = AP_SSID;
  doc["ip"] = WiFi.softAPIP().toString();
  serializeJson(doc, Serial);
  Serial.println();
}

bool connectWiFi(const String& ssid, const String& pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifi_connected = true;

    JsonDocument doc;
    doc["status"] = "connected";
    doc["ssid"] = ssid;
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    serializeJson(doc, Serial);
    Serial.println();

    return true;
  }

  JsonDocument doc;
  doc["status"] = "connect_failed";
  doc["ssid"] = ssid;
  serializeJson(doc, Serial);
  Serial.println();

  return false;
}

void setupNTP() {
  configTime(NTP_OFFSET, 0, NTP_SERVER);

  // Wait for time sync (with timeout)
  int attempts = 0;
  while (time(nullptr) < 100000 && attempts < 20) {
    delay(500);
    attempts++;
  }

  if (time(nullptr) > 100000) {
    time_synced = true;

    JsonDocument doc;
    doc["status"] = "ntp_synced";
    doc["server"] = NTP_SERVER;
    serializeJson(doc, Serial);
    Serial.println();
  }
}

uint8_t calculateChecksum(const uint8_t* data, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; i++) {
    sum += data[i];
  }
  return sum;
}

void sendBinaryTimeData() {
  if (!time_synced) return;

  time_t now = time(nullptr);
  struct tm* timeinfo = gmtime(&now);

  // Get microsecond precision (ESP8266 uses micros())
  unsigned long current_micros = micros();
  uint32_t microseconds = current_micros % 1000000;

  BinaryTimePacket packet;
  packet.sync_marker = SYNC_MARKER;
  packet.epoch_seconds = (uint32_t)now;
  packet.microseconds = microseconds;
  packet.year = timeinfo->tm_year + 1900;
  packet.month = timeinfo->tm_mon + 1;
  packet.day = timeinfo->tm_mday;
  packet.hour = timeinfo->tm_hour;
  packet.minute = timeinfo->tm_min;
  packet.second = timeinfo->tm_sec;

  // Calculate checksum (excluding checksum field itself)
  packet.checksum = calculateChecksum((uint8_t*)&packet, sizeof(packet) - 1);

  // Send binary packet
  Serial.write((uint8_t*)&packet, sizeof(packet));
  Serial.flush();
}

void sendJsonTimeData() {
  if (!time_synced) return;

  time_t now = time(nullptr);
  struct tm* timeinfo = gmtime(&now);

  char time_str[32];
  strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", timeinfo);

  JsonDocument doc;
  doc["type"] = "time";
  doc["epoch"] = now;
  doc["utc"] = time_str;
  doc["year"] = timeinfo->tm_year + 1900;
  doc["month"] = timeinfo->tm_mon + 1;
  doc["day"] = timeinfo->tm_mday;
  doc["hour"] = timeinfo->tm_hour;
  doc["minute"] = timeinfo->tm_min;
  doc["second"] = timeinfo->tm_sec;

  serializeJson(doc, Serial);
  Serial.println();
}

void sendTimeData() {
  if (!time_synced) return;

  switch (current_config.tx_mode) {
    case MODE_JSON:
      sendJsonTimeData();
      break;
    case MODE_BINARY:
      sendBinaryTimeData();
      break;
    case MODE_BOTH:
      sendJsonTimeData();
      sendBinaryTimeData();
      break;
  }
}

/// @brief  Incomming serial command handler
void handleSerialCommand() 
{
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() == 0) return;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, input);

    if (error) {
      JsonDocument resp;
      resp["error"] = "invalid_json";
      resp["message"] = error.c_str();
      serializeJson(resp, Serial);
      Serial.println();
      return;
    }

    const char* cmd = doc["cmd"];

    if (cmd == nullptr) {
      JsonDocument resp;
      resp["error"] = "missing_cmd";
      serializeJson(resp, Serial);
      Serial.println();
      return;
    }

    if (strcmp(cmd, "set_wifi") == 0) {
      const char* ssid = doc["ssid"];
      const char* pass = doc["pass"];

      if (ssid && pass) {
        saveWiFiCredentials(String(ssid), String(pass));

        JsonDocument resp;
        resp["status"] = "credentials_saved";
        resp["action"] = "rebooting";
        serializeJson(resp, Serial);
        Serial.println();

        delay(1000);
        ESP.restart();
      } else {
        JsonDocument resp;
        resp["error"] = "missing_ssid_or_pass";
        serializeJson(resp, Serial);
        Serial.println();
      }
    }
    else if (strcmp(cmd, "get_time") == 0) {
      sendTimeData();
    }
    else if (strcmp(cmd, "get_status") == 0) {
      JsonDocument resp;
      resp["wifi_connected"] = wifi_connected;
      resp["ap_mode"] = ap_mode;
      resp["time_synced"] = time_synced;
      if (wifi_connected) {
        resp["ip"] = WiFi.localIP().toString();
        resp["rssi"] = WiFi.RSSI();
      }
      serializeJson(resp, Serial);
      Serial.println();
    }
    else if (strcmp(cmd, "restart") == 0) {
      JsonDocument resp;
      resp["status"] = "restarting";
      serializeJson(resp, Serial);
      Serial.println();
      delay(500);
      ESP.restart();
    }
    else if (strcmp(cmd, "set_mode") == 0) {
      const char* mode_str = doc["mode"];
      if (mode_str) {
        uint8_t mode = MODE_JSON;
        if (strcmp(mode_str, "json") == 0) {
          mode = MODE_JSON;
        } else if (strcmp(mode_str, "binary") == 0) {
          mode = MODE_BINARY;
        } else if (strcmp(mode_str, "both") == 0) {
          mode = MODE_BOTH;
        } else {
          JsonDocument resp;
          resp["error"] = "invalid_mode";
          resp["valid_modes"] = "json, binary, both";
          serializeJson(resp, Serial);
          Serial.println();
          return;
        }

        current_config.tx_mode = mode;
        saveConfig();

        JsonDocument resp;
        resp["status"] = "mode_updated";
        resp["mode"] = mode_str;
        serializeJson(resp, Serial);
        Serial.println();
      } else {
        JsonDocument resp;
        resp["error"] = "missing_mode";
        serializeJson(resp, Serial);
        Serial.println();
      }
    }
    else if (strcmp(cmd, "set_interval") == 0) {
      int interval = doc["interval"];
      if (interval > 0 && interval <= 3600) {
        current_config.sync_interval = interval;
        saveConfig();

        JsonDocument resp;
        resp["status"] = "interval_updated";
        resp["interval"] = interval;
        serializeJson(resp, Serial);
        Serial.println();
      } else {
        JsonDocument resp;
        resp["error"] = "invalid_interval";
        resp["message"] = "Interval must be 1-3600 seconds";
        serializeJson(resp, Serial);
        Serial.println();
      }
    }
    else if (strcmp(cmd, "get_config") == 0) {
      JsonDocument resp;
      resp["tx_mode"] = current_config.tx_mode;
      resp["tx_mode_name"] = (current_config.tx_mode == MODE_JSON) ? "json" :
                             (current_config.tx_mode == MODE_BINARY) ? "binary" : "both";
      resp["sync_interval"] = current_config.sync_interval;
      serializeJson(resp, Serial);
      Serial.println();
    }
    else {
      JsonDocument resp;
      resp["error"] = "unknown_command";
      resp["cmd"] = cmd;
      serializeJson(resp, Serial);
      Serial.println();
    }
  }
}

void setup() {
  Serial.begin(UART_BAUD);
  delay(100);

  EEPROM.begin(EEPROM_SIZE);

  // Load configuration
  loadConfig();

  JsonDocument doc;
  doc["status"] = "boot";
  doc["version"] = "2.0";
  doc["tx_mode"] = (current_config.tx_mode == MODE_JSON) ? "json" :
                   (current_config.tx_mode == MODE_BINARY) ? "binary" : "both";
  doc["sync_interval"] = current_config.sync_interval;
  serializeJson(doc, Serial);
  Serial.println();

  wifi_configured = loadWiFiCredentials();

  // If no credentials in EEPROM, try default credentials
  if (!wifi_configured) {
    stored_ssid = DEFAULT_SSID;
    stored_pass = DEFAULT_PASS;
    wifi_configured = true;
  }

  if (wifi_configured) {
    if (connectWiFi(stored_ssid, stored_pass)) {
      setupNTP();
      last_ntp_update = millis();
    } else {
      startAPMode();
    }
  } else {
    startAPMode();
  }

  last_status_send = millis();
}

void loop() {
  handleSerialCommand();

  if (wifi_connected) {
    // Periodic NTP update
    if (millis() - last_ntp_update > NTP_UPDATE_INTERVAL) {
      setupNTP();
      last_ntp_update = millis();
    }

    // Periodic time broadcast (using configurable interval)
    unsigned long sync_interval_ms = (unsigned long)current_config.sync_interval * 1000UL;
    if (time_synced && millis() - last_status_send > sync_interval_ms) {
      sendTimeData();
      last_status_send = millis();
    }
  }

  yield();
}