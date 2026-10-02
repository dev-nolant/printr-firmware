// Optional cloud connection: registration/pairing, message polling,
// delivery acknowledgement and heartbeats.
//
// Everything runs from cloud::loop(), which performs at most one HTTP request
// per call so the local web server stays responsive in between.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace cloud {

enum class State : uint8_t {
    Disabled,        // no server URL configured: local-only mode
    NoWifi,
    WaitingForTime,  // certificate checks need a correct clock (NTP)
    Registering,
    Online,
    Backoff,         // last request failed; retrying with exponential backoff
    PaperOut,        // messages are waiting but the printer has no paper
    RegistrationRefused,  // server knows this device but refuses new credentials
};

void begin();
void loop();

void pollNow();
void heartbeatNow();
void reRegister();       // forget credentials and register again
bool markAllPrinted();   // blocking; for recovering from a stuck queue
void resetBackoff();
void reloadTrust();      // after the custom CA changes

State state();
const char* stateName(State s);
void statusJson(JsonObject out);

// Custom CA stored on flash, trusted in addition to the bundled roots.
bool setCustomCa(const String& pem, String& error);
bool hasCustomCa();

}  // namespace cloud
