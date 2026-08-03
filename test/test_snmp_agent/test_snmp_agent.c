// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SNMP v1/v2c agent core (pc_snmp_agent_process). Each test
// builds a real request datagram with the BER encoder, runs it through the
// agent, and decodes the response - no sockets, no heap.

#include "network_drivers/transport/udp.h"
#include "services/net/snmp/snmp_agent.h"
#include "services/net/snmp/snmp_ber.h"
#include <string.h>
#include <unity.h>

// ---------------------------------------------------------------------------
// Test MIB
// ---------------------------------------------------------------------------

static const uint32_t OID_SYSDESCR[] = {1, 3, 6, 1, 2, 1, 1, 1, 0};
static const uint32_t OID_SYSUPTIME[] = {1, 3, 6, 1, 2, 1, 1, 3, 0};
static const uint32_t OID_SYSPREFIX[] = {1, 3, 6, 1, 2, 1, 1}; // for GetNext
static const uint32_t OID_RW[] = {1, 3, 6, 1, 4, 1, 49374, 1, 0};
static const uint32_t OID_RO[] = {1, 3, 6, 1, 4, 1, 49374, 2, 0};
static const uint32_t OID_CTR[] = {1, 3, 6, 1, 4, 1, 49374, 3, 0};
static const uint32_t OID_PAST_END[] = {1, 3, 6, 1, 4, 1, 49374, 99, 0};
static const uint32_t OID_UNKNOWN[] = {1, 3, 6, 1, 2, 1, 99, 0};
// sysDescr exists as object 1.3.6.1.2.1.1.1 but only instance .0; ask for .5.
static const uint32_t OID_SYSDESCR_BADINST[] = {1, 3, 6, 1, 2, 1, 1, 1, 5};

static const char *SYSDESCR_VAL = "PC test agent";

static bool g_set_called = false;
static long g_set_value = 0;

static bool rw_setter(const SnmpValue *in)
{
    g_set_called = true;
    if (in->type != (uint8_t)BER_INTEGER)
    {
        return false;
    }
    g_set_value = in->ival;
    return true;
}

static bool ctr_getter(SnmpValue *out)
{
    out->type = (uint8_t)SNMP_COUNTER32;
    out->uval = 12345u;
    return true;
}

void setUp()
{
    pc_snmp_agent_init("public");
    pc_snmp_agent_set_rw_community("private");
    pc_snmp_agent_set_system(SYSDESCR_VAL, "admin", "esp32", "lab", 72);
    pc_snmp_agent_add_integer(OID_RW, 9, 42, rw_setter);
    pc_snmp_agent_add_integer(OID_RO, 9, 7, NULL); // read-only (no setter)
    pc_snmp_agent_add_dynamic(OID_CTR, 9, (uint8_t)SNMP_COUNTER32, ctr_getter);
    g_set_called = false;
    g_set_value = 0;
}

void tearDown()
{
}

// ---------------------------------------------------------------------------
// Request builder / response parser
// ---------------------------------------------------------------------------

static size_t build_req(uint8_t *buf, size_t cap, long version, const char *comm, uint8_t pdu, long reqid, long f2,
                        long f3, const uint32_t *oid, size_t oidn, const SnmpValue *setval)
{
    BerEnc e;
    pc_ber_enc_init(&e, buf, cap);
    size_t msg = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
    pc_ber_put_integer(&e, version);
    pc_ber_put_octet_string(&e, (uint8_t)BER_OCTET_STRING, (const uint8_t *)comm, strlen(comm));
    size_t pdus = pc_ber_seq_begin(&e, pdu);
    pc_ber_put_integer(&e, reqid);
    pc_ber_put_integer(&e, f2);
    pc_ber_put_integer(&e, f3);
    size_t vbl = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
    size_t vb = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
    pc_ber_put_oid(&e, oid, oidn);
    if (setval && setval->type == (uint8_t)BER_INTEGER)
    {
        pc_ber_put_integer(&e, setval->ival);
    }
    else if (setval && setval->type == (uint8_t)BER_OCTET_STRING)
    {
        pc_ber_put_octet_string(&e, (uint8_t)BER_OCTET_STRING, (const uint8_t *)setval->str, setval->str_len);
    }
    else
    {
        pc_ber_put_null(&e);
    }
    pc_ber_seq_end(&e, vb);
    pc_ber_seq_end(&e, vbl);
    pc_ber_seq_end(&e, pdus);
    pc_ber_seq_end(&e, msg);
    return e.ok ? e.len : 0;
}

