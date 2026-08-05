// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for mmgr/swar.h against the newlib string routines it
// replaces: scan_nul/strnlen, eq_str/strcmp, eq_str_ci/strcasecmp, diff_ci/strncasecmp, copy/strncpy,
// find/strstr, has/strstr.
//
// This exists because the host measurement is not evidence. An x86 host runs these against a libc
// whose string routines are hand-written AVX2 and does so on a wide out-of-order core with more load
// ports than the loop can saturate - so on the host, a lane-math routine that issues ONE load per
// step measured SLOWER than one issuing `nlen` loads, and libc's SIMD strstr beat the lane search by
// 3x. Neither of those conditions holds on an LX7 or a riscv32: newlib's strnlen and strstr are byte
// loops, there is one load port, and unaligned loads are not free. Which implementation wins is a
// property of the die, and this is the only place it gets decided.
//
// The libc calls here are the baseline being measured. This file is not part of the library and
// SRCBANNED does not constrain it.
//
// Build/flash: see ../BENCH.md and s3/build_s3_swarbench.sh.
#include <Arduino.h>
#include <stdio.h>

#include "shared_primitives/runops.h"

static inline uint32_t cyc_now()
{
    return ESP.getCycleCount();
}

static double g_mhz = 240.0;

// Without this the optimizer deletes the call whose result nobody reads and the variant looks free.
static volatile uint32_t g_sink = 0;

#define BENCH(label, N, expr)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        expr; /* warm: first call faults in the code path and any libc tables */                                       \
        uint32_t _c0 = cyc_now();                                                                                      \
        for (uint32_t _i = 0; _i < (uint32_t)(N); _i++)                                                                \
        {                                                                                                              \
            expr;                                                                                                      \
        }                                                                                                              \
        double _cy = (double)(cyc_now() - _c0) / (double)(N);                                                          \
        Serial.printf("CB %-30s cyc=%-9.1f ns=%.1f\n", label, _cy, _cy * 1000.0 / g_mhz);                              \
        vTaskDelay(1);                                                                                                 \
    } while (0)

static uint32_t g_bad = 0;

#define CHECK(label, got, want)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        long _g = (long)(got);                                                                                         \
        long _w = (long)(want);                                                                                        \
        if (_g != _w)                                                                                                  \
        {                                                                                                              \
            Serial.printf("CB CHECK %-24s *** DIFFERS *** got=%ld want=%ld\n", label, _g, _w);                         \
            g_bad++;                                                                                                   \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            Serial.printf("CB CHECK %-24s identical\n", label);                                                        \
        }                                                                                                              \
    } while (0)

// Every compare below is buffer-against-buffer at BCAP. proto_agree's read_cap is a promise that
// many bytes are READABLE, so handing it a bound past the end of a short string literal is an
// out-of-bounds read rather than a measurement.
#define BCAP 32u
// A POWER OF TWO, so rotating the corpus is a mask and not a divide. It was 20, which made every
// row pay two runtime integer divisions on a die with no fast divide - common to both contenders,
// so it never changed which one won, but it adds the same constant to both and squeezes every ratio
// toward 1. The bench must not be the thing being measured.
#define NH 16

static const char *const hdr_src[NH] = {
    "Host",          "Content-Length",    "Content-Type",  "Transfer-Encoding",
    "Authorization", "Connection",        "Upgrade",       "User-Agent",
    "Accept",        "Accept-Encoding",   "Cache-Control", "X-Forwarded-Proto",
    "X-CSRF-Token",  "Sec-WebSocket-Key", "If-None-Match", "Cookie",
};
static const char *const probe_src[NH] = {
    "host",          "content-length",    "CONTENT-TYPE",  "transfer-encoding",
    "authorization", "connection",        "upgrade",       "user-agent",
    "accept",        "accept-encoding",   "cache-control", "x-forwarded-proto",
    "x-csrf-token",  "sec-websocket-key", "if-none-match", "cookie",
};

