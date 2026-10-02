#include "net.h"

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <time.h>

#include "config.h"
#include "log.h"
#include "receipts.h"
#include "web.h"

namespace net {
namespace {

constexpr uint32_t kPortalAfterDisconnectMs = 10UL * 60 * 1000;
constexpr uint32_t kFallbackPortalSeconds = 5 * 60;

WiFiManager wm;
WiFiManagerParameter* serverParam = nullptr;
bool portalWasActive = false;
bool portalForOutage = false;  // opened because saved WiFi was unreachable
bool wasConnected = false;
bool servicesStarted = false;
bool bannerPrinted = false;
bool setupPrinted = false;
uint32_t disconnectedSince = 0;

void onSaveParams() {
    if (!serverParam) return;
    String url = serverParam->getValue();
    String err;
    if (!config::normaliseServerUrl(url, err)) {
        LOGE("setup: ignoring server URL: %s", err.c_str());
        return;
    }
    if (url != settings.serverUrl) {
        settings.serverUrl = url;
        settings.apiKey = "";
        settings.displayCode = "";
        config::save();
        LOGI("setup: server URL set to '%s'", url.c_str());
    }
}

// True for power-on and the reset button (or USB flashing). False for
// software restarts -- over-the-air updates, the web "Reboot" action -- and
// crash/watchdog recoveries, which shouldn't cost a receipt each time.
bool wasHumanStart() {
    uint32_t reason = ESP.getResetInfoPtr()->reason;
    return reason == REASON_DEFAULT_RST || reason == REASON_EXT_SYS_RST;
}

void startServices() {
    if (servicesStarted) return;
    servicesStarted = true;

    ArduinoOTA.setHostname(settings.deviceName.c_str());
    ArduinoOTA.setPassword(settings.adminPassword.c_str());
    ArduinoOTA.onStart([]() { LOGI("ota: update starting"); });
    ArduinoOTA.onEnd([]() { LOGI("ota: update complete, rebooting"); });
    ArduinoOTA.onError([](ota_error_t e) { LOGE("ota: error %u", (unsigned)e); });
    ArduinoOTA.begin();  // also starts mDNS as <deviceName>.local
    MDNS.addService("http", "tcp", 80);
    LOGI("net: http://%s.local ready", settings.deviceName.c_str());
}

// WiFiManager always registers firmware-upload (/u, /update) and WiFi-erase
// (/erase) routes with no authentication; setShowInfoUpdate(false) only hides
// the buttons. The portal is an open access point, so shadow those routes
// with handlers that refuse. ESP8266WebServer uses the first matching route,
// and this callback runs before WiFiManager adds its own.
void blockUnsafePortalRoutes() {
    auto refuse = []() { wm.server->send(404, F("text/plain"), F("Not available")); };
    auto ignoreUpload = []() {};
    for (const char* path : {"/u", "/update", "/erase"}) {
        wm.server->on(path, HTTP_ANY, refuse, ignoreUpload);
    }
}

void startPortal(uint32_t timeoutSeconds, bool forOutage) {
    if (wm.getConfigPortalActive()) return;
    portalForOutage = forOutage;
    // Show the current value, not the one from boot, so saving WiFi without
    // touching this field can't revert a server URL changed since.
    if (serverParam) serverParam->setValue(settings.serverUrl.c_str(), 120);
    web::stop();  // the portal needs port 80
    wm.setConfigPortalTimeout(timeoutSeconds);
    wm.startConfigPortal(apName().c_str());
    portalWasActive = true;
    if (forOutage) {
        // WiFiManager turns the station off when it opens the portal while
        // disconnected. Turn it back on so the printer rejoins by itself as
        // soon as the router is back.
        WiFi.enableSTA(true);
        WiFi.begin();
    }
    LOGI("net: setup portal open on WiFi '%s' (http://192.168.4.1)", apName().c_str());

    if (!setupPrinted && !wm.getWiFiIsSaved()) {
        setupPrinted = true;
        receipts::setupInstructions(apName());
    }
}

}  // namespace

void begin() {
    WiFi.persistent(true);  // WiFi credentials are stored by the SDK
    WiFi.mode(WIFI_STA);
    WiFi.hostname(settings.deviceName);
    WiFi.setAutoReconnect(true);

    // TLS certificate checks need the real time; timestamps need the zone.
    configTime(settings.timezone.length() ? settings.timezone.c_str() : "UTC0", "pool.ntp.org",
               "time.google.com", "time.cloudflare.com");

    // No "exit": on first boot it would close the portal with no WiFi saved.
    static const char* menu[] = {"wifi", "info"};
    wm.setMenu(menu, 2);
    wm.setTitle("Printr setup");
    wm.setConfigPortalBlocking(false);
    wm.setCaptivePortalEnable(true);
    wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                           IPAddress(255, 255, 255, 0));
    wm.setMinimumSignalQuality(20);
    wm.setHostname(settings.deviceName);
    wm.setShowInfoUpdate(false);  // its firmware upload page has no authentication
    wm.setShowInfoErase(false);
    wm.setWiFiAutoReconnect(true);
    wm.setSaveParamsCallback(onSaveParams);
    wm.setWebServerCallback(blockUnsafePortalRoutes);
    wm.setCustomHeadElement("<meta name='viewport' content='width=device-width, initial-scale=1'>");

