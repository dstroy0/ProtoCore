// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/mac/hmac_sha256.h"
#include "network_drivers/transport/udp/udp.h"
#include "services/net/snmp/snmp_agent.h"
#include "services/net/snmp/snmp_ber.h"
#include "services/net/snmp/snmp_crypto.h"
#include "services/net/snmp/snmp_notify.h"
#include "services/net/snmp/snmp_v3.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>

static uint8_t tw[4096];

static const uint8_t *udp_cap(void)
{
    size_t n = protocore_net_host_udp_count();
    return n ? protocore_net_host_udp_at(n - 1)->data : NULL;
}
static size_t udp_cap_len(void)
{
    size_t n = protocore_net_host_udp_count();
    return n ? protocore_net_host_udp_at(n - 1)->len : 0;
}

static const uint32_t OID_SYSDESCR[] = {1, 3, 6, 1, 2, 1, 1, 1, 0};
static const char *SYSDESCR_VAL = "PC v3 agent";

void setUp()
{
    protocore_snmp_agent_init("public");
    protocore_snmp_agent_set_system(SYSDESCR_VAL, "admin", "esp32", "lab", 72);
    protocore_snmp_v3_init(NULL, 0);
    protocore_snmp_v3_set_boots(1);
    protocore_snmp_v3_set_user("myuser", "authpass12", "privpass12");
}
void tearDown()
{
}

void test_localize_key_sha256_vector()
{

    const uint8_t engine[] = {0x80, 0x00, 0xC0, 0xDE, 0x05, 0x01, 0x02, 0x03, 0x04};
    const uint8_t expect[32] = {0xb8, 0xa5, 0x56, 0xfc, 0xb1, 0xcd, 0xee, 0x18, 0x79, 0x22, 0x24,
                                0x79, 0x69, 0xaf, 0xe3, 0x90, 0x37, 0x34, 0x07, 0xd6, 0x52, 0x9a,
                                0x98, 0x08, 0x41, 0x8a, 0xef, 0x8e, 0xb0, 0xbf, 0x45, 0x86};
    uint8_t key[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "maplesyrup", engine, sizeof(engine), key);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, key, 32);
}

void test_localize_key_empty_password()
{
    const uint8_t engine[] = {0x80, 0x00, 0xC0, 0xDE, 0x05, 0x01, 0x02, 0x03, 0x04};
    const uint8_t zero[SNMP_USM_KEY_LEN] = {0};
    uint8_t key[SNMP_USM_KEY_LEN];

    memset(key, 0xAA, sizeof(key));
    protocore_snmp_usm_localize_key(tw, "", engine, sizeof(engine), key);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, key, SNMP_USM_KEY_LEN);

    memset(key, 0xBB, sizeof(key));
    protocore_snmp_usm_localize_key(tw, NULL, engine, sizeof(engine), key);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, key, SNMP_USM_KEY_LEN);
}

