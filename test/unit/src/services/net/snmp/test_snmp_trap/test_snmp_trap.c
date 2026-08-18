// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SNMP notification originator (services/net/snmp/snmp_notify.h).
//
// test_rfc3416_trap_variable_bindings is the load-bearing case. RFC 3416 sec 4.2.6 states that in
// an SNMPv2-Trap-PDU "the first two variable bindings ... are sysUpTime.0 and snmpTrapOID.0
// respectively", and RFC 3418 sec 2 assigns those two names. A receiver reads the notification's
// identity out of the second binding, so a message whose first two bindings are absent, reordered,
// or named by any other OID is not a notification at all.
//
// The OIDs, the PDU tags and the version field below all come from the registry assignments and
// ASN.1 in RFC 3418, RFC 3416 sec 3 and RFC 1901 sec 3, with the arc arithmetic written out.

#include "services/net/snmp/snmp_ber.h"
#include "services/net/snmp/snmp_notify.h"
#include <string.h>

#include <unity.h>

static uint8_t snmp_ber_work[16]; // the borrow an entry takes; SnmpBer never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// sysUpTime is { system 3 }, system is { mib-2 1 }, mib-2 is 1.3.6.1.2.1 (RFC 3418 sec 2), and .0
// is the single instance of a scalar.
static const uint32_t OID_SYSUPTIME_0[] = {1, 3, 6, 1, 2, 1, 1, 3, 0};

// snmpTrapOID is { snmpTrap 1 }, snmpTrap is { snmpMIBObjects 4 }, snmpMIBObjects is { snmpMIB 1 },
// snmpMIB is { snmpModules 1 }, snmpModules is { snmpV2 3 } and snmpV2 is { internet 6 }
// (RFC 3418 sec 2 over RFC 2578), so the name is 1.3.6.1.6.3.1.1.4.1 and the instance is .0.
static const uint32_t OID_SNMPTRAPOID_0[] = {1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0};

// An enterprise-specific notification under 1.3.6.1.4.1 (RFC 2578 sec 8.4 enterprises).
static const uint32_t TRAP_OID[] = {1, 3, 6, 1, 4, 1, 9999, 2, 0, 1};

static BerDec g_dec;

static void dec_open(const uint8_t *buf, size_t len)
{
    SnmpBer.dec = &g_dec;
    SnmpBer.buf.in = buf;
    SnmpBer.buf.cap = len;
    SnmpBer.dec_init(snmp_ber_work);
}

static proto_bool read_header(void)
{
    SnmpBer.read_header(snmp_ber_work);
    return SnmpBer.ok;
}

static proto_bool read_integer(void)
{
    SnmpBer.read_integer(snmp_ber_work);
    return SnmpBer.ok;
}

// Read the next OBJECT IDENTIFIER and assert it names exactly the arcs given.
static void expect_oid(const uint32_t *want, size_t n, const char *msg)
{
    uint32_t got[SNMP_MAX_OID_LEN];
    SnmpBer.read_args.arc_out = got;
    SnmpBer.read_args.arc_cap = SNMP_MAX_OID_LEN;
    SnmpBer.read_oid(snmp_ber_work);
    TEST_ASSERT_TRUE_MESSAGE(SnmpBer.ok, msg);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(n, SnmpBer.n, msg);
    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(want[i], got[i], msg);
    }
}

static void skip_value(size_t n)
{
    SnmpBer.read_args.skip = n;
    SnmpBer.skip(snmp_ber_work);
}

// Set every PDU member a build reads, then build a complete SNMPv2c message.
static size_t build(uint8_t *out, size_t cap, uint8_t pdu_tag, uint32_t request_id, uint32_t uptime,
                    const SnmpVarbind *vbs, size_t vb_count, const char *community)
{
    SnmpNotify.pdu.pdu_tag = pdu_tag;
    SnmpNotify.pdu.request_id = request_id;
    SnmpNotify.pdu.trap_oid = TRAP_OID;
    SnmpNotify.pdu.trap_oid_len = sizeof(TRAP_OID) / sizeof(TRAP_OID[0]);
    SnmpNotify.pdu.uptime_ticks = uptime;
    SnmpNotify.pdu.vbs = vbs;
    SnmpNotify.pdu.vb_count = vb_count;
    SnmpNotify.dst.community = community;
    SnmpNotify.buf.out = out;
    SnmpNotify.buf.cap = cap;
    SnmpNotify.build_v2c(protocore_snmp_notify_span());
    return SnmpNotify.n;
}

