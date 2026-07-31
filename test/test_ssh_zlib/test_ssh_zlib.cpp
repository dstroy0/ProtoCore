// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SSH server-to-client streaming compressor
// (network_drivers/presentation/ssh/transport/ssh_zlib). It emits ONE zlib stream per session with a
// persistent sliding window carried across packets (context takeover) and a Z_SYNC_FLUSH boundary
// after each packet - the 00 00 ff ff marker is KEPT on the wire.
//
// Correctness is checked two ways:
//   1. The whole session is one continuous zlib stream: header(2) + block + marker + block + marker...
//      Concatenating every packet's output, stripping the 2-byte zlib header, yields a valid RFC 1951
//      stream that inflate_raw() (validated against Python zlib in test_inflate) decodes to the
//      concatenation of the originals - proving cross-packet back-references resolve.
//   2. The exact packet-by-packet decodability under a REFERENCE zlib (Python zlib.decompressobj,
//      i.e. OpenSSH's model) is verified out-of-tree by tools' cross-check (see the module header);
//      this suite is the in-tree regression guard.

#include "network_drivers/presentation/codec/inflate/inflate.h"
#include "network_drivers/presentation/ssh/transport/ssh_zlib.h"
#include <string.h>
#include <unity.h>

// Compressor state + buffers (caller-owned).
static SshDeflate g_z;
static uint8_t g_work[SSH_ZLIB_WORK_SIZE];
static uint16_t g_head[SSH_ZLIB_HASH_SIZE];
static uint16_t g_prev[SSH_ZLIB_WORK_SIZE];
static uint16_t g_llc[288];
static uint8_t g_lll[288];
static uint16_t g_dc[30];
static uint8_t g_dl[30];

// Accumulators for the whole-session round-trip (sized for the longest test session, ~90 KB).
static uint8_t g_stream[128 * 1024]; // all compressed packets concatenated
static size_t g_stream_len;
static uint8_t g_orig[128 * 1024]; // all originals concatenated
static size_t g_orig_len;
static uint8_t g_out[2048]; // one packet's compressed output
static uint8_t g_decoded[128 * 1024];
static uint8_t g_iscratch[INFLATE_SCRATCH_SIZE];

static void reset_stream()
{
    ssh_deflate_init(&g_z, g_work, g_head, g_prev, g_llc, g_lll, g_dc, g_dl);
    g_stream_len = 0;
    g_orig_len = 0;
}

// Compress one payload and append both the compressed bytes and the original to the accumulators.
static size_t feed(const uint8_t *src, size_t n)
{
    size_t olen = 0;
    int rc = ssh_deflate_packet(&g_z, src, n, g_out, sizeof(g_out), &olen);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_TRUE(g_stream_len + olen <= sizeof(g_stream));
    memcpy(g_stream + g_stream_len, g_out, olen);
    g_stream_len += olen;
    TEST_ASSERT_TRUE(g_orig_len + n <= sizeof(g_orig));
    memcpy(g_orig + g_orig_len, src, n);
    g_orig_len += n;
    return olen;
}

// Decode the whole accumulated stream (minus the 2-byte zlib header) and assert it equals the
// concatenation of every original payload.
static void verify_stream()
{
    TEST_ASSERT_TRUE(g_stream_len >= 2);
    TEST_ASSERT_EQUAL_HEX8(0x78, g_stream[0]); // zlib CMF (deflate, 32 KB window)
    TEST_ASSERT_EQUAL_HEX8(0x9C, g_stream[1]); // zlib FLG (default level)
    size_t dlen = 0;
    int rc = (int)inflate_raw(g_stream + 2, g_stream_len - 2, g_decoded, sizeof(g_decoded), &dlen, g_iscratch,
                              sizeof(g_iscratch));
    TEST_ASSERT_EQUAL_INT(InflateResult::INFLATE_OK, rc);
    TEST_ASSERT_EQUAL_size_t(g_orig_len, dlen);
    TEST_ASSERT_EQUAL_MEMORY(g_orig, g_decoded, g_orig_len);
}

void setUp()
{
    reset_stream();
}
void tearDown()
{
}

// A realistic terminal session: repeated prompts must back-reference prior packets (context takeover).
void test_session_roundtrip_and_context_takeover()
{
    const char *prompt = "user@esp32:~$ ";
    feed((const uint8_t *)prompt, strlen(prompt));
    const char *cmd = "cat /etc/motd\r\n";
    feed((const uint8_t *)cmd, strlen(cmd));
    size_t repeat_clen = feed((const uint8_t *)prompt, strlen(prompt)); // exact repeat of packet 0
    const char *out = "Welcome to the server.\r\nWelcome to the server.\r\n";
    feed((const uint8_t *)out, strlen(out));
    verify_stream();
    // The repeated prompt must compress to less than its literal length: the window reached back into
    // the first packet's bytes. (14 raw bytes -> a single back-reference + framing.)
    TEST_ASSERT_TRUE(repeat_clen < strlen(prompt));
}

