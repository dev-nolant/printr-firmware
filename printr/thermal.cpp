#include "thermal.h"

#include <Adafruit_Thermal.h>
#include <SoftwareSerial.h>

#include "config.h"
#include "log.h"
#include "textfmt.h"

namespace thermal {
namespace {

SoftwareSerial printerSerial(PIN_PRINTER_RX, PIN_PRINTER_TX);
Adafruit_Thermal printer(&printerSerial);

// Width multiplier per size: Large is double width.
size_t widthDivisor(Size s) { return s == Size::Large ? 2 : 1; }

char sizeCode(Size s) {
    switch (s) {
        case Size::Medium: return 'M';
        case Size::Large: return 'L';
        default: return 'S';
    }
}

constexpr uint32_t kInverseLineSettleMs = 800;
constexpr uint32_t kFinishSettleMs = 300;

// Advance the paper one dot row at a time (ESC J 1) with a short pause
// between steps. A blank-line feed spins the stepper at full speed, and on
// weaker printers or power supplies it stalls there: it buzzes but the paper
// doesn't move, so the last printed part (the header, when mounted upside
// down) stays inside. Stepping slowly keeps the motor's torque up.
void feedSlowly(uint8_t lines) {
    constexpr uint8_t kDotsPerLine = 30;  // 24-dot text + 6-dot spacing (setDefault)
    constexpr uint8_t kStepDelayMs = 4;
    for (uint16_t i = 0; i < (uint16_t)lines * kDotsPerLine; i++) {
        printerSerial.write((uint8_t)27);
        printerSerial.write((uint8_t)'J');
        printerSerial.write((uint8_t)1);
        delay(kStepDelayMs);  // also yields to WiFi
    }
}

}  // namespace

size_t Receipt::columns(Size size) {
    size_t w = settings.lineWidth / widthDivisor(size);
    return w ? w : 1;
}

bool Receipt::text(const char* utf8, const Style& style) {
    if (!utf8) return true;
    std::string cp;
    bool cut = textfmt::toPrinterText(utf8, strlen(utf8), cp, kMaxMessageBytes);
    std::vector<std::string> wrapped;
    cut |= textfmt::wrap(cp, columns(style.size), wrapped, kMaxMessageLines);
    for (auto& l : wrapped) lines_.push_back({std::move(l), style});
    return !cut;
}

void Receipt::line(const std::string& cp850, const Style& style) {
    std::string clipped = cp850.substr(0, columns(style.size));
    lines_.push_back({clipped, style});
}

void Receipt::blank(uint8_t n) {
    while (n--) lines_.push_back({std::string(), Style()});
}

void Receipt::print() {
    if (lines_.empty()) return;

    printer.wake();
    applySettings();

    const size_t n = lines_.size();
    Style current;  // matches the state applySettings() leaves the printer in
    for (size_t k = 0; k < n; k++) {
        const Line& l = lines_[settings.upsideDown ? n - 1 - k : k];
        const Style& s = l.style;
        if (s.size != current.size) printer.setSize(sizeCode(s.size));
        if (s.bold != current.bold) s.bold ? printer.boldOn() : printer.boldOff();
        if (s.inverse != current.inverse) s.inverse ? printer.inverseOn() : printer.inverseOff();
        if (s.justify != current.justify) printer.justify(s.justify);
        current = s;
        printer.println(l.text.c_str());
        if (s.inverse) {
            // A solid black line takes far longer to burn than the library's
            // timing estimate (which assumes ordinary text). Without flow
            // control the next bytes overflow the printer's input buffer and
            // get dropped -- when it was the last line, the paper feed was
            // lost and the header stayed inside the printer.
            delay(kInverseLineSettleMs);
        }
        // Adafruit_Thermal busy-waits between lines; let the WiFi stack run
        // and keep the software watchdog fed.
        yield();
    }
    delay(kFinishSettleMs);  // let the last line finish before the feed
    printer.setDefault();
    feedSlowly(settings.feedLines);
    printer.sleep();

    lines_.clear();
    lines_.shrink_to_fit();
}

void begin() {
    printerSerial.begin(settings.printerBaud);
    printer.begin();
    applySettings();
    printer.sleep();
    LOGI("printer: ready (%lu baud, %s)", (unsigned long)settings.printerBaud,
         settings.upsideDown ? "upside down" : "normal");
}

void applySettings() {
    printer.setDefault();  // also selects the default code page; overridden below
    printer.setHeatConfig(settings.heatDots, settings.heatTime, settings.heatInterval);
    printer.setPrintDensity(settings.printDensity, settings.printBreakTime);
    if (settings.upsideDown) printer.upsideDownOn(); else printer.upsideDownOff();
    printer.setCodePage(CODEPAGE_CP850);  // textfmt produces CP850
}

Paper paperStatus() {
    while (printerSerial.available()) printerSerial.read();  // drop stale bytes

    printer.wake();
    // ESC v n: transmit paper sensor status (firmware >= 2.64, the library default).
    printerSerial.write(27);
    printerSerial.write('v');
    printerSerial.write((uint8_t)0);

    int status = -1;
    uint32_t start = millis();
    while (millis() - start < 500) {
        if (printerSerial.available()) {
            status = printerSerial.read();
            break;
        }
        delay(10);
    }
    printer.sleep();

    // Adafruit_Thermal::hasPaper() treats "no answer" as "no paper", which
    // would block printing forever on boards without the RX wire.
    if (status < 0) return Paper::Unknown;
    return (status & 0x04) ? Paper::Out : Paper::Ok;
}

const char* paperName(Paper p) {
    switch (p) {
        case Paper::Ok: return "ok";
        case Paper::Out: return "out";
        default: return "unknown";
    }
}

}  // namespace thermal
