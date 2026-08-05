// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit and stress tests for SHA-1, Base64, and the WebSocket frame parser.
//
// Sections:
//   SHA-1       -- known-answer tests against RFC 3174 / RFC 6455 §B vectors
//   BASE64      -- encode and decode round-trips and known-answer tests
//   WS POOL     -- ws_init / ws_alloc / ws_find / ws_free invariants
//   WS PARSER   -- per-state-machine path through ws_parse()
//   STRESS      -- sustained-load and boundary-value coverage

#include "crypto/hash/sha1.h"
#include "network_drivers/presentation/codec/base64/base64.h"
#include "network_drivers/presentation/http/websocket/websocket.h"
#include <string.h>

#include <unity.h>

#if PC_ENABLE_WS_DEFLATE
#include "mmgr/plaintext.h"                                     // arena-exhaustion drive for the fail-closed path
#include "network_drivers/presentation/codec/deflate/deflate.h" // DEFLATE_SCRATCH_SIZE for the starved-send path
#include "network_drivers/presentation/codec/inflate/inflate.h"
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Push raw bytes into slot's ring buffer (simulates lwIP recv callback)
static void push_bytes(uint8_t slot, const uint8_t *data, size_t len)
{
    TcpConn *s = &conn_pool[slot];
    for (size_t i = 0; i < len; i++)
    {
        size_t next = (s->rx_head + 1) % RX_BUF_SIZE;
        if (next == s->rx_tail)
        {
            break; // ring full
        }
        s->rx_buffer[s->rx_head] = data[i];
        s->rx_head = next;
    }
}

// Build a WebSocket frame into dst.
// Uses mask key {0,0,0,0} so the stored payload equals the unmasked input.
// Returns the number of bytes written.
static size_t build_frame(uint8_t *dst, WsOpcode opcode, proto_bool fin, const uint8_t *payload, uint16_t payload_len,
                          proto_bool masked)
{
    size_t pos = 0;
    dst[pos++] = (fin ? 0x80u : 0x00u) | (uint8_t)opcode;

    uint8_t mask_bit = masked ? 0x80u : 0x00u;
    if (payload_len <= 125)
    {
        dst[pos++] = mask_bit | (uint8_t)payload_len;
    }
    else
    {
        dst[pos++] = mask_bit | 126u;
        dst[pos++] = (uint8_t)(payload_len >> 8);
        dst[pos++] = (uint8_t)(payload_len);
    }

    if (masked)
    {
        dst[pos++] = 0;
        dst[pos++] = 0;
        dst[pos++] = 0;
        dst[pos++] = 0;
    }

    if (payload && payload_len > 0)
    {
        memcpy(dst + pos, payload, payload_len);
        pos += payload_len;
    }
    return pos;
}

void setUp()
{
    ws_init();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].pcb = pc_net_host_pcb();
    }
}

void tearDown()
{
}

// ====================================================================
// SHA-1 TESTS
// ====================================================================

// RFC 3174 test vector: empty string
void test_sha1_empty_string()
{
    uint8_t digest[PC_SHA1_DIGEST_LEN];
    pc_sha1((const uint8_t *)"", 0, digest);
    const uint8_t expected[PC_SHA1_DIGEST_LEN] = {0xDA, 0x39, 0xA3, 0xEE, 0x5E, 0x6B, 0x4B, 0x0D, 0x32, 0x55,
                                                  0xBF, 0xEF, 0x95, 0x60, 0x18, 0x90, 0xAF, 0xD8, 0x07, 0x09};
    TEST_ASSERT_EQUAL_MEMORY(expected, digest, PC_SHA1_DIGEST_LEN);
}

// RFC 3174 test vector: "abc"
void test_sha1_abc()
{
    uint8_t digest[PC_SHA1_DIGEST_LEN];
    pc_sha1((const uint8_t *)"abc", 3, digest);
    const uint8_t expected[PC_SHA1_DIGEST_LEN] = {0xA9, 0x99, 0x3E, 0x36, 0x47, 0x06, 0x81, 0x6A, 0xBA, 0x3E,
                                                  0x25, 0x71, 0x78, 0x50, 0xC2, 0x6C, 0x9C, 0xD0, 0xD8, 0x9D};
    TEST_ASSERT_EQUAL_MEMORY(expected, digest, PC_SHA1_DIGEST_LEN);
}

// RFC 6455 §B handshake key: SHA-1 of client key + magic GUID
void test_sha1_rfc6455_handshake_key()
{
    // Client sends: Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
    // Server concatenates with magic: 258EAFA5-E914-47DA-95CA-C5AB0DC85B11
    const char *input = "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[PC_SHA1_DIGEST_LEN];
    pc_sha1((const uint8_t *)input, strlen(input), digest);

    const uint8_t expected[PC_SHA1_DIGEST_LEN] = {0xB3, 0x7A, 0x4F, 0x2C, 0xC0, 0x62, 0x4F, 0x16, 0x90, 0xF6,
                                                  0x46, 0x06, 0xCF, 0x38, 0x59, 0x45, 0xB2, 0xBE, 0xC4, 0xEA};
    TEST_ASSERT_EQUAL_MEMORY(expected, digest, PC_SHA1_DIGEST_LEN);
}

// Digest changes when input changes (basic independence)
void test_sha1_different_inputs_different_digests()
{
    uint8_t d1[PC_SHA1_DIGEST_LEN], d2[PC_SHA1_DIGEST_LEN];
    pc_sha1((const uint8_t *)"abc", 3, d1);
    pc_sha1((const uint8_t *)"abd", 3, d2);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(d1, d2, PC_SHA1_DIGEST_LEN));
}

// ====================================================================
// BASE64 TESTS
// ====================================================================

// Encode single byte
void test_base64_encode_one_byte()
{
    const uint8_t src[] = {0x4D}; // 'M'
    char out[8] = {0};
    pc_base64_encode(src, 1, out);
    TEST_ASSERT_EQUAL_STRING("TQ==", out);
}

// Encode two bytes
void test_base64_encode_two_bytes()
{
    const uint8_t src[] = {0x4D, 0x61}; // "Ma"
    char out[8] = {0};
    pc_base64_encode(src, 2, out);
    TEST_ASSERT_EQUAL_STRING("TWE=", out);
}

// Encode three bytes (no padding needed)
void test_base64_encode_three_bytes()
{
    const uint8_t src[] = {0x4D, 0x61, 0x6E}; // "Man"
    char out[8] = {0};
    pc_base64_encode(src, 3, out);
    TEST_ASSERT_EQUAL_STRING("TWFu", out);
}

// Encode the RFC 6455 §B SHA-1 digest → known accept header value
void test_base64_encode_ws_accept_key()
{
    const uint8_t digest[PC_SHA1_DIGEST_LEN] = {0xB3, 0x7A, 0x4F, 0x2C, 0xC0, 0x62, 0x4F, 0x16, 0x90, 0xF6,
                                                0x46, 0x06, 0xCF, 0x38, 0x59, 0x45, 0xB2, 0xBE, 0xC4, 0xEA};
    char out[32] = {0};
    pc_base64_encode(digest, PC_SHA1_DIGEST_LEN, out);
    TEST_ASSERT_EQUAL_STRING("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", out);
}

// Decode single-byte padded string
void test_base64_decode_one_byte()
{
    uint8_t dst[4] = {0};
    size_t n = pc_base64_decode("TQ==", dst, sizeof(dst));
    TEST_ASSERT_EQUAL(1, (int)n);
    TEST_ASSERT_EQUAL(0x4D, (int)dst[0]);
}

// Decode two-byte padded string
void test_base64_decode_two_bytes()
{
    uint8_t dst[4] = {0};
    size_t n = pc_base64_decode("TWE=", dst, sizeof(dst));
    TEST_ASSERT_EQUAL(2, (int)n);
    TEST_ASSERT_EQUAL(0x4D, (int)dst[0]);
    TEST_ASSERT_EQUAL(0x61, (int)dst[1]);
}

// Decode three-byte unpadded string
void test_base64_decode_three_bytes()
{
    uint8_t dst[4] = {0};
    size_t n = pc_base64_decode("TWFu", dst, sizeof(dst));
    TEST_ASSERT_EQUAL(3, (int)n);
    TEST_ASSERT_EQUAL(0x4D, (int)dst[0]);
    TEST_ASSERT_EQUAL(0x61, (int)dst[1]);
    TEST_ASSERT_EQUAL(0x6E, (int)dst[2]);
}

