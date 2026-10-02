// Printer output.
//
// All receipts are composed with Receipt in natural reading order. When the
// printer is mounted upside down (settings.upsideDown) the lines are sent in
// reverse so the paper still reads top-to-bottom.
#pragma once

#include <Arduino.h>

#include <string>
#include <vector>

namespace thermal {

enum class Paper : uint8_t { Ok, Out, Unknown };

enum class Size : uint8_t { Small, Medium /* double height */, Large /* double height+width */ };

struct Style {
    Size size = Size::Small;
    bool bold = false;
    bool inverse = false;
    char justify = 'L';  // 'L', 'C' or 'R'
};

inline Style bold(char justify = 'L') { Style s; s.bold = true; s.justify = justify; return s; }
inline Style centered(Size size = Size::Small) { Style s; s.size = size; s.justify = 'C'; return s; }

class Receipt {
public:
    // Add UTF-8 text; it is transcoded, sanitised and word-wrapped for the
    // style's size. Returns false if the text had to be truncated.
    bool text(const char* utf8, const Style& style = Style());
    bool text(const String& utf8, const Style& style = Style()) { return text(utf8.c_str(), style); }

    // Add one line that is already in printer encoding (from toPrinterLine()).
    void line(const std::string& cp850, const Style& style = Style());

    void blank(uint8_t n = 1);

    // Width in columns for a given size with the current settings.
    static size_t columns(Size size);

    bool empty() const { return lines_.empty(); }

    // Prints and clears the receipt.
    void print();

private:
    struct Line {
        std::string text;
        Style style;
    };
    std::vector<Line> lines_;
};

void begin();

// Re-sends heat/density/orientation settings. Called before every job, so a
// printer that browned out and reset mid-print (common: they draw >1.5 A)
// comes back with the right configuration.
void applySettings();

// Queries the printer over its TX line. Unknown if nothing answers (RX not
// wired, or printer off).
Paper paperStatus();

const char* paperName(Paper p);

}  // namespace thermal
