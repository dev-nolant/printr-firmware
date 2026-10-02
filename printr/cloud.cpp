#include "cloud.h"

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <StreamString.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>

#include <memory>

#include "certs.h"
#include "config.h"
#include "log.h"
#include "receipts.h"
#include "textfmt.h"
#include "thermal.h"
#include "version.h"

namespace cloud {
namespace {

constexpr uint32_t kHttpTimeoutMs = 8000;
// The backend returns at most 10 messages of <= 500 chars each; 12 KB is
// generous while leaving room for the parsed JSON alongside it.
constexpr size_t kMaxResponseBytes = 12 * 1024;
constexpr uint32_t kBackoffMinMs = 5000;
constexpr uint32_t kBackoffMaxMs = 5UL * 60 * 1000;
constexpr uint32_t kPaperRecheckMs = 30000;
constexpr uint8_t kAuthFailuresBeforeReset = 3;
constexpr uint32_t kRegistrationRefusedRetryMs = 10UL * 60 * 1000;
constexpr size_t kMaxMessagesPerPoll = 10;
constexpr size_t kRecentIds = 32;
const char kCaPath[] = "/ca.pem";

State st = State::Disabled;
String lastError;

// Connection objects persist between requests so keep-alive and TLS session
// resumption work; a full TLS handshake costs ~1-2 s of CPU on the ESP8266,
// too much to pay on every poll. The TLS client is only
// allocated when an https server is configured: constructing one reserves
// BearSSL's ~6 KB stack, which local-only mode shouldn't pay for.
std::unique_ptr<BearSSL::WiFiClientSecure> tls;
WiFiClient plain;
HTTPClient http;
BearSSL::Session tlsSession;
std::unique_ptr<BearSSL::X509List> trust;
String preparedFor;  // serverUrl + tls mode the client is configured for
bool isHttps = false;

uint32_t lastPollMs = 0, lastHeartbeatMs = 0, backoffStartMs = 0;
uint32_t backoffMs = 0;
bool pollRequested = true, heartbeatRequested = true;
bool paperOut = false;
uint8_t failures = 0, authFailures = 0;
uint32_t printedCount = 0;
uint32_t lastSuccessMs = 0;
bool everSucceeded = false;

// IDs printed but not yet acknowledged by the server, and a ring of recently
// printed IDs. If an ack is lost the server resends the message; the ring
// stops it from printing twice.
std::vector<String> pendingAcks;
String recent[kRecentIds];
size_t recentNext = 0;

bool wasPrintedRecently(const String& id) {
    for (const String& r : recent) {
        if (r == id) return true;
    }
    return false;
}

void rememberPrinted(const String& id) {
    recent[recentNext] = id;
    recentNext = (recentNext + 1) % kRecentIds;
}

bool timeValid() { return time(nullptr) > 1700000000; }  // after Nov 2023

// A String sink for HTTPClient::writeToStream that refuses to grow past a
// limit, so a misbehaving server can't exhaust the heap.
class CappedString : public StreamString {
public:
    explicit CappedString(size_t cap) : cap_(cap) {}
    size_t write(const uint8_t* data, size_t len) override {
        if (length() + len > cap_) { overflow = true; return 0; }
        return StreamString::write(data, len);
    }
    size_t write(uint8_t c) override { return write(&c, 1); }
    // Reporting 0 once full makes the copy loop stop immediately instead of
    // spinning until the stream timeout.
    int availableForWrite() override { return overflow ? 0 : (int)(cap_ - length() + 1); }
    bool overflow = false;

private:
    size_t cap_;
};

void buildTrust() {
    trust.reset(new BearSSL::X509List());
    trust->append(kCaIsrgX1);
    trust->append(kCaIsrgX2);
    trust->append(kCaGtsR1);
    trust->append(kCaGtsR4);
    File f = LittleFS.open(kCaPath, "r");
    if (f) {
        String pem = f.readString();
        f.close();
        if (!trust->append(pem.c_str())) LOGW("cloud: stored custom CA is invalid, ignoring");
    }
}

void teardown() {
    http.end();
    tls.reset();  // frees TLS buffers (and the BearSSL stack if nothing else uses it)
    plain.stop();
    preparedFor = "";
}

// Configures the client for the current server. Returns false if the URL
// can't be used.
bool prepare() {
    String key = settings.serverUrl + (settings.tlsInsecure ? "|i" : "|v");
    if (key == preparedFor) return true;
    teardown();

    isHttps = settings.serverUrl.startsWith("https://");
    if (isHttps) {
        int hostStart = 8;
        int hostEnd = settings.serverUrl.indexOf('/', hostStart);
        String hostPort = settings.serverUrl.substring(hostStart, hostEnd < 0 ? settings.serverUrl.length() : hostEnd);
        int colon = hostPort.lastIndexOf(':');
        String host = colon > 0 ? hostPort.substring(0, colon) : hostPort;
        uint16_t port = colon > 0 ? hostPort.substring(colon + 1).toInt() : 443;
        if (host.length() == 0 || port == 0) {
            lastError = F("bad server URL");
            return false;
        }

        tls.reset(new BearSSL::WiFiClientSecure());
        tlsSession = BearSSL::Session();  // don't resume a session with another server
        tls->setSession(&tlsSession);
        if (settings.tlsInsecure) {
            LOGW("cloud: TLS certificate checks are DISABLED (tlsInsecure)");
            tls->setInsecure();
        } else {
            if (!trust) buildTrust();
            tls->setTrustAnchors(trust.get());
        }
        // Small TLS buffers save ~15 KB of RAM, but only if the server
        // supports max fragment length negotiation. Ask first; fall back to
        // the full 16 KB record size if not (otherwise large responses fail).
        // If the probe failed only because the server was unreachable, the
        // teardown after repeated failures (see fail()) re-probes later.
        if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(host.c_str(), port, 1024)) {
            tls->setBufferSizes(1024, 1024);
            LOGI("cloud: server supports MFLN, using 1 KB TLS buffers");
        } else {
            tls->setBufferSizes(16384, 512);
            LOGI("cloud: server lacks MFLN (or is unreachable), using 16 KB TLS receive buffer");
        }
    } else {
        LOGW("cloud: using plain http:// - traffic including the API key is unencrypted");
    }