// Decode the RFC 6455 §B accept key back to the original digest bytes
void test_base64_decode_ws_accept_key()
{
    uint8_t dst[PC_SHA1_DIGEST_LEN + 4] = {0};
    size_t n = pc_base64_decode("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", dst, sizeof(dst));
    TEST_ASSERT_EQUAL(PC_SHA1_DIGEST_LEN, (int)n);
    const uint8_t expected[PC_SHA1_DIGEST_LEN] = {0xB3, 0x7A, 0x4F, 0x2C, 0xC0, 0x62, 0x4F, 0x16, 0x90, 0xF6,
                                                  0x46, 0x06, 0xCF, 0x38, 0x59, 0x45, 0xB2, 0xBE, 0xC4, 0xEA};
    TEST_ASSERT_EQUAL_MEMORY(expected, dst, PC_SHA1_DIGEST_LEN);
}

// Encode then decode must return identical bytes
void test_base64_round_trip()
{
    const uint8_t src[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98};
    char encoded[24] = {0};
    uint8_t decoded[16] = {0};
    pc_base64_encode(src, sizeof(src), encoded);
    size_t n = pc_base64_decode(encoded, decoded, sizeof(decoded));
    TEST_ASSERT_EQUAL((int)sizeof(src), (int)n);
    TEST_ASSERT_EQUAL_MEMORY(src, decoded, sizeof(src));
}

// '=' padding is only valid as 1-2 trailing chars of the final quad; anything
// else must be rejected (return 0) rather than decoded as a zero sextet.
void test_base64_decode_rejects_misplaced_padding()
{
    uint8_t dst[8] = {0};
    TEST_ASSERT_EQUAL(0, (int)pc_base64_decode("A=BC", dst, sizeof(dst)));     // pad in pos 2
    TEST_ASSERT_EQUAL(0, (int)pc_base64_decode("AB=C", dst, sizeof(dst)));     // single pad in pos 3
    TEST_ASSERT_EQUAL(0, (int)pc_base64_decode("=BCD", dst, sizeof(dst)));     // pad in pos 1
    TEST_ASSERT_EQUAL(0, (int)pc_base64_decode("TWE=TWFu", dst, sizeof(dst))); // padding before end
    TEST_ASSERT_EQUAL(0, (int)pc_base64_decode("TWF", dst, sizeof(dst)));      // not a multiple of 4
    // Well-formed padded inputs still decode.
    TEST_ASSERT_EQUAL(1, (int)pc_base64_decode("TQ==", dst, sizeof(dst)));
    TEST_ASSERT_EQUAL(2, (int)pc_base64_decode("TWE=", dst, sizeof(dst)));
}

// An input that decodes to more than dst_cap bytes must fail (return 0) rather
// than overrun the destination buffer.
void test_base64_decode_respects_capacity()
{
    // "TWFu" decodes to 3 bytes ("Man"); a 2-byte buffer is too small.
    uint8_t dst[2] = {0};
    size_t n = pc_base64_decode("TWFu", dst, sizeof(dst));
    TEST_ASSERT_EQUAL(0, (int)n);
    // Exact capacity (3) succeeds.
    uint8_t dst3[3] = {0};
    TEST_ASSERT_EQUAL(3, (int)pc_base64_decode("TWFu", dst3, sizeof(dst3)));
}

// ====================================================================
// WS POOL TESTS
// ====================================================================

void test_ws_pool_size()
{
    TEST_ASSERT_EQUAL(2, MAX_WS_CONNS); // default
}

void test_ws_ids_match_indices_after_init()
{
    for (int i = 0; i < MAX_WS_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(i, (int)ws_pool[i].ws_id);
    }
}

void test_ws_all_inactive_after_init()
{
    for (int i = 0; i < MAX_WS_CONNS; i++)
    {
        TEST_ASSERT_FALSE(ws_pool[i].active);
    }
}

void test_ws_alloc_returns_non_null()
{
    TEST_ASSERT_NOT_NULL(ws_alloc(0));
}

void test_ws_alloc_sets_active()
{
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_TRUE(ws->active);
}

void test_ws_alloc_sets_slot_id()
{
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_EQUAL(0, (int)ws->slot_id);
}

void test_ws_alloc_sets_parse_state_header1()
{
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_EQUAL(WS_HEADER1, ws->parse_state);
}

void test_ws_alloc_pool_full_returns_null()
{
    TEST_ASSERT_NOT_NULL(ws_alloc(0));
    TEST_ASSERT_NOT_NULL(ws_alloc(1));
    TEST_ASSERT_NULL(ws_alloc(2)); // MAX_WS_CONNS = 2
}

// ws_active(): in-range + allocated -> true; unallocated and out-of-range -> false.
void test_ws_active_reflects_pool_state()
{
    TEST_ASSERT_FALSE(ws_active(0)); // not yet allocated
    ws_alloc(0);
    TEST_ASSERT_TRUE(ws_active(0));
    TEST_ASSERT_FALSE(ws_active((uint8_t)MAX_WS_CONNS)); // out of range
}

// ws_payload(): returns the slot's buf pointer when active, NULL otherwise.
void test_ws_payload_returns_buf_or_null()
{
    TEST_ASSERT_NULL(ws_payload(0)); // not yet allocated
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_EQUAL_PTR((const char *)ws->buf, ws_payload(0));
    TEST_ASSERT_NULL(ws_payload((uint8_t)MAX_WS_CONNS)); // out of range
}

void test_ws_find_returns_correct_conn()
{
    WsConn *allocated = ws_alloc(0);
    WsConn *found = ws_find(0);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_PTR(allocated, found);
}

void test_ws_find_returns_null_when_empty()
{
    TEST_ASSERT_NULL(ws_find(0));
}

void test_ws_find_returns_null_for_different_slot()
{
    ws_alloc(0);
    TEST_ASSERT_NULL(ws_find(1)); // slot 1 not allocated
}

void test_ws_find_after_both_slots_allocated()
{
    ws_alloc(0);
    ws_alloc(1);
    TEST_ASSERT_NOT_NULL(ws_find(0));
    TEST_ASSERT_NOT_NULL(ws_find(1));
}

void test_ws_free_deactivates_slot()
{
    ws_alloc(0);
    ws_free(0);
    TEST_ASSERT_FALSE(ws_pool[0].active);
}

void test_ws_free_restores_ws_id()
{
    ws_alloc(0);
    ws_free(0);
    TEST_ASSERT_EQUAL(0, (int)ws_pool[0].ws_id);
}

void test_ws_free_makes_slot_findable_as_null()
{
    ws_alloc(0);
    ws_free(0);
    TEST_ASSERT_NULL(ws_find(0));
}

void test_ws_free_nop_on_unallocated()
{
    ws_free(2); // slot 2 was never allocated
    TEST_ASSERT_FALSE(ws_pool[0].active);
    TEST_ASSERT_FALSE(ws_pool[1].active);
    TEST_PASS();
}

// With two active slots, freeing the second must skip past the first (active, but
// wrong slot_id) rather than stopping on it.
void test_ws_free_skips_active_slot_with_different_id()
{
    ws_alloc(0);
    ws_alloc(1);
    ws_free(1);
    TEST_ASSERT_TRUE(ws_pool[0].active); // untouched
    TEST_ASSERT_NULL(ws_find(1));
}

void test_ws_alloc_after_free_succeeds()
{
    ws_alloc(0);
    ws_free(0);
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    TEST_ASSERT_TRUE(ws->active);
    TEST_ASSERT_EQUAL(0, (int)ws->slot_id);
}

// ====================================================================
// WS FRAME PARSER TESTS
// ====================================================================

void test_ws_parse_text_frame_sets_ready()
{
    WsConn *ws = ws_alloc(0);
    const uint8_t payload[] = {'H', 'i'};
    uint8_t frame[12];
    size_t flen = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, payload, 2, PROTO_TRUE);
    push_bytes(0, frame, flen);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
}

void test_ws_parse_payload_stored_correctly()
{
    WsConn *ws = ws_alloc(0);
    const char *text = "Hello";
    uint8_t frame[16];
    size_t flen = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)text, (uint16_t)strlen(text), PROTO_TRUE);
    push_bytes(0, frame, flen);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL(5, (int)ws->payload_len);
    TEST_ASSERT_EQUAL_STRING("Hello", (const char *)ws->buf);
}

