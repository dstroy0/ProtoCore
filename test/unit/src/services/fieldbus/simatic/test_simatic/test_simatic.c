// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Siemens SIMATIC serial link (services/fieldbus/simatic/simatic.h).
//
// The load-bearing case is test_bcc_covers_stuffed_data_and_terminator. The Siemens SIMATIC S7-300
// CP 341 manual, "Block Checksum", defines the 3964R BCC as "the even longitudinal parity (EXOR
// operation on all data bytes)" whose "calculation begins with the first byte of user data ... and
// ends after the DLE ETX code", and adds "If DLE duplication occurs, the DLE code is accounted for
// twice in the BCC calculation". Those three sentences fix the operation, the range and the
// treatment of the doubled DLE, and a receiver that disagrees on any one of them NAKs every block.
//
// The RK512 command / area byte values are NOT published in a text obtainable here (the CP-module
// manual is not online in full), so the telegram cases are category-3 properties: field ORDER,
// big-endian word encoding (Siemens words are big-endian), build-parse round trip, and fail-closed
// refusals. They are marked as such where they appear.

#include "services/fieldbus/simatic/simatic.h"
#include <string.h>

#include <unity.h>

// The state machine writes bytes through tx and delivers blocks through rx; both are captured here.
static uint8_t g_tx[512];
static size_t g_tx_n;
static uint8_t g_rx[512];
static size_t g_rx_n;
static int g_rx_calls;

static void cap_tx(void *user, uint8_t b)
{
    (void)user;
    if (g_tx_n < sizeof(g_tx))
    {
        g_tx[g_tx_n++] = b;
    }
}

static void cap_rx(void *user, const uint8_t *d, size_t n)
{
    (void)user;
    g_rx_calls++;
    g_rx_n = n < sizeof(g_rx) ? n : sizeof(g_rx);
    memcpy(g_rx, d, g_rx_n);
}

void setUp(void)
{
    g_tx_n = 0;
    g_rx_n = 0;
    g_rx_calls = 0;
}
void tearDown(void)
{
}

// ---------------------------------------------------------------------------
// 3964R block framing
// ---------------------------------------------------------------------------

// CP 341 "Block Checksum": EXOR over the block from the first user-data byte through DLE ETX, with
// a duplicated DLE counted twice.
//
// Payload 'A' DLE 'B' stuffs to 41 10 10 42, so the checked range is
//   41 10 10 42 10 03
// and the expected BCC follows from the definition alone:
//   0x41 ^ 0x10 ^ 0x10 ^ 0x42 ^ 0x10 ^ 0x03
//   = 0x41 ^ 0x42 = 0x03          (the DLE pair cancels: 0x10 ^ 0x10 = 0)
//   0x03 ^ 0x10 = 0x13
//   0x13 ^ 0x03 = 0x10
void test_bcc_covers_stuffed_data_and_terminator(void)
{
    static const uint8_t DATA[] = {0x41, SIMATIC_DLE, 0x42};
    static const uint8_t WANT[] = {0x41, 0x10, 0x10, 0x42, SIMATIC_DLE, SIMATIC_ETX, 0x10};
    uint8_t buf[16];

    SimaticV.build_block_3964r_args.buf = buf;
    SimaticV.build_block_3964r_args.cap = sizeof(buf);
    SimaticV.build_block_3964r_args.data = DATA;
    SimaticV.build_block_3964r_args.len = sizeof(DATA);
    SimaticV.build_block_3964r_args.with_bcc = PROTO_TRUE;
    Simatic.build_block_3964r(protocore_simatic_span());
    size_t n = SimaticV.n;
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));

    // the same value reached through the exported checksum over the checked range
    SimaticV.bcc_3964r_args.data = WANT;
    SimaticV.bcc_3964r_args.len = sizeof(WANT) - 1;
    Simatic.bcc_3964r(protocore_simatic_span());
    TEST_ASSERT_EQUAL_HEX8(0x10, SimaticV.value);
}

// The doubled DLE contributes 0x10 ^ 0x10 = 0, so stuffing cannot change the checksum. Two payloads
// that differ only by an embedded DLE therefore share a BCC over their stuffed forms.
void test_doubled_dle_is_checksum_neutral(void)
{
    static const uint8_t PAIR[] = {SIMATIC_DLE, SIMATIC_DLE};
    SimaticV.bcc_3964r_args.data = PAIR;
    SimaticV.bcc_3964r_args.len = sizeof(PAIR);
    Simatic.bcc_3964r(protocore_simatic_span());
    TEST_ASSERT_EQUAL_HEX8(0x00, SimaticV.value);
    SimaticV.bcc_3964r_args.data = PAIR;
    SimaticV.bcc_3964r_args.len = 0;
    Simatic.bcc_3964r(protocore_simatic_span());
    TEST_ASSERT_EQUAL_HEX8(0x00, SimaticV.value); // empty range: the zero seed
}

// Without the "R" the block is the stuffed data plus DLE ETX and nothing else.
void test_block_without_bcc_ends_at_dle_etx(void)
{
    static const uint8_t DATA[] = {0x41, 0x42};
    static const uint8_t WANT[] = {0x41, 0x42, SIMATIC_DLE, SIMATIC_ETX};
    uint8_t buf[16];
    SimaticV.build_block_3964r_args.buf = buf;
    SimaticV.build_block_3964r_args.cap = sizeof(buf);
    SimaticV.build_block_3964r_args.data = DATA;
    SimaticV.build_block_3964r_args.len = sizeof(DATA);
    SimaticV.build_block_3964r_args.with_bcc = PROTO_FALSE;
    Simatic.build_block_3964r(protocore_simatic_span());
    size_t n = SimaticV.n;
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));
}

// Transparency is what stuffing buys: any payload, including one that is entirely control bytes,
// survives build then parse unchanged.
void test_block_round_trip_is_transparent(void)
{
    static const uint8_t PAYLOADS[][6] = {
        {0x00, 0x01, 0x02, 0x03, 0x04, 0x05},
        {SIMATIC_DLE, SIMATIC_DLE, SIMATIC_DLE, SIMATIC_DLE, SIMATIC_DLE, SIMATIC_DLE},
        {SIMATIC_STX, SIMATIC_ETX, SIMATIC_NAK, SIMATIC_DLE, SIMATIC_ETX, 0xFF},
    };
    for (size_t i = 0; i < sizeof(PAYLOADS) / sizeof(PAYLOADS[0]); i++)
    {
        for (int r = 0; r < 2; r++)
        {
            proto_bool with_bcc = r ? PROTO_TRUE : PROTO_FALSE;
            uint8_t blk[32];
            uint8_t out[16];
            size_t olen = 0;
            SimaticV.build_block_3964r_args.buf = blk;
            SimaticV.build_block_3964r_args.cap = sizeof(blk);
            SimaticV.build_block_3964r_args.data = PAYLOADS[i];
            SimaticV.build_block_3964r_args.len = 6;
            SimaticV.build_block_3964r_args.with_bcc = with_bcc;
            Simatic.build_block_3964r(protocore_simatic_span());
            size_t n = SimaticV.n;
            TEST_ASSERT_NOT_EQUAL(0, n);
            SimaticV.parse_block_3964r_args.buf = blk;
            SimaticV.parse_block_3964r_args.len = n;
            SimaticV.parse_block_3964r_args.with_bcc = with_bcc;
            SimaticV.parse_block_3964r_args.out = out;
            SimaticV.parse_block_3964r_args.out_cap = sizeof(out);
            SimaticV.parse_block_3964r_args.out_len = &olen;
            Simatic.parse_block_3964r(protocore_simatic_span());
            TEST_ASSERT_TRUE(SimaticV.ok);
            TEST_ASSERT_EQUAL_UINT(6u, olen);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOADS[i], out, 6);
        }
    }
}

