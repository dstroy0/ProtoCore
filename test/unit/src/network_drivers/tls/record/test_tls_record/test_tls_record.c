// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the TLS 1.3 record layer (network_drivers/tls/record/record.h).
//
// RFC 8448 sec 3 prints complete protected records from one real connection together with the
// traffic secrets they were sealed under. test_rfc8448_protected_records reseals three of them -
// the client Finished, the client's 50-octet application_data, and the client's close_notify - from
// nothing but those secrets and the plaintext the trace prints, and compares the whole record byte
// for byte. That pins the header, the TLSInnerPlaintext content-type trailer, the key and iv
// expansion of sec 7.3, and above all the sec 5.3 nonce: the alert is the second record under the
// application key, so it only matches if the sequence number advanced and entered the nonce.

#include "network_drivers/tls/record/record.h"
#include <string.h>

#include <unity.h>

static TlsRecordKeys g_keys;
static uint8_t g_out[512];

void setUp(void)
{
    memset(&g_keys, 0, sizeof(g_keys));
    memset(g_out, 0xAA, sizeof(g_out));
}
void tearDown(void)
{
}

// RFC 8448 sec 3 "derive secret 'tls13 c hs traffic'" - the client's handshake traffic secret.
static const uint8_t C_HS_SECRET[32] = {0xb3, 0xed, 0xdb, 0x12, 0x6e, 0x06, 0x7f, 0x35, 0xa7, 0x80, 0xb3,
                                        0xab, 0xf4, 0x5e, 0x2d, 0x8f, 0x3b, 0x1a, 0x95, 0x07, 0x38, 0xf5,
                                        0x2e, 0x96, 0x00, 0x74, 0x6a, 0x0e, 0x27, 0xa5, 0x5a, 0x21};
// Its "iv expanded", the sec 7.3 HKDF-Expand-Label(secret, "iv", "", 12).
static const uint8_t C_HS_IV[12] = {0x5b, 0xd3, 0xc7, 0x1b, 0x83, 0x6e, 0x0b, 0x76, 0xbb, 0x73, 0x26, 0x5f};

// "derive secret 'tls13 c ap traffic'" and its iv.
static const uint8_t C_AP_SECRET[32] = {0x9e, 0x40, 0x64, 0x6c, 0xe7, 0x9a, 0x7f, 0x9d, 0xc0, 0x5a, 0xf8,
                                        0x88, 0x9b, 0xce, 0x65, 0x52, 0x87, 0x5a, 0xfa, 0x0b, 0x06, 0xdf,
                                        0x00, 0x87, 0xf7, 0x92, 0xeb, 0xb7, 0xc1, 0x75, 0x04, 0xa5};
static const uint8_t C_AP_IV[12] = {0x5b, 0x78, 0x92, 0x3d, 0xee, 0x08, 0x57, 0x90, 0x33, 0xe5, 0x23, 0xd9};

// "{client} send handshake record", payload (36 octets) - the client Finished message.
static const uint8_t CLIENT_FINISHED[36] = {0x14, 0x00, 0x00, 0x20, 0xa8, 0xec, 0x43, 0x6d, 0x67, 0x76, 0x34, 0xae,
                                            0x52, 0x5a, 0xc1, 0xfc, 0xeb, 0xe1, 0x1a, 0x03, 0x9e, 0xc1, 0x76, 0x94,
                                            0xfa, 0xc6, 0xe9, 0x85, 0x27, 0xb6, 0x42, 0xf2, 0xed, 0xd5, 0xce, 0x61};
// and its complete record (58 octets).
static const uint8_t CLIENT_FINISHED_RECORD[58] = {
    0x17, 0x03, 0x03, 0x00, 0x35, 0x75, 0xec, 0x4d, 0xc2, 0x38, 0xcc, 0xe6, 0x0b, 0x29, 0x80,
    0x44, 0xa7, 0x1e, 0x21, 0x9c, 0x56, 0xcc, 0x77, 0xb0, 0x51, 0x7f, 0xe9, 0xb9, 0x3c, 0x7a,
    0x4b, 0xfc, 0x44, 0xd8, 0x7f, 0x38, 0xf8, 0x03, 0x38, 0xac, 0x98, 0xfc, 0x46, 0xde, 0xb3,
    0x84, 0xbd, 0x1c, 0xae, 0xac, 0xab, 0x68, 0x67, 0xd7, 0x26, 0xc4, 0x05, 0x46,
};