void test_aes128_fips197_vector()
{

    const uint8_t key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                             0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    const uint8_t pt[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                            0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    const uint8_t expect[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    uint8_t zero[16] = {0};
    uint8_t out[16];
    protocore_snmp_aes128_cfb(key, pt, zero, out, 16, PROTO_TRUE);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 16);
}

void test_aes_cfb_roundtrip_partial_block()
{
    const uint8_t key[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint8_t iv[16] = {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    const char *msg = "SNMPv3 scopedPDU spanning more than one AES block plus a tail.";
    size_t n = strlen(msg);
    uint8_t ct[128], pt[128];
    protocore_snmp_aes128_cfb(key, iv, (const uint8_t *)msg, ct, n, PROTO_TRUE);
    protocore_snmp_aes128_cfb(key, iv, ct, pt, n, PROTO_FALSE);
    TEST_ASSERT_EQUAL_MEMORY(msg, pt, n);
    TEST_ASSERT_TRUE(memcmp(msg, ct, n) != 0);
}

static uint8_t g_dec[1024];

typedef struct
{
    uint8_t engine_id[64];
    size_t engine_id_len;
    long boots, time;
    uint8_t flags;
    uint8_t pdu_tag;
    long request_id, err_status;
    uint32_t oid[SNMP_MAX_OID_LEN];
    size_t oid_len;
    uint8_t val_tag;
    char str[64];
    size_t str_len;
} V3View;

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static size_t build_get(uint8_t *out, size_t cap, proto_bool auth, proto_bool priv, const uint8_t *eid, size_t eid_len,
                        long boots, long time, const char *user, const uint8_t *authkey, const uint8_t *privkey,
                        long msg_id, long req_id, const uint32_t *oid, size_t oid_len)
{

    uint8_t pdu[256];
    BerEnc pe;
    protocore_ber_enc_init(&pe, pdu, sizeof(pdu));
    size_t pp = protocore_ber_seq_begin(&pe, (uint8_t)SNMP_TAG_SNMP_PDU_GET);
    SnmpBer.enc = &pe;
    SnmpBer.tlv.ival = req_id;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.enc = &pe;
    SnmpBer.tlv.ival = 0;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.enc = &pe;
    SnmpBer.tlv.ival = 0;
    SnmpBer.put_integer(SnmpBer.internal);
    size_t vbl = protocore_ber_seq_begin(&pe, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    size_t vb = protocore_ber_seq_begin(&pe, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    protocore_ber_put_oid(&pe, oid, oid_len);
    protocore_ber_put_null(&pe);
    protocore_ber_seq_end(&pe, vb);
    protocore_ber_seq_end(&pe, vbl);
    protocore_ber_seq_end(&pe, pp);

    uint8_t scoped[320];
    BerEnc se;
    protocore_ber_enc_init(&se, scoped, sizeof(scoped));
    size_t ss = protocore_ber_seq_begin(&se, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    protocore_ber_put_octet_string(&se, (uint8_t)SNMP_TAG_BER_OCTET_STRING, eid, eid_len);
    protocore_ber_put_octet_string(&se, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    protocore_ber_put_raw(&se, pdu, pe.len);
    protocore_ber_seq_end(&se, ss);

    uint8_t salt[8] = {0, 0, 0, 0, 0, 0, 0, 7};
    uint8_t cipher[320];
    const uint8_t *data = scoped;
    size_t data_len = se.len;
    if (priv)
    {
        uint8_t iv[16];
        put_be32(iv, (uint32_t)boots);
        put_be32(iv + 4, (uint32_t)time);
        memcpy(iv + 8, salt, 8);
        protocore_snmp_aes128_cfb(privkey, iv, scoped, cipher, se.len, PROTO_TRUE);
        data = cipher;
    }

    uint8_t secp[128];
    BerEnc se2;
    protocore_ber_enc_init(&se2, secp, sizeof(secp));
    size_t s2 = protocore_ber_seq_begin(&se2, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    protocore_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, eid, eid_len);
    SnmpBer.enc = &se2;
    SnmpBer.tlv.ival = boots;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.enc = &se2;
    SnmpBer.tlv.ival = time;
    SnmpBer.put_integer(SnmpBer.internal);
    protocore_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)user, strlen(user));
    size_t auth_off = 0;
    if (auth)
    {
        auth_off = se2.len + 2;
        uint8_t z[SNMP_V3_AUTH_PARAM_LEN] = {0};
        protocore_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, z, SNMP_V3_AUTH_PARAM_LEN);
    }
    else
    {
        protocore_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (priv)
    {
        protocore_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, salt, 8);
    }
    else
    {
        protocore_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    protocore_ber_seq_end(&se2, s2);

    BerEnc e;
    protocore_ber_enc_init(&e, out, cap);
    size_t msg = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = (int)SNMP_V3;
    SnmpBer.put_integer(SnmpBer.internal);
    size_t hdr = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = msg_id;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 65507;
    SnmpBer.put_integer(SnmpBer.internal);
    uint8_t fl = (uint8_t)((auth ? 1 : 0) | (priv ? 2 : 0) | (auth ? 0 : 4));
    protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &fl, 1);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 3;
    SnmpBer.put_integer(SnmpBer.internal);
    protocore_ber_seq_end(&e, hdr);
    protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, secp, se2.len);
    size_t sec_value_pos = e.len - se2.len;
    if (priv)
    {
        protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, data, data_len);
    }
    else
    {
        protocore_ber_put_raw(&e, data, data_len);
    }
    protocore_ber_seq_end(&e, msg);

    if (auth)
    {
        uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
        protocore_hmac_sha256(tw, authkey, SNMP_USM_KEY_LEN, out, e.len, mac);
        memcpy(out + sec_value_pos + auth_off, mac, SNMP_V3_AUTH_PARAM_LEN);
    }
    return e.ok ? e.len : 0;
}

static proto_bool parse_v3(const uint8_t *buf, size_t len, const uint8_t *privkey, V3View *v)
{
    memset(v, 0, sizeof(*v));
    BerDec d;
    protocore_ber_dec_init(&d, buf, len);
    uint8_t tag;
    size_t l;
    if (!protocore_ber_read_header(&d, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    long version;
    if (!protocore_ber_read_integer(&d, &version))
    {
        return PROTO_FALSE;
    }
    if (!protocore_ber_read_header(&d, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    long msgid, maxsize, secmodel;
    if (!protocore_ber_read_integer(&d, &msgid) || !protocore_ber_read_integer(&d, &maxsize))
    {
        return PROTO_FALSE;
    }
    size_t fl;
    if (!protocore_ber_read_header(&d, &tag, &fl) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    v->flags = d.buf[d.pos];
    d.pos += fl;
    if (!protocore_ber_read_integer(&d, &secmodel))
    {
        return PROTO_FALSE;
    }

    size_t seclen;
    if (!protocore_ber_read_header(&d, &tag, &seclen) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    BerDec sd;
    protocore_ber_dec_init(&sd, d.buf + d.pos, seclen);
    d.pos += seclen;
    if (!protocore_ber_read_header(&sd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    size_t eidl;
    if (!protocore_ber_read_header(&sd, &tag, &eidl) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    memcpy(v->engine_id, sd.buf + sd.pos, eidl < sizeof(v->engine_id) ? eidl : sizeof(v->engine_id));
    v->engine_id_len = eidl;
    sd.pos += eidl;
    if (!protocore_ber_read_integer(&sd, &v->boots) || !protocore_ber_read_integer(&sd, &v->time))
    {
        return PROTO_FALSE;
    }
    size_t ul;
    if (!protocore_ber_read_header(&sd, &tag, &ul) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    sd.pos += ul;
    size_t al;
    if (!protocore_ber_read_header(&sd, &tag, &al) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    sd.pos += al;
    size_t pl;
    if (!protocore_ber_read_header(&sd, &tag, &pl) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    const uint8_t *privparm = sd.buf + sd.pos;

    const uint8_t *scoped;
    size_t scoped_len;
    if (v->flags & 0x02)
    {
        size_t ctl;
        if (!protocore_ber_read_header(&d, &tag, &ctl) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
        {
            return PROTO_FALSE;
        }
        uint8_t iv[16];
        put_be32(iv, (uint32_t)v->boots);
        put_be32(iv + 4, (uint32_t)v->time);
        memcpy(iv + 8, privparm, 8);
        protocore_snmp_aes128_cfb(privkey, iv, d.buf + d.pos, g_dec, ctl, PROTO_FALSE);
        scoped = g_dec;
        scoped_len = ctl;
    }
    else
    {
        scoped = d.buf + d.pos;
        scoped_len = len - d.pos;
    }

    BerDec cd;
    protocore_ber_dec_init(&cd, scoped, scoped_len);
    if (!protocore_ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    if (!protocore_ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    cd.pos += l;
    if (!protocore_ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    cd.pos += l;
    if (!protocore_ber_read_header(&cd, &v->pdu_tag, &l))
    {
        return PROTO_FALSE;
    }
    if (!protocore_ber_read_integer(&cd, &v->request_id) || !protocore_ber_read_integer(&cd, &v->err_status))
    {
        return PROTO_FALSE;
    }
    long erridx;
    if (!protocore_ber_read_integer(&cd, &erridx))
    {
        return PROTO_FALSE;
    }
    if (!protocore_ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    if (cd.pos < cd.len)
    {
        if (!protocore_ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
        {
            return PROTO_FALSE;
        }
        if (!protocore_ber_read_oid(&cd, v->oid, SNMP_MAX_OID_LEN, &v->oid_len))
        {
            return PROTO_FALSE;
        }
        size_t vl;
        if (!protocore_ber_read_header(&cd, &v->val_tag, &vl))
        {
            return PROTO_FALSE;
        }
        if (v->val_tag == (uint8_t)SNMP_TAG_BER_OCTET_STRING)
        {
            size_t c = vl < sizeof(v->str) - 1 ? vl : sizeof(v->str) - 1;
            memcpy(v->str, cd.buf + cd.pos, c);
            v->str[c] = '\0';
            v->str_len = vl;
        }
    }
    return cd.ok;
}

static void discover(V3View *v)
{
    uint8_t req[256], resp[512];
    uint32_t empty_oid[] = {1, 3, 6, 1, 2, 1, 1, 1, 0};
    size_t rl =
        build_get(req, sizeof(req), PROTO_FALSE, PROTO_FALSE, NULL, 0, 0, 0, "", NULL, NULL, 100, 1, empty_oid, 9);
    size_t n = protocore_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, v));
}

void test_discovery_reports_engine_id()
{
    V3View v;
    discover(&v);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, v.pdu_tag);
    TEST_ASSERT_TRUE(v.engine_id_len >= 5);

    TEST_ASSERT_EQUAL_UINT(11, v.oid_len);
    TEST_ASSERT_EQUAL_UINT32(15u, v.oid[6]);
    TEST_ASSERT_EQUAL_UINT32(4u, v.oid[9]);
}

void test_authnopriv_get()
{
    V3View disc;
    discover(&disc);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", disc.engine_id, disc.engine_id_len, authkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time, "myuser", authkey, NULL, 200, 42, OID_SYSDESCR, 9);
    size_t n = protocore_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);

    V3View v;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &v));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_RESPONSE, v.pdu_tag);
    TEST_ASSERT_EQUAL_INT(42, v.request_id);
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, v.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_OCTET_STRING, v.val_tag);
    TEST_ASSERT_EQUAL_STRING(SYSDESCR_VAL, v.str);
    TEST_ASSERT_EQUAL_UINT8(0x01, v.flags & 0x03);
}

void test_authpriv_get()
{
    V3View disc;
    discover(&disc);
    uint8_t authkey[SNMP_USM_KEY_LEN], privkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", disc.engine_id, disc.engine_id_len, authkey);
    protocore_snmp_usm_localize_key(tw, "privpass12", disc.engine_id, disc.engine_id_len, privkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_TRUE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time, "myuser", authkey, privkey, 201, 77, OID_SYSDESCR, 9);
    size_t n = protocore_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);

    V3View v;
    TEST_ASSERT_TRUE(parse_v3(resp, n, privkey, &v));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_RESPONSE, v.pdu_tag);
    TEST_ASSERT_EQUAL_INT(77, v.request_id);
    TEST_ASSERT_EQUAL_STRING(SYSDESCR_VAL, v.str);
    TEST_ASSERT_EQUAL_UINT8(0x03, v.flags & 0x03);
}

void test_wrong_auth_password_reports_wrong_digest()
{
    V3View disc;
    discover(&disc);
    uint8_t badkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "wrongpass99", disc.engine_id, disc.engine_id_len, badkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time, "myuser", badkey, NULL, 202, 5, OID_SYSDESCR, 9);
    size_t n = protocore_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);

    V3View v;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &v));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, v.pdu_tag);

    TEST_ASSERT_EQUAL_UINT32(5u, v.oid[9]);
}

void test_unknown_user_reports()
{
    V3View disc;
    discover(&disc);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", disc.engine_id, disc.engine_id_len, authkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time, "nobody", authkey, NULL, 203, 9, OID_SYSDESCR, 9);
    size_t n = protocore_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);

    V3View v;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &v));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, v.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(3u, v.oid[9]);
}

void test_not_in_time_window_reports()
{
    V3View disc;
    discover(&disc);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", disc.engine_id, disc.engine_id_len, authkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time + 100000, "myuser", authkey, NULL, 204, 11, OID_SYSDESCR, 9);
    size_t n = protocore_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);

    V3View v;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &v));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, v.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(2u, v.oid[9]);
    TEST_ASSERT_EQUAL_UINT8(0x01, v.flags & 0x03);
}

