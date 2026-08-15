// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/hash/sha1.h"
#include "network_drivers/presentation/codec/base64/base64.h"
#include "network_drivers/presentation/http/websocket/websocket.h"
#include "network_drivers/transport/tcp/common.h"
#include <string.h>

#include "rx_feed.h"
#include <unity.h>

#if PROTOCORE_ENABLE_WS_DEFLATE
#include "mmgr/plaintext.h"
#include "network_drivers/presentation/codec/deflate/deflate.h"
#include "network_drivers/presentation/codec/inflate/inflate.h"
#include "network_drivers/transport/tcp/tcp.h"
#endif

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
#if PROTOCORE_ENABLE_WEBSOCKET
    Ws.init(Ws.internal);
#endif
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].pcb = protocore_net_host_pcb();
    }
}

void tearDown()
{
}

void test_sha1_empty_string()
{
    uint8_t digest[PROTOCORE_SHA1_DIGEST_LEN];
    protocore_sha1((const uint8_t *)"", 0, digest);
    const uint8_t expected[PROTOCORE_SHA1_DIGEST_LEN] = {0xDA, 0x39, 0xA3, 0xEE, 0x5E, 0x6B, 0x4B, 0x0D, 0x32, 0x55,
                                                         0xBF, 0xEF, 0x95, 0x60, 0x18, 0x90, 0xAF, 0xD8, 0x07, 0x09};
    TEST_ASSERT_EQUAL_MEMORY(expected, digest, PROTOCORE_SHA1_DIGEST_LEN);
}

void test_sha1_abc()
{
    uint8_t digest[PROTOCORE_SHA1_DIGEST_LEN];
    protocore_sha1((const uint8_t *)"abc", 3, digest);
    const uint8_t expected[PROTOCORE_SHA1_DIGEST_LEN] = {0xA9, 0x99, 0x3E, 0x36, 0x47, 0x06, 0x81, 0x6A, 0xBA, 0x3E,
                                                         0x25, 0x71, 0x78, 0x50, 0xC2, 0x6C, 0x9C, 0xD0, 0xD8, 0x9D};
    TEST_ASSERT_EQUAL_MEMORY(expected, digest, PROTOCORE_SHA1_DIGEST_LEN);
}

void test_sha1_rfc6455_handshake_key()
{

    const char *input = "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[PROTOCORE_SHA1_DIGEST_LEN];
    protocore_sha1((const uint8_t *)input, strlen(input), digest);

    const uint8_t expected[PROTOCORE_SHA1_DIGEST_LEN] = {0xB3, 0x7A, 0x4F, 0x2C, 0xC0, 0x62, 0x4F, 0x16, 0x90, 0xF6,
                                                         0x46, 0x06, 0xCF, 0x38, 0x59, 0x45, 0xB2, 0xBE, 0xC4, 0xEA};
    TEST_ASSERT_EQUAL_MEMORY(expected, digest, PROTOCORE_SHA1_DIGEST_LEN);
}

void test_sha1_different_inputs_different_digests()
{
    uint8_t d1[PROTOCORE_SHA1_DIGEST_LEN], d2[PROTOCORE_SHA1_DIGEST_LEN];
    protocore_sha1((const uint8_t *)"abc", 3, d1);
    protocore_sha1((const uint8_t *)"abd", 3, d2);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(d1, d2, PROTOCORE_SHA1_DIGEST_LEN));
}

void test_base64_encode_one_byte()
{
    const uint8_t src[] = {0x4D};
    char out[8] = {0};
    Base64.encode(src, 1, out);
    TEST_ASSERT_EQUAL_STRING("TQ==", out);
}

void test_base64_encode_two_bytes()
{
    const uint8_t src[] = {0x4D, 0x61};
    char out[8] = {0};
    Base64.encode(src, 2, out);
    TEST_ASSERT_EQUAL_STRING("TWE=", out);
}

void test_base64_encode_three_bytes()
{
    const uint8_t src[] = {0x4D, 0x61, 0x6E};
    char out[8] = {0};
    Base64.encode(src, 3, out);
    TEST_ASSERT_EQUAL_STRING("TWFu", out);
}

void test_base64_encode_ws_accept_key()
{
    const uint8_t digest[PROTOCORE_SHA1_DIGEST_LEN] = {0xB3, 0x7A, 0x4F, 0x2C, 0xC0, 0x62, 0x4F, 0x16, 0x90, 0xF6,
                                                       0x46, 0x06, 0xCF, 0x38, 0x59, 0x45, 0xB2, 0xBE, 0xC4, 0xEA};
    char out[32] = {0};
    Base64.encode(digest, PROTOCORE_SHA1_DIGEST_LEN, out);
    TEST_ASSERT_EQUAL_STRING("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", out);
}

void test_base64_decode_one_byte()
{
    uint8_t dst[4] = {0};
    size_t n = Base64.decode("TQ==", dst, sizeof(dst));
    TEST_ASSERT_EQUAL(1, (int)n);
    TEST_ASSERT_EQUAL(0x4D, (int)dst[0]);
}

void test_base64_decode_two_bytes()
{
    uint8_t dst[4] = {0};
    size_t n = Base64.decode("TWE=", dst, sizeof(dst));
    TEST_ASSERT_EQUAL(2, (int)n);
    TEST_ASSERT_EQUAL(0x4D, (int)dst[0]);
    TEST_ASSERT_EQUAL(0x61, (int)dst[1]);
}

void test_base64_decode_three_bytes()
{
    uint8_t dst[4] = {0};
    size_t n = Base64.decode("TWFu", dst, sizeof(dst));
    TEST_ASSERT_EQUAL(3, (int)n);
    TEST_ASSERT_EQUAL(0x4D, (int)dst[0]);
    TEST_ASSERT_EQUAL(0x61, (int)dst[1]);
    TEST_ASSERT_EQUAL(0x6E, (int)dst[2]);
}