// "{client} send application_data record", payload (50 octets) and its complete record (72 octets).
static const uint8_t APP_PAYLOAD[50] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21,
    0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31,
};
static const uint8_t APP_RECORD[72] = {
    0x17, 0x03, 0x03, 0x00, 0x43, 0xa2, 0x3f, 0x70, 0x54, 0xb6, 0x2c, 0x94, 0xd0, 0xaf, 0xfa, 0xfe, 0x82, 0x28,
    0xba, 0x55, 0xcb, 0xef, 0xac, 0xea, 0x42, 0xf9, 0x14, 0xaa, 0x66, 0xbc, 0xab, 0x3f, 0x2b, 0x98, 0x19, 0xa8,
    0xa5, 0xb4, 0x6b, 0x39, 0x5b, 0xd5, 0x4a, 0x9a, 0x20, 0x44, 0x1e, 0x2b, 0x62, 0x97, 0x4e, 0x1f, 0x5a, 0x62,
    0x92, 0xa2, 0x97, 0x70, 0x14, 0xbd, 0x1e, 0x3d, 0xea, 0xe6, 0x3a, 0xee, 0xbb, 0x21, 0x69, 0x49, 0x15, 0xe4,
};

// "{client} send alert record", payload (2 octets) = close_notify, and its complete record.
static const uint8_t ALERT_PAYLOAD[2] = {0x01, 0x00};
static const uint8_t ALERT_RECORD[24] = {
    0x17, 0x03, 0x03, 0x00, 0x13, 0xc9, 0x87, 0x27, 0x60, 0x65, 0x56, 0x66,
    0xb7, 0x4d, 0x7f, 0xf1, 0x15, 0x3e, 0xfd, 0x6d, 0xb6, 0xd0, 0xb0, 0xe3,
};

// The trace's ServerHello TLSPlaintext record (95 octets): a 5-byte header over the 90-byte message.
static const uint8_t SH_RECORD[95] = {
    0x16, 0x03, 0x03, 0x00, 0x5a, 0x02, 0x00, 0x00, 0x56, 0x03, 0x03, 0xa6, 0xaf, 0x06, 0xa4, 0x12, 0x18, 0x60, 0xdc,
    0x5e, 0x6e, 0x60, 0x24, 0x9c, 0xd3, 0x4c, 0x95, 0x93, 0x0c, 0x8a, 0xc5, 0xcb, 0x14, 0x34, 0xda, 0xc1, 0x55, 0x77,
    0x2e, 0xd3, 0xe2, 0x69, 0x28, 0x00, 0x13, 0x01, 0x00, 0x00, 0x2e, 0x00, 0x33, 0x00, 0x24, 0x00, 0x1d, 0x00, 0x20,
    0xc9, 0x82, 0x88, 0x76, 0x11, 0x20, 0x95, 0xfe, 0x66, 0x76, 0x2b, 0xdb, 0xf7, 0xc6, 0x72, 0xe1, 0x56, 0xd6, 0xcc,
    0x25, 0x3b, 0x83, 0x3d, 0xf1, 0xdd, 0x69, 0xb1, 0xb0, 0x4e, 0x75, 0x1f, 0x0f, 0x00, 0x2b, 0x00, 0x02, 0x03, 0x04,
};

static void derive(const uint8_t *secret)
{
    TlsRecord.key.keys = &g_keys;
    TlsRecord.key.cipher = TLS_CIPHER_AES_128_GCM_SHA256;
    TlsRecord.key.secret = secret;
    TlsRecord.keys_derive(NULL);
}

static void derive384(const uint8_t *secret)
{
    TlsRecord.key.keys = &g_keys;
    TlsRecord.key.cipher = TLS_CIPHER_AES_256_GCM_SHA384;
    TlsRecord.key.secret = secret;
    TlsRecord.keys_derive(NULL);
}