// Build a bare PDU (tag + request-id + 2 fields + a varbind list) for direct pc_snmp_dispatch_pdu tests.
// knob selects a malformation of the varbind list (or an OID-typed value).
enum
{
    VB_BAD_VBL_TAG,
    VB_TOO_MANY,
    VB_BAD_VB_TAG,
    VB_BAD_OID,
    VB_BAD_VALUE,
    VB_OID_VALUE,
    VB_BAD_OID_VALUE,
    VB_VALID
};
static size_t build_pdu(uint8_t *buf, size_t cap, int knob)
{
    BerEnc e;
    pc_ber_enc_init(&e, buf, cap);
    size_t pdus = pc_ber_seq_begin(&e, (uint8_t)SNMP_PDU_GET);
    pc_ber_put_integer(&e, 42); // request-id
    pc_ber_put_integer(&e, 0);
    pc_ber_put_integer(&e, 0);
    if (knob == VB_BAD_VBL_TAG)
    {
        pc_ber_put_octet_string(&e, (uint8_t)BER_OCTET_STRING, (const uint8_t *)"x",
                                1); // varbind list not a SEQUENCE
    }
    else
    {
        size_t vbl = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
        if (knob == VB_TOO_MANY)
        {
            for (int i = 0; i <= SNMP_MAX_VARBINDS; i++) // one more than the table holds
            {
                size_t vb = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
                pc_ber_put_oid(&e, OID_SYSDESCR, 9);
                pc_ber_put_null(&e);
                pc_ber_seq_end(&e, vb);
            }
        }
        else if (knob == VB_BAD_VB_TAG)
        {
            pc_ber_put_octet_string(&e, (uint8_t)BER_OCTET_STRING, (const uint8_t *)"x",
                                    1); // a varbind that is not a SEQUENCE
        }
        else if (knob == VB_BAD_OID)
        {
            size_t vb = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
            pc_ber_put_integer(&e, 5); // first field is not an OID
            pc_ber_put_null(&e);
            pc_ber_seq_end(&e, vb);
        }
        else if (knob == VB_BAD_VALUE)
        {
            size_t vb = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
            pc_ber_put_oid(&e, OID_SYSDESCR, 9);
            uint8_t badv[2] = {(uint8_t)BER_OCTET_STRING,
                               0x7F}; // declares 127 value octets that are not present
            pc_ber_put_raw(&e, badv, sizeof(badv));
            pc_ber_seq_end(&e, vb);
        }
        else if (knob ==
                 VB_OID_VALUE) // a varbind whose value is a valid OID (dec_value (uint8_t)BER_OID success)
        {
            size_t vb = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
            pc_ber_put_oid(&e, OID_SYSDESCR, 9);
            pc_ber_put_oid(&e, OID_SYSUPTIME, 9);
            pc_ber_seq_end(&e, vb);
        }
        else if (knob == VB_BAD_OID_VALUE) // an OID-typed value that fails to decode (empty OID)
        {
            size_t vb = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
            pc_ber_put_oid(&e, OID_SYSDESCR, 9);
            uint8_t empty_oid[2] = {(uint8_t)BER_OID, 0x00}; // OID tag, zero length -> pc_ber_read_oid rejects
            pc_ber_put_raw(&e, empty_oid, sizeof(empty_oid));
            pc_ber_seq_end(&e, vb);
        }
        else // VB_VALID: a well-formed GET varbind (name + NULL value)
        {
            size_t vb = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
            pc_ber_put_oid(&e, OID_SYSDESCR, 9);
            pc_ber_put_null(&e);
            pc_ber_seq_end(&e, vb);
        }
        pc_ber_seq_end(&e, vbl);
    }
    pc_ber_seq_end(&e, pdus);
    return e.ok ? e.len : 0;
}

struct RespView
{
    long version;
    uint8_t pdu_tag;
    long request_id;
    long err_status;
    long err_index;
    size_t nvb;
    uint32_t oid[SNMP_MAX_OID_LEN]; // first varbind
    size_t oid_len;
    uint8_t val_tag;
    uint8_t last_val_tag; // value tag of the final varbind (for GetBulk tail checks)
    long ival;
    uint32_t uval;
    char str[64];
    size_t str_len;
};

static bool parse_resp(const uint8_t *buf, size_t len, RespView *rv)
{
    memset(rv, 0, sizeof(*rv));
    BerDec d;
    pc_ber_dec_init(&d, buf, len);
    uint8_t tag;
    size_t l;
    if (!pc_ber_read_header(&d, &tag, &l) || tag != (uint8_t)BER_SEQUENCE)
    {
        return false;
    }
    if (!pc_ber_read_integer(&d, &rv->version))
    {
        return false;
    }
    uint8_t ctag;
    size_t cl;
    if (!pc_ber_read_header(&d, &ctag, &cl))
    {
        return false;
    }
    d.pos += cl; // skip community
    if (!pc_ber_read_header(&d, &rv->pdu_tag, &l))
    {
        return false;
    }
    if (!pc_ber_read_integer(&d, &rv->request_id) || !pc_ber_read_integer(&d, &rv->err_status) ||
        !pc_ber_read_integer(&d, &rv->err_index))
    {
        return false;
    }
    uint8_t vt;
    size_t vl;
    if (!pc_ber_read_header(&d, &vt, &vl) || vt != (uint8_t)BER_SEQUENCE)
    {
        return false;
    }
    size_t vend = d.pos + vl;
    bool first = true;
    while (d.pos < vend && d.ok)
    {
        uint8_t st;
        size_t sl;
        if (!pc_ber_read_header(&d, &st, &sl) || st != (uint8_t)BER_SEQUENCE)
        {
            return false;
        }
        size_t vbend = d.pos + sl;
        uint32_t oid[SNMP_MAX_OID_LEN];
        size_t on;
        if (!pc_ber_read_oid(&d, oid, SNMP_MAX_OID_LEN, &on))
        {
            return false;
        }
        size_t save = d.pos;
        uint8_t valtag;
        size_t vallen;
        if (!pc_ber_read_header(&d, &valtag, &vallen))
        {
            return false;
        }
        if (first)
        {
            memcpy(rv->oid, oid, on * sizeof(uint32_t));
            rv->oid_len = on;
            rv->val_tag = valtag;
            if (valtag == (uint8_t)BER_INTEGER)
            {
                d.pos = save;
                pc_ber_read_integer(&d, &rv->ival);
            }
            else if (valtag == (uint8_t)BER_OCTET_STRING || valtag == (uint8_t)SNMP_IPADDRESS ||
                     valtag == (uint8_t)SNMP_OPAQUE)
            {
                size_t cpy = vallen < sizeof(rv->str) - 1 ? vallen : sizeof(rv->str) - 1;
                memcpy(rv->str, d.buf + d.pos, cpy);
                rv->str[cpy] = '\0';
                rv->str_len = vallen;
            }
            else if (valtag == (uint8_t)SNMP_TIMETICKS || valtag == (uint8_t)SNMP_COUNTER32 ||
                     valtag == (uint8_t)SNMP_GAUGE32)
            {
                uint32_t a = 0;
                for (size_t i = 0; i < vallen; i++)
                {
                    a = (a << 8) | d.buf[d.pos + i];
                }
                rv->uval = a;
            }
            first = false;
        }
        d.pos = vbend;
        rv->nvb++;
    }
    return d.ok;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_get_string_v2c()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 111,
                          0, 0, OID_SYSDESCR, 9, NULL);
    TEST_ASSERT_TRUE(rl > 0);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_PDU_RESPONSE, rv.pdu_tag);
    TEST_ASSERT_EQUAL_INT(111, rv.request_id);
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)BER_OCTET_STRING, rv.val_tag);
    TEST_ASSERT_EQUAL_STRING(SYSDESCR_VAL, rv.str);
}

