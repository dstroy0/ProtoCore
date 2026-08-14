// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Waveshare HMMD mmWave radar codec (server/peripherals/hmmd/hmmd.h).
//
// No vendor protocol document for the S3KM1110 is obtainable, so the report path here is PROPERTIES
// against the layout the module's own header states: header F4 F3 F2 F1, a little-endian
// intra-frame length of 35, detect(1) + distance(2) + 16 gate energies(2 each), footer F8 F7 F6 F5.
//
// The command path is anchored on a published document. The header states the HMMD shares the
// LD2410's framing exactly, and Hi-Link's "HLK-LD2410 Serial communication protocol" V1.02 prints
// that envelope in Tables 2 and 3 and prints the open- and close-command frames octet for octet in
// sec 2.2.1 and 2.2.2. test_ld2410_v102_published_command_envelope is therefore the load-bearing
// case: those exact octets are what a module of this family accepts, so an encoder that got the
// little-endian length or the word order wrong could not produce them.

#include "server/peripherals/hmmd/hmmd.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Lay a report frame down from the fields, per the layout the header states. Little-endian
// throughout; @p out must hold PROTOCORE_HMMD_FRAME_MAX octets.
static void build_report(uint8_t *out, uint8_t detected, uint16_t distance_cm, const uint16_t *gates)
{
    static const uint8_t HDR[4] = {0xF4, 0xF3, 0xF2, 0xF1};
    static const uint8_t FTR[4] = {0xF8, 0xF7, 0xF6, 0xF5};
    memcpy(out, HDR, 4u);
    out[4] = (uint8_t)(PROTOCORE_HMMD_REPORT_LEN & 0xFFu);
    out[5] = (uint8_t)(PROTOCORE_HMMD_REPORT_LEN >> 8);
    out[6] = detected;
    out[7] = (uint8_t)(distance_cm & 0xFFu);
    out[8] = (uint8_t)(distance_cm >> 8);
    for (int i = 0; i < PROTOCORE_HMMD_GATES; i++)
    {
        out[9 + 2 * i] = (uint8_t)(gates[i] & 0xFFu);
        out[10 + 2 * i] = (uint8_t)(gates[i] >> 8);
    }
    memcpy(out + 41, FTR, 4u);
}

static const uint16_t GATES[PROTOCORE_HMMD_GATES] = {0x0000, 0x0001, 0x0102, 0x00FF, 0x0100, 0x1234, 0xFFFF, 0x00AA,
                                                     0x5500, 0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0xBEEF};

// The stated payload is 1 + 2 + 16*2 = 35 octets, and the whole frame is 4 + 2 + 35 + 4 = 45.
void test_declared_frame_geometry(void)
{
    TEST_ASSERT_EQUAL_INT(16, PROTOCORE_HMMD_GATES);
    TEST_ASSERT_EQUAL_INT(1 + 2 + 16 * 2, PROTOCORE_HMMD_REPORT_LEN);
    TEST_ASSERT_EQUAL_INT(4 + 2 + PROTOCORE_HMMD_REPORT_LEN + 4, PROTOCORE_HMMD_FRAME_MAX);
}

// Every field comes back where it was put: the detection flag, the little-endian distance, and all
// sixteen little-endian gate energies including ones whose two octets differ.
void test_report_fields_round_trip(void)
{
    uint8_t frame[PROTOCORE_HMMD_FRAME_MAX];
    build_report(frame, 0x01u, 0x0123u, GATES);

    HmmdReport r;
    memset(&r, 0xEE, sizeof(r));
    TEST_ASSERT_TRUE(protocore_hmmd_parse_report(frame, sizeof(frame), &r));
    TEST_ASSERT_EQUAL_UINT8(1u, r.detected);
    TEST_ASSERT_EQUAL_UINT16(0x0123u, r.distance_cm);
    for (int i = 0; i < PROTOCORE_HMMD_GATES; i++)
    {
        TEST_ASSERT_EQUAL_UINT16(GATES[i], r.gate_energy[i]);
    }

    // The distance is little-endian: swapping its two octets reads a different distance.
    uint8_t swapped[PROTOCORE_HMMD_FRAME_MAX];
    memcpy(swapped, frame, sizeof(frame));
    swapped[7] = frame[8];
    swapped[8] = frame[7];
    TEST_ASSERT_TRUE(protocore_hmmd_parse_report(swapped, sizeof(swapped), &r));
    TEST_ASSERT_EQUAL_UINT16(0x2301u, r.distance_cm);
}