static size_t protect(uint8_t type, const uint8_t *pt, size_t pt_len, size_t cap)
{
    TlsRecord.key.keys = &g_keys;
    TlsRecord.content_type = type;
    TlsRecord.sealed.pt = pt;
    TlsRecord.sealed.pt_len = pt_len;
    TlsRecord.out_args.out = g_out;
    TlsRecord.out_args.out_cap = cap;
    TlsRecord.protect(NULL);
    return TlsRecord.n;
}

static proto_bool unprotect(const uint8_t *rec, size_t rec_len, uint8_t *out, size_t cap, TlsCiphertext *info)
{
    TlsRecord.key.keys = &g_keys;
    TlsRecord.sealed.rec = rec;
    TlsRecord.sealed.rec_len = rec_len;
    TlsRecord.sealed.info = info;
    TlsRecord.out_args.out = out;
    TlsRecord.out_args.out_cap = cap;
    TlsRecord.unprotect(NULL);
    return TlsRecord.ok;
}

// The three records the RFC 8448 client sends, rebuilt from its traffic secrets alone.
void test_rfc8448_protected_records(void)
{
    // Record 0 under the client handshake key: the Finished message.
    derive(C_HS_SECRET);
    TEST_ASSERT_TRUE(g_keys.ready);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(C_HS_IV, g_keys.iv, sizeof(C_HS_IV));
    TEST_ASSERT_EQUAL_UINT64(0u, g_keys.seq);

    size_t n = protect(PROTOCORE_TLS_CT_HANDSHAKE, CLIENT_FINISHED, sizeof(CLIENT_FINISHED), sizeof(g_out));
    TEST_ASSERT_EQUAL_UINT(sizeof(CLIENT_FINISHED_RECORD), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLIENT_FINISHED_RECORD, g_out, sizeof(CLIENT_FINISHED_RECORD));
    TEST_ASSERT_EQUAL_UINT64(1u, g_keys.seq);

    // Record 0 under the client application key: 50 octets of application data.
    derive(C_AP_SECRET);
    TEST_ASSERT_TRUE(g_keys.ready);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(C_AP_IV, g_keys.iv, sizeof(C_AP_IV));

    n = protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD, sizeof(APP_PAYLOAD), sizeof(g_out));
    TEST_ASSERT_EQUAL_UINT(sizeof(APP_RECORD), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(APP_RECORD, g_out, sizeof(APP_RECORD));

    // Record 1 under the same key: close_notify. RFC 8446 sec 5.3 never sends the sequence number,
    // so this only matches if the counter advanced into the nonce.
    TEST_ASSERT_EQUAL_UINT64(1u, g_keys.seq);
    n = protect(PROTOCORE_TLS_CT_ALERT, ALERT_PAYLOAD, sizeof(ALERT_PAYLOAD), sizeof(g_out));
    TEST_ASSERT_EQUAL_UINT(sizeof(ALERT_RECORD), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ALERT_RECORD, g_out, sizeof(ALERT_RECORD));
    TEST_ASSERT_EQUAL_UINT64(2u, g_keys.seq);
}

// The receiving side of the same three records: the peer opens them with the same secrets and
// recovers the inner content type RFC 8446 sec 5.2 hides inside the encryption.
void test_rfc8448_records_open_again(void)
{
    uint8_t pt[128];
    TlsCiphertext info;

    derive(C_HS_SECRET);
    TEST_ASSERT_TRUE(unprotect(CLIENT_FINISHED_RECORD, sizeof(CLIENT_FINISHED_RECORD), pt, sizeof(pt), &info));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_TLS_CT_HANDSHAKE, info.content_type);
    TEST_ASSERT_EQUAL_UINT(sizeof(CLIENT_FINISHED), info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLIENT_FINISHED, pt, sizeof(CLIENT_FINISHED));
    TEST_ASSERT_EQUAL_UINT64(1u, g_keys.seq);

    derive(C_AP_SECRET);
    TEST_ASSERT_TRUE(unprotect(APP_RECORD, sizeof(APP_RECORD), pt, sizeof(pt), &info));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_TLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_EQUAL_UINT(sizeof(APP_PAYLOAD), info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(APP_PAYLOAD, pt, sizeof(APP_PAYLOAD));

    TEST_ASSERT_TRUE(unprotect(ALERT_RECORD, sizeof(ALERT_RECORD), pt, sizeof(pt), &info));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_TLS_CT_ALERT, info.content_type);
    TEST_ASSERT_EQUAL_UINT(sizeof(ALERT_PAYLOAD), info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ALERT_PAYLOAD, pt, sizeof(ALERT_PAYLOAD));
    TEST_ASSERT_EQUAL_UINT64(2u, g_keys.seq);

    // Opening the same record again is a different sequence number, so it no longer verifies: this
    // is what makes the counter part of the authentication rather than bookkeeping.
    TEST_ASSERT_FALSE(unprotect(ALERT_RECORD, sizeof(ALERT_RECORD), pt, sizeof(pt), &info));
    TEST_ASSERT_EQUAL_UINT64(2u, g_keys.seq); // a failed open does not advance the count
}