void test_get_unknown_v2c_exception()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 7, 0,
                          0, OID_UNKNOWN, 8, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_NO_SUCH_OBJECT, rv.val_tag);
}

// RFC 3416 4.2.1: a known object (sysDescr) but a nonexistent instance (.5) must
// report noSuchInstance, distinct from the noSuchObject above for an unknown OID.
void test_get_bad_instance_v2c_nosuchinstance()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 8, 0,
                          0, OID_SYSDESCR_BADINST, 9, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_NO_SUCH_INSTANCE, rv.val_tag);
}

void test_get_unknown_v1_error()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V1, "public", (uint8_t)SNMP_PDU_GET, 7, 0,
                          0, OID_UNKNOWN, 8, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_SUCH_NAME, rv.err_status);
    TEST_ASSERT_EQUAL_INT(1, rv.err_index);
}

void test_getnext_walks_to_first()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GETNEXT, 5,
                          0, 0, OID_SYSPREFIX, 7, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)BER_OCTET_STRING, rv.val_tag);
    TEST_ASSERT_EQUAL_UINT(9, rv.oid_len);
    TEST_ASSERT_EQUAL_UINT32(1u, rv.oid[7]); // sysDescr index .1
    TEST_ASSERT_EQUAL_STRING(SYSDESCR_VAL, rv.str);
}

void test_getnext_past_end_endofmibview()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GETNEXT, 9,
                          0, 0, OID_PAST_END, 9, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_END_OF_MIB_VIEW, rv.val_tag);
}

void test_set_without_rw_community_denied()
{
    uint8_t req[256], resp[256];
    SnmpValue sv;
    memset(&sv, 0, sizeof(sv));
    sv.type = (uint8_t)BER_INTEGER;
    sv.ival = 99;
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_SET, 3, 0,
                          0, OID_RW, 9, &sv);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ACCESS, rv.err_status);
    TEST_ASSERT_FALSE(g_set_called);
}

void test_set_with_rw_community_invokes_setter()
{
    uint8_t req[256], resp[256];
    SnmpValue sv;
    memset(&sv, 0, sizeof(sv));
    sv.type = (uint8_t)BER_INTEGER;
    sv.ival = 99;
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "private", (uint8_t)SNMP_PDU_SET, 3, 0,
                          0, OID_RW, 9, &sv);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_TRUE(g_set_called);
    TEST_ASSERT_EQUAL_INT(99, g_set_value);
}

void test_set_readonly_not_writable()
{
    uint8_t req[256], resp[256];
    SnmpValue sv;
    memset(&sv, 0, sizeof(sv));
    sv.type = (uint8_t)BER_INTEGER;
    sv.ival = 1;
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "private", (uint8_t)SNMP_PDU_SET, 3, 0,
                          0, OID_RO, 9, &sv);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NOT_WRITABLE, rv.err_status);
}

void test_getbulk_returns_multiple()
{
    uint8_t req[512], resp[512];
    // non-repeaters=0, max-repetitions=3, one repeater starting at the system prefix.
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GETBULK, 1,
                          0, 3, OID_SYSPREFIX, 7, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_UINT(3, rv.nvb);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)BER_OCTET_STRING, rv.val_tag); // first = sysDescr
}

void test_dynamic_counter_value()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 2, 0,
                          0, OID_CTR, 9, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_COUNTER32, rv.val_tag);
    TEST_ASSERT_EQUAL_UINT32(12345u, rv.uval);
}

void test_uptime_is_timeticks()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 2, 0,
                          0, OID_SYSUPTIME, 9, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TIMETICKS, rv.val_tag);
}

void test_unknown_community_no_response()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "wrongcomm", (uint8_t)SNMP_PDU_GET, 1,
                          0, 0, OID_SYSDESCR, 9, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT(0, n);
}

void test_v3_message_dropped()
{
    uint8_t req[64];
    BerEnc e;
    pc_ber_enc_init(&e, req, sizeof(req));
    size_t msg = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
    pc_ber_put_integer(&e, (int)SNMP_V3);
    pc_ber_seq_end(&e, msg);
    uint8_t resp[64];
    size_t n = pc_snmp_agent_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT(0, n);
}

static const uint32_t OID_IP[] = {1, 3, 6, 1, 4, 1, 49374, 4, 0};
static bool ip_getter(SnmpValue *out)
{
    out->type = (uint8_t)SNMP_IPADDRESS;
    out->uval = 0xC0A80101u; // 192.168.1.1
    return true;
}

// Registration rejects an OID shorter than 2 arcs; a cleared rw community denies Set.
void test_registration_and_rw_edges()
{
    const uint32_t shortoid[] = {1};
    TEST_ASSERT_FALSE(pc_snmp_agent_add_string(shortoid, 1, "x", NULL));
    TEST_ASSERT_FALSE(pc_snmp_agent_add_integer(shortoid, 1, 5, NULL));
    TEST_ASSERT_FALSE(pc_snmp_agent_add_dynamic(shortoid, 1, (uint8_t)BER_INTEGER, NULL));

    // With the rw community cleared, a Set arriving on the ro community is answered
    // (community is still known) but denied write access.
    pc_snmp_agent_set_rw_community(NULL);
    uint8_t req[256], resp[256];
    SnmpValue sv;
    memset(&sv, 0, sizeof(sv));
    sv.type = (uint8_t)BER_INTEGER;
    sv.ival = 1;
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_SET, 3, 0,
                          0, OID_RW, 9, &sv);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ACCESS, rv.err_status);
}

