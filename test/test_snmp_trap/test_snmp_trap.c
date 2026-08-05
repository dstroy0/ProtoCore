// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host unit tests for the outbound SNMP notification builder (env:native_snmp_trap).

#include "services/net/snmp/snmp_ber.h"
#include "services/net/snmp/snmp_notify.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static const uint32_t SYSUPTIME0[] = {1, 3, 6, 1, 2, 1, 1, 3, 0};
static const uint32_t SNMPTRAPOID0[] = {1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0};
static const uint32_t TRAP_OID[] = {1, 3, 6, 1, 4, 1, 9999, 2, 0, 1};

static void assert_oid(BerDec *d, const uint32_t *expect, size_t n)
{
    uint32_t oid[SNMP_MAX_OID_LEN];
    size_t on = 0;
    TEST_ASSERT_TRUE(pc_ber_read_oid(d, oid, SNMP_MAX_OID_LEN, &on));
    TEST_ASSERT_EQUAL_size_t(n, on);
    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(expect[i], oid[i]);
    }
}

// Build a v2c trap with one extra Gauge32 binding and decode the whole structure.
void test_trap_v2c_structure()
{
    SnmpVarbind vb;
    memset(&vb, 0, sizeof(vb));
    static const uint32_t vboid[] = {1, 3, 6, 1, 4, 1, 9999, 3, 0};
    vb.oid = vboid;
    vb.oid_len = sizeof(vboid) / sizeof(uint32_t);
    vb.type = (uint8_t)SNMP_VB_GAUGE32;
    vb.ival = 42;

    uint8_t buf[256];
    size_t len = pc_snmp_notify_build_v2c(buf, sizeof(buf), "public", (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 7, TRAP_OID,
                                          sizeof(TRAP_OID) / sizeof(uint32_t), 12345, &vb, 1);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_HEX8(0x30, buf[0]);

    BerDec d;
    pc_ber_dec_init(&d, buf, len);
    uint8_t tag;
    size_t l;
    long v;
    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &l)); // message SEQUENCE
    TEST_ASSERT_EQUAL_HEX8(0x30, tag);
    TEST_ASSERT_TRUE(pc_ber_read_integer(&d, &v)); // version
    TEST_ASSERT_EQUAL(1, v);
    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &l)); // community OCTET STRING
    TEST_ASSERT_EQUAL_HEX8(0x04, tag);
    TEST_ASSERT_EQUAL_MEMORY("public", buf + d.pos, 6);
    pc_ber_skip(&d, l);
    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &l)); // PDU
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, tag);
    TEST_ASSERT_TRUE(pc_ber_read_integer(&d, &v)); // request-id
    TEST_ASSERT_EQUAL(7, v);
    TEST_ASSERT_TRUE(pc_ber_read_integer(&d, &v)); // error-status
    TEST_ASSERT_EQUAL(0, v);
    TEST_ASSERT_TRUE(pc_ber_read_integer(&d, &v)); // error-index
    TEST_ASSERT_EQUAL(0, v);
    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &l)); // varbinds SEQUENCE
    TEST_ASSERT_EQUAL_HEX8(0x30, tag);

    // vb1: sysUpTime.0 = TimeTicks
    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &l));
    TEST_ASSERT_EQUAL_HEX8(0x30, tag);
    assert_oid(&d, SYSUPTIME0, sizeof(SYSUPTIME0) / sizeof(uint32_t));
    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &l));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_TIMETICKS, tag);
    pc_ber_skip(&d, l);

    // vb2: snmpTrapOID.0 = OID(trap_oid)
    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &l));
    TEST_ASSERT_EQUAL_HEX8(0x30, tag);
    assert_oid(&d, SNMPTRAPOID0, sizeof(SNMPTRAPOID0) / sizeof(uint32_t));
    assert_oid(&d, TRAP_OID, sizeof(TRAP_OID) / sizeof(uint32_t));

    // vb3: the extra Gauge32 binding
    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &l));
    TEST_ASSERT_EQUAL_HEX8(0x30, tag);
    assert_oid(&d, vboid, sizeof(vboid) / sizeof(uint32_t));
    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &l));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_GAUGE32, tag);
}

void test_inform_tag()
{
    uint8_t buf[128];
    size_t len = pc_snmp_notify_build_v2c(buf, sizeof(buf), "public", 0xA6 /* InformRequest */, 1, TRAP_OID,
                                          sizeof(TRAP_OID) / sizeof(uint32_t), 1, NULL, 0);
    TEST_ASSERT_GREATER_THAN(0, len);
    BerDec d;
    pc_ber_dec_init(&d, buf, len);
    uint8_t tag;
    size_t l;
    long v;
    pc_ber_read_header(&d, &tag, &l);
    pc_ber_read_integer(&d, &v);
    pc_ber_read_header(&d, &tag, &l);
    pc_ber_skip(&d, l); // community
    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &l));
    TEST_ASSERT_EQUAL_HEX8(0xA6, tag); // InformRequest PDU
}