void test_ws_parse_binary_frame_sets_ready()
{
    WsConn *ws = ws_alloc(0);
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t frame[16];
    size_t flen = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, payload, 3, PROTO_TRUE);
    push_bytes(0, frame, flen);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL(WS_OP_BINARY, ws->opcode);
    TEST_ASSERT_EQUAL(3, (int)ws->payload_len);
    TEST_ASSERT_EQUAL(0x01, (int)ws->buf[0]);
    TEST_ASSERT_EQUAL(0x02, (int)ws->buf[1]);
    TEST_ASSERT_EQUAL(0x03, (int)ws->buf[2]);
}

void test_ws_parse_zero_length_unmasked_frame()
{
    WsConn *ws = ws_alloc(0);
    // Unmasked zero-length frame: 0x81 0x00.
    // RFC 6455 §5.1: client-to-server frames MUST be masked; the server must
    // fail the connection on any unmasked frame.
    uint8_t frame[2] = {0x81, 0x00};
    push_bytes(0, frame, 2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_zero_length_masked_frame()
{
    WsConn *ws = ws_alloc(0);
    // Masked zero-length text frame: FIN|TEXT, MASK|0, 4-byte mask key.
    // The 4 mask bytes must be consumed before the frame is complete.
    uint8_t frame[6] = {0x81, 0x80, 0x00, 0x00, 0x00, 0x00};
    push_bytes(0, frame, 6);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL(0, (int)ws->payload_len);
}

void test_ws_reject_unmasked_data_frame()
{
    WsConn *ws = ws_alloc(0);
    // FIN|TEXT, unmasked, length 3 - RFC 6455 §5.1 requires masking.
    uint8_t frame[5] = {0x81, 0x03, 'a', 'b', 'c'};
    push_bytes(0, frame, 5);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_reject_reserved_opcode()
{
    WsConn *ws = ws_alloc(0);
    // Opcode 0x3 is reserved (RFC 6455 §5.2) - must fail the connection.
    uint8_t frame[2] = {0x83, 0x80};
    push_bytes(0, frame, 2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_reject_fragmented_control_frame()
{
    WsConn *ws = ws_alloc(0);
    // PING with FIN=0 - control frames MUST NOT be fragmented (RFC 6455 §5.5).
    uint8_t frame[2] = {0x09, 0x80};
    push_bytes(0, frame, 2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_reject_oversized_control_frame()
{
    WsConn *ws = ws_alloc(0);
    // PING (masked) with payload length 126 - control frames MUST be <= 125
    // bytes (RFC 6455 §5.5).
    uint8_t frame[2] = {0x89, (uint8_t)(0x80u | 126u)};
    push_bytes(0, frame, 2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_16bit_length_frame()
{
    WsConn *ws = ws_alloc(0);
    // Build a 130-byte payload (> 125, requires 16-bit length field)
    static uint8_t payload[130];
    for (int i = 0; i < 130; i++)
    {
        payload[i] = (uint8_t)(i & 0xFF);
    }

    // Frame: FIN|BINARY, MASK|126, hi, lo, 4-byte mask, 130 payload bytes. BINARY
    // (not TEXT) since the 0..129 byte ramp is not valid UTF-8; this test exercises
    // the 16-bit length field, not text validation.
    static uint8_t frame[142];
    size_t flen = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, payload, 130, PROTO_TRUE);

    push_bytes(0, frame, flen);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL(130, (int)ws->payload_len);
    TEST_ASSERT_EQUAL(0, (int)ws->buf[0]);
    TEST_ASSERT_EQUAL(129 & 0xFF, (int)ws->buf[129]);
}

void test_ws_parse_rsv1_set_closes_protocol()
{
    WsConn *ws = ws_alloc(0);
    // FIN=1, RSV1=0x40, TEXT: byte0 = 0x80|0x40|0x01 = 0xC1
    uint8_t frame[2] = {0xC1, 0x00};
    push_bytes(0, frame, 2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_rsv2_set_closes_protocol()
{
    WsConn *ws = ws_alloc(0);
    // FIN=1, RSV2=0x20, TEXT: byte0 = 0x80|0x20|0x01 = 0xA1
    uint8_t frame[2] = {0xA1, 0x00};
    push_bytes(0, frame, 2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_rsv3_set_closes_protocol()
{
    WsConn *ws = ws_alloc(0);
    // FIN=1, RSV3=0x10, TEXT: byte0 = 0x80|0x10|0x01 = 0x91
    uint8_t frame[2] = {0x91, 0x00};
    push_bytes(0, frame, 2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_64bit_length_closes_too_big()
{
    WsConn *ws = ws_alloc(0);
    // FIN=1, TEXT, MASK=1, len7=127 (64-bit length), then 8 length bytes
    uint8_t frame[10] = {
        0x81,                     // FIN=1, TEXT
        0xFF,                     // MASK=1, len7=127
        0,    0, 0, 0, 0, 0, 0, 1 // 8-byte big-endian length = 1
    };
    push_bytes(0, frame, sizeof(frame));
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_oversized_16bit_length_closes_too_big()
{
    WsConn *ws = ws_alloc(0);
    uint16_t big_len = (uint16_t)WS_FRAME_SIZE + 1;
    uint8_t frame[4] = {0x81,
                        0xFE, // MASK=1, len7=126 → 16-bit extended
                        (uint8_t)(big_len >> 8), (uint8_t)(big_len)};
    push_bytes(0, frame, sizeof(frame));
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_fragment_start_waits_for_continuation()
{
    WsConn *ws = ws_alloc(0);
    // FIN=0, TEXT, "Hi" - start of a fragmented message; not deliverable yet.
    uint8_t frame[8] = {0x01, 0x82, 0, 0, 0, 0, 'H', 'i'};
    push_bytes(0, frame, sizeof(frame));
    ws_parse(ws);
    TEST_ASSERT_NOT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_NOT_EQUAL(WS_ERROR, ws->parse_state);
    TEST_ASSERT_TRUE(ws->fragmenting);
}

void test_ws_fragmented_message_reassembled()
{
    WsConn *ws = ws_alloc(0);
    uint8_t f1[16], f2[16];
    size_t n1 = build_frame(f1, WS_OP_TEXT, PROTO_FALSE, (const uint8_t *)"He", 2, PROTO_TRUE);
    size_t n2 = build_frame(f2, WS_OP_CONTINUATION, PROTO_TRUE, (const uint8_t *)"llo", 3, PROTO_TRUE);

    push_bytes(0, f1, n1);
    ws_parse(ws);
    TEST_ASSERT_TRUE(ws->fragmenting);

    push_bytes(0, f2, n2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL(WS_OP_TEXT, ws->opcode); // original message opcode reported
    TEST_ASSERT_EQUAL(5, (int)ws->payload_len);
    TEST_ASSERT_EQUAL_MEMORY("Hello", ws->buf, 5);
}

void test_ws_control_frame_interleaved_in_fragments()
{
    WsConn *ws = ws_alloc(0);
    uint8_t f1[16], pf[16], f2[16];
    size_t n1 = build_frame(f1, WS_OP_TEXT, PROTO_FALSE, (const uint8_t *)"He", 2, PROTO_TRUE);
    size_t np = build_frame(pf, WS_OP_PING, PROTO_TRUE, (const uint8_t *)"x", 1, PROTO_TRUE);
    size_t n2 = build_frame(f2, WS_OP_CONTINUATION, PROTO_TRUE, (const uint8_t *)"llo", 3, PROTO_TRUE);

    // A PING arrives between the two data fragments; it must be handled without
    // corrupting the partial message (RFC 6455 §5.4).
    push_bytes(0, f1, n1);
    push_bytes(0, pf, np);
    push_bytes(0, f2, n2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL(WS_OP_TEXT, ws->opcode);
    TEST_ASSERT_EQUAL(5, (int)ws->payload_len);
    TEST_ASSERT_EQUAL_MEMORY("Hello", ws->buf, 5);
}

// A multi-fragment message whose running total exceeds WS_FRAME_SIZE must be
// rejected even though each individual fragment fits - the per-frame check is
// against the accumulated msg_len, not just the current frame.
void test_ws_fragment_accumulation_overflow_rejected()
{
    WsConn *ws = ws_alloc(0);
    static uint8_t payload[WS_FRAME_SIZE];
    static uint8_t frame[WS_FRAME_SIZE + 8];
    for (int i = 0; i < WS_FRAME_SIZE; i++)
    {
        payload[i] = (uint8_t)(i & 0xFF);
    }

    // Fragment 1 (FIN=0): exactly WS_FRAME_SIZE bytes - fits, starts reassembly.
    size_t n1 = build_frame(frame, WS_OP_TEXT, PROTO_FALSE, payload, (uint16_t)WS_FRAME_SIZE, PROTO_TRUE);
    push_bytes(0, frame, n1);
    ws_parse(ws);
    TEST_ASSERT_TRUE(ws->fragmenting);
    TEST_ASSERT_NOT_EQUAL(WS_ERROR, ws->parse_state);

    // Fragment 2 (CONTINUATION, FIN=1): one more byte pushes the total over the
    // buffer - must be rejected with CLOSE_TOO_BIG, not overrun ws->buf.
    uint8_t one = 'x';
    size_t n2 = build_frame(frame, WS_OP_CONTINUATION, PROTO_TRUE, &one, 1, PROTO_TRUE);
    push_bytes(0, frame, n2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_continuation_without_start_rejected()
{
    WsConn *ws = ws_alloc(0);
    // CONTINUATION with no message in progress (RFC 6455 §5.4) → 1002.
    uint8_t frame[8] = {0x80, 0x82, 0, 0, 0, 0, 'H', 'i'};
    push_bytes(0, frame, sizeof(frame));
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_new_data_frame_during_fragmentation_rejected()
{
    WsConn *ws = ws_alloc(0);
    uint8_t f1[16], f2[16];
    size_t n1 = build_frame(f1, WS_OP_TEXT, PROTO_FALSE, (const uint8_t *)"He", 2, PROTO_TRUE);
    // A second TEXT (new message) before finishing the first is illegal.
    size_t n2 = build_frame(f2, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)"llo", 3, PROTO_TRUE);
    push_bytes(0, f1, n1);
    push_bytes(0, f2, n2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_ping_auto_pong_resets_frame()
{
    WsConn *ws = ws_alloc(0);
    // FIN=1, PING=0x09: byte0 = 0x89
    uint8_t frame[10] = {0x89, // FIN=1, PING
                         0x84, // MASK=1, len=4
                         0,    0, 0, 0, 'p', 'i', 'n', 'g'};
    push_bytes(0, frame, sizeof(frame));
    ws_parse(ws);
    // Auto-pong fires and frame resets - parser waits for next frame
    TEST_ASSERT_EQUAL(WS_HEADER1, ws->parse_state);
}

void test_ws_parse_pong_silently_ignored()
{
    WsConn *ws = ws_alloc(0);
    // FIN=1, PONG=0x0A: byte0 = 0x8A
    uint8_t frame[8] = {0x8A, // FIN=1, PONG
                        0x82, // MASK=1, len=2
                        0,    0, 0, 0, 0, 0};
    push_bytes(0, frame, sizeof(frame));
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_HEADER1, ws->parse_state);
}

void test_ws_parse_close_marks_ws_closed()
{
    WsConn *ws = ws_alloc(0);
    // FIN=1, CLOSE=0x08: byte0 = 0x88
    uint8_t frame[8] = {
        0x88,                     // FIN=1, CLOSE
        0x82,                     // MASK=1, len=2
        0,    0, 0, 0, 0x03, 0xE8 // status code 1000 = WS_CLOSE_NORMAL
    };
    push_bytes(0, frame, sizeof(frame));
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_CLOSED, ws->parse_state);
}

void test_ws_parse_stops_at_frame_ready()
{
    WsConn *ws = ws_alloc(0);
    const uint8_t p[] = {'A'};
    uint8_t f1[8], f2[8];
    size_t l1 = build_frame(f1, WS_OP_TEXT, PROTO_TRUE, p, 1, PROTO_TRUE);
    size_t l2 = build_frame(f2, WS_OP_TEXT, PROTO_TRUE, p, 1, PROTO_TRUE);

    // Push two complete frames -- parser should stop after the first
    push_bytes(0, f1, l1);
    push_bytes(0, f2, l2);
    ws_parse(ws);

    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    // Ring buffer still has second frame bytes left
    TEST_ASSERT_NOT_EQUAL(conn_pool[0].rx_head, conn_pool[0].rx_tail);
}

// A close frame followed by more buffered bytes: ws_parse must stop as soon as
// parse_state flips to WS_CLOSED (checked at the top of the loop on the NEXT
// iteration), leaving the trailing bytes untouched in the ring.
void test_ws_parse_stops_after_close_leaves_ring_untouched()
{
    WsConn *ws = ws_alloc(0);
    uint8_t close_frame[8] = {
        0x88,                     // FIN=1, CLOSE
        0x82,                     // MASK=1, len=2
        0,    0, 0, 0, 0x03, 0xE8 // status code 1000
    };
    const uint8_t p[] = {'z'};
    uint8_t next[8];
    size_t nlen = build_frame(next, WS_OP_TEXT, PROTO_TRUE, p, 1, PROTO_TRUE);

    push_bytes(0, close_frame, sizeof(close_frame));
    push_bytes(0, next, nlen);
    ws_parse(ws);

    TEST_ASSERT_EQUAL(WS_CLOSED, ws->parse_state);
    // Trailing frame bytes were left in the ring (loop returned before reading them).
    TEST_ASSERT_NOT_EQUAL(conn_pool[0].rx_head, conn_pool[0].rx_tail);
}

void test_ws_reset_frame_clears_fields()
{
    WsConn *ws = ws_alloc(0);
    ws->parse_state = WS_FRAME_READY;
    ws->payload_len = 10;
    ws->payload_idx = 10;
    ws->fin = PROTO_TRUE;
    ws->masked = PROTO_TRUE;
    ws->mask_key[0] = 0xAB;
    ws->buf[0] = 'X';
    ws->len64_count = 5;

    ws_reset_frame(ws);

    TEST_ASSERT_EQUAL(WS_HEADER1, ws->parse_state);
    TEST_ASSERT_EQUAL(0, (int)ws->payload_len);
    TEST_ASSERT_EQUAL(0, (int)ws->payload_idx);
    TEST_ASSERT_FALSE(ws->fin);
    TEST_ASSERT_FALSE(ws->masked);
    TEST_ASSERT_EQUAL(0, (int)ws->mask_key[0]);
    TEST_ASSERT_EQUAL('\0', (char)ws->buf[0]);
}

// ws_feed_byte()'s dispatch switch has no default case body of its own beyond a bare
// break - an out-of-range parse_state (never produced by this module itself) must be a
// silent no-op rather than touch any frame state.
void test_ws_feed_byte_unknown_parse_state_is_nop()
{
    WsConn *ws = ws_alloc(0);
    ws->parse_state = (WsParseState)99;
    ws->payload_idx = 0;
    ws_feed_byte(ws, 0x42);
    TEST_ASSERT_EQUAL((WsParseState)99, ws->parse_state); // untouched
    TEST_ASSERT_EQUAL(0, (int)ws->payload_idx);           // untouched
}

// Defensive capacity guard: WS_PAYLOAD for a control frame at ctl_buf's capacity must drop
// the byte instead of writing past the buffer. Unreachable via the wire protocol (control
// frames are capped at 125 bytes by the WS_HEADER2 guard, one below this limit), so the
// state is driven directly - the same pattern as the HTTP body-length capacity guard test.
void test_ws_payload_ctl_buf_capacity_guard_direct()
{
    WsConn *ws = ws_alloc(0);
    ws->opcode = WS_OP_PING;
    ws->parse_state = WS_PAYLOAD;
    ws->payload_len = 200; // defensive: the wire protocol never allows this for a control frame
    ws->payload_idx = sizeof(ws->ctl_buf) - 1;
    ws_feed_byte(ws, 'X');
    TEST_ASSERT_EQUAL(sizeof(ws->ctl_buf), (size_t)ws->payload_idx);     // index still advances
    TEST_ASSERT_EQUAL('\0', (char)ws->ctl_buf[sizeof(ws->ctl_buf) - 1]); // but no write occurred
}

// Same guard for the data-message buf: unreachable via the wire protocol (WS_HEADER2 /
// WS_LEN16_LO both reject a frame whose msg_len + payload_len would exceed WS_FRAME_SIZE
// before WS_PAYLOAD is ever entered), so drive the state directly.
void test_ws_payload_data_buf_capacity_guard_direct()
{
    WsConn *ws = ws_alloc(0);
    ws->opcode = WS_OP_BINARY;
    ws->parse_state = WS_PAYLOAD;
    ws->msg_len = WS_FRAME_SIZE; // defensive: the wire protocol never allows this
    ws->payload_len = 10;
    ws->payload_idx = 0;
    ws_feed_byte(ws, 'Y');
    TEST_ASSERT_EQUAL(1, (int)ws->payload_idx);            // index still advances
    TEST_ASSERT_EQUAL('\0', (char)ws->buf[WS_FRAME_SIZE]); // last valid slot untouched
}

void test_ws_parse_mask_applied_correctly()
{
    WsConn *ws = ws_alloc(0);
    // Frame with mask key {0x37, 0xFA, 0x21, 0x3D}, payload 'H' XOR 0x37 = 0x7F
    // Payload byte 'H' = 0x48; 0x48 ^ 0x37 = 0x7F
    uint8_t frame[8] = {
        0x81, // FIN=1, TEXT
        0x81, // MASK=1, len=1
        0x37, 0xFA, 0x21, 0x3D,
        0x7F // 'H' ^ 0x37 = 0x7F
    };
    push_bytes(0, frame, sizeof(frame));
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL('H', (char)ws->buf[0]); // 0x7F ^ 0x37 = 0x48 = 'H'
}

// RFC 6455 8.1: a TEXT message that is not valid UTF-8 must fail the connection (1007).
void test_ws_text_invalid_utf8_rejected()
{
    WsConn *ws = ws_alloc(0);
    const uint8_t bad[] = {0xC3, 0x28}; // 0xC3 starts a 2-byte seq; 0x28 is not a continuation
    uint8_t frame[16];
    size_t n = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, bad, 2, PROTO_TRUE);
    push_bytes(0, frame, n);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

// Valid multi-byte UTF-8 in a TEXT frame is accepted ("h" + U+00E9 + "llo").
void test_ws_text_valid_utf8_accepted()
{
    WsConn *ws = ws_alloc(0);
    const uint8_t ok[] = {'h', 0xC3, 0xA9, 'l', 'l', 'o'};
    uint8_t frame[16];
    size_t n = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, ok, (uint16_t)sizeof(ok), PROTO_TRUE);
    push_bytes(0, frame, n);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL((int)sizeof(ok), (int)ws->payload_len);
}

// A BINARY frame is never UTF-8-validated: arbitrary bytes are accepted.
void test_ws_binary_arbitrary_bytes_accepted()
{
    WsConn *ws = ws_alloc(0);
    const uint8_t bin[] = {0xFF, 0xFE, 0x00, 0xC3, 0x28};
    uint8_t frame[16];
    size_t n = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, bin, (uint16_t)sizeof(bin), PROTO_TRUE);
    push_bytes(0, frame, n);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
}

// ====================================================================
// STRESS TESTS
// ====================================================================

// 100 parse+free cycles on the same slot -- no state accumulation
void stress_ws_parse_reset_100_cycles()
{
    const char *text = "test";
    uint8_t frame[12];
    size_t flen = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)text, 4, PROTO_TRUE);
    for (int i = 0; i < 100; i++)
    {
        WsConn *ws = ws_alloc(0);
        TEST_ASSERT_NOT_NULL_MESSAGE(ws, "alloc failed");
        push_bytes(0, frame, flen);
        ws_parse(ws);
        TEST_ASSERT_EQUAL_MESSAGE(WS_FRAME_READY, ws->parse_state, "not FRAME_READY");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(text, (const char *)ws->buf, "payload mismatch");
        ws_free(0);
        conn_pool[0].rx_head = conn_pool[0].rx_tail = 0;
    }
}

// Alloc/free cycle across full pool, 50 iterations
void stress_ws_alloc_free_pool_cycle()
{
    for (int cycle = 0; cycle < 50; cycle++)
    {
        WsConn *w0 = ws_alloc(0);
        WsConn *w1 = ws_alloc(1);
        TEST_ASSERT_NOT_NULL(w0);
        TEST_ASSERT_NOT_NULL(w1);
        TEST_ASSERT_NULL(ws_alloc(2)); // pool full at MAX_WS_CONNS=2

        ws_free(0);
        WsConn *w0b = ws_alloc(0);
        TEST_ASSERT_NOT_NULL(w0b);
        TEST_ASSERT_EQUAL(0, (int)w0b->slot_id);

        ws_free(0);
        ws_free(1);
        TEST_ASSERT_FALSE(ws_pool[0].active);
        TEST_ASSERT_FALSE(ws_pool[1].active);
    }
}

// Byte-by-byte incremental parse -- worst case for streaming input
void stress_ws_parse_incremental_byte_by_byte()
{
    WsConn *ws = ws_alloc(0);
    const char *text = "Incremental";
    uint8_t frame[20];
    size_t flen = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)text, (uint16_t)strlen(text), PROTO_TRUE);
    for (size_t i = 0; i < flen; i++)
    {
        push_bytes(0, &frame[i], 1);
        ws_parse(ws);
        if (i < flen - 1)
        {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(WS_ERROR, ws->parse_state, "WS_ERROR during valid incremental parse");
        }
    }
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL_STRING(text, (const char *)ws->buf);
}

// Parse a full WS_FRAME_SIZE payload -- boundary value for the buffer
void stress_ws_parse_max_payload()
{
    WsConn *ws = ws_alloc(0);
    static uint8_t payload[WS_FRAME_SIZE];
    static uint8_t frame[WS_FRAME_SIZE + 8]; // +8: 2 hdr + 2 extended len + 4 mask

    for (int i = 0; i < WS_FRAME_SIZE; i++)
    {
        payload[i] = (uint8_t)(i & 0xFF);
    }

    size_t flen = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, payload, (uint16_t)WS_FRAME_SIZE, PROTO_TRUE);
    push_bytes(0, frame, flen);
    ws_parse(ws);

    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL((int)WS_FRAME_SIZE, (int)ws->payload_len);
    TEST_ASSERT_EQUAL(0, (int)ws->buf[0]);
    TEST_ASSERT_EQUAL((int)((WS_FRAME_SIZE - 1) & 0xFF), (int)ws->buf[WS_FRAME_SIZE - 1]);
    TEST_ASSERT_EQUAL('\0', (char)ws->buf[WS_FRAME_SIZE]);
}

// Two consecutive valid frames on the same slot
void stress_ws_parse_two_consecutive_frames()
{
    WsConn *ws = ws_alloc(0);
    const char *t1 = "first";
    const char *t2 = "second";
    uint8_t f1[16], f2[16];
    size_t l1 = build_frame(f1, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)t1, (uint16_t)strlen(t1), PROTO_TRUE);
    size_t l2 = build_frame(f2, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)t2, (uint16_t)strlen(t2), PROTO_TRUE);

    // First frame
    push_bytes(0, f1, l1);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL_STRING(t1, (const char *)ws->buf);

    // Reset and parse second frame
    ws_reset_frame(ws);
    push_bytes(0, f2, l2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL_STRING(t2, (const char *)ws->buf);
}

#if PC_ENABLE_WS_DEFLATE
// permessage-deflate: a compressed (RSV1) frame is decompressed before delivery.
void test_ws_permessage_deflate_inbound()
{
    // "Hello, World!" as permessage-deflate (SYNC_FLUSH, marker stripped) - the
    // same vector the inflate suite uses; the parser appends the marker itself.
    static const uint8_t comp[] = {242, 72, 205, 201, 201, 215, 81, 8, 207, 47, 202, 73, 81, 4, 0};
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE; // extension negotiated at handshake

    uint8_t frame[64];
    size_t n = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, comp, (uint16_t)sizeof(comp), PROTO_TRUE);
    frame[0] |= 0x40; // RSV1 = compressed message

    push_bytes(0, frame, n);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL_size_t(13, ws->msg_len);
    TEST_ASSERT_EQUAL_STRING("Hello, World!", (const char *)ws->buf);
}

// With the feature compiled in but no extension negotiated, an RSV1 frame is
// still a protocol error.
void test_ws_rsv1_without_negotiation_closes()
{
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_FALSE;
    uint8_t frame[16];
    size_t n = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)"x", 1, PROTO_TRUE);
    frame[0] |= 0x40;
    push_bytes(0, frame, n);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

// Outbound: a compressible data frame is sent with RSV1 set and a body that
// inflates back to the original (the on-wire form a browser would decode).
void test_ws_permessage_deflate_outbound()
{
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    const char *msg = "The quick brown fox. The quick brown fox. The quick brown fox.";
    uint16_t mlen = (uint16_t)strlen(msg);

    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_TEXT, (const uint8_t *)msg, mlen));
    tcp_capture_disable();

    const uint8_t *sent = (const uint8_t *)tcp_captured();
    size_t sent_len = tcp_captured_len();
    TEST_ASSERT_TRUE(sent_len >= 2);

    // FIN + RSV1 + TEXT; compressed body is shorter than the original.
    TEST_ASSERT_EQUAL_UINT8(0x80 | 0x40 | (uint8_t)WS_OP_TEXT, sent[0]);
    uint16_t plen = sent[1] & 0x7F;
    size_t hdr = 2;
    if (plen == 126)
    {
        plen = (uint16_t)((sent[2] << 8) | sent[3]);
        hdr = 4;
    }
    TEST_ASSERT_TRUE(plen < mlen); // it actually compressed
    TEST_ASSERT_EQUAL_size_t(hdr + plen, sent_len);

    // Re-append the RFC 7692 marker and inflate: must equal the original message.
    uint8_t comp[256];
    TEST_ASSERT_TRUE(plen + 4 <= sizeof(comp));
    memcpy(comp, sent + hdr, plen);
    comp[plen] = 0x00;
    comp[plen + 1] = 0x00;
    comp[plen + 2] = 0xff;
    comp[plen + 3] = 0xff;

    uint8_t out[256];
    uint8_t scr[INFLATE_SCRATCH_SIZE];
    size_t out_len = 0;
    int rc = (int)inflate_raw(comp, plen + 4, out, sizeof(out), &out_len, scr, sizeof(scr));
    TEST_ASSERT_EQUAL_INT(INFLATE_OK, rc);
    TEST_ASSERT_EQUAL_size_t(mlen, out_len);
    TEST_ASSERT_EQUAL_MEMORY(msg, out, mlen);
}

// Outbound: a payload that would not shrink is sent uncompressed (RSV1 clear),
// and a control frame is never compressed even on a pmd connection.
void test_ws_outbound_incompressible_not_flagged()
{
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_TEXT, (const uint8_t *)"x", 1));
    tcp_capture_disable();
    const uint8_t *sent = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_TEXT, sent[0]); // no RSV1: not worth compressing
    TEST_ASSERT_EQUAL_UINT8(1, sent[1] & 0x7F);
    TEST_ASSERT_EQUAL_UINT8('x', sent[2]);

    // A PONG control frame is never compressed, even with data-like content.
    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_PONG, (const uint8_t *)"AAAAAAAAAAAAAAAA", 16));
    tcp_capture_disable();
    sent = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_PONG, sent[0]); // RSV1 clear on control frames
}
// A frame marked compressed (RSV1) whose payload is not valid DEFLATE closes the connection.
void test_ws_deflate_inflate_error_closes()
{
    static const uint8_t garbage[] = {0xFF, 0xFF, 0xFF}; // invalid deflate block type
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;
    uint8_t frame[32];
    size_t n = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, garbage, (uint16_t)sizeof(garbage), PROTO_TRUE);
    frame[0] |= 0x40; // RSV1 = compressed
    push_bytes(0, frame, n);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state); // inflate error -> closed
}

