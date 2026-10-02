#include "textfmt.h"

#include <string.h>

// Lookup tables live in flash on the ESP8266 (plain `const` data would be
// copied into the ~80 KB of RAM). On the host these macros are no-ops.
#if defined(ARDUINO)
#include <pgmspace.h>
#else
#define PROGMEM
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define memcpy_P memcpy
#endif

namespace textfmt {
namespace {

struct CpMap {
    uint16_t cp;
    uint8_t byte;
};

// Every non-ASCII character CP850 can print, sorted by code point.
// Generated with Python: bytes(range(128, 256)).decode('cp850').
const CpMap kCp850[] PROGMEM = {
    {0x00A0,0xFF}, {0x00A1,0xAD}, {0x00A2,0xBD}, {0x00A3,0x9C}, {0x00A4,0xCF}, {0x00A5,0xBE},
    {0x00A6,0xDD}, {0x00A7,0xF5}, {0x00A8,0xF9}, {0x00A9,0xB8}, {0x00AA,0xA6}, {0x00AB,0xAE},
    {0x00AC,0xAA}, {0x00AD,0xF0}, {0x00AE,0xA9}, {0x00AF,0xEE}, {0x00B0,0xF8}, {0x00B1,0xF1},
    {0x00B2,0xFD}, {0x00B3,0xFC}, {0x00B4,0xEF}, {0x00B5,0xE6}, {0x00B6,0xF4}, {0x00B7,0xFA},
    {0x00B8,0xF7}, {0x00B9,0xFB}, {0x00BA,0xA7}, {0x00BB,0xAF}, {0x00BC,0xAC}, {0x00BD,0xAB},
    {0x00BE,0xF3}, {0x00BF,0xA8}, {0x00C0,0xB7}, {0x00C1,0xB5}, {0x00C2,0xB6}, {0x00C3,0xC7},
    {0x00C4,0x8E}, {0x00C5,0x8F}, {0x00C6,0x92}, {0x00C7,0x80}, {0x00C8,0xD4}, {0x00C9,0x90},
    {0x00CA,0xD2}, {0x00CB,0xD3}, {0x00CC,0xDE}, {0x00CD,0xD6}, {0x00CE,0xD7}, {0x00CF,0xD8},
    {0x00D0,0xD1}, {0x00D1,0xA5}, {0x00D2,0xE3}, {0x00D3,0xE0}, {0x00D4,0xE2}, {0x00D5,0xE5},
    {0x00D6,0x99}, {0x00D7,0x9E}, {0x00D8,0x9D}, {0x00D9,0xEB}, {0x00DA,0xE9}, {0x00DB,0xEA},
    {0x00DC,0x9A}, {0x00DD,0xED}, {0x00DE,0xE8}, {0x00DF,0xE1}, {0x00E0,0x85}, {0x00E1,0xA0},
    {0x00E2,0x83}, {0x00E3,0xC6}, {0x00E4,0x84}, {0x00E5,0x86}, {0x00E6,0x91}, {0x00E7,0x87},
    {0x00E8,0x8A}, {0x00E9,0x82}, {0x00EA,0x88}, {0x00EB,0x89}, {0x00EC,0x8D}, {0x00ED,0xA1},
    {0x00EE,0x8C}, {0x00EF,0x8B}, {0x00F0,0xD0}, {0x00F1,0xA4}, {0x00F2,0x95}, {0x00F3,0xA2},
    {0x00F4,0x93}, {0x00F5,0xE4}, {0x00F6,0x94}, {0x00F7,0xF6}, {0x00F8,0x9B}, {0x00F9,0x97},
    {0x00FA,0xA3}, {0x00FB,0x96}, {0x00FC,0x81}, {0x00FD,0xEC}, {0x00FE,0xE7}, {0x00FF,0x98},
    {0x0131,0xD5}, {0x0192,0x9F}, {0x2017,0xF2}, {0x2500,0xC4}, {0x2502,0xB3}, {0x250C,0xDA},
    {0x2510,0xBF}, {0x2514,0xC0}, {0x2518,0xD9}, {0x251C,0xC3}, {0x2524,0xB4}, {0x252C,0xC2},
    {0x2534,0xC1}, {0x253C,0xC5}, {0x2550,0xCD}, {0x2551,0xBA}, {0x2554,0xC9}, {0x2557,0xBB},
    {0x255A,0xC8}, {0x255D,0xBC}, {0x2560,0xCC}, {0x2563,0xB9}, {0x2566,0xCB}, {0x2569,0xCA},
    {0x256C,0xCE}, {0x2580,0xDF}, {0x2584,0xDC}, {0x2588,0xDB}, {0x2591,0xB0}, {0x2592,0xB1},
    {0x2593,0xB2}, {0x25A0,0xFE},
};

// Base ASCII letter for U+0100..U+017F (Latin Extended-A), e.g. "ł" -> 'l'.
// Generated from Unicode NFKD decomposition plus manual fixes for letters that
// don't decompose (Đ, Ħ, Ł, Œ, ...).
const char kLatinExtA[] PROGMEM =
    "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLl"
    "NnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

struct SymMap {
    uint32_t cp;
    char ascii[7];
};

// ASCII stand-ins for symbols and emoji, sorted by code point.
const SymMap kSymbols[] PROGMEM = {
    {0x2010, "-"},     {0x2011, "-"},     {0x2012, "-"},     {0x2013, "-"},
    {0x2014, "-"},     {0x2015, "-"},     {0x2018, "'"},     {0x2019, "'"},
    {0x201A, ","},     {0x201C, "\""},    {0x201D, "\""},    {0x201E, "\""},
    {0x2022, "*"},     {0x2026, "..."},   {0x2032, "'"},     {0x2033, "\""},
    {0x2039, "<"},     {0x203A, ">"},     {0x20AC, "EUR"},   {0x20BF, "(B)"},
    {0x2122, "TM"},    {0x2190, "<-"},    {0x2191, "^"},     {0x2192, "->"},
    {0x2193, "v"},     {0x2194, "<->"},   {0x23F0, "[|]"},   {0x2600, "(O)"},
    {0x2601, "(~)"},   {0x2602, "T"},     {0x2603, "*"},     {0x2605, "*"},
    {0x2606, "*"},     {0x2614, "T"},     {0x2615, "[_]D"},  {0x263A, ":)"},
    {0x2665, "<3"},    {0x26A1, "!"},     {0x26C4, "*"},     {0x26C5, "~O~"},
    {0x26C8, "!'!"},   {0x2705, "[x]"},   {0x2713, "v"},     {0x2714, "v"},
    {0x2716, "x"},     {0x2728, "*"},     {0x2744, "*"},     {0x274C, "[X]"},
    {0x2753, "?"},     {0x2757, "!"},     {0x2764, "<3"},    {0x27A1, "->"},
    {0x2B50, "*"},     {0x1F30D, "(@)"},  {0x1F30E, "(@)"},  {0x1F30F, "(@)"},
    {0x1F310, "(@)"},  {0x1F319, "("},    {0x1F324, "~O~"},  {0x1F325, "~O~"},
    {0x1F326, "~O'"},  {0x1F327, "'''"},  {0x1F328, "*'*"},  {0x1F329, "!"},
    {0x1F32A, "@>"},   {0x1F32B, "~~~"},  {0x1F382, "iii"},  {0x1F389, "\\o/"},
    {0x1F431, "=^.^="},{0x1F44B, "o/"},   {0x1F44D, "(y)"},  {0x1F44E, "(n)"},
    {0x1F499, "<3"},   {0x1F49A, "<3"},   {0x1F49B, "<3"},   {0x1F49C, "<3"},
    {0x1F4AA, "}<"},   {0x1F4C8, "/^"},   {0x1F4C9, "\\v"},  {0x1F4DD, "[_]"},
    {0x1F504, "(<)"},  {0x1F600, ":D"},   {0x1F601, ":D"},   {0x1F602, ":')"},
    {0x1F603, ":D"},   {0x1F604, ":-D"},  {0x1F605, "^^;"},  {0x1F606, "XD"},
    {0x1F609, ";)"},   {0x1F60A, ":)"},   {0x1F60D, "<3"},   {0x1F60E, "B)"},
    {0x1F618, ":*"},   {0x1F61B, ":P"},   {0x1F61C, ";P"},   {0x1F622, ":'("},
    {0x1F62D, ":'("},  {0x1F642, ":)"},   {0x1F643, "(:"},   {0x1F914, ":?"},
    {0x1F917, "(^_^)"},{0x1F923, "XD"},   {0x1F970, "<3"},
};

// Characters with no visible form (zero-width joiners, variation selectors,
// emoji skin-tone modifiers, tag characters). Dropping them lets "☀️" (sun +
// VS16) and "👍🏽" (thumbs up + skin tone) resolve to their base mapping.
bool isInvisible(uint32_t cp) {
    return (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
           (cp >= 0x2060 && cp <= 0x2064) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
           cp == 0xFEFF || cp == 0x00AD || (cp >= 0x1F3FB && cp <= 0x1F3FF) ||
           (cp >= 0xE0000 && cp <= 0xE007F);
}

int lookupCp850(uint32_t cp) {
    if (cp > 0xFFFF) return -1;
    size_t lo = 0, hi = sizeof(kCp850) / sizeof(kCp850[0]);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint16_t v = pgm_read_word(&kCp850[mid].cp);
        if (v == cp) return pgm_read_byte(&kCp850[mid].byte);
        if (v < cp) lo = mid + 1; else hi = mid;
    }
    return -1;
}

bool lookupSymbol(uint32_t cp, char* ascii /* >= 7 bytes */) {
    size_t lo = 0, hi = sizeof(kSymbols) / sizeof(kSymbols[0]);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        SymMap m;
        memcpy_P(&m, &kSymbols[mid], sizeof(m));
        if (m.cp == cp) {
            memcpy(ascii, m.ascii, sizeof(m.ascii));
            return true;
        }
        if (m.cp < cp) lo = mid + 1; else hi = mid;
    }
    return false;
}

// Decode one UTF-8 code point starting at s[i]. Advances i. Returns
// 0xFFFD for malformed, overlong, surrogate, or out-of-range sequences.
uint32_t decodeUtf8(const uint8_t* s, size_t len, size_t& i) {
    uint8_t b0 = s[i++];
    if (b0 < 0x80) return b0;

    int extra;
    uint32_t cp, min;
    if ((b0 & 0xE0) == 0xC0)      { extra = 1; cp = b0 & 0x1F; min = 0x80; }
    else if ((b0 & 0xF0) == 0xE0) { extra = 2; cp = b0 & 0x0F; min = 0x800; }
    else if ((b0 & 0xF8) == 0xF0) { extra = 3; cp = b0 & 0x07; min = 0x10000; }
    else return 0xFFFD;  // stray continuation byte or invalid lead byte

    for (int k = 0; k < extra; k++) {
        // Don't consume a byte that isn't a continuation: it starts the
        // next character and must be decoded on its own.
        if (i >= len || (s[i] & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (s[i++] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;
    return cp;
}

// Append the printer representation of one code point. `col` tracks the
// current column so tabs can expand to the right width.
void appendCodePoint(uint32_t cp, std::string& out, size_t& col) {
    if (cp == '\n') { out += '\n'; col = 0; return; }
    if (cp == '\t') {
        size_t n = 4 - (col % 4);
        out.append(n, ' ');
        col += n;
        return;
    }
    if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) return;  // control chars
    if (isInvisible(cp)) return;

    if (cp < 0x80) { out += (char)cp; col++; return; }
    if (cp == 0xA0 || cp == 0x2007 || cp == 0x202F || (cp >= 0x2000 && cp <= 0x200A)) {
        out += ' '; col++; return;  // exotic spaces print as a normal space
    }

    int b = lookupCp850(cp);
    if (b >= 0) { out += (char)b; col++; return; }

    if (cp >= 0x100 && cp < 0x180) {
        out += (char)pgm_read_byte(&kLatinExtA[cp - 0x100]);
        col++;
        return;
    }

    char ascii[7];
    if (lookupSymbol(cp, ascii)) {
        out += ascii;
        col += strlen(ascii);
        return;
    }

    out += '?';
    col++;
}

void rtrim(std::string& s) {
    size_t end = s.find_last_not_of(' ');
    s.erase(end == std::string::npos ? 0 : end + 1);
}

}  // namespace

bool toPrinterText(const char* utf8, size_t len, std::string& out, size_t maxBytes) {
    out.clear();
    out.reserve(len < maxBytes ? len : maxBytes);
    const uint8_t* s = (const uint8_t*)utf8;
    size_t i = 0, col = 0;

    while (i < len) {
        uint32_t cp = decodeUtf8(s, len, i);
        if (cp == '\r') {
            if (i < len && s[i] == '\n') i++;  // CRLF -> LF
            cp = '\n';
        }
        size_t before = out.size();
        appendCodePoint(cp, out, col);
        if (out.size() > maxBytes) {
            out.resize(before);
            return true;
        }
    }
    return false;
}

std::string toPrinterLine(const char* utf8, size_t len, size_t maxChars) {
    std::string out;
    toPrinterText(utf8, len, out, maxChars * 2 + 16);
    for (char& c : out) {
        if (c == '\n') c = ' ';
    }
    size_t start = out.find_first_not_of(' ');
    if (start == std::string::npos) return std::string();
    out.erase(0, start);
    rtrim(out);
    if (out.size() > maxChars) {
        out.resize(maxChars);
        rtrim(out);
    }
    return out;
}

bool wrap(const std::string& text, size_t width, std::vector<std::string>& out, size_t maxLines) {
    out.clear();
    if (width == 0 || maxLines == 0) return !text.empty();

    bool truncated = false;
    size_t pos = 0;
    const size_t n = text.size();

    while (pos <= n && !truncated) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = n;
        std::string para = text.substr(pos, nl - pos);
        rtrim(para);
        pos = nl + 1;

        if (para.empty()) {
            out.push_back(std::string());
        } else {
            size_t p = 0;
            const size_t m = para.size();
            while (p < m) {
                if (out.size() >= maxLines) { truncated = true; break; }
                if (m - p <= width) {
                    out.push_back(para.substr(p));
                    break;
                }
                // Last space at or before the column limit. A space exactly
                // at `width` means the preceding word fits perfectly.
                size_t brk = std::string::npos;
                for (size_t j = p + width; j > p; j--) {
                    if (para[j] == ' ') { brk = j; break; }
                }
                std::string line;
                if (brk == std::string::npos) {
                    line = para.substr(p, width);  // no space: hard split, keep every char
                    p += width;
                } else {
                    line = para.substr(p, brk - p);
                    p = brk;
                }
                rtrim(line);
                out.push_back(line);
                while (p < m && para[p] == ' ') p++;  // don't start a line with spaces
            }
        }
        if (out.size() > maxLines) truncated = true;
        if (nl == n) break;
    }

    // Drop leading/trailing blank lines so we don't waste paper.
    while (!out.empty() && out.back().empty()) out.pop_back();
    size_t lead = 0;
    while (lead < out.size() && out[lead].empty()) lead++;
    out.erase(out.begin(), out.begin() + lead);

    if (truncated) {
        if (out.size() >= maxLines) out.resize(maxLines - 1);
        std::string marker = "[...truncated]";
        if (marker.size() > width) marker.resize(width);
        out.push_back(marker);
    }
    return truncated;
}

namespace {

int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    // Howard Hinnant's algorithm; valid for the proleptic Gregorian calendar.
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

bool readDigits(const char*& s, int count, int& value) {
    value = 0;
    for (int i = 0; i < count; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        value = value * 10 + (s[i] - '0');
    }
    s += count;
    return true;
}

}  // namespace

bool parseIso8601(const char* s, int64_t& epochOut) {
    if (!s) return false;
    int Y, M, D, h, m, sec;
    if (!readDigits(s, 4, Y) || *s++ != '-' || !readDigits(s, 2, M) || *s++ != '-' ||
        !readDigits(s, 2, D) || (*s != 'T' && *s != 't' && *s != ' ')) {
        return false;
    }
    s++;
    if (!readDigits(s, 2, h) || *s++ != ':' || !readDigits(s, 2, m) || *s++ != ':' ||
        !readDigits(s, 2, sec)) {
        return false;
    }
    if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || m > 59 || sec > 60) return false;

    if (*s == '.' || *s == ',') {
        s++;
        while (*s >= '0' && *s <= '9') s++;
    }

    int offsetSec = 0;
    if (*s == 'Z' || *s == 'z') {
        s++;
    } else if (*s == '+' || *s == '-') {
        int sign = (*s == '-') ? -1 : 1;
        s++;
        int oh, om = 0;
        if (!readDigits(s, 2, oh)) return false;
        if (*s == ':') s++;
        if (*s >= '0' && *s <= '9' && !readDigits(s, 2, om)) return false;
        if (oh > 23 || om > 59) return false;
        offsetSec = sign * (oh * 3600 + om * 60);
    } else {
        return false;  // no zone designator: ambiguous, refuse rather than guess
    }
    if (*s != '\0') return false;

    epochOut = daysFromCivil(Y, (unsigned)M, (unsigned)D) * 86400 + h * 3600 + m * 60 + sec -
               offsetSec;
    return true;
}

std::string htmlEscape(const char* s) {
    std::string out;
    if (!s) return out;
    for (; *s; s++) {
        switch (*s) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += *s;
        }
    }
    return out;
}

}  // namespace textfmt

namespace textfmt {

bool tablesSorted() {
    const size_t n1 = sizeof(kCp850) / sizeof(kCp850[0]);
    for (size_t i = 1; i < n1; i++) {
        if (pgm_read_word(&kCp850[i - 1].cp) >= pgm_read_word(&kCp850[i].cp)) return false;
    }
    const size_t n2 = sizeof(kSymbols) / sizeof(kSymbols[0]);
    SymMap a, b;
    for (size_t i = 1; i < n2; i++) {
        memcpy_P(&a, &kSymbols[i - 1], sizeof(a));
        memcpy_P(&b, &kSymbols[i], sizeof(b));
        if (a.cp >= b.cp) return false;
    }
    return sizeof(kLatinExtA) - 1 == 0x80;
}

}  // namespace textfmt
