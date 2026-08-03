// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Siemens SIMATIC serial codec (services/fieldbus/simatic): 3964R block framing
// (DLE-doubling + XOR BCC), the 3964R link state machine (STX/DLE handshake, retries, QVZ/ZVZ
// timeouts, priority arbitration), and RK512 SEND/FETCH/reaction telegrams (big-endian words).

#include "services/fieldbus/simatic/simatic.h"
#include <string.h>
#include <unity.h>

// ---- tx / rx capture for the state machine --------------------------------

static uint8_t g_tx[2048];
static size_t g_tx_n;
static uint8_t g_rx[2048];
static size_t g_rx_n;

static void cap_tx(void *user, uint8_t b)
{
    (void)user;
    if (g_tx_n < 2048)
    {
        g_tx[g_tx_n++] = b;
    }
}
static void cap_rx(void *user, const uint8_t *d, size_t n)
{
    (void)user;
    g_rx_n = n < sizeof(g_rx) ? n : sizeof(g_rx);
    memcpy(g_rx, d, g_rx_n);
}

void setUp()
{
    g_tx_n = 0;
    g_rx_n = 0;
}
void tearDown()
{
}

// ---- 3964R framing --------------------------------------------------------

void test_bcc_is_xor()
{
    const uint8_t d[] = {0x11, 0x22, 0x33};
    TEST_ASSERT_EQUAL_HEX8(0x11 ^ 0x22 ^ 0x33, pc_3964r_bcc(d, sizeof(d)));
    // a doubled DLE pair cancels in the XOR (0x10 ^ 0x10 = 0)
    const uint8_t pair[] = {SIMATIC_DLE, SIMATIC_DLE};
    TEST_ASSERT_EQUAL_HEX8(0, pc_3964r_bcc(pair, sizeof(pair)));
}

void test_build_block_stuffs_dle_and_terminates()
{
    const uint8_t data[] = {0x41, SIMATIC_DLE, 0x42}; // 'A', DLE, 'B'
    uint8_t buf[32];
    size_t n = pc_3964r_build_block(buf, sizeof(buf), data, sizeof(data), PROTO_TRUE);
    // 0x41, DLE, DLE (doubled), 0x42, DLE, ETX, BCC
    TEST_ASSERT_EQUAL_size_t(7, n);
    const uint8_t want[] = {0x41, SIMATIC_DLE, SIMATIC_DLE, 0x42, SIMATIC_DLE, SIMATIC_ETX};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, 6);
    TEST_ASSERT_EQUAL_HEX8(pc_3964r_bcc(buf, 6), buf[6]); // BCC over stuffed data + DLE ETX
}

void test_block_round_trip_with_embedded_dle()
{
    const uint8_t data[] = {0x01, SIMATIC_DLE, SIMATIC_ETX, SIMATIC_DLE, 0xFF}; // DLE and ETX in payload
    uint8_t blk[64];
    size_t n = pc_3964r_build_block(blk, sizeof(blk), data, sizeof(data), PROTO_TRUE);
    TEST_ASSERT_GREATER_THAN_size_t(0, n);
    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_TRUE(pc_3964r_parse_block(blk, n, PROTO_TRUE, out, sizeof(out), &olen));
    TEST_ASSERT_EQUAL_size_t(sizeof(data), olen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, out, olen);
}

void test_block_round_trip_no_bcc()
{
    const uint8_t data[] = {0xAA, 0xBB, 0xCC};
    uint8_t blk[32];
    size_t n = pc_3964r_build_block(blk, sizeof(blk), data, sizeof(data), PROTO_FALSE);
    uint8_t out[32];
    size_t olen = 0;
    TEST_ASSERT_TRUE(pc_3964r_parse_block(blk, n, PROTO_FALSE, out, sizeof(out), &olen));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, out, olen);
}