// A compressed (RSV1) message whose DEFLATE stream inflates to more than
// WS_FRAME_SIZE bytes must fail the connection with 1009 (WS_CLOSE_TOO_BIG).
// The 9 compressed bytes below are `zlib.compressobj(-15)` of 600 'A's with the
// RFC 7692 00 00 ff ff marker stripped (the parser re-appends it); 600 > 512.
void test_ws_permessage_deflate_inflate_overflow_closes()
{
    static const uint8_t comp[] = {114, 116, 28, 5, 163, 128, 250, 0, 0};
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    uint8_t frame[32];
    size_t n = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, comp, (uint16_t)sizeof(comp), PROTO_TRUE);
    frame[0] |= 0x40; // RSV1 = compressed message
    push_bytes(0, frame, n);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state); // inflate overflow -> 1009, fail closed
}

// A compressed (RSV1) message that arrives with the scratch arena already
// exhausted cannot borrow its inflate buffers; ws_finish_frame must fail closed
// (1002, WS_CLOSE_PROTOCOL) rather than dereference a null borrow. Drain the
// arena up front, then restore it so later tests see a clean arena.
void test_ws_permessage_deflate_scratch_exhausted_closes()
{
    static const uint8_t comp[] = {242, 72, 205, 201, 201, 215, 81, 8, 207, 47, 202, 73, 81, 4, 0};
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    // Consume the whole arena so every pc_plaintext_alloc() in ws_finish_frame returns null.
    void *hog = pc_plaintext_alloc(pc_plaintext_capacity(), 1);
    TEST_ASSERT_NOT_NULL(hog);

    uint8_t frame[32];
    size_t n = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, comp, (uint16_t)sizeof(comp), PROTO_TRUE);
    frame[0] |= 0x40; // RSV1 = compressed message
    push_bytes(0, frame, n);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state); // arena exhausted -> fail closed

    pc_plaintext_reset(); // hand the arena back for the remaining tests
}