void test_base64_decode_ws_accept_key()
{
    uint8_t dst[PROTOCORE_SHA1_DIGEST_LEN + 4] = {0};
    size_t n = Base64.decode("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", dst, sizeof(dst));
    TEST_ASSERT_EQUAL(PROTOCORE_SHA1_DIGEST_LEN, (int)n);
    const uint8_t expected[PROTOCORE_SHA1_DIGEST_LEN] = {0xB3, 0x7A, 0x4F, 0x2C, 0xC0, 0x62, 0x4F, 0x16, 0x90, 0xF6,
                                                         0x46, 0x06, 0xCF, 0x38, 0x59, 0x45, 0xB2, 0xBE, 0xC4, 0xEA};
    TEST_ASSERT_EQUAL_MEMORY(expected, dst, PROTOCORE_SHA1_DIGEST_LEN);
}

void test_base64_round_trip()
{
    const uint8_t src[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98};
    char encoded[24] = {0};
    uint8_t decoded[16] = {0};
    Base64.encode(src, sizeof(src), encoded);
    size_t n = Base64.decode(encoded, decoded, sizeof(decoded));
    TEST_ASSERT_EQUAL((int)sizeof(src), (int)n);
    TEST_ASSERT_EQUAL_MEMORY(src, decoded, sizeof(src));
}

void test_base64_decode_rejects_misplaced_padding()
{
    uint8_t dst[8] = {0};
    TEST_ASSERT_EQUAL(0, (int)Base64.decode("A=BC", dst, sizeof(dst)));
    TEST_ASSERT_EQUAL(0, (int)Base64.decode("AB=C", dst, sizeof(dst)));
    TEST_ASSERT_EQUAL(0, (int)Base64.decode("=BCD", dst, sizeof(dst)));
    TEST_ASSERT_EQUAL(0, (int)Base64.decode("TWE=TWFu", dst, sizeof(dst)));
    TEST_ASSERT_EQUAL(0, (int)Base64.decode("TWF", dst, sizeof(dst)));

    TEST_ASSERT_EQUAL(1, (int)Base64.decode("TQ==", dst, sizeof(dst)));
    TEST_ASSERT_EQUAL(2, (int)Base64.decode("TWE=", dst, sizeof(dst)));
}

void test_base64_decode_respects_capacity()
{

    uint8_t dst[2] = {0};
    size_t n = Base64.decode("TWFu", dst, sizeof(dst));
    TEST_ASSERT_EQUAL(0, (int)n);

    uint8_t dst3[3] = {0};
    TEST_ASSERT_EQUAL(3, (int)Base64.decode("TWFu", dst3, sizeof(dst3)));
}

void test_ws_pool_size()
{
    TEST_ASSERT_EQUAL(2, MAX_WS_CONNS);
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
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    TEST_ASSERT_NOT_NULL(Ws.found);
}

void test_ws_alloc_sets_active()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_TRUE(ws->active);
}

void test_ws_alloc_sets_slot_id()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_EQUAL(0, (int)ws->slot_id);
}

void test_ws_alloc_sets_parse_state_header1()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_EQUAL(WS_HEADER1, ws->parse_state);
}

void test_ws_alloc_pool_full_returns_null()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    TEST_ASSERT_NOT_NULL(Ws.found);
    Ws.slot = 1;
    Ws.alloc(Ws.internal);
    TEST_ASSERT_NOT_NULL(Ws.found);
    Ws.slot = 2;
    Ws.alloc(Ws.internal);
    TEST_ASSERT_NULL(Ws.found);
}

void test_ws_active_reflects_pool_state()
{
    Ws.ws_id = 0;
    Ws.active(Ws.internal);
    TEST_ASSERT_FALSE(Ws.ok);
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    Ws.ws_id = 0;
    Ws.active(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    Ws.ws_id = (uint8_t)MAX_WS_CONNS;
    Ws.active(Ws.internal);
    TEST_ASSERT_FALSE(Ws.ok);
}

void test_ws_payload_returns_buf_or_null()
{
    Ws.ws_id = 0;
    Ws.payload_of(Ws.internal);
    TEST_ASSERT_NULL(Ws.text);
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    Ws.ws_id = 0;
    Ws.payload_of(Ws.internal);
    TEST_ASSERT_EQUAL_PTR((const char *)ws->buf, Ws.text);
    Ws.ws_id = (uint8_t)MAX_WS_CONNS;
    Ws.payload_of(Ws.internal);
    TEST_ASSERT_NULL(Ws.text);
}

void test_ws_find_returns_correct_conn()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *allocated = Ws.found;
    Ws.slot = 0;
    Ws.find(Ws.internal);
    WsConn *found = Ws.found;
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_PTR(allocated, found);
}

void test_ws_find_returns_null_when_empty()
{
    Ws.slot = 0;
    Ws.find(Ws.internal);
    TEST_ASSERT_NULL(Ws.found);
}

void test_ws_find_returns_null_for_different_slot()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    Ws.slot = 1;
    Ws.find(Ws.internal);
    TEST_ASSERT_NULL(Ws.found);
}

void test_ws_find_after_both_slots_allocated()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    Ws.slot = 1;
    Ws.alloc(Ws.internal);
    Ws.slot = 0;
    Ws.find(Ws.internal);
    TEST_ASSERT_NOT_NULL(Ws.found);
    Ws.slot = 1;
    Ws.find(Ws.internal);
    TEST_ASSERT_NOT_NULL(Ws.found);
}

void test_ws_free_deactivates_slot()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    Ws.slot = 0;
    Ws.free(Ws.internal);
    TEST_ASSERT_FALSE(ws_pool[0].active);
}

