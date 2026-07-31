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

// Use a function-local static so the variable is shared across all TUs
// (inline functions with static locals are merged by the linker per C++11 §3.2).
// A plain `static` variable in a header would give each TU its own copy,
// causing set_millis() in test files to not affect millis() in tcp.cpp.
inline uint32_t &_millis_ref()
{
    static uint32_t v = 0;
    return v;
}
inline uint32_t millis()
{
    return _millis_ref();
}
inline void set_millis(uint32_t v)
{
    _millis_ref() = v;
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

// static inline: each TU gets its own copy; no multiple-definition link error.
// The shared seed state is in the function-local static (merged per C++11 §3.2
// for inline functions, but we use static to avoid the linkage issue entirely).
static inline uint32_t esp_random()
{
    static bool seeded = false;
    static uint32_t ctr = 0;
    if (!seeded)
    {
        srand((unsigned)time(nullptr) ^ 0xDEADBEEFu);
        seeded = true;
    }
    // Mix in a counter so repeated calls within the same millisecond differ.
    return (uint32_t)rand() ^ (++ctr * 0x9e3779b9u);
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
