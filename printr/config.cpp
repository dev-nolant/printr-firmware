#include "config.h"

#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "log.h"

Settings settings;

namespace config {

bool applyInto(JsonObjectConst in, Settings& s, String& error, bool& rebootNeeded, bool internal);

namespace {

constexpr int kSchemaVersion = 1;
const char kPath[] = "/config.json";
const char kTmpPath[] = "/config.tmp";

String randomToken(size_t len, const char* alphabet) {
    size_t n = strlen(alphabet);
    String s;
    s.reserve(len);
    for (size_t i = 0; i < len; i++) {
        // ESP.random() reads the hardware RNG; modulo bias is negligible for n <= 64.
        s += alphabet[ESP.random() % n];
    }
    return s;
}

bool isPrintableAscii(const String& s) {
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] < 0x20 || s[i] > 0x7E) return false;
    }
    return true;
}

bool validHostname(const String& s) {
    if (s.length() < 1 || s.length() > 24) return false;
    if (!isalpha((unsigned char)s[0]) || s[s.length() - 1] == '-') return false;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (!(isalnum((unsigned char)c) || c == '-')) return false;
    }
    return true;
}

bool validCredential(const String& s, size_t maxLen) {
    return s.length() > 0 && s.length() <= maxLen && isPrintableAscii(s) && s.indexOf(' ') < 0;
}

template <typename T>
bool readUint(JsonVariantConst v, T& out, uint32_t lo, uint32_t hi) {
    if (!v.is<uint32_t>()) return false;
    uint32_t x = v.as<uint32_t>();
    if (x < lo || x > hi) return false;
    out = (T)x;
    return true;
}

// Loads every field present in `obj`, ignoring bad values (keeps defaults).
// Used for the stored file, where one bad field shouldn't lose the rest.
void loadLenient(JsonObjectConst obj, Settings& s) {
    String err;
    bool reboot;
    for (JsonPairConst kv : obj) {
        JsonDocument one;
        one[kv.key()] = kv.value();
        Settings trial = s;
        // Reuse the strict validator one field at a time.
        if (applyInto(one.as<JsonObjectConst>(), trial, err, reboot, true)) {
            s = trial;
        } else {
            LOGW("config: ignoring stored %s (%s)", kv.key().c_str(), err.c_str());
        }
    }
}

}  // namespace

bool normaliseServerUrl(String& url, String& error) {
    url.trim();
    while (url.endsWith("/")) url.remove(url.length() - 1);
    if (url.length() == 0) return true;
    if (url.length() > 120) {
        error = F("serverUrl too long");
        return false;
    }
    if (!url.startsWith("https://") && !url.startsWith("http://")) {
        error = F("serverUrl must start with https:// or http://");
        return false;
    }
    int hostStart = url.indexOf("://") + 3;
    if ((int)url.length() <= hostStart || !isPrintableAscii(url) || url.indexOf(' ') >= 0) {
        error = F("serverUrl is not a valid URL");
        return false;
    }
    return true;
}

