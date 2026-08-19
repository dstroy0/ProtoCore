// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/udp/server/server.h"
#include "network_drivers/transport/udp/udp.h"
#include "services/net/snmp/snmp_agent/snmp_agent.h"
#include "services/net/snmp/snmp_ber/snmp_ber.h"
#include <string.h>

#include <unity.h>

static uint8_t snmp_ber_work[16]; // the borrow an entry takes; SnmpBer never reads it

// The PDU dispatcher, reached through the agent's namespace.
static size_t snmp_dispatch_pdu(const uint8_t *req, size_t req_len, proto_bool allow_write, proto_bool v2c,
                                uint8_t *out, size_t out_cap)
{
    SnmpAgent.pdu.req = req;
    SnmpAgent.pdu.req_len = req_len;
    SnmpAgent.pdu.allow_write = allow_write;
    SnmpAgent.pdu.v2c = v2c;
    SnmpAgent.pdu.out = out;
    SnmpAgent.pdu.out_cap = out_cap;
    SnmpAgent.dispatch_pdu(protocore_snmp_agent_span());
    return SnmpAgent.n;
}

// The BER codec, reached through its namespace. The cursor stays the caller's: several encodings
// are open at once here, so each call names the one it acts on.
static void ber_enc_init(BerEnc *e, uint8_t *buf, size_t cap)
{
    SnmpBer.enc = e;
    SnmpBer.buf.out = buf;
    SnmpBer.buf.cap = cap;
    SnmpBer.enc_init(snmp_ber_work);
}

static void ber_dec_init(BerDec *d, const uint8_t *buf, size_t len)
{
    SnmpBer.dec = d;
    SnmpBer.buf.in = buf;
    SnmpBer.buf.cap = len;
    SnmpBer.dec_init(snmp_ber_work);
}

static size_t ber_seq_begin(BerEnc *e, uint8_t tag)
{
    SnmpBer.enc = e;
    SnmpBer.tlv.tag = tag;
    SnmpBer.seq_begin(snmp_ber_work);
    return SnmpBer.tlv.token;
}

static void ber_seq_end(BerEnc *e, size_t token)
{
    SnmpBer.enc = e;
    SnmpBer.tlv.token = token;
    SnmpBer.seq_end(snmp_ber_work);
}

static void ber_put_octet_string(BerEnc *e, uint8_t tag, const uint8_t *bytes, size_t len)
{
    SnmpBer.enc = e;
    SnmpBer.tlv.tag = tag;
    SnmpBer.tlv.bytes = bytes;
    SnmpBer.tlv.len = len;
    SnmpBer.put_octet_string(snmp_ber_work);
}

static void ber_put_null(BerEnc *e)
{
    SnmpBer.enc = e;
    SnmpBer.put_null(snmp_ber_work);
}

static void ber_put_oid(BerEnc *e, const uint32_t *arcs, size_t arc_count)
{
    SnmpBer.enc = e;
    SnmpBer.tlv.arcs = arcs;
    SnmpBer.tlv.arc_count = arc_count;
    SnmpBer.put_oid(snmp_ber_work);
}

static void ber_put_raw(BerEnc *e, const uint8_t *bytes, size_t len)
{
    SnmpBer.enc = e;
    SnmpBer.tlv.bytes = bytes;
    SnmpBer.tlv.len = len;
    SnmpBer.put_raw(snmp_ber_work);
}

static void ber_put_uint(BerEnc *e, uint8_t tag, uint32_t uval)
{
    SnmpBer.enc = e;
    SnmpBer.tlv.tag = tag;
    SnmpBer.tlv.uval = uval;
    SnmpBer.put_uint(snmp_ber_work);
}

static void ber_put_integer(BerEnc *e, long ival)
{
    SnmpBer.enc = e;
    SnmpBer.tlv.ival = ival;
    SnmpBer.put_integer(snmp_ber_work);
}

static proto_bool ber_read_header(BerDec *d, uint8_t *tag, size_t *vlen)
{
    SnmpBer.dec = d;
    SnmpBer.read_header(snmp_ber_work);
    if (tag)
    {
        *tag = SnmpBer.tag;
    }
    if (vlen)
    {
        *vlen = SnmpBer.vlen;
    }
    return SnmpBer.ok;
}

static proto_bool ber_read_integer(BerDec *d, long *out)
{
    SnmpBer.dec = d;
    SnmpBer.read_integer(snmp_ber_work);
    if (out)
    {
        *out = SnmpBer.ival;
    }
    return SnmpBer.ok;
}

static proto_bool ber_read_oid(BerDec *d, uint32_t *arc_out, size_t arc_cap, size_t *count)
{
    SnmpBer.dec = d;
    SnmpBer.read_args.arc_out = arc_out;
    SnmpBer.read_args.arc_cap = arc_cap;
    SnmpBer.read_oid(snmp_ber_work);
    if (count)
    {
        *count = SnmpBer.n;
    }
    return SnmpBer.ok;
}

static proto_bool ber_skip(BerDec *d, size_t len)
{
    SnmpBer.dec = d;
    SnmpBer.read_args.skip = len;
    SnmpBer.skip(snmp_ber_work);
    return SnmpBer.ok;
}

// The SNMP agent, reached through its namespace: set the members a call takes, invoke it, read the
// outcome off the same handle.
static void snmp_init(const char *ro)
{
    SnmpAgent.community.ro = ro;
    SnmpAgent.init(protocore_snmp_agent_span());
}