void test_parse_rejects_bad()
{
    uint8_t out[32];
    size_t olen = 0;
    // bad BCC
    uint8_t blk[16];
    const uint8_t data[] = {0x10, 0x20};
    size_t n = pc_3964r_build_block(blk, sizeof(blk), data, sizeof(data), PROTO_TRUE);
    blk[n - 1] ^= 0xFF; // corrupt BCC
    TEST_ASSERT_FALSE(pc_3964r_parse_block(blk, n, PROTO_TRUE, out, sizeof(out), &olen));
    // missing DLE ETX terminator
    const uint8_t noterm[] = {0x41, 0x42, 0x43};
    TEST_ASSERT_FALSE(pc_3964r_parse_block(noterm, sizeof(noterm), PROTO_FALSE, out, sizeof(out), &olen));
    // dangling DLE (truncated)
    const uint8_t dangle[] = {0x41, SIMATIC_DLE};
    TEST_ASSERT_FALSE(pc_3964r_parse_block(dangle, sizeof(dangle), PROTO_FALSE, out, sizeof(out), &olen));
    // DLE + illegal control byte
    const uint8_t badctl[] = {SIMATIC_DLE, 0x55, SIMATIC_DLE, SIMATIC_ETX};
    TEST_ASSERT_FALSE(pc_3964r_parse_block(badctl, sizeof(badctl), PROTO_FALSE, out, sizeof(out), &olen));
    // out-buffer overflow (fail-closed)
    const uint8_t big[] = {1, 2, 3, 4, 5};
    uint8_t bigblk[32];
    size_t bn = pc_3964r_build_block(bigblk, sizeof(bigblk), big, sizeof(big), PROTO_FALSE);
    uint8_t tiny[2];
    TEST_ASSERT_FALSE(pc_3964r_parse_block(bigblk, bn, PROTO_FALSE, tiny, sizeof(tiny), &olen));
}

void test_build_block_rejects_bad_args()
{
    // A null destination, and a null payload pointer with a non-zero length, are refused; a null
    // payload with len 0 is the legal "empty block" (terminator only).
    uint8_t buf[8];
    const uint8_t d[] = {0x41};
    TEST_ASSERT_EQUAL_size_t(0, pc_3964r_build_block(NULL, sizeof(buf), d, 1, PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(0, pc_3964r_build_block(buf, sizeof(buf), NULL, 1, PROTO_FALSE));
    size_t n = pc_3964r_build_block(buf, sizeof(buf), NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_DLE, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_ETX, buf[1]);
}

void test_build_block_overflow_at_each_stage()
{
    // Every write stage is capacity-checked independently: payload byte, the doubled DLE, the
    // DLE ETX terminator, and the BCC.
    uint8_t buf[8];
    const uint8_t d[] = {0x41};
    const uint8_t dle[] = {SIMATIC_DLE};
    TEST_ASSERT_EQUAL_size_t(0, pc_3964r_build_block(buf, 0, d, 1, PROTO_FALSE));   // no room at all
    TEST_ASSERT_EQUAL_size_t(0, pc_3964r_build_block(buf, 1, dle, 1, PROTO_FALSE)); // room for DLE, not its double
    TEST_ASSERT_EQUAL_size_t(0, pc_3964r_build_block(buf, 2, d, 1, PROTO_FALSE));   // no room for DLE ETX
    TEST_ASSERT_EQUAL_size_t(0, pc_3964r_build_block(buf, 3, d, 1, PROTO_TRUE));    // no room for the BCC
    TEST_ASSERT_EQUAL_size_t(3, pc_3964r_build_block(buf, 3, d, 1, PROTO_FALSE));   // same block fits without one
}

void test_parse_block_rejects_null_args()
{
    // All three pointers are mandatory; a missing one fails closed rather than writing anywhere.
    uint8_t out[8];
    size_t olen = 0;
    const uint8_t blk[] = {0x41, SIMATIC_DLE, SIMATIC_ETX};
    TEST_ASSERT_FALSE(pc_3964r_parse_block(NULL, sizeof(blk), PROTO_FALSE, out, sizeof(out), &olen));
    TEST_ASSERT_FALSE(pc_3964r_parse_block(blk, sizeof(blk), PROTO_FALSE, NULL, sizeof(out), &olen));
    TEST_ASSERT_FALSE(pc_3964r_parse_block(blk, sizeof(blk), PROTO_FALSE, out, sizeof(out), NULL));
    TEST_ASSERT_TRUE(pc_3964r_parse_block(blk, sizeof(blk), PROTO_FALSE, out, sizeof(out), &olen));
    TEST_ASSERT_EQUAL_size_t(1, olen);
}

void test_parse_block_missing_bcc_and_doubled_dle_overflow()
{
    uint8_t out[8];
    size_t olen = 0;
    // R variant whose trailing BCC was truncated away: the terminator alone is not enough
    const uint8_t nobcc[] = {0x41, SIMATIC_DLE, SIMATIC_ETX};
    TEST_ASSERT_FALSE(pc_3964r_parse_block(nobcc, sizeof(nobcc), PROTO_TRUE, out, sizeof(out), &olen));
    // a doubled DLE that does not fit the caller's buffer is refused (fail-closed)
    const uint8_t doubled[] = {SIMATIC_DLE, SIMATIC_DLE, SIMATIC_DLE, SIMATIC_ETX};
    TEST_ASSERT_FALSE(pc_3964r_parse_block(doubled, sizeof(doubled), PROTO_FALSE, out, 0, &olen));
}

// ---- 3964R link state machine ---------------------------------------------

void test_sm_send_happy_path()
{
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_TRUE, cap_tx, cap_rx, NULL);
    const uint8_t msg[] = {0x31, 0x32};
    TEST_ASSERT_TRUE(pc_3964r_send(&c, msg, sizeof(msg), 0));
    // first tx is STX; awaiting connect
    TEST_ASSERT_EQUAL_size_t(1, g_tx_n);
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_STX, g_tx[0]);
    // partner acks connect with DLE -> we send the block
    pc_3964r_rx_byte(&c, SIMATIC_DLE, 1);
    TEST_ASSERT_GREATER_THAN_size_t(1, g_tx_n);            // block bytes emitted
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_ETX, g_tx[g_tx_n - 2]); // ... DLE ETX BCC tail
    // partner acks the block with DLE -> done
    pc_3964r_rx_byte(&c, SIMATIC_DLE, 2);
    TEST_ASSERT_TRUE(pc_3964r_idle(&c));
}

