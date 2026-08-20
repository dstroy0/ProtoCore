// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the DShot ESC throttle codec (server/peripherals/dshot/dshot.h).
//
// DShot has no standards body; its published definition is the Betaflight DShot document
// (betaflight.com/docs/development/API/Dshot), which fixes the frame as SSSSSSSSSSSTCCCC - 11 value
// bits, the telemetry request, and a 4-bit checksum - with
//     crc = (value ^ (value >> 4) ^ (value >> 8)) & 0x0F
// over the 12-bit value:telemetry word, inverted for bidirectional DShot, values 1..47 reserved for
// commands and 48..2047 throttle, and a bit-timing table of T1H / T0H per rate.
//
// test_published_crc_over_worked_frames is the load-bearing case: every expected frame there is
// that formula worked out nibble by nibble in the comment beside it, so the encoder cannot pass by
// agreeing with itself.

#include "server/peripherals/dshot/dshot.h"

#include <unity.h>

static uint8_t dshot_work[16]; // the borrow an entry takes; Dshot never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// The frame is (value << 5) | (telemetry << 4) | crc, and the crc is the xor of the three nibbles
// of the 12-bit (value << 1 | telemetry) word. Each line below is that xor written out.
void test_published_crc_over_worked_frames(void)
{
    struct
    {
        uint16_t value;
        proto_bool tlm;
        uint16_t want;
    } static const CASES[] = {
        // value 0, tlm 0: v12 = 0x000, nibbles 0^0^0 = 0 -> frame 0x0000
        {0u, PROTO_FALSE, 0x0000u},
        // value 0, tlm 1: v12 = 0x001, nibbles 0^0^1 = 1 -> frame 0x0011
        {0u, PROTO_TRUE, 0x0011u},
        // value 1, tlm 0: v12 = 0x002, nibbles 0^0^2 = 2 -> frame 0x0022
        {1u, PROTO_FALSE, 0x0022u},
        // DSHOT_CMD_BEACON5 with telemetry: v12 = (5<<1)|1 = 0x00B, 0^0^B = B -> frame 0x00BB
        {5u, PROTO_TRUE, 0x00BBu},
        // value 48 (first throttle step): v12 = 0x060, 0^6^0 = 6 -> frame 0x0606
        {48u, PROTO_FALSE, 0x0606u},
        // value 1000: v12 = 1000*2 = 2000 = 0x7D0, nibbles 0^D^7 = A -> frame 0x7D0A
        {1000u, PROTO_FALSE, 0x7D0Au},
        // value 1000 with telemetry: v12 = 0x7D1, 1^D^7 = B -> frame 0x7D1B
        {1000u, PROTO_TRUE, 0x7D1Bu},
        // value 2047 (max): v12 = 0xFFE, E^F^F = E -> frame 0xFFEE
        {2047u, PROTO_FALSE, 0xFFEEu},
        // value 2047 with telemetry: v12 = 0xFFF, F^F^F = F -> frame 0xFFFF
        {2047u, PROTO_TRUE, 0xFFFFu},
    };

    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        DshotV.encode_args.value11 = CASES[i].value;
        DshotV.encode_args.telemetry = CASES[i].tlm;
        DshotV.encode_args.bidirectional = PROTO_FALSE;
        Dshot.encode(dshot_work);
        TEST_ASSERT_EQUAL_HEX16(CASES[i].want, DshotV.frame);
    }
}

