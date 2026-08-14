// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the HLK-LD2410 mmWave radar codec (server/peripherals/ld2410/ld2410.h).
//
// The governing document is Hi-Link's "HLK-LD2410 Human presence sensing module - Serial
// communication protocol", V1.02. It prints whole frames in hex: sec 2.2.1 the enable-configuration
// command and its ACK, 2.2.2 end configuration, 2.2.5 / 2.2.6 engineering mode on and off, 2.2.11
// restart, and sec 2.3.2 one normal-mode and one engineering-mode report frame.
//
// test_v102_published_report_frames is the load-bearing case: those two report frames are the only
// published octets that exercise every field of the payload at once, so decoding them field by
// field is what proves the offsets, the little-endian distances, and the engineering block are
// where the document puts them rather than where the parser happens to look.

#include "server/peripherals/ld2410/ld2410.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// V1.02 sec 2.3.2, "Report data in normal working mode":
//   F4 F3 F2 F1 | 0D 00 | 02 AA 02 51 00 00 00 00 3B 00 00 55 00 | F8 F7 F6 F5
// Payload per Table 9 / Table 11: data type 0x02, head 0xAA, target state, 2-octet moving distance,
// moving energy, 2-octet stationary distance, stationary energy, 2-octet detection distance, tail
// 0x55, check 0x00. Distances are little-endian, so 51 00 is 0x0051 = 81 cm.
static const uint8_t BASIC[23] = {0xF4, 0xF3, 0xF2, 0xF1, 0x0D, 0x00, 0x02, 0xAA, 0x02, 0x51, 0x00, 0x00,
                                  0x00, 0x00, 0x3B, 0x00, 0x00, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};

// V1.02 sec 2.3.2, "Report data in engineering mode":
//   F4 F3 F2 F1 | 23 00 | 01 AA 03 1E 00 3C 00 00 39 00 00 08 08
//                         3C 22 05 03 03 04 03 06 05
//                         00 00 39 10 13 06 06 08 04
//                         03 05 55 00 | F8 F7 F6 F5
// Table 13 appends the two maximum gate numbers, nine moving gate energies, nine stationary gate
// energies and the retained octets to the basic payload.
static const uint8_t ENGINEERING[45] = {0xF4, 0xF3, 0xF2, 0xF1, 0x23, 0x00, 0x01, 0xAA, 0x03, 0x1E, 0x00, 0x3C,
                                        0x00, 0x00, 0x39, 0x00, 0x00, 0x08, 0x08, 0x3C, 0x22, 0x05, 0x03, 0x03,
                                        0x04, 0x03, 0x06, 0x05, 0x00, 0x00, 0x39, 0x10, 0x13, 0x06, 0x06, 0x08,
                                        0x04, 0x03, 0x05, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};