static void snmp_set_rw_community(const char *rw)
{
    SnmpAgent.community.rw = rw;
    SnmpAgent.set_rw_community(protocore_snmp_agent_span());
}

static void snmp_set_system(const char *descr, const char *contact, const char *name, const char *location,
                            long services)
{
    SnmpAgent.system.descr = descr;
    SnmpAgent.system.contact = contact;
    SnmpAgent.system.name = name;
    SnmpAgent.system.location = location;
    SnmpAgent.system.services = services;
    SnmpAgent.set_system(protocore_snmp_agent_span());
}

static proto_bool snmp_add_integer(const uint32_t *oid, size_t oid_len, long ival, SnmpSetFn setter)
{
    SnmpAgent.object.oid = oid;
    SnmpAgent.object.oid_len = oid_len;
    SnmpAgent.object.ival = ival;
    SnmpAgent.object.setter = setter;
    SnmpAgent.add_integer(protocore_snmp_agent_span());
    return SnmpAgent.ok;
}

static proto_bool snmp_add_string(const uint32_t *oid, size_t oid_len, const char *text, SnmpSetFn setter)
{
    SnmpAgent.object.oid = oid;
    SnmpAgent.object.oid_len = oid_len;
    SnmpAgent.object.text = text;
    SnmpAgent.object.setter = setter;
    SnmpAgent.add_string(protocore_snmp_agent_span());
    return SnmpAgent.ok;
}

static proto_bool snmp_add_dynamic(const uint32_t *oid, size_t oid_len, uint8_t type, SnmpGetFn getter)
{
    SnmpAgent.object.oid = oid;
    SnmpAgent.object.oid_len = oid_len;
    SnmpAgent.object.type = type;
    SnmpAgent.object.getter = getter;
    SnmpAgent.object.setter = NULL;
    SnmpAgent.add_dynamic(protocore_snmp_agent_span());
    return SnmpAgent.ok;
}

static proto_bool snmp_listen(uint16_t port)
{
    SnmpAgent.port = port;
    SnmpAgent.listen(protocore_snmp_agent_span());
    return SnmpAgent.ok;
}

static size_t snmp_process(const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap)
{
    SnmpAgent.msg.req = req;
    SnmpAgent.msg.req_len = req_len;
    SnmpAgent.msg.resp = resp;
    SnmpAgent.msg.resp_cap = resp_cap;
    SnmpAgent.process(protocore_snmp_agent_span());
    return SnmpAgent.n;
}

static const uint32_t OID_SYSDESCR[] = {1, 3, 6, 1, 2, 1, 1, 1, 0};
static const uint32_t OID_SYSUPTIME[] = {1, 3, 6, 1, 2, 1, 1, 3, 0};
static const uint32_t OID_SYSPREFIX[] = {1, 3, 6, 1, 2, 1, 1};
static const uint32_t OID_RW[] = {1, 3, 6, 1, 4, 1, 49374, 1, 0};
static const uint32_t OID_RO[] = {1, 3, 6, 1, 4, 1, 49374, 2, 0};
static const uint32_t OID_CTR[] = {1, 3, 6, 1, 4, 1, 49374, 3, 0};
static const uint32_t OID_PAST_END[] = {1, 3, 6, 1, 4, 1, 49374, 99, 0};
static const uint32_t OID_UNKNOWN[] = {1, 3, 6, 1, 2, 1, 99, 0};

static const uint32_t OID_SYSDESCR_BADINST[] = {1, 3, 6, 1, 2, 1, 1, 1, 5};

static const char *SYSDESCR_VAL = "PC test agent";

static proto_bool g_set_called = PROTO_FALSE;
static long g_set_value = 0;

static proto_bool rw_setter(const SnmpValue *in)
{
    g_set_called = PROTO_TRUE;
    if (in->type != (uint8_t)SNMP_TAG_BER_INTEGER)
    {
        return PROTO_FALSE;
    }
    g_set_value = in->ival;
    return PROTO_TRUE;
}

static proto_bool ctr_getter(SnmpValue *out)
{
    out->type = (uint8_t)SNMP_TAG_SNMP_COUNTER32;
    out->uval = 12345u;
    return PROTO_TRUE;
}

void setUp()
{
    snmp_init("public");
    snmp_set_rw_community("private");
    snmp_set_system(SYSDESCR_VAL, "admin", "esp32", "lab", 72);
    snmp_add_integer(OID_RW, 9, 42, rw_setter);
    snmp_add_integer(OID_RO, 9, 7, NULL);
    snmp_add_dynamic(OID_CTR, 9, (uint8_t)SNMP_TAG_SNMP_COUNTER32, ctr_getter);
    g_set_called = PROTO_FALSE;
    g_set_value = 0;
}

void tearDown()
{
}

