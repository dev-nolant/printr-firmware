// Every receipt the device prints, in one place.
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace receipts {

// createdAt: seconds since epoch, or 0 to omit the timestamp. A timestamp is
// only printed when a timezone is configured.
void message(const char* text, const char* from, int64_t createdAt);

void setupInstructions(const String& apName);
void online(const IPAddress& ip, const String& ssid);
void pairing(const String& pairingCode, const String& dashboardUrl);
void notice(const char* title, const char* body);
void testPage();
void characterTest();

}  // namespace receipts