// Header: the detection flag is a flag, so only the set value reports a target; anything else is
// "no target" rather than a truthy count.
void test_detection_flag_is_exactly_one(void)
{
    uint8_t frame[PROTOCORE_HMMD_FRAME_MAX];
    HmmdReport r;

    build_report(frame, 0x01u, 250u, GATES);
    TEST_ASSERT_TRUE(protocore_hmmd_parse_report(frame, sizeof(frame), &r));
    TEST_ASSERT_EQUAL_UINT8(1u, r.detected);
    TEST_ASSERT_TRUE(protocore_hmmd_present(&r));
    TEST_ASSERT_EQUAL_UINT16(250u, protocore_hmmd_distance_cm(&r));

    build_report(frame, 0x00u, 250u, GATES);
    TEST_ASSERT_TRUE(protocore_hmmd_parse_report(frame, sizeof(frame), &r));
    TEST_ASSERT_EQUAL_UINT8(0u, r.detected);
    TEST_ASSERT_FALSE(protocore_hmmd_present(&r));
    // no target means no distance, whatever the payload carried
    TEST_ASSERT_EQUAL_UINT16(0u, protocore_hmmd_distance_cm(&r));

    build_report(frame, 0x02u, 250u, GATES);
    TEST_ASSERT_TRUE(protocore_hmmd_parse_report(frame, sizeof(frame), &r));
    TEST_ASSERT_EQUAL_UINT8(0u, r.detected);
    TEST_ASSERT_FALSE(protocore_hmmd_present(&r));

    TEST_ASSERT_FALSE(protocore_hmmd_present(NULL));
    TEST_ASSERT_EQUAL_UINT16(0u, protocore_hmmd_distance_cm(NULL));
}

// Header: "the header, footer, and the length agreeing with the buffer are the whole of the
// validation". Each of those three is broken in turn, and each refusal is required.
void test_malformed_report_frames_are_refused(void)
{
    uint8_t good[PROTOCORE_HMMD_FRAME_MAX];
    uint8_t bad[PROTOCORE_HMMD_FRAME_MAX];
    HmmdReport r;
    build_report(good, 1u, 100u, GATES);
    TEST_ASSERT_TRUE(protocore_hmmd_parse_report(good, sizeof(good), &r));

    TEST_ASSERT_FALSE(protocore_hmmd_parse_report(NULL, sizeof(good), &r));
    TEST_ASSERT_FALSE(protocore_hmmd_parse_report(good, sizeof(good), NULL));
    // this module emits exactly one report length, so a shorter or longer buffer is not one
    TEST_ASSERT_FALSE(protocore_hmmd_parse_report(good, PROTOCORE_HMMD_FRAME_MAX - 1u, &r));
    TEST_ASSERT_FALSE(protocore_hmmd_parse_report(good, 0u, &r));

    for (size_t k = 0; k < 4u; k++)
    {
        memcpy(bad, good, sizeof(good));
        bad[k] ^= 0xFFu; // header octet
        TEST_ASSERT_FALSE(protocore_hmmd_parse_report(bad, sizeof(bad), &r));

        memcpy(bad, good, sizeof(good));
        bad[41u + k] ^= 0xFFu; // footer octet
        TEST_ASSERT_FALSE(protocore_hmmd_parse_report(bad, sizeof(bad), &r));
    }

    memcpy(bad, good, sizeof(good));
    bad[4] = 34u; // an intra-frame length other than 35
    TEST_ASSERT_FALSE(protocore_hmmd_parse_report(bad, sizeof(bad), &r));

    memcpy(bad, good, sizeof(good));
    bad[5] = 0x01u; // 35 in the low octet plus 0x0100 in the high one
    TEST_ASSERT_FALSE(protocore_hmmd_parse_report(bad, sizeof(bad), &r));
}