static size_t build_req(uint8_t *buf, size_t cap, long version, const char *comm, uint8_t pdu, long reqid, long f2,
                        long f3, const uint32_t *oid, size_t oidn, const SnmpValue *setval)
{
    BerEnc e;
    ber_enc_init(&e, buf, cap);
    size_t msg = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = version;
    SnmpBer.put_integer(snmp_ber_work);
    ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)comm, strlen(comm));
    size_t pdus = ber_seq_begin(&e, pdu);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = reqid;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = f2;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = f3;
    SnmpBer.put_integer(snmp_ber_work);
    size_t vbl = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    size_t vb = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    ber_put_oid(&e, oid, oidn);
    if (setval && setval->type == (uint8_t)SNMP_TAG_BER_INTEGER)
    {
        SnmpBer.enc = &e;
        SnmpBer.tlv.ival = setval->ival;
        SnmpBer.put_integer(snmp_ber_work);
    }
    else if (setval && setval->type == (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)setval->str, setval->str_len);
    }
    else
    {
        ber_put_null(&e);
    }
    ber_seq_end(&e, vb);
    ber_seq_end(&e, vbl);
    ber_seq_end(&e, pdus);
    ber_seq_end(&e, msg);
    return e.ok ? e.len : 0;
}

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
    ber_enc_init(&e, buf, cap);
    size_t pdus = ber_seq_begin(&e, (uint8_t)SNMP_TAG_SNMP_PDU_GET);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 42;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 0;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 0;
    SnmpBer.put_integer(snmp_ber_work);
    if (knob == VB_BAD_VBL_TAG)
    {
        ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)"x", 1);
    }
    else
    {
        size_t vbl = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
        if (knob == VB_TOO_MANY)
        {
            for (int i = 0; i <= SNMP_MAX_VARBINDS; i++)
            {
                size_t vb = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
                ber_put_oid(&e, OID_SYSDESCR, 9);
                ber_put_null(&e);
                ber_seq_end(&e, vb);
            }
        }
        else if (knob == VB_BAD_VB_TAG)
        {
            ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)"x", 1);
        }
        else if (knob == VB_BAD_OID)
        {
            size_t vb = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
            SnmpBer.enc = &e;
            SnmpBer.tlv.ival = 5;
            SnmpBer.put_integer(snmp_ber_work);
            ber_put_null(&e);
            ber_seq_end(&e, vb);
        }
        else if (knob == VB_BAD_VALUE)
        {
            size_t vb = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
            ber_put_oid(&e, OID_SYSDESCR, 9);
            uint8_t badv[2] = {(uint8_t)SNMP_TAG_BER_OCTET_STRING, 0x7F};
            ber_put_raw(&e, badv, sizeof(badv));
            ber_seq_end(&e, vb);
        }
        else if (knob == VB_OID_VALUE)
        {
            size_t vb = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
            ber_put_oid(&e, OID_SYSDESCR, 9);
            ber_put_oid(&e, OID_SYSUPTIME, 9);
            ber_seq_end(&e, vb);
        }
        else if (knob == VB_BAD_OID_VALUE)
        {
            size_t vb = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
            ber_put_oid(&e, OID_SYSDESCR, 9);
            uint8_t empty_oid[2] = {(uint8_t)SNMP_TAG_BER_OID, 0x00};
            ber_put_raw(&e, empty_oid, sizeof(empty_oid));
            ber_seq_end(&e, vb);
        }
        else
        {
            size_t vb = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
            ber_put_oid(&e, OID_SYSDESCR, 9);
            ber_put_null(&e);
            ber_seq_end(&e, vb);
        }
        ber_seq_end(&e, vbl);
    }
    ber_seq_end(&e, pdus);
    return e.ok ? e.len : 0;
}

typedef struct
{
    long version;
    uint8_t pdu_tag;
    long request_id;
    long err_status;
    long err_index;
    size_t nvb;
    uint32_t oid[SNMP_MAX_OID_LEN];
    size_t oid_len;
    uint8_t val_tag;
    uint8_t last_val_tag;
    long ival;
    uint32_t uval;
    char str[64];
    size_t str_len;
} RespView;

static proto_bool parse_resp(const uint8_t *buf, size_t len, RespView *rv)
{
    memset(rv, 0, sizeof(*rv));
    BerDec d;
    ber_dec_init(&d, buf, len);
    uint8_t tag;
    size_t l;
    if (!ber_read_header(&d, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    if (!ber_read_integer(&d, &rv->version))
    {
        return PROTO_FALSE;
    }
    uint8_t ctag;
    size_t cl;
    if (!ber_read_header(&d, &ctag, &cl))
    {
        return PROTO_FALSE;
    }
    d.pos += cl;
    if (!ber_read_header(&d, &rv->pdu_tag, &l))
    {
        return PROTO_FALSE;
    }
    if (!ber_read_integer(&d, &rv->request_id) || !ber_read_integer(&d, &rv->err_status) ||
        !ber_read_integer(&d, &rv->err_index))
    {
        return PROTO_FALSE;
    }
    uint8_t vt;
    size_t vl;
    if (!ber_read_header(&d, &vt, &vl) || vt != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    size_t vend = d.pos + vl;
    proto_bool first = PROTO_TRUE;
    while (d.pos < vend && d.ok)
    {
        uint8_t st;
        size_t sl;
        if (!ber_read_header(&d, &st, &sl) || st != (uint8_t)SNMP_TAG_BER_SEQUENCE)
        {
            return PROTO_FALSE;
        }
        size_t vbend = d.pos + sl;
        uint32_t oid[SNMP_MAX_OID_LEN];
        size_t on;
        if (!ber_read_oid(&d, oid, SNMP_MAX_OID_LEN, &on))
        {
            return PROTO_FALSE;
        }
        size_t save = d.pos;
        uint8_t valtag;
        size_t vallen;
        if (!ber_read_header(&d, &valtag, &vallen))
        {
            return PROTO_FALSE;
        }
        if (first)
        {
            memcpy(rv->oid, oid, on * sizeof(uint32_t));
            rv->oid_len = on;
            rv->val_tag = valtag;
            if (valtag == (uint8_t)SNMP_TAG_BER_INTEGER)
            {
                d.pos = save;
                ber_read_integer(&d, &rv->ival);
            }
            else if (valtag == (uint8_t)SNMP_TAG_BER_OCTET_STRING || valtag == (uint8_t)SNMP_TAG_SNMP_IPADDRESS ||
                     valtag == (uint8_t)SNMP_TAG_SNMP_OPAQUE)
            {
                size_t cpy = vallen < sizeof(rv->str) - 1 ? vallen : sizeof(rv->str) - 1;
                memcpy(rv->str, d.buf + d.pos, cpy);
                rv->str[cpy] = '\0';
                rv->str_len = vallen;
            }
            else if (valtag == (uint8_t)SNMP_TAG_SNMP_TIMETICKS || valtag == (uint8_t)SNMP_TAG_SNMP_COUNTER32 ||
                     valtag == (uint8_t)SNMP_TAG_SNMP_GAUGE32)
            {
                uint32_t a = 0;
                for (size_t i = 0; i < vallen; i++)
                {
                    a = (a << 8) | d.buf[d.pos + i];
                }
                rv->uval = a;
            }
            first = PROTO_FALSE;
        }
        d.pos = vbend;
        rv->nvb++;
    }
    return d.ok;
}

void test_get_string_v2c()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 111, 0, 0,
                          OID_SYSDESCR, 9, NULL);
    TEST_ASSERT_TRUE(rl > 0);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_RESPONSE, rv.pdu_tag);
    TEST_ASSERT_EQUAL_INT(111, rv.request_id);
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_OCTET_STRING, rv.val_tag);
    TEST_ASSERT_EQUAL_STRING(SYSDESCR_VAL, rv.str);
}