// Feed one compressed (RSV1) frame with the arena pre-drained to leave exactly
// `leave` free bytes, so a chosen pc_plaintext_alloc() in ws_finish_frame is the one
// that fails. Exercises the middle/last legs of the `if (!in || !out || !tbl)`
// short-circuit (out==null with in!=null; tbl==null with in,out!=null).
static void feed_compressed_with_arena_leaving(size_t leave)
{
    static const uint8_t comp[] = {242, 72, 205, 201, 201, 215, 81, 8, 207, 47, 202, 73, 81, 4, 0};
    conn_pool[0].rx_head = conn_pool[0].rx_tail = 0;
    ws_free(0);
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    pc_plaintext_reset();
    void *hog = pc_plaintext_alloc(pc_plaintext_capacity() - leave, 1);
    TEST_ASSERT_NOT_NULL(hog);

    uint8_t frame[32];
    size_t n = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, comp, (uint16_t)sizeof(comp), PROTO_TRUE);
    frame[0] |= 0x40;
    push_bytes(0, frame, n);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
    pc_plaintext_reset();
}

// The inbound arena-exhaustion guard `if (!in || !out || !tbl)` must fail closed
// on ANY of the three borrows, not just the first. comp_len is 15, so in wants
// 19 bytes, out wants WS_FRAME_SIZE (512), tbl wants INFLATE_SCRATCH_SIZE (1536).
void test_ws_permessage_deflate_partial_scratch_failures()
{
    feed_compressed_with_arena_leaving(19 + 100);      // in ok, out fails
    feed_compressed_with_arena_leaving(19 + 512 + 16); // in ok, out ok, tbl fails
}