void test_v102_published_report_frames(void)
{
    Ld2410Report r;
    memset(&r, 0xEE, sizeof(r));
    TEST_ASSERT_TRUE(protocore_ld2410_parse_report(BASIC, sizeof(BASIC), &r));
    TEST_ASSERT_EQUAL_UINT8(0u, r.engineering);
    TEST_ASSERT_EQUAL_UINT8(LD2410_STATE_STATIC, r.state); // 0x02, Table 12 "Stationary target"
    TEST_ASSERT_EQUAL_UINT16(81u, r.moving_cm);            // 0x0051
    TEST_ASSERT_EQUAL_UINT8(0u, r.moving_energy);
    TEST_ASSERT_EQUAL_UINT16(0u, r.static_cm);
    TEST_ASSERT_EQUAL_UINT8(0x3Bu, r.static_energy); // 59
    TEST_ASSERT_EQUAL_UINT16(0u, r.detect_cm);
    // no engineering block in a 0x02 frame
    TEST_ASSERT_EQUAL_UINT8(0u, r.max_moving_gate);
    TEST_ASSERT_EQUAL_UINT8(0u, r.max_static_gate);
    TEST_ASSERT_EQUAL_UINT8(0u, r.light);
    TEST_ASSERT_EQUAL_UINT8(0u, r.out_pin);

    memset(&r, 0xEE, sizeof(r));
    TEST_ASSERT_TRUE(protocore_ld2410_parse_report(ENGINEERING, sizeof(ENGINEERING), &r));
    TEST_ASSERT_EQUAL_UINT8(1u, r.engineering);
    TEST_ASSERT_EQUAL_UINT8(LD2410_STATE_BOTH, r.state); // 0x03
    TEST_ASSERT_EQUAL_UINT16(30u, r.moving_cm);          // 0x001E
    TEST_ASSERT_EQUAL_UINT8(0x3Cu, r.moving_energy);     // 60
    TEST_ASSERT_EQUAL_UINT16(0u, r.static_cm);
    TEST_ASSERT_EQUAL_UINT8(0x39u, r.static_energy); // 57
    TEST_ASSERT_EQUAL_UINT16(0u, r.detect_cm);
    TEST_ASSERT_EQUAL_UINT8(8u, r.max_moving_gate);
    TEST_ASSERT_EQUAL_UINT8(8u, r.max_static_gate);
    static const uint8_t MOVING[LD2410_MAX_GATES] = {0x3C, 0x22, 0x05, 0x03, 0x03, 0x04, 0x03, 0x06, 0x05};
    static const uint8_t STATIC[LD2410_MAX_GATES] = {0x00, 0x00, 0x39, 0x10, 0x13, 0x06, 0x06, 0x08, 0x04};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MOVING, r.moving_gate_energy, LD2410_MAX_GATES);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(STATIC, r.static_gate_energy, LD2410_MAX_GATES);
    TEST_ASSERT_EQUAL_UINT8(0x03u, r.light);
    TEST_ASSERT_EQUAL_UINT8(0x05u, r.out_pin);

    // The document's own consistency check: the reported moving energy is gate 0's, and the
    // stationary energy is the largest gate's, which is gate 2 at 0x39.
    TEST_ASSERT_EQUAL_UINT8(r.moving_energy, r.moving_gate_energy[0]);
    TEST_ASSERT_EQUAL_UINT8(r.static_energy, r.static_gate_energy[2]);
}

// Table 8: the intra-frame length is 0x0D for a basic frame and 0x23 for an engineering one, and
// the whole frame is 4 + 2 + length + 4.
void test_v102_frame_lengths(void)
{
    TEST_ASSERT_EQUAL_size_t(4u + 2u + 0x0Du + 4u, sizeof(BASIC));
    TEST_ASSERT_EQUAL_size_t(4u + 2u + 0x23u + 4u, sizeof(ENGINEERING));
    TEST_ASSERT_EQUAL_UINT8(0x0Du, BASIC[4]);
    TEST_ASSERT_EQUAL_UINT8(0x23u, ENGINEERING[4]);
    TEST_ASSERT_TRUE(sizeof(ENGINEERING) <= LD2410_FRAME_MAX);
}

// Table 12 assigns 0x00 no target, 0x01 moving, 0x02 stationary, 0x03 both. Presence follows the
// state and the distance helper picks the field the state names.
void test_v102_target_state_drives_presence_and_distance(void)
{
    Ld2410Report r;
    memset(&r, 0, sizeof(r));
    r.moving_cm = 111u;
    r.static_cm = 222u;

    r.state = LD2410_STATE_NONE;
    TEST_ASSERT_FALSE(protocore_ld2410_present(&r));
    TEST_ASSERT_EQUAL_UINT16(0u, protocore_ld2410_distance_cm(&r));

    r.state = LD2410_STATE_MOVING;
    TEST_ASSERT_TRUE(protocore_ld2410_present(&r));
    TEST_ASSERT_EQUAL_UINT16(111u, protocore_ld2410_distance_cm(&r));

    r.state = LD2410_STATE_STATIC;
    TEST_ASSERT_TRUE(protocore_ld2410_present(&r));
    TEST_ASSERT_EQUAL_UINT16(222u, protocore_ld2410_distance_cm(&r));

    // Both targets present: the moving one is the one being tracked.
    r.state = LD2410_STATE_BOTH;
    TEST_ASSERT_TRUE(protocore_ld2410_present(&r));
    TEST_ASSERT_EQUAL_UINT16(111u, protocore_ld2410_distance_cm(&r));

    TEST_ASSERT_FALSE(protocore_ld2410_present(NULL));
    TEST_ASSERT_EQUAL_UINT16(0u, protocore_ld2410_distance_cm(NULL));
}

