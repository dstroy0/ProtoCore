// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Thread spinel / HDLC-lite framing codec (services/radio/thread/thread.h).
//
// RFC 1662 Appendix C states the FCS-16 was designed so that running it over a frame that already
// carries its own FCS yields one fixed pattern, the "good FCS" 0xf0b8. test_rfc1662_good_fcs_residue
// is the load-bearing case: it is the receiver's own acceptance test written from the RFC's number,
// not from this CRC's output, so an FCS with the wrong init, the wrong reflection, or the wrong
// final XOR cannot pass it. RFC 1662 sec 4.2 supplies the escape table verbatim, and the spinel
// protocol reference supplies the packed-integer example and the header bit layout.

#include "services/radio/thread/thread.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The nine ASCII octets every CRC catalogue entry publishes its check value over.
static const uint8_t CHECK9[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

// CRC-16/X-25, the HDLC FCS: catalogue check value 0x906E over "123456789".
void test_x25_catalog_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x906Eu, protocore_spinel_fcs(CHECK9, sizeof(CHECK9)));
}

// RFC 1662 Appendix C: the receiver runs the FCS over the data AND the transmitted FCS field and
// gets the "good FCS" 0xf0b8. RFC 1662 sec C.2 complements the FCS before transmitting it, which
// CRC-16/X-25 folds in as its final XOR, so this module's fcs() over the same span returns the
// complement of that constant: 0xf0b8 XOR 0xffff = 0x0f47.
void test_rfc1662_good_fcs_residue(void)
{
    static const uint8_t PAYLOADS[4][6] = {
        {0x80, 0x02, 0x02, 0x00, 0x00, 0x00},
        {0x81, 0x06, 0x01, 0x04, 0x03, 0x00},
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    for (size_t i = 0; i < 4; i++)
    {
        uint8_t framed[8];
        const uint16_t fcs = protocore_spinel_fcs(PAYLOADS[i], 6);
        memcpy(framed, PAYLOADS[i], 6);
        framed[6] = (uint8_t)(fcs & 0xFF); // low byte first on the wire
        framed[7] = (uint8_t)(fcs >> 8);
        TEST_ASSERT_EQUAL_HEX16(0x0F47u, protocore_spinel_fcs(framed, 8));
    }
}

// RFC 1662 sec 4.2 prints the encodings: 0x7e -> 7d 5e, 0x7d -> 7d 5d, 0x11 -> 7d 31, 0x13 -> 7d 33.
// Those four are exactly the reserved set HDLC-lite stuffs.
void test_rfc1662_escape_table(void)
{
    static const uint8_t RESERVED[4] = {0x7E, 0x7D, 0x11, 0x13};
    static const uint8_t WANT[8] = {0x7D, 0x5E, 0x7D, 0x5D, 0x7D, 0x31, 0x7D, 0x33};
    uint8_t out[32];
    const uint16_t n = protocore_spinel_frame_encode(RESERVED, 4, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 8);
    TEST_ASSERT_EQUAL_MEMORY(WANT, out, 8);
    TEST_ASSERT_EQUAL_HEX8(HDLC_FLAG, out[n - 1]); // the delimiter is never stuffed
    for (uint16_t i = 0; i + 1 < n; i++)
    {
        TEST_ASSERT_TRUE(out[i] != HDLC_FLAG); // and never appears inside the frame
    }
}

// Encode then decode returns the payload unchanged, and reports the whole frame consumed.
void test_frame_round_trip(void)
{
    static const uint8_t PAYLOAD[9] = {0x81, 0x06, 0x01, 0x7E, 0x7D, 0x11, 0x13, 0x00, 0xAA};
    uint8_t frame[64];
    const uint16_t n = protocore_spinel_frame_encode(PAYLOAD, sizeof(PAYLOAD), frame, sizeof(frame));
    TEST_ASSERT_TRUE(n > 0);

    uint8_t back[64];
    uint16_t back_len = 0;
    TEST_ASSERT_EQUAL_INT((int)n, protocore_spinel_frame_decode(frame, n, back, sizeof(back), &back_len));
    TEST_ASSERT_EQUAL_UINT16(sizeof(PAYLOAD), back_len);
    TEST_ASSERT_EQUAL_MEMORY(PAYLOAD, back, sizeof(PAYLOAD));
}

// One flipped bit anywhere in the frame body must fail the FCS.
void test_decode_rejects_a_corrupted_frame(void)
{
    static const uint8_t PAYLOAD[5] = {0x80, 0x02, 0x02, 0x01, 0x02};
    uint8_t frame[32];
    const uint16_t n = protocore_spinel_frame_encode(PAYLOAD, sizeof(PAYLOAD), frame, sizeof(frame));
    TEST_ASSERT_TRUE(n > 0);

    uint8_t back[32];
    uint16_t back_len = 0;
    for (uint16_t i = 0; i + 1 < n; i++)
    {
        uint8_t bad[32];
        memcpy(bad, frame, n);
        bad[i] = (uint8_t)(bad[i] ^ 0x01);
        if (bad[i] == HDLC_FLAG || bad[i] == HDLC_ESCAPE)
        {
            continue; // a flip that lands on a delimiter changes the framing, not the FCS
        }
        TEST_ASSERT_EQUAL_INT(-1, protocore_spinel_frame_decode(bad, n, back, sizeof(back), &back_len));
    }
}

// Framing faults are told apart from "need more bytes": no flag yet is 0, a broken frame is -1.
void test_decode_framing_faults(void)
{
    uint8_t back[64];
    uint16_t back_len = 0;

    static const uint8_t NO_FLAG[4] = {0x01, 0x02, 0x03, 0x04};
    TEST_ASSERT_EQUAL_INT(0, protocore_spinel_frame_decode(NO_FLAG, sizeof(NO_FLAG), back, sizeof(back), &back_len));

    static const uint8_t DANGLING[3] = {0x01, HDLC_ESCAPE, HDLC_FLAG};
    TEST_ASSERT_EQUAL_INT(-1, protocore_spinel_frame_decode(DANGLING, sizeof(DANGLING), back, sizeof(back), &back_len));

    static const uint8_t TOO_SHORT[2] = {0x01, HDLC_FLAG}; // one byte cannot hold a two-byte FCS
    TEST_ASSERT_EQUAL_INT(-1,
                          protocore_spinel_frame_decode(TOO_SHORT, sizeof(TOO_SHORT), back, sizeof(back), &back_len));

    // A payload that does not fit the caller's buffer is refused rather than truncated into it.
    static const uint8_t PAYLOAD[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t frame[32];
    const uint16_t n = protocore_spinel_frame_encode(PAYLOAD, sizeof(PAYLOAD), frame, sizeof(frame));
    TEST_ASSERT_EQUAL_INT(-1, protocore_spinel_frame_decode(frame, n, back, 4, &back_len));
}

// Encode refuses a payload past the configured maximum and a buffer with no room for the flag.
void test_encode_bounds(void)
{
    uint8_t big[PROTOCORE_THREAD_MAX_DATA + 1];
    memset(big, 0x41, sizeof(big));
    uint8_t out[PROTOCORE_THREAD_MAX_DATA * 2 + 8];
    TEST_ASSERT_EQUAL_UINT16(0, protocore_spinel_frame_encode(big, sizeof(big), out, sizeof(out)));

    // The frame is all-or-nothing: one octet short of the exact length writes nothing.
    static const uint8_t SMALL[4] = {1, 2, 3, 4};
    const uint16_t exact = protocore_spinel_frame_encode(SMALL, sizeof(SMALL), out, sizeof(out));
    TEST_ASSERT_TRUE(exact >= 7); // 4 payload + FCS(2) + flag, more if the FCS needs stuffing
    TEST_ASSERT_EQUAL_UINT16(0, protocore_spinel_frame_encode(SMALL, sizeof(SMALL), out, (uint16_t)(exact - 1)));
    TEST_ASSERT_EQUAL_UINT16(exact, protocore_spinel_frame_encode(SMALL, sizeof(SMALL), out, exact));
}

// The spinel reference gives one worked packed-integer example: 1337 => 0x0539 => [39 0A] => [B9 0A].
// The rest are the boundaries of the 7-bits-per-byte, little-endian, high-bit-continues definition:
//   127  = 0b1111111            -> one byte, no continuation           -> 7F
//   128  = 0b10000000           -> low 7 = 0, rest = 1                 -> 80 01
//   16383 = 0b11111111111111    -> low 7 = 0x7F, rest = 0x7F           -> FF 7F
//   16384 = 0b100000000000000   -> low 7 = 0, next 7 = 0, rest = 1     -> 80 80 01
//   0xFFFFFFFF = 32 ones        -> 4 full groups of 7 plus 4 bits left -> FF FF FF FF 0F
void test_spinel_packed_uint_vectors(void)
{
    struct
    {
        uint32_t value;
        uint8_t len;
        uint8_t bytes[5];
    } static const CASES[] = {
        {0u, 1, {0x00}},
        {1u, 1, {0x01}},
        {127u, 1, {0x7F}},
        {128u, 2, {0x80, 0x01}},
        {129u, 2, {0x81, 0x01}},
        {1337u, 2, {0xB9, 0x0A}},
        {16383u, 2, {0xFF, 0x7F}},
        {16384u, 3, {0x80, 0x80, 0x01}},
        {16385u, 3, {0x81, 0x80, 0x01}},
        {0xFFFFFFFFu, 5, {0xFF, 0xFF, 0xFF, 0xFF, 0x0F}},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t out[5];
        TEST_ASSERT_EQUAL_UINT8(CASES[i].len, protocore_spinel_pack_uint(CASES[i].value, out, sizeof(out)));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(CASES[i].bytes, out, CASES[i].len);

        uint32_t back = 0;
        TEST_ASSERT_EQUAL_INT(CASES[i].len, protocore_spinel_unpack_uint(out, CASES[i].len, &back));
        TEST_ASSERT_EQUAL_UINT32(CASES[i].value, back);
    }
}

// A truncated packed integer asks for more bytes; one that needs a sixth group cannot be a uint32.
void test_spinel_packed_uint_faults(void)
{
    static const uint8_t TRUNCATED[2] = {0x80, 0x80}; // both continue, none terminates
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_INT(0, protocore_spinel_unpack_uint(TRUNCATED, sizeof(TRUNCATED), &v));

    static const uint8_t OVERFLOWS[6] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x01};
    TEST_ASSERT_EQUAL_INT(-1, protocore_spinel_unpack_uint(OVERFLOWS, sizeof(OVERFLOWS), &v));

    // A four-byte buffer cannot hold the five-byte encoding of a 32-bit value.
    uint8_t out[4];
    TEST_ASSERT_EQUAL_UINT8(0, protocore_spinel_pack_uint(0xFFFFFFFFu, out, sizeof(out)));
}

// The spinel header byte is FLG(2) | NLI(2) | TID(4), most significant first, with FLG fixed at 0b10.
void test_spinel_header_bit_layout(void)
{
    for (uint8_t iid = 0; iid < 4; iid++)
    {
        for (uint8_t tid = 0; tid < 16; tid++)
        {
            const uint8_t h = protocore_spinel_header(iid, tid);
            TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)(h >> 6)); // FLG == 0b10
            TEST_ASSERT_EQUAL_UINT8(iid, protocore_spinel_header_iid(h));
            TEST_ASSERT_EQUAL_UINT8(tid, protocore_spinel_header_tid(h));
        }
    }
    // Interface 0, transaction 1: 0b10 000 1 -> 0x81.
    TEST_ASSERT_EQUAL_HEX8(0x81, protocore_spinel_header(0, 1));
    // Interface 2, transaction 15: 0b10 | 10 | 1111 -> 0xAF.
    TEST_ASSERT_EQUAL_HEX8(0xAF, protocore_spinel_header(2, 15));
}