// A single flipped bit anywhere in the block must change the BCC, or the check buys nothing. Walked
// over every byte of the checked range and every bit position.
void test_single_bit_flip_changes_the_bcc(void)
{
    static const uint8_t BASE[] = {0x41, 0x10, 0x10, 0x42, SIMATIC_DLE, SIMATIC_ETX};
    SimaticV.bcc_3964r_args.data = BASE;
    SimaticV.bcc_3964r_args.len = sizeof(BASE);
    Simatic.bcc_3964r(protocore_simatic_span());
    uint8_t good = SimaticV.value;
    for (size_t i = 0; i < sizeof(BASE); i++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            uint8_t copy[sizeof(BASE)];
            memcpy(copy, BASE, sizeof(BASE));
            copy[i] ^= (uint8_t)(1u << bit);
            SimaticV.bcc_3964r_args.data = copy;
            SimaticV.bcc_3964r_args.len = sizeof(copy);
            Simatic.bcc_3964r(protocore_simatic_span());
            TEST_ASSERT_NOT_EQUAL(good, SimaticV.value);
        }
    }
}

// A corrupted BCC is refused rather than delivered: the check is fail-closed.
void test_parse_refuses_a_wrong_bcc(void)
{
    static const uint8_t DATA[] = {0x41, 0x42, 0x43};
    uint8_t blk[16];
    uint8_t out[16];
    size_t olen = 0;
    SimaticV.build_block_3964r_args.buf = blk;
    SimaticV.build_block_3964r_args.cap = sizeof(blk);
    SimaticV.build_block_3964r_args.data = DATA;
    SimaticV.build_block_3964r_args.len = sizeof(DATA);
    SimaticV.build_block_3964r_args.with_bcc = PROTO_TRUE;
    Simatic.build_block_3964r(protocore_simatic_span());
    size_t n = SimaticV.n;
    SimaticV.parse_block_3964r_args.buf = blk;
    SimaticV.parse_block_3964r_args.len = n;
    SimaticV.parse_block_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.parse_block_3964r_args.out = out;
    SimaticV.parse_block_3964r_args.out_cap = sizeof(out);
    SimaticV.parse_block_3964r_args.out_len = &olen;
    Simatic.parse_block_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    blk[n - 1] ^= 0xFF;
    SimaticV.parse_block_3964r_args.buf = blk;
    SimaticV.parse_block_3964r_args.len = n;
    SimaticV.parse_block_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.parse_block_3964r_args.out = out;
    SimaticV.parse_block_3964r_args.out_cap = sizeof(out);
    SimaticV.parse_block_3964r_args.out_len = &olen;
    Simatic.parse_block_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);
}

// Framing errors are refused: a dangling DLE, DLE followed by a byte that is neither DLE nor ETX,
// no terminator at all, and a missing BCC byte.
void test_parse_refuses_bad_framing(void)
{
    uint8_t out[16];
    size_t olen = 0;

    static const uint8_t DANGLING[] = {0x41, SIMATIC_DLE};
    SimaticV.parse_block_3964r_args.buf = DANGLING;
    SimaticV.parse_block_3964r_args.len = sizeof(DANGLING);
    SimaticV.parse_block_3964r_args.with_bcc = PROTO_FALSE;
    SimaticV.parse_block_3964r_args.out = out;
    SimaticV.parse_block_3964r_args.out_cap = sizeof(out);
    SimaticV.parse_block_3964r_args.out_len = &olen;
    Simatic.parse_block_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);

    static const uint8_t ILLEGAL[] = {0x41, SIMATIC_DLE, SIMATIC_STX, SIMATIC_DLE, SIMATIC_ETX};
    SimaticV.parse_block_3964r_args.buf = ILLEGAL;
    SimaticV.parse_block_3964r_args.len = sizeof(ILLEGAL);
    SimaticV.parse_block_3964r_args.with_bcc = PROTO_FALSE;
    SimaticV.parse_block_3964r_args.out = out;
    SimaticV.parse_block_3964r_args.out_cap = sizeof(out);
    SimaticV.parse_block_3964r_args.out_len = &olen;
    Simatic.parse_block_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);

    static const uint8_t NO_END[] = {0x41, 0x42, 0x43};
    SimaticV.parse_block_3964r_args.buf = NO_END;
    SimaticV.parse_block_3964r_args.len = sizeof(NO_END);
    SimaticV.parse_block_3964r_args.with_bcc = PROTO_FALSE;
    SimaticV.parse_block_3964r_args.out = out;
    SimaticV.parse_block_3964r_args.out_cap = sizeof(out);
    SimaticV.parse_block_3964r_args.out_len = &olen;
    Simatic.parse_block_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);

    static const uint8_t NO_BCC[] = {0x41, SIMATIC_DLE, SIMATIC_ETX};
    SimaticV.parse_block_3964r_args.buf = NO_BCC;
    SimaticV.parse_block_3964r_args.len = sizeof(NO_BCC);
    SimaticV.parse_block_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.parse_block_3964r_args.out = out;
    SimaticV.parse_block_3964r_args.out_cap = sizeof(out);
    SimaticV.parse_block_3964r_args.out_len = &olen;
    Simatic.parse_block_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);
}