void test_get_unknown_v2c_exception()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 7, 0, 0,
                          OID_UNKNOWN, 8, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_NO_SUCH_OBJECT, rv.val_tag);
}

void test_get_bad_instance_v2c_nosuchinstance()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 8, 0, 0,
                          OID_SYSDESCR_BADINST, 9, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_NO_SUCH_INSTANCE, rv.val_tag);
}

void test_get_unknown_v1_error()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V1, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 7, 0, 0,
                          OID_UNKNOWN, 8, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_SUCH_NAME, rv.err_status);
    TEST_ASSERT_EQUAL_INT(1, rv.err_index);
}

void test_getnext_walks_to_first()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT, 5, 0, 0,
                          OID_SYSPREFIX, 7, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_OCTET_STRING, rv.val_tag);
    TEST_ASSERT_EQUAL_UINT(9, rv.oid_len);
    TEST_ASSERT_EQUAL_UINT32(1u, rv.oid[7]);
    TEST_ASSERT_EQUAL_STRING(SYSDESCR_VAL, rv.str);
}

void test_getnext_past_end_endofmibview()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT, 9, 0, 0,
                          OID_PAST_END, 9, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW, rv.val_tag);
}

void test_set_without_rw_community_denied()
{
    uint8_t req[256], resp[256];
    SnmpValue sv;
    memset(&sv, 0, sizeof(sv));
    sv.type = (uint8_t)SNMP_TAG_BER_INTEGER;
    sv.ival = 99;
    size_t rl =
        build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_SET, 3, 0, 0, OID_RW, 9, &sv);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
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
    sv.type = (uint8_t)SNMP_TAG_BER_INTEGER;
    sv.ival = 99;
    size_t rl =
        build_req(req, sizeof(req), (int)SNMP_V2C, "private", (uint8_t)SNMP_TAG_SNMP_PDU_SET, 3, 0, 0, OID_RW, 9, &sv);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
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
    sv.type = (uint8_t)SNMP_TAG_BER_INTEGER;
    sv.ival = 1;
    size_t rl =
        build_req(req, sizeof(req), (int)SNMP_V2C, "private", (uint8_t)SNMP_TAG_SNMP_PDU_SET, 3, 0, 0, OID_RO, 9, &sv);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NOT_WRITABLE, rv.err_status);
}

void test_getbulk_returns_multiple()
{
    uint8_t req[512], resp[512];

    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GETBULK, 1, 0, 3,
                          OID_SYSPREFIX, 7, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_UINT(3, rv.nvb);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_OCTET_STRING, rv.val_tag);
}

void test_dynamic_counter_value()
{
    uint8_t req[256], resp[256];
    size_t rl =
        build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 2, 0, 0, OID_CTR, 9, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_COUNTER32, rv.val_tag);
    TEST_ASSERT_EQUAL_UINT32(12345u, rv.uval);
}

void test_uptime_is_timeticks()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 2, 0, 0,
                          OID_SYSUPTIME, 9, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_TIMETICKS, rv.val_tag);
}

void test_unknown_community_no_response()
{
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "wrongcomm", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 1, 0, 0,
                          OID_SYSDESCR, 9, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT(0, n);
}

void test_v3_message_dropped()
{
    uint8_t req[64];
    BerEnc e;
    ber_enc_init(&e, req, sizeof(req));
    size_t msg = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = (int)SNMP_V3;
    SnmpBer.put_integer(snmp_ber_work);
    ber_seq_end(&e, msg);
    uint8_t resp[64];
    size_t n = snmp_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT(0, n);
}

static const uint32_t OID_IP[] = {1, 3, 6, 1, 4, 1, 49374, 4, 0};
static proto_bool ip_getter(SnmpValue *out)
{
    out->type = (uint8_t)SNMP_TAG_SNMP_IPADDRESS;
    out->uval = 0xC0A80101u;
    return PROTO_TRUE;
}