// An IpAddress-typed dynamic value encodes with the (uint8_t)SNMP_IPADDRESS tag.
void test_ipaddress_value_encodes()
{
    TEST_ASSERT_TRUE(pc_snmp_agent_add_dynamic(OID_IP, 9, (uint8_t)SNMP_IPADDRESS, ip_getter));
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 1, 0,
                          0, OID_IP, 9, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    // Encoded as a 4-byte IpAddress-tagged string (RFC 2578 IpAddress = network-order octets).
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_IPADDRESS, rv.val_tag);
    TEST_ASSERT_EQUAL_size_t(4, rv.str_len);
    const uint8_t want[4] = {0xC0, 0xA8, 0x01, 0x01};
    TEST_ASSERT_EQUAL_MEMORY(want, rv.str, 4);
}

// Set with a wrong-typed value (string into an integer object) is wrongType; Set of an
// unknown OID is noSuchName.
void test_set_wrong_type_and_unknown()
{
    uint8_t req[256], resp[256];
    RespView rv;
    SnmpValue s;
    memset(&s, 0, sizeof(s));
    s.type = (uint8_t)BER_OCTET_STRING;
    s.str = "hi";
    s.str_len = 2;
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "private", (uint8_t)SNMP_PDU_SET, 3, 0,
                          0, OID_RW, 9, &s);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_WRONG_TYPE, rv.err_status);

    SnmpValue iv;
    memset(&iv, 0, sizeof(iv));
    iv.type = (uint8_t)BER_INTEGER;
    iv.ival = 1;
    rl = build_req(req, sizeof(req), (int)SNMP_V2C, "private", (uint8_t)SNMP_PDU_SET, 3, 0, 0,
                   OID_UNKNOWN, 8, &iv);
    n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_SUCH_NAME, rv.err_status);
}

// GetBulk with a non-repeater; a v1 GetBulk is not allowed; a repeater past the end
// of the MIB yields endOfMibView for every remaining repetition.
void test_getbulk_variants()
{
    uint8_t req[512], resp[512];
    RespView rv;
    // non-repeaters = 1, max-repetitions = 2, one varbind at the system prefix.
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GETBULK, 1,
                          1, 2, OID_SYSPREFIX, 7, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_GREATER_THAN(0, (int)rv.nvb);

    // GetBulk is v2c+: a v1 GetBulk is dropped.
    rl = build_req(req, sizeof(req), (int)SNMP_V1, "public", (uint8_t)SNMP_PDU_GETBULK, 1, 0, 2,
                   OID_SYSPREFIX, 7, NULL);
    TEST_ASSERT_EQUAL_UINT(0, pc_snmp_agent_process(req, rl, resp, sizeof(resp)));

    // A repeater starting past the last object: every repetition is endOfMibView.
    rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GETBULK, 1, 0, 3,
                   OID_PAST_END, 9, NULL);
    n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_END_OF_MIB_VIEW, rv.val_tag);
}

// Build a GET-shaped PDU whose one varbind carries a value TLV of @p value_kind, to
// exercise the value decoder's non-NULL branches. Returns the encoded PDU length.
static size_t build_pdu_with_value(uint8_t *buf, size_t cap, uint8_t pdu_tag, int value_kind)
{
    BerEnc e;
    pc_ber_enc_init(&e, buf, cap);
    size_t p = pc_ber_seq_begin(&e, pdu_tag);
    pc_ber_put_integer(&e, 1);
    pc_ber_put_integer(&e, 0);
    pc_ber_put_integer(&e, 0);
    size_t vbl = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
    size_t vb = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
    pc_ber_put_oid(&e, OID_SYSDESCR, 9);
    if (value_kind == 1)
    {
        pc_ber_put_uint(&e, (uint8_t)SNMP_GAUGE32, 500); // application uint value
    }
    else if (value_kind == 2)
    {
        pc_ber_put_oid(&e, OID_SYSUPTIME, 9); // OID value
    }
    else
    {
        pc_ber_put_null(&e);
    }
    pc_ber_seq_end(&e, vb);
    pc_ber_seq_end(&e, vbl);
    pc_ber_seq_end(&e, p);
    return e.ok ? e.len : 0;
}

// The value decoder handles uint/OID-typed varbind values, unsupported PDU tags are
// dropped, and any truncated prefix of a valid PDU fails closed.
void test_dispatch_value_types_and_malformed()
{
    uint8_t pdu[128], out[256];
    // uint-typed and OID-typed varbind values decode without error.
    size_t g = build_pdu_with_value(pdu, sizeof(pdu), (uint8_t)SNMP_PDU_GET, 1);
    TEST_ASSERT_TRUE(pc_snmp_dispatch_pdu(pdu, g, false, true, out, sizeof(out)) > 0);
    size_t o = build_pdu_with_value(pdu, sizeof(pdu), (uint8_t)SNMP_PDU_GET, 2);
    TEST_ASSERT_TRUE(pc_snmp_dispatch_pdu(pdu, o, false, true, out, sizeof(out)) > 0);

    // An unsupported PDU tag (a trap) is dropped.
    size_t t = build_pdu_with_value(pdu, sizeof(pdu), (uint8_t)SNMP_PDU_TRAPV2, 0);
    TEST_ASSERT_EQUAL_UINT(0, pc_snmp_dispatch_pdu(pdu, t, false, true, out, sizeof(out)));

    // Every truncated prefix of a valid GET PDU fails closed.
    size_t full = build_pdu_with_value(pdu, sizeof(pdu), (uint8_t)SNMP_PDU_GET, 0);
    TEST_ASSERT_TRUE(pc_snmp_dispatch_pdu(pdu, full, false, true, out, sizeof(out)) > 0);
    for (size_t l = 0; l < full; l++)
    {
        TEST_ASSERT_EQUAL_UINT(0, pc_snmp_dispatch_pdu(pdu, l, false, true, out, sizeof(out)));
    }
}