static proto_bool find_inform_with_reqid(const uint8_t *d, size_t n, uint32_t reqid)
{
    for (size_t i = 0; i + 2 < n; i++)
    {
        if (d[i] != 0xA6)
        {
            continue;
        }

        size_t j = i + 1;
        uint8_t l = d[j++];
        if (l & 0x80)
        {
            j += (l & 0x7F);
        }

        if (j + 2 >= n || d[j] != 0x02)
        {
            continue;
        }
        uint8_t ilen = d[j + 1];
        if (ilen == 0 || ilen > 4 || j + 2 + ilen > n)
        {
            continue;
        }
        uint32_t v = 0;
        for (uint8_t k = 0; k < ilen; k++)
        {
            v = (v << 8) | d[j + 2 + k];
        }
        if (v == reqid)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

void test_inform_v3_builds_informrequest()
{
    protocore_snmp_v3_set_user("myuser", "authpass12", "");
    protocore_net_host_udp_reset();

    const uint32_t reqid = 0x4321;
    proto_bool ok = protocore_snmp_inform_v3("127.0.0.1", 162, reqid, OID_SYSDESCR, 9, NULL, 0);
    TEST_ASSERT_TRUE(ok);

    const uint8_t *d = udp_cap();
    size_t n = udp_cap_len();
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_EQUAL_HEX8(0x30, d[0]);

    size_t off = 1;
    uint8_t l0 = d[off++];
    if (l0 & 0x80)
    {
        off += (l0 & 0x7F);
    }
    TEST_ASSERT_TRUE(d[off] == 0x02 && d[off + 1] == 0x01 && d[off + 2] == 0x03);
    TEST_ASSERT_TRUE(find_inform_with_reqid(d, n, reqid));
}

void test_v3_message_structure_rejections()
{
    V3View v;
    discover(&v);
    uint8_t req[300], resp[512];
    size_t full = build_get(req, sizeof(req), PROTO_FALSE, PROTO_FALSE, v.engine_id, v.engine_id_len, v.boots, v.time,
                            "myuser", NULL, NULL, 300, 7, OID_SYSDESCR, 9);
    TEST_ASSERT_TRUE(full > 0);
    TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(req, full, resp, sizeof(resp)));
    for (size_t L = 0; L < full; L++)
    {
        TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(req, L, resp, sizeof(resp)));
    }

    uint8_t pk[SNMP_USM_KEY_LEN] = {0};
    size_t pl = build_get(req, sizeof(req), PROTO_FALSE, PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                          "myuser", NULL, pk, 300, 8, OID_SYSDESCR, 9);
    TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(req, pl, resp, sizeof(resp)));
}