void test_registration_and_rw_edges()
{
    const uint32_t shortoid[] = {1};
    TEST_ASSERT_FALSE(snmp_add_string(shortoid, 1, "x", NULL));
    TEST_ASSERT_FALSE(snmp_add_integer(shortoid, 1, 5, NULL));
    TEST_ASSERT_FALSE(snmp_add_dynamic(shortoid, 1, (uint8_t)SNMP_TAG_BER_INTEGER, NULL));

    snmp_set_rw_community(NULL);
    uint8_t req[256], resp[256];
    SnmpValue sv;
    memset(&sv, 0, sizeof(sv));
    sv.type = (uint8_t)SNMP_TAG_BER_INTEGER;
    sv.ival = 1;
    size_t rl =
        build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_SET, 3, 0, 0, OID_RW, 9, &sv);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ACCESS, rv.err_status);
}

void test_ipaddress_value_encodes()
{
    TEST_ASSERT_TRUE(snmp_add_dynamic(OID_IP, 9, (uint8_t)SNMP_TAG_SNMP_IPADDRESS, ip_getter));
    uint8_t req[256], resp[256];
    size_t rl =
        build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 1, 0, 0, OID_IP, 9, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));

    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_IPADDRESS, rv.val_tag);
    TEST_ASSERT_EQUAL_size_t(4, rv.str_len);
    const uint8_t want[4] = {0xC0, 0xA8, 0x01, 0x01};
    TEST_ASSERT_EQUAL_MEMORY(want, rv.str, 4);
}

void test_set_wrong_type_and_unknown()
{
    uint8_t req[256], resp[256];
    RespView rv;
    SnmpValue s;
    memset(&s, 0, sizeof(s));
    s.type = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    s.str = "hi";
    s.str_len = 2;
    size_t rl =
        build_req(req, sizeof(req), (int)SNMP_V2C, "private", (uint8_t)SNMP_TAG_SNMP_PDU_SET, 3, 0, 0, OID_RW, 9, &s);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_WRONG_TYPE, rv.err_status);

    SnmpValue iv;
    memset(&iv, 0, sizeof(iv));
    iv.type = (uint8_t)SNMP_TAG_BER_INTEGER;
    iv.ival = 1;
    rl = build_req(req, sizeof(req), (int)SNMP_V2C, "private", (uint8_t)SNMP_TAG_SNMP_PDU_SET, 3, 0, 0, OID_UNKNOWN, 8,
                   &iv);
    n = snmp_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_SUCH_NAME, rv.err_status);
}

void test_getbulk_variants()
{
    uint8_t req[512], resp[512];
    RespView rv;

    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GETBULK, 1, 1, 2,
                          OID_SYSPREFIX, 7, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_GREATER_THAN(0, (int)rv.nvb);

    rl = build_req(req, sizeof(req), (int)SNMP_V1, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GETBULK, 1, 0, 2, OID_SYSPREFIX,
                   7, NULL);
    TEST_ASSERT_EQUAL_UINT(0, snmp_process(req, rl, resp, sizeof(resp)));

    rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GETBULK, 1, 0, 3, OID_PAST_END,
                   9, NULL);
    n = snmp_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW, rv.val_tag);
}

static size_t build_pdu_with_value(uint8_t *buf, size_t cap, uint8_t pdu_tag, int value_kind)
{
    BerEnc e;
    ber_enc_init(&e, buf, cap);
    size_t p = ber_seq_begin(&e, pdu_tag);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 1;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 0;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 0;
    SnmpBer.put_integer(snmp_ber_work);
    size_t vbl = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    size_t vb = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    ber_put_oid(&e, OID_SYSDESCR, 9);
    if (value_kind == 1)
    {
        ber_put_uint(&e, (uint8_t)SNMP_TAG_SNMP_GAUGE32, 500);
    }
    else if (value_kind == 2)
    {
        ber_put_oid(&e, OID_SYSUPTIME, 9);
    }
    else
    {
        ber_put_null(&e);
    }
    ber_seq_end(&e, vb);
    ber_seq_end(&e, vbl);
    ber_seq_end(&e, p);
    return e.ok ? e.len : 0;
}

void test_dispatch_value_types_and_malformed()
{
    uint8_t pdu[128], out[256];

    size_t g = build_pdu_with_value(pdu, sizeof(pdu), (uint8_t)SNMP_TAG_SNMP_PDU_GET, 1);
    TEST_ASSERT_TRUE(snmp_dispatch_pdu(pdu, g, PROTO_FALSE, PROTO_TRUE, out, sizeof(out)) > 0);
    size_t o = build_pdu_with_value(pdu, sizeof(pdu), (uint8_t)SNMP_TAG_SNMP_PDU_GET, 2);
    TEST_ASSERT_TRUE(snmp_dispatch_pdu(pdu, o, PROTO_FALSE, PROTO_TRUE, out, sizeof(out)) > 0);

    size_t t = build_pdu_with_value(pdu, sizeof(pdu), (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, 0);
    TEST_ASSERT_EQUAL_UINT(0, snmp_dispatch_pdu(pdu, t, PROTO_FALSE, PROTO_TRUE, out, sizeof(out)));

    size_t full = build_pdu_with_value(pdu, sizeof(pdu), (uint8_t)SNMP_TAG_SNMP_PDU_GET, 0);
    TEST_ASSERT_TRUE(snmp_dispatch_pdu(pdu, full, PROTO_FALSE, PROTO_TRUE, out, sizeof(out)) > 0);
    for (size_t l = 0; l < full; l++)
    {
        TEST_ASSERT_EQUAL_UINT(0, snmp_dispatch_pdu(pdu, l, PROTO_FALSE, PROTO_TRUE, out, sizeof(out)));
    }
}

void test_getbulk_repeaters_and_end()
{
    uint8_t req[256], resp[512];

    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GETBULK, 20, 0, 3,
                          OID_SYSPREFIX, 7, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_TRUE(rv.nvb >= 1);

    rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GETBULK, 21, 1, 2,
                   OID_PAST_END, 9, NULL);
    TEST_ASSERT_TRUE(snmp_process(req, rl, resp, sizeof(resp)) > 0);
}