void test_sm_receive_path_delivers()
{
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_TRUE, cap_tx, cap_rx, NULL);
    // build a block the "partner" sends us
    const uint8_t payload[] = {0x53, 0x10, 0x54}; // 'S', DLE, 'T'
    uint8_t blk[32];
    size_t n = pc_3964r_build_block(blk, sizeof(blk), payload, sizeof(payload), PROTO_TRUE);
    // partner opens with STX -> we reply DLE (ready)
    pc_3964r_rx_byte(&c, SIMATIC_STX, 0);
    TEST_ASSERT_EQUAL_size_t(1, g_tx_n);
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_DLE, g_tx[0]);
    // feed the block body byte by byte
    for (size_t i = 0; i < n; i++)
    {
        pc_3964r_rx_byte(&c, blk[i], (uint32_t)(1 + i));
    }
    // we ack with a final DLE and deliver the un-stuffed payload
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_DLE, g_tx[g_tx_n - 1]);
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), g_rx_n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, g_rx, g_rx_n);
    TEST_ASSERT_TRUE(pc_3964r_idle(&c));
}

void test_sm_block_nak_retries()
{
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_FALSE, cap_tx, cap_rx, NULL);
    const uint8_t msg[] = {0x01};
    pc_3964r_send(&c, msg, sizeof(msg), 0);
    pc_3964r_rx_byte(&c, SIMATIC_DLE, 1); // connect
    size_t before = g_tx_n;
    pc_3964r_rx_byte(&c, SIMATIC_NAK, 2); // reject the block -> resend STX
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_STX, g_tx[g_tx_n - 1]);
    TEST_ASSERT_GREATER_THAN_size_t(before, g_tx_n);
    TEST_ASSERT_FALSE(pc_3964r_idle(&c)); // retrying
}

void test_sm_qvz_timeout_then_abort()
{
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_FALSE, cap_tx, cap_rx, NULL);
    const uint8_t msg[] = {0x01};
    pc_3964r_send(&c, msg, sizeof(msg), 0); // STX at t=0, deadline t=QVZ
    // never ack; tick past the deadline repeatedly -> connection retries then abort
    uint32_t t = PC_SIMATIC_QVZ_MS;
    for (int i = 0; i < SIMATIC_MAX_CONN_RETRY + 1; i++)
    {
        pc_3964r_tick(&c, t);
        t += PC_SIMATIC_QVZ_MS;
    }
    TEST_ASSERT_TRUE(pc_3964r_idle(&c)); // gave up after MAX retries
}

void test_sm_priority_arbitration()
{
    // Low-priority station, mid-send, sees a partner STX -> yields to receive.
    Simatic3964Ctx lo;
    pc_3964r_init(&lo, PROTO_FALSE, PROTO_TRUE, cap_tx, cap_rx, NULL);
    const uint8_t msg[] = {0x01};
    pc_3964r_send(&lo, msg, sizeof(msg), 0); // sent our STX, awaiting connect
    g_tx_n = 0;
    pc_3964r_rx_byte(&lo, SIMATIC_STX, 1);        // collision
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_DLE, g_tx[0]); // yielded: replied DLE (now receiving)

    // High-priority station in the same collision keeps its send (ignores the partner STX).
    Simatic3964Ctx hi;
    pc_3964r_init(&hi, PROTO_TRUE, PROTO_TRUE, cap_tx, cap_rx, NULL);
    pc_3964r_send(&hi, msg, sizeof(msg), 0);
    g_tx_n = 0;
    pc_3964r_rx_byte(&hi, SIMATIC_STX, 1);
    TEST_ASSERT_EQUAL_size_t(0, g_tx_n); // ignored; still awaiting its own connect DLE
    TEST_ASSERT_FALSE(pc_3964r_idle(&hi));
}