    serverParam = new WiFiManagerParameter("server", "Cloud server URL (optional, leave blank for local-only)",
                                           settings.serverUrl.c_str(), 120);
    wm.addParameter(serverParam);

    disconnectedSince = millis();
    if (wm.getWiFiIsSaved()) {
        LOGI("net: connecting to saved WiFi '%s'", WiFi.SSID().c_str());
        WiFi.begin();
    } else {
        LOGI("net: no WiFi saved");
        startPortal(0, false);  // stays open until configured
    }
}

void loop() {
    if (portalWasActive) {
        wm.process();
        if (!wm.getConfigPortalActive()) {
            portalWasActive = false;
            LOGI("net: setup portal closed");
            web::start();
            disconnectedSince = millis();
            if (!wm.getWiFiIsSaved()) startPortal(0, false);  // still nothing configured
        } else if (portalForOutage && WiFi.isConnected()) {
            // Opened because the network was down, and it's back.
            LOGI("net: WiFi is back, closing setup portal");
            wm.stopConfigPortal();
        }
    }

    bool connected = WiFi.isConnected();
    if (connected && !wasConnected) {
        LOGI("net: connected to '%s', IP %s, RSSI %d dBm", WiFi.SSID().c_str(),
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
        startServices();
        if (!portalWasActive) web::start();
        if (settings.bootBanner && !bannerPrinted) {
            bannerPrinted = true;
            if (wasHumanStart()) {
                receipts::online(WiFi.localIP(), WiFi.SSID());
            } else {
                LOGI("net: quiet restart (%s), skipping status receipt", ESP.getResetReason().c_str());
            }
        }
    } else if (!connected && wasConnected) {
        LOGW("net: WiFi connection lost, reconnecting");
        disconnectedSince = millis();
    }
    wasConnected = connected;

    if (!connected && !portalWasActive && wm.getWiFiIsSaved() &&
        millis() - disconnectedSince > kPortalAfterDisconnectMs) {
        LOGW("net: WiFi unreachable for 10 min, opening setup portal");
        startPortal(kFallbackPortalSeconds, true);
    }

    if (servicesStarted) ArduinoOTA.handle();  // also runs mDNS
}

bool portalActive() { return wm.getConfigPortalActive(); }

void openPortal() { startPortal(kFallbackPortalSeconds, false); }

void forgetWifi() {
    LOGW("net: erasing saved WiFi and restarting");
    wm.resetSettings();
    delay(500);
    ESP.restart();
}

bool timeSynced() { return time(nullptr) > 1700000000; }

String apName() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String name = settings.deviceName + "-setup-" + mac.substring(8);
    // SSIDs are limited to 32 bytes; softAP() refuses longer ones.
    if (name.length() > 32) name = settings.deviceName.substring(0, 32 - 11) + "-setup-" + mac.substring(8);
    return name;
}

}  // namespace net