// `internal` allows device-managed fields (credentials) to be loaded from the
// stored file; they can never be set through the settings API.
bool applyInto(JsonObjectConst in, Settings& s, String& error, bool& rebootNeeded, bool internal) {
    rebootNeeded = false;
    for (JsonPairConst kv : in) {
        const char* k = kv.key().c_str();
        JsonVariantConst v = kv.value();
        bool ok = true;

        if (!strcmp(k, "deviceName")) {
            String x = v.as<String>();
            x.toLowerCase();
            ok = v.is<const char*>() && validHostname(x);
            if (ok && x != s.deviceName) { s.deviceName = x; rebootNeeded = true; }
            if (!ok) error = F("deviceName: 1-24 letters, digits or '-', starting with a letter");
        } else if (!strcmp(k, "adminPassword")) {
            String x = v.as<String>();
            ok = v.is<const char*>() && x.length() >= 8 && x.length() <= 64 && isPrintableAscii(x);
            if (ok && x != s.adminPassword) { s.adminPassword = x; rebootNeeded = true; }
            if (!ok) error = F("adminPassword: 8-64 printable characters");
        } else if (!strcmp(k, "requireAuthToPrint")) {
            ok = v.is<bool>();
            if (ok) s.requireAuthToPrint = v.as<bool>();
        } else if (!strcmp(k, "upsideDown")) {
            ok = v.is<bool>();
            if (ok) s.upsideDown = v.as<bool>();
        } else if (!strcmp(k, "lineWidth")) {
            ok = readUint(v, s.lineWidth, 16, 64);
            if (!ok) error = F("lineWidth: 16-64");
        } else if (!strcmp(k, "printerBaud")) {
            uint32_t b = 0;
            ok = readUint(v, b, 1200, 115200) &&
                 (b == 9600 || b == 19200 || b == 38400 || b == 57600 || b == 115200);
            if (ok && b != s.printerBaud) { s.printerBaud = b; rebootNeeded = true; }
            if (!ok) error = F("printerBaud: 9600, 19200, 38400, 57600 or 115200");
        } else if (!strcmp(k, "heatDots")) {
            ok = readUint(v, s.heatDots, 0, 47);
            if (!ok) error = F("heatDots: 0-47");
        } else if (!strcmp(k, "heatTime")) {
            ok = readUint(v, s.heatTime, 3, 255);
            if (!ok) error = F("heatTime: 3-255");
        } else if (!strcmp(k, "heatInterval")) {
            ok = readUint(v, s.heatInterval, 0, 255);
            if (!ok) error = F("heatInterval: 0-255");
        } else if (!strcmp(k, "printDensity")) {
            ok = readUint(v, s.printDensity, 0, 31);
            if (!ok) error = F("printDensity: 0-31");
        } else if (!strcmp(k, "printBreakTime")) {
            ok = readUint(v, s.printBreakTime, 0, 7);
            if (!ok) error = F("printBreakTime: 0-7");
        } else if (!strcmp(k, "feedLines")) {
            ok = readUint(v, s.feedLines, 0, 10);
            if (!ok) error = F("feedLines: 0-10");
        } else if (!strcmp(k, "bootBanner")) {
            ok = v.is<bool>();
            if (ok) s.bootBanner = v.as<bool>();
        } else if (!strcmp(k, "checkPaper")) {
            ok = v.is<bool>();
            if (ok) s.checkPaper = v.as<bool>();
        } else if (!strcmp(k, "timezone")) {
            String x = v.as<String>();
            ok = v.is<const char*>() && x.length() <= 64 && isPrintableAscii(x);
            if (ok) s.timezone = x;
            if (!ok) error = F("timezone: POSIX TZ string, max 64 chars");
        } else if (!strcmp(k, "serverUrl")) {
            String x = v.as<String>();
            ok = v.is<const char*>() && normaliseServerUrl(x, error);
            if (ok && x != s.serverUrl) {
                s.serverUrl = x;
                if (!internal) {
                    // Credentials belong to a specific server.
                    s.apiKey = "";
                    s.displayCode = "";
                }
            }
            if (!ok && error.length() == 0) error = F("serverUrl must be a string");
        } else if (!strcmp(k, "tlsInsecure")) {
            ok = v.is<bool>();
            if (ok) s.tlsInsecure = v.as<bool>();
        } else if (!strcmp(k, "pollSeconds")) {
            ok = readUint(v, s.pollSeconds, 2, 3600);
            if (!ok) error = F("pollSeconds: 2-3600");
        } else if (!strcmp(k, "heartbeatSeconds")) {
            ok = readUint(v, s.heartbeatSeconds, 15, 3600);
            if (!ok) error = F("heartbeatSeconds: 15-3600");
        } else if (internal && !strcmp(k, "apiKey")) {
            ok = v.is<const char*>() && (v.as<String>().length() == 0 || validCredential(v.as<String>(), 128));
            if (ok) s.apiKey = v.as<String>();
        } else if (internal && !strcmp(k, "displayCode")) {
            ok = v.is<const char*>() && v.as<String>().length() <= 32 && isPrintableAscii(v.as<String>());
            if (ok) s.displayCode = v.as<String>();
        } else if (internal && !strcmp(k, "deviceSecret")) {
            ok = v.is<const char*>() && validCredential(v.as<String>(), 128);
            if (ok) s.deviceSecret = v.as<String>();
        } else if (internal && !strcmp(k, "v")) {
            // schema version, handled by the loader
        } else {
            ok = false;
            error = String(F("unknown setting: ")) + k;
        }

        if (!ok) {
            if (error.length() == 0) error = String(F("invalid value for ")) + k;
            return false;
        }
    }
    return true;
}

