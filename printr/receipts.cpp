#include "receipts.h"

#include <time.h>

#include "config.h"
#include "log.h"
#include "textfmt.h"
#include "thermal.h"
#include "version.h"

using thermal::Receipt;
using thermal::Size;
using thermal::Style;

namespace receipts {
namespace {

// A full-width header, e.g. "=========== Message ===========".
//
// Deliberately not an inverse (solid black) bar: burning a whole black line
// draws a current spike that browns out printers on marginal power supplies,
// and the paper feed after it is lost -- the header then stays stuck inside.
void banner(Receipt& r, const char* title) {
    // One column short of full width: some printers emit an extra blank line
    // when a line exactly fills the buffer and is followed by a newline.
    size_t width = Receipt::columns(Size::Small) - 1;
    std::string t = textfmt::toPrinterLine(title, strlen(title), width > 4 ? width - 4 : width);
    size_t fill = width - t.size() - 2;  // 2 spaces around the title
    std::string line(fill / 2, '=');
    line += ' ';
    line += t;
    line += ' ';
    line.append(width - line.size(), '=');
    r.line(line, thermal::bold());
}

bool formatLocalTime(int64_t epoch, char* out, size_t len) {
    // Skip if unknown, or if the clock isn't synced yet (would print 1970).
    if (epoch < 1700000000 || settings.timezone.length() == 0) return false;
    time_t t = (time_t)epoch;
    struct tm tm;
    if (!localtime_r(&t, &tm)) return false;
    return strftime(out, len, "%a %b %d, %H:%M", &tm) > 0;
}

}  // namespace

void message(const char* text, const char* from, int64_t createdAt) {
    Receipt r;
    banner(r, "Message");
    r.blank();

    std::string sender = textfmt::toPrinterLine(from ? from : "", from ? strlen(from) : 0,
                                                kMaxSenderChars);
    if (sender.empty()) sender = "Unknown";
    r.line("From: " + sender, thermal::bold());

    char when[32];
    if (formatLocalTime(createdAt, when, sizeof(when))) r.line(when);

    r.blank();
    if (!r.text(text)) LOGW("print: message truncated");
    r.print();
    LOGI("print: message from %s", sender.c_str());
}

void setupInstructions(const String& apName) {
    Receipt r;
    banner(r, "WiFi setup");
    r.blank();
    r.text("1. On your phone, join the WiFi network:");
    r.text(apName, thermal::bold());
    r.text("   (no password)");
    r.blank();
    r.text("2. A setup page should open. If not, browse to:");
    r.text("http://192.168.4.1", thermal::bold());
    r.blank();
    r.text("3. Pick your WiFi and enter its password.");
    r.blank();
    r.text("Page won't open? Turn off mobile data and try again.");
    r.blank();
    r.text("Admin password (keep this):");
    r.text(settings.adminPassword, thermal::bold());
    r.print();
}

void online(const IPAddress& ip, const String& ssid) {
    Receipt r;
    r.text("PRINTR", thermal::centered(Size::Large));
    r.text("online", thermal::centered());
    r.blank();
    r.text(String(F("Network: ")) + ssid);
    r.text(String(F("Address: http://")) + ip.toString());
    r.text(String(F("         http://")) + settings.deviceName + F(".local"));
    if (settings.serverUrl.length()) {
        r.text(String(F("Cloud:   ")) + settings.serverUrl);
        if (settings.displayCode.length()) r.text(String(F("Code:    ")) + settings.displayCode);
    } else {
        r.text(F("Cloud:   off (local only)"));
    }
    r.text(F("Firmware " PRINTR_FW_VERSION));
    r.print();
}

void pairing(const String& pairingCode, const String& dashboardUrl) {
    Receipt r;
    banner(r, "Pair this printer");
    r.blank();
    r.text(pairingCode, thermal::centered(Size::Large));
    r.blank();
    r.text("Enter this code on:");
    r.text(dashboardUrl, thermal::bold());
    r.text("Dashboard > Printers > Pair Printer");
    r.print();
}

void notice(const char* title, const char* body) {
    Receipt r;
    banner(r, title);
    if (body && *body) {
        r.blank();
        r.text(body);
    }
    r.print();
}

void testPage() {
    Receipt r;
    r.text("TEST PRINT", thermal::centered(Size::Medium));
    r.blank();
    r.text("Printer is working!");
    r.text(String(F("Uptime: ")) + (millis() / 1000) + F(" s"));
    r.text(String(F("Paper sensor: ")) + thermal::paperName(thermal::paperStatus()));
    r.blank();
    Style b = thermal::bold();
    r.text("Bold text", b);
    Style inv;
    inv.inverse = true;
    r.text("Inverse text", inv);
    r.text("Centered", thermal::centered());
    r.text("Medium", thermal::centered(Size::Medium));
    r.text("Large", thermal::centered(Size::Large));
    r.blank();
    std::string ruler;
    for (size_t i = 0; i < Receipt::columns(Size::Small); i++) ruler += char('0' + (i + 1) % 10);
    r.line(ruler);
    r.print();
}

void characterTest() {
    // Accents, emoji, smart quotes and long words: the easy things to get wrong.
    static const char* const kSamples[] = {
        "Hola pap\xC3\xAD",
        "Special: \xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA \xC3\xB1 \xC3\x81\xC3\x89\xC3\x8D"
        "\xC3\x93\xC3\x9A \xC3\x91 \xC3\xBC \xC3\xA7 \xC3\x9F \xE2\x82\xAC",
        "Symbols: * ^ ~ ` | \\ @ # $ % & ( ) [ ] { }",
        "Weather: \xE2\x98\x80\xEF\xB8\x8F 72\xC2\xB0""F  \xF0\x9F\x8C\xA7 \xE2\x9D\x84 \xF0\x9F\x8C\x99",
        "Emoji: \xF0\x9F\x98\x84 \xF0\x9F\x91\x8D \xE2\x9D\xA4\xEF\xB8\x8F \xE2\x9C\x85 \xE2\x9D\x8C",
        "Quotes: \xE2\x80\x9Csmart\xE2\x80\x9D \xE2\x80\x98single\xE2\x80\x99 \xE2\x80\x94 dash\xE2\x80\xA6",
        "* Chicken* Beef* Ground beef* Chips* Coca Cola* Juice * Naan* Cream* Ginger * "
        "Turmeric * Chili powder* Garam masala* Red onion* All spice* Coriander * Naan",
        "https://example.com/a/very/long/url/that/has/no/spaces/at/all",
    };
    for (const char* s : kSamples) {
        message(s, "Character test", 0);
        delay(250);
    }
}

}  // namespace receipts