void test_v3_init_and_boots_accessors()
{
    V3View v;
    discover(&v);
    uint8_t custom[8] = {0x80, 0, 0, 0, 1, 2, 3, 4};
    protocore_snmp_v3_init(custom, sizeof(custom));
    protocore_snmp_v3_init(v.engine_id, v.engine_id_len);
    protocore_snmp_v3_set_user("myuser", "authpass12", "privpass12");

    uint32_t saved = protocore_snmp_v3_get_boots();
    protocore_snmp_v3_set_boots(0xABCD);
    TEST_ASSERT_EQUAL_UINT32(0xABCD, protocore_snmp_v3_get_boots());
    protocore_snmp_v3_set_boots(saved);
}

void test_v3_discovery_variants()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t wrong_eid[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33};
    uint8_t privkey[SNMP_USM_KEY_LEN] = {0};

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_TRUE, wrong_eid, sizeof(wrong_eid), v.boots, v.time,
                          "myuser", authkey, privkey, 300, 9, OID_SYSDESCR, 9);
    TEST_ASSERT_TRUE(protocore_snmp_v3_process(req, rl, resp, sizeof(resp)) > 0);

    rl = build_get(req, sizeof(req), PROTO_FALSE, PROTO_FALSE, wrong_eid, sizeof(wrong_eid), 0, 0, "", NULL, NULL, 300,
                   10, OID_SYSDESCR, 9);
    TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(req, rl, resp, 20));
}

void test_v3_priv_not_configured()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
    protocore_snmp_v3_set_user("myuser", "authpass12", "");
    uint8_t privkey[SNMP_USM_KEY_LEN] = {0};

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                          "myuser", authkey, privkey, 300, 11, OID_SYSDESCR, 9);
    size_t n = protocore_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    protocore_snmp_v3_set_user("myuser", "authpass12", "privpass12");
}

void test_v3_notify_paths()
{
    uint32_t trap_oid[] = {1, 3, 6, 1, 4, 1, 49374, 0, 1};
    protocore_snmp_v3_set_user("myuser", "", "");
    TEST_ASSERT_FALSE(protocore_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, NULL, 0));

    protocore_snmp_v3_set_user("myuser", "authpass12", "privpass12");
    TEST_ASSERT_TRUE(protocore_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, NULL, 0));
    TEST_ASSERT_TRUE(udp_cap_len() > 0);
}

static size_t build_v3_raw_scoped(uint8_t *out, size_t cap, proto_bool auth, const uint8_t *eid, size_t eid_len,
                                  long boots, long time, const char *user, const uint8_t *authkey, long msg_id,
                                  const uint8_t *scoped, size_t scoped_len, proto_bool priv, size_t auth_plen,
                                  size_t priv_plen)
{
    proto_bool digest = auth && auth_plen == SNMP_V3_AUTH_PARAM_LEN;
    uint8_t salt[SNMP_V3_PRIV_PARAM_LEN] = {0, 0, 0, 0, 0, 0, 0, 7};
    uint8_t secp[128];
    BerEnc se2;
    protocore_ber_enc_init(&se2, secp, sizeof(secp));
    size_t s2 = protocore_ber_seq_begin(&se2, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    protocore_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, eid, eid_len);
    SnmpBer.enc = &se2;
    SnmpBer.tlv.ival = boots;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.enc = &se2;
    SnmpBer.tlv.ival = time;
    SnmpBer.put_integer(SnmpBer.internal);
    protocore_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)user, strlen(user));
    size_t auth_off = 0;
    if (auth)
    {
        auth_off = se2.len + 2;
        uint8_t z[SNMP_V3_AUTH_PARAM_LEN] = {0};
        protocore_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, z, auth_plen);
    }
    else
    {
        protocore_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (priv)
    {
        protocore_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, salt, priv_plen);
    }
    else
    {
        protocore_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    protocore_ber_seq_end(&se2, s2);

    BerEnc e;
    protocore_ber_enc_init(&e, out, cap);
    size_t msg = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = (int)SNMP_V3;
    SnmpBer.put_integer(SnmpBer.internal);
    size_t hdr = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = msg_id;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 65507;
    SnmpBer.put_integer(SnmpBer.internal);
    uint8_t fl = (uint8_t)((auth ? 0x01 : 0) | (priv ? 0x02 : 0) | (auth ? 0 : 0x04));
    protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &fl, 1);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 3;
    SnmpBer.put_integer(SnmpBer.internal);
    protocore_ber_seq_end(&e, hdr);
    protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, secp, se2.len);
    size_t sec_value_pos = e.len - se2.len;
    protocore_ber_put_raw(&e, scoped, scoped_len);
    protocore_ber_seq_end(&e, msg);
    if (!e.ok)
    {
        return 0;
    }
    if (digest)
    {
        uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
        protocore_hmac_sha256(tw, authkey, SNMP_USM_KEY_LEN, out, e.len, mac);
        memcpy(out + sec_value_pos + auth_off, mac, SNMP_V3_AUTH_PARAM_LEN);
    }
    return e.len;
}