    http.setReuse(true);
    http.setTimeout(kHttpTimeoutMs);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setUserAgent(F("printr/" PRINTR_FW_VERSION));
    preparedFor = key;
    return true;
}

// Errors that mean a kept-alive connection had silently gone away.
bool isStaleConnectionError(int code) {
    return code == HTTPC_ERROR_SEND_HEADER_FAILED || code == HTTPC_ERROR_SEND_PAYLOAD_FAILED ||
           code == HTTPC_ERROR_NOT_CONNECTED || code == HTTPC_ERROR_CONNECTION_LOST ||
           code == HTTPC_ERROR_READ_TIMEOUT;
}

int requestOnce(const char* method, const char* path, const String& body, String& response,
                bool withAuth) {
    response = "";
    if (isHttps && !settings.tlsInsecure) tls->setX509Time(time(nullptr));

    String url = settings.serverUrl + path;
    WiFiClient& client = isHttps ? static_cast<WiFiClient&>(*tls) : plain;
    if (!http.begin(client, url)) return HTTPC_ERROR_CONNECTION_FAILED;

    http.addHeader(F("Accept"), F("application/json"));
    if (body.length()) http.addHeader(F("Content-Type"), F("application/json"));
    if (withAuth) {
        http.addHeader(F("X-API-Key"), settings.apiKey);
        http.addHeader(F("X-Device-ID"), config::deviceId());
    }

    int code = http.sendRequest(method, body);
    if (code > 0) {
        int size = http.getSize();
        if (size > (int)kMaxResponseBytes) {
            LOGE("cloud: %s response too large (%d bytes)", path, size);
            code = HTTPC_ERROR_TOO_LESS_RAM;
        } else if (size != 0) {
            CappedString sink(kMaxResponseBytes);
            if (size > 0) sink.reserve(size);
            int n = http.writeToStream(&sink);
            if (sink.overflow) {
                LOGE("cloud: %s response exceeded %u bytes", path, kMaxResponseBytes);
                code = HTTPC_ERROR_TOO_LESS_RAM;
            } else if (n < 0) {
                code = n;
            } else {
                response = std::move(sink);
            }
        }
    }
    http.end();
    if (code < 0 && tls) tls->stop();  // never reuse a broken connection
    return code;
}

// Performs one request. Returns the HTTP status (or a negative HTTPClient
// error) and fills `response` with at most kMaxResponseBytes of body.
int request(const char* method, const char* path, const String& body, String& response,
            bool withAuth = true) {
    response = "";
    if (!prepare()) return HTTPC_ERROR_CONNECTION_FAILED;

    uint32_t heapBefore = ESP.getFreeHeap();
    bool reused = isHttps && tls->connected();
    int code = requestOnce(method, path, body, response, withAuth);
    if (reused && isStaleConnectionError(code)) {
        // The server (or a NAT box) closed the idle keep-alive connection.
        // That's routine, not a failure: reconnect once and retry.
        LOGD("cloud: kept-alive connection was stale, reconnecting");
        code = requestOnce(method, path, body, response, withAuth);
    }

    if (code < 0) {
        char err[64] = "";
        if (isHttps) tls->getLastSSLError(err, sizeof(err));
        LOGE("cloud: %s %s failed: %s%s%s", method, path, HTTPClient::errorToString(code).c_str(),
             err[0] ? " / " : "", err);
    } else {
        LOGD("cloud: %s %s -> %d (%u bytes, heap %u -> %u)", method, path, code, response.length(),
             heapBefore, ESP.getFreeHeap());
    }
    return code;
}

void succeed() {
    failures = 0;
    backoffMs = 0;
    authFailures = 0;
    lastSuccessMs = millis();
    everSucceeded = true;
    lastError = "";
    st = paperOut ? State::PaperOut : State::Online;
    if (paperOut) lastError = F("printer is out of paper");
}

void fail(const String& why) {
    failures = failures < 250 ? failures + 1 : failures;
    // 5 s, 10 s, 20 s ... capped at 5 min, with +/-20% jitter so a fleet of
    // printers doesn't hammer a recovering server in lockstep.
    uint32_t base = kBackoffMinMs << (failures - 1 < 6 ? failures - 1 : 6);
    if (base > kBackoffMaxMs) base = kBackoffMaxMs;
    int32_t jitter = (int32_t)(ESP.random() % (base / 5 + 1)) * 2 - (int32_t)(base / 5);
    backoffMs = base + jitter;
    backoffStartMs = millis();
    if (failures == 3) teardown();
    lastError = why;
    st = State::Backoff;
    LOGW("cloud: %s - retry in %lu s", why.c_str(), (unsigned long)(backoffMs / 1000));
}

// 401/403 means the server doesn't recognise our credentials (e.g. the
// printer was deleted). After a few in a row, start over with registration.
void authFailed(int code) {
    authFailures++;
    fail(String(F("server rejected credentials (HTTP ")) + code + ")");
    if (authFailures >= kAuthFailuresBeforeReset) {
        LOGW("cloud: credentials rejected %u times, re-registering", authFailures);
        settings.apiKey = "";
        settings.displayCode = "";
        config::save();
        authFailures = 0;
    }
}

void handleHttpError(int code, const __FlashStringHelper* what) {
    if (code == 401 || code == 403) {
        authFailed(code);
    } else if (code < 0) {
        fail(String(what) + F(": ") + HTTPClient::errorToString(code));
    } else {
        fail(String(what) + F(": HTTP ") + code);
    }
}

String dashboardUrlFrom(JsonDocument& resp) {
    const char* d = resp["dashboard_url"];
    if (d && *d) return d;
    // Fall back to the server's origin.
    int hostEnd = settings.serverUrl.indexOf('/', settings.serverUrl.indexOf("://") + 3);
    return hostEnd < 0 ? settings.serverUrl : settings.serverUrl.substring(0, hostEnd);
}

bool validCredential(const char* s, size_t maxLen) {
    if (!s) return false;
    size_t n = strlen(s);
    if (n < 8 || n > maxLen) return false;
    for (size_t i = 0; i < n; i++) {
        if (s[i] <= 0x20 || s[i] > 0x7E) return false;
    }
    return true;
}

void doRegister() {
    st = State::Registering;
    JsonDocument req;
    req["device_id"] = config::deviceId();
    req["device_secret"] = settings.deviceSecret;
    req["name"] = settings.deviceName;
    req["firmware"] = PRINTR_FW_VERSION;
    String body, resp;
    serializeJson(req, body);

    LOGI("cloud: registering with %s", settings.serverUrl.c_str());
    int code = request("POST", "/api/get-pairing-code", body, resp, false);
    if (code == 403) {
        // The server knows this device_id but won't re-issue credentials
        // (already paired, or the device secret changed). Retrying quickly
        // won't help; a human has to remove the printer on the dashboard.
        JsonDocument err;
        deserializeJson(err, resp);
        String why = err["error"] | "server refused registration";
        LOGE("cloud: registration refused: %s", why.c_str());
        failures = 0;
        backoffMs = kRegistrationRefusedRetryMs;
        backoffStartMs = millis();
        lastError = String(F("registration refused: ")) + why;
        st = State::RegistrationRefused;
        return;
    }
    if (code != 200 && code != 201) {
        handleHttpError(code, F("registration failed"));
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp);
    const char* apiKey = doc["api_key"];
    const char* displayCode = doc["display_code"] | "";
    if (err || !(doc["success"] | false) || !validCredential(apiKey, 128)) {
        // Never store a missing key as the literal string "null" (old bug).
        fail(F("registration: bad response from server"));
        return;
    }

    settings.apiKey = apiKey;
    settings.displayCode = String(displayCode).substring(0, 32);
    if (!config::save()) LOGE("cloud: could not persist credentials");
    succeed();

    const char* pairingCode = doc["pairing_code"];
    bool paired = doc["paired"] | false;
    if (!paired && pairingCode && *pairingCode) {
        LOGI("cloud: pairing code %s", pairingCode);
        receipts::pairing(pairingCode, dashboardUrlFrom(doc));
    } else {
        LOGI("cloud: registered, already paired (code %s)", settings.displayCode.c_str());
    }
    pollRequested = heartbeatRequested = true;
}

bool flushAcks() {
    if (pendingAcks.empty()) return true;
    JsonDocument req;
    JsonArray ids = req["message_ids"].to<JsonArray>();
    for (const String& id : pendingAcks) ids.add(id);
    String body, resp;
    serializeJson(req, body);

    int code = request("POST", "/api/messages/mark-specific-printed", body, resp);
    if (code == 200) {
        LOGI("cloud: acknowledged %u message(s)", pendingAcks.size());
        pendingAcks.clear();
        succeed();
        return true;
    }
    if (code == 400 || code == 404) {
        // The server doesn't know these IDs any more; retrying won't help.
        LOGW("cloud: server refused ack (HTTP %d), dropping %u id(s)", code, pendingAcks.size());
        pendingAcks.clear();
        return true;
    }
    handleHttpError(code, F("acknowledge failed"));
    return false;
}

String stringField(JsonObjectConst m, const char* a, const char* b) {
    JsonVariantConst v = m[a];
    if (v.isNull() && b) v = m[b];
    if (v.isNull()) return String();
    return v.as<String>();  // also turns numeric IDs into strings
}

void doPoll() {
    lastPollMs = millis();
    pollRequested = false;

    String resp;
    int code = request("GET", "/api/messages/pending", String(), resp);
    if (code != 200) {
        handleHttpError(code, F("poll failed"));
        return;
    }

    // Keep only the fields we use (the server also sends e.g. sender_email).
    JsonDocument filter;
    JsonObject f = filter["messages"][0].to<JsonObject>();
    for (const char* k : {"id", "message_text", "content", "sender_name", "senderName", "created_at"}) {
        f[k] = true;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp, DeserializationOption::Filter(filter));
    resp = String();  // free the raw text before printing
    if (err) {
        fail(String(F("poll: invalid JSON: ")) + err.c_str());
        return;
    }
    succeed();

    JsonArrayConst messages = doc["messages"];
    if (messages.size() == 0) return;
    LOGI("cloud: %u message(s) pending", messages.size());

    if (settings.checkPaper && thermal::paperStatus() == thermal::Paper::Out) {
        // Leave them on the server; they'll print once paper is loaded.
        if (!paperOut) LOGW("cloud: paper out, holding %u message(s)", messages.size());
        paperOut = true;
        st = State::PaperOut;
        lastError = F("printer is out of paper");
        return;
    }
    paperOut = false;

    size_t handled = 0;
    for (JsonObjectConst m : messages) {
        if (handled++ >= kMaxMessagesPerPoll) break;
        String id = stringField(m, "id", nullptr);
        if (id.length() == 0 || id.length() > 64) {
            LOGW("cloud: skipping message with missing/invalid id");
            continue;
        }
        if (wasPrintedRecently(id)) {
            LOGI("cloud: %s already printed, re-acknowledging", id.c_str());
        } else {
            String text = stringField(m, "message_text", "content");
            String from = stringField(m, "sender_name", "senderName");
            int64_t createdAt = 0;
            textfmt::parseIso8601(m["created_at"] | "", createdAt);
            if (text.length() == 0) {
                LOGW("cloud: message %s is empty, acknowledging without printing", id.c_str());
            } else {
                receipts::message(text.c_str(), from.c_str(), createdAt);
                printedCount++;
            }
            rememberPrinted(id);
        }
        bool queued = false;
        for (const String& p : pendingAcks) queued |= (p == id);
        if (!queued) pendingAcks.push_back(id);
    }
    flushAcks();
    // More may be waiting (server sends at most 10 at a time).
    if (messages.size() >= kMaxMessagesPerPoll) pollRequested = true;
}

void doHeartbeat() {
    lastHeartbeatMs = millis();
    heartbeatRequested = false;

    JsonDocument req;
    req["device_id"] = config::deviceId();
    req["uptime"] = (uint32_t)(micros64() / 1000000ULL);
    req["free_heap"] = ESP.getFreeHeap();
    req["rssi"] = WiFi.RSSI();
    req["firmware"] = PRINTR_FW_VERSION;
    String body, resp;
    serializeJson(req, body);

    int code = request("POST", "/api/heartbeat", body, resp);
    if (code == 200) {
        succeed();
    } else {
        handleHttpError(code, F("heartbeat failed"));
    }
}

}  // namespace