// Every guard sec 2.3.1 states: the frame header, the length agreeing with the buffer, the footer,
// the data-type octet, the 0xAA head and the 0x55 tail.
void test_malformed_report_frames_are_refused(void)
{
    Ld2410Report r;
    uint8_t bad[sizeof(ENGINEERING)];

    TEST_ASSERT_FALSE(protocore_ld2410_parse_report(NULL, sizeof(BASIC), &r));
    TEST_ASSERT_FALSE(protocore_ld2410_parse_report(BASIC, sizeof(BASIC), NULL));
    TEST_ASSERT_FALSE(protocore_ld2410_parse_report(BASIC, 22u, &r)); // length disagrees with the buffer
    TEST_ASSERT_FALSE(protocore_ld2410_parse_report(BASIC, 12u, &r)); // shorter than the smallest frame

    for (size_t k = 0; k < 4u; k++)
    {
        memcpy(bad, BASIC, sizeof(BASIC));
        bad[k] ^= 0xFFu; // header octet
        TEST_ASSERT_FALSE(protocore_ld2410_parse_report(bad, sizeof(BASIC), &r));

        memcpy(bad, BASIC, sizeof(BASIC));
        bad[19u + k] ^= 0xFFu; // footer octet
        TEST_ASSERT_FALSE(protocore_ld2410_parse_report(bad, sizeof(BASIC), &r));
    }

    memcpy(bad, BASIC, sizeof(BASIC));
    bad[6] = 0x03u; // Table 10 defines only 0x01 and 0x02
    TEST_ASSERT_FALSE(protocore_ld2410_parse_report(bad, sizeof(BASIC), &r));

    memcpy(bad, BASIC, sizeof(BASIC));
    bad[6] = 0x01u; // engineering type with the basic length
    TEST_ASSERT_FALSE(protocore_ld2410_parse_report(bad, sizeof(BASIC), &r));

    memcpy(bad, ENGINEERING, sizeof(ENGINEERING));
    bad[6] = 0x02u; // basic type with the engineering length
    TEST_ASSERT_FALSE(protocore_ld2410_parse_report(bad, sizeof(ENGINEERING), &r));

    memcpy(bad, BASIC, sizeof(BASIC));
    bad[7] = 0x00u; // intra-frame head marker
    TEST_ASSERT_FALSE(protocore_ld2410_parse_report(bad, sizeof(BASIC), &r));

    memcpy(bad, BASIC, sizeof(BASIC));
    bad[17] = 0x00u; // tail
    TEST_ASSERT_FALSE(protocore_ld2410_parse_report(bad, sizeof(BASIC), &r));

    memcpy(bad, ENGINEERING, sizeof(ENGINEERING));
    bad[39] = 0x00u; // engineering tail
    TEST_ASSERT_FALSE(protocore_ld2410_parse_report(bad, sizeof(ENGINEERING), &r));

    memcpy(bad, BASIC, sizeof(BASIC));
    bad[4] = 0x0Cu; // the length no longer frames the buffer
    TEST_ASSERT_FALSE(protocore_ld2410_parse_report(bad, sizeof(BASIC), &r));
}

