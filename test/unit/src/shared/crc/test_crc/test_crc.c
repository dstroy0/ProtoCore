// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the shared parameterized CRC engine (shared/crc/crc.h).
//
// The load-bearing case is test_catalogue_check_values. Every CRC in the Rocksoft/Williams catalogue
// publishes a check value - the CRC of the nine ASCII octets "123456789" - and each of the thirteen
// presets is asserted against its own. That is what makes the engine trustworthy with no reference
// implementation to diff against: a wrong polynomial, a swapped init, or a flipped reflect flag
// cannot reproduce a published check value by accident.
//
// The rest cover the three-step split against the one-shot, the degenerate inputs, and the
// properties a CRC must have to be worth computing at all - a single flipped bit must change it, and
// reordering the octets must change it.

#include "shared/crc/crc.h"
#include <string.h>

#include <unity.h>

static uint8_t crc_work[16]; // the borrow an entry takes; Crc never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t CHECK_INPUT[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

static uint32_t compute(const protocore_crc_params *p, const uint8_t *d, size_t n)
{
    CrcV.args.params = p;
    CrcV.args.data = d;
    CrcV.args.len = n;
    Crc.compute(crc_work);
    return CrcV.value;
}

static uint32_t begin(const protocore_crc_params *p)
{
    CrcV.args.params = p;
    Crc.begin(crc_work);
    return CrcV.value;
}

static uint32_t update(const protocore_crc_params *p, uint32_t crc, const uint8_t *d, size_t n)
{
    CrcV.args.params = p;
    CrcV.args.crc = crc;
    CrcV.args.data = d;
    CrcV.args.len = n;
    Crc.update(crc_work);
    return CrcV.value;
}

static uint32_t finish(const protocore_crc_params *p, uint32_t crc)
{
    CrcV.args.params = p;
    CrcV.args.crc = crc;
    Crc.final(crc_work);
    return CrcV.value;
}

static uint32_t check_of(const protocore_crc_params *p)
{
    return compute(p, CHECK_INPUT, sizeof(CHECK_INPUT));
}

// The catalogue check value for each preset. A parameter typo cannot survive this.
void test_catalogue_check_values(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xF4u, check_of(&PROTOCORE_CRC8_SMBUS));
    TEST_ASSERT_EQUAL_HEX32(0xA1u, check_of(&PROTOCORE_CRC8_MAXIM_DOW));
    TEST_ASSERT_EQUAL_HEX32(0xF7u, check_of(&PROTOCORE_CRC8_NRSC5));
    TEST_ASSERT_EQUAL_HEX32(0xBB3Du, check_of(&PROTOCORE_CRC16_ARC));
    TEST_ASSERT_EQUAL_HEX32(0x4B37u, check_of(&PROTOCORE_CRC16_MODBUS));
    TEST_ASSERT_EQUAL_HEX32(0x29B1u, check_of(&PROTOCORE_CRC16_IBM_3740));
    TEST_ASSERT_EQUAL_HEX32(0x31C3u, check_of(&PROTOCORE_CRC16_XMODEM));
    TEST_ASSERT_EQUAL_HEX32(0x2189u, check_of(&PROTOCORE_CRC16_KERMIT));
    TEST_ASSERT_EQUAL_HEX32(0x906Eu, check_of(&PROTOCORE_CRC16_X25));
    TEST_ASSERT_EQUAL_HEX32(0xEA82u, check_of(&PROTOCORE_CRC16_DNP));
    TEST_ASSERT_EQUAL_HEX32(0x21CF02u, check_of(&PROTOCORE_CRC24_OPENPGP));
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, check_of(&PROTOCORE_CRC32_ISO_HDLC));
    TEST_ASSERT_EQUAL_HEX32(0xFC891918u, check_of(&PROTOCORE_CRC32_BZIP2));
}

// Reflected and unreflected variants of one polynomial must NOT agree - proof the reflect flags are
// actually wired through rather than ignored.
void test_reflection_flags_actually_apply(void)
{
    TEST_ASSERT_NOT_EQUAL(check_of(&PROTOCORE_CRC16_XMODEM), check_of(&PROTOCORE_CRC16_KERMIT));
    TEST_ASSERT_NOT_EQUAL(check_of(&PROTOCORE_CRC32_ISO_HDLC), check_of(&PROTOCORE_CRC32_BZIP2));
    // ...and so must differing init values on otherwise identical parameters
    TEST_ASSERT_NOT_EQUAL(check_of(&PROTOCORE_CRC16_ARC), check_of(&PROTOCORE_CRC16_MODBUS));
}