void test_ws_free_restores_ws_id()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    Ws.slot = 0;
    Ws.free(Ws.internal);
    TEST_ASSERT_EQUAL(0, (int)ws_pool[0].ws_id);
}

void test_ws_free_makes_slot_findable_as_null()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    Ws.slot = 0;
    Ws.free(Ws.internal);
    Ws.slot = 0;
    Ws.find(Ws.internal);
    TEST_ASSERT_NULL(Ws.found);
}

void test_ws_free_nop_on_unallocated()
{
    Ws.slot = 2;
    Ws.free(Ws.internal);
    TEST_ASSERT_FALSE(ws_pool[0].active);
    TEST_ASSERT_FALSE(ws_pool[1].active);
    TEST_PASS();
}

void test_ws_free_skips_active_slot_with_different_id()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    Ws.slot = 1;
    Ws.alloc(Ws.internal);
    Ws.slot = 1;
    Ws.free(Ws.internal);
    TEST_ASSERT_TRUE(ws_pool[0].active);
    Ws.slot = 1;
    Ws.find(Ws.internal);
    TEST_ASSERT_NULL(Ws.found);
}

void test_ws_alloc_after_free_succeeds()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    Ws.slot = 0;
    Ws.free(Ws.internal);
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    TEST_ASSERT_TRUE(ws->active);
    TEST_ASSERT_EQUAL(0, (int)ws->slot_id);
}

void test_ws_parse_text_frame_sets_ready()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    const uint8_t payload[] = {'H', 'i'};
    uint8_t frame[12];
    size_t flen = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, payload, 2, PROTO_TRUE);
    push_bytes(0, frame, flen);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
}

void test_ws_parse_payload_stored_correctly()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    const char *text = "Hello";
    uint8_t frame[16];
    size_t flen = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)text, (uint16_t)strlen(text), PROTO_TRUE);
    push_bytes(0, frame, flen);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL(5, (int)ws->payload_len);
    TEST_ASSERT_EQUAL_STRING("Hello", (const char *)ws->buf);
}

void test_ws_parse_binary_frame_sets_ready()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t frame[16];
    size_t flen = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, payload, 3, PROTO_TRUE);
    push_bytes(0, frame, flen);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL(WS_OP_BINARY, ws->opcode);
    TEST_ASSERT_EQUAL(3, (int)ws->payload_len);
    TEST_ASSERT_EQUAL(0x01, (int)ws->buf[0]);
    TEST_ASSERT_EQUAL(0x02, (int)ws->buf[1]);
    TEST_ASSERT_EQUAL(0x03, (int)ws->buf[2]);
}

void test_ws_parse_zero_length_unmasked_frame()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[2] = {0x81, 0x00};
    push_bytes(0, frame, 2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_zero_length_masked_frame()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[6] = {0x81, 0x80, 0x00, 0x00, 0x00, 0x00};
    push_bytes(0, frame, 6);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL(0, (int)ws->payload_len);
}