// Bidirectional DShot inverts the checksum, so the same value differs from the normal frame in
// exactly the low nibble, and in all four of its bits.
void test_bidirectional_inverts_only_the_checksum(void)
{
    // value 1000: normal crc A -> inverted 5, so 0x7D0A becomes 0x7D05
    DshotV.encode_args.value11 = 1000u;
    DshotV.encode_args.telemetry = PROTO_FALSE;
    DshotV.encode_args.bidirectional = PROTO_TRUE;
    Dshot.encode(dshot_work);
    TEST_ASSERT_EQUAL_HEX16(0x7D05u, DshotV.frame);
    // value 0: normal crc 0 -> inverted F
    DshotV.encode_args.value11 = 0u;
    DshotV.encode_args.telemetry = PROTO_FALSE;
    DshotV.encode_args.bidirectional = PROTO_TRUE;
    Dshot.encode(dshot_work);
    TEST_ASSERT_EQUAL_HEX16(0x000Fu, DshotV.frame);
    // value 2047 with telemetry: normal crc F -> inverted 0
    DshotV.encode_args.value11 = 2047u;
    DshotV.encode_args.telemetry = PROTO_TRUE;
    DshotV.encode_args.bidirectional = PROTO_TRUE;
    Dshot.encode(dshot_work);
    TEST_ASSERT_EQUAL_HEX16(0xFFF0u, DshotV.frame);

    for (uint16_t v = 0u; v <= DSHOT_VALUE_MAX; v = (uint16_t)(v + 7u))
    {
        DshotV.encode_args.value11 = v;
        DshotV.encode_args.telemetry = PROTO_FALSE;
        DshotV.encode_args.bidirectional = PROTO_FALSE;
        Dshot.encode(dshot_work);
        uint16_t normal = DshotV.frame;
        DshotV.encode_args.value11 = v;
        DshotV.encode_args.telemetry = PROTO_FALSE;
        DshotV.encode_args.bidirectional = PROTO_TRUE;
        Dshot.encode(dshot_work);
        uint16_t bidir = DshotV.frame;
        TEST_ASSERT_EQUAL_HEX16(0x000Fu, (uint16_t)(normal ^ bidir));
    }
}

// Encode then decode returns the value and telemetry bit unchanged, in both CRC conventions, over
// the whole 11-bit value domain.
void test_encode_decode_round_trip_over_the_whole_value_domain(void)
{
    for (uint16_t v = 0u; v <= DSHOT_VALUE_MAX; v++)
    {
        for (int t = 0; t < 2; t++)
        {
            for (int b = 0; b < 2; b++)
            {
                proto_bool tlm = t ? PROTO_TRUE : PROTO_FALSE;
                proto_bool bidir = b ? PROTO_TRUE : PROTO_FALSE;
                DshotV.encode_args.value11 = v;
                DshotV.encode_args.telemetry = tlm;
                DshotV.encode_args.bidirectional = bidir;
                Dshot.encode(dshot_work);
                uint16_t frame = DshotV.frame;
                uint16_t got_v = 0xFFFFu;
                proto_bool got_t = PROTO_FALSE;
                DshotV.decode_args.frame = frame;
                DshotV.decode_args.value11 = &got_v;
                DshotV.decode_args.telemetry = &got_t;
                DshotV.decode_args.bidirectional = bidir;
                Dshot.decode(dshot_work);
                TEST_ASSERT_TRUE(DshotV.ok);
                TEST_ASSERT_EQUAL_HEX16(v, got_v);
                TEST_ASSERT_EQUAL_INT(t ? 1 : 0, got_t ? 1 : 0);
            }
        }
    }
}

// A frame whose checksum does not match is rejected: an ESC that acted on a corrupted frame would
// act on a corrupted throttle. Every single-bit error in the 16-bit frame is caught, because the
// xor-of-nibbles checksum changes for any one bit flipped.
void test_every_single_bit_error_is_rejected(void)
{
    static const uint16_t VALUE[] = {0u, 48u, 1000u, 1500u, 2047u};
    for (size_t i = 0; i < sizeof(VALUE) / sizeof(VALUE[0]); i++)
    {
        DshotV.encode_args.value11 = VALUE[i];
        DshotV.encode_args.telemetry = PROTO_FALSE;
        DshotV.encode_args.bidirectional = PROTO_FALSE;
        Dshot.encode(dshot_work);
        uint16_t frame = DshotV.frame;
        DshotV.decode_args.frame = frame;
        DshotV.decode_args.value11 = NULL;
        DshotV.decode_args.telemetry = NULL;
        DshotV.decode_args.bidirectional = PROTO_FALSE;
        Dshot.decode(dshot_work);
        TEST_ASSERT_TRUE(DshotV.ok);
        for (int bit = 0; bit < 16; bit++)
        {
            uint16_t bad = (uint16_t)(frame ^ (1u << bit));
            DshotV.decode_args.frame = bad;
            DshotV.decode_args.value11 = NULL;
            DshotV.decode_args.telemetry = NULL;
            DshotV.decode_args.bidirectional = PROTO_FALSE;
            Dshot.decode(dshot_work);
            TEST_ASSERT_FALSE_MESSAGE(DshotV.ok, "bit error accepted");
        }
    }
}