void test_getbulk_repeaters_and_end()
{
    uint8_t req[256], resp[512];
    // Pure repeaters (non_rep=0, max_rep=3) walk successive OIDs from the sys prefix.
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GETBULK,
                          20, 0, 3, OID_SYSPREFIX, 7, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_TRUE(rv.nvb >= 1);
    // A non-repeater whose OID is past the end yields endOfMibView.
    rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GETBULK, 21, 1, 2,
                   OID_PAST_END, 9, NULL);
    TEST_ASSERT_TRUE(pc_snmp_agent_process(req, rl, resp, sizeof(resp)) > 0);
}

void test_getbulk_nonrep_clamp_and_v1_reject()
{
    uint8_t req[256], resp[512];
    // non_rep (5) exceeds the single varbind -> clamped to the varbind count.
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GETBULK,
                          22, 5, 2, OID_SYSPREFIX, 7, NULL);
    TEST_ASSERT_TRUE(pc_snmp_agent_process(req, rl, resp, sizeof(resp)) > 0);
    // GetBulk is v2c+; a v1 GetBulk is rejected.
    rl = build_req(req, sizeof(req), (int)SNMP_V1, "public", (uint8_t)SNMP_PDU_GETBULK, 23, 0, 3,
                   OID_SYSPREFIX, 7, NULL);
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_agent_process(req, rl, resp, sizeof(resp)));
}

void test_response_too_big_reencodes()
{
    uint8_t req[256], resp[28]; // too small for the full sysDescr response
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 40, 0,
                          0, OID_SYSDESCR, 9, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    if (n > 0)
    {
        RespView rv;
        TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
        TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_TOO_BIG, rv.err_status);
    }
}

void test_version_and_community_guards()
{
    uint8_t req[256], resp[512];
    // v3 with the USM layer not built here -> 0.
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V3, "public", (uint8_t)SNMP_PDU_GET, 1, 0,
                          0, OID_SYSDESCR, 9, NULL);
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_agent_process(req, rl, resp, sizeof(resp)));
    // An unknown version number is rejected.
    rl = build_req(req, sizeof(req), 5, "public", (uint8_t)SNMP_PDU_GET, 1, 0, 0, OID_SYSDESCR, 9, NULL);
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_agent_process(req, rl, resp, sizeof(resp)));
    // An unknown community is silently dropped.
    rl = build_req(req, sizeof(req), (int)SNMP_V2C, "wrongcomm", (uint8_t)SNMP_PDU_GET, 1, 0, 0,
                   OID_SYSDESCR, 9, NULL);
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_agent_process(req, rl, resp, sizeof(resp)));
}

void test_dispatch_malformed_pdu()
{
    uint8_t resp[128];
    // A PDU whose header parses but whose request-id integer is truncated fails closed.
    uint8_t junk[3] = {0xA0, 0x01, 0x05};
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_dispatch_pdu(junk, sizeof(junk), false, true, resp, sizeof(resp)));
    // A lone PDU tag with no content fails closed.
    uint8_t bare[1] = {0xA0};
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_dispatch_pdu(bare, sizeof(bare), false, true, resp, sizeof(resp)));
}

void test_udp_handler_via_inject()
{
    pc_udp_reset_listeners();
    pc_udp_capture_enable();
    pc_udp_capture_reset();
    pc_snmp_agent_begin_udp(161);
    uint8_t req[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 50, 0,
                          0, OID_SYSDESCR, 9, NULL);
    pc_udp_inject(161, "192.0.2.1", 40000, req, rl);
    // The bound handler processed the datagram and sent a reply (captured).
    TEST_ASSERT_TRUE(pc_udp_captured_len() > 0);
    pc_udp_reset_listeners();
}

void test_malformed_message_guards()
{
    uint8_t resp[128];
    uint8_t not_seq[3] = {0x02, 0x01, 0x00}; // an INTEGER where the wrapper SEQUENCE is expected
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_agent_process(not_seq, sizeof(not_seq), resp, sizeof(resp)));
    uint8_t empty_seq[2] = {0x30, 0x00}; // SEQUENCE with no version integer
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_agent_process(empty_seq, sizeof(empty_seq), resp, sizeof(resp)));
    uint8_t bad_comm[8] = {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00}; // version ok, community not OCTET STRING
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_agent_process(bad_comm, sizeof(bad_comm), resp, sizeof(resp)));
}

// Each malformed varbind list is rejected by the dispatcher's per-varbind guards; an OID-typed value
// is decoded (dec_value's (uint8_t)BER_OID branch) then handled as a normal GET.
void test_snmp_dispatch_varbind_guards()
{
    uint8_t pdu[512], resp[256];
    int reject[] = {VB_BAD_VBL_TAG, VB_TOO_MANY, VB_BAD_VB_TAG, VB_BAD_OID, VB_BAD_VALUE, VB_BAD_OID_VALUE};
    for (unsigned i = 0; i < sizeof(reject) / sizeof(reject[0]); i++)
    {
        size_t n = build_pdu(pdu, sizeof(pdu), reject[i]);
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_EQUAL_size_t(0, pc_snmp_dispatch_pdu(pdu, n, false, true, resp, sizeof(resp)));
    }
    size_t n = build_pdu(pdu, sizeof(pdu), VB_OID_VALUE);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(pc_snmp_dispatch_pdu(pdu, n, false, true, resp, sizeof(resp)) > 0); // GET with an OID value
    // A response that does not fit the output buffer is re-encoded as tooBig with an empty varbind list.
    n = build_pdu(pdu, sizeof(pdu), VB_VALID);
    uint8_t tiny[24]; // fits the empty tooBig PDU but not the full sysDescr response
    TEST_ASSERT_TRUE(pc_snmp_dispatch_pdu(pdu, n, false, true, tiny, sizeof(tiny)) > 0);
}