void begin() {
    st = settings.serverUrl.length() ? State::NoWifi : State::Disabled;
}

void loop() {
    if (settings.serverUrl.length() == 0) {
        if (st != State::Disabled) {
            teardown();
            pendingAcks.clear();
            st = State::Disabled;
        }
        return;
    }
    if (!WiFi.isConnected()) {
        st = State::NoWifi;
        return;
    }
    if (settings.serverUrl.startsWith("https://") && !settings.tlsInsecure && !timeValid()) {
        st = State::WaitingForTime;
        return;
    }
    uint32_t now = millis();
    if (backoffMs && now - backoffStartMs < backoffMs) return;

    if (settings.apiKey.length() == 0) {
        doRegister();
        return;
    }
    if (!pendingAcks.empty()) {
        flushAcks();
        return;
    }
    uint32_t pollInterval = (uint32_t)settings.pollSeconds * 1000;
    if (paperOut && pollInterval < kPaperRecheckMs) pollInterval = kPaperRecheckMs;
    if (pollRequested || now - lastPollMs >= pollInterval) {
        doPoll();
        return;
    }
    if (heartbeatRequested || now - lastHeartbeatMs >= (uint32_t)settings.heartbeatSeconds * 1000) {
        doHeartbeat();
    }
}

void pollNow() { pollRequested = true; backoffMs = 0; paperOut = false; }
void heartbeatNow() { heartbeatRequested = true; backoffMs = 0; }
void resetBackoff() { failures = 0; backoffMs = 0; authFailures = 0; }