void test_ws_reject_unmasked_data_frame()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[5] = {0x81, 0x03, 'a', 'b', 'c'};
    push_bytes(0, frame, 5);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_reject_reserved_opcode()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[2] = {0x83, 0x80};
    push_bytes(0, frame, 2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_reject_fragmented_control_frame()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[2] = {0x09, 0x80};
    push_bytes(0, frame, 2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_reject_oversized_control_frame()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[2] = {0x89, (uint8_t)(0x80u | 126u)};
    push_bytes(0, frame, 2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_16bit_length_frame()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    static uint8_t payload[130];
    for (int i = 0; i < 130; i++)
    {
        payload[i] = (uint8_t)(i & 0xFF);
    }

    static uint8_t frame[142];
    size_t flen = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, payload, 130, PROTO_TRUE);

    push_bytes(0, frame, flen);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL(130, (int)ws->payload_len);
    TEST_ASSERT_EQUAL(0, (int)ws->buf[0]);
    TEST_ASSERT_EQUAL(129 & 0xFF, (int)ws->buf[129]);
}

void test_ws_parse_rsv1_set_closes_protocol()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[2] = {0xC1, 0x00};
    push_bytes(0, frame, 2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_rsv2_set_closes_protocol()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[2] = {0xA1, 0x00};
    push_bytes(0, frame, 2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_rsv3_set_closes_protocol()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[2] = {0x91, 0x00};
    push_bytes(0, frame, 2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_64bit_length_closes_too_big()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[10] = {0x81, 0xFF, 0, 0, 0, 0, 0, 0, 0, 1};
    push_bytes(0, frame, sizeof(frame));
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_oversized_16bit_length_closes_too_big()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    uint16_t big_len = (uint16_t)WS_FRAME_SIZE + 1;
    uint8_t frame[4] = {0x81, 0xFE, (uint8_t)(big_len >> 8), (uint8_t)(big_len)};
    push_bytes(0, frame, sizeof(frame));
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_fragment_start_waits_for_continuation()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[8] = {0x01, 0x82, 0, 0, 0, 0, 'H', 'i'};
    push_bytes(0, frame, sizeof(frame));
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_NOT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_NOT_EQUAL(WS_ERROR, ws->parse_state);
    TEST_ASSERT_TRUE(ws->fragmenting);
}

void test_ws_fragmented_message_reassembled()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    uint8_t f1[16], f2[16];
    size_t n1 = build_frame(f1, WS_OP_TEXT, PROTO_FALSE, (const uint8_t *)"He", 2, PROTO_TRUE);
    size_t n2 = build_frame(f2, WS_OP_CONTINUATION, PROTO_TRUE, (const uint8_t *)"llo", 3, PROTO_TRUE);

    push_bytes(0, f1, n1);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_TRUE(ws->fragmenting);

    push_bytes(0, f2, n2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL(WS_OP_TEXT, ws->opcode);
    TEST_ASSERT_EQUAL(5, (int)ws->payload_len);
    TEST_ASSERT_EQUAL_MEMORY("Hello", ws->buf, 5);
}

void test_ws_control_frame_interleaved_in_fragments()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    uint8_t f1[16], pf[16], f2[16];
    size_t n1 = build_frame(f1, WS_OP_TEXT, PROTO_FALSE, (const uint8_t *)"He", 2, PROTO_TRUE);
    size_t np = build_frame(pf, WS_OP_PING, PROTO_TRUE, (const uint8_t *)"x", 1, PROTO_TRUE);
    size_t n2 = build_frame(f2, WS_OP_CONTINUATION, PROTO_TRUE, (const uint8_t *)"llo", 3, PROTO_TRUE);

    push_bytes(0, f1, n1);
    push_bytes(0, pf, np);
    push_bytes(0, f2, n2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL(WS_OP_TEXT, ws->opcode);
    TEST_ASSERT_EQUAL(5, (int)ws->payload_len);
    TEST_ASSERT_EQUAL_MEMORY("Hello", ws->buf, 5);
}

void test_ws_fragment_accumulation_overflow_rejected()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    static uint8_t payload[WS_FRAME_SIZE];
    static uint8_t frame[WS_FRAME_SIZE + 8];
    for (int i = 0; i < WS_FRAME_SIZE; i++)
    {
        payload[i] = (uint8_t)(i & 0xFF);
    }

    size_t n1 = build_frame(frame, WS_OP_TEXT, PROTO_FALSE, payload, (uint16_t)WS_FRAME_SIZE, PROTO_TRUE);
    push_bytes(0, frame, n1);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_TRUE(ws->fragmenting);
    TEST_ASSERT_NOT_EQUAL(WS_ERROR, ws->parse_state);

    uint8_t one = 'x';
    size_t n2 = build_frame(frame, WS_OP_CONTINUATION, PROTO_TRUE, &one, 1, PROTO_TRUE);
    push_bytes(0, frame, n2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_continuation_without_start_rejected()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[8] = {0x80, 0x82, 0, 0, 0, 0, 'H', 'i'};
    push_bytes(0, frame, sizeof(frame));
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_new_data_frame_during_fragmentation_rejected()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    uint8_t f1[16], f2[16];
    size_t n1 = build_frame(f1, WS_OP_TEXT, PROTO_FALSE, (const uint8_t *)"He", 2, PROTO_TRUE);

    size_t n2 = build_frame(f2, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)"llo", 3, PROTO_TRUE);
    push_bytes(0, f1, n1);
    push_bytes(0, f2, n2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_parse_ping_auto_pong_resets_frame()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[10] = {0x89, 0x84, 0, 0, 0, 0, 'p', 'i', 'n', 'g'};
    push_bytes(0, frame, sizeof(frame));
    Ws.conn = ws;
    Ws.parse(Ws.internal);

    TEST_ASSERT_EQUAL(WS_HEADER1, ws->parse_state);
}

void test_ws_parse_pong_silently_ignored()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[8] = {0x8A, 0x82, 0, 0, 0, 0, 0, 0};
    push_bytes(0, frame, sizeof(frame));
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_HEADER1, ws->parse_state);
}

void test_ws_parse_close_marks_ws_closed()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[8] = {0x88, 0x82, 0, 0, 0, 0, 0x03, 0xE8};
    push_bytes(0, frame, sizeof(frame));
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_CLOSED, ws->parse_state);
}

void test_ws_parse_stops_at_frame_ready()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    const uint8_t p[] = {'A'};
    uint8_t f1[8], f2[8];
    size_t l1 = build_frame(f1, WS_OP_TEXT, PROTO_TRUE, p, 1, PROTO_TRUE);
    size_t l2 = build_frame(f2, WS_OP_TEXT, PROTO_TRUE, p, 1, PROTO_TRUE);

    push_bytes(0, f1, l1);
    push_bytes(0, f2, l2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);

    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
}

void test_ws_parse_stops_after_close()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    uint8_t close_frame[8] = {0x88, 0x82, 0, 0, 0, 0, 0x03, 0xE8};
    const uint8_t p[] = {'z'};
    uint8_t next[8];
    size_t nlen = build_frame(next, WS_OP_TEXT, PROTO_TRUE, p, 1, PROTO_TRUE);

    push_bytes(0, close_frame, sizeof(close_frame));
    push_bytes(0, next, nlen);
    Ws.conn = ws;
    Ws.parse(Ws.internal);

    TEST_ASSERT_EQUAL(WS_CLOSED, ws->parse_state);
}

void test_ws_reset_frame_clears_fields()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    ws->parse_state = WS_FRAME_READY;
    ws->payload_len = 10;
    ws->payload_idx = 10;
    ws->fin = PROTO_TRUE;
    ws->masked = PROTO_TRUE;
    ws->mask_key[0] = 0xAB;
    ws->buf[0] = 'X';
    ws->len64_count = 5;

    Ws.conn = ws;
    Ws.reset_frame(Ws.internal);

    TEST_ASSERT_EQUAL(WS_HEADER1, ws->parse_state);
    TEST_ASSERT_EQUAL(0, (int)ws->payload_len);
    TEST_ASSERT_EQUAL(0, (int)ws->payload_idx);
    TEST_ASSERT_FALSE(ws->fin);
    TEST_ASSERT_FALSE(ws->masked);
    TEST_ASSERT_EQUAL(0, (int)ws->mask_key[0]);
    TEST_ASSERT_EQUAL('\0', (char)ws->buf[0]);
}

void test_ws_feed_byte_unknown_parse_state_is_nop()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    ws->parse_state = (WsParseState)99;
    ws->payload_idx = 0;
    Ws.conn = ws;
    Ws.byte = 0x42;
    Ws.feed_byte(Ws.internal);
    TEST_ASSERT_EQUAL((WsParseState)99, ws->parse_state);
    TEST_ASSERT_EQUAL(0, (int)ws->payload_idx);
}