// A payload that will not fit the caller's destination is refused rather than truncated, and a
// block that will not fit the build buffer reports 0.
void test_bounds_are_refusals_not_truncations(void)
{
    static const uint8_t DATA[] = {0x41, 0x42, 0x43, 0x44};
    uint8_t blk[16];
    SimaticV.build_block_3964r_args.buf = blk;
    SimaticV.build_block_3964r_args.cap = sizeof(blk);
    SimaticV.build_block_3964r_args.data = DATA;
    SimaticV.build_block_3964r_args.len = sizeof(DATA);
    SimaticV.build_block_3964r_args.with_bcc = PROTO_TRUE;
    Simatic.build_block_3964r(protocore_simatic_span());
    size_t n = SimaticV.n;

    uint8_t small[3];
    size_t olen = 0;
    SimaticV.parse_block_3964r_args.buf = blk;
    SimaticV.parse_block_3964r_args.len = n;
    SimaticV.parse_block_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.parse_block_3964r_args.out = small;
    SimaticV.parse_block_3964r_args.out_cap = sizeof(small);
    SimaticV.parse_block_3964r_args.out_len = &olen;
    Simatic.parse_block_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);

    // exactly the room the block needs, then one byte short of it
    uint8_t tight[7];
    SimaticV.build_block_3964r_args.buf = tight;
    SimaticV.build_block_3964r_args.cap = sizeof(tight);
    SimaticV.build_block_3964r_args.data = DATA;
    SimaticV.build_block_3964r_args.len = sizeof(DATA);
    SimaticV.build_block_3964r_args.with_bcc = PROTO_TRUE;
    Simatic.build_block_3964r(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(7u, SimaticV.n);
    SimaticV.build_block_3964r_args.buf = tight;
    SimaticV.build_block_3964r_args.cap = 6;
    SimaticV.build_block_3964r_args.data = DATA;
    SimaticV.build_block_3964r_args.len = sizeof(DATA);
    SimaticV.build_block_3964r_args.with_bcc = PROTO_TRUE;
    Simatic.build_block_3964r(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(0u, SimaticV.n);

    SimaticV.build_block_3964r_args.buf = NULL;
    SimaticV.build_block_3964r_args.cap = 16;
    SimaticV.build_block_3964r_args.data = DATA;
    SimaticV.build_block_3964r_args.len = sizeof(DATA);
    SimaticV.build_block_3964r_args.with_bcc = PROTO_TRUE;
    Simatic.build_block_3964r(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(0u, SimaticV.n);
    SimaticV.build_block_3964r_args.buf = blk;
    SimaticV.build_block_3964r_args.cap = sizeof(blk);
    SimaticV.build_block_3964r_args.data = NULL;
    SimaticV.build_block_3964r_args.len = 4;
    SimaticV.build_block_3964r_args.with_bcc = PROTO_TRUE;
    Simatic.build_block_3964r(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(0u, SimaticV.n);
    SimaticV.parse_block_3964r_args.buf = NULL;
    SimaticV.parse_block_3964r_args.len = 4;
    SimaticV.parse_block_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.parse_block_3964r_args.out = small;
    SimaticV.parse_block_3964r_args.out_cap = sizeof(small);
    SimaticV.parse_block_3964r_args.out_len = &olen;
    Simatic.parse_block_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);
    SimaticV.parse_block_3964r_args.buf = blk;
    SimaticV.parse_block_3964r_args.len = n;
    SimaticV.parse_block_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.parse_block_3964r_args.out = NULL;
    SimaticV.parse_block_3964r_args.out_cap = 4;
    SimaticV.parse_block_3964r_args.out_len = &olen;
    Simatic.parse_block_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);
    SimaticV.parse_block_3964r_args.buf = blk;
    SimaticV.parse_block_3964r_args.len = n;
    SimaticV.parse_block_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.parse_block_3964r_args.out = small;
    SimaticV.parse_block_3964r_args.out_cap = sizeof(small);
    SimaticV.parse_block_3964r_args.out_len = NULL;
    Simatic.parse_block_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);
}

// ---------------------------------------------------------------------------
// 3964R link state machine
// ---------------------------------------------------------------------------

// The interactive handshake, in the order the procedure defines it: STX out, partner's DLE in, then
// the block, then the partner's DLE ends the job and the link returns to idle.
void test_send_handshake_order(void)
{
    Simatic3964Ctx ctx;
    static const uint8_t DATA[] = {0x41, 0x42};
    SimaticV.init_3964r_args.ctx = &ctx;
    SimaticV.init_3964r_args.high_priority = PROTO_TRUE;
    SimaticV.init_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.init_3964r_args.tx = cap_tx;
    SimaticV.init_3964r_args.rx = cap_rx;
    SimaticV.init_3964r_args.user = NULL;
    Simatic.init_3964r(protocore_simatic_span());

    SimaticV.send_3964r_args.ctx = &ctx;
    SimaticV.send_3964r_args.data = DATA;
    SimaticV.send_3964r_args.len = sizeof(DATA);
    SimaticV.send_3964r_args.now_ms = 0;
    Simatic.send_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    TEST_ASSERT_EQUAL_UINT(1u, g_tx_n);
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_STX, g_tx[0]);
    SimaticV.idle_3964r_args.ctx = &ctx;
    Simatic.idle_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);

    SimaticV.rx_byte_3964r_args.ctx = &ctx;
    SimaticV.rx_byte_3964r_args.b = SIMATIC_DLE;
    SimaticV.rx_byte_3964r_args.now_ms = 1;
    Simatic.rx_byte_3964r(protocore_simatic_span()); // connect acknowledged
    static const uint8_t WANT[] = {SIMATIC_STX, 0x41, 0x42, SIMATIC_DLE, SIMATIC_ETX, 0x41 ^ 0x42 ^ 0x10 ^ 0x03};
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), g_tx_n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_tx, sizeof(WANT));

    SimaticV.rx_byte_3964r_args.ctx = &ctx;
    SimaticV.rx_byte_3964r_args.b = SIMATIC_DLE;
    SimaticV.rx_byte_3964r_args.now_ms = 2;
    Simatic.rx_byte_3964r(protocore_simatic_span()); // block acknowledged
    SimaticV.idle_3964r_args.ctx = &ctx;
    Simatic.idle_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
}

// The receive half: a partner STX is answered with DLE, the block is collected, and a valid block
// is acked with DLE and handed up exactly once.
void test_receive_acks_and_delivers(void)
{
    Simatic3964Ctx ctx;
    static const uint8_t DATA[] = {0x41, SIMATIC_DLE, 0x42};
    uint8_t blk[16];
    SimaticV.init_3964r_args.ctx = &ctx;
    SimaticV.init_3964r_args.high_priority = PROTO_FALSE;
    SimaticV.init_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.init_3964r_args.tx = cap_tx;
    SimaticV.init_3964r_args.rx = cap_rx;
    SimaticV.init_3964r_args.user = NULL;
    Simatic.init_3964r(protocore_simatic_span());

    SimaticV.build_block_3964r_args.buf = blk;
    SimaticV.build_block_3964r_args.cap = sizeof(blk);
    SimaticV.build_block_3964r_args.data = DATA;
    SimaticV.build_block_3964r_args.len = sizeof(DATA);
    SimaticV.build_block_3964r_args.with_bcc = PROTO_TRUE;
    Simatic.build_block_3964r(protocore_simatic_span());
    size_t n = SimaticV.n;
    SimaticV.rx_byte_3964r_args.ctx = &ctx;
    SimaticV.rx_byte_3964r_args.b = SIMATIC_STX;
    SimaticV.rx_byte_3964r_args.now_ms = 0;
    Simatic.rx_byte_3964r(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(1u, g_tx_n);
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_DLE, g_tx[0]); // ready

    for (size_t i = 0; i < n; i++)
    {
        SimaticV.rx_byte_3964r_args.ctx = &ctx;
        SimaticV.rx_byte_3964r_args.b = blk[i];
        SimaticV.rx_byte_3964r_args.now_ms = (uint32_t)(1 + i);
        Simatic.rx_byte_3964r(protocore_simatic_span());
    }
    TEST_ASSERT_EQUAL_UINT(2u, g_tx_n);
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_DLE, g_tx[1]); // block acknowledged
    TEST_ASSERT_EQUAL_INT(1, g_rx_calls);
    TEST_ASSERT_EQUAL_UINT(sizeof(DATA), g_rx_n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, g_rx, sizeof(DATA));
    SimaticV.idle_3964r_args.ctx = &ctx;
    Simatic.idle_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
}