// A request OID that extends a registered OID (longer, shared prefix) drives oid_cmp's an<bn branch.
void test_snmp_oid_cmp_request_longer()
{
    uint8_t req[256], resp[256];
    static const uint32_t OID_LONGER[] = {1, 3, 6, 1, 2, 1, 1, 1, 0, 7}; // sysDescr.0 + an extra arc
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 1, 0,
                          0, OID_LONGER, 10, NULL);
    TEST_ASSERT_TRUE(rl > 0);
    TEST_ASSERT_TRUE(pc_snmp_agent_process(req, rl, resp, sizeof(resp)) > 0);
}

// ---------------------------------------------------------------------------
// Registration / configuration edges
// ---------------------------------------------------------------------------

// A null or empty read-only community falls back to the built-in default ("public"),
// rather than leaving the agent with no community at all.
void test_init_community_defaults()
{
    uint8_t req[256], resp[256];
    RespView rv;
    const char *args[] = {NULL, ""};
    for (unsigned i = 0; i < 2; i++)
    {
        pc_snmp_agent_init(args[i]); // clears the MIB too, so re-register
        pc_snmp_agent_set_system(SYSDESCR_VAL, "admin", "esp32", "lab", 72);
        size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET,
                              60 + (long)i, 0, 0, OID_SYSDESCR, 9, NULL);
        size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
        TEST_ASSERT_TRUE(n > 0); // "public" is accepted in both cases
        TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
        TEST_ASSERT_EQUAL_STRING(SYSDESCR_VAL, rv.str);
    }
}

// An empty rw community clears write access exactly as a null one does.
void test_empty_rw_community_clears_write()
{
    pc_snmp_agent_set_rw_community("");
    uint8_t req[256], resp[256];
    SnmpValue sv;
    memset(&sv, 0, sizeof(sv));
    sv.type = (uint8_t)BER_INTEGER;
    sv.ival = 5;
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_SET, 61, 0,
                          0, OID_RW, 9, &sv);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ACCESS, rv.err_status);
    TEST_ASSERT_FALSE(g_set_called);
}

// A string object registered with a null value reads back as a zero-length OCTET STRING
// (not a crash and not a stale pointer).
void test_add_string_null_value()
{
    static const uint32_t OID_NULLSTR[] = {1, 3, 6, 1, 4, 1, 49374, 6, 0};
    TEST_ASSERT_TRUE(pc_snmp_agent_add_string(OID_NULLSTR, 9, NULL, NULL));
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 62, 0,
                          0, OID_NULLSTR, 9, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)BER_OCTET_STRING, rv.val_tag);
    TEST_ASSERT_EQUAL_size_t(0, rv.str_len);
}

// Registration is bounded on both axes: an OID with more arcs than SNMP_MAX_OID_LEN is
// refused, and once the table holds SNMP_MAX_MIB_ENTRIES objects every further add fails -
// including the ones pc_snmp_agent_set_system() makes internally.
void test_registration_table_limits()
{
    uint32_t toolong[SNMP_MAX_OID_LEN + 1];
    for (size_t i = 0; i < SNMP_MAX_OID_LEN + 1; i++)
    {
        toolong[i] = (uint32_t)(i + 1);
    }
    TEST_ASSERT_FALSE(pc_snmp_agent_add_integer(toolong, SNMP_MAX_OID_LEN + 1, 1, NULL));

    pc_snmp_agent_init("public"); // empty table
    uint32_t oid[] = {1, 3, 6, 1, 4, 1, 49374, 50, 0};
    for (size_t i = 0; i < SNMP_MAX_MIB_ENTRIES; i++)
    {
        oid[7] = (uint32_t)(50 + i);
        TEST_ASSERT_TRUE(pc_snmp_agent_add_integer(oid, 9, (long)i, NULL));
    }
    oid[7] = 200;
    TEST_ASSERT_FALSE(pc_snmp_agent_add_integer(oid, 9, 1, NULL)); // table full
    TEST_ASSERT_FALSE(pc_snmp_agent_add_string(oid, 9, "x", NULL));
    TEST_ASSERT_FALSE(pc_snmp_agent_add_dynamic(oid, 9, (uint8_t)SNMP_COUNTER32, ctr_getter));
    // set_system on a full table registers nothing (its sysObjectID mib_alloc returns null).
    pc_snmp_agent_set_system(SYSDESCR_VAL, "admin", "esp32", "lab", 72);
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 63, 0,
                          0, OID_SYSDESCR, 9, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_NO_SUCH_OBJECT, rv.val_tag); // sysDescr never got in
}

// mib_find_next returns the lexicographically smallest successor, not the first one it
// happens to meet: an object registered out of order (sorting before every other entry)
// still wins the GetNext.
void test_getnext_picks_smallest_out_of_order()
{
    static const uint32_t OID_EARLY[] = {1, 3, 6, 1, 2, 1, 1, 0, 0};          // sorts before sysDescr (.1.0)
    TEST_ASSERT_TRUE(pc_snmp_agent_add_integer(OID_EARLY, 9, 1234, NULL)); // registered last
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GETNEXT,
                          64, 0, 0, OID_SYSPREFIX, 7, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_UINT(9, rv.oid_len);
    TEST_ASSERT_EQUAL_UINT32(0u, rv.oid[7]); // the late-registered .0.0, not sysDescr .1.0
    TEST_ASSERT_EQUAL_INT(1234, rv.ival);
}