// begin/update/final in pieces must equal compute, or a caller cannot checksum a header and payload
// that are not contiguous in memory.
void test_streaming_matches_the_one_shot(void)
{
    const protocore_crc_params *p = &PROTOCORE_CRC32_ISO_HDLC;
    const uint32_t want = check_of(p);

    for (size_t split = 0; split <= sizeof(CHECK_INPUT); split++)
    {
        uint32_t c = begin(p);
        c = update(p, c, CHECK_INPUT, split);
        c = update(p, c, CHECK_INPUT + split, sizeof(CHECK_INPUT) - split);
        TEST_ASSERT_EQUAL_HEX32(want, finish(p, c));
    }

    // octet-at-a-time is the same thing taken to the limit
    uint32_t c = begin(p);
    for (size_t i = 0; i < sizeof(CHECK_INPUT); i++)
    {
        c = update(p, c, CHECK_INPUT + i, 1);
    }
    TEST_ASSERT_EQUAL_HEX32(want, finish(p, c));
}

// An intermediate register is not a finished CRC: the output reflection and the final XOR belong to
// final, so folding every octet and reading the register without it must differ.
void test_the_intermediate_register_is_not_the_crc(void)
{
    const protocore_crc_params *p = &PROTOCORE_CRC32_ISO_HDLC;
    uint32_t raw = update(p, begin(p), CHECK_INPUT, sizeof(CHECK_INPUT));
    TEST_ASSERT_NOT_EQUAL(raw, finish(p, raw));
    TEST_ASSERT_EQUAL_HEX32(check_of(p), finish(p, raw));
}

// The property that makes a CRC worth computing.
void test_single_bit_flip_changes_the_crc(void)
{
    const protocore_crc_params *p = &PROTOCORE_CRC16_MODBUS;
    uint8_t buf[9];
    memcpy(buf, CHECK_INPUT, sizeof(buf));
    const uint32_t base = compute(p, buf, sizeof(buf));

    for (size_t byte = 0; byte < sizeof(buf); byte++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            buf[byte] ^= (uint8_t)(1u << bit);
            TEST_ASSERT_NOT_EQUAL(base, compute(p, buf, sizeof(buf)));
            buf[byte] ^= (uint8_t)(1u << bit); // restore
        }
    }
}

// Byte order matters: a CRC that ignored ordering would miss reordered payloads.
void test_order_sensitivity(void)
{
    const protocore_crc_params *p = &PROTOCORE_CRC32_ISO_HDLC;
    const uint8_t a[3] = {0x01, 0x02, 0x03};
    const uint8_t b[3] = {0x03, 0x02, 0x01};
    TEST_ASSERT_NOT_EQUAL(compute(p, a, 3), compute(p, b, 3));
}

// Leading zero octets must change the result, or length-extension mistakes go undetected.
void test_leading_zeros_are_significant(void)
{
    const protocore_crc_params *p = &PROTOCORE_CRC16_MODBUS; // non-zero init, so zeros do fold in
    const uint8_t one[1] = {0x00};
    const uint8_t two[2] = {0x00, 0x00};
    TEST_ASSERT_NOT_EQUAL(compute(p, one, 1), compute(p, two, 2));
}

void test_empty_input_is_the_bare_init(void)
{
    // With no octets folded in, the result is init through the output stage - not an error.
    const protocore_crc_params *p = &PROTOCORE_CRC32_ISO_HDLC;
    TEST_ASSERT_EQUAL_HEX32(finish(p, begin(p)), compute(p, CHECK_INPUT, 0));
    // init 0xFFFFFFFF reflected is 0xFFFFFFFF, XORed with 0xFFFFFFFF is 0
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, compute(p, NULL, 0));
}

void test_width_is_respected(void)
{
    // Every result must fit its declared width - a leaked high bit would corrupt a packed frame.
    TEST_ASSERT_EQUAL_HEX32(0u, check_of(&PROTOCORE_CRC8_SMBUS) & ~0xFFu);
    TEST_ASSERT_EQUAL_HEX32(0u, check_of(&PROTOCORE_CRC16_ARC) & ~0xFFFFu);
    TEST_ASSERT_EQUAL_HEX32(0u, check_of(&PROTOCORE_CRC24_OPENPGP) & ~0xFFFFFFu);
}

// A width outside the supported 8..32 range is clamped: below 8 behaves as 8, above 32 as 32.
void test_out_of_range_width_is_clamped(void)
{
    const protocore_crc_params lo4 = {4, 0x07u, 0x00u, PROTO_FALSE, PROTO_FALSE, 0x00u};
    const protocore_crc_params lo8 = {8, 0x07u, 0x00u, PROTO_FALSE, PROTO_FALSE, 0x00u};
    TEST_ASSERT_EQUAL_HEX32(check_of(&lo8), check_of(&lo4));

    const protocore_crc_params hi40 = {40, 0x04C11DB7u, 0xFFFFFFFFu, PROTO_TRUE, PROTO_TRUE, 0xFFFFFFFFu};
    const protocore_crc_params hi32 = {32, 0x04C11DB7u, 0xFFFFFFFFu, PROTO_TRUE, PROTO_TRUE, 0xFFFFFFFFu};
    TEST_ASSERT_EQUAL_HEX32(check_of(&hi32), check_of(&hi40));
}