// A block whose BCC does not match is NAKed and never handed up.
void test_receive_naks_a_bad_bcc(void)
{
    Simatic3964Ctx ctx;
    static const uint8_t DATA[] = {0x41, 0x42};
    uint8_t blk[16];
    SimaticV.init_3964r_args.ctx = &ctx;
    SimaticV.init_3964r_args.high_priority = PROTO_FALSE;
    SimaticV.init_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.init_3964r_args.tx = cap_tx;
    SimaticV.init_3964r_args.rx = cap_rx;
    SimaticV.init_3964r_args.user = NULL;
    Simatic.init_3964r(protocore_simatic_span());

    SimaticV.build_block_3964r_args.buf = blk;
    SimaticV.build_block_3964r_args.cap = sizeof(blk);
    SimaticV.build_block_3964r_args.data = DATA;
    SimaticV.build_block_3964r_args.len = sizeof(DATA);
    SimaticV.build_block_3964r_args.with_bcc = PROTO_TRUE;
    Simatic.build_block_3964r(protocore_simatic_span());
    size_t n = SimaticV.n;
    blk[n - 1] ^= 0x01;
    SimaticV.rx_byte_3964r_args.ctx = &ctx;
    SimaticV.rx_byte_3964r_args.b = SIMATIC_STX;
    SimaticV.rx_byte_3964r_args.now_ms = 0;
    Simatic.rx_byte_3964r(protocore_simatic_span());
    for (size_t i = 0; i < n; i++)
    {
        SimaticV.rx_byte_3964r_args.ctx = &ctx;
        SimaticV.rx_byte_3964r_args.b = blk[i];
        SimaticV.rx_byte_3964r_args.now_ms = (uint32_t)(1 + i);
        Simatic.rx_byte_3964r(protocore_simatic_span());
    }
    TEST_ASSERT_EQUAL_UINT(2u, g_tx_n);
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_NAK, g_tx[1]);
    TEST_ASSERT_EQUAL_INT(0, g_rx_calls);
    SimaticV.idle_3964r_args.ctx = &ctx;
    Simatic.idle_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
}

// On a simultaneous STX the low-priority station yields to receive and the high-priority one holds
// its send. One end of a link is configured each way, so exactly one block moves.
void test_priority_arbitration_on_an_stx_collision(void)
{
    Simatic3964Ctx low;
    static const uint8_t DATA[] = {0x41};
    SimaticV.init_3964r_args.ctx = &low;
    SimaticV.init_3964r_args.high_priority = PROTO_FALSE;
    SimaticV.init_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.init_3964r_args.tx = cap_tx;
    SimaticV.init_3964r_args.rx = cap_rx;
    SimaticV.init_3964r_args.user = NULL;
    Simatic.init_3964r(protocore_simatic_span());
    SimaticV.send_3964r_args.ctx = &low;
    SimaticV.send_3964r_args.data = DATA;
    SimaticV.send_3964r_args.len = sizeof(DATA);
    SimaticV.send_3964r_args.now_ms = 0;
    Simatic.send_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    SimaticV.rx_byte_3964r_args.ctx = &low;
    SimaticV.rx_byte_3964r_args.b = SIMATIC_STX;
    SimaticV.rx_byte_3964r_args.now_ms = 1;
    Simatic.rx_byte_3964r(protocore_simatic_span()); // the partner's STX arrives too
    TEST_ASSERT_EQUAL_UINT(2u, g_tx_n);
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_DLE, g_tx[1]); // yielded: answered ready

    setUp();
    Simatic3964Ctx high;
    SimaticV.init_3964r_args.ctx = &high;
    SimaticV.init_3964r_args.high_priority = PROTO_TRUE;
    SimaticV.init_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.init_3964r_args.tx = cap_tx;
    SimaticV.init_3964r_args.rx = cap_rx;
    SimaticV.init_3964r_args.user = NULL;
    Simatic.init_3964r(protocore_simatic_span());
    SimaticV.send_3964r_args.ctx = &high;
    SimaticV.send_3964r_args.data = DATA;
    SimaticV.send_3964r_args.len = sizeof(DATA);
    SimaticV.send_3964r_args.now_ms = 0;
    Simatic.send_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    SimaticV.rx_byte_3964r_args.ctx = &high;
    SimaticV.rx_byte_3964r_args.b = SIMATIC_STX;
    SimaticV.rx_byte_3964r_args.now_ms = 1;
    Simatic.rx_byte_3964r(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(1u, g_tx_n); // held: still only its own STX
}

// A NAK on the block restarts the transfer from STX, and the retry count is bounded: after
// SIMATIC_MAX_BLOCK_RETRY attempts the link gives up rather than retrying forever.
void test_block_nak_retries_are_bounded(void)
{
    Simatic3964Ctx ctx;
    static const uint8_t DATA[] = {0x41};
    SimaticV.init_3964r_args.ctx = &ctx;
    SimaticV.init_3964r_args.high_priority = PROTO_TRUE;
    SimaticV.init_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.init_3964r_args.tx = cap_tx;
    SimaticV.init_3964r_args.rx = cap_rx;
    SimaticV.init_3964r_args.user = NULL;
    Simatic.init_3964r(protocore_simatic_span());
    SimaticV.send_3964r_args.ctx = &ctx;
    SimaticV.send_3964r_args.data = DATA;
    SimaticV.send_3964r_args.len = sizeof(DATA);
    SimaticV.send_3964r_args.now_ms = 0;
    Simatic.send_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);

    int stx_count = 1;
    for (int i = 0; i < SIMATIC_MAX_BLOCK_RETRY + 2; i++)
    {
        SimaticV.idle_3964r_args.ctx = &ctx;
        Simatic.idle_3964r(protocore_simatic_span());
        if (SimaticV.ok)
        {
            break;
        }
        SimaticV.rx_byte_3964r_args.ctx = &ctx;
        SimaticV.rx_byte_3964r_args.b = SIMATIC_DLE;
        SimaticV.rx_byte_3964r_args.now_ms = 1;
        Simatic.rx_byte_3964r(protocore_simatic_span()); // connect
        SimaticV.rx_byte_3964r_args.ctx = &ctx;
        SimaticV.rx_byte_3964r_args.b = SIMATIC_NAK;
        SimaticV.rx_byte_3964r_args.now_ms = 2;
        Simatic.rx_byte_3964r(protocore_simatic_span()); // reject the block
        SimaticV.idle_3964r_args.ctx = &ctx;
        Simatic.idle_3964r(protocore_simatic_span());
        if (!SimaticV.ok)
        {
            stx_count++;
        }
    }
    SimaticV.idle_3964r_args.ctx = &ctx;
    Simatic.idle_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    TEST_ASSERT_EQUAL_INT(SIMATIC_MAX_BLOCK_RETRY, stx_count);
}

