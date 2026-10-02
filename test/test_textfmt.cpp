// Host-side unit tests for textfmt. Build and run with firmware/test/run_tests.sh
#include "../printr/textfmt.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        checks++;                                                        \
        if (!(cond)) {                                                   \
            failures++;                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                \
    } while (0)

static std::string P(const std::string& utf8, size_t max = 4096) {
    std::string out;
    textfmt::toPrinterText(utf8.data(), utf8.size(), out, max);
    return out;
}

static std::vector<std::string> W(const std::string& text, size_t width, size_t maxLines = 100) {
    std::vector<std::string> out;
    textfmt::wrap(text, width, out, maxLines);
    return out;
}

static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) s += '|';
        s += v[i];
    }
    return s;
}

static void testTranscode() {
    CHECK(textfmt::tablesSorted());

    CHECK(P("Hello, world!") == "Hello, world!");
    // Accents map to CP850 bytes; raw UTF-8 would print as garbage.
    CHECK(P("\xC3\xA9") == "\x82");                    // é
    CHECK(P("Hola pap\xC3\xAD") == "Hola pap\xA1");    // í
    CHECK(P("\xC3\xB1\xC3\x91") == "\xA4\xA5");        // ñÑ
    CHECK(P("25\xC2\xB0""C") == "25\xF8""C");          // degree sign now prints
    CHECK(P("\xE2\x94\x80") == "\xC4");                // box drawing ─
    // Latin Extended-A folds to ASCII.
    CHECK(P("\xC5\x81\xC3\xB3""d\xC5\xBA") == "L\xA2""dz");  // Łódź
    // Emoji and symbols.
    CHECK(P("\xF0\x9F\x98\x84") == ":-D");             // 😄
    CHECK(P("\xE2\x98\x80\xEF\xB8\x8F") == "(O)");     // ☀️ (with VS16)
    CHECK(P("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD") == "(y)");  // 👍🏽 (skin tone dropped)
    CHECK(P("\xE2\x80\x9CHi\xE2\x80\x9D \xE2\x80\x94 ok\xE2\x80\xA6") == "\"Hi\" - ok...");
    CHECK(P("\xE2\x9C\x85 done") == "[x] done");
    CHECK(P("\xF0\x9F\xA6\x84") == "?");               // unmapped emoji 🦄
    CHECK(P("\xD0\x96") == "?");                       // Cyrillic Ж

    // Control characters can't reach the printer (ESC/GS command injection).
    CHECK(P("a\x1B" "@b\x1D" "Vc\x07") == "a@bVc");
    CHECK(P("a\xC2\x9B" "b") == "ab");                 // C1 control U+009B
    // Line endings.
    CHECK(P("a\r\nb\rc\nd") == "a\nb\nc\nd");
    // Tabs expand to 4-column stops.
    CHECK(P("\tx") == "    x");
    CHECK(P("ab\tx") == "ab  x");

    // Invalid UTF-8 never produces raw high bytes.
    CHECK(P("\xFF") == "?");
    CHECK(P("\xC3") == "?");                           // truncated sequence
    CHECK(P("\xC3" "A") == "?A");                      // bad continuation keeps next char
    CHECK(P("\xC0\xAF") == "?");                       // overlong '/'
    CHECK(P("\xED\xA0\x80") == "?");                   // UTF-16 surrogate
    CHECK(P("\xF4\x90\x80\x80") == "?");               // > U+10FFFF
    CHECK(P("\x80\x80z") == "??z");                    // stray continuations

    // Truncation respects maxBytes and never splits a multi-byte stand-in.
    std::string out;
    CHECK(textfmt::toPrinterText("abcdef", 6, out, 4) == true && out == "abcd");
    CHECK(textfmt::toPrinterText("ab\xE2\x80\xA6", 5, out, 4) == true && out == "ab");
    CHECK(textfmt::toPrinterText("abc", 3, out, 3) == false && out == "abc");

    CHECK(textfmt::toPrinterLine("  Jane\nDoe  ", 12, 32) == "Jane Doe");
    CHECK(textfmt::toPrinterLine("abcdefghij", 10, 4) == "abcd");
    CHECK(textfmt::toPrinterLine("   ", 3, 10) == "");
    CHECK(textfmt::toPrinterLine("ab   cd", 7, 4) == "ab");
}

