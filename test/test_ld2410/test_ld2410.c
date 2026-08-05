// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the LD2410 mmWave radar codec (services/peripherals/ld2410): decoding a basic and an
// engineering-mode report frame, rejecting malformed frames, the byte-by-byte stream
// reassembler (including resync past leading noise and split feeds), the presence/distance
// helpers, and the exact bytes of the config-command encoders. The UART pump is ESP32-only and
// not exercised here.

#include "services/peripherals/ld2410/ld2410.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// A basic (target) frame: moving target, moving 250cm/80, static 300cm/40, detect 250cm.
static const uint8_t BASIC[] = {
    0xF4, 0xF3, 0xF2, 0xF1, // header
    0x0D, 0x00,             // length = 13
    0x02, 0xAA, 0x01,       // type basic, head, state = moving
    0xFA, 0x00, 0x50,       // moving 250cm, energy 80
    0x2C, 0x01, 0x28,       // static 300cm, energy 40
    0xFA, 0x00,             // detect 250cm
    0x55, 0x00,             // tail, check
    0xF8, 0xF7, 0xF6, 0xF5, // footer
};

// An engineering frame: both targets, with per-gate energies and light/out.
static const uint8_t ENG[] = {
    0xF4, 0xF3, 0xF2, 0xF1,                     //
    0x23, 0x00,                                 // length = 35
    0x01, 0xAA, 0x03,                           // type engineering, head, state = both
    0x64, 0x00, 0x4B,                           // moving 100cm, energy 75
    0x96, 0x00, 0x3C,                           // static 150cm, energy 60
    0xC8, 0x00,                                 // detect 200cm
    0x08, 0x08,                                 // max moving gate 8, max static gate 8
    10,   20,   30,   40,   50, 60, 70, 80, 90, // moving gate energies (9)
    5,    15,   25,   35,   45, 55, 65, 75, 85, // static gate energies (9)
    0x80, 0x01,                                 // light 128, out pin 1
    0x55, 0x00,                                 // tail, check
    0xF8, 0xF7, 0xF6, 0xF5,                     //
};

void test_parse_basic()
{
    Ld2410Report r;
    TEST_ASSERT_TRUE(pc_ld2410_parse_report(BASIC, sizeof(BASIC), &r));
    TEST_ASSERT_EQUAL_UINT8(0, r.engineering);
    TEST_ASSERT_EQUAL_UINT8(LD2410_STATE_MOVING, r.state);
    TEST_ASSERT_EQUAL_UINT16(250, r.moving_cm);
    TEST_ASSERT_EQUAL_UINT8(80, r.moving_energy);
    TEST_ASSERT_EQUAL_UINT16(300, r.static_cm);
    TEST_ASSERT_EQUAL_UINT8(40, r.static_energy);
    TEST_ASSERT_EQUAL_UINT16(250, r.detect_cm);
}

void test_parse_engineering()
{
    Ld2410Report r;
    TEST_ASSERT_TRUE(pc_ld2410_parse_report(ENG, sizeof(ENG), &r));
    TEST_ASSERT_EQUAL_UINT8(1, r.engineering);
    TEST_ASSERT_EQUAL_UINT8(LD2410_STATE_BOTH, r.state);
    TEST_ASSERT_EQUAL_UINT16(100, r.moving_cm);
    TEST_ASSERT_EQUAL_UINT16(150, r.static_cm);
    TEST_ASSERT_EQUAL_UINT16(200, r.detect_cm);
    TEST_ASSERT_EQUAL_UINT8(8, r.max_moving_gate);
    TEST_ASSERT_EQUAL_UINT8(8, r.max_static_gate);
    TEST_ASSERT_EQUAL_UINT8(10, r.moving_gate_energy[0]);
    TEST_ASSERT_EQUAL_UINT8(90, r.moving_gate_energy[8]);
    TEST_ASSERT_EQUAL_UINT8(5, r.static_gate_energy[0]);
    TEST_ASSERT_EQUAL_UINT8(85, r.static_gate_energy[8]);
    TEST_ASSERT_EQUAL_UINT8(128, r.light);
    TEST_ASSERT_EQUAL_UINT8(1, r.out_pin);
}