void test_v3_field_tag_corruption(void)
{
    V3View v;
    discover(&v);
    uint8_t req[300], resp[512];
    size_t full = build_get(req, sizeof(req), PROTO_FALSE, PROTO_FALSE, v.engine_id, v.engine_id_len, v.boots, v.time,
                            "myuser", NULL, NULL, 300, 7, OID_SYSDESCR, 9);
    TEST_ASSERT_TRUE(full > 0);

    BerDec d;
    protocore_ber_dec_init(&d, req, full);
    uint8_t tag;
    size_t l;
    long tmp;
    protocore_ber_read_header(&d, &tag, &l);
    size_t ver_tag = d.pos;
    protocore_ber_read_integer(&d, &tmp);
    size_t gdata_tag = d.pos;
    protocore_ber_read_header(&d, &tag, &l);
    size_t msgid_tag = d.pos;
    protocore_ber_read_integer(&d, &tmp);
    protocore_ber_read_integer(&d, &tmp);
    size_t flags_tag = d.pos;
    protocore_ber_read_header(&d, &tag, &l);
    d.pos += l;
    size_t secmodel_tag = d.pos;
    protocore_ber_read_integer(&d, &tmp);
    size_t secp_tag = d.pos;
    protocore_ber_read_header(&d, &tag, &l);
    size_t base = d.pos;
    BerDec sd;
    protocore_ber_dec_init(&sd, req + base, l);
    size_t sseq_tag = base + sd.pos;
    protocore_ber_read_header(&sd, &tag, &l);
    size_t eid_tag = base + sd.pos;
    protocore_ber_read_header(&sd, &tag, &l);
    sd.pos += l;
    size_t boots_tag = base + sd.pos;
    protocore_ber_read_integer(&sd, &tmp);
    protocore_ber_read_integer(&sd, &tmp);
    size_t uname_tag = base + sd.pos;
    protocore_ber_read_header(&sd, &tag, &l);
    sd.pos += l;
    size_t aparm_tag = base + sd.pos;
    protocore_ber_read_header(&sd, &tag, &l);
    sd.pos += l;
    size_t pparm_tag = base + sd.pos;

    struct
    {
        size_t off;
        uint8_t val;
    } muts[] = {
        {ver_tag, 0xFF},          {ver_tag + 2, 0x04}, {gdata_tag, 0xFF}, {msgid_tag, 0xFF}, {flags_tag, 0xFF},
        {secmodel_tag + 2, 0x04}, {secp_tag, 0xFF},    {sseq_tag, 0xFF},  {eid_tag, 0xFF},   {boots_tag, 0xFF},
        {uname_tag, 0xFF},        {aparm_tag, 0xFF},   {pparm_tag, 0xFF},
    };
    for (size_t i = 0; i < sizeof(muts) / sizeof(muts[0]); i++)
    {
        uint8_t bad[300];
        memcpy(bad, req, full);
        bad[muts[i].off] = muts[i].val;
        TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(bad, full, resp, sizeof(resp)));
    }
}

void test_v3_scoped_parse_rejections(void)
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t req[320], resp[512];

    const uint8_t not_seq[] = {0x04, 0x01, 0x00};
    const uint8_t bad_eid[] = {0x30, 0x03, 0x02, 0x01, 0x00};
    const uint8_t bad_ctx[] = {0x30, 0x06, 0x04, 0x01, 0x00, 0x02, 0x01, 0x00};
    const uint8_t no_pdu[] = {0x30, 0x06, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00};
    const uint8_t empty_pdu[] = {0x30, 0x08, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0xA0, 0x00};
    const uint8_t *scopeds[] = {not_seq, bad_eid, bad_ctx, no_pdu, empty_pdu};
    const size_t lens[] = {sizeof(not_seq), sizeof(bad_eid), sizeof(bad_ctx), sizeof(no_pdu), sizeof(empty_pdu)};
    for (size_t i = 0; i < sizeof(scopeds) / sizeof(scopeds[0]); i++)
    {
        size_t rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                                        "myuser", authkey, 400 + (long)i, scopeds[i], lens[i], PROTO_FALSE,
                                        SNMP_V3_AUTH_PARAM_LEN, SNMP_V3_PRIV_PARAM_LEN);
        TEST_ASSERT_TRUE(rl > 0);
        TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(req, rl, resp, sizeof(resp)));
    }
}