// Regression (found on the ESP32-P4 HW run): a request/response peer replies to a received block from
// inside the rx callback (e.g. an RK512 FETCH -> a reaction). deliver_or_nak must be IDLE before the
// callback so that reply-send is accepted, else the responder never answers.
static Simatic3964Ctx *g_reply_ctx = NULL;
static void rx_then_reply(void *user, const uint8_t *d, size_t n)
{
    (void)user;
    (void)d;
    (void)n;
    if (g_reply_ctx)
    {
        const uint8_t reply[] = {0xAA, 0xBB};
        pc_3964r_send(g_reply_ctx, reply, sizeof(reply), 100);
    }
}

void test_sm_reply_from_rx_callback()
{
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_TRUE, cap_tx, rx_then_reply, NULL);
    g_reply_ctx = &c;
    const uint8_t payload[] = {0x01};
    uint8_t blk[16];
    size_t n = pc_3964r_build_block(blk, sizeof(blk), payload, sizeof(payload), PROTO_TRUE);
    pc_3964r_rx_byte(&c, SIMATIC_STX, 0);
    for (size_t i = 0; i < n; i++)
    {
        pc_3964r_rx_byte(&c, blk[i], (uint32_t)(1 + i));
    }
    g_reply_ctx = NULL;
    // the block was acked (DLE) then the reply's STX was emitted -> the link is now sending the reply
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_STX, g_tx[g_tx_n - 1]);
    TEST_ASSERT_FALSE(pc_3964r_idle(&c));
}

void test_sm_send_rejects_when_busy_or_unframeable()
{
    // One job in flight at a time, and a payload that cannot be framed inside the block buffer is
    // rejected without disturbing the idle link.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_TRUE, cap_tx, cap_rx, NULL);
    const uint8_t msg[] = {0x01};
    TEST_ASSERT_TRUE(pc_3964r_send(&c, msg, sizeof(msg), 0));
    TEST_ASSERT_FALSE(pc_3964r_send(&c, msg, sizeof(msg), 0));

    Simatic3964Ctx d;
    pc_3964r_init(&d, PROTO_TRUE, PROTO_TRUE, cap_tx, cap_rx, NULL);
    static uint8_t big[PC_SIMATIC_BLOCK_MAX];
    memset(big, 0x41, sizeof(big)); // no room left for DLE ETX BCC
    TEST_ASSERT_FALSE(pc_3964r_send(&d, big, sizeof(big), 0));
    TEST_ASSERT_TRUE(pc_3964r_idle(&d));
}

void test_sm_null_callbacks_are_safe()
{
    // tx/rx are optional: the link still runs the handshake and accepts a block, it just has
    // nowhere to put the bytes.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_TRUE, NULL, NULL, NULL);
    const uint8_t payload[] = {0x41};
    uint8_t blk[16];
    size_t n = pc_3964r_build_block(blk, sizeof(blk), payload, sizeof(payload), PROTO_TRUE);
    pc_3964r_rx_byte(&c, SIMATIC_STX, 0);
    for (size_t i = 0; i < n; i++)
    {
        pc_3964r_rx_byte(&c, blk[i], (uint32_t)(1 + i));
    }
    TEST_ASSERT_EQUAL_size_t(0, g_tx_n); // no sink -> nothing emitted
    TEST_ASSERT_EQUAL_size_t(0, g_rx_n); // no delivery callback
    TEST_ASSERT_TRUE(pc_3964r_idle(&c)); // the block was still consumed and the link freed
}

void test_sm_receive_bad_bcc_naks()
{
    // A check-invalid block is NAKed and never delivered.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_TRUE, cap_tx, cap_rx, NULL);
    const uint8_t payload[] = {0x41, 0x42};
    uint8_t blk[16];
    size_t n = pc_3964r_build_block(blk, sizeof(blk), payload, sizeof(payload), PROTO_TRUE);
    blk[n - 1] ^= 0xFF; // corrupt the BCC
    pc_3964r_rx_byte(&c, SIMATIC_STX, 0);
    for (size_t i = 0; i < n; i++)
    {
        pc_3964r_rx_byte(&c, blk[i], (uint32_t)(1 + i));
    }
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_NAK, g_tx[g_tx_n - 1]);
    TEST_ASSERT_EQUAL_size_t(0, g_rx_n);
    TEST_ASSERT_TRUE(pc_3964r_idle(&c));
}