// A pmd-negotiated connection that receives an UNcompressed data frame (no RSV1)
// exercises `msg_compressed = pmd && (rsv & 0x40)` with pmd true / rsv clear, and
// the RSV-validation `(rsv & 0x40) && !(pmd && new_data)` with the 0x40 bit clear.
void test_ws_pmd_negotiated_uncompressed_frame_accepted()
{
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE; // extension negotiated, but this frame is not compressed
    const char *text = "plain";
    uint8_t frame[16];
    size_t n = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)text, (uint16_t)strlen(text), PROTO_TRUE);
    push_bytes(0, frame, n);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_FALSE(ws->msg_compressed);
    TEST_ASSERT_EQUAL_STRING("plain", (const char *)ws->buf);
}

// Outbound compression on a BINARY frame (opcode==BINARY leg of the send-side
// guard) and, with the arena drained, the `if (scr && cbuf)` fall-through that
// sends the payload uncompressed rather than dereferencing a null borrow.
void test_ws_outbound_binary_and_scratch_starved()
{
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;
    ws_set_frag_size(0);

    // BINARY, compressible: takes the opcode==BINARY leg and actually compresses.
    static uint8_t bin[64];
    for (int i = 0; i < 64; i++)
    {
        bin[i] = (uint8_t)(i & 1); // highly repetitive -> shrinks
    }
    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_BINARY, bin, sizeof(bin)));
    const uint8_t *s = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | 0x40 | (uint8_t)WS_OP_BINARY, s[0]); // RSV1 set
    tcp_capture_disable();

    // scr (DEFLATE_SCRATCH_SIZE) cannot be borrowed -> compression skipped, sent raw.
    pc_plaintext_reset();
    void *hog = pc_plaintext_alloc(pc_plaintext_capacity(), 1);
    TEST_ASSERT_NOT_NULL(hog);
    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_BINARY, bin, sizeof(bin)));
    s = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_BINARY, s[0]); // no RSV1: not compressed
    tcp_capture_disable();

    // scr ok but cbuf (cap bytes) cannot be borrowed -> same fall-through.
    pc_plaintext_reset();
    hog = pc_plaintext_alloc(pc_plaintext_capacity() - (DEFLATE_SCRATCH_SIZE + 8), 1);
    TEST_ASSERT_NOT_NULL(hog);
    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_BINARY, bin, sizeof(bin)));
    s = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_BINARY, s[0]); // no RSV1: cbuf borrow failed
    tcp_capture_disable();
    pc_plaintext_reset();
}