// The reassembler must find a frame in a stream that starts mid-noise, and must not report one
// until the last octet has arrived.
static void feed_noise(Ld2410Stream *s, Ld2410Report *r)
{
    static const uint8_t NOISE[] = {0x00, 0xF4, 0xF3, 0x11, 0xFF, 0xF4, 0x00, 0x5A};
    for (size_t i = 0; i < sizeof(NOISE); i++)
    {
        TEST_ASSERT_FALSE(protocore_ld2410_stream_push(s, NOISE[i], r));
    }
}

void test_stream_resyncs_past_noise_and_reports_once(void)
{
    Ld2410Stream s;
    Ld2410Report r;
    protocore_ld2410_stream_reset(&s);

    feed_noise(&s, &r);
    for (size_t i = 0; i + 1u < sizeof(BASIC); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(protocore_ld2410_stream_push(&s, BASIC[i], &r), "reported before the last octet");
    }
    TEST_ASSERT_TRUE(protocore_ld2410_stream_push(&s, BASIC[sizeof(BASIC) - 1u], &r));
    TEST_ASSERT_EQUAL_UINT16(81u, r.moving_cm);
    TEST_ASSERT_EQUAL_UINT8(LD2410_STATE_STATIC, r.state);

    // The stream is back in sync for the next frame, including a different frame kind.
    feed_noise(&s, &r);
    for (size_t i = 0; i + 1u < sizeof(ENGINEERING); i++)
    {
        TEST_ASSERT_FALSE(protocore_ld2410_stream_push(&s, ENGINEERING[i], &r));
    }
    TEST_ASSERT_TRUE(protocore_ld2410_stream_push(&s, ENGINEERING[sizeof(ENGINEERING) - 1u], &r));
    TEST_ASSERT_EQUAL_UINT8(1u, r.engineering);
    TEST_ASSERT_EQUAL_UINT16(30u, r.moving_cm);
}

// A header partly repeated inside the noise must not desynchronize the search: F4 F3 F2 F1 has
// distinct octets, so a false start restarts the match at most at the first one.
void test_stream_handles_a_partial_header_before_the_real_one(void)
{
    Ld2410Stream s;
    Ld2410Report r;
    protocore_ld2410_stream_reset(&s);

    static const uint8_t PARTIAL[] = {0xF4, 0xF3, 0xF2, 0xF4, 0xF4, 0xF3};
    for (size_t i = 0; i < sizeof(PARTIAL); i++)
    {
        TEST_ASSERT_FALSE(protocore_ld2410_stream_push(&s, PARTIAL[i], &r));
    }
    for (size_t i = 0; i + 1u < sizeof(BASIC); i++)
    {
        TEST_ASSERT_FALSE(protocore_ld2410_stream_push(&s, BASIC[i], &r));
    }
    TEST_ASSERT_TRUE(protocore_ld2410_stream_push(&s, BASIC[sizeof(BASIC) - 1u], &r));
    TEST_ASSERT_EQUAL_UINT16(81u, r.moving_cm);
}

// A length field larger than the reassembler's buffer is dropped rather than overrunning it, and
// the stream recovers to decode the next real frame.
void test_stream_drops_an_absurd_length_and_recovers(void)
{
    Ld2410Stream s;
    Ld2410Report r;
    protocore_ld2410_stream_reset(&s);

    static const uint8_t HUGE[6] = {0xF4, 0xF3, 0xF2, 0xF1, 0xFF, 0xFF};
    for (size_t i = 0; i < sizeof(HUGE); i++)
    {
        TEST_ASSERT_FALSE(protocore_ld2410_stream_push(&s, HUGE[i], &r));
    }
    TEST_ASSERT_EQUAL_UINT16(0u, s.pos);
    TEST_ASSERT_EQUAL_UINT8(0u, s.phase);

    for (size_t i = 0; i + 1u < sizeof(BASIC); i++)
    {
        TEST_ASSERT_FALSE(protocore_ld2410_stream_push(&s, BASIC[i], &r));
    }
    TEST_ASSERT_TRUE(protocore_ld2410_stream_push(&s, BASIC[sizeof(BASIC) - 1u], &r));
}