// A route path is mostly slashes, so a search for "/:" that filters on the first byte alone makes a
// candidate of every segment boundary. This is the shape fill_route_base() actually sees.
#define NR 8
static const char *const route_src[NR] = {
    "/api/v1/users/:id",       "/api/v1/orders/:oid/items", "/static/js/app/bundle", "/v2/tenants/:t/keys/:k",
    "/health/live/ready/deep", "/a/b/c/d/e/f/g/:z",         "/files/upload/chunked", "/ws/stream/:room/join"};

static char hdrs[NH][BCAP];
static char hdrs_copy[NH][BCAP];
static char probes[NH][BCAP];
static char routes[NR][BCAP];
static char longhay[640];
static char dst[BCAP];

static uint32_t g_i = 0; // rotates the corpus so no single entry is measured in isolation

// Guard-bit masks, one lane set at a time plus a few with several, for the lane-index sweep below.
// The whole file reduces to "build a mask, ask which lane fires first", so what that question costs
// is the floor under every primitive here and worth measuring on its own.
#define NM 8
static uint32_t g_masks[NM];

static void stage()
{
    for (int k = 0; k < NH; k++)
    {
        memcpy(hdrs[k], hdr_src[k], strlen(hdr_src[k]) + 1);
        memcpy(hdrs_copy[k], hdr_src[k], strlen(hdr_src[k]) + 1);
        memcpy(probes[k], probe_src[k], strlen(probe_src[k]) + 1);
    }
    for (int k = 0; k < NR; k++)
    {
        memcpy(routes[k], route_src[k], strlen(route_src[k]) + 1);
    }
    memset(longhay, 'a', sizeof(longhay) - 1);
    longhay[sizeof(longhay) - 1] = '\0';
    memcpy(longhay + 400, "boundary=--xyz", 14);

    g_masks[0] = 0x00000080u;
    g_masks[1] = 0x00008000u;
    g_masks[2] = 0x00800000u;
    g_masks[3] = 0x80000000u;
    g_masks[4] = 0x00808000u;
    g_masks[5] = 0x80800000u;
    g_masks[6] = 0x80008080u;
    g_masks[7] = 0x00800080u;
}

// A bench that measures the wrong answer is worthless, so every pair is proved against newlib over
// the whole corpus before a single cycle is counted.
static void checks()
{
    uint32_t a = 0;
    uint32_t b = 0;
    for (int k = 0; k < NH; k++)
    {
        a += (uint32_t)proto_scan_nul(hdrs[k], BCAP);
        b += (uint32_t)strnlen(hdrs[k], BCAP);
    }
    CHECK("scan_nul vs strnlen", a, b);

    a = 0;
    b = 0;
    for (int k = 0; k < NH; k++)
    {
        for (int j = 0; j < NH; j++)
        {
            a += proto_eq_str(hdrs[k], hdrs_copy[j], BCAP) ? 1u : 0u;
            b += (strcmp(hdrs[k], hdrs_copy[j]) == 0) ? 1u : 0u;
        }
    }
    CHECK("eq_str vs strcmp", a, b);

    a = 0;
    b = 0;
    for (int k = 0; k < NH; k++)
    {
        for (int j = 0; j < NH; j++)
        {
            a += proto_eq_str_ci(hdrs[k], probes[j], BCAP) ? 1u : 0u;
            b += (strcasecmp(hdrs[k], probes[j]) == 0) ? 1u : 0u;
        }
    }
    CHECK("eq_str_ci vs strcasecmp", a, b);

    a = 0;
    b = 0;
    for (int k = 0; k < NR; k++)
    {
        a += proto_has(routes[k], BCAP, "/:", sizeof("/:")) ? 1u : 0u;
        b += (strstr(routes[k], "/:") != NULL) ? 1u : 0u;
        const char *f = proto_find(routes[k], BCAP, "/:", sizeof("/:"));
        const char *s = strstr(routes[k], "/:");
        a += (uint32_t)(f == NULL ? 0 : (f - routes[k]) + 1);
        b += (uint32_t)(s == NULL ? 0 : (s - routes[k]) + 1);
    }
    CHECK("has+find vs strstr", a, b);

    a = (uint32_t)(uintptr_t)proto_find(longhay, sizeof(longhay), "boundary=", sizeof("boundary="));
    b = (uint32_t)(uintptr_t)strstr(longhay, "boundary=");
    CHECK("find long vs strstr", a, b);

    (void)proto_copy(dst, hdrs[1], BCAP);
    CHECK("copy vs strncpy", strcmp(dst, hdrs[1]), 0);
}