void test_ws_payload_ctl_buf_capacity_guard_direct()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    ws->opcode = WS_OP_PING;
    ws->parse_state = WS_PAYLOAD;
    ws->payload_len = 200;
    ws->payload_idx = sizeof(ws->ctl_buf) - 1;
    Ws.conn = ws;
    Ws.byte = 'X';
    Ws.feed_byte(Ws.internal);
    TEST_ASSERT_EQUAL(sizeof(ws->ctl_buf), (size_t)ws->payload_idx);
    TEST_ASSERT_EQUAL('\0', (char)ws->ctl_buf[sizeof(ws->ctl_buf) - 1]);
}

void test_ws_payload_data_buf_capacity_guard_direct()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    ws->opcode = WS_OP_BINARY;
    ws->parse_state = WS_PAYLOAD;
    ws->msg_len = WS_FRAME_SIZE;
    ws->payload_len = 10;
    ws->payload_idx = 0;
    Ws.conn = ws;
    Ws.byte = 'Y';
    Ws.feed_byte(Ws.internal);
    TEST_ASSERT_EQUAL(1, (int)ws->payload_idx);
    TEST_ASSERT_EQUAL('\0', (char)ws->buf[WS_FRAME_SIZE]);
}

void test_ws_parse_mask_applied_correctly()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;

    uint8_t frame[8] = {0x81, 0x81, 0x37, 0xFA, 0x21, 0x3D, 0x7F};
    push_bytes(0, frame, sizeof(frame));
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL('H', (char)ws->buf[0]);
}

void test_ws_text_invalid_utf8_rejected()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    const uint8_t bad[] = {0xC3, 0x28};
    uint8_t frame[16];
    size_t n = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, bad, 2, PROTO_TRUE);
    push_bytes(0, frame, n);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_text_valid_utf8_accepted()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    const uint8_t ok[] = {'h', 0xC3, 0xA9, 'l', 'l', 'o'};
    uint8_t frame[16];
    size_t n = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, ok, (uint16_t)sizeof(ok), PROTO_TRUE);
    push_bytes(0, frame, n);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL((int)sizeof(ok), (int)ws->payload_len);
}

void test_ws_binary_arbitrary_bytes_accepted()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    const uint8_t bin[] = {0xFF, 0xFE, 0x00, 0xC3, 0x28};
    uint8_t frame[16];
    size_t n = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, bin, (uint16_t)sizeof(bin), PROTO_TRUE);
    push_bytes(0, frame, n);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
}

void stress_ws_parse_reset_100_cycles()
{
    const char *text = "test";
    uint8_t frame[12];
    size_t flen = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)text, 4, PROTO_TRUE);
    for (int i = 0; i < 100; i++)
    {
        Ws.slot = 0;
        Ws.alloc(Ws.internal);
        WsConn *ws = Ws.found;
        TEST_ASSERT_NOT_NULL_MESSAGE(ws, "alloc failed");
        push_bytes(0, frame, flen);
        Ws.conn = ws;
        Ws.parse(Ws.internal);
        TEST_ASSERT_EQUAL_MESSAGE(WS_FRAME_READY, ws->parse_state, "not FRAME_READY");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(text, (const char *)ws->buf, "payload mismatch");
        Ws.slot = 0;
        Ws.free(Ws.internal);
        conn_pool[0].rx_head = conn_pool[0].rx_tail = 0;
    }
}

void stress_ws_alloc_free_pool_cycle()
{
    for (int cycle = 0; cycle < 50; cycle++)
    {
        Ws.slot = 0;
        Ws.alloc(Ws.internal);
        WsConn *w0 = Ws.found;
        Ws.slot = 1;
        Ws.alloc(Ws.internal);
        WsConn *w1 = Ws.found;
        TEST_ASSERT_NOT_NULL(w0);
        TEST_ASSERT_NOT_NULL(w1);
        Ws.slot = 2;
        Ws.alloc(Ws.internal);
        TEST_ASSERT_NULL(Ws.found);

        Ws.slot = 0;
        Ws.free(Ws.internal);
        Ws.slot = 0;
        Ws.alloc(Ws.internal);
        WsConn *w0b = Ws.found;
        TEST_ASSERT_NOT_NULL(w0b);
        TEST_ASSERT_EQUAL(0, (int)w0b->slot_id);

        Ws.slot = 0;
        Ws.free(Ws.internal);
        Ws.slot = 1;
        Ws.free(Ws.internal);
        TEST_ASSERT_FALSE(ws_pool[0].active);
        TEST_ASSERT_FALSE(ws_pool[1].active);
    }
}

void stress_ws_parse_incremental_byte_by_byte()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    const char *text = "Incremental";
    uint8_t frame[20];
    size_t flen = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)text, (uint16_t)strlen(text), PROTO_TRUE);
    for (size_t i = 0; i < flen; i++)
    {
        push_bytes(0, &frame[i], 1);
        Ws.conn = ws;
        Ws.parse(Ws.internal);
        if (i < flen - 1)
        {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(WS_ERROR, ws->parse_state, "WS_ERROR during valid incremental parse");
        }
    }
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL_STRING(text, (const char *)ws->buf);
}

void stress_ws_parse_max_payload()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    static uint8_t payload[WS_FRAME_SIZE];
    static uint8_t frame[WS_FRAME_SIZE + 8];

    for (int i = 0; i < WS_FRAME_SIZE; i++)
    {
        payload[i] = (uint8_t)(i & 0xFF);
    }

    size_t flen = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, payload, (uint16_t)WS_FRAME_SIZE, PROTO_TRUE);
    push_bytes(0, frame, flen);
    Ws.conn = ws;
    Ws.parse(Ws.internal);

    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL((int)WS_FRAME_SIZE, (int)ws->payload_len);
    TEST_ASSERT_EQUAL(0, (int)ws->buf[0]);
    TEST_ASSERT_EQUAL((int)((WS_FRAME_SIZE - 1) & 0xFF), (int)ws->buf[WS_FRAME_SIZE - 1]);
    TEST_ASSERT_EQUAL('\0', (char)ws->buf[WS_FRAME_SIZE]);
}