void test_v3_discovery_malformed_scoped(void)
{
    V3View v;
    discover(&v);
    uint8_t wrong_eid[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33};
    uint8_t req[320], resp[512];

    const uint8_t not_seq[] = {0x04, 0x01, 0x00};
    const uint8_t non_int_rid[] = {0x30, 0x0B, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0xA0, 0x03, 0x04, 0x01, 0x00};
    const uint8_t *scopeds[] = {not_seq, non_int_rid};
    const size_t lens[] = {sizeof(not_seq), sizeof(non_int_rid)};
    for (size_t i = 0; i < 2; i++)
    {
        size_t rl = build_v3_raw_scoped(req, sizeof(req), PROTO_FALSE, wrong_eid, sizeof(wrong_eid), v.boots, v.time,
                                        "", NULL, 410 + (long)i, scopeds[i], lens[i], PROTO_FALSE,
                                        SNMP_V3_AUTH_PARAM_LEN, SNMP_V3_PRIV_PARAM_LEN);
        TEST_ASSERT_TRUE(rl > 0);

        TEST_ASSERT_TRUE(protocore_snmp_v3_process(req, rl, resp, sizeof(resp)) > 0);
    }
}

void test_v3_auth_edge_rejections(void)
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t req[320], resp[512];

    const uint8_t any_scoped[] = {0x30, 0x02, 0x04, 0x00};
    size_t rl =
        build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time, "myuser",
                            authkey, 420, any_scoped, sizeof(any_scoped), PROTO_FALSE, 16, SNMP_V3_PRIV_PARAM_LEN);
    TEST_ASSERT_TRUE(rl > 0);
    size_t n = protocore_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(5u, r.oid[9]);

    const uint8_t not_octet[] = {0x02, 0x01, 0x00};
    rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time, "myuser",
                             authkey, 421, not_octet, sizeof(not_octet), PROTO_TRUE, SNMP_V3_AUTH_PARAM_LEN,
                             SNMP_V3_PRIV_PARAM_LEN);
    TEST_ASSERT_TRUE(rl > 0);
    TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(req, rl, resp, sizeof(resp)));
}

void test_v3_notify_overflow_guards()
{
    protocore_snmp_v3_set_user("myuser", "authpass12", "privpass12");
    uint32_t trap_oid[] = {1, 3, 6, 1, 4, 1, 49374, 0, 1};
    uint32_t vb_oid[] = {1, 3, 6, 1, 4, 1, 49374, 5, 0};
    static uint8_t big[1600];
    memset(big, 0x41, sizeof(big));

    SnmpVarbind vb;
    vb.oid = vb_oid;
    vb.oid_len = 9;
    vb.type = (uint8_t)SNMP_VB_STRING;
    vb.ival = 0;
    vb.bytes = big;
    vb.oid_val = NULL;
    vb.oid_val_len = 0;

    vb.blen = 32;
    TEST_ASSERT_TRUE(protocore_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, &vb, 1));
    vb.blen = 1550;
    TEST_ASSERT_FALSE(protocore_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, &vb, 1));
    for (size_t blen = 1360; blen <= 1480; blen += 2)
    {
        vb.blen = blen;
        (void)protocore_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, &vb, 1);
    }
}

static size_t g_big_len = 0;
static uint8_t g_big[1600];
static proto_bool big_getter(SnmpValue *out)
{
    out->type = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    out->str = (const char *)g_big;
    out->str_len = g_big_len;
    return PROTO_TRUE;
}

void test_v3_response_scopedpdu_overflow()
{
    memset(g_big, 0x42, sizeof(g_big));
    static const uint32_t big_oid[] = {1, 3, 6, 1, 4, 1, 49374, 7, 0};
    TEST_ASSERT_TRUE(protocore_snmp_agent_add_dynamic(big_oid, 9, (uint8_t)SNMP_TAG_BER_OCTET_STRING, big_getter));

    V3View disc;
    discover(&disc);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", disc.engine_id, disc.engine_id_len, authkey);

    uint8_t req[300], resp[2048];
    proto_bool saw_overflow = PROTO_FALSE, saw_ok = PROTO_FALSE;
    for (size_t vlen = 1400; vlen <= 1472; vlen++)
    {
        g_big_len = vlen;
        size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                              disc.time, "myuser", authkey, NULL, 500, 88, big_oid, 9);
        TEST_ASSERT_TRUE(rl > 0);
        size_t n = protocore_snmp_agent_process(req, rl, resp, sizeof(resp));
        if (n == 0)
        {
            saw_overflow = PROTO_TRUE;
        }
        else
        {
            saw_ok = PROTO_TRUE;
        }
    }
    TEST_ASSERT_TRUE(saw_overflow);
    TEST_ASSERT_TRUE(saw_ok);
}

static size_t build_v3_frame(uint8_t *out, size_t cap, long msg_id, uint8_t flags, const uint8_t *sec, size_t sec_len,
                             const uint8_t *tail, size_t tail_len)
{
    BerEnc e;
    protocore_ber_enc_init(&e, out, cap);
    size_t msg = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = (int)SNMP_V3;
    SnmpBer.put_integer(SnmpBer.internal);
    size_t hdr = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = msg_id;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 65507;
    SnmpBer.put_integer(SnmpBer.internal);
    protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &flags, 1);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 3;
    SnmpBer.put_integer(SnmpBer.internal);
    protocore_ber_seq_end(&e, hdr);
    protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, sec, sec_len);
    protocore_ber_put_raw(&e, tail, tail_len);
    protocore_ber_seq_end(&e, msg);
    return e.ok ? e.len : 0;
}