// Walk the RFC 1901 sec 3 wrapper and the RFC 3416 sec 3 PDU header, leaving the cursor on the
// first variable binding. Reports the PDU's identifier octet.
static uint8_t walk_to_varbinds(const uint8_t *msg, size_t len, const char *community)
{
    dec_open(msg, len);
    TEST_ASSERT_TRUE(read_header()); // Message ::= SEQUENCE
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_SEQUENCE, SnmpBer.tag);
    TEST_ASSERT_TRUE(read_integer()); // version(1), RFC 1901 sec 3
    TEST_ASSERT_EQUAL_INT(1, SnmpBer.ival);
    TEST_ASSERT_TRUE(read_header()); // community OCTET STRING
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_OCTET_STRING, SnmpBer.tag);
    TEST_ASSERT_EQUAL_size_t(strlen(community), SnmpBer.vlen);
    TEST_ASSERT_EQUAL_MEMORY(community, g_dec.buf + g_dec.pos, SnmpBer.vlen);
    skip_value(SnmpBer.vlen);

    TEST_ASSERT_TRUE(read_header()); // the notification PDU
    const uint8_t pdu_tag = SnmpBer.tag;
    TEST_ASSERT_TRUE(read_integer()); // request-id
    const long request_id = SnmpBer.ival;
    (void)request_id;
    TEST_ASSERT_TRUE(read_integer()); // error-status, 0 in a notification (RFC 3416 sec 4.2.6)
    TEST_ASSERT_EQUAL_INT(0, SnmpBer.ival);
    TEST_ASSERT_TRUE(read_integer()); // error-index, 0
    TEST_ASSERT_EQUAL_INT(0, SnmpBer.ival);
    TEST_ASSERT_TRUE(read_header()); // variable-bindings SEQUENCE
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_SEQUENCE, SnmpBer.tag);
    return pdu_tag;
}

// RFC 3416 sec 4.2.6: sysUpTime.0 first, snmpTrapOID.0 second, caller bindings after them.
void test_rfc3416_trap_variable_bindings(void)
{
    static const uint32_t VB_OID[] = {1, 3, 6, 1, 4, 1, 9999, 3, 0};
    SnmpVarbind vb;
    memset(&vb, 0, sizeof(vb));
    vb.oid = VB_OID;
    vb.oid_len = sizeof(VB_OID) / sizeof(VB_OID[0]);
    vb.type = (uint8_t)SNMP_VB_GAUGE32;
    vb.ival = 42;

    uint8_t msg[256];
    const size_t n = build(msg, sizeof(msg), (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 7, 12345, &vb, 1, "public");
    TEST_ASSERT_TRUE(SnmpNotify.ok);
    TEST_ASSERT_TRUE(n > 0);

    const uint8_t pdu_tag = walk_to_varbinds(msg, n, "public");
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, pdu_tag);

    // binding 1: sysUpTime.0 with a TimeTicks value
    TEST_ASSERT_TRUE(read_header());
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_SEQUENCE, SnmpBer.tag);
    expect_oid(OID_SYSUPTIME_0, sizeof(OID_SYSUPTIME_0) / sizeof(OID_SYSUPTIME_0[0]), "sysUpTime.0");
    TEST_ASSERT_TRUE(read_header());
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_TIMETICKS, SnmpBer.tag);
    skip_value(SnmpBer.vlen);

    // binding 2: snmpTrapOID.0 whose value is the notification's own name
    TEST_ASSERT_TRUE(read_header());
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_SEQUENCE, SnmpBer.tag);
    expect_oid(OID_SNMPTRAPOID_0, sizeof(OID_SNMPTRAPOID_0) / sizeof(OID_SNMPTRAPOID_0[0]), "snmpTrapOID.0");
    expect_oid(TRAP_OID, sizeof(TRAP_OID) / sizeof(TRAP_OID[0]), "trap OID value");

    // binding 3: the caller's Gauge32
    TEST_ASSERT_TRUE(read_header());
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_SEQUENCE, SnmpBer.tag);
    expect_oid(VB_OID, sizeof(VB_OID) / sizeof(VB_OID[0]), "caller binding");
    TEST_ASSERT_TRUE(read_header());
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_GAUGE32, SnmpBer.tag);
}