// Empty payloads are valid packets (a keep-alive / zero-length channel write) and must round-trip.
void test_empty_payloads()
{
    feed((const uint8_t *)"hello", 5);
    feed((const uint8_t *)"", 0);
    feed((const uint8_t *)"world", 5);
    feed((const uint8_t *)"", 0);
    verify_stream();
}

// All 256 byte values in one packet, then again, exercises the full literal alphabet + a big match.
void test_all_byte_values()
{
    uint8_t buf[256];
    for (int i = 0; i < 256; i++)
    {
        buf[i] = (uint8_t)i;
    }
    feed(buf, sizeof(buf));
    feed(buf, sizeof(buf)); // identical -> should be a long back-reference
    verify_stream();
}

// A long session that slides the window many times over must still decode as one stream.
void test_window_slide_long_session()
{
    uint8_t buf[1500];
    for (int k = 0; k < 60; k++) // 60 * 1500 = 90 KB >> window
    {
        for (int j = 0; j < (int)sizeof(buf); j++)
        {
            buf[j] = (uint8_t)("The quick brown fox. "[j % 21]) ^ (uint8_t)((j & 1) ? 0 : k);
        }
        feed(buf, sizeof(buf));
    }
    verify_stream();
}

// A max-size payload (the input-length boundary) must be accepted and round-trip.
void test_max_input_payload()
{
    static uint8_t buf[PC_SSH_ZLIB_MAX_IN];
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)(i * 31 + 7);
    }
    feed(buf, sizeof(buf));
    verify_stream();
}

// Deterministic xorshift32 for fuzzing.
static uint32_t s_rng = 0x51ab77c3u;
static uint32_t rng()
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

// Fuzz: many packets of random length + content across one continuous stream must always round-trip.
void test_fuzz_stream_roundtrip()
{
    uint8_t buf[400];
    for (int iter = 0; iter < 200; iter++)
    {
        size_t n = rng() % (sizeof(buf) + 1);
        for (size_t i = 0; i < n; i++)
        {
            buf[i] = (uint8_t)rng();
        }
        feed(buf, n);
    }
    verify_stream();
}

// Low-entropy fuzz -> lots of cross-packet matches, exercising the chain walk + window.
void test_fuzz_low_entropy_stream()
{
    uint8_t buf[400];
    for (int iter = 0; iter < 200; iter++)
    {
        size_t n = 1 + rng() % sizeof(buf);
        for (size_t i = 0; i < n; i++)
        {
            buf[i] = (uint8_t)('a' + (rng() % 4));
        }
        feed(buf, n);
    }
    verify_stream();
}

// Input longer than PC_SSH_ZLIB_MAX_IN must be rejected (the caller chunks to packet size).
void test_oversize_input_rejected()
{
    static uint8_t buf[PC_SSH_ZLIB_MAX_IN + 1];
    memset(buf, 'x', sizeof(buf));
    size_t olen = 0;
    int rc = ssh_deflate_packet(&g_z, buf, sizeof(buf), g_out, sizeof(g_out), &olen);
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

// A too-small output buffer must fail closed (report -1, never overrun dst_cap).
void test_output_overflow_fails_closed()
{
    uint8_t buf[256];
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)rng(); // incompressible -> expands slightly
    }
    uint8_t tiny[8];
    size_t olen = 0;
    int rc = ssh_deflate_packet(&g_z, buf, sizeof(buf), tiny, sizeof(tiny), &olen);
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

// A corrupted `hist` (past the hist<=PC_WINDOW invariant normal operation always maintains) must still
// fail closed rather than overrun the work buffer in the memcpy that follows. SshDeflate is a caller-
// owned flat struct with no encapsulation, so this pokes the field directly to defend the guard itself.
void test_hist_overflow_invariant_rejected()
{
    g_z.hist = SSH_ZLIB_WORK_SIZE; // hist + src_len will exceed the work buffer capacity
    uint8_t buf[1] = {'x'};
    size_t olen = 0;
    int rc = ssh_deflate_packet(&g_z, buf, sizeof(buf), g_out, sizeof(g_out), &olen);
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_session_roundtrip_and_context_takeover);
    RUN_TEST(test_empty_payloads);
    RUN_TEST(test_all_byte_values);
    RUN_TEST(test_window_slide_long_session);
    RUN_TEST(test_max_input_payload);
    RUN_TEST(test_fuzz_stream_roundtrip);
    RUN_TEST(test_fuzz_low_entropy_stream);
    RUN_TEST(test_oversize_input_rejected);
    RUN_TEST(test_output_overflow_fails_closed);
    RUN_TEST(test_hist_overflow_invariant_rejected);
    return UNITY_END();
}
