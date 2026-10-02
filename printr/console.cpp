#include "console.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

#include "cloud.h"
#include "config.h"
#include "log.h"
#include "net.h"
#include "receipts.h"
#include "thermal.h"
#include "version.h"

namespace console {
namespace {

constexpr size_t kMaxLine = 160;
String line;
bool overflow = false;

String mask(const String& secret) {
    if (secret.length() <= 6) return secret.length() ? F("***") : F("(none)");
    return secret.substring(0, 4) + F("...") + secret.substring(secret.length() - 2);
}

void showInfo() {
    Serial.printf_P(PSTR("Device ID:      %s\n"), config::deviceId().c_str());
    Serial.printf_P(PSTR("Firmware:       %s\n"), PRINTR_FW_VERSION);
    Serial.printf_P(PSTR("Name:           %s.local\n"), settings.deviceName.c_str());
    Serial.printf_P(PSTR("Admin password: %s\n"), settings.adminPassword.c_str());
    Serial.printf_P(PSTR("WiFi:           %s (%s)\n"), WiFi.SSID().c_str(),
                    WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "disconnected");
    Serial.printf_P(PSTR("Server:         %s\n"),
                    settings.serverUrl.length() ? settings.serverUrl.c_str() : "(none, local only)");
    Serial.printf_P(PSTR("API key:        %s\n"), mask(settings.apiKey).c_str());
    Serial.printf_P(PSTR("Display code:   %s\n"), settings.displayCode.c_str());
    Serial.printf_P(PSTR("Cloud state:    %s\n"), cloud::stateName(cloud::state()));
}

void showStatus() {
    Serial.printf_P(PSTR("Free heap:      %u B (max block %u B, fragmentation %u%%)\n"),
                    ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(), ESP.getHeapFragmentation());
    Serial.printf_P(PSTR("Uptime:         %lu s\n"), (unsigned long)(micros64() / 1000000ULL));
    Serial.printf_P(PSTR("Last reset:     %s\n"), ESP.getResetReason().c_str());
    Serial.printf_P(PSTR("WiFi RSSI:      %d dBm\n"), WiFi.RSSI());
    Serial.printf_P(PSTR("Time synced:    %s\n"), net::timeSynced() ? "yes" : "no");
    Serial.printf_P(PSTR("Paper:          %s\n"), thermal::paperName(thermal::paperStatus()));
    JsonDocument doc;
    cloud::statusJson(doc.to<JsonObject>());
    Serial.print(F("Cloud:          "));
    serializeJson(doc, Serial);
    Serial.println();
}

void setServer(const String& arg) {
    JsonDocument doc;
    doc["serverUrl"] = arg;
    String err;
    bool reboot;
    if (!config::applyJson(doc.as<JsonObjectConst>(), err, reboot) || !config::save()) {
        Serial.printf_P(PSTR("Error: %s\n"), err.length() ? err.c_str() : "could not save");
        return;
    }
    cloud::resetBackoff();
    Serial.printf_P(PSTR("Server set to '%s'\n"), settings.serverUrl.c_str());
}

void run(String cmd) {
    cmd.trim();
    if (cmd.length() == 0) return;
    char c = toupper(cmd[0]);
    String arg = cmd.substring(1);
    arg.trim();

    switch (c) {
        case '?': printHelp(); break;
        case 'I': showInfo(); break;
        case 'S': showStatus(); break;
        case 'T': Serial.println(F("Printing test page...")); receipts::testPage(); break;
        case 'P': Serial.println(F("Printing character test...")); receipts::characterTest(); break;
        case 'M': Serial.println(F("Checking for messages...")); cloud::pollNow(); break;
        case 'H': Serial.println(F("Sending heartbeat...")); cloud::heartbeatNow(); break;
        case 'F': cloud::resetBackoff(); Serial.println(F("Failure state reset.")); break;
        case 'R': Serial.println(F("Re-registering with server...")); cloud::reRegister(); break;
        case 'X':
            Serial.println(cloud::markAllPrinted() ? F("All messages marked as printed.")
                                                   : F("Failed (is a server configured and registered?)"));
            break;
        case 'U':
            if (cmd.length() == 1) {
                Serial.println(F("Usage: U <url>   e.g. U https://printr.example.com   (U - to clear)"));
            } else {
                setServer(arg == "-" ? String() : arg);
            }
            break;
        case 'C': Serial.println(F("Opening WiFi setup portal...")); net::openPortal(); break;
        case 'W': Serial.println(F("Forgetting WiFi...")); net::forgetWifi(); break;
        case 'B': Serial.println(F("Rebooting...")); delay(100); ESP.restart(); break;
        case 'Z':
            if (arg != F("CONFIRM")) {
                Serial.println(F("Erases all settings and WiFi. Type: Z CONFIRM"));
            } else {
                config::factoryReset();
                net::forgetWifi();
            }
            break;
        default:
            if (cmd.length() > 0 && !isPrintable(cmd[0])) break;  // line noise
            Serial.printf_P(PSTR("Unknown command '%s'. Type ? for help.\n"), cmd.c_str());
    }
}

}  // namespace

void printHelp() {
    Serial.println(F(
        "\nCommands (press Enter after each):\n"
        "  ?          this help\n"
        "  I          device info (incl. admin password)\n"
        "  S          system status\n"
        "  T          print test page\n"
        "  P          print character test\n"
        "  M          check for messages now\n"
        "  H          send heartbeat now\n"
        "  F          reset failure/backoff state\n"
        "  R          re-register with server\n"
        "  X          mark all server messages as printed\n"
        "  U <url>    set server URL (U - to clear)\n"
        "  C          open WiFi setup portal\n"
        "  W          forget WiFi and restart\n"
        "  B          reboot\n"
        "  Z CONFIRM  factory reset\n"));
}

void loop() {
    // Bounded per call so a flood of serial input can't starve everything else.
    for (int budget = 64; budget > 0 && Serial.available(); budget--) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (!overflow) run(line);
            else Serial.println(F("Command too long, ignored."));
            line = "";
            overflow = false;
        } else if (line.length() < kMaxLine) {
            line += c;
        } else {
            overflow = true;
        }
    }
}

}  // namespace console