static size_t build_secparams_prefix(uint8_t *out, size_t cap, unsigned nfields)
{
    BerEnc e;
    protocore_ber_enc_init(&e, out, cap);
    size_t s = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    if (nfields >= 1)
    {
        protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (nfields >= 2)
    {
        SnmpBer.enc = &e;
        SnmpBer.tlv.ival = 1;
        SnmpBer.put_integer(SnmpBer.internal);
    }
    if (nfields >= 3)
    {
        SnmpBer.enc = &e;
        SnmpBer.tlv.ival = 0;
        SnmpBer.put_integer(SnmpBer.internal);
    }
    if (nfields >= 4)
    {
        protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (nfields >= 5)
    {
        protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    protocore_ber_seq_end(&e, s);
    return e.ok ? e.len : 0;
}

void test_v3_truncated_fields_fail_closed()
{
    uint8_t req[320], resp[512];

    for (int stop = 0; stop <= 4; stop++)
    {
        BerEnc e;
        protocore_ber_enc_init(&e, req, sizeof(req));
        size_t msg = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
        SnmpBer.enc = &e;
        SnmpBer.tlv.ival = (int)SNMP_V3;
        SnmpBer.put_integer(SnmpBer.internal);
        if (stop >= 1)
        {
            size_t hdr = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
            SnmpBer.enc = &e;
            SnmpBer.tlv.ival = 500 + stop;
            SnmpBer.put_integer(SnmpBer.internal);
            if (stop >= 2)
            {
                SnmpBer.enc = &e;
                SnmpBer.tlv.ival = 65507;
                SnmpBer.put_integer(SnmpBer.internal);
            }
            if (stop >= 3)
            {
                uint8_t fl = 0x04;
                protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &fl, 1);
            }
            if (stop >= 4)
            {
                SnmpBer.enc = &e;
                SnmpBer.tlv.ival = 3;
                SnmpBer.put_integer(SnmpBer.internal);
            }
            protocore_ber_seq_end(&e, hdr);
        }
        protocore_ber_seq_end(&e, msg);
        TEST_ASSERT_TRUE(e.ok);
        TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(req, e.len, resp, sizeof(resp)));
    }

    const uint8_t tail[] = {0x30, 0x02, 0x04, 0x00};
    size_t fl = build_v3_frame(req, sizeof(req), 510, 0x04, NULL, 0, tail, sizeof(tail));
    TEST_ASSERT_TRUE(fl > 0);
    TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(req, fl, resp, sizeof(resp)));
    for (unsigned nf = 0; nf <= 5; nf++)
    {
        uint8_t sec[64];
        size_t sl = build_secparams_prefix(sec, sizeof(sec), nf);
        TEST_ASSERT_TRUE(sl > 0);
        size_t n = build_v3_frame(req, sizeof(req), 520 + (long)nf, 0x04, sec, sl, tail, sizeof(tail));
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(req, n, resp, sizeof(resp)));
    }
}

void test_v3_outer_tag_and_empty_flags()
{
    uint8_t resp[512];
    const uint8_t not_seq[] = {0x04, 0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(not_seq, sizeof(not_seq), resp, sizeof(resp)));

    uint8_t req[320];
    BerEnc e;
    protocore_ber_enc_init(&e, req, sizeof(req));
    size_t msg = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = (int)SNMP_V3;
    SnmpBer.put_integer(SnmpBer.internal);
    size_t hdr = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 530;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 65507;
    SnmpBer.put_integer(SnmpBer.internal);
    protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 3;
    SnmpBer.put_integer(SnmpBer.internal);
    protocore_ber_seq_end(&e, hdr);
    protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    protocore_ber_seq_end(&e, msg);
    TEST_ASSERT_TRUE(e.ok);
    TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(req, e.len, resp, sizeof(resp)));
}

void test_v3_scoped_truncated_headers()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t req[320], resp[512];

    const uint8_t no_len[] = {0x30};
    const uint8_t empty_seq[] = {0x30, 0x00};
    const uint8_t eid_only[] = {0x30, 0x02, 0x04, 0x00};
    const uint8_t *scopeds[] = {no_len, empty_seq, eid_only};
    const size_t lens[] = {sizeof(no_len), sizeof(empty_seq), sizeof(eid_only)};
    for (unsigned i = 0; i < 3; i++)
    {
        size_t rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                                        "myuser", authkey, 540 + (long)i, scopeds[i], lens[i], PROTO_FALSE,
                                        SNMP_V3_AUTH_PARAM_LEN, SNMP_V3_PRIV_PARAM_LEN);
        TEST_ASSERT_TRUE(rl > 0);
        TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(req, rl, resp, sizeof(resp)));
    }
}

void test_v3_same_length_wrong_engine_id()
{
    V3View v;
    discover(&v);
    uint8_t wrong[SNMP_V3_ENGINEID_MAX];
    TEST_ASSERT_TRUE(v.engine_id_len <= sizeof(wrong));
    memcpy(wrong, v.engine_id, v.engine_id_len);
    wrong[v.engine_id_len - 1] ^= 0xFF;

    uint8_t req[320], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_FALSE, PROTO_FALSE, wrong, v.engine_id_len, 0, 0, "", NULL, NULL, 550,
                          1, OID_SYSDESCR, 9);
    size_t n = protocore_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(4u, r.oid[9]);
}

void test_v3_unknown_user_variants()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t req[320], resp[512];
    V3View r;

    struct
    {
        const char *cfg_user;
        const char *cfg_auth;
        const char *req_user;
    } cases[] = {
        {"myuser", "", "myuser"},
        {"myuser", "authpass12", "bob"},
        {"", "authpass12", ""},
    };
    for (unsigned i = 0; i < 3; i++)
    {
        protocore_snmp_v3_set_user(cases[i].cfg_user, cases[i].cfg_auth, "");
        size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, v.engine_id, v.engine_id_len, v.boots, v.time,
                              cases[i].req_user, authkey, NULL, 560 + (long)i, 3, OID_SYSDESCR, 9);
        TEST_ASSERT_TRUE(rl > 0);
        size_t n = protocore_snmp_v3_process(req, rl, resp, sizeof(resp));
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
        TEST_ASSERT_EQUAL_UINT32(3u, r.oid[9]);
    }
}