// QVZ: no connect DLE inside the acknowledgement-delay window re-sends STX; the reattempts are
// bounded the same way and the link ends idle.
void test_qvz_timeout_retries_then_gives_up(void)
{
    Simatic3964Ctx ctx;
    static const uint8_t DATA[] = {0x41};
    SimaticV.init_3964r_args.ctx = &ctx;
    SimaticV.init_3964r_args.high_priority = PROTO_TRUE;
    SimaticV.init_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.init_3964r_args.tx = cap_tx;
    SimaticV.init_3964r_args.rx = cap_rx;
    SimaticV.init_3964r_args.user = NULL;
    Simatic.init_3964r(protocore_simatic_span());
    SimaticV.send_3964r_args.ctx = &ctx;
    SimaticV.send_3964r_args.data = DATA;
    SimaticV.send_3964r_args.len = sizeof(DATA);
    SimaticV.send_3964r_args.now_ms = 0;
    Simatic.send_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);

    SimaticV.tick_3964r_args.ctx = &ctx;
    SimaticV.tick_3964r_args.now_ms = PROTOCORE_SIMATIC_QVZ_MS - 1;
    Simatic.tick_3964r(protocore_simatic_span()); // inside the window: nothing happens
    TEST_ASSERT_EQUAL_UINT(1u, g_tx_n);

    uint32_t t = 0;
    for (int i = 0; i < SIMATIC_MAX_CONN_RETRY + 2; i++)
    {
        t += PROTOCORE_SIMATIC_QVZ_MS;
        SimaticV.tick_3964r_args.ctx = &ctx;
        SimaticV.tick_3964r_args.now_ms = t;
        Simatic.tick_3964r(protocore_simatic_span());
    }
    SimaticV.idle_3964r_args.ctx = &ctx;
    Simatic.idle_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    TEST_ASSERT_EQUAL_UINT((size_t)SIMATIC_MAX_CONN_RETRY, g_tx_n); // one STX per attempt
}

// ZVZ: a gap between characters of an inbound block aborts the receive with a NAK.
void test_zvz_inter_character_timeout_naks(void)
{
    Simatic3964Ctx ctx;
    SimaticV.init_3964r_args.ctx = &ctx;
    SimaticV.init_3964r_args.high_priority = PROTO_FALSE;
    SimaticV.init_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.init_3964r_args.tx = cap_tx;
    SimaticV.init_3964r_args.rx = cap_rx;
    SimaticV.init_3964r_args.user = NULL;
    Simatic.init_3964r(protocore_simatic_span());
    SimaticV.rx_byte_3964r_args.ctx = &ctx;
    SimaticV.rx_byte_3964r_args.b = SIMATIC_STX;
    SimaticV.rx_byte_3964r_args.now_ms = 0;
    Simatic.rx_byte_3964r(protocore_simatic_span());
    SimaticV.rx_byte_3964r_args.ctx = &ctx;
    SimaticV.rx_byte_3964r_args.b = 0x41;
    SimaticV.rx_byte_3964r_args.now_ms = 1;
    Simatic.rx_byte_3964r(protocore_simatic_span());
    SimaticV.tick_3964r_args.ctx = &ctx;
    SimaticV.tick_3964r_args.now_ms = 1 + PROTOCORE_SIMATIC_ZVZ_MS - 1;
    Simatic.tick_3964r(protocore_simatic_span());
    SimaticV.idle_3964r_args.ctx = &ctx;
    Simatic.idle_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);
    SimaticV.tick_3964r_args.ctx = &ctx;
    SimaticV.tick_3964r_args.now_ms = 1 + PROTOCORE_SIMATIC_ZVZ_MS;
    Simatic.tick_3964r(protocore_simatic_span());
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_NAK, g_tx[g_tx_n - 1]);
    SimaticV.idle_3964r_args.ctx = &ctx;
    Simatic.idle_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    TEST_ASSERT_EQUAL_INT(0, g_rx_calls);
}

// Half-duplex: one job at a time, and a payload the block buffer cannot hold is refused up front.
void test_send_refuses_when_busy_or_unframeable(void)
{
    Simatic3964Ctx ctx;
    static const uint8_t DATA[] = {0x41};
    uint8_t big[PROTOCORE_SIMATIC_BLOCK_MAX];
    memset(big, 0x41, sizeof(big));

    SimaticV.init_3964r_args.ctx = &ctx;
    SimaticV.init_3964r_args.high_priority = PROTO_TRUE;
    SimaticV.init_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.init_3964r_args.tx = cap_tx;
    SimaticV.init_3964r_args.rx = cap_rx;
    SimaticV.init_3964r_args.user = NULL;
    Simatic.init_3964r(protocore_simatic_span());
    SimaticV.send_3964r_args.ctx = &ctx;
    SimaticV.send_3964r_args.data = DATA;
    SimaticV.send_3964r_args.len = sizeof(DATA);
    SimaticV.send_3964r_args.now_ms = 0;
    Simatic.send_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    SimaticV.send_3964r_args.ctx = &ctx;
    SimaticV.send_3964r_args.data = DATA;
    SimaticV.send_3964r_args.len = sizeof(DATA);
    SimaticV.send_3964r_args.now_ms = 0;
    Simatic.send_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);

    SimaticV.init_3964r_args.ctx = &ctx;
    SimaticV.init_3964r_args.high_priority = PROTO_TRUE;
    SimaticV.init_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.init_3964r_args.tx = cap_tx;
    SimaticV.init_3964r_args.rx = cap_rx;
    SimaticV.init_3964r_args.user = NULL;
    Simatic.init_3964r(protocore_simatic_span());
    SimaticV.send_3964r_args.ctx = &ctx;
    SimaticV.send_3964r_args.data = big;
    SimaticV.send_3964r_args.len = sizeof(big);
    SimaticV.send_3964r_args.now_ms = 0;
    Simatic.send_3964r(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok); // no room for DLE ETX BCC
}