void test_sm_receive_no_bcc_variant_delivers()
{
    // Plain 3964 (no BCC): DLE ETX finalizes the block immediately, no trailing check byte.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_FALSE, cap_tx, cap_rx, NULL);
    const uint8_t payload[] = {0x41, SIMATIC_DLE};
    uint8_t blk[16];
    size_t n = pc_3964r_build_block(blk, sizeof(blk), payload, sizeof(payload), PROTO_FALSE);
    pc_3964r_rx_byte(&c, SIMATIC_STX, 0);
    for (size_t i = 0; i < n; i++)
    {
        pc_3964r_rx_byte(&c, blk[i], (uint32_t)(1 + i));
    }
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_DLE, g_tx[g_tx_n - 1]); // acked
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), g_rx_n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, g_rx, g_rx_n);
    TEST_ASSERT_TRUE(pc_3964r_idle(&c));
}

void test_sm_receive_illegal_control_naks()
{
    // DLE followed by something that is neither DLE nor ETX is a framing error mid-collect.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_TRUE, cap_tx, cap_rx, NULL);
    pc_3964r_rx_byte(&c, SIMATIC_STX, 0);
    pc_3964r_rx_byte(&c, SIMATIC_DLE, 1);
    pc_3964r_rx_byte(&c, 0x55, 2);
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_NAK, g_tx[g_tx_n - 1]);
    TEST_ASSERT_EQUAL_size_t(0, g_rx_n);
    TEST_ASSERT_TRUE(pc_3964r_idle(&c));
}

void test_sm_receive_overflow_naks()
{
    // A partner that never terminates the block fills rxbuf; the next byte is rejected.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_TRUE, cap_tx, cap_rx, NULL);
    pc_3964r_rx_byte(&c, SIMATIC_STX, 0);
    for (size_t i = 0; i < PC_SIMATIC_BLOCK_MAX; i++)
    {
        pc_3964r_rx_byte(&c, 0x41, (uint32_t)(1 + i));
    }
    TEST_ASSERT_FALSE(pc_3964r_idle(&c)); // exactly full, still collecting
    pc_3964r_rx_byte(&c, 0x41, 1000);     // one byte too many
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_NAK, g_tx[g_tx_n - 1]);
    TEST_ASSERT_TRUE(pc_3964r_idle(&c));
}

void test_sm_idle_ignores_non_stx()
{
    // Line noise while idle must not open a receive.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_FALSE, cap_tx, cap_rx, NULL);
    pc_3964r_rx_byte(&c, 0x55, 0);
    TEST_ASSERT_EQUAL_size_t(0, g_tx_n);
    TEST_ASSERT_TRUE(pc_3964r_idle(&c));
}

void test_sm_conn_nak_retries_then_gives_up()
{
    // A partner that NAKs the connect gets MAX_CONN_RETRY fresh STXs, then the job is abandoned.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_FALSE, cap_tx, cap_rx, NULL);
    const uint8_t msg[] = {0x01};
    pc_3964r_send(&c, msg, sizeof(msg), 0);
    for (int i = 1; i < SIMATIC_MAX_CONN_RETRY; i++)
    {
        pc_3964r_rx_byte(&c, SIMATIC_NAK, (uint32_t)i);
        TEST_ASSERT_EQUAL_HEX8(SIMATIC_STX, g_tx[g_tx_n - 1]);
        TEST_ASSERT_FALSE(pc_3964r_idle(&c));
    }
    pc_3964r_rx_byte(&c, SIMATIC_NAK, 99);
    TEST_ASSERT_TRUE(pc_3964r_idle(&c));
}

void test_sm_await_conn_ignores_other_bytes()
{
    // Neither DLE, STX nor NAK: nothing happens, we keep waiting for the connect.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_FALSE, cap_tx, cap_rx, NULL);
    const uint8_t msg[] = {0x01};
    pc_3964r_send(&c, msg, sizeof(msg), 0);
    g_tx_n = 0;
    pc_3964r_rx_byte(&c, 0x55, 1);
    TEST_ASSERT_EQUAL_size_t(0, g_tx_n);
    TEST_ASSERT_FALSE(pc_3964r_idle(&c));
}