// RFC 8446 sec 5.1: "struct { ContentType type; ProtocolVersion legacy_record_version; uint16
// length; opaque fragment[TLSPlaintext.length]; } TLSPlaintext", with legacy_record_version 0x0303
// on everything after the first flight. The trace's ServerHello record is that shape.
void test_rfc8446_5_1_plaintext_record(void)
{
    TlsRecord.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecord.plain.fragment = SH_RECORD + PROTOCORE_TLS_PLAINTEXT_HDR_LEN;
    TlsRecord.plain.frag_len = sizeof(SH_RECORD) - PROTOCORE_TLS_PLAINTEXT_HDR_LEN;
    TlsRecord.out_args.out = g_out;
    TlsRecord.out_args.out_cap = sizeof(g_out);
    TlsRecord.plaintext_build(NULL);

    TEST_ASSERT_EQUAL_UINT(sizeof(SH_RECORD), TlsRecord.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SH_RECORD, g_out, sizeof(SH_RECORD));

    TlsPlaintext view;
    TlsRecord.sealed.rec = SH_RECORD;
    TlsRecord.sealed.rec_len = sizeof(SH_RECORD);
    TlsRecord.plain.view = &view;
    TlsRecord.plaintext_parse(NULL);

    TEST_ASSERT_EQUAL_UINT(sizeof(SH_RECORD), TlsRecord.n);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_TLS_CT_HANDSHAKE, view.content_type);
    TEST_ASSERT_EQUAL_UINT(90u, view.frag_len);
    TEST_ASSERT_EQUAL_HEX8(0x02, view.fragment[0]); // the ServerHello handshake type
}

// sec 5.1 gives the length field 16 bits, so a record longer than the stream has delivered is not
// yet a record; a header alone is not either. Neither is an error, but neither parses.
void test_plaintext_parse_waits_for_the_whole_record(void)
{
    TlsPlaintext view;
    TlsRecord.plain.view = &view;
    TlsRecord.out_args.out = g_out;
    TlsRecord.out_args.out_cap = sizeof(g_out);

    for (size_t take = 0; take < sizeof(SH_RECORD); take++)
    {
        TlsRecord.sealed.rec = SH_RECORD;
        TlsRecord.sealed.rec_len = take;
        TlsRecord.plaintext_parse(NULL);
        TEST_ASSERT_EQUAL_UINT(0u, TlsRecord.n);
    }

    // Extra bytes after the record belong to the next one: the parse reports only its own length.
    uint8_t stream[sizeof(SH_RECORD) + 8];
    memcpy(stream, SH_RECORD, sizeof(SH_RECORD));
    memset(stream + sizeof(SH_RECORD), 0x16, 8);
    TlsRecord.sealed.rec = stream;
    TlsRecord.sealed.rec_len = sizeof(stream);
    TlsRecord.plaintext_parse(NULL);
    TEST_ASSERT_EQUAL_UINT(sizeof(SH_RECORD), TlsRecord.n);
}