void test_getbulk_nonrep_clamp_and_v1_reject()
{
    uint8_t req[256], resp[512];

    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GETBULK, 22, 5, 2,
                          OID_SYSPREFIX, 7, NULL);
    TEST_ASSERT_TRUE(snmp_process(req, rl, resp, sizeof(resp)) > 0);

    rl = build_req(req, sizeof(req), (int)SNMP_V1, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GETBULK, 23, 0, 3,
                   OID_SYSPREFIX, 7, NULL);
    TEST_ASSERT_EQUAL_size_t(0, snmp_process(req, rl, resp, sizeof(resp)));
}

void test_response_too_big_reencodes()
{
    uint8_t req[256], resp[28];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 40, 0, 0,
                          OID_SYSDESCR, 9, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
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

    size_t rl = build_req(req, sizeof(req), (int)SNMP_V3, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 1, 0, 0,
                          OID_SYSDESCR, 9, NULL);
    TEST_ASSERT_EQUAL_size_t(0, snmp_process(req, rl, resp, sizeof(resp)));

    rl = build_req(req, sizeof(req), 5, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 1, 0, 0, OID_SYSDESCR, 9, NULL);
    TEST_ASSERT_EQUAL_size_t(0, snmp_process(req, rl, resp, sizeof(resp)));

    rl = build_req(req, sizeof(req), (int)SNMP_V2C, "wrongcomm", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 1, 0, 0, OID_SYSDESCR,
                   9, NULL);
    TEST_ASSERT_EQUAL_size_t(0, snmp_process(req, rl, resp, sizeof(resp)));
}

void test_dispatch_malformed_pdu()
{
    uint8_t resp[128];

    uint8_t junk[3] = {0xA0, 0x01, 0x05};
    TEST_ASSERT_EQUAL_size_t(0, snmp_dispatch_pdu(junk, sizeof(junk), PROTO_FALSE, PROTO_TRUE, resp, sizeof(resp)));

    uint8_t bare[1] = {0xA0};
    TEST_ASSERT_EQUAL_size_t(0, snmp_dispatch_pdu(bare, sizeof(bare), PROTO_FALSE, PROTO_TRUE, resp, sizeof(resp)));
}

static void inject(uint16_t port, const char *src_ip, uint16_t src_port, const uint8_t *data, size_t len)
{
    protocore_net_host_udp_deliver(port, src_ip, src_port, (void *)(uintptr_t)data, (uint16_t)len);
    UdpListener.poll(protocore_udp_listener_span());
}

static void reset_udp(void)
{
    UdpListener.port = 161;
    UdpListener.close(protocore_udp_listener_span());
    (void)UdpListener.ok;
    protocore_net_host_udp_reset();
}

void test_udp_handler_via_inject()
{
    reset_udp();
    snmp_listen(161);
    uint8_t req[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 50, 0, 0,
                          OID_SYSDESCR, 9, NULL);
    inject(161, "192.0.2.1", 40000, req, rl);

    TEST_ASSERT_EQUAL_size_t(1, protocore_net_host_udp_count());
    TEST_ASSERT_TRUE(protocore_net_host_udp_at(0)->len > 0);
    TEST_ASSERT_EQUAL_UINT16(40000, protocore_net_host_udp_at(0)->dst_port);
    reset_udp();
}

void test_malformed_message_guards()
{
    uint8_t resp[128];
    uint8_t not_seq[3] = {0x02, 0x01, 0x00};
    TEST_ASSERT_EQUAL_size_t(0, snmp_process(not_seq, sizeof(not_seq), resp, sizeof(resp)));
    uint8_t empty_seq[2] = {0x30, 0x00};
    TEST_ASSERT_EQUAL_size_t(0, snmp_process(empty_seq, sizeof(empty_seq), resp, sizeof(resp)));
    uint8_t bad_comm[8] = {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00};
    TEST_ASSERT_EQUAL_size_t(0, snmp_process(bad_comm, sizeof(bad_comm), resp, sizeof(resp)));
}

void test_snmp_dispatch_varbind_guards()
{
    uint8_t pdu[512], resp[256];
    int reject[] = {VB_BAD_VBL_TAG, VB_TOO_MANY, VB_BAD_VB_TAG, VB_BAD_OID, VB_BAD_VALUE, VB_BAD_OID_VALUE};
    for (unsigned i = 0; i < sizeof(reject) / sizeof(reject[0]); i++)
    {
        size_t n = build_pdu(pdu, sizeof(pdu), reject[i]);
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_EQUAL_size_t(0, snmp_dispatch_pdu(pdu, n, PROTO_FALSE, PROTO_TRUE, resp, sizeof(resp)));
    }
    size_t n = build_pdu(pdu, sizeof(pdu), VB_OID_VALUE);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(snmp_dispatch_pdu(pdu, n, PROTO_FALSE, PROTO_TRUE, resp, sizeof(resp)) > 0);

    n = build_pdu(pdu, sizeof(pdu), VB_VALID);
    uint8_t tiny[24];
    TEST_ASSERT_TRUE(snmp_dispatch_pdu(pdu, n, PROTO_FALSE, PROTO_TRUE, tiny, sizeof(tiny)) > 0);
}

