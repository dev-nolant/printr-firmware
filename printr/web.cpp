#include "web.h"

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <Updater.h>
#include <WiFiUdp.h>
#include <uri/UriBraces.h>

#include "cloud.h"
#include "config.h"
#include "log.h"
#include "net.h"
#include "receipts.h"
#include "thermal.h"
#include "version.h"
#include "webui.h"

namespace web {
namespace {

ESP8266WebServer server(80);
bool running = false;
bool routesInstalled = false;

// Brute-force protection for the admin password.
constexpr uint8_t kMaxAuthFailures = 10;
constexpr uint32_t kAuthLockoutMs = 60000;
uint8_t authFailures = 0;
uint32_t authLockedAt = 0;

// Deferred actions that must run after the HTTP response is sent.
enum class Deferred : uint8_t { None, Reboot, ForgetWifi, FactoryReset, Portal };
Deferred deferred = Deferred::None;
uint32_t deferredAt = 0;

void sendJson(int code, JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    server.sendHeader(F("Cache-Control"), F("no-store"));
    server.send(code, F("application/json"), out);
}

void sendError(int code, const String& msg) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = msg;
    sendJson(code, doc);
}

void sendOk(const char* key = nullptr, bool value = false) {
    JsonDocument doc;
    doc["ok"] = true;
    if (key) doc[key] = value;
    sendJson(200, doc);
}

// Browsers always send Origin on cross-site POSTs. Scripts and curl don't
// send it at all, and our own UI sends a matching one.
bool sameOrigin() {
    String origin = server.header(F("Origin"));
    if (origin.length() == 0 || origin == F("null")) return origin.length() == 0;
    const String& host = server.hostHeader();
    return origin == String(F("http://")) + host;
}

bool checkAdmin() {
    if (authFailures >= kMaxAuthFailures) {
        if (millis() - authLockedAt < kAuthLockoutMs) {
            sendError(429, F("too many failed logins, try again in a minute"));
            return false;
        }
        authFailures = 0;
    }
    if (server.authenticate("admin", settings.adminPassword.c_str())) {
        authFailures = 0;
        return true;
    }
    // Only count attempts that actually supplied credentials.
    if (server.hasHeader(F("Authorization"))) {
        if (++authFailures >= kMaxAuthFailures) {
            authLockedAt = millis();
            LOGW("web: admin login locked for 60 s after repeated failures");
        }
    }
    server.requestAuthentication(BASIC_AUTH, "printr", F("admin login required"));
    return false;
}

bool guardMutation(bool needsAdmin) {
    if (!sameOrigin()) {
        sendError(403, F("cross-origin request refused"));
        return false;
    }
    return needsAdmin ? checkAdmin() : true;
}

void handleRoot() {
    server.sendHeader(F("Cache-Control"), F("no-store"));
    server.send_P(200, PSTR("text/html; charset=utf-8"), kIndexHtml, sizeof(kIndexHtml) - 1);
}

void handleStatus() {
    JsonDocument doc;
    doc["device_id"] = config::deviceId();
    doc["name"] = settings.deviceName;
    doc["firmware"] = PRINTR_FW_VERSION;
    doc["uptime_s"] = (uint32_t)(micros64() / 1000000ULL);
    doc["reset_reason"] = ESP.getResetReason();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["max_block"] = ESP.getMaxFreeBlockSize();
    doc["heap_fragmentation"] = ESP.getHeapFragmentation();
    JsonObject w = doc["wifi"].to<JsonObject>();
    w["connected"] = WiFi.isConnected();
    w["ssid"] = WiFi.SSID();
    w["ip"] = WiFi.localIP().toString();
    w["rssi"] = WiFi.RSSI();
    w["portal"] = net::portalActive();
    doc["time_synced"] = net::timeSynced();
    cloud::statusJson(doc["cloud"].to<JsonObject>());
    // Flat fields kept for clients of the older /status format.
    doc["display_code"] = settings.displayCode;
    doc["registered"] = settings.apiKey.length() > 0;
    doc["cloud_connected"] = cloud::state() == cloud::State::Online;
    doc["wifi_connected"] = WiFi.isConnected();
    doc["local_ip"] = WiFi.localIP().toString();
    sendJson(200, doc);
}

bool paperOk() {
    if (settings.checkPaper && thermal::paperStatus() == thermal::Paper::Out) {
        sendError(503, F("printer is out of paper"));
        return false;
    }
    return true;
}

void printBody(const String& text, const String& from) {
    if (text.length() == 0) {
        sendError(400, F("nothing to print"));
        return;
    }
    if (text.length() > kMaxMessageBytes) {
        sendError(413, String(F("message too long (max ")) + kMaxMessageBytes + F(" bytes)"));
        return;
    }
    if (!paperOk()) return;
    receipts::message(text.c_str(), from.c_str(), (int64_t)time(nullptr));
    JsonDocument doc;
    doc["ok"] = true;
    doc["status"] = "printed";  // legacy field
    sendJson(200, doc);
}

void handlePrint() {
    if (!guardMutation(settings.requireAuthToPrint)) return;
    const String& body = server.arg(F("plain"));
    String contentType = server.header(F("Content-Type"));
    if (contentType.startsWith(F("application/json"))) {
        JsonDocument doc;
        if (deserializeJson(doc, body) || !doc["text"].is<const char*>()) {
            sendError(400, F("expected JSON {\"text\": \"...\", \"from\": \"...\"}"));
            return;
        }
        printBody(doc["text"].as<String>(), doc["from"] | "Local network");
    } else {
        printBody(body, server.hasArg(F("from")) ? server.arg(F("from")) : String(F("Local network")));
    }
}

void handleLegacySend() {
    if (!guardMutation(settings.requireAuthToPrint)) return;
    if (!server.hasArg(F("plain"))) {
        sendError(400, F("Missing payload"));
        return;
    }
    printBody(server.arg(F("plain")), F("Local Network"));
}

void handleGetSettings() {
    if (!checkAdmin()) return;
    JsonDocument doc;
    config::toJson(doc.to<JsonObject>(), false);
    sendJson(200, doc);
}

void handlePostSettings() {
    if (!guardMutation(true)) return;
    JsonDocument doc;
    if (deserializeJson(doc, server.arg(F("plain"))) || !doc.is<JsonObject>()) {
        sendError(400, F("expected a JSON object"));
        return;
    }
    String err;
    bool reboot = false;
    String oldServer = settings.serverUrl;
    String oldTz = settings.timezone;
    if (!config::applyJson(doc.as<JsonObjectConst>(), err, reboot)) {
        sendError(400, err);
        return;
    }
    if (!config::save()) {
        sendError(500, F("could not save settings to flash"));
        return;
    }
    if (settings.timezone != oldTz) {
        setenv("TZ", settings.timezone.length() ? settings.timezone.c_str() : "UTC0", 1);
        tzset();
    }
    if (settings.serverUrl != oldServer) cloud::resetBackoff();
    LOGI("web: settings updated%s", reboot ? " (reboot required)" : "");
    sendOk("reboot_required", reboot);
}

void handleAction() {
    if (!guardMutation(true)) return;
    String a = server.pathArg(0);

    if (a == F("test")) {
        receipts::testPage();
    } else if (a == F("chartest")) {
        receipts::characterTest();
    } else if (a == F("poll")) {
        cloud::pollNow();
    } else if (a == F("heartbeat")) {
        cloud::heartbeatNow();
    } else if (a == F("register")) {
        cloud::reRegister();
    } else if (a == F("markall")) {
        if (!cloud::markAllPrinted()) {
            sendError(502, F("server did not accept the request"));
            return;
        }
    } else if (a == F("portal")) {
        deferred = Deferred::Portal;
    } else if (a == F("reboot")) {
        deferred = Deferred::Reboot;
    } else if (a == F("forget-wifi")) {
        deferred = Deferred::ForgetWifi;
    } else if (a == F("factory-reset")) {
        deferred = Deferred::FactoryReset;
    } else {
        sendError(404, F("unknown action"));
        return;
    }
    deferredAt = millis();
    sendOk();
}

void handleCa() {
    if (!guardMutation(true)) return;
    String err;
    if (!cloud::setCustomCa(server.arg(F("plain")), err)) {
        sendError(400, err);
        return;
    }
    sendOk();
}

// ---- Firmware upload ------------------------------------------------------
// Our own handler instead of ESP8266HTTPUpdateServer, which only checks the
// password. Browsers keep sending cached Basic credentials, so without the
// Origin check any website could flash firmware once you'd logged in.
bool uploadAllowed = false;
String uploadError;

void handleUpdatePage() {
    if (!checkAdmin()) return;
    server.send_P(200, PSTR("text/html; charset=utf-8"), PSTR(
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Printr update</title><body style='font:16px system-ui;margin:2em'>"
        "<h2>Firmware update</h2><p>Upload a .bin built for this board. The printer reboots when done.</p>"
        "<form method=POST enctype=multipart/form-data><input type=file name=firmware accept=.bin required> "
        "<button>Update</button></form><p><a href='/'>Back</a></p>"));
}

void handleUpdateUpload() {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        uploadError = "";
        uploadAllowed = sameOrigin() && server.authenticate("admin", settings.adminPassword.c_str());
        if (!uploadAllowed) return;
        LOGI("ota: receiving %s", up.filename.c_str());
        WiFiUDP::stopAll();
        uint32_t maxSize = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSize, U_FLASH)) uploadError = Update.getErrorString();
    } else if (!uploadAllowed || uploadError.length()) {
        if (up.status == UPLOAD_FILE_ABORTED && Update.isRunning()) Update.end();
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) uploadError = Update.getErrorString();
    } else if (up.status == UPLOAD_FILE_END) {
        if (!Update.end(true)) uploadError = Update.getErrorString();
        else LOGI("ota: %u bytes written", up.totalSize);
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.end();
        uploadError = F("upload aborted");
    }
    yield();
}