// sec 5.1: "implementations MUST NOT send zero-length fragments of Handshake ... types" but a
// zero-length plaintext record is still buildable for application_data, and sec 5.1 caps a fragment
// at 2^14 octets.
void test_plaintext_build_bounds(void)
{
    TlsRecord.content_type = PROTOCORE_TLS_CT_APPLICATION_DATA;
    TlsRecord.plain.fragment = NULL;
    TlsRecord.plain.frag_len = 0;
    TlsRecord.out_args.out = g_out;
    TlsRecord.out_args.out_cap = sizeof(g_out);
    TlsRecord.plaintext_build(NULL);
    TEST_ASSERT_EQUAL_UINT((size_t)PROTOCORE_TLS_PLAINTEXT_HDR_LEN, TlsRecord.n);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_TLS_CT_APPLICATION_DATA, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03, g_out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x03, g_out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[4]);

    // A destination one octet short writes nothing.
    TlsRecord.plain.fragment = APP_PAYLOAD;
    TlsRecord.plain.frag_len = sizeof(APP_PAYLOAD);
    TlsRecord.out_args.out_cap = PROTOCORE_TLS_PLAINTEXT_HDR_LEN + sizeof(APP_PAYLOAD) - 1;
    TlsRecord.plaintext_build(NULL);
    TEST_ASSERT_EQUAL_UINT(0u, TlsRecord.n);

    TlsRecord.out_args.out_cap = PROTOCORE_TLS_PLAINTEXT_HDR_LEN + sizeof(APP_PAYLOAD);
    TlsRecord.plaintext_build(NULL);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_TLS_PLAINTEXT_HDR_LEN + sizeof(APP_PAYLOAD), TlsRecord.n);

    // sec 5.1: "The length MUST NOT exceed 2^14 bytes."
    TlsRecord.plain.frag_len = PROTOCORE_TLS_MAX_PLAINTEXT + 1;
    TlsRecord.out_args.out_cap = sizeof(g_out);
    TlsRecord.plaintext_build(NULL);
    TEST_ASSERT_EQUAL_UINT(0u, TlsRecord.n);
}

// RFC 8446 sec 5.2: the TLSCiphertext header carries "opaque_type: The outer opaque_type field ...
// is always set to the value 23 (application_data)", and the true type rides inside the encryption.
void test_rfc8446_5_2_outer_type_is_always_application_data(void)
{
    derive(C_AP_SECRET);
    static const uint8_t MSG[4] = {0xde, 0xad, 0xbe, 0xef};

    for (uint8_t type = PROTOCORE_TLS_CT_CHANGE_CIPHER_SPEC; type <= PROTOCORE_TLS_CT_APPLICATION_DATA; type++)
    {
        size_t n = protect(type, MSG, sizeof(MSG), sizeof(g_out));
        TEST_ASSERT_EQUAL_UINT(PROTOCORE_TLS_PLAINTEXT_HDR_LEN + sizeof(MSG) + 1 + PROTOCORE_TLS_TAG_LEN, n);
        TEST_ASSERT_EQUAL_HEX8(PROTOCORE_TLS_CT_APPLICATION_DATA, g_out[0]);
        TEST_ASSERT_EQUAL_HEX8(0x03, g_out[1]);
        TEST_ASSERT_EQUAL_HEX8(0x03, g_out[2]);
        TEST_ASSERT_EQUAL_UINT(n - PROTOCORE_TLS_PLAINTEXT_HDR_LEN, ((size_t)g_out[3] << 8) | g_out[4]);
        // The sealed body reveals nothing of the real type in the clear.
        TEST_ASSERT_NOT_EQUAL(0, memcmp(g_out + PROTOCORE_TLS_PLAINTEXT_HDR_LEN, MSG, sizeof(MSG)));
    }
}