// A PROP_VALUE_GET of PROTOCOL_VERSION is header | CMD | PROP with both integers packed. Both fit a
// single byte here, so the payload is literally 81 02 01 and the parse reports a value offset of 3.
void test_spinel_command_build_and_parse(void)
{
    static const uint8_t WANT[3] = {0x81, SPINEL_CMD_PROP_VALUE_GET, SPINEL_PROP_PROTOCOL_VERSION};
    uint8_t out[16];
    const uint16_t n = protocore_spinel_command_build(protocore_spinel_header(0, 1), SPINEL_CMD_PROP_VALUE_GET,
                                                      SPINEL_PROP_PROTOCOL_VERSION, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(3, n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, out, 3);

    uint8_t header = 0;
    uint32_t cmd = 0;
    uint32_t prop = 0;
    const uint8_t *value = NULL;
    uint16_t value_len = 0xFFFF;
    TEST_ASSERT_EQUAL_INT(3, protocore_spinel_command_parse(out, n, &header, &cmd, &prop, &value, &value_len));
    TEST_ASSERT_EQUAL_HEX8(0x81, header);
    TEST_ASSERT_EQUAL_UINT32(SPINEL_CMD_PROP_VALUE_GET, cmd);
    TEST_ASSERT_EQUAL_UINT32(SPINEL_PROP_PROTOCOL_VERSION, prop);
    TEST_ASSERT_EQUAL_UINT16(0, value_len);

    // Setting the PAN id: the property is 0x36, still one packed octet, and the value is the
    // uint16 0x1234 little-endian.
    const uint16_t m = protocore_spinel_command_build(0x82, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_MAC_15_4_PANID,
                                                      (const uint8_t *)"\x34\x12", 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(5, m); // header + cmd(1) + prop(1) + value(2)
    TEST_ASSERT_EQUAL_INT(3, protocore_spinel_command_parse(out, m, &header, &cmd, &prop, &value, &value_len));
    TEST_ASSERT_EQUAL_UINT32(SPINEL_PROP_MAC_15_4_PANID, prop);
    TEST_ASSERT_EQUAL_UINT16(2, value_len);
    TEST_ASSERT_EQUAL_HEX8(0x34, value[0]);

    // A property id past 127 needs two packed octets, so the value offset moves out by one: 0x100
    // is 0b1_0000000, low seven bits 0 with the continuation bit, then 1 -> 80 02.
    const uint16_t k = protocore_spinel_command_build(0x83, SPINEL_CMD_PROP_VALUE_IS, 0x100u, (const uint8_t *)"\xAA",
                                                      1, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(5, k); // header + cmd(1) + prop(2) + value(1)
    TEST_ASSERT_EQUAL_HEX8(0x80, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x02, out[3]);
    TEST_ASSERT_EQUAL_INT(4, protocore_spinel_command_parse(out, k, &header, &cmd, &prop, &value, &value_len));
    TEST_ASSERT_EQUAL_UINT32(0x100u, prop);
    TEST_ASSERT_EQUAL_UINT16(1, value_len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, value[0]);
}

// A command payload survives the HDLC layer it rides in: build, frame, unframe, parse.
void test_spinel_command_through_hdlc(void)
{
    uint8_t cmdbuf[32];
    const uint16_t clen =
        protocore_spinel_command_build(protocore_spinel_header(0, 3), SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_PHY_CHAN,
                                       (const uint8_t *)"\x0B", 1, cmdbuf, sizeof(cmdbuf));
    TEST_ASSERT_EQUAL_UINT16(4, clen);

    uint8_t frame[64];
    const uint16_t flen = protocore_spinel_frame_encode(cmdbuf, clen, frame, sizeof(frame));
    TEST_ASSERT_TRUE(flen > 0);

    uint8_t payload[64];
    uint16_t plen = 0;
    TEST_ASSERT_EQUAL_INT((int)flen, protocore_spinel_frame_decode(frame, flen, payload, sizeof(payload), &plen));

    uint32_t cmd = 0;
    uint32_t prop = 0;
    const uint8_t *value = NULL;
    uint16_t value_len = 0;
    TEST_ASSERT_TRUE(protocore_spinel_command_parse(payload, plen, NULL, &cmd, &prop, &value, &value_len) > 0);
    TEST_ASSERT_EQUAL_UINT32(SPINEL_CMD_PROP_VALUE_SET, cmd);
    TEST_ASSERT_EQUAL_UINT32(SPINEL_PROP_PHY_CHAN, prop);
    TEST_ASSERT_EQUAL_UINT16(1, value_len);
    TEST_ASSERT_EQUAL_HEX8(0x0B, value[0]); // 802.15.4 channel 11
}

// The spinel datatypes S, L, s, l are little-endian on the wire; E and 6 are 8 and 16 raw octets.
void test_spinel_value_wire_layout(void)
{
    uint8_t buf[64];
    SpinelWriter w;
    protocore_spinel_writer_init(&w, buf, sizeof(buf));
    protocore_spinel_put_u16(&w, 0x1234);
    protocore_spinel_put_u32(&w, 0x89ABCDEFu);
    protocore_spinel_put_i8(&w, -1);
    protocore_spinel_put_i16(&w, -2);
    TEST_ASSERT_EQUAL_UINT16(9, protocore_spinel_writer_len(&w));

    static const uint8_t WANT[9] = {0x34, 0x12, 0xEF, 0xCD, 0xAB, 0x89, 0xFF, 0xFE, 0xFF};
    TEST_ASSERT_EQUAL_MEMORY(WANT, buf, 9);

    SpinelReader r;
    protocore_spinel_reader_init(&r, buf, 9);
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    int8_t i8 = 0;
    int16_t i16 = 0;
    TEST_ASSERT_TRUE(protocore_spinel_get_u16(&r, &u16));
    TEST_ASSERT_TRUE(protocore_spinel_get_u32(&r, &u32));
    TEST_ASSERT_TRUE(protocore_spinel_get_i8(&r, &i8));
    TEST_ASSERT_TRUE(protocore_spinel_get_i16(&r, &i16));
    TEST_ASSERT_EQUAL_HEX16(0x1234, u16);
    TEST_ASSERT_EQUAL_HEX32(0x89ABCDEFu, u32);
    TEST_ASSERT_EQUAL_INT8(-1, i8);
    TEST_ASSERT_EQUAL_INT16(-2, i16);
    TEST_ASSERT_TRUE(protocore_spinel_reader_ok(&r));
}

// Every field a property value can carry survives a write then a read in the same order.
void test_spinel_value_round_trip(void)
{
    static const uint8_t EUI[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    static const uint8_t LL[16] = {0xFE, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
    static const uint8_t RAW[3] = {0xDE, 0xAD, 0xBE};

    uint8_t buf[96];
    SpinelWriter w;
    protocore_spinel_writer_init(&w, buf, sizeof(buf));
    protocore_spinel_put_bool(&w, PROTO_TRUE);
    protocore_spinel_put_uint(&w, 1337u);
    protocore_spinel_put_eui64(&w, EUI);
    protocore_spinel_put_ipv6(&w, LL);
    protocore_spinel_put_utf8(&w, "OPENTHREAD");
    protocore_spinel_put_data_wlen(&w, RAW, sizeof(RAW));
    protocore_spinel_put_data(&w, RAW, sizeof(RAW));
    const uint16_t n = protocore_spinel_writer_len(&w);
    TEST_ASSERT_TRUE(n > 0);

    SpinelReader r;
    protocore_spinel_reader_init(&r, buf, n);
    proto_bool b = PROTO_FALSE;
    uint32_t packed = 0;
    const uint8_t *eui = NULL;
    const uint8_t *ip6 = NULL;
    const char *s = NULL;
    uint16_t slen = 0;
    const uint8_t *d1 = NULL;
    uint16_t d1len = 0;
    const uint8_t *d2 = NULL;
    uint16_t d2len = 0;
    TEST_ASSERT_TRUE(protocore_spinel_get_bool(&r, &b));
    TEST_ASSERT_TRUE(protocore_spinel_get_uint(&r, &packed));
    TEST_ASSERT_TRUE(protocore_spinel_get_eui64(&r, &eui));
    TEST_ASSERT_TRUE(protocore_spinel_get_ipv6(&r, &ip6));
    TEST_ASSERT_TRUE(protocore_spinel_get_utf8(&r, &s, &slen));
    TEST_ASSERT_TRUE(protocore_spinel_get_data_wlen(&r, &d1, &d1len));
    TEST_ASSERT_TRUE(protocore_spinel_get_data(&r, &d2, &d2len));

    TEST_ASSERT_TRUE(b);
    TEST_ASSERT_EQUAL_UINT32(1337u, packed);
    TEST_ASSERT_EQUAL_MEMORY(EUI, eui, 8);
    TEST_ASSERT_EQUAL_MEMORY(LL, ip6, 16);
    TEST_ASSERT_EQUAL_UINT16(10, slen);
    TEST_ASSERT_EQUAL_MEMORY("OPENTHREAD", s, 10);
    TEST_ASSERT_EQUAL_UINT16(sizeof(RAW), d1len);
    TEST_ASSERT_EQUAL_MEMORY(RAW, d1, sizeof(RAW));
    TEST_ASSERT_EQUAL_UINT16(sizeof(RAW), d2len);
    TEST_ASSERT_EQUAL_MEMORY(RAW, d2, sizeof(RAW));
    TEST_ASSERT_TRUE(protocore_spinel_reader_ok(&r));
}

// One read past the end latches err for the whole sequence, and one write past cap makes the
// finished length 0: a caller checks once at the end rather than after every field.
void test_spinel_cursor_bounds_latch(void)
{
    static const uint8_t VALUE[3] = {0x01, 0x02, 0x03};
    SpinelReader r;
    protocore_spinel_reader_init(&r, VALUE, sizeof(VALUE));
    uint32_t u32 = 0;
    TEST_ASSERT_FALSE(protocore_spinel_get_u32(&r, &u32)); // 4 bytes wanted, 3 present
    TEST_ASSERT_FALSE(protocore_spinel_reader_ok(&r));
    uint8_t u8 = 0;
    TEST_ASSERT_FALSE(protocore_spinel_get_u8(&r, &u8)); // stays latched
    TEST_ASSERT_FALSE(protocore_spinel_reader_ok(&r));

    // A value with no NUL is not a UTF8 field.
    SpinelReader s;
    protocore_spinel_reader_init(&s, (const uint8_t *)"abc", 3);
    const char *str = NULL;
    uint16_t slen = 0;
    TEST_ASSERT_FALSE(protocore_spinel_get_utf8(&s, &str, &slen));
    TEST_ASSERT_FALSE(protocore_spinel_reader_ok(&s));

    uint8_t small[3];
    SpinelWriter w;
    protocore_spinel_writer_init(&w, small, sizeof(small));
    TEST_ASSERT_TRUE(protocore_spinel_put_u16(&w, 0xBEEF));
    TEST_ASSERT_FALSE(protocore_spinel_put_u32(&w, 0)); // 4 more, 1 left
    TEST_ASSERT_EQUAL_UINT16(0, protocore_spinel_writer_len(&w));
}

// The registry maps a property id to its name and its leading datatype; anything unlisted is UNKNOWN.
void test_spinel_property_registry(void)
{
    const SpinelPropInfo *e = protocore_spinel_prop_lookup(SPINEL_PROP_NCP_VERSION);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("NCP_VERSION", e->name);
    TEST_ASSERT_EQUAL_CHAR('U', e->type); // a UTF8 string

    e = protocore_spinel_prop_lookup(SPINEL_PROP_IPV6_LL_ADDR);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_CHAR('6', e->type); // a 16-octet IPv6 address

    TEST_ASSERT_NULL(protocore_spinel_prop_lookup(0xFFFFu));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", protocore_spinel_prop_name(0xFFFFu));
    TEST_ASSERT_EQUAL_STRING("LAST_STATUS", protocore_spinel_prop_name(SPINEL_PROP_LAST_STATUS));
}

// LAST_STATUS carries a packed integer. The spinel status registry puts the reset causes in one
// block, SPINEL_STATUS_RESET__BEGIN = 112 up to SPINEL_STATUS_RESET__END = 128 exclusive, so all
// sixteen name RESET - RESET_WATCHDOG at 120 among them - and 111 and 128 are outside it.
void test_spinel_status_names(void)
{
    TEST_ASSERT_EQUAL_STRING("OK", protocore_spinel_status_name(SPINEL_STATUS_OK));
    TEST_ASSERT_EQUAL_STRING("PARSE_ERROR", protocore_spinel_status_name(SPINEL_STATUS_PARSE_ERROR));
    TEST_ASSERT_EQUAL_STRING("EMPTY", protocore_spinel_status_name(SPINEL_STATUS_EMPTY));

    TEST_ASSERT_EQUAL_INT(112, SPINEL_STATUS_RESET_POWER_ON);
    TEST_ASSERT_EQUAL_INT(128, SPINEL_STATUS_RESET_END);
    for (uint32_t s = SPINEL_STATUS_RESET_POWER_ON; s < SPINEL_STATUS_RESET_END; s++)
    {
        TEST_ASSERT_EQUAL_STRING("RESET", protocore_spinel_status_name(s));
    }
    TEST_ASSERT_EQUAL_STRING("RESET", protocore_spinel_status_name(114u)); // RESET_SOFTWARE
    TEST_ASSERT_EQUAL_STRING("RESET", protocore_spinel_status_name(120u)); // RESET_WATCHDOG

    TEST_ASSERT_EQUAL_STRING("UNKNOWN", protocore_spinel_status_name(SPINEL_STATUS_RESET_POWER_ON - 1));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", protocore_spinel_status_name(SPINEL_STATUS_RESET_END));
}

// An unsolicited PROP_VALUE_IS(LAST_STATUS) is what an NCP sends after a reset: the whole path from
// UART bytes to a status name, with the status a packed integer inside the value.
void test_spinel_last_status_decode(void)
{
    uint8_t value[5];
    const uint8_t vlen = protocore_spinel_pack_uint(SPINEL_STATUS_RESET_POWER_ON, value, sizeof(value));
    TEST_ASSERT_EQUAL_UINT8(1, vlen); // 112 < 128, so one byte

    uint8_t cmdbuf[16];
    const uint16_t clen = protocore_spinel_command_build(protocore_spinel_header(0, 0), SPINEL_CMD_PROP_VALUE_IS,
                                                         SPINEL_PROP_LAST_STATUS, value, vlen, cmdbuf, sizeof(cmdbuf));
    uint8_t frame[32];
    const uint16_t flen = protocore_spinel_frame_encode(cmdbuf, clen, frame, sizeof(frame));

    uint8_t payload[32];
    uint16_t plen = 0;
    TEST_ASSERT_EQUAL_INT((int)flen, protocore_spinel_frame_decode(frame, flen, payload, sizeof(payload), &plen));

    uint32_t cmd = 0;
    uint32_t prop = 0;
    const uint8_t *val = NULL;
    uint16_t val_len = 0;
    TEST_ASSERT_TRUE(protocore_spinel_command_parse(payload, plen, NULL, &cmd, &prop, &val, &val_len) > 0);
    TEST_ASSERT_EQUAL_UINT32(SPINEL_CMD_PROP_VALUE_IS, cmd);
    TEST_ASSERT_EQUAL_UINT32(SPINEL_PROP_LAST_STATUS, prop);

    SpinelReader r;
    protocore_spinel_reader_init(&r, val, val_len);
    uint32_t status = 0;
    TEST_ASSERT_TRUE(protocore_spinel_get_uint(&r, &status));
    TEST_ASSERT_EQUAL_UINT32(SPINEL_STATUS_RESET_POWER_ON, status);
    TEST_ASSERT_EQUAL_STRING("RESET", protocore_spinel_status_name(status));
}

// Null buffers are reported, never written through or read from.
void test_null_arguments_are_refused(void)
{
    uint8_t out[16];
    TEST_ASSERT_EQUAL_UINT8(0, protocore_spinel_pack_uint(1, NULL, 4));
    TEST_ASSERT_EQUAL_INT(0, protocore_spinel_unpack_uint(NULL, 4, NULL));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_spinel_frame_encode(NULL, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_spinel_frame_encode(out, 4, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(0, protocore_spinel_frame_decode(NULL, 4, out, sizeof(out), NULL));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_spinel_command_build(0x80, 1, 1, NULL, 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, protocore_spinel_command_parse(NULL, 4, NULL, NULL, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(-1, protocore_spinel_command_parse(out, 0, NULL, NULL, NULL, NULL, NULL));
}
