#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PROGMEM
#define PROGMEM
#endif

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

// The virtual clock the suite drives: a test calls set_millis() and the library reads it back
// through pc_millis(). The counter is one object for the whole program - a test writes it from
// its own translation unit and server/clock/clock.c reads it from another, so a per-TU copy
// would leave the library reading a counter nobody set. A weak definition in the header gives
// every TU the same symbol and lets the linker collapse them to one instance, which is what the
// C++ build got for free from ODR merging of inline-function-local statics.
__attribute__((weak)) uint32_t g_pc_mock_millis = 0;

static inline uint32_t millis(void)
{
    return g_pc_mock_millis;
}
static inline void set_millis(uint32_t v)
{
    g_pc_mock_millis = v;
}

// ---------------------------------------------------------------------------
// Hardware RNG mock (esp_random replacement for native builds)
//
// On ESP32, esp_random() draws from the hardware RNG peripheral which is
// seeded by analog thermal/RF noise - cryptographically appropriate.
//
// On native (x86) this mock uses rand() seeded from system time XOR a
// monotonic counter.  IT IS NOT CRYPTOGRAPHICALLY SECURE and is present
// only to allow the SSH crypto unit tests to exercise the protocol logic
// on the host.  Never use the native build output as a real SSH server.
// ---------------------------------------------------------------------------
#include <stdlib.h>
#include <time.h>

// static inline: each TU gets its own seed flag and counter, which only costs a re-seed per TU.
static inline uint32_t esp_random(void)
{
    static int seeded = 0;
    static uint32_t ctr = 0;
    if (!seeded)
    {
        srand((unsigned)time(NULL) ^ 0xDEADBEEFu);
        seeded = 1;
    }
    // Mix in a counter so repeated calls within the same millisecond differ.
    ctr++;
    return (uint32_t)rand() ^ (ctr * 0x9e3779b9u);
}

static inline void esp_fill_random(void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < len; i += 4)
    {
        uint32_t r = esp_random();
        size_t n = (i + 4 <= len) ? 4 : (len - i);
        __builtin_memcpy(p + i, &r, n);
    }
}