// A frame whose body fails the parser is dropped, and the reassembler resyncs rather than staying
// wedged on it.
void test_stream_drops_a_bad_frame_and_keeps_going(void)
{
    Ld2410Stream s;
    Ld2410Report r;
    uint8_t bad[sizeof(BASIC)];
    memcpy(bad, BASIC, sizeof(BASIC));
    bad[7] = 0x00u; // no 0xAA head marker

    protocore_ld2410_stream_reset(&s);
    for (size_t i = 0; i < sizeof(bad); i++)
    {
        TEST_ASSERT_FALSE(protocore_ld2410_stream_push(&s, bad[i], &r));
    }
    for (size_t i = 0; i + 1u < sizeof(BASIC); i++)
    {
        TEST_ASSERT_FALSE(protocore_ld2410_stream_push(&s, BASIC[i], &r));
    }
    TEST_ASSERT_TRUE(protocore_ld2410_stream_push(&s, BASIC[sizeof(BASIC) - 1u], &r));
}

// V1.02 sec 2.2: every command frame the document prints, octet for octet.
void test_v102_published_command_frames(void)
{
    uint8_t buf[24];

    // 2.2.1 enable configuration: word 0x00FF, value 0x0001
    static const uint8_t ENABLE[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF,
                                       0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(14u, protocore_ld2410_cmd_config_enable(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ENABLE, buf, 14u);

    // 2.2.2 end configuration: word 0x00FE, no value
    static const uint8_t END[12] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(12u, protocore_ld2410_cmd_config_end(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(END, buf, 12u);

    // 2.2.5 enable engineering mode: word 0x0062
    static const uint8_t ENG_ON[12] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x62, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(12u, protocore_ld2410_cmd_engineering(buf, sizeof(buf), PROTO_TRUE));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ENG_ON, buf, 12u);

    // 2.2.6 close engineering mode: word 0x0063
    static const uint8_t ENG_OFF[12] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x63, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(12u, protocore_ld2410_cmd_engineering(buf, sizeof(buf), PROTO_FALSE));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ENG_OFF, buf, 12u);

    // 2.2.11 restart the module: word 0x00A3
    static const uint8_t RESTART[12] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xA3, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(12u, protocore_ld2410_cmd_restart(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RESTART, buf, 12u);
}

// The LD2410B extends the same envelope. Its commands follow the identical Table 2 layout: header,
// little-endian frame data length of (2 + value octets), little-endian command word, value, MFR.
void test_ld2410b_command_frames_follow_the_same_envelope(void)
{
    uint8_t buf[24];

    // Bluetooth on / off: word 0x00A4, value 0x0001 / 0x0000
    static const uint8_t BT_ON[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xA4,
                                      0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    static const uint8_t BT_OFF[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xA4,
                                       0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(14u, protocore_ld2410_cmd_bluetooth(buf, sizeof(buf), PROTO_TRUE));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BT_ON, buf, 14u);
    TEST_ASSERT_EQUAL_size_t(14u, protocore_ld2410_cmd_bluetooth(buf, sizeof(buf), PROTO_FALSE));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BT_OFF, buf, 14u);

    // Get MAC: word 0x00A5, value 0x0001
    static const uint8_t MAC[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xA5, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(14u, protocore_ld2410_cmd_get_mac(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MAC, buf, 14u);

    // Set Bluetooth password: word 0x00A9, six octets in natural order. The factory default is the
    // ASCII "HiLink" = 48 69 4C 69 6E 6B.
    static const uint8_t PWD[18] = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0xA9, 0x00, 0x48,
                                    0x69, 0x4C, 0x69, 0x6E, 0x6B, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(18u, protocore_ld2410_cmd_set_bt_password(buf, sizeof(buf), "HiLink"));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PWD, buf, 18u);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ld2410_cmd_set_bt_password(buf, sizeof(buf), NULL));
}

// Every command encoder writes nothing into a buffer that cannot hold the whole frame.
void test_command_encoders_fail_closed(void)
{
    uint8_t buf[24];
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ld2410_cmd_config_enable(NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ld2410_cmd_config_enable(buf, 13u));
    TEST_ASSERT_EQUAL_size_t(14u, protocore_ld2410_cmd_config_enable(buf, 14u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ld2410_cmd_config_end(buf, 11u));
    TEST_ASSERT_EQUAL_size_t(12u, protocore_ld2410_cmd_config_end(buf, 12u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ld2410_cmd_engineering(buf, 11u, PROTO_TRUE));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ld2410_cmd_restart(buf, 11u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ld2410_cmd_bluetooth(buf, 13u, PROTO_TRUE));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ld2410_cmd_get_mac(buf, 13u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ld2410_cmd_set_bt_password(buf, 17u, "HiLink"));
}

// V1.02 Table 5: an ACK's command word is the request's with 0x0100 set, followed by a 2-octet
// status. Both published ACKs are decoded here.
void test_v102_published_ack_frames(void)
{
    // 2.2.1 enable-configuration ACK: status 0, protocol version 0x0001, buffer size 0x0040
    static const uint8_t ENABLE_ACK[18] = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0xFF, 0x01, 0x00,
                                           0x00, 0x01, 0x00, 0x40, 0x00, 0x04, 0x03, 0x02, 0x01};
    Ld2410Ack a;
    TEST_ASSERT_TRUE(protocore_ld2410_parse_ack(ENABLE_ACK, sizeof(ENABLE_ACK), &a));
    TEST_ASSERT_EQUAL_HEX16(0x01FFu, a.command); // 0x00FF | 0x0100
    TEST_ASSERT_EQUAL_HEX16(0x0000u, a.status);
    TEST_ASSERT_TRUE(protocore_ld2410_ack_ok(&a));
    TEST_ASSERT_EQUAL_size_t(4u, a.payload_len);
    static const uint8_t PAYLOAD[4] = {0x01, 0x00, 0x40, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, a.payload, 4u);

    // 2.2.2 end-configuration ACK: status 0, no further data
    static const uint8_t END_ACK[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFE,
                                        0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_TRUE(protocore_ld2410_parse_ack(END_ACK, sizeof(END_ACK), &a));
    TEST_ASSERT_EQUAL_HEX16(0x01FEu, a.command);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, a.status);
    TEST_ASSERT_EQUAL_size_t(0u, a.payload_len);
    TEST_ASSERT_NULL(a.payload);
    TEST_ASSERT_TRUE(protocore_ld2410_ack_ok(&a));

    // "0 success, 1 failure": a non-zero status is not an accepted command.
    uint8_t fail[14];
    memcpy(fail, END_ACK, sizeof(END_ACK));
    fail[8] = 0x01u;
    TEST_ASSERT_TRUE(protocore_ld2410_parse_ack(fail, sizeof(fail), &a));
    TEST_ASSERT_EQUAL_HEX16(0x0001u, a.status);
    TEST_ASSERT_FALSE(protocore_ld2410_ack_ok(&a));
    TEST_ASSERT_FALSE(protocore_ld2410_ack_ok(NULL));
}

// The get-MAC reply is command word 0x01A5 with six address octets after the status; the document
// does not print one, so the octets below are arbitrary and the property asserted is that they come
// back in wire order. Only a successful reply to that word yields a MAC at all.
void test_get_mac_ack_yields_the_address(void)
{
    static const uint8_t MAC_ACK[20] = {0xFD, 0xFC, 0xFB, 0xFA, 0x0A, 0x00, 0xA5, 0x01, 0x00, 0x00,
                                        0x8F, 0x27, 0x2E, 0x1A, 0xCB, 0x36, 0x04, 0x03, 0x02, 0x01};
    Ld2410Ack a;
    uint8_t mac[6] = {0};
    TEST_ASSERT_TRUE(protocore_ld2410_parse_ack(MAC_ACK, sizeof(MAC_ACK), &a));
    TEST_ASSERT_EQUAL_HEX16(0x01A5u, a.command);
    TEST_ASSERT_EQUAL_size_t(6u, a.payload_len);
    TEST_ASSERT_TRUE(protocore_ld2410_ack_mac(&a, mac));
    static const uint8_t WANT[6] = {0x8F, 0x27, 0x2E, 0x1A, 0xCB, 0x36};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, mac, 6u);

    // A failed get-MAC, a different command word, and a short payload all yield nothing.
    uint8_t bad[20];
    memcpy(bad, MAC_ACK, sizeof(MAC_ACK));
    bad[8] = 0x01u; // status failure
    TEST_ASSERT_TRUE(protocore_ld2410_parse_ack(bad, sizeof(bad), &a));
    TEST_ASSERT_FALSE(protocore_ld2410_ack_mac(&a, mac));

    memcpy(bad, MAC_ACK, sizeof(MAC_ACK));
    bad[6] = 0xA4u; // the Bluetooth-enable ACK, not get-MAC
    TEST_ASSERT_TRUE(protocore_ld2410_parse_ack(bad, sizeof(bad), &a));
    TEST_ASSERT_FALSE(protocore_ld2410_ack_mac(&a, mac));

    TEST_ASSERT_FALSE(protocore_ld2410_ack_mac(NULL, mac));
    TEST_ASSERT_FALSE(protocore_ld2410_ack_mac(&a, NULL));
}

// Table 4: header, an intra-frame length that frames the buffer exactly, and the MFR footer. An
// ACK also carries at least the command word and the status, so its length is at least 4.
void test_malformed_ack_frames_are_refused(void)
{
    static const uint8_t GOOD[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFE,
                                     0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    Ld2410Ack a;
    uint8_t bad[14];

    TEST_ASSERT_FALSE(protocore_ld2410_parse_ack(NULL, sizeof(GOOD), &a));
    TEST_ASSERT_FALSE(protocore_ld2410_parse_ack(GOOD, sizeof(GOOD), NULL));
    TEST_ASSERT_TRUE(protocore_ld2410_parse_ack(GOOD, sizeof(GOOD), &a));
    TEST_ASSERT_FALSE(protocore_ld2410_parse_ack(GOOD, 13u, &a)); // shorter than the smallest ACK

    for (size_t k = 0; k < 4u; k++)
    {
        memcpy(bad, GOOD, sizeof(GOOD));
        bad[k] ^= 0xFFu;
        TEST_ASSERT_FALSE(protocore_ld2410_parse_ack(bad, sizeof(GOOD), &a));

        memcpy(bad, GOOD, sizeof(GOOD));
        bad[10u + k] ^= 0xFFu;
        TEST_ASSERT_FALSE(protocore_ld2410_parse_ack(bad, sizeof(GOOD), &a));
    }

    memcpy(bad, GOOD, sizeof(GOOD));
    bad[4] = 0x03u; // below the command word plus status
    TEST_ASSERT_FALSE(protocore_ld2410_parse_ack(bad, sizeof(GOOD), &a));

    memcpy(bad, GOOD, sizeof(GOOD));
    bad[4] = 0x06u; // a length the buffer cannot hold
    TEST_ASSERT_FALSE(protocore_ld2410_parse_ack(bad, sizeof(GOOD), &a));

    // A report frame is not an ACK: the two envelopes never accept each other's frames.
    TEST_ASSERT_FALSE(protocore_ld2410_parse_ack(BASIC, sizeof(BASIC), &a));
    Ld2410Report r;
    TEST_ASSERT_FALSE(protocore_ld2410_parse_report(GOOD, sizeof(GOOD), &r));
}