// Which shape wins is a distribution question, so the corpus is generated with the distribution as
// the knob rather than taken from one real example. A buffer holds an alphabet of A distinct bytes,
// uniform, so the anchor byte fires on 1/A of all positions. The needle's last byte is outside the
// alphabet, so no needle ever matches: every run is a full scan and the timing is the loop, not
// where the answer happened to sit.
#define ND 5
static const uint32_t DENS[ND] = {2u, 4u, 8u, 16u, 32u};
#define DLEN 256
static char dbuf[ND][DLEN];

static void build_dist()
{
    for (int d = 0; d < ND; d++)
    {
        uint32_t a = DENS[d];
        uint32_t x = 0x2545F491u; // fixed seed: the corpus must be identical on every run
        char prev = 0;
        for (int k = 0; k < DLEN - 1; k++)
        {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            char c = (char)('a' + (x % a));
            // No two 'a' in a row. The needle is a run of 'a', so it can never occur at any length,
            // while the anchor byte still fires on 1/A of positions and every candidate runs the
            // verify and fails at its second byte. Density is the only thing A changes.
            if (c == 'a' && prev == 'a')
            {
                c = (char)('a' + 1u + (x % (a - 1u)));
            }
            dbuf[d][k] = c;
            prev = c;
        }
        dbuf[d][DLEN - 1] = '\0';
    }
}

// The routing classification table, as supplied. 128 entries of uint32_t is 512 bytes, so it spans
// 16 cache lines on a 32-byte-line part; the route corpus touches the lowercase run and a handful of
// punctuation, which is most of them.
#define RT_INVALID 0x00000000U
#define RT_PATH_CHAR (1U << 0)
#define RT_SLASH (1U << 1)
#define RT_QUERY_START (1U << 2)
#define RT_AMPERSAND (1U << 3)
#define RT_EQUALS (1U << 4)
#define RT_FRAGMENT (1U << 5)
#define RT_PERCENT (1U << 6)

// The range-designator form ([0 ... 127] = ...) is a GNU C extension that C++ rejects, so the same
// contents are filled at boot. The rows below time the lookup, which is unaffected.
static uint32_t router_url_table[128];

static void build_router_table()
{
    for (int c = 0; c < 128; c++)
    {
        router_url_table[c] = RT_INVALID;
    }
    for (int c = 'a'; c <= 'z'; c++)
    {
        router_url_table[c] = RT_PATH_CHAR;
    }
    for (int c = 'A'; c <= 'Z'; c++)
    {
        router_url_table[c] = RT_PATH_CHAR;
    }
    for (int c = '0'; c <= '9'; c++)
    {
        router_url_table[c] = RT_PATH_CHAR;
    }
    static const char PATHY[] = "-._~!$'()*+,;@:";
    for (int k = 0; PATHY[k] != '\0'; k++)
    {
        router_url_table[(int)(unsigned char)PATHY[k]] = RT_PATH_CHAR;
    }
    router_url_table[(int)'/'] = RT_SLASH;
    router_url_table[(int)'?'] = RT_QUERY_START;
    router_url_table[(int)'&'] = RT_AMPERSAND;
    router_url_table[(int)'='] = RT_EQUALS;
    router_url_table[(int)'#'] = RT_FRAGMENT;
    router_url_table[(int)'%'] = RT_PERCENT;
}

