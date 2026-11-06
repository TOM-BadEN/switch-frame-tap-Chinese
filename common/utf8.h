/* UTF-8 boundaries shared by the desktop viewer and the Switch manager. */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Decode one character at *cursor and advance it. Invalid bytes yield U+FFFD;
 * a terminating NUL yields zero without advancing. cursor must be non-null. */
static inline uint32_t sftUtf8Next(const char **cursor) {
    const unsigned char *start = (const unsigned char *)*cursor; /* First byte of this character. */
    const unsigned char *next = start; /* Next byte to validate, never beyond the terminating NUL. */
    uint32_t code = *next++; /* Accumulated Unicode scalar value. */
    int count = 0; /* Number of required continuation bytes. */
    uint32_t minimum = 0; /* Smallest legal scalar for this encoded length. */
    if (code == 0) { return 0; }
    if (code < 0x80) { *cursor = (const char *)next; return code; }
    if (code >= 0xC2 && code <= 0xDF) { count = 1; minimum = 0x80; code &= 31; }
    else if (code >= 0xE0 && code <= 0xEF) { count = 2; minimum = 0x800; code &= 15; }
    else if (code >= 0xF0 && code <= 0xF4) { count = 3; minimum = 0x10000; code &= 7; }
    else { *cursor = (const char *)(start + 1); return 0xFFFD; }
    for (int index = 0; index < count; ++index) { /* Continuation-byte index in this scalar. */
        if ((*next & 0xC0) != 0x80) { *cursor = (const char *)(start + 1); return 0xFFFD; }
        code = (code << 6) | (*next++ & 63);
    }
    *cursor = (const char *)next;
    return code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF) ? 0xFFFD : code;
}

/* Return the complete UTF-8 prefix length of NUL-terminated text fitting
 * maxBytes bytes, excluding the terminator. Does not modify text. */
static inline size_t sftUtf8Prefix(const char *text, size_t maxBytes) {
    const char *cursor = text; /* Boundary after the last decoded scalar. */
    size_t length = 0; /* Last complete prefix that fits the destination. */
    while (*cursor) {
        sftUtf8Next(&cursor);
        if ((size_t)(cursor - text) > maxBytes) { break; }
        length = (size_t)(cursor - text);
    }
    return length;
}