void test_sm_await_end_ignores_noise_then_gives_up()
{
    // In TX_AWAIT_END only DLE (done) and NAK (repeat) mean anything; MAX_BLOCK_RETRY rejections
    // abandon the job.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_FALSE, cap_tx, cap_rx, NULL);
    const uint8_t msg[] = {0x01};
    pc_3964r_send(&c, msg, sizeof(msg), 0);
    pc_3964r_rx_byte(&c, SIMATIC_DLE, 1); // connect -> block sent
    g_tx_n = 0;
    pc_3964r_rx_byte(&c, 0x55, 2);
    TEST_ASSERT_EQUAL_size_t(0, g_tx_n);
    TEST_ASSERT_FALSE(pc_3964r_idle(&c));
    for (int i = 0; i < SIMATIC_MAX_BLOCK_RETRY - 1; i++)
    {
        pc_3964r_rx_byte(&c, SIMATIC_NAK, 3); // reject -> repeat from STX
        TEST_ASSERT_FALSE(pc_3964r_idle(&c));
        pc_3964r_rx_byte(&c, SIMATIC_DLE, 4); // partner connects again
    }
    pc_3964r_rx_byte(&c, SIMATIC_NAK, 5);
    TEST_ASSERT_TRUE(pc_3964r_idle(&c));
}

void test_sm_tick_before_deadline_is_a_noop()
{
    // The QVZ timer must not fire early.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_FALSE, cap_tx, cap_rx, NULL);
    const uint8_t msg[] = {0x01};
    pc_3964r_send(&c, msg, sizeof(msg), 0);
    g_tx_n = 0;
    pc_3964r_tick(&c, PC_SIMATIC_QVZ_MS - 1);
    TEST_ASSERT_EQUAL_size_t(0, g_tx_n);
    TEST_ASSERT_FALSE(pc_3964r_idle(&c));
}

void test_sm_tick_block_timeout_retries_then_gives_up()
{
    // No end DLE within QVZ repeats the block from STX, up to MAX_BLOCK_RETRY times.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_FALSE, cap_tx, cap_rx, NULL);
    const uint8_t msg[] = {0x01};
    pc_3964r_send(&c, msg, sizeof(msg), 0);
    uint32_t t = 0;
    for (int i = 0; i < SIMATIC_MAX_BLOCK_RETRY - 1; i++)
    {
        pc_3964r_rx_byte(&c, SIMATIC_DLE, t); // connect -> awaiting the end DLE
        t += PC_SIMATIC_QVZ_MS;
        pc_3964r_tick(&c, t);
        TEST_ASSERT_EQUAL_HEX8(SIMATIC_STX, g_tx[g_tx_n - 1]);
        TEST_ASSERT_FALSE(pc_3964r_idle(&c));
    }
    pc_3964r_rx_byte(&c, SIMATIC_DLE, t);
    t += PC_SIMATIC_QVZ_MS;
    pc_3964r_tick(&c, t);
    TEST_ASSERT_TRUE(pc_3964r_idle(&c));
}

void test_sm_tick_zvz_aborts_receive()
{
    // A partner that stops mid-block trips the ZVZ inter-character timeout -> NAK, link freed.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_TRUE, cap_tx, cap_rx, NULL);
    pc_3964r_rx_byte(&c, SIMATIC_STX, 0);
    pc_3964r_rx_byte(&c, 0x41, 1);
    pc_3964r_tick(&c, 1 + PC_SIMATIC_ZVZ_MS);
    TEST_ASSERT_EQUAL_HEX8(SIMATIC_NAK, g_tx[g_tx_n - 1]);
    TEST_ASSERT_TRUE(pc_3964r_idle(&c));
}

void test_sm_unknown_state_is_inert()
{
    // Defensive: a state byte outside the four defined states (a corrupted context) makes both
    // dispatchers fall through - no transition, no bytes on the wire.
    Simatic3964Ctx c;
    pc_3964r_init(&c, PROTO_TRUE, PROTO_TRUE, cap_tx, cap_rx, NULL);
    c.state = (Simatic3964State)0x7F;
    c.deadline_ms = 0;
    pc_3964r_rx_byte(&c, SIMATIC_STX, 10);
    pc_3964r_tick(&c, 10); // deadline already past, so the switch is reached
    TEST_ASSERT_EQUAL_size_t(0, g_tx_n);
    TEST_ASSERT_FALSE(pc_3964r_idle(&c));
    TEST_ASSERT_EQUAL_UINT8(0x7F, (uint8_t)c.state);
}

// ---- RK512 telegrams ------------------------------------------------------