// --- Equivalence with the in-tree hand-rolled CRCs ----------------------------------------------
// Sixteen services in this tree each hand-rolled a CRC loop before this engine existed. Those loops
// ARE the reference implementations, and they are interop-proven against real peers - so the engine
// replacing them has to agree with them octet for octet, not merely reproduce a catalogue check
// value. Each loop below is transcribed verbatim from the service that owned it (this env compiles
// no service sources, so they cannot simply be linked).
//
// Do NOT "simplify" them to call the engine, or this test starts comparing the engine against
// itself and proves nothing.

// services/radio/thread (spinel FCS), services/fieldbus/mbplus, services/transportation/nema_ts2
static uint16_t ref_x25(const uint8_t *d, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
        }
    }
    return (uint16_t)(crc ^ 0xFFFF);
}

// services/energy/c37118, services/fieldbus/interbus, services/radio/zigbee (CCITT-FALSE)
static uint16_t ref_ccitt_false(const uint8_t *d, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= (uint16_t)((uint16_t)d[i] << 8);
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// services/fieldbus/df1, server/peripherals/sdi12 (reflected 0xA001, init 0)
static uint16_t ref_arc(const uint8_t *d, size_t n)
{
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

// services/fieldbus/modbus
static uint16_t ref_modbus(const uint8_t *d, size_t n)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

// services/energy/dnp3 (reflected 0xA6BC = reflect(0x3D65), xorout 0xFFFF)
static uint16_t ref_dnp(const uint8_t *d, size_t n)
{
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA6BC) : (uint16_t)(crc >> 1);
        }
    }
    return (uint16_t)(crc ^ 0xFFFF);
}

// services/fieldbus/rawl2 (Ethernet FCS), services/storage/wal (same params, table-driven)
static uint32_t ref_crc32(const uint8_t *d, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

// services/radio/enocean (ESP3 CRC-8, poly 0x07 MSB-first, init 0)
static uint8_t ref_crc8_smbus(const uint8_t *d, size_t n)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// server/peripherals/sht3x (Sensirion CRC-8, poly 0x31 MSB-first, init 0xFF)
static uint8_t ref_sensirion(const uint8_t *d, size_t n)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void test_engine_matches_the_hand_rolled_implementations(void)
{
    // A spread of lengths, including the empty and single-octet degenerate cases, over a buffer with
    // both high-bit-set and zero octets so a sign or shift mistake cannot hide.
    uint8_t buf[64];
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)(i * 37u + (i << 3));
    }
    buf[5] = 0x00;
    buf[6] = 0xFF;
    buf[7] = 0x80;

    for (size_t n = 0; n <= sizeof(buf); n++)
    {
        TEST_ASSERT_EQUAL_HEX16(ref_x25(buf, n), compute(&PROTOCORE_CRC16_X25, buf, n));
        TEST_ASSERT_EQUAL_HEX16(ref_ccitt_false(buf, n), compute(&PROTOCORE_CRC16_IBM_3740, buf, n));
        TEST_ASSERT_EQUAL_HEX16(ref_arc(buf, n), compute(&PROTOCORE_CRC16_ARC, buf, n));
        TEST_ASSERT_EQUAL_HEX16(ref_modbus(buf, n), compute(&PROTOCORE_CRC16_MODBUS, buf, n));
        TEST_ASSERT_EQUAL_HEX16(ref_dnp(buf, n), compute(&PROTOCORE_CRC16_DNP, buf, n));
        TEST_ASSERT_EQUAL_HEX32(ref_crc32(buf, n), compute(&PROTOCORE_CRC32_ISO_HDLC, buf, n));
        TEST_ASSERT_EQUAL_HEX8(ref_crc8_smbus(buf, n), compute(&PROTOCORE_CRC8_SMBUS, buf, n));
        TEST_ASSERT_EQUAL_HEX8(ref_sensirion(buf, n), compute(&PROTOCORE_CRC8_NRSC5, buf, n));
    }
}

// A missing definition or a null buffer with a nonzero length is reported, never read through.
void test_null_guards(void)
{
    TEST_ASSERT_EQUAL_HEX32(0u, begin(NULL));
    TEST_ASSERT_EQUAL_HEX32(0u, finish(NULL, 0x1234u));
    TEST_ASSERT_EQUAL_HEX32(0u, compute(NULL, CHECK_INPUT, sizeof(CHECK_INPUT)));

    const protocore_crc_params *p = &PROTOCORE_CRC32_ISO_HDLC;
    const uint32_t start = begin(p);
    TEST_ASSERT_EQUAL_HEX32(start, update(p, start, NULL, 4));
    TEST_ASSERT_EQUAL_HEX32(start, update(NULL, start, CHECK_INPUT, 4));
}