// The same verdict the six ORed masks give, reduced to the delimiter bits so both rows fold the
// identical quantity into the sink.
#define RT_DELIMS (RT_SLASH | RT_QUERY_START | RT_AMPERSAND | RT_EQUALS | RT_FRAGMENT | RT_PERCENT)

static void swar_bench_task(void *)
{
    g_mhz = (double)getCpuFrequencyMhz();
    Serial.printf("CB swarbench  lanes=%u bytes  cpu=%.0f MHz\n", (unsigned)PC_SWAR_BYTES, g_mhz);

    stage();
    build_dist();
    build_router_table();
    checks();
    if (g_bad != 0)
    {
        Serial.printf("CB *** %u CHECK failures - every number below is invalid ***\n", (unsigned)g_bad);
    }

    // Every row below carries this: rotate the corpus index and fold into the volatile sink. It is
    // common to both contenders so it never changes which one wins, but it is a constant added to
    // both, and a constant added to both squeezes every ratio toward 1. Subtract it before quoting
    // a ratio, or quote the raw cycles.
    Serial.println("CB --- harness baseline (index + sink, no string work) ---");
    BENCH("baseline", 4000, g_sink += (uint32_t)(uintptr_t)hdrs[g_i++ % NH]);

    // Which lane fires first, five spellings of the same question. The die has nsau (count leading
    // zeros) and no popcount, so the arithmetic identity that is cheapest on paper is a libcall
    // here. What ships is the ctzll form, which is a 64-bit count on a 32-bit register file.
    Serial.println("CB --- lane index: five spellings of one question ---");
    BENCH("lane/zero_lane (ships)", 4000, g_sink += (uint32_t)pc_swar_zero_lane(g_masks[g_i++ & (NM - 1u)]));
    BENCH("lane/ctz32", 4000, g_sink += (uint32_t)(__builtin_ctz(g_masks[g_i++ & (NM - 1u)]) >> 3));
    BENCH("lane/clz32", 4000, g_sink += (uint32_t)((31u - (uint32_t)__builtin_clz(g_masks[g_i++ & (NM - 1u)])) >> 3));
    BENCH("lane/popcount", 4000, {
        uint32_t _m = g_masks[g_i++ & (NM - 1u)];
        g_sink += (uint32_t)(__builtin_popcount((_m - 1u) & ~_m) >> 3);
    });
    BENCH("lane/nsau raw", 4000, g_sink += (uint32_t)__builtin_clz(g_masks[g_i++ & (NM - 1u)]));

    Serial.println("CB --- short strings (HTTP field names, 4-18 bytes) ---");
    BENCH("scan_nul", 4000, g_sink += (uint32_t)proto_scan_nul(hdrs[g_i++ % NH], BCAP));
    BENCH("strnlen", 4000, g_sink += (uint32_t)strnlen(hdrs[g_i++ % NH], BCAP));
    BENCH("eq_str", 4000, g_sink += proto_eq_str(hdrs[g_i % NH], hdrs_copy[g_i++ % NH], BCAP));
    BENCH("strcmp", 4000, g_sink += (strcmp(hdrs[g_i % NH], hdrs_copy[g_i++ % NH]) == 0));
    BENCH("eq_str_ci", 4000, g_sink += proto_eq_str_ci(hdrs[g_i % NH], probes[g_i++ % NH], BCAP));
    BENCH("strcasecmp", 4000, g_sink += (strcasecmp(hdrs[g_i % NH], probes[g_i++ % NH]) == 0));
    BENCH("diff_ci", 4000, g_sink += (uint32_t)proto_diff_ci(hdrs[g_i % NH], probes[g_i++ % NH], 8));
    BENCH("strncasecmp", 4000, g_sink += (strncasecmp(hdrs[g_i % NH], probes[g_i++ % NH], 8) == 0));
    BENCH("copy", 4000, g_sink += (uint32_t)proto_copy(dst, hdrs[g_i++ % NH], BCAP));
    BENCH("strncpy", 4000, g_sink += (uint32_t)(uintptr_t)strncpy(dst, hdrs[g_i++ % NH], BCAP));

    Serial.println("CB --- long buffer (512 B), needle near the end ---");
    BENCH("scan_nul long", 400, g_sink += (uint32_t)proto_scan_nul(longhay + (g_i++ & 15), 512));
    BENCH("strnlen long", 400, g_sink += (uint32_t)strnlen(longhay + (g_i++ & 15), 512));
    BENCH("find long", 400,
          g_sink += (uint32_t)(uintptr_t)proto_find(longhay + (g_i++ & 15), 512, "boundary=", sizeof("boundary=")));
    BENCH("strstr long", 400, g_sink += (uint32_t)(uintptr_t)strstr(longhay + (g_i++ & 15), "boundary="));

    // A one-byte needle is the question scan_nul already answers, asked about a different byte, so
    // these rows are the convergence test: find should cost scan_nul plus one XOR per word. Compare
    // against "scan_nul long" above, not against strstr.
    BENCH("find 1byte long", 400,
          g_sink += (uint32_t)(uintptr_t)proto_find(longhay + (g_i++ & 15), 512, "?", sizeof("?")));
    BENCH("strchr 1byte long", 400, g_sink += (uint32_t)(uintptr_t)strchr(longhay + (g_i++ & 15), '?'));

    // 6 bytes: one carrier word plus 2, so the needle is settled by two overlapping whole loads
    // rather than one load and a 2-byte walk. "boundary=" at 9 exceeds 2*PC_SWAR_BYTES and does not.
    BENCH("find 6B long", 400,
          g_sink += (uint32_t)(uintptr_t)proto_find(longhay + (g_i++ & 15), 512, "ndary=", sizeof("ndary=")));
    BENCH("strstr 6B long", 400, g_sink += (uint32_t)(uintptr_t)strstr(longhay + (g_i++ & 15), "ndary="));

    Serial.println("CB --- dense first byte: route path searched for \"/:\" ---");
    BENCH("find /:", 4000, g_sink += (uint32_t)(uintptr_t)proto_find(routes[g_i++ % NR], BCAP, "/:", sizeof("/:")));
    BENCH("has /:", 4000, g_sink += proto_has(routes[g_i++ % NR], BCAP, "/:", sizeof("/:")));
    BENCH("strstr /:", 4000, g_sink += (uint32_t)(uintptr_t)strstr(routes[g_i++ % NR], "/:"));
    BENCH("has GET", 4000, g_sink += proto_has(hdrs[g_i++ % NH], BCAP, "GET", sizeof("GET")));
    BENCH("strstr GET", 4000, g_sink += (uint32_t)(uintptr_t)strstr(hdrs[g_i++ % NH], "GET"));

    // Needle length 1..4 over ONE corpus with ONE first byte, so density is held fixed and the only
    // variable is nlen. "/:" against "GET" cannot answer where the crossover is, because those rows
    // differ in first-byte density as well as in length.
    // The pattern as bits, not as characters. P is the pattern word, M says which bits are
    // significant, and a hit is (w ^ P) & M == 0. Three masks over the same corpus and the same
    // 2-byte pattern "/:", so the only variable is how many bits M admits.
    //
    // M = 0xFFFF   every bit of both bytes, identical to the byte-equality the code ships.
    // M = 0xFFDF   bit 5 of the second byte dropped: one pattern, either case, no fold and no branch.
    // M = 0x1F1F   the member field only, tags dropped, which is the widest mask that still
    //              distinguishes the letters.
    Serial.println("CB --- sieve byte test: plain eq vs masked eq, every offset ---");
#define SIEVE(body)                                                                                                    \
    {                                                                                                                  \
        uint32_t _h = 0;                                                                                               \
        const char *_s = routes[g_i++ % NR];                                                                           \
        for (uint32_t _k = 0; _k + 8u <= BCAP; _k += 4u)                                                               \
        {                                                                                                              \
            uint32_t _w0 = pc_swar_load_al(_s + _k);                                                                   \
            uint32_t _w1 = pc_swar_load_al(_s + _k + 4u);                                                              \
            uint32_t _f1 = (_w0 >> 8) | (_w1 << 24);                                                                   \
            body;                                                                                                      \
        }                                                                                                              \
        g_sink += _h;                                                                                                  \
    }
    // What ships: two 8-column equalities ANDed, all four start positions per step.
    BENCH("sieve eq", 4000, SIEVE(_h += pc_swar_eq(_w0, '/') & pc_swar_eq(_f1, ':')));
    // The same, with the byte test generalized to a masked bit pattern. M=FF is identical in meaning,
    // so this row is purely the price of the extra AND per pattern byte.
    BENCH("sieve masked M=FF", 4000,
          SIEVE(_h += pc_swar_has_zero((_w0 ^ (PC_SWAR_ONES * (uint32_t)'/')) & (PC_SWAR_ONES * 0xFFu)) &
                      pc_swar_has_zero((_f1 ^ (PC_SWAR_ONES * (uint32_t)':')) & (PC_SWAR_ONES * 0xFFu))));
    // Bit 5 dropped from the pattern: the same search, case-insensitive, no fold and no second path.
    BENCH("sieve masked ci", 4000,
          SIEVE(_h += pc_swar_has_zero((_w0 ^ (PC_SWAR_ONES * (uint32_t)'/')) & (PC_SWAR_ONES * 0xDFu)) &
                      pc_swar_has_zero((_f1 ^ (PC_SWAR_ONES * (uint32_t)':')) & (PC_SWAR_ONES * 0xDFu))));

    // Case-insensitive against case-sensitive, same dispatch, same corpus, one needle length apart.
    // The ci arms differ only in the byte test, so these pairs price pc_swar_eq_ci against pc_swar_eq
    // inside each arm rather than in isolation.
    Serial.println("CB --- case-insensitive find: same dispatch, folded byte test ---");
    BENCH("find    /: (len2)", 4000,
          g_sink += (uint32_t)(uintptr_t)proto_find(routes[g_i++ % NR], BCAP, "/:", sizeof("/:")));
    BENCH("find_ci /: (len2)", 4000,
          g_sink += (uint32_t)(uintptr_t)proto_find_ci(routes[g_i++ % NR], BCAP, "/:", sizeof("/:")));
    BENCH("find    x (len1)", 4000,
          g_sink += (uint32_t)(uintptr_t)proto_find(hdrs[g_i++ % NH], BCAP, "e", sizeof("e")));
    BENCH("find_ci E (len1)", 4000,
          g_sink += (uint32_t)(uintptr_t)proto_find_ci(hdrs[g_i++ % NH], BCAP, "E", sizeof("E")));
    BENCH("find    Content (len7)", 4000,
          g_sink += (uint32_t)(uintptr_t)proto_find(hdrs[g_i++ % NH], BCAP, "Content", sizeof("Content")));
    BENCH("find_ci CONTENT (len7)", 4000,
          g_sink += (uint32_t)(uintptr_t)proto_find_ci(hdrs[g_i++ % NH], BCAP, "CONTENT", sizeof("CONTENT")));

    // Anchor density 1/A against needle length, everything else held fixed. "Z" is outside the
    // alphabet, so the needle cannot occur and each row is a complete 255-byte scan.
    // Class queries, one range pair against a shared fold. Independent: each class is its own
    // ge/le pair on the raw word. Shared: fold case out once (w | 0x20), one pair says "letter",
    // and after that upper and lower differ only in bit 5, which a shift moves onto the guard bit.
    // The fold costs more than a single pair, so it only pays from the second query on.
    // The routing delimiter set: / ? & = # %. Three ways to ask "where is the next structural byte",
    // over the same route corpus.
    //   ored     six pc_swar_eq masks ORed, which is what the primitive's own doc describes
    //   prefilt  every one of the six is in 0x20..0x3F, a 32-byte aligned block, so one mask test
    //            rejects a whole word before any of the six run
    //   table    one indexed load per byte, the routing-flags table shape
    Serial.println("CB --- routing delimiter set: ORed masks vs block prefilter vs table ---");
    BENCH("delim ored", 4000, {
        uint32_t _a = 0;
        const char *_s = routes[g_i++ % NR];
        for (uint32_t _k = 0; _k + 4u <= BCAP; _k += 4u)
        {
            uint32_t _w = pc_swar_load_al(_s + _k);
            _a += pc_swar_eq(_w, '/') | pc_swar_eq(_w, '?') | pc_swar_eq(_w, '&') | pc_swar_eq(_w, '=') |
                  pc_swar_eq(_w, '#') | pc_swar_eq(_w, '%');
        }
        g_sink += _a;
    });
    BENCH("delim prefilt", 4000, {
        uint32_t _a = 0;
        const char *_s = routes[g_i++ % NR];
        for (uint32_t _k = 0; _k + 4u <= BCAP; _k += 4u)
        {
            uint32_t _w = pc_swar_load_al(_s + _k);
            uint32_t _blk = pc_swar_has_zero((_w & (PC_SWAR_ONES * 0xE0u)) ^ (PC_SWAR_ONES * 0x20u));
            if (_blk != 0)
            {
                _a += pc_swar_eq(_w, '/') | pc_swar_eq(_w, '?') | pc_swar_eq(_w, '&') | pc_swar_eq(_w, '=') |
                      pc_swar_eq(_w, '#') | pc_swar_eq(_w, '%');
            }
        }
        g_sink += _a;
    });
    BENCH("delim table", 4000, {
        uint32_t _a = 0;
        const char *_s = routes[g_i++ % NR];
        for (uint32_t _k = 0; _k < BCAP; _k++)
        {
            _a += router_url_table[(uint8_t)_s[_k] & 0x7Fu] & RT_DELIMS;
        }
        g_sink += _a;
    });

    Serial.println("CB --- class queries: independent pairs vs one shared fold ---");
    BENCH("upper only, own pair", 400, {
        uint32_t _a = 0;
        for (int _k = 0; _k + 4 <= 256; _k += 4)
        {
            uint32_t _w = pc_swar_load_al(dbuf[4] + _k);
            _a += pc_swar_ge(_w, 'A') & pc_swar_le(_w, 'Z');
        }
        g_sink += _a;
    });
    BENCH("upper only, shared fold", 400, {
        uint32_t _a = 0;
        for (int _k = 0; _k + 4 <= 256; _k += 4)
        {
            uint32_t _w = pc_swar_load_al(dbuf[4] + _k);
            uint32_t _lo = _w | (PC_SWAR_ONES * 0x20u);
            uint32_t _al = pc_swar_ge(_lo, 'a') & pc_swar_le(_lo, 'z') & ~_lo;
            _a += _al & ~((_w << 2) & PC_SWAR_HIGH);
        }
        g_sink += _a;
    });
    BENCH("upper+lower, own pairs", 400, {
        uint32_t _a = 0;
        for (int _k = 0; _k + 4 <= 256; _k += 4)
        {
            uint32_t _w = pc_swar_load_al(dbuf[4] + _k);
            _a += pc_swar_ge(_w, 'A') & pc_swar_le(_w, 'Z');
            _a += pc_swar_ge(_w, 'a') & pc_swar_le(_w, 'z');
        }
        g_sink += _a;
    });
    BENCH("upper+lower, shared fold", 400, {
        uint32_t _a = 0;
        for (int _k = 0; _k + 4 <= 256; _k += 4)
        {
            uint32_t _w = pc_swar_load_al(dbuf[4] + _k);
            uint32_t _lo = _w | (PC_SWAR_ONES * 0x20u);
            uint32_t _al = pc_swar_ge(_lo, 'a') & pc_swar_le(_lo, 'z') & ~_lo;
            uint32_t _cs = (_w << 2) & PC_SWAR_HIGH;
            _a += _al & ~_cs;
            _a += _al & _cs;
        }
        g_sink += _a;
    });

    Serial.println("CB --- distribution sweep: anchor fires 1/A, full scan, no match ---");
    for (int _d = 0; _d < ND; _d++)
    {
        char _lbl[32];
        const char *_h = dbuf[_d];
        snprintf(_lbl, sizeof(_lbl), "A=%-2u len2", (unsigned)DENS[_d]);
        BENCH(_lbl, 400, g_sink += (uint32_t)(uintptr_t)proto_find(_h, DLEN, "aa", sizeof("aa")));
        snprintf(_lbl, sizeof(_lbl), "A=%-2u len3", (unsigned)DENS[_d]);
        BENCH(_lbl, 400, g_sink += (uint32_t)(uintptr_t)proto_find(_h, DLEN, "aaa", sizeof("aaa")));
        snprintf(_lbl, sizeof(_lbl), "A=%-2u len4", (unsigned)DENS[_d]);
        BENCH(_lbl, 400, g_sink += (uint32_t)(uintptr_t)proto_find(_h, DLEN, "aaaa", sizeof("aaaa")));
        snprintf(_lbl, sizeof(_lbl), "A=%-2u strstr2", (unsigned)DENS[_d]);
        BENCH(_lbl, 400, g_sink += (uint32_t)(uintptr_t)strstr(_h, "aa"));
    }

    Serial.println("CB --- needle length ladder, fixed corpus and first byte ---");
    BENCH("len1 /", 4000, g_sink += (uint32_t)(uintptr_t)proto_find(routes[g_i++ % NR], BCAP, "/", sizeof("/")));
    BENCH("len2 /:", 4000, g_sink += (uint32_t)(uintptr_t)proto_find(routes[g_i++ % NR], BCAP, "/:", sizeof("/:")));
    BENCH("len3 /ap", 4000, g_sink += (uint32_t)(uintptr_t)proto_find(routes[g_i++ % NR], BCAP, "/ap", sizeof("/ap")));
    BENCH("len4 /api", 4000,
          g_sink += (uint32_t)(uintptr_t)proto_find(routes[g_i++ % NR], BCAP, "/api", sizeof("/api")));
    BENCH("len3 strstr", 4000, g_sink += (uint32_t)(uintptr_t)strstr(routes[g_i++ % NR], "/ap"));
    BENCH("len4 strstr", 4000, g_sink += (uint32_t)(uintptr_t)strstr(routes[g_i++ % NR], "/api"));

    Serial.println("CB --- mismatch at byte 0 (the common dispatch case) ---");
    BENCH("eq_str_ci miss", 4000, g_sink += proto_eq_str_ci(hdrs[g_i++ % NH], "zzzzzzzz", sizeof("zzzzzzzz")));
    BENCH("strcasecmp miss", 4000, g_sink += (strcasecmp(hdrs[g_i++ % NH], "zzzzzzzz") == 0));

    Serial.printf("CB done (sink=%u, checkfail=%u)\n", (unsigned)g_sink, (unsigned)g_bad);
    vTaskDelete(nullptr);
}

void setup()
{
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000)
    {
        delay(10);
    }
    delay(400);
    xTaskCreatePinnedToCore(swar_bench_task, "swarbench", 16384, nullptr, 5, nullptr, 1);
}

void loop()
{
    delay(1000);
}