// The uptime a caller states is the TimeTicks value of the first binding, and RFC 2578 sec 7.1.8
// makes that a non-negative 32-bit quantity: 0x80000000 takes a leading 0x00 sign octet, so the
// value is five octets 00 80 00 00 00.
void test_sysuptime_value_is_the_caller_uptime(void)
{
    uint8_t msg[256];
    const size_t n = build(msg, sizeof(msg), (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1, 0x80000000u, NULL, 0, "public");
    TEST_ASSERT_TRUE(n > 0);
    (void)walk_to_varbinds(msg, n, "public");

    TEST_ASSERT_TRUE(read_header()); // binding SEQUENCE
    expect_oid(OID_SYSUPTIME_0, sizeof(OID_SYSUPTIME_0) / sizeof(OID_SYSUPTIME_0[0]), "sysUpTime.0");
    TEST_ASSERT_TRUE(read_header());
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_TIMETICKS, SnmpBer.tag);
    TEST_ASSERT_EQUAL_size_t(5, SnmpBer.vlen);
    static const uint8_t WANT[5] = {0x00, 0x80, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_dec.buf + g_dec.pos, 5);
}

// RFC 3416 sec 3: SNMPv2-Trap-PDU is [7] IMPLICIT PDU and InformRequest-PDU is [6], and a
// context-specific constructed identifier octet is 0xA0 | tag number.
void test_rfc3416_pdu_tags(void)
{
    uint8_t msg[256];

    size_t n = build(msg, sizeof(msg), (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1, 1, NULL, 0, "public");
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8(0xA7, walk_to_varbinds(msg, n, "public"));

    n = build(msg, sizeof(msg), (uint8_t)SNMP_TAG_SNMP_PDU_INFORM, 1, 1, NULL, 0, "public");
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8(0xA6, walk_to_varbinds(msg, n, "public"));
}

// RFC 3416 sec 4.1: an InformRequest-PDU is answered by a Response-PDU carrying the same
// request-id, so the value the caller states is the value that goes on the wire.
void test_request_id_is_the_callers(void)
{
    uint8_t msg[256];
    const size_t n = build(msg, sizeof(msg), (uint8_t)SNMP_TAG_SNMP_PDU_INFORM, 305419896u, 1, NULL, 0, "public");
    TEST_ASSERT_TRUE(n > 0);

    dec_open(msg, n);
    TEST_ASSERT_TRUE(read_header()); // Message SEQUENCE
    TEST_ASSERT_TRUE(read_integer());
    TEST_ASSERT_TRUE(read_header()); // community
    SnmpBer.read_args.skip = SnmpBer.vlen;
    SnmpBer.skip(snmp_ber_work);
    TEST_ASSERT_TRUE(read_header()); // PDU
    TEST_ASSERT_TRUE(read_integer());
    TEST_ASSERT_EQUAL_INT(305419896, SnmpBer.ival);
}

// RFC 1901 sec 3: the wrapper is SEQUENCE { version INTEGER {version(1)}, community OCTET STRING,
// data }, so a community other than "public" is carried verbatim and the version stays 1.
void test_rfc1901_message_wrapper(void)
{
    uint8_t msg[256];
    const size_t n = build(msg, sizeof(msg), (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1, 1, NULL, 0, "s3cr3t-ro");
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8(0x30, msg[0]);
    (void)walk_to_varbinds(msg, n, "s3cr3t-ro");
}

// RFC 2578 sec 7.1: each ::SnmpVbType carries its own identifier octet - the ASN.1 simple types
// under their universal tags, the application-wide types under [APPLICATION n] as 0x40 | n.
void test_rfc2578_varbind_value_tags(void)
{
    static const uint32_t NAME[] = {1, 3, 6, 1, 4, 1, 1, 1};
    static const uint32_t OID_VALUE[] = {1, 3, 6, 1, 2, 1};
    static const uint8_t IPV4[4] = {10, 0, 0, 1};
    SnmpVarbind vbs[6];
    memset(vbs, 0, sizeof(vbs));
    for (size_t i = 0; i < 6; i++)
    {
        vbs[i].oid = NAME;
        vbs[i].oid_len = sizeof(NAME) / sizeof(NAME[0]);
    }
    vbs[0].type = (uint8_t)SNMP_VB_INT;
    vbs[0].ival = -5;
    vbs[1].type = (uint8_t)SNMP_VB_STRING;
    vbs[1].bytes = (const uint8_t *)"hi";
    vbs[1].blen = 2;
    vbs[2].type = (uint8_t)SNMP_VB_OID;
    vbs[2].oid_val = OID_VALUE;
    vbs[2].oid_val_len = sizeof(OID_VALUE) / sizeof(OID_VALUE[0]);
    vbs[3].type = (uint8_t)SNMP_VB_COUNTER32;
    vbs[3].ival = 100;
    vbs[4].type = (uint8_t)SNMP_VB_TIMETICKS;
    vbs[4].ival = 200;
    vbs[5].type = (uint8_t)SNMP_VB_IPADDR;
    vbs[5].bytes = IPV4;
    vbs[5].blen = 4;

    uint8_t msg[512];
    const size_t n = build(msg, sizeof(msg), (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1, 1, vbs, 6, "public");
    TEST_ASSERT_TRUE(SnmpNotify.ok);
    (void)walk_to_varbinds(msg, n, "public");

    for (int i = 0; i < 2; i++) // the two mandatory bindings come first
    {
        TEST_ASSERT_TRUE(read_header());
        skip_value(SnmpBer.vlen);
    }
    static const uint8_t WANT[6] = {0x02,  // INTEGER
                                    0x04,  // OCTET STRING
                                    0x06,  // OBJECT IDENTIFIER
                                    0x41,  // Counter32,  [APPLICATION 1]
                                    0x43,  // TimeTicks,  [APPLICATION 3]
                                    0x40}; // IpAddress,  [APPLICATION 0]
    for (int i = 0; i < 6; i++)
    {
        TEST_ASSERT_TRUE(read_header()); // binding SEQUENCE
        TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_SEQUENCE, SnmpBer.tag);
        expect_oid(NAME, sizeof(NAME) / sizeof(NAME[0]), "binding name");
        TEST_ASSERT_TRUE(read_header()); // the typed value
        TEST_ASSERT_EQUAL_HEX8(WANT[i], SnmpBer.tag);
        skip_value(SnmpBer.vlen);
    }
}

// RFC 2578 sec 7.1.5: IpAddress is [APPLICATION 0] IMPLICIT OCTET STRING (SIZE (4)), so the four
// octets go on the wire in network byte order, unchanged.
void test_rfc2578_ipaddress_is_four_octets(void)
{
    static const uint32_t NAME[] = {1, 3, 6, 1, 4, 1, 1, 1};
    static const uint8_t IPV4[4] = {192, 0, 2, 1}; // RFC 5737 documentation address
    SnmpVarbind vb;
    memset(&vb, 0, sizeof(vb));
    vb.oid = NAME;
    vb.oid_len = sizeof(NAME) / sizeof(NAME[0]);
    vb.type = (uint8_t)SNMP_VB_IPADDR;
    vb.bytes = IPV4;
    vb.blen = 4;

    uint8_t msg[256];
    const size_t n = build(msg, sizeof(msg), (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1, 1, &vb, 1, "public");
    (void)walk_to_varbinds(msg, n, "public");
    for (int i = 0; i < 2; i++)
    {
        TEST_ASSERT_TRUE(read_header());
        skip_value(SnmpBer.vlen);
    }
    TEST_ASSERT_TRUE(read_header());
    expect_oid(NAME, sizeof(NAME) / sizeof(NAME[0]), "binding name");
    TEST_ASSERT_TRUE(read_header());
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_IPADDRESS, SnmpBer.tag);
    TEST_ASSERT_EQUAL_size_t(4, SnmpBer.vlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(IPV4, g_dec.buf + g_dec.pos, 4);
}

// A PDU can be appended to an encoder the caller already opened, without the message wrapper: the
// PDU's own identifier octet comes first and the mandatory bindings still lead.
void test_build_pdu_appends_to_an_open_encoder(void)
{
    uint8_t buf[256];
    BerEnc e;
    SnmpBer.enc = &e;
    SnmpBer.buf.out = buf;
    SnmpBer.buf.cap = sizeof(buf);
    SnmpBer.enc_init(snmp_ber_work);

    SnmpNotify.pdu.pdu_tag = (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2;
    SnmpNotify.pdu.request_id = 3;
    SnmpNotify.pdu.trap_oid = TRAP_OID;
    SnmpNotify.pdu.trap_oid_len = sizeof(TRAP_OID) / sizeof(TRAP_OID[0]);
    SnmpNotify.pdu.uptime_ticks = 9;
    SnmpNotify.pdu.vbs = NULL;
    SnmpNotify.pdu.vb_count = 0;
    SnmpNotify.buf.enc = &e;
    SnmpNotify.build_pdu(protocore_snmp_notify_span());
    TEST_ASSERT_TRUE(SnmpNotify.ok);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, buf[0]);

    dec_open(buf, e.len);
    TEST_ASSERT_TRUE(read_header());
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, SnmpBer.tag);
    TEST_ASSERT_TRUE(read_integer());
    TEST_ASSERT_EQUAL_INT(3, SnmpBer.ival);
    TEST_ASSERT_TRUE(read_integer());
    TEST_ASSERT_TRUE(read_integer());
    TEST_ASSERT_TRUE(read_header()); // variable-bindings SEQUENCE
    TEST_ASSERT_TRUE(read_header()); // first binding
    expect_oid(OID_SYSUPTIME_0, sizeof(OID_SYSUPTIME_0) / sizeof(OID_SYSUPTIME_0[0]), "sysUpTime.0");
}

// A buffer too small for the whole message reports nothing written rather than a truncated
// datagram: a partial BER encoding is not a shorter notification, it is garbage.
void test_short_buffer_writes_nothing(void)
{
    uint8_t small[8];
    const size_t n = build(small, sizeof(small), (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1, 1, NULL, 0, "public");
    TEST_ASSERT_EQUAL_size_t(0, n);
    TEST_ASSERT_FALSE(SnmpNotify.ok);
}

// A binding whose type is none of the ::SnmpVbType values has no encoding, so the whole build
// fails rather than emitting a binding with a missing value.
void test_unknown_varbind_type_fails_closed(void)
{
    static const uint32_t NAME[] = {1, 3, 6, 1};
    SnmpVarbind vb;
    memset(&vb, 0, sizeof(vb));
    vb.oid = NAME;
    vb.oid_len = 4;
    vb.type = 99;

    uint8_t msg[256];
    const size_t n = build(msg, sizeof(msg), (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1, 1, &vb, 1, "public");
    TEST_ASSERT_EQUAL_size_t(0, n);
    TEST_ASSERT_FALSE(SnmpNotify.ok);
}

// A missing destination buffer, community, or notification name is refused before anything is
// written; the notification name is what RFC 3418 sec 2 puts in the second binding, so there is no
// notification without it.
void test_missing_arguments_are_refused(void)
{
    uint8_t msg[128];
    TEST_ASSERT_EQUAL_size_t(0, build(NULL, sizeof(msg), (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1, 1, NULL, 0, "public"));
    TEST_ASSERT_EQUAL_size_t(0, build(msg, sizeof(msg), (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 1, 1, NULL, 0, NULL));

    SnmpNotify.pdu.pdu_tag = (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2;
    SnmpNotify.pdu.trap_oid = NULL;
    SnmpNotify.pdu.trap_oid_len = 0;
    SnmpNotify.dst.community = "public";
    SnmpNotify.buf.out = msg;
    SnmpNotify.buf.cap = sizeof(msg);
    SnmpNotify.build_v2c(protocore_snmp_notify_span());
    TEST_ASSERT_EQUAL_size_t(0, SnmpNotify.n);
    TEST_ASSERT_FALSE(SnmpNotify.ok);

    // build_pdu with no open encoder is the same refusal.
    SnmpNotify.buf.enc = NULL;
    SnmpNotify.pdu.trap_oid = TRAP_OID;
    SnmpNotify.pdu.trap_oid_len = sizeof(TRAP_OID) / sizeof(TRAP_OID[0]);
    SnmpNotify.build_pdu(protocore_snmp_notify_span());
    TEST_ASSERT_FALSE(SnmpNotify.ok);
}

// RFC 3417 sec 3.1 carries a notification on a UDP datagram. This build has no transport, so the
// two send calls report that nothing was transmitted instead of claiming success.
void test_sends_report_no_transport(void)
{
    SnmpNotify.pdu.trap_oid = TRAP_OID;
    SnmpNotify.pdu.trap_oid_len = sizeof(TRAP_OID) / sizeof(TRAP_OID[0]);
    SnmpNotify.pdu.vbs = NULL;
    SnmpNotify.pdu.vb_count = 0;
    SnmpNotify.dst.dst_ip = "127.0.0.1";
    SnmpNotify.dst.port = 162; // RFC 3417 sec 3.2
    SnmpNotify.dst.community = "public";

    SnmpNotify.trap_v2c(protocore_snmp_notify_span());
    TEST_ASSERT_FALSE(SnmpNotify.ok);
    TEST_ASSERT_EQUAL_size_t(0, SnmpNotify.n);

    SnmpNotify.pdu.request_id = 1;
    SnmpNotify.inform_v2c(protocore_snmp_notify_span());
    TEST_ASSERT_FALSE(SnmpNotify.ok);
    TEST_ASSERT_EQUAL_size_t(0, SnmpNotify.n);
}