// ---------------------------------------------------------------------------
// v1 error-status variants (v2c counterparts are covered above)
// ---------------------------------------------------------------------------

// v1 has no notWritable/wrongType/noAccess: the same three Set failures report the
// classic readOnly / badValue / noSuchName instead.
void test_set_v1_error_variants()
{
    uint8_t req[256], resp[256];
    RespView rv;

    SnmpValue iv;
    memset(&iv, 0, sizeof(iv));
    iv.type = (uint8_t)BER_INTEGER;
    iv.ival = 1;
    // Writable community, but the object has no setter -> readOnly (v2c would say notWritable).
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V1, "private", (uint8_t)SNMP_PDU_SET, 70, 0,
                          0, OID_RO, 9, &iv);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_READ_ONLY, rv.err_status);

    // Setter rejects the value type -> badValue (v2c would say wrongType).
    SnmpValue sv;
    memset(&sv, 0, sizeof(sv));
    sv.type = (uint8_t)BER_OCTET_STRING;
    sv.str = "hi";
    sv.str_len = 2;
    rl = build_req(req, sizeof(req), (int)SNMP_V1, "private", (uint8_t)SNMP_PDU_SET, 71, 0, 0,
                   OID_RW, 9, &sv);
    n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_BAD_VALUE, rv.err_status);

    // Read-only community -> noSuchName (v2c would say noAccess).
    rl = build_req(req, sizeof(req), (int)SNMP_V1, "public", (uint8_t)SNMP_PDU_SET, 72, 0, 0,
                   OID_RW, 9, &iv);
    n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_SUCH_NAME, rv.err_status);
    TEST_ASSERT_EQUAL_INT(1, rv.err_index);
}

// ---------------------------------------------------------------------------
// Dispatch / MIB lookup edges
// ---------------------------------------------------------------------------

static bool failing_getter(SnmpValue *out)
{
    (void)out;
    return false; // the instance exists but has no readable value right now
}

// A registered object whose getter declines reports noSuchInstance (the object is known,
// only this instance's value is unavailable) and echoes the *registered* OID.
void test_get_failing_getter_is_nosuchinstance()
{
    static const uint32_t OID_FAIL[] = {1, 3, 6, 1, 4, 1, 49374, 7, 0};
    TEST_ASSERT_TRUE(pc_snmp_agent_add_dynamic(OID_FAIL, 9, (uint8_t)SNMP_COUNTER32, failing_getter));
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 65, 0,
                          0, OID_FAIL, 9, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_NO_SUCH_INSTANCE, rv.val_tag);
    TEST_ASSERT_EQUAL_UINT(9, rv.oid_len);
    TEST_ASSERT_EQUAL_UINT32(7u, rv.oid[7]);
}

// A GET for an OID shorter than any registered object's type-prefix cannot name a known
// object, so it is noSuchObject rather than noSuchInstance.
void test_get_short_oid_is_nosuchobject()
{
    static const uint32_t OID_SHORT[] = {1, 3, 6};
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_PDU_GET, 66, 0,
                          0, OID_SHORT, 3, NULL);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_NO_SUCH_OBJECT, rv.val_tag);
}

// Build a GetBulk request carrying @p nvb identical repeater varbinds (build_req emits only one).
static size_t build_getbulk_multi(uint8_t *buf, size_t cap, long reqid, long non_rep, long max_rep, const uint32_t *oid,
                                  size_t oidn, size_t nvb)
{
    BerEnc e;
    pc_ber_enc_init(&e, buf, cap);
    size_t msg = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
    pc_ber_put_integer(&e, (int)SNMP_V2C);
    pc_ber_put_octet_string(&e, (uint8_t)BER_OCTET_STRING, (const uint8_t *)"public", 6);
    size_t pdus = pc_ber_seq_begin(&e, (uint8_t)SNMP_PDU_GETBULK);
    pc_ber_put_integer(&e, reqid);
    pc_ber_put_integer(&e, non_rep);
    pc_ber_put_integer(&e, max_rep);
    size_t vbl = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
    for (size_t i = 0; i < nvb; i++)
    {
        size_t vb = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
        pc_ber_put_oid(&e, oid, oidn);
        pc_ber_put_null(&e);
        pc_ber_seq_end(&e, vb);
    }
    pc_ber_seq_end(&e, vbl);
    pc_ber_seq_end(&e, pdus);
    pc_ber_seq_end(&e, msg);
    return e.ok ? e.len : 0;
}

// GetBulk is capped by the output varbind table: 3 repeaters x 10 repetitions would be 30
// varbinds, but the walk stops at SNMP_MAX_VARBINDS - both the repetition loop and the
// per-repeater loop have to honour the cap, mid-repetition.
void test_getbulk_saturates_varbind_table()
{
    uint8_t req[512], resp[2048];
    size_t rl = build_getbulk_multi(req, sizeof(req), 67, 0, 10, OID_SYSPREFIX, 7, 3);
    TEST_ASSERT_TRUE(rl > 0);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_UINT(SNMP_MAX_VARBINDS, rv.nvb); // saturated, not 30
}