void reRegister() {
    settings.apiKey = "";
    settings.displayCode = "";
    config::save();
    resetBackoff();
}

bool markAllPrinted() {
    if (settings.serverUrl.length() == 0 || settings.apiKey.length() == 0) return false;
    String resp;
    int code = request("POST", "/api/messages/mark-all-printed", String(), resp);
    if (code == 200) pendingAcks.clear();
    return code == 200;
}

void reloadTrust() {
    teardown();  // drop the TLS client before the trust list it points at
    trust.reset();
}

bool setCustomCa(const String& pem, String& error) {
    if (pem.length() == 0) {
        LittleFS.remove(kCaPath);
        reloadTrust();
        return true;
    }
    if (pem.length() > 8192 || pem.indexOf(F("-----BEGIN CERTIFICATE-----")) < 0) {
        error = F("expected a PEM certificate (-----BEGIN CERTIFICATE-----)");
        return false;
    }
    BearSSL::X509List test;
    if (!test.append(pem.c_str()) || test.getCount() == 0) {
        error = F("certificate could not be parsed");
        return false;
    }
    File f = LittleFS.open(kCaPath, "w");
    if (!f || f.print(pem) != pem.length()) {
        error = F("could not write to flash");
        return false;
    }
    f.close();
    reloadTrust();
    return true;
}