// The reassembler must find a frame in a stream that starts mid-noise, must not report until the
// last octet arrives, and must be ready for the next frame afterwards.
void test_stream_resyncs_past_noise_and_reports_once(void)
{
    static const uint8_t NOISE[] = {0x00, 0xF4, 0xF3, 0x11, 0xFF, 0xF4, 0x00, 0x5A};
    uint8_t frame[PROTOCORE_HMMD_FRAME_MAX];
    HmmdStream s;
    HmmdReport r;
    build_report(frame, 1u, 0x0321u, GATES);
    protocore_hmmd_stream_reset(&s);

    for (int round = 0; round < 2; round++)
    {
        for (size_t i = 0; i < sizeof(NOISE); i++)
        {
            TEST_ASSERT_FALSE(protocore_hmmd_stream_push(&s, NOISE[i], &r));
        }
        for (size_t i = 0; i + 1u < sizeof(frame); i++)
        {
            TEST_ASSERT_FALSE_MESSAGE(protocore_hmmd_stream_push(&s, frame[i], &r), "reported before the last octet");
        }
        TEST_ASSERT_TRUE(protocore_hmmd_stream_push(&s, frame[sizeof(frame) - 1u], &r));
        TEST_ASSERT_EQUAL_UINT16(0x0321u, r.distance_cm);
        TEST_ASSERT_EQUAL_UINT8(1u, r.detected);
    }
}

// The header octets are distinct, so a false start restarts the match at most at the first octet
// and the real frame right behind it is still found.
void test_stream_handles_a_partial_header_before_the_real_one(void)
{
    static const uint8_t PARTIAL[] = {0xF4, 0xF3, 0xF2, 0xF4, 0xF4, 0xF3};
    uint8_t frame[PROTOCORE_HMMD_FRAME_MAX];
    HmmdStream s;
    HmmdReport r;
    build_report(frame, 1u, 77u, GATES);
    protocore_hmmd_stream_reset(&s);

    for (size_t i = 0; i < sizeof(PARTIAL); i++)
    {
        TEST_ASSERT_FALSE(protocore_hmmd_stream_push(&s, PARTIAL[i], &r));
    }
    for (size_t i = 0; i + 1u < sizeof(frame); i++)
    {
        TEST_ASSERT_FALSE(protocore_hmmd_stream_push(&s, frame[i], &r));
    }
    TEST_ASSERT_TRUE(protocore_hmmd_stream_push(&s, frame[sizeof(frame) - 1u], &r));
    TEST_ASSERT_EQUAL_UINT16(77u, r.distance_cm);
}

// A length field wider than the fixed buffer is dropped rather than overrunning it, and the stream
// recovers to decode the next real frame.
void test_stream_drops_an_absurd_length_and_recovers(void)
{
    static const uint8_t HUGE[6] = {0xF4, 0xF3, 0xF2, 0xF1, 0xFF, 0xFF};
    uint8_t frame[PROTOCORE_HMMD_FRAME_MAX];
    HmmdStream s;
    HmmdReport r;
    build_report(frame, 1u, 42u, GATES);
    protocore_hmmd_stream_reset(&s);

    for (size_t i = 0; i < sizeof(HUGE); i++)
    {
        TEST_ASSERT_FALSE(protocore_hmmd_stream_push(&s, HUGE[i], &r));
    }
    TEST_ASSERT_EQUAL_UINT16(0u, s.pos);
    TEST_ASSERT_EQUAL_UINT8(0u, s.phase);

    for (size_t i = 0; i + 1u < sizeof(frame); i++)
    {
        TEST_ASSERT_FALSE(protocore_hmmd_stream_push(&s, frame[i], &r));
    }
    TEST_ASSERT_TRUE(protocore_hmmd_stream_push(&s, frame[sizeof(frame) - 1u], &r));
    TEST_ASSERT_EQUAL_UINT16(42u, r.distance_cm);
}

// A frame whose body fails validation is dropped and the reassembler resyncs rather than wedging.
void test_stream_drops_a_bad_frame_and_keeps_going(void)
{
    uint8_t good[PROTOCORE_HMMD_FRAME_MAX];
    uint8_t bad[PROTOCORE_HMMD_FRAME_MAX];
    HmmdStream s;
    HmmdReport r;
    build_report(good, 1u, 55u, GATES);
    memcpy(bad, good, sizeof(good));
    bad[44] = 0x00u; // broken footer

    protocore_hmmd_stream_reset(&s);
    for (size_t i = 0; i < sizeof(bad); i++)
    {
        TEST_ASSERT_FALSE(protocore_hmmd_stream_push(&s, bad[i], &r));
    }
    for (size_t i = 0; i + 1u < sizeof(good); i++)
    {
        TEST_ASSERT_FALSE(protocore_hmmd_stream_push(&s, good[i], &r));
    }
    TEST_ASSERT_TRUE(protocore_hmmd_stream_push(&s, good[sizeof(good) - 1u], &r));
    TEST_ASSERT_EQUAL_UINT16(55u, r.distance_cm);
}