// init leaves the link idle with no bytes emitted, and null callbacks are not called through.
void test_init_is_idle_and_null_callbacks_are_safe(void)
{
    Simatic3964Ctx ctx;
    static const uint8_t DATA[] = {0x41};
    SimaticV.init_3964r_args.ctx = &ctx;
    SimaticV.init_3964r_args.high_priority = PROTO_TRUE;
    SimaticV.init_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.init_3964r_args.tx = NULL;
    SimaticV.init_3964r_args.rx = NULL;
    SimaticV.init_3964r_args.user = NULL;
    Simatic.init_3964r(protocore_simatic_span());
    SimaticV.idle_3964r_args.ctx = &ctx;
    Simatic.idle_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    SimaticV.send_3964r_args.ctx = &ctx;
    SimaticV.send_3964r_args.data = DATA;
    SimaticV.send_3964r_args.len = sizeof(DATA);
    SimaticV.send_3964r_args.now_ms = 0;
    Simatic.send_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    SimaticV.rx_byte_3964r_args.ctx = &ctx;
    SimaticV.rx_byte_3964r_args.b = SIMATIC_DLE;
    SimaticV.rx_byte_3964r_args.now_ms = 1;
    Simatic.rx_byte_3964r(protocore_simatic_span());
    SimaticV.rx_byte_3964r_args.ctx = &ctx;
    SimaticV.rx_byte_3964r_args.b = SIMATIC_DLE;
    SimaticV.rx_byte_3964r_args.now_ms = 2;
    Simatic.rx_byte_3964r(protocore_simatic_span());
    SimaticV.idle_3964r_args.ctx = &ctx;
    Simatic.idle_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, g_tx_n); // nothing captured: the sink was null
}

// A tick while idle is a no-op, so a caller may drive it unconditionally.
void test_tick_while_idle_does_nothing(void)
{
    Simatic3964Ctx ctx;
    SimaticV.init_3964r_args.ctx = &ctx;
    SimaticV.init_3964r_args.high_priority = PROTO_TRUE;
    SimaticV.init_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.init_3964r_args.tx = cap_tx;
    SimaticV.init_3964r_args.rx = cap_rx;
    SimaticV.init_3964r_args.user = NULL;
    Simatic.init_3964r(protocore_simatic_span());
    SimaticV.tick_3964r_args.ctx = &ctx;
    SimaticV.tick_3964r_args.now_ms = 0;
    Simatic.tick_3964r(protocore_simatic_span());
    SimaticV.tick_3964r_args.ctx = &ctx;
    SimaticV.tick_3964r_args.now_ms = 1000000;
    Simatic.tick_3964r(protocore_simatic_span());
    SimaticV.idle_3964r_args.ctx = &ctx;
    Simatic.idle_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, g_tx_n);
}

// ---------------------------------------------------------------------------
// RK512 telegrams - category-3 properties (see the file header)
// ---------------------------------------------------------------------------

// Siemens words are big-endian, so a word's high octet precedes its low octet everywhere in the
// telegram: in the address field, the count field and every data word.
void test_rk512_words_are_big_endian(void)
{
    static const uint16_t WORDS[] = {0x1234, 0xABCD};
    uint8_t buf[32];
    SimaticV.build_send_rk512_args.buf = buf;
    SimaticV.build_send_rk512_args.cap = sizeof(buf);
    SimaticV.build_send_rk512_args.area = RK512_AREA_DB;
    SimaticV.build_send_rk512_args.dbnr = 10;
    SimaticV.build_send_rk512_args.addr = 0x0102;
    SimaticV.build_send_rk512_args.words = WORDS;
    SimaticV.build_send_rk512_args.wcount = 2;
    Simatic.build_send_rk512(protocore_simatic_span());
    size_t n = SimaticV.n;
    TEST_ASSERT_EQUAL_UINT(12u, n); // 8-byte header + 2 words

    TEST_ASSERT_EQUAL_HEX8(0x01, buf[4]); // addr high
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[5]); // addr low
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[6]); // count high
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[7]); // count low
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[8]);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[9]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, buf[10]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, buf[11]);
}

// The header a builder emits is the header the parser reads back: every field survives.
void test_rk512_header_round_trip(void)
{
    static const Rk512Area AREAS[] = {RK512_AREA_DB, RK512_AREA_DX, RK512_AREA_MB, RK512_AREA_EB,
                                      RK512_AREA_AB, RK512_AREA_PB, RK512_AREA_ZB, RK512_AREA_TB};
    uint8_t buf[32];
    Rk512Header h;
    for (size_t i = 0; i < sizeof(AREAS) / sizeof(AREAS[0]); i++)
    {
        SimaticV.build_fetch_rk512_args.buf = buf;
        SimaticV.build_fetch_rk512_args.cap = sizeof(buf);
        SimaticV.build_fetch_rk512_args.area = AREAS[i];
        SimaticV.build_fetch_rk512_args.dbnr = (uint8_t)i;
        SimaticV.build_fetch_rk512_args.addr = 0xBEEF;
        SimaticV.build_fetch_rk512_args.wcount = 0x00FF;
        Simatic.build_fetch_rk512(protocore_simatic_span());
        size_t n = SimaticV.n;
        TEST_ASSERT_EQUAL_UINT(8u, n);
        SimaticV.parse_header_rk512_args.buf = buf;
        SimaticV.parse_header_rk512_args.len = n;
        SimaticV.parse_header_rk512_args.out = &h;
        Simatic.parse_header_rk512(protocore_simatic_span());
        TEST_ASSERT_TRUE(SimaticV.ok);
        TEST_ASSERT_EQUAL_INT(RK512_CMD_FETCH, h.cmd);
        TEST_ASSERT_EQUAL_INT(AREAS[i], h.area);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, h.dbnr);
        TEST_ASSERT_EQUAL_HEX16(0xBEEF, h.addr);
        TEST_ASSERT_EQUAL_HEX16(0x00FF, h.count);
    }

    static const uint16_t W[] = {0x0001};
    SimaticV.build_send_rk512_args.buf = buf;
    SimaticV.build_send_rk512_args.cap = sizeof(buf);
    SimaticV.build_send_rk512_args.area = RK512_AREA_MB;
    SimaticV.build_send_rk512_args.dbnr = 3;
    SimaticV.build_send_rk512_args.addr = 7;
    SimaticV.build_send_rk512_args.words = W;
    SimaticV.build_send_rk512_args.wcount = 1;
    Simatic.build_send_rk512(protocore_simatic_span());
    size_t sn = SimaticV.n;
    SimaticV.parse_header_rk512_args.buf = buf;
    SimaticV.parse_header_rk512_args.len = sn;
    SimaticV.parse_header_rk512_args.out = &h;
    Simatic.parse_header_rk512(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    TEST_ASSERT_EQUAL_INT(RK512_CMD_SEND, h.cmd);
    TEST_ASSERT_EQUAL_UINT16(1u, h.count);
}