void test_v3_oversized_message_is_wrong_digest()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);

    static uint8_t scoped[SNMP_MSG_BUF_SIZE + 64];
    memset(scoped, 0x00, sizeof(scoped));
    scoped[0] = 0x30;
    static uint8_t req[SNMP_MSG_BUF_SIZE + 256];
    uint8_t resp[512];
    size_t rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                                    "myuser", authkey, 570, scoped, sizeof(scoped), PROTO_FALSE, SNMP_V3_AUTH_PARAM_LEN,
                                    SNMP_V3_PRIV_PARAM_LEN);
    TEST_ASSERT_TRUE(rl > SNMP_MSG_BUF_SIZE);
    size_t n = protocore_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(5u, r.oid[9]);
}

void test_v3_boots_mismatch_not_in_time()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);

    uint8_t req[320], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, v.engine_id, v.engine_id_len, v.boots + 7, v.time,
                          "myuser", authkey, NULL, 580, 4, OID_SYSDESCR, 9);
    size_t n = protocore_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(2u, r.oid[9]);
    TEST_ASSERT_EQUAL_UINT8(0x01, r.flags & 0x03);
}

void test_v3_privacy_parameter_edges()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t req[320], resp[512];

    const uint8_t any[] = {0x04, 0x02, 0x00, 0x00};
    size_t rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                                    "myuser", authkey, 590, any, sizeof(any), PROTO_TRUE, SNMP_V3_AUTH_PARAM_LEN, 4);
    TEST_ASSERT_TRUE(rl > 0);
    size_t n = protocore_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(6u, r.oid[9]);

    const uint8_t stub[] = {0x04};
    rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time, "myuser",
                             authkey, 591, stub, sizeof(stub), PROTO_TRUE, SNMP_V3_AUTH_PARAM_LEN,
                             SNMP_V3_PRIV_PARAM_LEN);
    TEST_ASSERT_TRUE(rl > 0);
    TEST_ASSERT_EQUAL_UINT(0, protocore_snmp_v3_process(req, rl, resp, sizeof(resp)));
}

void test_v3_init_length_guards_and_null_user()
{
    V3View before;
    discover(&before);

    uint8_t tooshort[4] = {0x80, 0x00, 0x00, 0x01};
    protocore_snmp_v3_init(tooshort, sizeof(tooshort));
    V3View after;
    discover(&after);
    TEST_ASSERT_EQUAL_UINT(before.engine_id_len, after.engine_id_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before.engine_id, after.engine_id, before.engine_id_len);

    uint8_t toolong[SNMP_V3_ENGINEID_MAX + 1];
    memset(toolong, 0xA5, sizeof(toolong));
    protocore_snmp_v3_init(toolong, sizeof(toolong));
    discover(&after);
    TEST_ASSERT_EQUAL_UINT(before.engine_id_len, after.engine_id_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before.engine_id, after.engine_id, before.engine_id_len);

    protocore_snmp_v3_set_user(NULL, NULL, NULL);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    protocore_snmp_usm_localize_key(tw, "authpass12", after.engine_id, after.engine_id_len, authkey);
    uint8_t req[320], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, after.engine_id, after.engine_id_len, after.boots,
                          after.time, "myuser", authkey, NULL, 600, 5, OID_SYSDESCR, 9);
    size_t n = protocore_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(3u, r.oid[9]);
}

void test_v3_trap_reports_transport_failure()
{
    uint32_t trap_oid[] = {1, 3, 6, 1, 4, 1, 49374, 0, 1};

    TEST_ASSERT_FALSE(protocore_snmp_trap_v3("not.an.address", 162, trap_oid, 9, NULL, 0));
    TEST_ASSERT_FALSE(protocore_snmp_trap_v3(NULL, 162, trap_oid, 9, NULL, 0));

    protocore_net_host_udp_reset();
    mock_udp_send_fail_after(0);
    TEST_ASSERT_FALSE(protocore_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_net_host_udp_count());

    mock_udp_send_fail_after(-1);
    TEST_ASSERT_TRUE(protocore_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(1, protocore_net_host_udp_count());
    TEST_ASSERT_EQUAL_UINT16(162, protocore_net_host_udp_at(0)->dst_port);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_v3_trap_reports_transport_failure);
    RUN_TEST(test_v3_truncated_fields_fail_closed);
    RUN_TEST(test_v3_outer_tag_and_empty_flags);
    RUN_TEST(test_v3_scoped_truncated_headers);
    RUN_TEST(test_v3_same_length_wrong_engine_id);
    RUN_TEST(test_v3_unknown_user_variants);
    RUN_TEST(test_v3_oversized_message_is_wrong_digest);
    RUN_TEST(test_v3_boots_mismatch_not_in_time);
    RUN_TEST(test_v3_privacy_parameter_edges);
    RUN_TEST(test_v3_init_length_guards_and_null_user);
    RUN_TEST(test_v3_response_scopedpdu_overflow);
    RUN_TEST(test_v3_field_tag_corruption);
    RUN_TEST(test_v3_scoped_parse_rejections);
    RUN_TEST(test_v3_discovery_malformed_scoped);
    RUN_TEST(test_v3_auth_edge_rejections);
    RUN_TEST(test_v3_message_structure_rejections);
    RUN_TEST(test_v3_init_and_boots_accessors);
    RUN_TEST(test_v3_discovery_variants);
    RUN_TEST(test_v3_priv_not_configured);
    RUN_TEST(test_v3_notify_paths);
    RUN_TEST(test_v3_notify_overflow_guards);
    RUN_TEST(test_localize_key_sha256_vector);
    RUN_TEST(test_localize_key_empty_password);
    RUN_TEST(test_aes128_fips197_vector);
    RUN_TEST(test_aes_cfb_roundtrip_partial_block);
    RUN_TEST(test_discovery_reports_engine_id);
    RUN_TEST(test_authnopriv_get);
    RUN_TEST(test_authpriv_get);
    RUN_TEST(test_wrong_auth_password_reports_wrong_digest);
    RUN_TEST(test_unknown_user_reports);
    RUN_TEST(test_not_in_time_window_reports);
    RUN_TEST(test_inform_v3_builds_informrequest);
    return UNITY_END();
}
