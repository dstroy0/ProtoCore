// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the BACnet/IP BVLC + NPDU codec (services/fieldbus/bacnet): the BVLC envelope and
// the NPDU header (control byte + optional addressing). Layout per ASHRAE 135. Pure host tests.

#include "services/fieldbus/bacnet/bacnet.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_bvlc_bytes()
{
    const uint8_t npdu[] = {0x01, 0x00, 0xAA};
    uint8_t buf[16];
    size_t n = pc_bvlc_build(buf, sizeof(buf), BVLC_FUNC_ORIGINAL_UNICAST, npdu, sizeof(npdu));
    const uint8_t expect[] = {0x81, 0x0A, 0x00, 0x07, 0x01, 0x00, 0xAA}; // type, func, len 7, npdu
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    uint8_t func;
    const uint8_t *p;
    size_t plen;
    TEST_ASSERT_TRUE(pc_bvlc_parse(buf, n, &func, &p, &plen));
    TEST_ASSERT_EQUAL_HEX8(BVLC_FUNC_ORIGINAL_UNICAST, func);
    TEST_ASSERT_EQUAL_size_t(3, plen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(npdu, p, 3);
}

// A local APDU NPDU: version + control(0) + apdu, no addressing.
void test_npdu_local()
{
    const uint8_t apdu[] = {0x41, 0x42, 0x43};
    uint8_t buf[16];
    size_t n = pc_npdu_build(buf, sizeof(buf), false, NPDU_PRIO_NORMAL, false, 0, NULL, 0, 0, apdu, sizeof(apdu));
    const uint8_t expect[] = {0x01, 0x00, 0x41, 0x42, 0x43};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    NpduInfo info;
    TEST_ASSERT_TRUE(pc_npdu_parse(buf, n, &info));
    TEST_ASSERT_FALSE(info.dest_present);
    TEST_ASSERT_FALSE(info.network_message);
    TEST_ASSERT_EQUAL_size_t(3, info.apdu_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(apdu, info.apdu, 3);
}

// A routed NPDU with a destination: control 0x20, DNET/DLEN/DADR + hop count.
void test_npdu_dest()
{
    const uint8_t dadr[] = {0x0A};
    const uint8_t apdu[] = {0x10};
    uint8_t buf[32];
    size_t n = pc_npdu_build(buf, sizeof(buf), true, NPDU_PRIO_NORMAL, true, 0x0005, dadr, sizeof(dadr), 0xFF, apdu,
                             sizeof(apdu));
    // 01, control(0x20|0x04 reply), 00 05 (dnet), 01 (dlen), 0A (dadr), FF (hop), 10 (apdu)
    const uint8_t expect[] = {0x01, 0x24, 0x00, 0x05, 0x01, 0x0A, 0xFF, 0x10};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    NpduInfo info;
    TEST_ASSERT_TRUE(pc_npdu_parse(buf, n, &info));
    TEST_ASSERT_TRUE(info.dest_present);
    TEST_ASSERT_EQUAL_HEX16(0x0005, info.dnet);
    TEST_ASSERT_EQUAL_HEX8(0xFF, info.hop_count);
    TEST_ASSERT_EQUAL_size_t(1, info.apdu_len);
    TEST_ASSERT_EQUAL_HEX8(0x10, info.apdu[0]);
}

// A global broadcast: DNET 0xFFFF, DLEN 0.
void test_npdu_broadcast()
{
    const uint8_t apdu[] = {0x10, 0x08};
    uint8_t buf[16];
    size_t n =
        pc_npdu_build(buf, sizeof(buf), false, NPDU_PRIO_NORMAL, true, 0xFFFF, NULL, 0, 0xFF, apdu, sizeof(apdu));
    const uint8_t expect[] = {0x01, 0x20, 0xFF, 0xFF, 0x00, 0xFF, 0x10, 0x08};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// The parser skips both destination and source address fields before the APDU.
void test_npdu_parse_with_source()
{
    const uint8_t frame[] = {
        0x01, 0x28,             // version, control: dest + source present
        0x00, 0x05, 0x01, 0x0A, // DNET 5, DLEN 1, DADR 0A
        0x00, 0x03, 0x01, 0x0B, // SNET 3, SLEN 1, SADR 0B
        0xFF,                   // hop count
        0x30, 0x31              // apdu
    };
    NpduInfo info;
    TEST_ASSERT_TRUE(pc_npdu_parse(frame, sizeof(frame), &info));
    TEST_ASSERT_TRUE(info.dest_present);
    TEST_ASSERT_TRUE(info.src_present);
    TEST_ASSERT_EQUAL_HEX16(0x0005, info.dnet);
    TEST_ASSERT_EQUAL_HEX16(0x0003, info.snet);
    TEST_ASSERT_EQUAL_HEX8(0xFF, info.hop_count);
    TEST_ASSERT_EQUAL_size_t(2, info.apdu_len);
    TEST_ASSERT_EQUAL_HEX8(0x30, info.apdu[0]);
}

// Full BACnet/IP stack: BVLC wrapping an NPDU wrapping an APDU.
void test_full_stack()
{
    const uint8_t apdu[] = {0x10, 0x08, 0x12, 0x34};
    uint8_t npdu[16];
    size_t nlen =
        pc_npdu_build(npdu, sizeof(npdu), false, NPDU_PRIO_NORMAL, false, 0, NULL, 0, 0, apdu, sizeof(apdu));
    uint8_t buf[32];
    size_t n = pc_bvlc_build(buf, sizeof(buf), BVLC_FUNC_ORIGINAL_BROADCAST, npdu, nlen);

    uint8_t func;
    const uint8_t *p;
    size_t plen;
    TEST_ASSERT_TRUE(pc_bvlc_parse(buf, n, &func, &p, &plen));
    TEST_ASSERT_EQUAL_HEX8(BVLC_FUNC_ORIGINAL_BROADCAST, func);
    NpduInfo info;
    TEST_ASSERT_TRUE(pc_npdu_parse(p, plen, &info));
    TEST_ASSERT_EQUAL_size_t(sizeof(apdu), info.apdu_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(apdu, info.apdu, sizeof(apdu));
}

void test_parse_rejects_bad()
{
    uint8_t func;
    const uint8_t *p;
    size_t plen;
    const uint8_t bad_type[] = {0x82, 0x0A, 0x00, 0x04}; // not BACnet/IP
    TEST_ASSERT_FALSE(pc_bvlc_parse(bad_type, sizeof(bad_type), &func, &p, &plen));
    const uint8_t short_bvlc[] = {0x81, 0x0A, 0x00, 0x08, 0x01}; // declares 8, only 5 buffered
    TEST_ASSERT_FALSE(pc_bvlc_parse(short_bvlc, sizeof(short_bvlc), &func, &p, &plen));

    NpduInfo info;
    const uint8_t bad_ver[] = {0x02, 0x00, 0x41};
    TEST_ASSERT_FALSE(pc_npdu_parse(bad_ver, sizeof(bad_ver), &info));
    const uint8_t trunc_dest[] = {0x01, 0x20, 0x00, 0x05, 0x05, 0x0A}; // DLEN 5 overruns
    TEST_ASSERT_FALSE(pc_npdu_parse(trunc_dest, sizeof(trunc_dest), &info));
}

void test_overflow_fails_closed()
{
    const uint8_t apdu[] = {1, 2, 3, 4};
    uint8_t small[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_bvlc_build(small, sizeof(small), BVLC_FUNC_ORIGINAL_UNICAST, apdu, sizeof(apdu)));
    uint8_t nsmall[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_npdu_build(nsmall, sizeof(nsmall), false, 0, false, 0, NULL, 0, 0, apdu, 4));
}

// BVLC / NPDU builders fail closed on null args, and NPDU parsing rejects a header
// that claims a destination/source but is truncated.
void test_bacnet_guards_and_truncations()
{
    uint8_t buf[64], npdu[4] = {NPDU_VERSION, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT(0, pc_bvlc_build(NULL, sizeof(buf), 0x0A, npdu, 4)); // null buffer
    TEST_ASSERT_EQUAL_UINT(0, pc_bvlc_build(buf, sizeof(buf), 0x0A, NULL, 5));  // pc_npdu_len w/o npdu
    TEST_ASSERT_EQUAL_UINT(0,
                           pc_npdu_build(NULL, sizeof(buf), false, 0, false, 0, NULL, 0, 0, npdu, 4)); // null buf
    TEST_ASSERT_EQUAL_UINT(
        0, pc_npdu_build(buf, sizeof(buf), false, 0, false, 0, NULL, 0, 0, NULL, 5)); // apdu w/o ptr

    NpduInfo info;
    uint8_t dest_trunc[2] = {NPDU_VERSION, NPCI_DEST_PRESENT};
    TEST_ASSERT_FALSE(pc_npdu_parse(dest_trunc, 2, &info)); // destination announced, none present
    uint8_t src_trunc[2] = {NPDU_VERSION, NPCI_SRC_PRESENT};
    TEST_ASSERT_FALSE(pc_npdu_parse(src_trunc, 2, &info));                             // source announced, none present
    uint8_t src_overrun[6] = {NPDU_VERSION, NPCI_SRC_PRESENT, 0x00, 0x01, 0xFF, 0x00}; // SLEN overruns the buffer
    TEST_ASSERT_FALSE(pc_npdu_parse(src_overrun, 6, &info));
    uint8_t no_hop[5] = {NPDU_VERSION, NPCI_DEST_PRESENT, 0x00, 0x01, 0x00}; // valid dest, then no hop-count byte
    TEST_ASSERT_FALSE(pc_npdu_parse(no_hop, 5, &info));
}

// pc_bvlc_build: a zero-length NPDU payload (e.g. a header-only BVLC-Result) exercises the
// "no npdu" side of the length guard and skips the memcpy; a declared length that itself
// exceeds the 16-bit BVLL length field trips the overflow guard before the cap is even checked.
void test_bvlc_build_zero_len_and_giant_overflow()
{
    uint8_t buf[16];
    size_t n = pc_bvlc_build(buf, sizeof(buf), BVLC_FUNC_RESULT, NULL, 0);
    const uint8_t expect[] = {0x81, 0x00, 0x00, 0x04}; // type, func, len 4 (header only)
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    uint8_t dummy_npdu[1] = {0};
    TEST_ASSERT_EQUAL_size_t(
        0, pc_bvlc_build(buf, sizeof(buf), BVLC_FUNC_ORIGINAL_UNICAST, dummy_npdu, 0x10000)); // total > 0xFFFF
}

// pc_bvlc_parse: a null buffer, a buffer shorter than the fixed header, a declared BVLL
// length that is itself smaller than the header, and all-null optional outputs.
void test_bvlc_parse_edge_branches()
{
    uint8_t func;
    const uint8_t *p;
    size_t plen;
    TEST_ASSERT_FALSE(pc_bvlc_parse(NULL, 8, &func, &p, &plen)); // null buffer

    const uint8_t too_short[2] = {0x81, 0x0A};
    TEST_ASSERT_FALSE(pc_bvlc_parse(too_short, sizeof(too_short), &func, &p, &plen)); // len < header

    const uint8_t small_total[4] = {0x81, 0x0A, 0x00, 0x03}; // declares total 3, less than the header
    TEST_ASSERT_FALSE(pc_bvlc_parse(small_total, sizeof(small_total), &func, &p, &plen));

    const uint8_t header_only[4] = {0x81, 0x0A, 0x00, 0x04}; // valid, no npdu payload
    TEST_ASSERT_TRUE(
        pc_bvlc_parse(header_only, sizeof(header_only), NULL, NULL, NULL)); // all outputs optional
}

// pc_npdu_build: a zero-length APDU (header-only NPDU) exercises the "no apdu" side of the
// length guard and skips the trailing memcpy; a destination announced (dadr_len != 0) with a
// null DADR pointer trips the guard and fails closed.
void test_npdu_build_zero_apdu_and_null_dadr()
{
    uint8_t buf[16];
    size_t n = pc_npdu_build(buf, sizeof(buf), false, NPDU_PRIO_NORMAL, false, 0, NULL, 0, 0, NULL, 0);
    const uint8_t expect[] = {0x01, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    TEST_ASSERT_EQUAL_size_t(
        0, pc_npdu_build(buf, sizeof(buf), false, NPDU_PRIO_NORMAL, true, 0x0005, NULL, 3, 0xFF, NULL, 0));
}

// pc_npdu_parse: a null buffer, a null output pointer, and a length too short to hold the
// fixed version+control octets.
void test_npdu_parse_null_buf_out_and_short()
{
    const uint8_t frame[2] = {NPDU_VERSION, 0x00};
    NpduInfo info;
    TEST_ASSERT_FALSE(pc_npdu_parse(NULL, sizeof(frame), &info)); // null buffer
    TEST_ASSERT_FALSE(pc_npdu_parse(frame, sizeof(frame), NULL)); // null output
    TEST_ASSERT_FALSE(pc_npdu_parse(frame, 1, &info));               // len < 2
}

void test_apdu_parse()
{
    BacnetApdu a;
    // Confirmed-Request ReadProperty (service 12): type/flags, max octet, invoke id 1, service choice, data.
    const uint8_t creq[] = {0x00, 0x05, 0x01, 0x0C, 0xDE, 0xAD};
    TEST_ASSERT_TRUE(pc_apdu_parse(creq, sizeof(creq), &a));
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_CONFIRMED_REQUEST, a.pdu_type);
    TEST_ASSERT_FALSE(a.segmented);
    TEST_ASSERT_EQUAL_UINT8(1, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(12, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(2, a.service_data_len);
    TEST_ASSERT_EQUAL_HEX8(0xDE, a.service_data[0]);

    // A segmented Confirmed-Request (SEG|SA) carries a sequence number + window before the service choice.
    const uint8_t seg[] = {0x0A, 0x05, 0x02, 0x00, 0x04, 0x0C, 0x11};
    TEST_ASSERT_TRUE(pc_apdu_parse(seg, sizeof(seg), &a));
    TEST_ASSERT_TRUE(a.segmented);
    TEST_ASSERT_TRUE(a.sa);
    TEST_ASSERT_EQUAL_UINT8(2, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(12, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(1, a.service_data_len);

    // Unconfirmed-Request I-Am (service 0), no invoke id.
    const uint8_t ureq[] = {0x10, 0x00, 0xC4};
    TEST_ASSERT_TRUE(pc_apdu_parse(ureq, sizeof(ureq), &a));
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_UNCONFIRMED_REQUEST, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(0, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(1, a.service_data_len);

    // Simple-ACK for WriteProperty (service 15): invoke id + service choice, no data.
    const uint8_t sack[] = {0x20, 0x01, 0x0F};
    TEST_ASSERT_TRUE(pc_apdu_parse(sack, sizeof(sack), &a));
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_SIMPLE_ACK, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(1, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(15, a.service_choice);
    TEST_ASSERT_NULL(a.service_data);

    // Complex-ACK ReadProperty (service 12): invoke id + service choice + data.
    const uint8_t cack[] = {0x30, 0x01, 0x0C, 0xBE};
    TEST_ASSERT_TRUE(pc_apdu_parse(cack, sizeof(cack), &a));
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_COMPLEX_ACK, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(1, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(12, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(1, a.service_data_len);

    // Guards: an unsupported type (Error = 5), truncated headers, and nulls all fail closed.
    const uint8_t err[] = {0x50, 0x01, 0x00};
    TEST_ASSERT_FALSE(pc_apdu_parse(err, sizeof(err), &a));
    TEST_ASSERT_FALSE(pc_apdu_parse(creq, 3, &a)); // confirmed request needs the service choice
    TEST_ASSERT_FALSE(pc_apdu_parse(seg, 5, &a));  // segmented: needs seq/window + service choice
    TEST_ASSERT_FALSE(pc_apdu_parse(sack, 2, &a)); // simple-ack needs the service choice
    TEST_ASSERT_FALSE(pc_apdu_parse(NULL, 4, &a));
    TEST_ASSERT_FALSE(pc_apdu_parse(creq, sizeof(creq), NULL));
    TEST_ASSERT_FALSE(pc_apdu_parse(creq, 0, &a));
}

void test_apdu_build_who_is()
{
    uint8_t buf[16];
    BacnetApdu a;

    // Unbounded Who-Is (no limits): the 2-octet form every device answers.
    size_t n = pc_apdu_build_who_is(buf, sizeof(buf), 0, 0, false);
    const uint8_t unbounded[] = {0x10, 0x08};
    TEST_ASSERT_EQUAL_size_t(sizeof(unbounded), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(unbounded, buf, n);
    // It round-trips through the parser as an unconfirmed Who-Is.
    TEST_ASSERT_TRUE(pc_apdu_parse(buf, n, &a));
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_UNCONFIRMED_REQUEST, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(BACNET_SVC_UN_WHO_IS, a.service_choice);

    // Bounded 100..200: 1-octet limits behind context tags 0 and 1.
    n = pc_apdu_build_who_is(buf, sizeof(buf), 100, 200, true);
    const uint8_t small_range[] = {0x10, 0x08, 0x09, 0x64, 0x19, 0xC8};
    TEST_ASSERT_EQUAL_size_t(sizeof(small_range), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(small_range, buf, n);

    // Bounded 300..4000000: a 2-octet low limit and a 3-octet high limit (minimal length each).
    n = pc_apdu_build_who_is(buf, sizeof(buf), 300, 4000000, true);
    const uint8_t big_range[] = {0x10, 0x08, 0x0A, 0x01, 0x2C, 0x1B, 0x3D, 0x09, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(big_range), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(big_range, buf, n);

    // A zero limit encodes as a single 0x00 value octet.
    n = pc_apdu_build_who_is(buf, sizeof(buf), 0, 0, true);
    const uint8_t zero_range[] = {0x10, 0x08, 0x09, 0x00, 0x19, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(zero_range), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero_range, buf, n);

    // Guards: low > high, a limit beyond the 22-bit instance range, a null buffer, and a too-small buffer.
    TEST_ASSERT_EQUAL_size_t(0, pc_apdu_build_who_is(buf, sizeof(buf), 200, 100, true));    // low > high
    TEST_ASSERT_EQUAL_size_t(0, pc_apdu_build_who_is(buf, sizeof(buf), 0, 0x400000, true)); // > BACNET_MAX_INSTANCE
    TEST_ASSERT_EQUAL_size_t(0, pc_apdu_build_who_is(NULL, sizeof(buf), 0, 0, false));
    TEST_ASSERT_EQUAL_size_t(0, pc_apdu_build_who_is(buf, 1, 0, 0, false)); // needs 2
}

void test_apdu_build_i_am()
{
    uint8_t buf[32];
    BacnetApdu a;

    // I-Am for Device 260, max APDU 1476, no-segmentation (3), vendor 42.
    size_t n = pc_apdu_build_i_am(buf, sizeof(buf), 260, 1476, 3, 42);
    const uint8_t expect[] = {
        0x10, 0x00,                   // unconfirmed request, service choice 0 (I-Am)
        0xC4, 0x02, 0x00, 0x01, 0x04, // device object id: app tag 12, (8<<22)|260 = 0x02000104
        0x22, 0x05, 0xC4,             // max APDU 1476: app tag 2, 2 octets
        0x91, 0x03,                   // segmentation supported: app tag 9 (enumerated), value 3
        0x21, 0x2A                    // vendor id 42: app tag 2, 1 octet
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
    // It round-trips through the parser as an unconfirmed I-Am.
    TEST_ASSERT_TRUE(pc_apdu_parse(buf, n, &a));
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_UNCONFIRMED_REQUEST, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(BACNET_SVC_UN_I_AM, a.service_choice);

    // Guards: an out-of-range instance, a bad segmentation enum, a null buffer, and a too-small buffer.
    TEST_ASSERT_EQUAL_size_t(0, pc_apdu_build_i_am(buf, sizeof(buf), 0x400000, 480, 3, 1)); // > BACNET_MAX_INSTANCE
    TEST_ASSERT_EQUAL_size_t(0, pc_apdu_build_i_am(buf, sizeof(buf), 1, 480, 4, 1));        // segmentation > 3
    TEST_ASSERT_EQUAL_size_t(0, pc_apdu_build_i_am(NULL, sizeof(buf), 1, 480, 3, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_apdu_build_i_am(buf, 8, 260, 1476, 3, 42)); // does not fit
}

void test_apdu_build_read_property()
{
    uint8_t buf[32];
    BacnetApdu a;

    // ReadProperty: invoke 1, max-resp 0x05, Analog Input 5, present-value (85).
    size_t n =
        pc_apdu_build_read_property(buf, sizeof(buf), 1, 0x05, BACNET_OBJ_ANALOG_INPUT, 5, BACNET_PROP_PRESENT_VALUE);
    const uint8_t expect[] = {
        0x00, 0x05, 0x01, 0x0C,       // confirmed request, max-resp 0x05, invoke 1, service choice 12
        0x0C, 0x00, 0x00, 0x00, 0x05, // object id: context tag 0, (0<<22)|5
        0x19, 0x55                    // property id: context tag 1, present-value (85 = 0x55)
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
    // It round-trips through the parser as a confirmed ReadProperty request.
    TEST_ASSERT_TRUE(pc_apdu_parse(buf, n, &a));
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_CONFIRMED_REQUEST, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(1, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(BACNET_SVC_CONF_READ_PROPERTY, a.service_choice);

    // A Device object (type 8, instance 260 = oid 0x02000104) reading object-name (77 = 0x4D).
    n = pc_apdu_build_read_property(buf, sizeof(buf), 2, 0x05, BACNET_OBJ_DEVICE, 260, BACNET_PROP_OBJECT_NAME);
    const uint8_t expect2[] = {0x00, 0x05, 0x02, 0x0C, 0x0C, 0x02, 0x00, 0x01, 0x04, 0x19, 0x4D};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect2), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect2, buf, n);

    // Guards: an out-of-range instance, an out-of-range object type, a null buffer, and a too-small buffer.
    TEST_ASSERT_EQUAL_size_t(0, pc_apdu_build_read_property(buf, sizeof(buf), 1, 5, 0, 0x400000, 85)); // instance
    TEST_ASSERT_EQUAL_size_t(0, pc_apdu_build_read_property(buf, sizeof(buf), 1, 5, 0x400, 1, 85));    // obj type
    TEST_ASSERT_EQUAL_size_t(0, pc_apdu_build_read_property(NULL, sizeof(buf), 1, 5, 0, 1, 85));
    TEST_ASSERT_EQUAL_size_t(0, pc_apdu_build_read_property(buf, 8, 0, 5, 0, 5, 85)); // does not fit
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_bacnet_guards_and_truncations);
    RUN_TEST(test_bvlc_bytes);
    RUN_TEST(test_npdu_local);
    RUN_TEST(test_npdu_dest);
    RUN_TEST(test_npdu_broadcast);
    RUN_TEST(test_npdu_parse_with_source);
    RUN_TEST(test_full_stack);
    RUN_TEST(test_parse_rejects_bad);
    RUN_TEST(test_overflow_fails_closed);
    RUN_TEST(test_bvlc_build_zero_len_and_giant_overflow);
    RUN_TEST(test_bvlc_parse_edge_branches);
    RUN_TEST(test_npdu_build_zero_apdu_and_null_dadr);
    RUN_TEST(test_npdu_parse_null_buf_out_and_short);
    RUN_TEST(test_apdu_parse);
    RUN_TEST(test_apdu_build_who_is);
    RUN_TEST(test_apdu_build_i_am);
    RUN_TEST(test_apdu_build_read_property);
    return UNITY_END();
}