// Outbound send-side guard `pmd && len>0 && (TEXT||BINARY)`: pmd true with len==0
// (short-circuits at len) and pmd true on a control opcode (both data legs false).
void test_ws_outbound_pmd_zero_len_and_control()
{
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;
    ws_set_frag_size(0);

    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_TEXT, NULL, 0)); // len==0 leg
    tcp_capture_disable();

    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_PING, (const uint8_t *)"AAAA", 4)); // control, both data legs false
    tcp_capture_disable();
}

// RFC 7692: RSV1 marks compression only on the FIRST frame of a message; a
// CONTINUATION frame with RSV1 set is a protocol error. Reaching that check
// exercises the `!(pmd && new_data)` leg of the RSV-validation with new_data
// false (continuation) while pmd is true and RSV1 is set.
void test_ws_pmd_continuation_with_rsv1_rejected()
{
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    // Frame 1: TEXT, FIN=0 - opens a fragmented message.
    uint8_t f1[16];
    size_t n1 = build_frame(f1, WS_OP_TEXT, PROTO_FALSE, (const uint8_t *)"He", 2, PROTO_TRUE);
    push_bytes(0, f1, n1);
    ws_parse(ws);
    TEST_ASSERT_TRUE(ws->fragmenting);

    // Frame 2: CONTINUATION, FIN=1, RSV1 set - illegal per RFC 7692.
    uint8_t f2[16];
    size_t n2 = build_frame(f2, WS_OP_CONTINUATION, PROTO_TRUE, (const uint8_t *)"llo", 3, PROTO_TRUE);
    f2[0] |= 0x40; // RSV1 on a continuation frame
    push_bytes(0, f2, n2);
    ws_parse(ws);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

#endif // PC_ENABLE_WS_DEFLATE

// Outbound fragmentation (RFC 6455 sec 5.4): a data message longer than the frag size is split into
// opcode|FIN=0, CONTINUATION|FIN=0..., CONTINUATION|FIN=1 frames; frag=0 restores the single frame.
void test_ws_outbound_fragmentation()
{
    WsConn *ws = ws_alloc(0);
    TEST_ASSERT_NOT_NULL(ws);
#if PC_ENABLE_WS_DEFLATE
    ws->pmd = PROTO_FALSE; // send the payload verbatim so the split is exactly these bytes
#endif
    const uint8_t msg[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    ws_set_frag_size(4);
    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_BINARY, msg, sizeof(msg)));
    tcp_capture_disable();
    ws_set_frag_size(0); // restore the default for every other test

    const uint8_t *s = (const uint8_t *)tcp_captured();
    // 3 frames, each a 2-byte header (len <= 125): [BINARY,FIN=0,4][CONT,FIN=0,4][CONT,FIN=1,2].
    TEST_ASSERT_EQUAL_size_t(3 * 2 + 10, tcp_captured_len());
    TEST_ASSERT_EQUAL_UINT8(WS_OP_BINARY, s[0]); // first: FIN clear, opcode
    TEST_ASSERT_EQUAL_UINT8(4, s[1]);
    TEST_ASSERT_EQUAL_UINT8(1, s[2]);
    TEST_ASSERT_EQUAL_UINT8(WS_OP_CONTINUATION, s[6]); // middle: FIN clear, CONTINUATION
    TEST_ASSERT_EQUAL_UINT8(4, s[7]);
    TEST_ASSERT_EQUAL_UINT8(5, s[8]);
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_CONTINUATION, s[12]); // last: FIN set, CONTINUATION
    TEST_ASSERT_EQUAL_UINT8(2, s[13]);
    TEST_ASSERT_EQUAL_UINT8(9, s[14]);
    TEST_ASSERT_EQUAL_UINT8(10, s[15]);

    // frag == 0 -> a single FIN frame again (default behavior).
    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_BINARY, msg, sizeof(msg)));
    tcp_capture_disable();
    s = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_BINARY, s[0]); // FIN set, one frame
    TEST_ASSERT_EQUAL_UINT8(10, s[1]);
}

void test_ws_send_frame_paths_and_parse_guard()
{
    WsConn *ws = ws_alloc(0);
    ws_set_frag_size(0); // single frame, no fragmentation
    uint8_t payload[200];
    for (int i = 0; i < 200; i++)
    {
        payload[i] = (uint8_t)i;
    }
    // Medium frame (len >= 126) uses the extended 16-bit length header.
    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_BINARY, payload, 200));
    const uint8_t *sent = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(126, sent[1]); // 16-bit extended-length marker (server->client unmasked)
    TEST_ASSERT_EQUAL_UINT8(0, sent[2]);   // length high byte
    TEST_ASSERT_EQUAL_UINT8(200, sent[3]); // length low byte
    tcp_capture_disable();
    // A payload larger than the fragment size emits multiple frames (continuation).
    ws_set_frag_size(64);
    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_TEXT, payload, 200));
    TEST_ASSERT_TRUE(tcp_captured_len() > 200); // payload + several frame headers
    tcp_capture_disable();
    ws_set_frag_size(0);
    // ws_send_frame + ws_parse on an inactive connection both fail closed / return immediately.
    conn_pool[0].state = CONN_CLOSING;
    TEST_ASSERT_FALSE(ws_send_frame(ws, WS_OP_TEXT, payload, 10));
    ws_parse(ws);
    conn_pool[0].state = CONN_ACTIVE;
}

// ws_emit_one's header write failing must fail the whole send and stop before the payload.
void test_ws_send_frame_header_write_failure()
{
    WsConn *ws = ws_alloc(0);
    ws_set_frag_size(0);
    tcp_capture_reset();
    mock_send_fail_after(0); // the very first write (the header) fails
    TEST_ASSERT_FALSE(ws_send_frame(ws, WS_OP_TEXT, (const uint8_t *)"hi", 2));
    mock_send_fail_after(-1);
    tcp_capture_disable();
}

// The header write can succeed while the payload write fails (a second, independent send).
void test_ws_send_frame_payload_write_failure()
{
    WsConn *ws = ws_alloc(0);
    ws_set_frag_size(0);
    tcp_capture_reset();
    mock_send_fail_after(1); // header write succeeds; the payload write fails
    TEST_ASSERT_FALSE(ws_send_frame(ws, WS_OP_TEXT, (const uint8_t *)"hi", 2));
    mock_send_fail_after(-1);
    tcp_capture_disable();
}

// len==0 short-circuits past the payload write entirely (e.g. a zero-length control reply).
void test_ws_send_frame_zero_length_payload()
{
    WsConn *ws = ws_alloc(0);
    ws_set_frag_size(0);
    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_PONG, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(2, tcp_captured_len()); // header only, no payload bytes
    tcp_capture_disable();
}

// A non-zero length with a null payload pointer must not dereference it - the payload write
// is skipped (header still goes out) rather than crashing.
void test_ws_send_frame_null_payload_with_nonzero_length()
{
    WsConn *ws = ws_alloc(0);
    ws_set_frag_size(0);
    tcp_capture_reset();
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_PING, NULL, 5));
    TEST_ASSERT_EQUAL_size_t(2, tcp_captured_len()); // header only; payload skipped (NULL)
    tcp_capture_disable();
}

// A non-zero fragment size that is still >= the message length takes the single-FIN-frame
// path, not the fragmentation loop.
void test_ws_send_frame_fits_within_frag_size_single_frame()
{
    WsConn *ws = ws_alloc(0);
    ws_set_frag_size(100);
    tcp_capture_reset();
    const uint8_t msg[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    TEST_ASSERT_TRUE(ws_send_frame(ws, WS_OP_BINARY, msg, sizeof(msg)));
    const uint8_t *s = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_BINARY, s[0]); // FIN set, single frame
    TEST_ASSERT_EQUAL_UINT8(10, s[1]);
    tcp_capture_disable();
    ws_set_frag_size(0);
}