// RFC 8446 sec 5.2: "the receiving implementation scans the field from the end toward the beginning
// until it finds a non-zero octet. This non-zero octet is the content type"; the zeros before it
// are padding and are not part of the content.
void test_rfc8446_5_2_padding_is_stripped(void)
{
    // Sealing "abc" || 0x17 with an outer content type of 0x00 lays down the inner plaintext
    // "abc" || application_data || 0x00 - the same octets a peer that padded by one would send.
    static const uint8_t INNER[4] = {'a', 'b', 'c', PROTOCORE_TLS_CT_APPLICATION_DATA};
    derive(C_AP_SECRET);
    size_t n = protect(0x00, INNER, sizeof(INNER), sizeof(g_out));
    TEST_ASSERT_TRUE(n > 0);

    uint8_t rec[128];
    memcpy(rec, g_out, n);

    uint8_t pt[128];
    TlsCiphertext info;
    derive(C_AP_SECRET); // fresh keys: the peer opens this as its record 0
    TEST_ASSERT_TRUE(unprotect(rec, n, pt, sizeof(pt), &info));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_TLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_EQUAL_UINT(3u, info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("abc", pt, 3);
}

// RFC 8446 sec 5.4: "Implementations MUST NOT send Handshake and Alert records that have a
// zero-length TLSInnerPlaintext.content", and "If the resulting scan finds no non-zero octet ... the
// receiver MUST terminate the connection with an 'unexpected_message' alert." Both directions here.
void test_rfc8446_5_4_zero_length_inner_content(void)
{
    derive(C_AP_SECRET);
    TEST_ASSERT_EQUAL_UINT(0u, protect(PROTOCORE_TLS_CT_HANDSHAKE, NULL, 0, sizeof(g_out)));
    TEST_ASSERT_EQUAL_UINT(0u, protect(PROTOCORE_TLS_CT_ALERT, NULL, 0, sizeof(g_out)));
    TEST_ASSERT_EQUAL_UINT64(0u, g_keys.seq); // a refused record does not consume a sequence number

    // application_data may be zero length: that is how a sender pads the stream.
    size_t n = protect(PROTOCORE_TLS_CT_APPLICATION_DATA, NULL, 0, sizeof(g_out));
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_TLS_PLAINTEXT_HDR_LEN + 1 + PROTOCORE_TLS_TAG_LEN, n);

    uint8_t rec[64];
    memcpy(rec, g_out, n);
    uint8_t pt[64];
    TlsCiphertext info;
    derive(C_AP_SECRET);
    TEST_ASSERT_TRUE(unprotect(rec, n, pt, sizeof(pt), &info));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_TLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_EQUAL_UINT(0u, info.pt_len);

    // An inner plaintext that is all zeros names no content type at all: sealing 0x00 with an outer
    // type of 0x00 produces exactly that record, and opening it must fail.
    static const uint8_t ZERO = 0x00;
    derive(C_AP_SECRET);
    n = protect(0x00, &ZERO, 1, sizeof(g_out));
    TEST_ASSERT_TRUE(n > 0);
    memcpy(rec, g_out, n);
    derive(C_AP_SECRET);
    TEST_ASSERT_FALSE(unprotect(rec, n, pt, sizeof(pt), &info));
    TEST_ASSERT_EQUAL_UINT64(0u, g_keys.seq);
}

// RFC 8446 sec 5.2 names the record header as the AEAD's additional data, so every octet of the
// record is covered. A single flipped bit anywhere in it must be refused, and the sequence number
// must not move: a peer that could burn a counter with garbage would desynchronize the connection.
void test_a_tampered_record_is_refused(void)
{
    uint8_t rec[sizeof(APP_RECORD)];
    uint8_t pt[128];
    TlsCiphertext info;

    for (size_t i = 0; i < sizeof(APP_RECORD); i++)
    {
        memcpy(rec, APP_RECORD, sizeof(rec));
        rec[i] ^= 0x01;
        derive(C_AP_SECRET);
        TEST_ASSERT_FALSE_MESSAGE(unprotect(rec, sizeof(rec), pt, sizeof(pt), &info), "a flipped bit must not verify");
        TEST_ASSERT_EQUAL_UINT64(0u, g_keys.seq);
    }
}