void test_snmp_oid_cmp_request_longer()
{
    uint8_t req[256], resp[256];
    static const uint32_t OID_LONGER[] = {1, 3, 6, 1, 2, 1, 1, 1, 0, 7};
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 1, 0, 0,
                          OID_LONGER, 10, NULL);
    TEST_ASSERT_TRUE(rl > 0);
    TEST_ASSERT_TRUE(snmp_process(req, rl, resp, sizeof(resp)) > 0);
}

void test_init_community_defaults()
{
    uint8_t req[256], resp[256];
    RespView rv;
    const char *args[] = {NULL, ""};
    for (unsigned i = 0; i < 2; i++)
    {
        snmp_init(args[i]);
        snmp_set_system(SYSDESCR_VAL, "admin", "esp32", "lab", 72);
        size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 60 + (long)i,
                              0, 0, OID_SYSDESCR, 9, NULL);
        size_t n = snmp_process(req, rl, resp, sizeof(resp));
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
        TEST_ASSERT_EQUAL_STRING(SYSDESCR_VAL, rv.str);
    }
}

void test_empty_rw_community_clears_write()
{
    snmp_set_rw_community("");
    uint8_t req[256], resp[256];
    SnmpValue sv;
    memset(&sv, 0, sizeof(sv));
    sv.type = (uint8_t)SNMP_TAG_BER_INTEGER;
    sv.ival = 5;
    size_t rl =
        build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_SET, 61, 0, 0, OID_RW, 9, &sv);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ACCESS, rv.err_status);
    TEST_ASSERT_FALSE(g_set_called);
}

void test_add_string_null_value()
{
    static const uint32_t OID_NULLSTR[] = {1, 3, 6, 1, 4, 1, 49374, 6, 0};
    TEST_ASSERT_TRUE(snmp_add_string(OID_NULLSTR, 9, NULL, NULL));
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 62, 0, 0,
                          OID_NULLSTR, 9, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_OCTET_STRING, rv.val_tag);
    TEST_ASSERT_EQUAL_size_t(0, rv.str_len);
}

void test_registration_table_limits()
{
    uint32_t toolong[SNMP_MAX_OID_LEN + 1];
    for (size_t i = 0; i < SNMP_MAX_OID_LEN + 1; i++)
    {
        toolong[i] = (uint32_t)(i + 1);
    }
    TEST_ASSERT_FALSE(snmp_add_integer(toolong, SNMP_MAX_OID_LEN + 1, 1, NULL));

    snmp_init("public");
    uint32_t oid[] = {1, 3, 6, 1, 4, 1, 49374, 50, 0};
    for (size_t i = 0; i < SNMP_MAX_MIB_ENTRIES; i++)
    {
        oid[7] = (uint32_t)(50 + i);
        TEST_ASSERT_TRUE(snmp_add_integer(oid, 9, (long)i, NULL));
    }
    oid[7] = 200;
    TEST_ASSERT_FALSE(snmp_add_integer(oid, 9, 1, NULL));
    TEST_ASSERT_FALSE(snmp_add_string(oid, 9, "x", NULL));
    TEST_ASSERT_FALSE(snmp_add_dynamic(oid, 9, (uint8_t)SNMP_TAG_SNMP_COUNTER32, ctr_getter));

    snmp_set_system(SYSDESCR_VAL, "admin", "esp32", "lab", 72);
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 63, 0, 0,
                          OID_SYSDESCR, 9, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_NO_SUCH_OBJECT, rv.val_tag);
}

void test_getnext_picks_smallest_out_of_order()
{
    static const uint32_t OID_EARLY[] = {1, 3, 6, 1, 2, 1, 1, 0, 0};
    TEST_ASSERT_TRUE(snmp_add_integer(OID_EARLY, 9, 1234, NULL));
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT, 64, 0, 0,
                          OID_SYSPREFIX, 7, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_UINT(9, rv.oid_len);
    TEST_ASSERT_EQUAL_UINT32(0u, rv.oid[7]);
    TEST_ASSERT_EQUAL_INT(1234, rv.ival);
}

void test_set_v1_error_variants()
{
    uint8_t req[256], resp[256];
    RespView rv;

    SnmpValue iv;
    memset(&iv, 0, sizeof(iv));
    iv.type = (uint8_t)SNMP_TAG_BER_INTEGER;
    iv.ival = 1;

    size_t rl =
        build_req(req, sizeof(req), (int)SNMP_V1, "private", (uint8_t)SNMP_TAG_SNMP_PDU_SET, 70, 0, 0, OID_RO, 9, &iv);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_READ_ONLY, rv.err_status);

    SnmpValue sv;
    memset(&sv, 0, sizeof(sv));
    sv.type = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    sv.str = "hi";
    sv.str_len = 2;
    rl = build_req(req, sizeof(req), (int)SNMP_V1, "private", (uint8_t)SNMP_TAG_SNMP_PDU_SET, 71, 0, 0, OID_RW, 9, &sv);
    n = snmp_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_BAD_VALUE, rv.err_status);

    rl = build_req(req, sizeof(req), (int)SNMP_V1, "public", (uint8_t)SNMP_TAG_SNMP_PDU_SET, 72, 0, 0, OID_RW, 9, &iv);
    n = snmp_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_SUCH_NAME, rv.err_status);
    TEST_ASSERT_EQUAL_INT(1, rv.err_index);
}