void handleUpdateDone() {
    if (!uploadAllowed) {
        if (!sameOrigin()) sendError(403, F("cross-origin request refused"));
        else checkAdmin();  // sends 401 / lockout response
        return;
    }
    if (uploadError.length() || Update.hasError()) {
        LOGE("ota: update failed: %s", uploadError.c_str());
        sendError(500, String(F("update failed: ")) + uploadError);
        return;
    }
    server.send(200, F("text/html"), F("<meta http-equiv=refresh content='15;url=/'>Update OK, rebooting..."));
    deferred = Deferred::Reboot;
    deferredAt = millis();
}

void handleNotFound() {
    sendError(404, F("not found"));
}

void installRoutes() {
    // Authorization is always collected by the server.
    server.collectHeaders("Origin", "Content-Type");

    server.on(F("/"), HTTP_GET, handleRoot);
    server.on(F("/api/status"), HTTP_GET, handleStatus);
    server.on(F("/status"), HTTP_GET, handleStatus);
    server.on(F("/config"), HTTP_GET, []() {
        server.sendHeader(F("Location"), F("/"));
        server.send(302);
    });
    server.on(F("/api/print"), HTTP_POST, handlePrint);
    server.on(F("/send"), HTTP_POST, handleLegacySend);
    server.on(F("/api/settings"), HTTP_GET, handleGetSettings);
    server.on(F("/api/settings"), HTTP_POST, handlePostSettings);
    server.on(UriBraces("/api/action/{}"), HTTP_POST, handleAction);
    server.on(F("/api/ca"), HTTP_POST, handleCa);
    server.onNotFound(handleNotFound);
    server.on(F("/update"), HTTP_GET, handleUpdatePage);
    server.on(F("/update"), HTTP_POST, handleUpdateDone, handleUpdateUpload);
}

}  // namespace

void begin() {
    // Routes are installed once; start() is called when WiFi comes up.
}

void start() {
    if (running) return;
    if (!routesInstalled) {
        installRoutes();
        routesInstalled = true;
    }
    server.begin();
    running = true;
    LOGI("web: listening on port 80");
}

void stop() {
    if (!running) return;
    server.stop();
    running = false;
}

void loop() {
    if (running) server.handleClient();

    // Give the HTTP response a moment to leave before acting.
    if (deferred != Deferred::None && millis() - deferredAt > 500) {
        Deferred d = deferred;
        deferred = Deferred::None;
        switch (d) {
            case Deferred::Reboot:
                LOGI("web: reboot requested");
                ESP.restart();
                break;
            case Deferred::ForgetWifi:
                net::forgetWifi();
                break;
            case Deferred::FactoryReset:
                LOGW("web: factory reset requested");
                config::factoryReset();
                net::forgetWifi();  // restarts
                break;
            case Deferred::Portal:
                net::openPortal();
                break;
            default:
                break;
        }
    }
}

}  // namespace web