void stress_ws_parse_two_consecutive_frames()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    const char *t1 = "first";
    const char *t2 = "second";
    uint8_t f1[16], f2[16];
    size_t l1 = build_frame(f1, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)t1, (uint16_t)strlen(t1), PROTO_TRUE);
    size_t l2 = build_frame(f2, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)t2, (uint16_t)strlen(t2), PROTO_TRUE);

    push_bytes(0, f1, l1);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL_STRING(t1, (const char *)ws->buf);

    Ws.conn = ws;
    Ws.reset_frame(Ws.internal);
    push_bytes(0, f2, l2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL_STRING(t2, (const char *)ws->buf);
}

#if PROTOCORE_ENABLE_WS_DEFLATE

void test_ws_permessage_deflate_inbound()
{

    static const uint8_t comp[] = {242, 72, 205, 201, 201, 215, 81, 8, 207, 47, 202, 73, 81, 4, 0};
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    uint8_t frame[64];
    size_t n = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, comp, (uint16_t)sizeof(comp), PROTO_TRUE);
    frame[0] |= 0x40;

    push_bytes(0, frame, n);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_EQUAL_size_t(13, ws->msg_len);
    TEST_ASSERT_EQUAL_STRING("Hello, World!", (const char *)ws->buf);
}

void test_ws_rsv1_without_negotiation_closes()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_FALSE;
    uint8_t frame[16];
    size_t n = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)"x", 1, PROTO_TRUE);
    frame[0] |= 0x40;
    push_bytes(0, frame, n);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_permessage_deflate_outbound()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    const char *msg = "The quick brown fox. The quick brown fox. The quick brown fox.";
    uint16_t mlen = (uint16_t)strlen(msg);

    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_TEXT;
    Ws.frame.payload = (const uint8_t *)msg;
    Ws.frame.len = mlen;
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    tcp_capture_disable();

    const uint8_t *sent = (const uint8_t *)tcp_captured();
    size_t sent_len = tcp_captured_len();
    TEST_ASSERT_TRUE(sent_len >= 2);

    TEST_ASSERT_EQUAL_UINT8(0x80 | 0x40 | (uint8_t)WS_OP_TEXT, sent[0]);
    uint16_t plen = sent[1] & 0x7F;
    size_t hdr = 2;
    if (plen == 126)
    {
        plen = (uint16_t)((sent[2] << 8) | sent[3]);
        hdr = 4;
    }
    TEST_ASSERT_TRUE(plen < mlen);
    TEST_ASSERT_EQUAL_size_t(hdr + plen, sent_len);

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
    int rc = (int)Inflate.raw(comp, plen + 4, out, sizeof(out), &out_len, scr, sizeof(scr));
    TEST_ASSERT_EQUAL_INT(INFLATE_OK, rc);
    TEST_ASSERT_EQUAL_size_t(mlen, out_len);
    TEST_ASSERT_EQUAL_MEMORY(msg, out, mlen);
}

void test_ws_outbound_incompressible_not_flagged()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_TEXT;
    Ws.frame.payload = (const uint8_t *)"x";
    Ws.frame.len = 1;
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    tcp_capture_disable();
    const uint8_t *sent = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_TEXT, sent[0]);
    TEST_ASSERT_EQUAL_UINT8(1, sent[1] & 0x7F);
    TEST_ASSERT_EQUAL_UINT8('x', sent[2]);

    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_PONG;
    Ws.frame.payload = (const uint8_t *)"AAAAAAAAAAAAAAAA";
    Ws.frame.len = 16;
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    tcp_capture_disable();
    sent = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_PONG, sent[0]);
}

void test_ws_deflate_inflate_error_closes()
{
    static const uint8_t garbage[] = {0xFF, 0xFF, 0xFF};
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;
    uint8_t frame[32];
    size_t n = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, garbage, (uint16_t)sizeof(garbage), PROTO_TRUE);
    frame[0] |= 0x40;
    push_bytes(0, frame, n);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_permessage_deflate_inflate_overflow_closes()
{
    static const uint8_t comp[] = {114, 116, 28, 5, 163, 128, 250, 0, 0};
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    uint8_t frame[32];
    size_t n = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, comp, (uint16_t)sizeof(comp), PROTO_TRUE);
    frame[0] |= 0x40;
    push_bytes(0, frame, n);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

void test_ws_permessage_deflate_scratch_exhausted_closes()
{
    static const uint8_t comp[] = {242, 72, 205, 201, 201, 215, 81, 8, 207, 47, 202, 73, 81, 4, 0};
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    void *hog = protocore_plaintext_alloc(protocore_plaintext_capacity(), 1);
    TEST_ASSERT_NOT_NULL(hog);

    uint8_t frame[32];
    size_t n = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, comp, (uint16_t)sizeof(comp), PROTO_TRUE);
    frame[0] |= 0x40;
    push_bytes(0, frame, n);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);

    protocore_plaintext_reset();
}

static void feed_compressed_with_arena_leaving(size_t leave)
{
    static const uint8_t comp[] = {242, 72, 205, 201, 201, 215, 81, 8, 207, 47, 202, 73, 81, 4, 0};
    conn_pool[0].rx_head = conn_pool[0].rx_tail = 0;
    Ws.slot = 0;
    Ws.free(Ws.internal);
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    protocore_plaintext_reset();
    void *hog = protocore_plaintext_alloc(protocore_plaintext_capacity() - leave, 1);
    TEST_ASSERT_NOT_NULL(hog);

    uint8_t frame[32];
    size_t n = build_frame(frame, WS_OP_BINARY, PROTO_TRUE, comp, (uint16_t)sizeof(comp), PROTO_TRUE);
    frame[0] |= 0x40;
    push_bytes(0, frame, n);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
    protocore_plaintext_reset();
}