void test_reject_malformed()
{
    Ld2410Report r;
    uint8_t bad[sizeof(BASIC)];
    // bad header
    memcpy(bad, BASIC, sizeof(BASIC));
    bad[0] = 0x00;
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(bad, sizeof(BASIC), &r));
    // bad footer
    memcpy(bad, BASIC, sizeof(BASIC));
    bad[21] = 0x00;
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(bad, sizeof(BASIC), &r));
    // wrong length field for the buffer
    memcpy(bad, BASIC, sizeof(BASIC));
    bad[4] = 0x0C;
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(bad, sizeof(BASIC), &r));
    // missing head marker
    memcpy(bad, BASIC, sizeof(BASIC));
    bad[7] = 0x00;
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(bad, sizeof(BASIC), &r));
    // bad tail
    memcpy(bad, BASIC, sizeof(BASIC));
    bad[17] = 0x00;
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(bad, sizeof(BASIC), &r));
    // unknown data type
    memcpy(bad, BASIC, sizeof(BASIC));
    bad[6] = 0x09;
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(bad, sizeof(BASIC), &r));
    // short buffer
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(BASIC, 10, &r));
}

void test_stream_resync_and_split()
{
    Ld2410Stream s;
    pc_ld2410_stream_reset(&s);
    Ld2410Report r;
    int reports = 0;

    // Leading noise, including a false partial header (F4 then a non-F3), then the real frame.
    const uint8_t noise[] = {0x00, 0xFF, 0xF4, 0x11, 0xF4, 0xF3, 0x99};
    for (unsigned i = 0; i < sizeof(noise); i++)
    {
        if (pc_ld2410_stream_push(&s, noise[i], &r))
        {
            reports++;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, reports);

    // Feed the whole basic frame one byte at a time; exactly one report should complete.
    for (unsigned i = 0; i < sizeof(BASIC); i++)
    {
        if (pc_ld2410_stream_push(&s, BASIC[i], &r))
        {
            reports++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, reports);
    TEST_ASSERT_EQUAL_UINT16(250, r.moving_cm);

    // A second frame back-to-back proves the stream reset itself.
    for (unsigned i = 0; i < sizeof(ENG); i++)
    {
        if (pc_ld2410_stream_push(&s, ENG[i], &r))
        {
            reports++;
        }
    }
    TEST_ASSERT_EQUAL_INT(2, reports);
    TEST_ASSERT_EQUAL_UINT8(1, r.engineering);
}

void test_stream_absurd_length_drops()
{
    Ld2410Stream s;
    pc_ld2410_stream_reset(&s);
    Ld2410Report r;
    // header + a length larger than the frame buffer -> must drop and resync, then decode.
    const uint8_t huge[] = {0xF4, 0xF3, 0xF2, 0xF1, 0xFF, 0xFF};
    for (unsigned i = 0; i < sizeof(huge); i++)
    {
        TEST_ASSERT_FALSE(pc_ld2410_stream_push(&s, huge[i], &r));
    }
    int reports = 0;
    for (unsigned i = 0; i < sizeof(BASIC); i++)
    {
        if (pc_ld2410_stream_push(&s, BASIC[i], &r))
        {
            reports++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, reports);
}

void test_helpers()
{
    Ld2410Report r;
    pc_ld2410_parse_report(BASIC, sizeof(BASIC), &r);
    TEST_ASSERT_TRUE(pc_ld2410_present(&r));
    TEST_ASSERT_EQUAL_UINT16(250, pc_ld2410_distance_cm(&r)); // moving -> moving distance

    r.state = LD2410_STATE_STATIC;
    TEST_ASSERT_EQUAL_UINT16(300, pc_ld2410_distance_cm(&r)); // static -> static distance
    r.state = LD2410_STATE_NONE;
    TEST_ASSERT_FALSE(pc_ld2410_present(&r));
    TEST_ASSERT_EQUAL_UINT16(0, pc_ld2410_distance_cm(&r));
    TEST_ASSERT_FALSE(pc_ld2410_present(NULL));
}

void test_command_encoders()
{
    uint8_t f[32];
    const uint8_t enable[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_INT((int)(sizeof(enable)), (int)(pc_ld2410_cmd_config_enable(f, sizeof(f))));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(enable, f, sizeof(enable));

    const uint8_t end[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_INT((int)(sizeof(end)), (int)(pc_ld2410_cmd_config_end(f, sizeof(f))));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(end, f, sizeof(end));

    const uint8_t eng_on[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x62, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_INT((int)(sizeof(eng_on)), (int)(pc_ld2410_cmd_engineering(f, sizeof(f), PROTO_TRUE)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(eng_on, f, sizeof(eng_on));

    const uint8_t eng_off[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x63, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_INT((int)(sizeof(eng_off)), (int)(pc_ld2410_cmd_engineering(f, sizeof(f), PROTO_FALSE)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(eng_off, f, sizeof(eng_off));

    const uint8_t restart[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xA3, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_INT((int)(sizeof(restart)), (int)(pc_ld2410_cmd_restart(f, sizeof(f))));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(restart, f, sizeof(restart));

    // Too-small buffer must refuse, not overrun.
    TEST_ASSERT_EQUAL_INT((int)(0), (int)(pc_ld2410_cmd_config_enable(f, 4)));
}

void test_host_stubs_and_parse_guards()
{
    // begin() opens the unit and the config commands reach the wire; nothing has arrived yet, so
    // there is no report to hand back.
    TEST_ASSERT_TRUE(pc_ld2410_begin(16, 17));
    TEST_ASSERT_FALSE(pc_ld2410_poll());
    TEST_ASSERT_NULL(pc_ld2410_last());
    TEST_ASSERT_TRUE(pc_ld2410_set_engineering(PROTO_TRUE));
    TEST_ASSERT_TRUE(pc_ld2410_restart());
    // Malformed report frames fail closed.
    Ld2410Report rep;
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(NULL, 20, &rep));
    uint8_t too_short[4] = {0xF4, 0xF3, 0xF2, 0xF1};
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(too_short, sizeof(too_short), &rep));
}

// --- LD2410B ---------------------------------------------------------------
// The B is the BLE build of the same radar on the same `FD FC FB FA` protocol; these are the two
// commands that exist only on it. The expected frames are the worked examples from the LD2410B
// serial communication protocol document, byte for byte.

void test_ld2410b_command_encoders(void)
{
    uint8_t buf[32];

    // "FD FC FB FA | 04 00 | A4 00 | 01 00 | 04 03 02 01"  (Bluetooth on)
    static const uint8_t want_bt_on[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xA4,
                                           0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_UINT32(14, (uint32_t)pc_ld2410_cmd_bluetooth(buf, sizeof(buf), PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_bt_on, buf, 14);

    // Bluetooth off differs only in the value word (0x0000)
    TEST_ASSERT_EQUAL_UINT32(14, (uint32_t)pc_ld2410_cmd_bluetooth(buf, sizeof(buf), PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[8]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[9]);

    // "FD FC FB FA | 04 00 | A5 00 | 01 00 | 04 03 02 01"  (query MAC)
    static const uint8_t want_mac_q[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xA5,
                                           0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_UINT32(14, (uint32_t)pc_ld2410_cmd_get_mac(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_mac_q, buf, 14);

    // "FD FC FB FA | 08 00 | A9 00 | 48 69 4C 69 6E 6B | 04 03 02 01"  (set BT password "HiLink",
    // the factory default the spec uses as its worked example - ASCII in natural order)
    static const uint8_t want_pw[18] = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0xA9, 0x00, 0x48,
                                        0x69, 0x4C, 0x69, 0x6E, 0x6B, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_UINT32(18, (uint32_t)pc_ld2410_cmd_set_bt_password(buf, sizeof(buf), "HiLink"));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_pw, buf, 18);

    // capacity guards
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_ld2410_cmd_bluetooth(buf, 13, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_ld2410_cmd_get_mac(buf, 13));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_ld2410_cmd_get_mac(NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_ld2410_cmd_set_bt_password(buf, 17, "HiLink"));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_ld2410_cmd_set_bt_password(buf, sizeof(buf), NULL));
}

void test_ld2410b_ack_decoding(void)
{
    Ld2410Ack ack;

    // get-MAC ACK: "FD FC FB FA | 0A 00 | A5 01 | 00 00 | 8F 27 2E B8 0F 65 | 04 03 02 01"
    static const uint8_t mac_ack[20] = {0xFD, 0xFC, 0xFB, 0xFA, 0x0A, 0x00, 0xA5, 0x01, 0x00, 0x00,
                                        0x8F, 0x27, 0x2E, 0xB8, 0x0F, 0x65, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_TRUE(pc_ld2410_parse_ack(mac_ack, sizeof(mac_ack), &ack));
    TEST_ASSERT_EQUAL_UINT16(0x01A5, ack.command); // request word | 0x0100
    TEST_ASSERT_EQUAL_UINT16(0, ack.status);
    TEST_ASSERT_TRUE(pc_ld2410_ack_ok(&ack));
    TEST_ASSERT_EQUAL_UINT32(6, (uint32_t)ack.payload_len);

    static const uint8_t want_mac[6] = {0x8F, 0x27, 0x2E, 0xB8, 0x0F, 0x65};
    uint8_t mac[6];
    TEST_ASSERT_TRUE(pc_ld2410_ack_mac(&ack, mac));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_mac, mac, 6);

    // Bluetooth-on ACK: no data beyond the status word
    static const uint8_t bt_ack[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xA4,
                                       0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_TRUE(pc_ld2410_parse_ack(bt_ack, sizeof(bt_ack), &ack));
    TEST_ASSERT_EQUAL_UINT16(0x01A4, ack.command);
    TEST_ASSERT_TRUE(pc_ld2410_ack_ok(&ack));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)ack.payload_len);
    TEST_ASSERT_NULL(ack.payload);
    // a non-MAC ACK must not yield a MAC
    TEST_ASSERT_FALSE(pc_ld2410_ack_mac(&ack, mac));

    // a failure status is decoded but reported as not-ok, and yields no MAC
    uint8_t fail_ack[14];
    memcpy(fail_ack, bt_ack, sizeof(fail_ack));
    fail_ack[8] = 0x01; // status = 1 (failure)
    TEST_ASSERT_TRUE(pc_ld2410_parse_ack(fail_ack, sizeof(fail_ack), &ack));
    TEST_ASSERT_FALSE(pc_ld2410_ack_ok(&ack));
}

void test_ld2410b_ack_rejects_malformed(void)
{
    Ld2410Ack ack;
    static const uint8_t good[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xA4,
                                     0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    uint8_t bad[20];

    TEST_ASSERT_FALSE(pc_ld2410_parse_ack(NULL, 14, &ack));
    TEST_ASSERT_FALSE(pc_ld2410_parse_ack(good, 14, NULL));
    TEST_ASSERT_FALSE(pc_ld2410_parse_ack(good, 13, &ack)); // under the minimum

    memcpy(bad, good, 14);
    bad[0] = 0xF4; // report-frame header, not a command header
    TEST_ASSERT_FALSE(pc_ld2410_parse_ack(bad, 14, &ack));

    memcpy(bad, good, 14);
    bad[13] = 0xFF; // corrupt footer
    TEST_ASSERT_FALSE(pc_ld2410_parse_ack(bad, 14, &ack));

    // declared intra-frame length that disagrees with the buffer, both ways
    memcpy(bad, good, 14);
    bad[4] = 0x06;
    TEST_ASSERT_FALSE(pc_ld2410_parse_ack(bad, 14, &ack));
    memcpy(bad, good, 14);
    bad[4] = 0x03; // shorter than word+status
    TEST_ASSERT_FALSE(pc_ld2410_parse_ack(bad, 14, &ack));
}

// --- extra branch coverage --------------------------------------------------
// The tests above already exercise the common paths; these fill in the remaining guard
// branches that gcovr's baseline flagged as never taken: a null `out`, a data-type byte whose
// declared length disagrees with LEN_BASIC/LEN_ENGINEERING, and a corrupt engineering-frame tail.

void test_parse_report_more_branches(void)
{
    Ld2410Report r;

    // out == NULL on an otherwise well-formed frame must still fail closed.
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(BASIC, sizeof(BASIC), NULL));

    // type byte says "basic" (0x02) but the declared intra-frame length is not LEN_BASIC (13),
    // while the length field still frames the buffer exactly and the footer is intact.
    static const uint8_t bad_basic_len[24] = {
        0xF4, 0xF3, 0xF2, 0xF1,                   // header
        0x0E, 0x00,                               // length = 14 (!= LEN_BASIC)
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // payload: type basic, ...
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ... (14 bytes total)
        0xF8, 0xF7, 0xF6, 0xF5,                   // footer
    };
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(bad_basic_len, sizeof(bad_basic_len), &r));

    // type byte says "engineering" (0x01) but the declared length is not LEN_ENGINEERING (35).
    static const uint8_t bad_eng_len[40] = {
        0xF4, 0xF3, 0xF2, 0xF1,                                     // header
        0x1E, 0x00,                                                 // length = 30 (!= LEN_ENGINEERING)
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // payload: type engineering,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ... zero-filled
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ... (30 bytes total)
        0xF8, 0xF7, 0xF6, 0xF5,                                     // footer
    };
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(bad_eng_len, sizeof(bad_eng_len), &r));

    // A well-formed engineering frame with a corrupt tail marker (byte 33 of the payload).
    uint8_t bad_eng_tail[sizeof(ENG)];
    memcpy(bad_eng_tail, ENG, sizeof(ENG));
    bad_eng_tail[39] = 0x00; // was 0x55
    TEST_ASSERT_FALSE(pc_ld2410_parse_report(bad_eng_tail, sizeof(bad_eng_tail), &r));
}

void test_stream_header_partial_resync(void)
{
    Ld2410Stream s;
    pc_ld2410_stream_reset(&s);
    Ld2410Report r;
    int reports = 0;

    // F4 (matches [0]), F3 (matches [1]), then F4 again while [2]=F2 was expected: must resync
    // to hdr_match=1 (the byte equals HDR[0] mid-resync), not drop back to 0.
    const uint8_t partial[] = {0xF4, 0xF3, 0xF4};
    for (unsigned i = 0; i < sizeof(partial); i++)
    {
        TEST_ASSERT_FALSE(pc_ld2410_stream_push(&s, partial[i], &r));
    }

    // Completing the header from that resynced position, then the rest of BASIC's bytes (from
    // its length field onward), must still decode a full, correct report.
    const uint8_t rest[] = {0xF3, 0xF2, 0xF1};
    for (unsigned i = 0; i < sizeof(rest); i++)
    {
        TEST_ASSERT_FALSE(pc_ld2410_stream_push(&s, rest[i], &r));
    }
    for (unsigned i = 4; i < sizeof(BASIC); i++)
    {
        if (pc_ld2410_stream_push(&s, BASIC[i], &r))
        {
            reports++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, reports);
    TEST_ASSERT_EQUAL_UINT16(250, r.moving_cm);
}

void test_distance_cm_and_ack_extra_branches(void)
{
    // Null-report guard, and the state == BOTH arm (moving distance wins).
    TEST_ASSERT_EQUAL_UINT16(0, pc_ld2410_distance_cm(NULL));

    Ld2410Report r;
    TEST_ASSERT_TRUE(pc_ld2410_parse_report(ENG, sizeof(ENG), &r));
    TEST_ASSERT_EQUAL_UINT8(LD2410_STATE_BOTH, r.state);
    TEST_ASSERT_EQUAL_UINT16(100, pc_ld2410_distance_cm(&r)); // BOTH -> moving distance

    TEST_ASSERT_FALSE(pc_ld2410_ack_ok(NULL));

    // ack_mac guard branches a well-formed pc_ld2410_parse_ack() output never reaches: a null
    // ack, a null mac, a non-zero status, a too-short payload, and payload_len satisfied but a
    // null payload pointer (parse_ack always pairs payload_len==0 with a null payload, so the
    // last case is built directly on a plain struct rather than via parse_ack).
    uint8_t mac[6];
    TEST_ASSERT_FALSE(pc_ld2410_ack_mac(NULL, mac));

    Ld2410Ack ack;
    memset(&ack, 0, sizeof(ack));
    ack.command = 0x01A5;
    ack.status = 0;
    static const uint8_t payload6[6] = {1, 2, 3, 4, 5, 6};
    ack.payload = payload6;
    ack.payload_len = 6;
    TEST_ASSERT_FALSE(pc_ld2410_ack_mac(&ack, NULL)); // mac == NULL

    ack.status = 1; // non-zero status
    TEST_ASSERT_FALSE(pc_ld2410_ack_mac(&ack, mac));
    ack.status = 0;

    ack.payload_len = 5; // short payload
    TEST_ASSERT_FALSE(pc_ld2410_ack_mac(&ack, mac));

    ack.payload_len = 6;
    ack.payload = NULL; // payload_len satisfied but no payload pointer
    TEST_ASSERT_FALSE(pc_ld2410_ack_mac(&ack, mac));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_basic);
    RUN_TEST(test_parse_engineering);
    RUN_TEST(test_reject_malformed);
    RUN_TEST(test_stream_resync_and_split);
    RUN_TEST(test_stream_absurd_length_drops);
    RUN_TEST(test_helpers);
    RUN_TEST(test_command_encoders);
    RUN_TEST(test_host_stubs_and_parse_guards);
    RUN_TEST(test_ld2410b_command_encoders);
    RUN_TEST(test_ld2410b_ack_decoding);
    RUN_TEST(test_ld2410b_ack_rejects_malformed);
    RUN_TEST(test_parse_report_more_branches);
    RUN_TEST(test_stream_header_partial_resync);
    RUN_TEST(test_distance_cm_and_ack_extra_branches);
    return UNITY_END();
}