bool applyJson(JsonObjectConst in, String& error, bool& rebootNeeded) {
    Settings trial = settings;
    if (!applyInto(in, trial, error, rebootNeeded, false)) return false;
    settings = trial;
    return true;
}

void toJson(JsonObject o, bool includeSecrets) {
    o["deviceName"] = settings.deviceName;
    o["requireAuthToPrint"] = settings.requireAuthToPrint;
    o["upsideDown"] = settings.upsideDown;
    o["lineWidth"] = settings.lineWidth;
    o["printerBaud"] = settings.printerBaud;
    o["heatDots"] = settings.heatDots;
    o["heatTime"] = settings.heatTime;
    o["heatInterval"] = settings.heatInterval;
    o["printDensity"] = settings.printDensity;
    o["printBreakTime"] = settings.printBreakTime;
    o["feedLines"] = settings.feedLines;
    o["bootBanner"] = settings.bootBanner;
    o["checkPaper"] = settings.checkPaper;
    o["timezone"] = settings.timezone;
    o["serverUrl"] = settings.serverUrl;
    o["tlsInsecure"] = settings.tlsInsecure;
    o["pollSeconds"] = settings.pollSeconds;
    o["heartbeatSeconds"] = settings.heartbeatSeconds;
    if (includeSecrets) {
        o["adminPassword"] = settings.adminPassword;
        o["apiKey"] = settings.apiKey;
        o["displayCode"] = settings.displayCode;
        o["deviceSecret"] = settings.deviceSecret;
    }
}

bool save() {
    JsonDocument doc;
    toJson(doc.to<JsonObject>(), true);
    doc["v"] = kSchemaVersion;

    File f = LittleFS.open(kTmpPath, "w");
    if (!f) {
        LOGE("config: cannot open %s for writing", kTmpPath);
        return false;
    }
    size_t expected = measureJson(doc);
    size_t written = serializeJson(doc, f);
    f.close();
    if (written != expected) {
        LOGE("config: short write (%u of %u bytes), filesystem full?", written, expected);
        LittleFS.remove(kTmpPath);
        return false;
    }
    // LittleFS rename atomically replaces the destination.
    if (!LittleFS.rename(kTmpPath, kPath)) {
        LOGE("config: rename failed");
        return false;
    }
    return true;
}

void begin() {
    if (!LittleFS.begin()) {
        // begin() auto-formats an unformatted partition; failing here means
        // the sketch was built with no filesystem (Tools > Flash Size).
        LOGE("config: LittleFS mount failed - settings will not persist!");
    }

    bool loaded = false;
    File f = LittleFS.open(kPath, "r");
    if (f) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err || !doc.is<JsonObject>()) {
            LOGE("config: %s is corrupt (%s), using defaults", kPath, err.c_str());
        } else {
            int v = doc["v"] | 0;
            if (v > kSchemaVersion) {
                LOGW("config: written by newer firmware (v%d), loading known fields", v);
            }
            loadLenient(doc.as<JsonObjectConst>(), settings);
            loaded = true;
        }
    }

    bool dirty = !loaded;
    if (settings.deviceSecret.length() == 0) {
        settings.deviceSecret = randomToken(32, "0123456789abcdef");
        dirty = true;
    }
    if (settings.adminPassword.length() == 0) {
        // No 0/O/1/l/I so it can be read off a receipt without mistakes.
        settings.adminPassword = randomToken(10, "abcdefghjkmnpqrstuvwxyz23456789");
        dirty = true;
    }
    if (dirty) save();
    LOGI("config: %s", loaded ? "loaded" : "defaults");
}

void factoryReset() {
    LittleFS.remove(kPath);
    LittleFS.remove(kTmpPath);
    LittleFS.remove("/ca.pem");
    // Keep the device secret: it is this printer's identity, like its MAC
    // address. Changing it would make a server refuse to re-pair the device.
    String secret = settings.deviceSecret;
    settings = Settings();
    settings.deviceSecret = secret;
    save();
}

String deviceId() {
    static String id;
    if (id.length() == 0) {
        String mac = WiFi.macAddress();
        mac.replace(":", "");
        id = "ESP_" + mac;
    }
    return id;
}

}  // namespace config