// A write failure partway through the fragmentation loop (second fragment) must abort the
// whole send rather than silently dropping the remaining fragments.
void test_ws_send_frame_fragmentation_mid_send_failure()
{
    WsConn *ws = ws_alloc(0);
    ws_set_frag_size(4);
    const uint8_t msg[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    tcp_capture_reset();
    mock_send_fail_after(2); // frame 1's header+payload succeed; frame 2's header fails
    TEST_ASSERT_FALSE(ws_send_frame(ws, WS_OP_BINARY, msg, sizeof(msg)));
    mock_send_fail_after(-1);
    tcp_capture_disable();
    ws_set_frag_size(0);
}

// ws_close() on a slot whose transport connection is no longer active must still mark the
// WS state closed, but must not try to flush a dead connection.
void test_ws_close_when_conn_inactive_skips_flush()
{
    WsConn *ws = ws_alloc(0);
    conn_pool[0].state = CONN_CLOSING;
    ws_close(ws, WS_CLOSE_NORMAL); // must not crash
    TEST_ASSERT_EQUAL(WS_CLOSED, ws->parse_state);
    conn_pool[0].state = CONN_ACTIVE;
}

// A PING that completes while the transport connection is no longer active: the auto-pong
// reply is skipped (ws_send_frame fails closed) and the subsequent flush guard must also see
// the connection inactive rather than flush a dead PCB. Feeds ws_feed_byte() directly (not
// ws_parse(), which itself gates on conn activity) to isolate ws_finish_frame's own guard.
void test_ws_ping_flush_skipped_when_conn_inactive()
{
    WsConn *ws = ws_alloc(0);
    uint8_t frame[6] = {0x89, 0x80, 0, 0, 0, 0}; // FIN=1, PING, MASK=1, len=0
    conn_pool[0].state = CONN_CLOSING;
    for (size_t i = 0; i < sizeof(frame); i++)
    {
        ws_feed_byte(ws, frame[i]);
    }
    TEST_ASSERT_EQUAL(WS_HEADER1, ws->parse_state); // control frame handled, resumed
    conn_pool[0].state = CONN_ACTIVE;
}

int main()
{
    UNITY_BEGIN();

    // SHA-1 tests
    RUN_TEST(test_sha1_empty_string);
    RUN_TEST(test_sha1_abc);
    RUN_TEST(test_sha1_rfc6455_handshake_key);
    RUN_TEST(test_sha1_different_inputs_different_digests);

    // Base64 tests
    RUN_TEST(test_base64_encode_one_byte);
    RUN_TEST(test_base64_encode_two_bytes);
    RUN_TEST(test_base64_encode_three_bytes);
    RUN_TEST(test_base64_encode_ws_accept_key);
    RUN_TEST(test_base64_decode_one_byte);
    RUN_TEST(test_base64_decode_two_bytes);
    RUN_TEST(test_base64_decode_three_bytes);
    RUN_TEST(test_base64_decode_ws_accept_key);
    RUN_TEST(test_base64_decode_rejects_misplaced_padding);
    RUN_TEST(test_base64_decode_respects_capacity);
    RUN_TEST(test_base64_round_trip);

    // WS pool tests
    RUN_TEST(test_ws_pool_size);
    RUN_TEST(test_ws_ids_match_indices_after_init);
    RUN_TEST(test_ws_all_inactive_after_init);
    RUN_TEST(test_ws_alloc_returns_non_null);
    RUN_TEST(test_ws_alloc_sets_active);
    RUN_TEST(test_ws_alloc_sets_slot_id);
    RUN_TEST(test_ws_alloc_sets_parse_state_header1);
    RUN_TEST(test_ws_alloc_pool_full_returns_null);
    RUN_TEST(test_ws_active_reflects_pool_state);
    RUN_TEST(test_ws_payload_returns_buf_or_null);
    RUN_TEST(test_ws_find_returns_correct_conn);
    RUN_TEST(test_ws_find_returns_null_when_empty);
    RUN_TEST(test_ws_find_returns_null_for_different_slot);
    RUN_TEST(test_ws_find_after_both_slots_allocated);
    RUN_TEST(test_ws_free_deactivates_slot);
    RUN_TEST(test_ws_free_restores_ws_id);
    RUN_TEST(test_ws_free_makes_slot_findable_as_null);
    RUN_TEST(test_ws_free_nop_on_unallocated);
    RUN_TEST(test_ws_free_skips_active_slot_with_different_id);
    RUN_TEST(test_ws_alloc_after_free_succeeds);

    // WS frame parser tests
    RUN_TEST(test_ws_parse_text_frame_sets_ready);
    RUN_TEST(test_ws_parse_payload_stored_correctly);
    RUN_TEST(test_ws_parse_binary_frame_sets_ready);
    RUN_TEST(test_ws_parse_zero_length_unmasked_frame);
    RUN_TEST(test_ws_parse_zero_length_masked_frame);
    RUN_TEST(test_ws_reject_unmasked_data_frame);
    RUN_TEST(test_ws_reject_reserved_opcode);
    RUN_TEST(test_ws_reject_fragmented_control_frame);
    RUN_TEST(test_ws_reject_oversized_control_frame);
    RUN_TEST(test_ws_parse_16bit_length_frame);
    RUN_TEST(test_ws_parse_rsv1_set_closes_protocol);
    RUN_TEST(test_ws_parse_rsv2_set_closes_protocol);
    RUN_TEST(test_ws_parse_rsv3_set_closes_protocol);
    RUN_TEST(test_ws_parse_64bit_length_closes_too_big);
    RUN_TEST(test_ws_parse_oversized_16bit_length_closes_too_big);
    RUN_TEST(test_ws_fragment_start_waits_for_continuation);
    RUN_TEST(test_ws_fragmented_message_reassembled);
    RUN_TEST(test_ws_control_frame_interleaved_in_fragments);
    RUN_TEST(test_ws_fragment_accumulation_overflow_rejected);
    RUN_TEST(test_ws_continuation_without_start_rejected);
    RUN_TEST(test_ws_new_data_frame_during_fragmentation_rejected);
    RUN_TEST(test_ws_parse_ping_auto_pong_resets_frame);
    RUN_TEST(test_ws_parse_pong_silently_ignored);
    RUN_TEST(test_ws_parse_close_marks_ws_closed);
    RUN_TEST(test_ws_parse_stops_at_frame_ready);
    RUN_TEST(test_ws_parse_stops_after_close_leaves_ring_untouched);
    RUN_TEST(test_ws_reset_frame_clears_fields);
    RUN_TEST(test_ws_feed_byte_unknown_parse_state_is_nop);
    RUN_TEST(test_ws_payload_ctl_buf_capacity_guard_direct);
    RUN_TEST(test_ws_payload_data_buf_capacity_guard_direct);
    RUN_TEST(test_ws_parse_mask_applied_correctly);
    RUN_TEST(test_ws_text_invalid_utf8_rejected);
    RUN_TEST(test_ws_text_valid_utf8_accepted);
    RUN_TEST(test_ws_binary_arbitrary_bytes_accepted);
#if PC_ENABLE_WS_DEFLATE
    RUN_TEST(test_ws_permessage_deflate_inbound);
    RUN_TEST(test_ws_rsv1_without_negotiation_closes);
    RUN_TEST(test_ws_permessage_deflate_outbound);
    RUN_TEST(test_ws_deflate_inflate_error_closes);
    RUN_TEST(test_ws_outbound_incompressible_not_flagged);
    RUN_TEST(test_ws_permessage_deflate_inflate_overflow_closes);
    RUN_TEST(test_ws_permessage_deflate_scratch_exhausted_closes);
    RUN_TEST(test_ws_permessage_deflate_partial_scratch_failures);
    RUN_TEST(test_ws_pmd_negotiated_uncompressed_frame_accepted);
    RUN_TEST(test_ws_outbound_binary_and_scratch_starved);
    RUN_TEST(test_ws_outbound_pmd_zero_len_and_control);
    RUN_TEST(test_ws_pmd_continuation_with_rsv1_rejected);
#endif

    RUN_TEST(test_ws_outbound_fragmentation);

    // Stress tests
    RUN_TEST(stress_ws_parse_reset_100_cycles);
    RUN_TEST(stress_ws_alloc_free_pool_cycle);
    RUN_TEST(stress_ws_parse_incremental_byte_by_byte);
    RUN_TEST(stress_ws_parse_max_payload);
    RUN_TEST(stress_ws_parse_two_consecutive_frames);

    RUN_TEST(test_ws_send_frame_paths_and_parse_guard);
    RUN_TEST(test_ws_send_frame_header_write_failure);
    RUN_TEST(test_ws_send_frame_payload_write_failure);
    RUN_TEST(test_ws_send_frame_zero_length_payload);
    RUN_TEST(test_ws_send_frame_null_payload_with_nonzero_length);
    RUN_TEST(test_ws_send_frame_fits_within_frag_size_single_frame);
    RUN_TEST(test_ws_send_frame_fragmentation_mid_send_failure);
    RUN_TEST(test_ws_close_when_conn_inactive_skips_flush);
    RUN_TEST(test_ws_ping_flush_skipped_when_conn_inactive);
    return UNITY_END();
}