void test_buffer_too_small()
{
    uint8_t buf[8];
    size_t len = pc_snmp_notify_build_v2c(buf, sizeof(buf), "public", (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1, TRAP_OID,
                                          sizeof(TRAP_OID) / sizeof(uint32_t), 1, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(0, len);
}

// Every varbind value type encodes with its correct BER/application tag.
void test_all_varbind_types()
{
    static const uint32_t voi[] = {1, 3, 6, 1, 4, 1, 1, 1};
    static const uint32_t oidval[] = {1, 3, 6, 1, 2, 1};
    const uint8_t ip[4] = {10, 0, 0, 1};
    SnmpVarbind vbs[6];
    memset(vbs, 0, sizeof(vbs));
    for (int i = 0; i < 6; i++)
    {
        vbs[i].oid = voi;
        vbs[i].oid_len = sizeof(voi) / sizeof(uint32_t);
    }
    vbs[0].type = (uint8_t)SNMP_VB_INT;
    vbs[0].ival = -5;
    vbs[1].type = (uint8_t)SNMP_VB_STRING;
    vbs[1].bytes = (const uint8_t *)"hi";
    vbs[1].blen = 2;
    vbs[2].type = (uint8_t)SNMP_VB_OID;
    vbs[2].oid_val = oidval;
    vbs[2].oid_val_len = sizeof(oidval) / sizeof(uint32_t);
    vbs[3].type = (uint8_t)SNMP_VB_COUNTER32;
    vbs[3].ival = 100;
    vbs[4].type = (uint8_t)SNMP_VB_TIMETICKS;
    vbs[4].ival = 200;
    vbs[5].type = (uint8_t)SNMP_VB_IPADDR;
    vbs[5].bytes = ip;
    vbs[5].blen = 4;

    uint8_t buf[512];
    size_t len = pc_snmp_notify_build_v2c(buf, sizeof(buf), "public", (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1, TRAP_OID,
                                          sizeof(TRAP_OID) / sizeof(uint32_t), 1, vbs, 6);
    TEST_ASSERT_GREATER_THAN(0, len);

    BerDec d;
    pc_ber_dec_init(&d, buf, len);
    uint8_t tag;
    size_t l;
    long v;
    pc_ber_read_header(&d, &tag, &l); // message SEQUENCE
    pc_ber_read_integer(&d, &v);      // version
    pc_ber_read_header(&d, &tag, &l);
    pc_ber_skip(&d, l);               // community
    pc_ber_read_header(&d, &tag, &l); // PDU
    pc_ber_read_integer(&d, &v);      // request-id
    pc_ber_read_integer(&d, &v);      // error-status
    pc_ber_read_integer(&d, &v);      // error-index
    pc_ber_read_header(&d, &tag, &l); // varbinds SEQUENCE
    for (int i = 0; i < 2; i++)       // skip the 2 mandatory varbinds
    {
        pc_ber_read_header(&d, &tag, &l);
        pc_ber_skip(&d, l);
    }
    const uint8_t expect[6] = {0x02,
                               0x04,
                               0x06,
                               (uint8_t)SNMP_TAG_SNMP_COUNTER32,
                               (uint8_t)SNMP_TAG_SNMP_TIMETICKS,
                               (uint8_t)SNMP_TAG_SNMP_IPADDRESS};
    for (int i = 0; i < 6; i++)
    {
        TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &l)); // varbind SEQUENCE
        TEST_ASSERT_EQUAL_HEX8(0x30, tag);
        uint32_t o[SNMP_MAX_OID_LEN];
        size_t on = 0;
        pc_ber_read_oid(&d, o, SNMP_MAX_OID_LEN, &on);      // the binding OID
        TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &l)); // the typed value
        TEST_ASSERT_EQUAL_HEX8(expect[i], tag);
        pc_ber_skip(&d, l);
    }
}

// An unknown varbind type marks the encoder not-ok so the build fails closed.
void test_invalid_varbind_type()
{
    static const uint32_t voi[] = {1, 3, 6, 1};
    SnmpVarbind vb;
    memset(&vb, 0, sizeof(vb));
    vb.oid = voi;
    vb.oid_len = 4;
    vb.type = 99; // not a defined type
    uint8_t buf[128];
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_notify_build_v2c(buf, sizeof(buf), "public", (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2,
                                                         1, TRAP_OID, sizeof(TRAP_OID) / sizeof(uint32_t), 1, &vb, 1));
}

void test_build_v2c_null_args()
{
    uint8_t buf[128];
    const size_t tn = sizeof(TRAP_OID) / sizeof(uint32_t);
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_notify_build_v2c(NULL, 128, "public", (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1,
                                                         TRAP_OID, tn, 1, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_notify_build_v2c(buf, sizeof(buf), NULL, (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1,
                                                         TRAP_OID, tn, 1, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_notify_build_v2c(buf, sizeof(buf), "public", (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2,
                                                         1, NULL, 0, 1, NULL, 0));
}

// On the host build the UDP transport is a stub that reports failure.
void test_host_transport_stubs()
{
    const size_t tn = sizeof(TRAP_OID) / sizeof(uint32_t);
    TEST_ASSERT_FALSE(pc_snmp_trap_v2c("127.0.0.1", 162, "public", TRAP_OID, tn, NULL, 0));
    TEST_ASSERT_FALSE(pc_snmp_inform_v2c("127.0.0.1", 162, "public", 1, TRAP_OID, tn, NULL, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_trap_v2c_structure);
    RUN_TEST(test_all_varbind_types);
    RUN_TEST(test_invalid_varbind_type);
    RUN_TEST(test_build_v2c_null_args);
    RUN_TEST(test_host_transport_stubs);
    RUN_TEST(test_inform_tag);
    RUN_TEST(test_buffer_too_small);
    return UNITY_END();
}
