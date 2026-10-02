// Text handling for the thermal printer.
//
// This file has no Arduino dependencies on purpose: it is compiled both into
// the firmware and into the host-side unit tests in firmware/test/.
//
// The printer understands single-byte code pages, not UTF-8. Everything that
// reaches the printer goes through toPrinterText() first, which:
//   - decodes UTF-8 (invalid sequences become '?', never garbage bytes)
//   - maps Latin-1 / box-drawing characters to their CP850 byte
//   - folds Latin Extended-A letters (e.g. "ł", "č") to their ASCII base letter
//   - replaces common emoji and typographic symbols with ASCII stand-ins
//   - strips every control character except '\n', so a message can never
//     smuggle ESC/GS printer commands (cut, reconfigure, etc.) to the printer
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace textfmt {

// Convert arbitrary (possibly invalid) UTF-8 to printable CP850 bytes.
// Line endings are normalised to '\n'. Tabs expand to the next 4-column stop.
// Output is capped at maxBytes; returns true if the input was truncated.
bool toPrinterText(const char* utf8, size_t len, std::string& out, size_t maxBytes);

// Same as toPrinterText() but flattens newlines to spaces and clips to
// maxChars. For single-line fields like a sender name.
std::string toPrinterLine(const char* utf8, size_t len, size_t maxChars);

// Word-wrap CP850 text (one byte == one column) to `width` columns.
//   - blank lines inside the text are preserved; leading/trailing ones are not
//   - words longer than a line are hard-split without losing characters
//   - at most maxLines lines are produced; returns true if text was cut off,
//     in which case the last line is replaced with a truncation marker
bool wrap(const std::string& text, size_t width, std::vector<std::string>& out, size_t maxLines);

// Parse an ISO-8601 UTC/offset timestamp ("2025-01-02T03:04:05.678Z",
// "2025-01-02T03:04:05+02:00") into seconds since the Unix epoch.
bool parseIso8601(const char* s, int64_t& epochOut);

// Escape &, <, >, ", ' for safe inclusion in HTML.
std::string htmlEscape(const char* s);

// Verifies the lookup tables are sorted (binary search depends on it).
// Used by the unit tests; cheap enough to call at boot too.
bool tablesSorted();

}  // namespace textfmt