// The PDU field reads fail closed one field at a time: a PDU truncated after the
// request-id, after error-status, after error-index, and one whose varbind list header is
// present but whose single varbind is a lone byte.
void test_dispatch_truncated_pdu_fields()
{
    uint8_t out[128];
    const uint8_t no_field2[] = {0xA0, 0x03, 0x02, 0x01, 0x01};
    const uint8_t no_field3[] = {0xA0, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00};
    const uint8_t no_vbl[] = {0xA0, 0x09, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00};
    // varbind list declares one byte of content; that byte is a bare tag with no length octet.
    const uint8_t stub_vb[] = {0xA0, 0x0C, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00, 0x30, 0x01, 0x30};
    const uint8_t *pdus[] = {no_field2, no_field3, no_vbl, stub_vb};
    const size_t lens[] = {sizeof(no_field2), sizeof(no_field3), sizeof(no_vbl), sizeof(stub_vb)};
    for (unsigned i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_size_t(0, pc_snmp_dispatch_pdu(pdus[i], lens[i], false, true, out, sizeof(out)));
    }
}

// A GET with an empty varbind list produces an empty response; if even that does not fit
// the caller's buffer the dispatcher returns 0 rather than re-encoding a tooBig it also
// cannot fit (the tooBig retry is only for a non-empty varbind list).
void test_dispatch_empty_varbind_list_tiny_buffer()
{
    uint8_t pdu[64];
    BerEnc e;
    pc_ber_enc_init(&e, pdu, sizeof(pdu));
    size_t p = pc_ber_seq_begin(&e, (uint8_t)SNMP_PDU_GET);
    pc_ber_put_integer(&e, 1);
    pc_ber_put_integer(&e, 0);
    pc_ber_put_integer(&e, 0);
    size_t vbl = pc_ber_seq_begin(&e, (uint8_t)BER_SEQUENCE);
    pc_ber_seq_end(&e, vbl); // no varbinds
    pc_ber_seq_end(&e, p);
    TEST_ASSERT_TRUE(e.ok);

    uint8_t big[128];
    TEST_ASSERT_TRUE(pc_snmp_dispatch_pdu(pdu, e.len, false, true, big, sizeof(big)) > 0);
    uint8_t tiny[8]; // too small even for the empty response header
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_dispatch_pdu(pdu, e.len, false, true, tiny, sizeof(tiny)));
}

// The message wrapper fails closed when the datagram ends inside the outer TLV header or
// straight after the version, before any community can be read.
void test_message_truncated_before_community()
{
    uint8_t resp[128];
    const uint8_t lone_tag[] = {0x30}; // SEQUENCE tag, no length octet
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_agent_process(lone_tag, sizeof(lone_tag), resp, sizeof(resp)));
    const uint8_t no_community[] = {0x30, 0x03, 0x02, 0x01, 0x01}; // v2c, then nothing
    TEST_ASSERT_EQUAL_size_t(0, pc_snmp_agent_process(no_community, sizeof(no_community), resp, sizeof(resp)));
}

// The UDP binding stays silent when the agent produces no response (an unparsable
// datagram), rather than sending an empty reply.
void test_udp_handler_drops_unanswerable()
{
    pc_udp_reset_listeners();
    pc_udp_capture_enable();
    pc_udp_capture_reset();
    pc_snmp_agent_begin_udp(161);
    const uint8_t junk[] = {0x02, 0x01, 0x00}; // not a message wrapper -> agent returns 0
    pc_udp_inject(161, "192.0.2.1", 40000, junk, sizeof(junk));
    TEST_ASSERT_EQUAL_size_t(0, pc_udp_captured_len()); // nothing sent
    pc_udp_reset_listeners();
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_init_community_defaults);
    RUN_TEST(test_empty_rw_community_clears_write);
    RUN_TEST(test_add_string_null_value);
    RUN_TEST(test_registration_table_limits);
    RUN_TEST(test_getnext_picks_smallest_out_of_order);
    RUN_TEST(test_set_v1_error_variants);
    RUN_TEST(test_get_failing_getter_is_nosuchinstance);
    RUN_TEST(test_get_short_oid_is_nosuchobject);
    RUN_TEST(test_getbulk_saturates_varbind_table);
    RUN_TEST(test_dispatch_truncated_pdu_fields);
    RUN_TEST(test_dispatch_empty_varbind_list_tiny_buffer);
    RUN_TEST(test_message_truncated_before_community);
    RUN_TEST(test_udp_handler_drops_unanswerable);
    RUN_TEST(test_registration_and_rw_edges);
    RUN_TEST(test_ipaddress_value_encodes);
    RUN_TEST(test_set_wrong_type_and_unknown);
    RUN_TEST(test_getbulk_variants);
    RUN_TEST(test_dispatch_value_types_and_malformed);
    RUN_TEST(test_get_string_v2c);
    RUN_TEST(test_get_unknown_v2c_exception);
    RUN_TEST(test_get_bad_instance_v2c_nosuchinstance);
    RUN_TEST(test_get_unknown_v1_error);
    RUN_TEST(test_getnext_walks_to_first);
    RUN_TEST(test_getnext_past_end_endofmibview);
    RUN_TEST(test_set_without_rw_community_denied);
    RUN_TEST(test_set_with_rw_community_invokes_setter);
    RUN_TEST(test_set_readonly_not_writable);
    RUN_TEST(test_getbulk_returns_multiple);
    RUN_TEST(test_dynamic_counter_value);
    RUN_TEST(test_uptime_is_timeticks);
    RUN_TEST(test_unknown_community_no_response);
    RUN_TEST(test_v3_message_dropped);
    RUN_TEST(test_getbulk_repeaters_and_end);
    RUN_TEST(test_getbulk_nonrep_clamp_and_v1_reject);
    RUN_TEST(test_response_too_big_reencodes);
    RUN_TEST(test_version_and_community_guards);
    RUN_TEST(test_dispatch_malformed_pdu);
    RUN_TEST(test_udp_handler_via_inject);
    RUN_TEST(test_malformed_message_guards);
    RUN_TEST(test_snmp_dispatch_varbind_guards);
    RUN_TEST(test_snmp_oid_cmp_request_longer);
    return UNITY_END();
}
