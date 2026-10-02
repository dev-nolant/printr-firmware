// WiFi connection, setup portal, time sync, mDNS and network OTA.
//
//   - The setup portal only opens automatically when no WiFi is saved. With
//     saved WiFi the device keeps retrying in the background, so a router
//     reboot or power cut doesn't leave it stuck in setup mode.
//   - If saved WiFi stays unreachable for 10 minutes, the portal opens
//     alongside the retries so the network can be changed, without printing.
//   - The portal runs non-blocking, so the rest of the firmware keeps going.
#pragma once

#include <Arduino.h>

namespace net {

void begin();
void loop();

bool portalActive();
void openPortal();   // manual (serial "C" / web action)
void forgetWifi();   // erase saved WiFi and restart into setup
bool timeSynced();
String apName();

}  // namespace net
