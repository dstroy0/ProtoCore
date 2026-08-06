// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tests the SSH client-to-server resumable INFLATE (ssh_inflate) against golden vectors produced by
// real zlib (compressobj level 6, windowBits 15, Z_PARTIAL_FLUSH per packet) - exactly the wire format
// OpenSSH's compress.c emits. Decoding those packet-by-packet exercises the cross-packet bit carry,
// the 32 KB context-takeover window (packet 2 is a long back-reference into packet 0), fixed / dynamic
// / stored blocks, an empty payload, and a tiny 1-byte payload. Vectors: tools/gen_ssh_inflate_vectors.py.

#include "network_drivers/presentation/ssh/transport/ssh_inflate.h"

#include <unity.h>

#include "ssh_inflate_vectors.inc"

void setUp()
{
}
void tearDown()
{
}

static uint8_t g_window[SSH_INFLATE_WINDOW];

// Every real-zlib packet decodes byte-exact, in order, through one persistent stream.
void test_decode_partial_flush_stream()
{
    SshInflate z;
    SshInflater.init(&z, g_window);
    uint8_t out[2048];
    for (int i = 0; i < SSH_INFLATE_NVEC; i++)
    {
        size_t out_len = 0xDEAD;
        int rc = SshInflater.packet(&z, vec_comp[i], vec_comp_len[i], out, sizeof(out), &out_len);
        TEST_ASSERT_EQUAL_INT(0, rc);
        TEST_ASSERT_EQUAL_size_t(vec_plain_len[i], out_len);
        if (vec_plain_len[i])
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(vec_plain[i], out, vec_plain_len[i]);
        }
    }
}

// A fresh stream re-decodes the same vectors (init resets window + carry + header state).
void test_reinit_resets_stream()
{
    SshInflate z;
    SshInflater.init(&z, g_window);
    uint8_t out[2048];
    size_t out_len = 0;
    // Decode a couple of packets, then re-init and decode from the top again.
    SshInflater.packet(&z, vec_comp[0], vec_comp_len[0], out, sizeof(out), &out_len);
    SshInflater.packet(&z, vec_comp[1], vec_comp_len[1], out, sizeof(out), &out_len);

    SshInflater.init(&z, g_window);
    int rc = SshInflater.packet(&z, vec_comp[0], vec_comp_len[0], out, sizeof(out), &out_len);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_size_t(vec_plain_len[0], out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(vec_plain[0], out, vec_plain_len[0]);
}

// The 2-byte zlib header is validated: a wrong compression method / bad check byte fails closed.
void test_rejects_bad_header()
{
    SshInflate z;
    uint8_t out[64];
    size_t out_len = 0;

    SshInflater.init(&z, g_window);
    const uint8_t bad_cm[2] = {0x77, 0x9C}; // CM nibble != 8
    TEST_ASSERT_EQUAL_INT(-1, SshInflater.packet(&z, bad_cm, sizeof(bad_cm), out, sizeof(out), &out_len));

    SshInflater.init(&z, g_window);
    const uint8_t bad_check[2] = {0x78, 0x9D}; // CM=8 but (0x789D % 31) != 0
    TEST_ASSERT_EQUAL_INT(-1, SshInflater.packet(&z, bad_check, sizeof(bad_check), out, sizeof(out), &out_len));
}

// A reserved block type (BTYPE 3) after a valid header is rejected.
void test_rejects_bad_block_type()
{
    SshInflate z;
    SshInflater.init(&z, g_window);
    uint8_t out[64];
    size_t out_len = 0;
    // 0x78 0x9C zlib header, then a block header BFINAL=0 BTYPE=11 -> low three bits 0b110 = 0x06.
    const uint8_t bad_block[3] = {0x78, 0x9C, 0x06};
    TEST_ASSERT_EQUAL_INT(-1, SshInflater.packet(&z, bad_block, sizeof(bad_block), out, sizeof(out), &out_len));
}

// An output buffer too small for the decompressed payload fails closed rather than overrunning.
void test_output_overflow_fails_closed()
{
    SshInflate z;
    SshInflater.init(&z, g_window);
    uint8_t tiny[4];
    size_t out_len = 0;
    // vec 0 decompresses to 38 bytes; a 4-byte sink must report overflow.
    TEST_ASSERT_EQUAL_INT(-1, SshInflater.packet(&z, vec_comp[0], vec_comp_len[0], tiny, sizeof(tiny), &out_len));
}

// A split header (1 byte, then the rest) is carried and completes on the next call.
void test_header_split_across_calls()
{
    SshInflate z;
    SshInflater.init(&z, g_window);
    uint8_t out[2048];
    size_t out_len = 0xDEAD;
    // Feed just the first header byte: no output, no error, carried.
    TEST_ASSERT_EQUAL_INT(0, SshInflater.packet(&z, vec_comp[0], 1, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(0, out_len);
    // Feed the rest of packet 0: it now decodes fully.
    int rc = SshInflater.packet(&z, vec_comp[0] + 1, vec_comp_len[0] - 1, out, sizeof(out), &out_len);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_size_t(vec_plain_len[0], out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(vec_plain[0], out, vec_plain_len[0]);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_decode_partial_flush_stream);
    RUN_TEST(test_reinit_resets_stream);
    RUN_TEST(test_rejects_bad_header);
    RUN_TEST(test_rejects_bad_block_type);
    RUN_TEST(test_output_overflow_fails_closed);
    RUN_TEST(test_header_split_across_calls);
    return UNITY_END();
}