bool hasCustomCa() { return LittleFS.exists(kCaPath); }

State state() { return st; }

const char* stateName(State s) {
    switch (s) {
        case State::Disabled: return "disabled";
        case State::NoWifi: return "no_wifi";
        case State::WaitingForTime: return "waiting_for_time";
        case State::Registering: return "registering";
        case State::Online: return "online";
        case State::Backoff: return "backoff";
        case State::PaperOut: return "paper_out";
        case State::RegistrationRefused: return "registration_refused";
    }
    return "unknown";
}

void statusJson(JsonObject o) {
    o["state"] = stateName(st);
    o["server"] = settings.serverUrl;
    o["registered"] = settings.apiKey.length() > 0;
    o["display_code"] = settings.displayCode;
    o["last_error"] = lastError;
    o["consecutive_failures"] = failures;
    if (backoffMs) {
        uint32_t elapsed = millis() - backoffStartMs;
        o["retry_in_s"] = elapsed < backoffMs ? (backoffMs - elapsed) / 1000 : 0;
    }
    if (everSucceeded) o["last_success_s_ago"] = (millis() - lastSuccessMs) / 1000;
    o["pending_acks"] = pendingAcks.size();
    o["printed"] = printedCount;
    o["custom_ca"] = hasCustomCa();
    o["tls_insecure"] = settings.tlsInsecure;
}

}  // namespace cloud