// The two CRC conventions never accept each other's frames, so an ESC configured for one cannot be
// driven by the other by accident.
void test_the_two_crc_conventions_do_not_accept_each_other(void)
{
    for (uint16_t v = 0u; v <= DSHOT_VALUE_MAX; v = (uint16_t)(v + 13u))
    {
        DshotV.encode_args.value11 = v;
        DshotV.encode_args.telemetry = PROTO_FALSE;
        DshotV.encode_args.bidirectional = PROTO_FALSE;
        Dshot.encode(dshot_work);
        uint16_t normal = DshotV.frame;
        DshotV.encode_args.value11 = v;
        DshotV.encode_args.telemetry = PROTO_FALSE;
        DshotV.encode_args.bidirectional = PROTO_TRUE;
        Dshot.encode(dshot_work);
        uint16_t bidir = DshotV.frame;
        DshotV.decode_args.frame = normal;
        DshotV.decode_args.value11 = NULL;
        DshotV.decode_args.telemetry = NULL;
        DshotV.decode_args.bidirectional = PROTO_TRUE;
        Dshot.decode(dshot_work);
        TEST_ASSERT_FALSE(DshotV.ok);
        DshotV.decode_args.frame = bidir;
        DshotV.decode_args.value11 = NULL;
        DshotV.decode_args.telemetry = NULL;
        DshotV.decode_args.bidirectional = PROTO_FALSE;
        Dshot.decode(dshot_work);
        TEST_ASSERT_FALSE(DshotV.ok);
    }
}

// Header: "value11 above 2047 is masked to 11 bits", so the value field cannot spill into the
// telemetry bit or the checksum.
void test_values_wider_than_eleven_bits_are_masked(void)
{
    // Each pair is captured a frame at a time: both encodes report through the one namespace, so
    // comparing them in a single expression would compare the second frame with itself.
    static const struct
    {
        uint16_t inside;
        uint16_t wider;
        proto_bool tlm;
    } PAIR[] = {
        {0u, 0x0800u, PROTO_FALSE},   // bit 11 set
        {1u, 0xF801u, PROTO_FALSE},   // bits 11..15 set
        {2047u, 0xFFFFu, PROTO_TRUE}, // every bit above the field set
    };
    for (size_t i = 0; i < sizeof(PAIR) / sizeof(PAIR[0]); i++)
    {
        DshotV.encode_args.value11 = PAIR[i].inside;
        DshotV.encode_args.telemetry = PAIR[i].tlm;
        DshotV.encode_args.bidirectional = PROTO_FALSE;
        Dshot.encode(dshot_work);
        const uint16_t inside = DshotV.frame;
        DshotV.encode_args.value11 = PAIR[i].wider;
        DshotV.encode_args.telemetry = PAIR[i].tlm;
        DshotV.encode_args.bidirectional = PROTO_FALSE;
        Dshot.encode(dshot_work);
        TEST_ASSERT_EQUAL_HEX16(inside, DshotV.frame);
    }
}

// The value domain the document publishes: 0 disarm, 1..47 commands, 48..2047 throttle. Each named
// command lands in the reserved band and each decodes back to itself.
void test_published_command_and_throttle_domains(void)
{
    static const uint16_t CMD[] = {
        DSHOT_CMD_MOTOR_STOP,       DSHOT_CMD_BEACON1,          DSHOT_CMD_BEACON5,     DSHOT_CMD_ESC_INFO,
        DSHOT_CMD_SPIN_DIRECTION_1, DSHOT_CMD_SPIN_DIRECTION_2, DSHOT_CMD_3D_MODE_OFF, DSHOT_CMD_3D_MODE_ON,
        DSHOT_CMD_SETTINGS_REQUEST, DSHOT_CMD_SAVE_SETTINGS,
    };
    for (size_t i = 0; i < sizeof(CMD) / sizeof(CMD[0]); i++)
    {
        TEST_ASSERT_TRUE(CMD[i] < DSHOT_THROTTLE_MIN);
        uint16_t got = 0xFFFFu;
        // The encoded frame is captured into a local before the decode: both calls report through
        // the one namespace, so nesting them would have the decode read its own outcome.
        DshotV.encode_args.value11 = CMD[i];
        DshotV.encode_args.telemetry = PROTO_FALSE;
        DshotV.encode_args.bidirectional = PROTO_FALSE;
        Dshot.encode(dshot_work);
        const uint16_t frame = DshotV.frame;
        DshotV.decode_args.frame = frame;
        DshotV.decode_args.value11 = &got;
        DshotV.decode_args.telemetry = NULL;
        DshotV.decode_args.bidirectional = PROTO_FALSE;
        Dshot.decode(dshot_work);
        TEST_ASSERT_TRUE(DshotV.ok);
        TEST_ASSERT_EQUAL_HEX16(CMD[i], got);
    }
    TEST_ASSERT_EQUAL_UINT16(48u, DSHOT_THROTTLE_MIN);
    TEST_ASSERT_EQUAL_UINT16(2047u, DSHOT_THROTTLE_MAX);
    TEST_ASSERT_EQUAL_UINT16(2047u, DSHOT_VALUE_MAX);
    // 48..2047 is 2000 throttle steps
    TEST_ASSERT_EQUAL_UINT16(2000u, (uint16_t)(DSHOT_THROTTLE_MAX - DSHOT_THROTTLE_MIN + 1u));
}