void test_rk512_build_send_field_order()
{
    const uint16_t words[] = {0x1234, 0xABCD};
    uint8_t buf[32];
    size_t n = pc_rk512_build_send(buf, sizeof(buf), RK512_AREA_DB, 5, 0x0010, words, 2);
    TEST_ASSERT_EQUAL_size_t(8 + 4, n);
    // [SEND, coord=0, area=DB, dbnr=5, addr BE, count BE, words BE]
    const uint8_t want[] = {0x00, 0x00, (uint8_t)RK512_AREA_DB, 0x05, 0x00, 0x10, 0x00, 0x02, 0x12, 0x34, 0xAB, 0xCD};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, n);
}

void test_rk512_build_fetch_and_parse()
{
    uint8_t buf[16];
    size_t n = pc_rk512_build_fetch(buf, sizeof(buf), RK512_AREA_MB, 0, 0x0100, 4);
    TEST_ASSERT_EQUAL_size_t(8, n);
    Rk512Header h;
    TEST_ASSERT_TRUE(pc_rk512_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL(RK512_CMD_FETCH, h.cmd);
    TEST_ASSERT_EQUAL(RK512_AREA_MB, h.area);
    TEST_ASSERT_EQUAL_UINT16(0x0100, h.addr);
    TEST_ASSERT_EQUAL_UINT16(4, h.count);
}

void test_rk512_reaction_round_trip()
{
    uint8_t buf[8];
    size_t n = pc_rk512_build_reaction(buf, sizeof(buf), 0x0000);
    uint16_t status = 0xFFFF;
    const uint8_t *data = (const uint8_t *)1;
    size_t dlen = 99;
    TEST_ASSERT_TRUE(pc_rk512_parse_reaction(buf, n, &status, &data, &dlen));
    TEST_ASSERT_EQUAL_UINT16(0, status);
    TEST_ASSERT_NULL(data);
    TEST_ASSERT_EQUAL_size_t(0, dlen);
    // a non-zero error status
    pc_rk512_build_reaction(buf, sizeof(buf), 0x8001);
    pc_rk512_parse_reaction(buf, 3, &status, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT16(0x8001, status);
}

void test_rk512_parse_rejects()
{
    Rk512Header h;
    const uint8_t shortbuf[] = {0x00, 0x00};
    TEST_ASSERT_FALSE(pc_rk512_parse_header(shortbuf, sizeof(shortbuf), &h)); // too short
    const uint8_t badarea[] = {0x00, 0x00, 0xEE, 0x00, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_FALSE(pc_rk512_parse_header(badarea, sizeof(badarea), &h)); // invalid area
    // overflow guards
    uint8_t tiny[4];
    const uint16_t w[] = {1};
    TEST_ASSERT_EQUAL_size_t(0, pc_rk512_build_send(tiny, sizeof(tiny), RK512_AREA_DB, 0, 0, w, 1));
}

void test_rk512_build_guards()
{
    // Every builder fails closed on a null destination or a destination too small for its telegram.
    uint8_t buf[32];
    const uint16_t w[] = {0x0001};
    TEST_ASSERT_EQUAL_size_t(0, pc_rk512_build_send(NULL, sizeof(buf), RK512_AREA_DB, 0, 0, w, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_rk512_build_send(buf, sizeof(buf), RK512_AREA_DB, 0, 0, NULL, 1));
    // a null word array with count 0 is the legal header-only SEND
    TEST_ASSERT_EQUAL_size_t(8, pc_rk512_build_send(buf, sizeof(buf), RK512_AREA_DB, 0, 0, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_rk512_build_fetch(NULL, sizeof(buf), RK512_AREA_DB, 0, 0, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_rk512_build_fetch(buf, 7, RK512_AREA_DB, 0, 0, 1)); // < header
    TEST_ASSERT_EQUAL_size_t(0, pc_rk512_build_reaction(NULL, sizeof(buf), 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_rk512_build_reaction(buf, 2, 0)); // < 3
}

void test_rk512_parse_header_guards()
{
    // Null arguments, an area code under the valid range, and a REACTION command byte are all
    // rejected as request headers.
    Rk512Header h;
    const uint8_t ok[] = {0x00, 0x00, (uint8_t)RK512_AREA_DB, 0x01, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_FALSE(pc_rk512_parse_header(NULL, sizeof(ok), &h));
    TEST_ASSERT_FALSE(pc_rk512_parse_header(ok, sizeof(ok), NULL));
    uint8_t lowarea[8];
    memcpy(lowarea, ok, sizeof(ok));
    lowarea[2] = 0x00; // below Rk512Area::DB
    TEST_ASSERT_FALSE(pc_rk512_parse_header(lowarea, sizeof(lowarea), &h));
    uint8_t reaction[8];
    memcpy(reaction, ok, sizeof(ok));
    reaction[0] = (uint8_t)RK512_CMD_REACTION;
    TEST_ASSERT_FALSE(pc_rk512_parse_header(reaction, sizeof(reaction), &h));
    TEST_ASSERT_TRUE(pc_rk512_parse_header(ok, sizeof(ok), &h)); // the control case still parses
    TEST_ASSERT_EQUAL(RK512_CMD_SEND, h.cmd);
    TEST_ASSERT_EQUAL_UINT8(1, h.dbnr);
}

void test_rk512_parse_reaction_guards_and_data()
{
    // Null arguments / a short buffer / a non-REACTION command byte are refused; a FETCH response
    // hands back a pointer to the returned words inside the telegram.
    uint8_t buf[16];
    uint16_t status = 0;
    pc_rk512_build_reaction(buf, sizeof(buf), 0x0102);
    TEST_ASSERT_FALSE(pc_rk512_parse_reaction(NULL, 3, &status, NULL, NULL));
    TEST_ASSERT_FALSE(pc_rk512_parse_reaction(buf, 3, NULL, NULL, NULL));
    TEST_ASSERT_FALSE(pc_rk512_parse_reaction(buf, 2, &status, NULL, NULL));
    const uint8_t notreaction[3] = {(uint8_t)RK512_CMD_FETCH, 0x00, 0x00};
    TEST_ASSERT_FALSE(pc_rk512_parse_reaction(notreaction, sizeof(notreaction), &status, NULL, NULL));
    buf[3] = 0x12; // two data words appended by a FETCH responder
    buf[4] = 0x34;
    const uint8_t *data = NULL;
    size_t dlen = 0;
    TEST_ASSERT_TRUE(pc_rk512_parse_reaction(buf, 5, &status, &data, &dlen));
    TEST_ASSERT_EQUAL_UINT16(0x0102, status);
    TEST_ASSERT_EQUAL_PTR(buf + 3, data);
    TEST_ASSERT_EQUAL_size_t(2, dlen);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_bcc_is_xor);
    RUN_TEST(test_build_block_stuffs_dle_and_terminates);
    RUN_TEST(test_block_round_trip_with_embedded_dle);
    RUN_TEST(test_block_round_trip_no_bcc);
    RUN_TEST(test_parse_rejects_bad);
    RUN_TEST(test_build_block_rejects_bad_args);
    RUN_TEST(test_build_block_overflow_at_each_stage);
    RUN_TEST(test_parse_block_rejects_null_args);
    RUN_TEST(test_parse_block_missing_bcc_and_doubled_dle_overflow);
    RUN_TEST(test_sm_send_happy_path);
    RUN_TEST(test_sm_receive_path_delivers);
    RUN_TEST(test_sm_block_nak_retries);
    RUN_TEST(test_sm_qvz_timeout_then_abort);
    RUN_TEST(test_sm_priority_arbitration);
    RUN_TEST(test_sm_reply_from_rx_callback);
    RUN_TEST(test_sm_send_rejects_when_busy_or_unframeable);
    RUN_TEST(test_sm_null_callbacks_are_safe);
    RUN_TEST(test_sm_receive_bad_bcc_naks);
    RUN_TEST(test_sm_receive_no_bcc_variant_delivers);
    RUN_TEST(test_sm_receive_illegal_control_naks);
    RUN_TEST(test_sm_receive_overflow_naks);
    RUN_TEST(test_sm_idle_ignores_non_stx);
    RUN_TEST(test_sm_conn_nak_retries_then_gives_up);
    RUN_TEST(test_sm_await_conn_ignores_other_bytes);
    RUN_TEST(test_sm_await_end_ignores_noise_then_gives_up);
    RUN_TEST(test_sm_tick_before_deadline_is_a_noop);
    RUN_TEST(test_sm_tick_block_timeout_retries_then_gives_up);
    RUN_TEST(test_sm_tick_zvz_aborts_receive);
    RUN_TEST(test_sm_unknown_state_is_inert);
    RUN_TEST(test_rk512_build_send_field_order);
    RUN_TEST(test_rk512_build_fetch_and_parse);
    RUN_TEST(test_rk512_reaction_round_trip);
    RUN_TEST(test_rk512_parse_rejects);
    RUN_TEST(test_rk512_build_guards);
    RUN_TEST(test_rk512_parse_header_guards);
    RUN_TEST(test_rk512_parse_reaction_guards_and_data);
    return UNITY_END();
}
