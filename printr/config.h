// Persistent device settings.
//
// Stored as /config.json on LittleFS. Writes go to a temp file that is then
// renamed over the real one, so a power cut mid-save can't corrupt it.
// Every field is validated on load; a bad value falls back to its default.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// ---- Hardware (compile-time) ------------------------------------------------
// Printer TX/RX wiring. The printer's RX goes to PIN_PRINTER_TX.
#ifndef PIN_PRINTER_TX
#define PIN_PRINTER_TX D5
#endif
#ifndef PIN_PRINTER_RX
#define PIN_PRINTER_RX D6
#endif

// Hard limits that protect RAM (the ESP8266 has ~40 KB free at runtime).
constexpr size_t kMaxMessageBytes = 4096;   // UTF-8 bytes accepted per message
constexpr size_t kMaxMessageLines = 120;    // printed lines per message
constexpr size_t kMaxSenderChars = 24;

struct Settings {
    // Identity & access
    String deviceName = "printr";  // mDNS name (printr.local) and setup AP name
    String adminPassword;          // HTTP basic auth (user "admin") + OTA password
    bool requireAuthToPrint = false;

    // Printer
    bool upsideDown = true;
    uint8_t lineWidth = 32;
    uint32_t printerBaud = 9600;
    uint8_t heatDots = 7;
    uint8_t heatTime = 80;
    uint8_t heatInterval = 2;
    uint8_t printDensity = 10;
    uint8_t printBreakTime = 3;
    uint8_t feedLines = 4;          // lines fed after each receipt: clears the tear bar (header prints last when upside down)
    bool bootBanner = true;         // print "ONLINE" receipt on power-on (not after OTA/software restarts)
    bool checkPaper = true;         // hold cloud messages while paper is out (needs RX wired)
    String timezone;                // POSIX TZ, e.g. "EST5EDT,M3.2.0,M11.1.0"; empty = no timestamps

    // Cloud (optional). Empty serverUrl = local-only mode.
    String serverUrl;
    bool tlsInsecure = false;       // skip certificate checks (only for self-signed dev servers)
    uint16_t pollSeconds = 5;
    uint16_t heartbeatSeconds = 60;

    // Managed by the device, never edited directly by users.
    String apiKey;
    String displayCode;
    String deviceSecret;            // random, proves to the server we are the same device
};

extern Settings settings;

namespace config {

// Mounts the filesystem and loads settings (falling back to defaults).
// Generates the device secret and admin password on first boot.
void begin();

bool save();

// Deletes config and returns to defaults (WiFi credentials are separate).
void factoryReset();

// Apply user-editable fields from JSON. Unknown keys are rejected so typos
// don't silently do nothing. Validates everything before changing anything.
// Sets rebootNeeded if a changed field only takes effect after restart.
bool applyJson(JsonObjectConst in, String& error, bool& rebootNeeded);

// Serialise settings. Secrets (API key, device secret, admin password) are
// only included when includeSecrets is true.
void toJson(JsonObject out, bool includeSecrets);

// Normalise and validate a server URL ("" is valid: disables cloud).
bool normaliseServerUrl(String& url, String& error);

String deviceId();  // "ESP_" + MAC without colons

}  // namespace config