// A body shorter than the tag, a length field past the buffer, and a destination too small are all
// refused before the AEAD runs.
void test_malformed_ciphertext_is_refused(void)
{
    uint8_t pt[128];
    TlsCiphertext info;
    derive(C_AP_SECRET);

    // Fewer bytes than the header.
    TEST_ASSERT_FALSE(unprotect(APP_RECORD, PROTOCORE_TLS_PLAINTEXT_HDR_LEN - 1, pt, sizeof(pt), &info));
    // The declared body runs past what arrived.
    TEST_ASSERT_FALSE(unprotect(APP_RECORD, sizeof(APP_RECORD) - 1, pt, sizeof(pt), &info));

    // A body of exactly the tag length carries no inner plaintext.
    static const uint8_t TAG_ONLY[PROTOCORE_TLS_PLAINTEXT_HDR_LEN + PROTOCORE_TLS_TAG_LEN] = {
        0x17, 0x03, 0x03, 0x00, PROTOCORE_TLS_TAG_LEN,
    };
    TEST_ASSERT_FALSE(unprotect(TAG_ONLY, sizeof(TAG_ONLY), pt, sizeof(pt), &info));

    // A destination too small for the recovered plaintext.
    TEST_ASSERT_FALSE(unprotect(APP_RECORD, sizeof(APP_RECORD), pt, 4, &info));
    TEST_ASSERT_EQUAL_UINT64(0u, g_keys.seq);

    // A destination too small for the sealed record.
    TEST_ASSERT_EQUAL_UINT(
        0u, protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD, sizeof(APP_PAYLOAD), sizeof(APP_RECORD) - 1));
    TEST_ASSERT_EQUAL_UINT(sizeof(APP_RECORD), protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD,
                                                       sizeof(APP_PAYLOAD), sizeof(APP_RECORD)));
}

// An unkeyed direction protects and unprotects nothing: the record layer fails closed rather than
// emitting a record in the clear under a header that claims it is encrypted.
void test_unkeyed_direction_fails_closed(void)
{
    uint8_t pt[128];
    TlsCiphertext info;
    memset(&g_keys, 0, sizeof(g_keys));
    TEST_ASSERT_FALSE(g_keys.ready);

    TEST_ASSERT_EQUAL_UINT(0u,
                           protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD, sizeof(APP_PAYLOAD), sizeof(g_out)));
    TEST_ASSERT_FALSE(unprotect(APP_RECORD, sizeof(APP_RECORD), pt, sizeof(pt), &info));

    // Wiping a keyed direction returns it to that state, and resets the counter (sec 5.3).
    derive(C_AP_SECRET);
    TEST_ASSERT_TRUE(g_keys.ready);
    (void)protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD, sizeof(APP_PAYLOAD), sizeof(g_out));
    TEST_ASSERT_EQUAL_UINT64(1u, g_keys.seq);

    TlsRecord.key.keys = &g_keys;
    TlsRecord.keys_wipe(NULL);
    TEST_ASSERT_FALSE(g_keys.ready);
    TEST_ASSERT_EQUAL_UINT64(0u, g_keys.seq);
    for (size_t i = 0; i < sizeof(g_keys.iv); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, g_keys.iv[i]);
    }
    TEST_ASSERT_EQUAL_UINT(0u,
                           protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD, sizeof(APP_PAYLOAD), sizeof(g_out)));
}

// sec 5.3: "Each sequence number is set to zero at the beginning of a connection and whenever the
// key is changed", so a rederive starts a new key generation and reproduces record 0 exactly.
void test_rederiving_restarts_the_sequence(void)
{
    derive(C_AP_SECRET);
    size_t first = protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD, sizeof(APP_PAYLOAD), sizeof(g_out));
    uint8_t saved[sizeof(APP_RECORD)];
    memcpy(saved, g_out, first);

    (void)protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD, sizeof(APP_PAYLOAD), sizeof(g_out));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(saved, g_out, first)); // record 1 differs: the nonce moved

    derive(C_AP_SECRET);
    TEST_ASSERT_EQUAL_UINT64(0u, g_keys.seq);
    size_t again = protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD, sizeof(APP_PAYLOAD), sizeof(g_out));
    TEST_ASSERT_EQUAL_UINT(first, again);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(saved, g_out, first);
}

// ---- TLS_AES_256_GCM_SHA384 (0x1302) --------------------------------------
//
// RFC 8448's trace is 0x1301 throughout, so the cases above pin that suite and these pin the other.
// The AEAD itself is AES-256-GCM, already vector-tested against NIST/McGrew in its own suite; what is
// new here is the record layer picking it and expanding sec 7.3's key and iv at SHA-384.