static void testWrap() {
    CHECK(join(W("hello world", 32)) == "hello world");
    CHECK(join(W("hello world", 5)) == "hello|world");
    CHECK(join(W("hello world", 6)) == "hello|world");
    // Space exactly at the column limit: word fits perfectly on the line.
    CHECK(join(W("abcd efgh", 4)) == "abcd|efgh");
    // Long word: hard split, no characters dropped.
    CHECK(join(W("abcdefghij", 4)) == "abcd|efgh|ij");
    CHECK(join(W("see https://example.com/abcdef", 10)) == "see|https://ex|ample.com/|abcdef");
    // Blank lines inside the message are preserved; outer ones trimmed.
    CHECK(join(W("\n\na\n\nb\n\n", 10)) == "a||b");
    // Multiple spaces at wrap points don't start the next line.
    CHECK(join(W("aaa    bbb", 5)) == "aaa|bbb");
    // Leading indentation on the first line is kept.
    CHECK(join(W("  indented", 20)) == "  indented");
    CHECK(W("", 32).empty());
    CHECK(W("\n\n\n", 32).empty());
    CHECK(W("   ", 32).empty());

    // A long list with irregular spacing.
    std::string list = "* Chicken* Beef* Ground beef* Chips* Coca Cola* Juice * Naan";
    auto lines = W(list, 32);
    for (auto& l : lines) CHECK(l.size() <= 32);
    std::string rejoined;
    for (auto& l : lines) rejoined += l;
    std::string squashed;
    for (char c : list) if (c != ' ') squashed += c;
    std::string rejoinedSquashed;
    for (char c : rejoined) if (c != ' ') rejoinedSquashed += c;
    CHECK(rejoinedSquashed == squashed);  // nothing lost

    // Line cap.
    std::vector<std::string> out;
    CHECK(textfmt::wrap("a\nb\nc\nd\ne", 32, out, 3) == true);
    CHECK(join(out) == "a|b|[...truncated]");
    CHECK(textfmt::wrap("a\nb\nc", 32, out, 3) == false);
    CHECK(join(out) == "a|b|c");
    CHECK(textfmt::wrap("abcdefghijkl", 4, out, 2) == true);
    CHECK(join(out) == "abcd|[...");

    // Every line always fits, for many widths and inputs.
    const char* samples[] = {"The quick brown fox jumps over the lazy dog",
                             "x", "a b c d e f g", "longwordlongwordlongword short",
                             "trailing   ", "   leading", "mixed\n\nparagraphs here ok"};
    for (const char* s : samples) {
        for (size_t w = 1; w <= 40; w++) {
            for (auto& l : W(s, w)) CHECK(l.size() <= w);
        }
    }
}

static void testIso8601() {
    int64_t t = 0;
    CHECK(textfmt::parseIso8601("1970-01-01T00:00:00Z", t) && t == 0);
    CHECK(textfmt::parseIso8601("2025-01-02T03:04:05.678Z", t) && t == 1735787045);
    CHECK(textfmt::parseIso8601("2025-01-02T05:04:05+02:00", t) && t == 1735787045);
    CHECK(textfmt::parseIso8601("2025-01-01T22:04:05-0500", t) && t == 1735787045);
    CHECK(textfmt::parseIso8601("2024-02-29T12:00:00Z", t) && t == 1709208000);
    CHECK(textfmt::parseIso8601("2038-01-19T03:14:08Z", t) && t == 2147483648LL);  // past Y2038
    CHECK(!textfmt::parseIso8601("2025-01-02T03:04:05", t));  // no zone
    CHECK(!textfmt::parseIso8601("2025-13-02T03:04:05Z", t));
    CHECK(!textfmt::parseIso8601("garbage", t));
    CHECK(!textfmt::parseIso8601("", t));
    CHECK(!textfmt::parseIso8601(nullptr, t));
    CHECK(!textfmt::parseIso8601("2025-01-02T03:04:05Zjunk", t));
}

static void testHtml() {
    CHECK(textfmt::htmlEscape("<a href=\"x\">'&'</a>") ==
          "&lt;a href=&quot;x&quot;&gt;&#39;&amp;&#39;&lt;/a&gt;");
    CHECK(textfmt::htmlEscape(nullptr).empty());
}

int main() {
    testTranscode();
    testWrap();
    testIso8601();
    testHtml();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