void test_stream_null_arguments_are_refused(void)
{
    HmmdStream s;
    HmmdReport r;
    protocore_hmmd_stream_reset(&s);
    TEST_ASSERT_FALSE(protocore_hmmd_stream_push(NULL, 0xF4u, &r));
    TEST_ASSERT_FALSE(protocore_hmmd_stream_push(&s, 0xF4u, NULL));
    protocore_hmmd_stream_reset(NULL); // must not fault
}

// HLK-LD2410 Serial communication protocol V1.02, Tables 2 and 3: header FD FC FB FA, a 2-octet
// little-endian frame data length covering the command word plus its value, the little-endian
// command word, the value, then MFR 04 03 02 01. Sec 2.2.1 prints the open-command frame and 2.2.2
// the close-command frame in full; the header states this module shares that framing exactly.
void test_ld2410_v102_published_command_envelope(void)
{
    uint8_t buf[32];

    // 2.2.1: word 0x00FF, value 0x0001, so the frame data length is 4
    static const uint8_t OPEN[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF,
                                     0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(14u, protocore_hmmd_cmd_open(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(OPEN, buf, 14u);

    // 2.2.2: word 0x00FE, no value, so the frame data length is 2
    static const uint8_t CLOSE[12] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(12u, protocore_hmmd_cmd_close(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CLOSE, buf, 12u);
}

// The named encoders are the same envelope with the header's command words: firmware 0x0000,
// serial 0x0011, config 0x0008, register 0x0002.
void test_named_command_words(void)
{
    uint8_t buf[32];

    static const uint8_t FIRMWARE[12] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(12u, protocore_hmmd_cmd_read_firmware(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FIRMWARE, buf, 12u);

    static const uint8_t SERIAL[12] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x11, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(12u, protocore_hmmd_cmd_read_serial(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SERIAL, buf, 12u);

    static const uint8_t CONFIG[12] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x08, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(12u, protocore_hmmd_cmd_read_config(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CONFIG, buf, 12u);

    // The register selector is passed through verbatim and counts toward the frame data length.
    static const uint8_t SEL[3] = {0xAA, 0xBB, 0xCC};
    static const uint8_t REGISTER[15] = {0xFD, 0xFC, 0xFB, 0xFA, 0x05, 0x00, 0x02, 0x00,
                                         0xAA, 0xBB, 0xCC, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(15u, protocore_hmmd_cmd_read_register(buf, sizeof(buf), SEL, sizeof(SEL)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REGISTER, buf, 15u);
}

// The frame data length is always 2 (the command word) plus the value octets, and the whole frame
// is 10 more than that, for every value length the builder accepts.
void test_command_length_field_tracks_the_value(void)
{
    static const uint8_t VALUE[16] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                      0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    uint8_t buf[32];
    for (size_t vlen = 0u; vlen <= sizeof(VALUE); vlen++)
    {
        size_t n = protocore_hmmd_cmd_build(buf, sizeof(buf), 0x1234u, vlen ? VALUE : NULL, vlen);
        TEST_ASSERT_EQUAL_size_t(12u + vlen, n);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)((2u + vlen) & 0xFFu), buf[4]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)((2u + vlen) >> 8), buf[5]);
        TEST_ASSERT_EQUAL_HEX8(0x34u, buf[6]); // command word, little-endian
        TEST_ASSERT_EQUAL_HEX8(0x12u, buf[7]);
        if (vlen)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(VALUE, buf + 8, vlen);
        }
        static const uint8_t MFR[4] = {0x04, 0x03, 0x02, 0x01};
        TEST_ASSERT_EQUAL_HEX8_ARRAY(MFR, buf + 8 + vlen, 4u);
    }
}

void test_command_builder_fails_closed(void)
{
    uint8_t buf[32];
    static const uint8_t VALUE[2] = {0x01, 0x00};
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hmmd_cmd_build(NULL, sizeof(buf), 0x00FFu, VALUE, 2u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hmmd_cmd_build(buf, 13u, 0x00FFu, VALUE, 2u)); // needs 14
    TEST_ASSERT_EQUAL_size_t(14u, protocore_hmmd_cmd_build(buf, 14u, 0x00FFu, VALUE, 2u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hmmd_cmd_build(buf, sizeof(buf), 0x00FFu, NULL, 2u)); // no value to copy
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hmmd_cmd_open(buf, 13u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hmmd_cmd_close(buf, 11u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hmmd_cmd_read_firmware(buf, 11u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hmmd_cmd_read_serial(buf, 11u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hmmd_cmd_read_config(buf, 11u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hmmd_cmd_read_register(buf, 11u, NULL, 0u));
}

// An ACK rides the same envelope, so a frame this codec built is a frame this codec parses: the
// command word comes back and everything after it is the payload.
void test_ack_decodes_the_command_word_and_payload(void)
{
    uint8_t buf[32];
    HmmdAck a;

    // The open command as its own echo: word 0x00FF, two payload octets.
    TEST_ASSERT_EQUAL_size_t(14u, protocore_hmmd_cmd_open(buf, sizeof(buf)));
    TEST_ASSERT_TRUE(protocore_hmmd_parse_ack(buf, 14u, &a));
    TEST_ASSERT_EQUAL_HEX16(0x00FFu, a.command);
    TEST_ASSERT_EQUAL_size_t(2u, a.payload_len);
    TEST_ASSERT_EQUAL_HEX8(0x01u, a.payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, a.payload[1]);

    // A word-only frame has no payload at all.
    TEST_ASSERT_EQUAL_size_t(12u, protocore_hmmd_cmd_close(buf, sizeof(buf)));
    TEST_ASSERT_TRUE(protocore_hmmd_parse_ack(buf, 12u, &a));
    TEST_ASSERT_EQUAL_HEX16(0x00FEu, a.command);
    TEST_ASSERT_EQUAL_size_t(0u, a.payload_len);
    TEST_ASSERT_NULL(a.payload);

    // Header: the reply matches on the low octet, so this family's convention of setting bit 8 in
    // the reply still matches the request it answers.
    TEST_ASSERT_TRUE(protocore_hmmd_ack_matches(&a, 0x00FEu));
    TEST_ASSERT_TRUE(protocore_hmmd_ack_matches(&a, 0x01FEu));
    TEST_ASSERT_FALSE(protocore_hmmd_ack_matches(&a, 0x00FFu));
    TEST_ASSERT_FALSE(protocore_hmmd_ack_matches(NULL, 0x00FEu));
}

// The ACK envelope's own guards: header, the declared length framing the buffer exactly, the
// footer, and a length that at least covers the command word.
void test_malformed_ack_frames_are_refused(void)
{
    uint8_t good[14];
    uint8_t bad[14];
    HmmdAck a;
    TEST_ASSERT_EQUAL_size_t(14u, protocore_hmmd_cmd_open(good, sizeof(good)));
    TEST_ASSERT_TRUE(protocore_hmmd_parse_ack(good, sizeof(good), &a));

    TEST_ASSERT_FALSE(protocore_hmmd_parse_ack(NULL, sizeof(good), &a));
    TEST_ASSERT_FALSE(protocore_hmmd_parse_ack(good, sizeof(good), NULL));
    TEST_ASSERT_FALSE(protocore_hmmd_parse_ack(good, 11u, &a)); // shorter than the smallest ACK
    TEST_ASSERT_FALSE(protocore_hmmd_parse_ack(good, 13u, &a)); // the declared length overruns

    for (size_t k = 0; k < 4u; k++)
    {
        memcpy(bad, good, sizeof(good));
        bad[k] ^= 0xFFu;
        TEST_ASSERT_FALSE(protocore_hmmd_parse_ack(bad, sizeof(bad), &a));

        memcpy(bad, good, sizeof(good));
        bad[10u + k] ^= 0xFFu;
        TEST_ASSERT_FALSE(protocore_hmmd_parse_ack(bad, sizeof(bad), &a));
    }

    memcpy(bad, good, sizeof(good));
    bad[4] = 0x01u; // below the two octets of the command word
    TEST_ASSERT_FALSE(protocore_hmmd_parse_ack(bad, sizeof(bad), &a));

    memcpy(bad, good, sizeof(good));
    bad[4] = 0x06u; // a length the buffer cannot hold
    TEST_ASSERT_FALSE(protocore_hmmd_parse_ack(bad, sizeof(bad), &a));

    // The report and command envelopes never accept each other's frames.
    uint8_t report[PROTOCORE_HMMD_FRAME_MAX];
    build_report(report, 1u, 10u, GATES);
    TEST_ASSERT_FALSE(protocore_hmmd_parse_ack(report, sizeof(report), &a));
    HmmdReport r;
    TEST_ASSERT_FALSE(protocore_hmmd_parse_report(good, sizeof(good), &r));
}