// A reaction telegram carries the status word and, for a FETCH answer, the data words after it.
void test_rk512_reaction_carries_status_and_data(void)
{
    uint8_t buf[16];
    uint16_t status = 0xFFFF;
    const uint8_t *data = (const uint8_t *)0x1;
    size_t dlen = 99;

    SimaticV.build_reaction_rk512_args.buf = buf;
    SimaticV.build_reaction_rk512_args.cap = sizeof(buf);
    SimaticV.build_reaction_rk512_args.status = 0;
    Simatic.build_reaction_rk512(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(3u, SimaticV.n);
    SimaticV.parse_reaction_rk512_args.buf = buf;
    SimaticV.parse_reaction_rk512_args.len = 3;
    SimaticV.parse_reaction_rk512_args.status = &status;
    SimaticV.parse_reaction_rk512_args.data = &data;
    SimaticV.parse_reaction_rk512_args.dlen = &dlen;
    Simatic.parse_reaction_rk512(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    TEST_ASSERT_EQUAL_HEX16(0x0000, status);
    TEST_ASSERT_NULL(data);
    TEST_ASSERT_EQUAL_UINT(0u, dlen);

    // a non-zero status is a big-endian word like every other
    SimaticV.build_reaction_rk512_args.buf = buf;
    SimaticV.build_reaction_rk512_args.cap = sizeof(buf);
    SimaticV.build_reaction_rk512_args.status = 0x1234;
    Simatic.build_reaction_rk512(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(3u, SimaticV.n);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[2]);

    // a FETCH answer appends the words the caller read
    buf[3] = 0xAA;
    buf[4] = 0xBB;
    SimaticV.parse_reaction_rk512_args.buf = buf;
    SimaticV.parse_reaction_rk512_args.len = 5;
    SimaticV.parse_reaction_rk512_args.status = &status;
    SimaticV.parse_reaction_rk512_args.data = &data;
    SimaticV.parse_reaction_rk512_args.dlen = &dlen;
    Simatic.parse_reaction_rk512(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    TEST_ASSERT_EQUAL_HEX16(0x1234, status);
    TEST_ASSERT_EQUAL_UINT(2u, dlen);
    TEST_ASSERT_EQUAL_HEX8(0xAA, data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, data[1]);
}

// The parsers fail closed: a short telegram, an unknown command byte and an area code outside the
// defined range are all refused.
void test_rk512_parsers_fail_closed(void)
{
    uint8_t buf[16];
    Rk512Header h;
    uint16_t status = 0;

    SimaticV.build_fetch_rk512_args.buf = buf;
    SimaticV.build_fetch_rk512_args.cap = sizeof(buf);
    SimaticV.build_fetch_rk512_args.area = RK512_AREA_DB;
    SimaticV.build_fetch_rk512_args.dbnr = 1;
    SimaticV.build_fetch_rk512_args.addr = 0;
    SimaticV.build_fetch_rk512_args.wcount = 1;
    Simatic.build_fetch_rk512(protocore_simatic_span());
    size_t n = SimaticV.n;
    for (size_t short_len = 0; short_len < n; short_len++)
    {
        SimaticV.parse_header_rk512_args.buf = buf;
        SimaticV.parse_header_rk512_args.len = short_len;
        SimaticV.parse_header_rk512_args.out = &h;
        Simatic.parse_header_rk512(protocore_simatic_span());
        TEST_ASSERT_FALSE(SimaticV.ok);
    }

    buf[0] = (uint8_t)RK512_CMD_REACTION; // not a request command
    SimaticV.parse_header_rk512_args.buf = buf;
    SimaticV.parse_header_rk512_args.len = n;
    SimaticV.parse_header_rk512_args.out = &h;
    Simatic.parse_header_rk512(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);

    buf[0] = (uint8_t)RK512_CMD_FETCH;
    buf[2] = 0x00; // below RK512_AREA_DB
    SimaticV.parse_header_rk512_args.buf = buf;
    SimaticV.parse_header_rk512_args.len = n;
    SimaticV.parse_header_rk512_args.out = &h;
    Simatic.parse_header_rk512(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);
    buf[2] = 0x09; // above RK512_AREA_TB
    SimaticV.parse_header_rk512_args.buf = buf;
    SimaticV.parse_header_rk512_args.len = n;
    SimaticV.parse_header_rk512_args.out = &h;
    Simatic.parse_header_rk512(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);

    SimaticV.parse_header_rk512_args.buf = NULL;
    SimaticV.parse_header_rk512_args.len = n;
    SimaticV.parse_header_rk512_args.out = &h;
    Simatic.parse_header_rk512(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);
    SimaticV.parse_header_rk512_args.buf = buf;
    SimaticV.parse_header_rk512_args.len = n;
    SimaticV.parse_header_rk512_args.out = NULL;
    Simatic.parse_header_rk512(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);

    SimaticV.build_reaction_rk512_args.buf = buf;
    SimaticV.build_reaction_rk512_args.cap = sizeof(buf);
    SimaticV.build_reaction_rk512_args.status = 0;
    Simatic.build_reaction_rk512(protocore_simatic_span());
    SimaticV.parse_reaction_rk512_args.buf = buf;
    SimaticV.parse_reaction_rk512_args.len = 2;
    SimaticV.parse_reaction_rk512_args.status = &status;
    SimaticV.parse_reaction_rk512_args.data = NULL;
    SimaticV.parse_reaction_rk512_args.dlen = NULL;
    Simatic.parse_reaction_rk512(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);
    buf[0] = (uint8_t)RK512_CMD_SEND;
    SimaticV.parse_reaction_rk512_args.buf = buf;
    SimaticV.parse_reaction_rk512_args.len = 3;
    SimaticV.parse_reaction_rk512_args.status = &status;
    SimaticV.parse_reaction_rk512_args.data = NULL;
    SimaticV.parse_reaction_rk512_args.dlen = NULL;
    Simatic.parse_reaction_rk512(protocore_simatic_span());
    TEST_ASSERT_FALSE(SimaticV.ok);
}

// The builders refuse rather than overflow: one byte short of the exact need reports 0.
void test_rk512_builders_refuse_a_short_buffer(void)
{
    static const uint16_t W[] = {0x0001, 0x0002};
    uint8_t buf[16];
    SimaticV.build_send_rk512_args.buf = buf;
    SimaticV.build_send_rk512_args.cap = 12;
    SimaticV.build_send_rk512_args.area = RK512_AREA_DB;
    SimaticV.build_send_rk512_args.dbnr = 0;
    SimaticV.build_send_rk512_args.addr = 0;
    SimaticV.build_send_rk512_args.words = W;
    SimaticV.build_send_rk512_args.wcount = 2;
    Simatic.build_send_rk512(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(12u, SimaticV.n);
    SimaticV.build_send_rk512_args.buf = buf;
    SimaticV.build_send_rk512_args.cap = 11;
    SimaticV.build_send_rk512_args.area = RK512_AREA_DB;
    SimaticV.build_send_rk512_args.dbnr = 0;
    SimaticV.build_send_rk512_args.addr = 0;
    SimaticV.build_send_rk512_args.words = W;
    SimaticV.build_send_rk512_args.wcount = 2;
    Simatic.build_send_rk512(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(0u, SimaticV.n);
    SimaticV.build_send_rk512_args.buf = buf;
    SimaticV.build_send_rk512_args.cap = 16;
    SimaticV.build_send_rk512_args.area = RK512_AREA_DB;
    SimaticV.build_send_rk512_args.dbnr = 0;
    SimaticV.build_send_rk512_args.addr = 0;
    SimaticV.build_send_rk512_args.words = NULL;
    SimaticV.build_send_rk512_args.wcount = 2;
    Simatic.build_send_rk512(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(0u, SimaticV.n);
    SimaticV.build_send_rk512_args.buf = NULL;
    SimaticV.build_send_rk512_args.cap = 16;
    SimaticV.build_send_rk512_args.area = RK512_AREA_DB;
    SimaticV.build_send_rk512_args.dbnr = 0;
    SimaticV.build_send_rk512_args.addr = 0;
    SimaticV.build_send_rk512_args.words = W;
    SimaticV.build_send_rk512_args.wcount = 2;
    Simatic.build_send_rk512(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(0u, SimaticV.n);

    SimaticV.build_fetch_rk512_args.buf = buf;
    SimaticV.build_fetch_rk512_args.cap = 8;
    SimaticV.build_fetch_rk512_args.area = RK512_AREA_DB;
    SimaticV.build_fetch_rk512_args.dbnr = 0;
    SimaticV.build_fetch_rk512_args.addr = 0;
    SimaticV.build_fetch_rk512_args.wcount = 1;
    Simatic.build_fetch_rk512(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(8u, SimaticV.n);
    SimaticV.build_fetch_rk512_args.buf = buf;
    SimaticV.build_fetch_rk512_args.cap = 7;
    SimaticV.build_fetch_rk512_args.area = RK512_AREA_DB;
    SimaticV.build_fetch_rk512_args.dbnr = 0;
    SimaticV.build_fetch_rk512_args.addr = 0;
    SimaticV.build_fetch_rk512_args.wcount = 1;
    Simatic.build_fetch_rk512(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(0u, SimaticV.n);
    SimaticV.build_fetch_rk512_args.buf = NULL;
    SimaticV.build_fetch_rk512_args.cap = 8;
    SimaticV.build_fetch_rk512_args.area = RK512_AREA_DB;
    SimaticV.build_fetch_rk512_args.dbnr = 0;
    SimaticV.build_fetch_rk512_args.addr = 0;
    SimaticV.build_fetch_rk512_args.wcount = 1;
    Simatic.build_fetch_rk512(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(0u, SimaticV.n);

    SimaticV.build_reaction_rk512_args.buf = buf;
    SimaticV.build_reaction_rk512_args.cap = 3;
    SimaticV.build_reaction_rk512_args.status = 0;
    Simatic.build_reaction_rk512(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(3u, SimaticV.n);
    SimaticV.build_reaction_rk512_args.buf = buf;
    SimaticV.build_reaction_rk512_args.cap = 2;
    SimaticV.build_reaction_rk512_args.status = 0;
    Simatic.build_reaction_rk512(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(0u, SimaticV.n);
    SimaticV.build_reaction_rk512_args.buf = NULL;
    SimaticV.build_reaction_rk512_args.cap = 3;
    SimaticV.build_reaction_rk512_args.status = 0;
    Simatic.build_reaction_rk512(protocore_simatic_span());
    TEST_ASSERT_EQUAL_UINT(0u, SimaticV.n);
}

// An RK512 telegram is carried as a 3964R block payload, so a SEND whose words contain a DLE octet
// must arrive with those octets intact. This is the two layers composed.
void test_rk512_telegram_survives_3964r_framing(void)
{
    static const uint16_t WORDS[] = {0x1010, 0x0310}; // DLE and ETX octets inside the data
    uint8_t tel[32];
    uint8_t blk[64];
    uint8_t out[32];
    size_t olen = 0;

    SimaticV.build_send_rk512_args.buf = tel;
    SimaticV.build_send_rk512_args.cap = sizeof(tel);
    SimaticV.build_send_rk512_args.area = RK512_AREA_DB;
    SimaticV.build_send_rk512_args.dbnr = 5;
    SimaticV.build_send_rk512_args.addr = 0x0010;
    SimaticV.build_send_rk512_args.words = WORDS;
    SimaticV.build_send_rk512_args.wcount = 2;
    Simatic.build_send_rk512(protocore_simatic_span());
    size_t tn = SimaticV.n;
    TEST_ASSERT_EQUAL_UINT(12u, tn);
    SimaticV.build_block_3964r_args.buf = blk;
    SimaticV.build_block_3964r_args.cap = sizeof(blk);
    SimaticV.build_block_3964r_args.data = tel;
    SimaticV.build_block_3964r_args.len = tn;
    SimaticV.build_block_3964r_args.with_bcc = PROTO_TRUE;
    Simatic.build_block_3964r(protocore_simatic_span());
    size_t bn = SimaticV.n;
    TEST_ASSERT_NOT_EQUAL(0, bn);
    SimaticV.parse_block_3964r_args.buf = blk;
    SimaticV.parse_block_3964r_args.len = bn;
    SimaticV.parse_block_3964r_args.with_bcc = PROTO_TRUE;
    SimaticV.parse_block_3964r_args.out = out;
    SimaticV.parse_block_3964r_args.out_cap = sizeof(out);
    SimaticV.parse_block_3964r_args.out_len = &olen;
    Simatic.parse_block_3964r(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    TEST_ASSERT_EQUAL_UINT(tn, olen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tel, out, tn);

    Rk512Header h;
    SimaticV.parse_header_rk512_args.buf = out;
    SimaticV.parse_header_rk512_args.len = olen;
    SimaticV.parse_header_rk512_args.out = &h;
    Simatic.parse_header_rk512(protocore_simatic_span());
    TEST_ASSERT_TRUE(SimaticV.ok);
    TEST_ASSERT_EQUAL_HEX16(0x0010, h.addr);
    TEST_ASSERT_EQUAL_HEX16(2, h.count);
}