void test_ws_permessage_deflate_partial_scratch_failures()
{
    feed_compressed_with_arena_leaving(19 + 100);
    feed_compressed_with_arena_leaving(19 + 512 + 16);
}

void test_ws_pmd_negotiated_uncompressed_frame_accepted()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;
    const char *text = "plain";
    uint8_t frame[16];
    size_t n = build_frame(frame, WS_OP_TEXT, PROTO_TRUE, (const uint8_t *)text, (uint16_t)strlen(text), PROTO_TRUE);
    push_bytes(0, frame, n);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_FRAME_READY, ws->parse_state);
    TEST_ASSERT_FALSE(ws->msg_compressed);
    TEST_ASSERT_EQUAL_STRING("plain", (const char *)ws->buf);
}

void test_ws_outbound_binary_and_scratch_starved()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;
    Ws.frag_size = 0;
    Ws.set_frag_size(Ws.internal);

    static uint8_t bin[64];
    for (int i = 0; i < 64; i++)
    {
        bin[i] = (uint8_t)(i & 1);
    }
    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_BINARY;
    Ws.frame.payload = bin;
    Ws.frame.len = sizeof(bin);
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    const uint8_t *s = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | 0x40 | (uint8_t)WS_OP_BINARY, s[0]);
    tcp_capture_disable();

    protocore_plaintext_reset();
    void *hog = protocore_plaintext_alloc(protocore_plaintext_capacity(), 1);
    TEST_ASSERT_NOT_NULL(hog);
    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_BINARY;
    Ws.frame.payload = bin;
    Ws.frame.len = sizeof(bin);
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    s = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_BINARY, s[0]);
    tcp_capture_disable();

    protocore_plaintext_reset();
    hog = protocore_plaintext_alloc(protocore_plaintext_capacity() - (DEFLATE_SCRATCH_SIZE + 8), 1);
    TEST_ASSERT_NOT_NULL(hog);
    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_BINARY;
    Ws.frame.payload = bin;
    Ws.frame.len = sizeof(bin);
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    s = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_BINARY, s[0]);
    tcp_capture_disable();
    protocore_plaintext_reset();
}

void test_ws_outbound_pmd_zero_len_and_control()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;
    Ws.frag_size = 0;
    Ws.set_frag_size(Ws.internal);

    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_TEXT;
    Ws.frame.payload = NULL;
    Ws.frame.len = 0;
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    tcp_capture_disable();

    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_PING;
    Ws.frame.payload = (const uint8_t *)"AAAA";
    Ws.frame.len = 4;
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    tcp_capture_disable();
}

void test_ws_pmd_continuation_with_rsv1_rejected()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
    ws->pmd = PROTO_TRUE;

    uint8_t f1[16];
    size_t n1 = build_frame(f1, WS_OP_TEXT, PROTO_FALSE, (const uint8_t *)"He", 2, PROTO_TRUE);
    push_bytes(0, f1, n1);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_TRUE(ws->fragmenting);

    uint8_t f2[16];
    size_t n2 = build_frame(f2, WS_OP_CONTINUATION, PROTO_TRUE, (const uint8_t *)"llo", 3, PROTO_TRUE);
    f2[0] |= 0x40;
    push_bytes(0, f2, n2);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    TEST_ASSERT_EQUAL(WS_ERROR, ws->parse_state);
}

#endif

void test_ws_outbound_fragmentation()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    TEST_ASSERT_NOT_NULL(ws);
#if PROTOCORE_ENABLE_WS_DEFLATE
    ws->pmd = PROTO_FALSE;
#endif
    const uint8_t msg[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    Ws.frag_size = 4;
    Ws.set_frag_size(Ws.internal);
    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_BINARY;
    Ws.frame.payload = msg;
    Ws.frame.len = sizeof(msg);
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    tcp_capture_disable();
    Ws.frag_size = 0;
    Ws.set_frag_size(Ws.internal);

    const uint8_t *s = (const uint8_t *)tcp_captured();

    TEST_ASSERT_EQUAL_size_t(3 * 2 + 10, tcp_captured_len());
    TEST_ASSERT_EQUAL_UINT8(WS_OP_BINARY, s[0]);
    TEST_ASSERT_EQUAL_UINT8(4, s[1]);
    TEST_ASSERT_EQUAL_UINT8(1, s[2]);
    TEST_ASSERT_EQUAL_UINT8(WS_OP_CONTINUATION, s[6]);
    TEST_ASSERT_EQUAL_UINT8(4, s[7]);
    TEST_ASSERT_EQUAL_UINT8(5, s[8]);
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_CONTINUATION, s[12]);
    TEST_ASSERT_EQUAL_UINT8(2, s[13]);
    TEST_ASSERT_EQUAL_UINT8(9, s[14]);
    TEST_ASSERT_EQUAL_UINT8(10, s[15]);

    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_BINARY;
    Ws.frame.payload = msg;
    Ws.frame.len = sizeof(msg);
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    tcp_capture_disable();
    s = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_BINARY, s[0]);
    TEST_ASSERT_EQUAL_UINT8(10, s[1]);
}