// Betaflight's bit-timing table, in nanoseconds. The bit period is 1/bitrate and the header sets
// T1H at 3/4 of it and T0H at 3/8:
//   DShot150 : 1e9/150000  = 6666.67 -> 6667 ns; 3/4 = 5000, 3/8 = 2500  (published 5.00 / 2.50 us)
//   DShot300 : 1e9/300000  = 3333.33 -> 3333 ns; 3/4 = 2499, 3/8 = 1249  (published 2.50 / 1.25 us)
//   DShot600 : 1e9/600000  = 1666.67 -> 1667 ns; 3/4 = 1250, 3/8 =  625  (published 1.25 / 0.625 us)
//   DShot1200: 1e9/1200000 =  833.33 ->  833 ns; 3/4 =  624, 3/8 =  312  (published 0.625 / 0.313 us)
void test_bit_timing_is_three_quarters_and_three_eighths_of_the_period(void)
{
    struct
    {
        uint16_t rate;
        uint32_t period;
    } static const RATE[] = {{150u, 6667u}, {300u, 3333u}, {600u, 1667u}, {1200u, 833u}};

    for (size_t i = 0; i < sizeof(RATE) / sizeof(RATE[0]); i++)
    {
        DshotV.bit_ns_args.rate_kbit = RATE[i].rate;
        DshotV.bit_ns_args.bit = PROTO_TRUE;
        Dshot.bit_ns(dshot_work);
        uint32_t one = DshotV.ns;
        DshotV.bit_ns_args.rate_kbit = RATE[i].rate;
        DshotV.bit_ns_args.bit = PROTO_FALSE;
        Dshot.bit_ns(dshot_work);
        uint32_t zero = DshotV.ns;
        TEST_ASSERT_EQUAL_UINT32(RATE[i].period * 3u / 4u, one);
        TEST_ASSERT_EQUAL_UINT32(RATE[i].period * 3u / 8u, zero);
        // T1H is twice T0H to within the integer truncation of a single nanosecond
        TEST_ASSERT_TRUE(one - (2u * zero) <= 1u);
        TEST_ASSERT_TRUE(one < RATE[i].period); // the line must return low inside the bit period
    }
    DshotV.bit_ns_args.rate_kbit = 150u;
    DshotV.bit_ns_args.bit = PROTO_TRUE;
    Dshot.bit_ns(dshot_work);
    TEST_ASSERT_EQUAL_UINT32(5000u, DshotV.ns);
    DshotV.bit_ns_args.rate_kbit = 150u;
    DshotV.bit_ns_args.bit = PROTO_FALSE;
    Dshot.bit_ns(dshot_work);
    TEST_ASSERT_EQUAL_UINT32(2500u, DshotV.ns);
}

// Header: an unknown rate returns 0 rather than a plausible-looking pulse width.
void test_unknown_bit_rates_return_zero(void)
{
    DshotV.bit_ns_args.rate_kbit = 0u;
    DshotV.bit_ns_args.bit = PROTO_TRUE;
    Dshot.bit_ns(dshot_work);
    TEST_ASSERT_EQUAL_UINT32(0u, DshotV.ns);
    DshotV.bit_ns_args.rate_kbit = 1u;
    DshotV.bit_ns_args.bit = PROTO_FALSE;
    Dshot.bit_ns(dshot_work);
    TEST_ASSERT_EQUAL_UINT32(0u, DshotV.ns);
    DshotV.bit_ns_args.rate_kbit = 500u;
    DshotV.bit_ns_args.bit = PROTO_TRUE;
    Dshot.bit_ns(dshot_work);
    TEST_ASSERT_EQUAL_UINT32(0u, DshotV.ns);
    DshotV.bit_ns_args.rate_kbit = 2400u;
    DshotV.bit_ns_args.bit = PROTO_TRUE;
    Dshot.bit_ns(dshot_work);
    TEST_ASSERT_EQUAL_UINT32(0u, DshotV.ns);
}

