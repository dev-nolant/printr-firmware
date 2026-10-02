// Printr: WiFi thermal printer firmware for ESP8266 + Adafruit-compatible
// TTL thermal printers.
//
// See firmware/README.md for wiring, build/flash instructions and the API.
//
// Module map:
//   config    settings persisted to flash (LittleFS)
//   thermal   printer driver + Receipt layout (handles upside-down mounting)
//   textfmt   UTF-8 -> printer charset, emoji fallbacks, word wrap (unit tested)
//   receipts  every receipt the device prints
//   net       WiFi, setup portal, NTP, mDNS, network OTA
//   web       local HTTP API + web UI
//   cloud     optional server: pairing, message polling, heartbeats
//   console   serial commands

#include <ESP8266WiFi.h>

#include "cloud.h"
#include "config.h"
#include "console.h"
#include "log.h"
#include "net.h"
#include "textfmt.h"
#include "thermal.h"
#include "version.h"
#include "web.h"

namespace {

// If the heap is too fragmented for a TLS connection for this long, restart
// cleanly rather than limp along failing every request.
constexpr uint32_t kLowMemoryRestartMs = 5UL * 60 * 1000;
constexpr uint32_t kLowMemoryBlock = 6 * 1024;
uint32_t lowMemorySince = 0;

void checkHealth() {
    static uint32_t lastCheck = 0;
    if (millis() - lastCheck < 1000) return;
    lastCheck = millis();

    uint32_t block = ESP.getMaxFreeBlockSize();
    if (block >= kLowMemoryBlock) {
        lowMemorySince = 0;
        return;
    }
    if (lowMemorySince == 0) {
        lowMemorySince = millis();
        LOGW("health: low memory (max block %u B, free %u B)", block, ESP.getFreeHeap());
    } else if (millis() - lowMemorySince > kLowMemoryRestartMs) {
        LOGE("health: memory low for 5 min, restarting");
        delay(100);
        ESP.restart();
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial.println();
    LOGI("Printr firmware %s, reset reason: %s", PRINTR_FW_VERSION, ESP.getResetReason().c_str());

    if (!textfmt::tablesSorted()) LOGE("textfmt: lookup tables are not sorted - fix textfmt.cpp");

    config::begin();
    LOGI("device %s, name %s.local", config::deviceId().c_str(), settings.deviceName.c_str());

    thermal::begin();
    web::begin();
    net::begin();
    cloud::begin();

    console::printHelp();
}

void loop() {
    net::loop();
    web::loop();
    console::loop();
    cloud::loop();
    checkHealth();
    // Short sleep lets the WiFi modem idle between iterations (saves power
    // and heat) while keeping HTTP responses snappy.
    delay(2);
}