void test_ws_send_frame_paths_and_parse_guard()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    Ws.frag_size = 0;
    Ws.set_frag_size(Ws.internal);
    uint8_t payload[200];
    for (int i = 0; i < 200; i++)
    {
        payload[i] = (uint8_t)i;
    }

    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_BINARY;
    Ws.frame.payload = payload;
    Ws.frame.len = 200;
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    const uint8_t *sent = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(126, sent[1]);
    TEST_ASSERT_EQUAL_UINT8(0, sent[2]);
    TEST_ASSERT_EQUAL_UINT8(200, sent[3]);
    tcp_capture_disable();

    Ws.frag_size = 64;
    Ws.set_frag_size(Ws.internal);
    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_TEXT;
    Ws.frame.payload = payload;
    Ws.frame.len = 200;
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    TEST_ASSERT_TRUE(tcp_captured_len() > 200);
    tcp_capture_disable();
    Ws.frag_size = 0;
    Ws.set_frag_size(Ws.internal);

    conn_pool[0].state = CONN_CLOSING;
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_TEXT;
    Ws.frame.payload = payload;
    Ws.frame.len = 10;
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_FALSE(Ws.ok);
    Ws.conn = ws;
    Ws.parse(Ws.internal);
    conn_pool[0].state = CONN_ACTIVE;
}

void test_ws_send_frame_header_write_failure()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    Ws.frag_size = 0;
    Ws.set_frag_size(Ws.internal);
    tcp_capture_reset();
    mock_send_fail_after(0);
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_TEXT;
    Ws.frame.payload = (const uint8_t *)"hi";
    Ws.frame.len = 2;
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_FALSE(Ws.ok);
    mock_send_fail_after(-1);
    tcp_capture_disable();
}

void test_ws_send_frame_payload_write_failure()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    Ws.frag_size = 0;
    Ws.set_frag_size(Ws.internal);
    tcp_capture_reset();
    mock_send_fail_after(1);
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_TEXT;
    Ws.frame.payload = (const uint8_t *)"hi";
    Ws.frame.len = 2;
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_FALSE(Ws.ok);
    mock_send_fail_after(-1);
    tcp_capture_disable();
}

void test_ws_send_frame_zero_length_payload()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    Ws.frag_size = 0;
    Ws.set_frag_size(Ws.internal);
    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_PONG;
    Ws.frame.payload = NULL;
    Ws.frame.len = 0;
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    TEST_ASSERT_EQUAL_size_t(2, tcp_captured_len());
    tcp_capture_disable();
}

void test_ws_send_frame_null_payload_with_nonzero_length()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    Ws.frag_size = 0;
    Ws.set_frag_size(Ws.internal);
    tcp_capture_reset();
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_PING;
    Ws.frame.payload = NULL;
    Ws.frame.len = 5;
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    TEST_ASSERT_EQUAL_size_t(2, tcp_captured_len());
    tcp_capture_disable();
}

void test_ws_send_frame_fits_within_frag_size_single_frame()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    Ws.frag_size = 100;
    Ws.set_frag_size(Ws.internal);
    tcp_capture_reset();
    const uint8_t msg[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_BINARY;
    Ws.frame.payload = msg;
    Ws.frame.len = sizeof(msg);
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_TRUE(Ws.ok);
    const uint8_t *s = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT8(0x80 | (uint8_t)WS_OP_BINARY, s[0]);
    TEST_ASSERT_EQUAL_UINT8(10, s[1]);
    tcp_capture_disable();
    Ws.frag_size = 0;
    Ws.set_frag_size(Ws.internal);
}

void test_ws_send_frame_fragmentation_mid_send_failure()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    Ws.frag_size = 4;
    Ws.set_frag_size(Ws.internal);
    const uint8_t msg[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    tcp_capture_reset();
    mock_send_fail_after(2);
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_BINARY;
    Ws.frame.payload = msg;
    Ws.frame.len = sizeof(msg);
    Ws.send_frame(Ws.internal);
    TEST_ASSERT_FALSE(Ws.ok);
    mock_send_fail_after(-1);
    tcp_capture_disable();
    Ws.frag_size = 0;
    Ws.set_frag_size(Ws.internal);
}

void test_ws_close_when_conn_inactive_skips_flush()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    conn_pool[0].state = CONN_CLOSING;
    Ws.conn = ws;
    Ws.frame.code = WS_CLOSE_NORMAL;
    Ws.close(Ws.internal);
    TEST_ASSERT_EQUAL(WS_CLOSED, ws->parse_state);
    conn_pool[0].state = CONN_ACTIVE;
}

void test_ws_ping_flush_skipped_when_conn_inactive()
{
    Ws.slot = 0;
    Ws.alloc(Ws.internal);
    WsConn *ws = Ws.found;
    uint8_t frame[6] = {0x89, 0x80, 0, 0, 0, 0};
    conn_pool[0].state = CONN_CLOSING;
    for (size_t i = 0; i < sizeof(frame); i++)
    {
        Ws.conn = ws;
        Ws.byte = frame[i];
        Ws.feed_byte(Ws.internal);
    }
    TEST_ASSERT_EQUAL(WS_HEADER1, ws->parse_state);
    conn_pool[0].state = CONN_ACTIVE;
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_sha1_empty_string);
    RUN_TEST(test_sha1_abc);
    RUN_TEST(test_sha1_rfc6455_handshake_key);
    RUN_TEST(test_sha1_different_inputs_different_digests);

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
    RUN_TEST(test_ws_parse_stops_after_close);
    RUN_TEST(test_ws_reset_frame_clears_fields);
    RUN_TEST(test_ws_feed_byte_unknown_parse_state_is_nop);
    RUN_TEST(test_ws_payload_ctl_buf_capacity_guard_direct);
    RUN_TEST(test_ws_payload_data_buf_capacity_guard_direct);
    RUN_TEST(test_ws_parse_mask_applied_correctly);
    RUN_TEST(test_ws_text_invalid_utf8_rejected);
    RUN_TEST(test_ws_text_valid_utf8_accepted);
    RUN_TEST(test_ws_binary_arbitrary_bytes_accepted);
#if PROTOCORE_ENABLE_WS_DEFLATE
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
