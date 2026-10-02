#include "log.h"

#include <stdarg.h>

void logWrite(LogLevel level, PGM_P fmt, ...) {
    if (level > PRINTR_LOG_LEVEL) return;

    static const char kTags[] = "?EWID";
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf_P(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    uint32_t ms = millis();
    Serial.printf_P(PSTR("[%6lu.%03lu] %c: %s%s\n"), (unsigned long)(ms / 1000),
                    (unsigned long)(ms % 1000), kTags[level <= 4 ? level : 0], buf,
                    (n >= (int)sizeof(buf)) ? "..." : "");
}