// A 48-octet traffic secret, and the "key" and "iv" HKDF-Expand-Label produces from it at SHA-384.
// Same secret and same answers as test/vectors/openssl_hkdf_sha384_label.json rows 7 and 8, which
// openssl produced (tools/harness.py crypto hkdf384).
static const uint8_t AP_SECRET_384[48] = {0x03, 0x0a, 0x11, 0x18, 0x1f, 0x26, 0x2d, 0x34, 0x3b, 0x42, 0x49, 0x50,
                                          0x57, 0x5e, 0x65, 0x6c, 0x73, 0x7a, 0x81, 0x88, 0x8f, 0x96, 0x9d, 0xa4,
                                          0xab, 0xb2, 0xb9, 0xc0, 0xc7, 0xce, 0xd5, 0xdc, 0xe3, 0xea, 0xf1, 0xf8,
                                          0xff, 0x06, 0x0d, 0x14, 0x1b, 0x22, 0x29, 0x30, 0x37, 0x3e, 0x45, 0x4c};
static const uint8_t AP_IV_384[12] = {0x56, 0xcb, 0x24, 0xb5, 0xcf, 0x79, 0xfc, 0xc2, 0x21, 0xef, 0x63, 0xfc};

// The write IV the record layer expands must be the one openssl expands from the same secret. The
// key is not readable back - key_init consumes it and only the schedule stays resident - so the iv
// is the term this can compare directly, and the round trip below covers the key.
void test_sha384_suite_expands_the_published_iv(void)
{
    derive384(AP_SECRET_384);
    TEST_ASSERT_TRUE(g_keys.ready);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(AP_IV_384, g_keys.iv, sizeof(AP_IV_384));
    TEST_ASSERT_EQUAL_UINT64(0u, g_keys.seq);
}

// A record sealed under 0x1302 opens back to the same plaintext and inner content type.
void test_sha384_suite_round_trips_a_record(void)
{
    derive384(AP_SECRET_384);
    size_t n = protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD, sizeof(APP_PAYLOAD), sizeof(g_out));
    TEST_ASSERT_TRUE(n > sizeof(APP_PAYLOAD));

    uint8_t sealed[512];
    memcpy(sealed, g_out, n);

    uint8_t opened[512];
    TlsCiphertext info;
    derive384(AP_SECRET_384); // a fresh generation: the reader's sequence starts at zero too
    TEST_ASSERT_TRUE(unprotect(sealed, n, opened, sizeof(opened), &info));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_TLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_EQUAL_UINT(sizeof(APP_PAYLOAD), info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(APP_PAYLOAD, opened, sizeof(APP_PAYLOAD));
}

// The two suites are different keys over different AEADs, so a record sealed under one must not open
// under the other. Without this a build that ignored TlsRecordKeys::cipher would still round-trip.
void test_the_two_suites_do_not_open_each_other(void)
{
    derive384(AP_SECRET_384);
    size_t n384 = protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD, sizeof(APP_PAYLOAD), sizeof(g_out));
    uint8_t sealed384[512];
    memcpy(sealed384, g_out, n384);

    derive(C_AP_SECRET);
    size_t n256 = protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD, sizeof(APP_PAYLOAD), sizeof(g_out));
    uint8_t sealed256[512];
    memcpy(sealed256, g_out, n256);

    uint8_t opened[512];
    TlsCiphertext info;

    derive(C_AP_SECRET);
    TEST_ASSERT_FALSE(unprotect(sealed384, n384, opened, sizeof(opened), &info));

    derive384(AP_SECRET_384);
    TEST_ASSERT_FALSE(unprotect(sealed256, n256, opened, sizeof(opened), &info));

    // And the two seals of the same plaintext are not the same bytes.
    TEST_ASSERT_TRUE(n384 != n256 || memcmp(sealed384, sealed256, n384) != 0);
}

// The suite rides on the key, so wiping one leaves it refusing exactly as the other does.
void test_sha384_suite_fails_closed_when_unkeyed(void)
{
    derive384(AP_SECRET_384);
    TlsRecord.key.keys = &g_keys;
    TlsRecord.keys_wipe(NULL);
    TEST_ASSERT_FALSE(g_keys.ready);
    TEST_ASSERT_EQUAL_UINT(0u,
                           protect(PROTOCORE_TLS_CT_APPLICATION_DATA, APP_PAYLOAD, sizeof(APP_PAYLOAD), sizeof(g_out)));
}