static proto_bool failing_getter(SnmpValue *out)
{
    (void)out;
    return PROTO_FALSE;
}

void test_get_failing_getter_is_nosuchinstance()
{
    static const uint32_t OID_FAIL[] = {1, 3, 6, 1, 4, 1, 49374, 7, 0};
    TEST_ASSERT_TRUE(snmp_add_dynamic(OID_FAIL, 9, (uint8_t)SNMP_TAG_SNMP_COUNTER32, failing_getter));
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 65, 0, 0, OID_FAIL,
                          9, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_NO_SUCH_INSTANCE, rv.val_tag);
    TEST_ASSERT_EQUAL_UINT(9, rv.oid_len);
    TEST_ASSERT_EQUAL_UINT32(7u, rv.oid[7]);
}

void test_get_short_oid_is_nosuchobject()
{
    static const uint32_t OID_SHORT[] = {1, 3, 6};
    uint8_t req[256], resp[256];
    size_t rl = build_req(req, sizeof(req), (int)SNMP_V2C, "public", (uint8_t)SNMP_TAG_SNMP_PDU_GET, 66, 0, 0,
                          OID_SHORT, 3, NULL);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_NO_SUCH_OBJECT, rv.val_tag);
}

static size_t build_getbulk_multi(uint8_t *buf, size_t cap, long reqid, long non_rep, long max_rep, const uint32_t *oid,
                                  size_t oidn, size_t nvb)
{
    BerEnc e;
    ber_enc_init(&e, buf, cap);
    size_t msg = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = (int)SNMP_V2C;
    SnmpBer.put_integer(snmp_ber_work);
    ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)"public", 6);
    size_t pdus = ber_seq_begin(&e, (uint8_t)SNMP_TAG_SNMP_PDU_GETBULK);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = reqid;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = non_rep;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = max_rep;
    SnmpBer.put_integer(snmp_ber_work);
    size_t vbl = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    for (size_t i = 0; i < nvb; i++)
    {
        size_t vb = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
        ber_put_oid(&e, oid, oidn);
        ber_put_null(&e);
        ber_seq_end(&e, vb);
    }
    ber_seq_end(&e, vbl);
    ber_seq_end(&e, pdus);
    ber_seq_end(&e, msg);
    return e.ok ? e.len : 0;
}

void test_getbulk_saturates_varbind_table()
{
    uint8_t req[512], resp[2048];
    size_t rl = build_getbulk_multi(req, sizeof(req), 67, 0, 10, OID_SYSPREFIX, 7, 3);
    TEST_ASSERT_TRUE(rl > 0);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    RespView rv;
    TEST_ASSERT_TRUE(parse_resp(resp, n, &rv));
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, rv.err_status);
    TEST_ASSERT_EQUAL_UINT(SNMP_MAX_VARBINDS, rv.nvb);
}

void test_dispatch_truncated_pdu_fields()
{
    uint8_t out[128];
    const uint8_t no_field2[] = {0xA0, 0x03, 0x02, 0x01, 0x01};
    const uint8_t no_field3[] = {0xA0, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00};
    const uint8_t no_vbl[] = {0xA0, 0x09, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00};

    const uint8_t stub_vb[] = {0xA0, 0x0C, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00, 0x30, 0x01, 0x30};
    const uint8_t *pdus[] = {no_field2, no_field3, no_vbl, stub_vb};
    const size_t lens[] = {sizeof(no_field2), sizeof(no_field3), sizeof(no_vbl), sizeof(stub_vb)};
    for (unsigned i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_size_t(0, snmp_dispatch_pdu(pdus[i], lens[i], PROTO_FALSE, PROTO_TRUE, out, sizeof(out)));
    }
}

void test_dispatch_empty_varbind_list_tiny_buffer()
{
    uint8_t pdu[64];
    BerEnc e;
    ber_enc_init(&e, pdu, sizeof(pdu));
    size_t p = ber_seq_begin(&e, (uint8_t)SNMP_TAG_SNMP_PDU_GET);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 1;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 0;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 0;
    SnmpBer.put_integer(snmp_ber_work);
    size_t vbl = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    ber_seq_end(&e, vbl);
    ber_seq_end(&e, p);
    TEST_ASSERT_TRUE(e.ok);

    uint8_t big[128];
    TEST_ASSERT_TRUE(snmp_dispatch_pdu(pdu, e.len, PROTO_FALSE, PROTO_TRUE, big, sizeof(big)) > 0);
    uint8_t tiny[8];
    TEST_ASSERT_EQUAL_size_t(0, snmp_dispatch_pdu(pdu, e.len, PROTO_FALSE, PROTO_TRUE, tiny, sizeof(tiny)));
}

void test_message_truncated_before_community()
{
    uint8_t resp[128];
    const uint8_t lone_tag[] = {0x30};
    TEST_ASSERT_EQUAL_size_t(0, snmp_process(lone_tag, sizeof(lone_tag), resp, sizeof(resp)));
    const uint8_t no_community[] = {0x30, 0x03, 0x02, 0x01, 0x01};
    TEST_ASSERT_EQUAL_size_t(0, snmp_process(no_community, sizeof(no_community), resp, sizeof(resp)));
}

void test_udp_handler_drops_unanswerable()
{
    reset_udp();
    snmp_listen(161);
    const uint8_t junk[] = {0x02, 0x01, 0x00};
    inject(161, "192.0.2.1", 40000, junk, sizeof(junk));
    TEST_ASSERT_EQUAL_size_t(0, protocore_net_host_udp_count());
    reset_udp();
}