// The analog protocols' published pulse-width ranges: PWM 1000-2000 us, OneShot125 125-250 us,
// OneShot42 42-84 us, Multishot 5-25 us. Throttle 0 is the low end, 1000 the high end, and the
// midpoint is the arithmetic mean since the map is linear.
void test_analog_pulse_width_endpoints_and_midpoint(void)
{
    struct
    {
        protocore_esc_pwm mode;
        uint32_t lo_ns;
        uint32_t hi_ns;
    } static const MODE[] = {
        {PROTOCORE_ESC_PWM, 1000000u, 2000000u},
        {PROTOCORE_ESC_ONESHOT125, 125000u, 250000u},
        {PROTOCORE_ESC_ONESHOT42, 42000u, 84000u},
        {PROTOCORE_ESC_MULTISHOT, 5000u, 25000u},
    };
    for (size_t i = 0; i < sizeof(MODE) / sizeof(MODE[0]); i++)
    {
        DshotV.esc_pwm_ns_args.throttle_1000 = 0u;
        DshotV.esc_pwm_ns_args.mode = MODE[i].mode;
        Dshot.esc_pwm_ns(dshot_work);
        TEST_ASSERT_EQUAL_UINT32(MODE[i].lo_ns, DshotV.ns);
        DshotV.esc_pwm_ns_args.throttle_1000 = 1000u;
        DshotV.esc_pwm_ns_args.mode = MODE[i].mode;
        Dshot.esc_pwm_ns(dshot_work);
        TEST_ASSERT_EQUAL_UINT32(MODE[i].hi_ns, DshotV.ns);
        DshotV.esc_pwm_ns_args.throttle_1000 = 500u;
        DshotV.esc_pwm_ns_args.mode = MODE[i].mode;
        Dshot.esc_pwm_ns(dshot_work);
        TEST_ASSERT_EQUAL_UINT32((MODE[i].lo_ns + MODE[i].hi_ns) / 2u, DshotV.ns);
        // above the domain the throttle clamps rather than running past the maximum pulse
        DshotV.esc_pwm_ns_args.throttle_1000 = 1001u;
        DshotV.esc_pwm_ns_args.mode = MODE[i].mode;
        Dshot.esc_pwm_ns(dshot_work);
        TEST_ASSERT_EQUAL_UINT32(MODE[i].hi_ns, DshotV.ns);
        DshotV.esc_pwm_ns_args.throttle_1000 = 65535u;
        DshotV.esc_pwm_ns_args.mode = MODE[i].mode;
        Dshot.esc_pwm_ns(dshot_work);
        TEST_ASSERT_EQUAL_UINT32(MODE[i].hi_ns, DshotV.ns);
    }
}

// The map is linear and never leaves the mode's range, at every throttle step.
void test_analog_pulse_width_is_monotone_and_bounded(void)
{
    static const protocore_esc_pwm MODE[] = {PROTOCORE_ESC_PWM, PROTOCORE_ESC_ONESHOT125, PROTOCORE_ESC_ONESHOT42,
                                             PROTOCORE_ESC_MULTISHOT};
    for (size_t m = 0; m < sizeof(MODE) / sizeof(MODE[0]); m++)
    {
        DshotV.esc_pwm_ns_args.throttle_1000 = 0u;
        DshotV.esc_pwm_ns_args.mode = MODE[m];
        Dshot.esc_pwm_ns(dshot_work);
        uint32_t lo = DshotV.ns;
        DshotV.esc_pwm_ns_args.throttle_1000 = 1000u;
        DshotV.esc_pwm_ns_args.mode = MODE[m];
        Dshot.esc_pwm_ns(dshot_work);
        uint32_t hi = DshotV.ns;
        uint32_t prev = lo;
        for (uint16_t t = 0u; t <= 1000u; t++)
        {
            DshotV.esc_pwm_ns_args.throttle_1000 = t;
            DshotV.esc_pwm_ns_args.mode = MODE[m];
            Dshot.esc_pwm_ns(dshot_work);
            uint32_t ns = DshotV.ns;
            TEST_ASSERT_TRUE(ns >= prev);
            TEST_ASSERT_TRUE(ns >= lo && ns <= hi);
            prev = ns;
        }
    }
}
