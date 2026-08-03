// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SNMPv3 USM layer. The test acts as a full SNMP manager:
// it derives USM keys, builds authenticated/encrypted v3 messages, runs them
// through the agent, and verifies the authenticated/encrypted responses.
//
// Crypto is grounded against independent vectors: the SHA-256 key-localization
// expected value is computed with Python hashlib (RFC 3414 §2.6 / RFC 7860), and
// the AES-128 block cipher is checked against the FIPS-197 Appendix C.1 KAT.

#include "crypto/mac/hmac_sha256.h"
#include "network_drivers/transport/udp.h"
#include "services/net/snmp/snmp_agent.h"
#include "services/net/snmp/snmp_ber.h"
#include "services/net/snmp/snmp_crypto.h"
#include "services/net/snmp/snmp_notify.h"
#include "services/net/snmp/snmp_v3.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

static const uint32_t OID_SYSDESCR[] = {1, 3, 6, 1, 2, 1, 1, 1, 0};
static const char *SYSDESCR_VAL = "PC v3 agent";

void setUp()
{
    pc_snmp_agent_init("public");
    pc_snmp_agent_set_system(SYSDESCR_VAL, "admin", "esp32", "lab", 72);
    pc_snmp_v3_init(NULL, 0); // built-in default engine ID
    pc_snmp_v3_set_boots(1);
    pc_snmp_v3_set_user("myuser", "authpass12", "privpass12");
}
void tearDown()
{
}

// ==================== crypto known-answer vectors ====================

void test_localize_key_sha256_vector()
{
    // password "maplesyrup", engineID 80 00 C0 DE 05 01 02 03 04 (the agent default).
    // Expected localized key computed independently with Python hashlib.
    const uint8_t engine[] = {0x80, 0x00, 0xC0, 0xDE, 0x05, 0x01, 0x02, 0x03, 0x04};
    const uint8_t expect[32] = {0xb8, 0xa5, 0x56, 0xfc, 0xb1, 0xcd, 0xee, 0x18, 0x79, 0x22, 0x24,
                                0x79, 0x69, 0xaf, 0xe3, 0x90, 0x37, 0x34, 0x07, 0xd6, 0x52, 0x9a,
                                0x98, 0x08, 0x41, 0x8a, 0xef, 0x8e, 0xb0, 0xbf, 0x45, 0x86};
    uint8_t key[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("maplesyrup", engine, sizeof(engine), key);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, key, 32);
}

// An empty or null password fails closed to an all-zero key (rather than hashing an empty
// password into a usable one) - a v3 user with no auth secret cannot derive a valid key.
void test_localize_key_empty_password()
{
    const uint8_t engine[] = {0x80, 0x00, 0xC0, 0xDE, 0x05, 0x01, 0x02, 0x03, 0x04};
    const uint8_t zero[SNMP_USM_KEY_LEN] = {0};
    uint8_t key[SNMP_USM_KEY_LEN];

    memset(key, 0xAA, sizeof(key));
    pc_snmp_usm_localize_key("", engine, sizeof(engine), key); // empty password
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, key, SNMP_USM_KEY_LEN);

    memset(key, 0xBB, sizeof(key));
    pc_snmp_usm_localize_key(NULL, engine, sizeof(engine), key); // null password
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, key, SNMP_USM_KEY_LEN);
}

void test_aes128_fips197_vector()
{
    // FIPS-197 C.1. CFB with IV = plaintext and zero input yields E_key(plaintext).
    const uint8_t key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                             0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    const uint8_t pt[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                            0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    const uint8_t expect[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    uint8_t zero[16] = {0};
    uint8_t out[16];
    pc_snmp_aes128_cfb(key, pt, zero, out, 16, PROTO_TRUE); // out = 0 XOR E(IV=pt) = E(pt)
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 16);
}

void test_aes_cfb_roundtrip_partial_block()
{
    const uint8_t key[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint8_t iv[16] = {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    const char *msg = "SNMPv3 scopedPDU spanning more than one AES block plus a tail.";
    size_t n = strlen(msg);
    uint8_t ct[128], pt[128];
    pc_snmp_aes128_cfb(key, iv, (const uint8_t *)msg, ct, n, PROTO_TRUE);
    pc_snmp_aes128_cfb(key, iv, ct, pt, n, PROTO_FALSE);
    TEST_ASSERT_EQUAL_MEMORY(msg, pt, n);
    TEST_ASSERT_TRUE(memcmp(msg, ct, n) != 0); // actually encrypted
}

// ==================== v3 client (manager) helpers ====================

static uint8_t g_dec[1024]; // scratch for response decryption

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

// Build a v3 GET request for a single OID.
static size_t build_get(uint8_t *out, size_t cap, proto_bool auth, proto_bool priv, const uint8_t *eid, size_t eid_len,
                        long boots, long time, const char *user, const uint8_t *authkey, const uint8_t *privkey,
                        long msg_id, long req_id, const uint32_t *oid, size_t oid_len)
{
    // inner GET PDU
    uint8_t pdu[256];
    BerEnc pe;
    pc_ber_enc_init(&pe, pdu, sizeof(pdu));
    size_t pp = pc_ber_seq_begin(&pe, (uint8_t)SNMP_TAG_SNMP_PDU_GET);
    pc_ber_put_integer(&pe, req_id);
    pc_ber_put_integer(&pe, 0);
    pc_ber_put_integer(&pe, 0);
    size_t vbl = pc_ber_seq_begin(&pe, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    size_t vb = pc_ber_seq_begin(&pe, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_oid(&pe, oid, oid_len);
    pc_ber_put_null(&pe);
    pc_ber_seq_end(&pe, vb);
    pc_ber_seq_end(&pe, vbl);
    pc_ber_seq_end(&pe, pp);

    // scopedPDU
    uint8_t scoped[320];
    BerEnc se;
    pc_ber_enc_init(&se, scoped, sizeof(scoped));
    size_t ss = pc_ber_seq_begin(&se, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_octet_string(&se, (uint8_t)SNMP_TAG_BER_OCTET_STRING, eid, eid_len);
    pc_ber_put_octet_string(&se, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    pc_ber_put_raw(&se, pdu, pe.len);
    pc_ber_seq_end(&se, ss);

    // optional privacy
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
        pc_snmp_aes128_cfb(privkey, iv, scoped, cipher, se.len, PROTO_TRUE);
        data = cipher;
    }

    // secparams
    uint8_t secp[128];
    BerEnc se2;
    pc_ber_enc_init(&se2, secp, sizeof(secp));
    size_t s2 = pc_ber_seq_begin(&se2, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, eid, eid_len);
    pc_ber_put_integer(&se2, boots);
    pc_ber_put_integer(&se2, time);
    pc_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)user, strlen(user));
    size_t auth_off = 0;
    if (auth)
    {
        auth_off = se2.len + 2;
        uint8_t z[SNMP_V3_AUTH_PARAM_LEN] = {0};
        pc_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, z, SNMP_V3_AUTH_PARAM_LEN);
    }
    else
    {
        pc_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (priv)
    {
        pc_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, salt, 8);
    }
    else
    {
        pc_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    pc_ber_seq_end(&se2, s2);

    // message
    BerEnc e;
    pc_ber_enc_init(&e, out, cap);
    size_t msg = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_integer(&e, (int)SNMP_V3);
    size_t hdr = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_integer(&e, msg_id);
    pc_ber_put_integer(&e, 65507);
    uint8_t fl = (uint8_t)((auth ? 1 : 0) | (priv ? 2 : 0) | (auth ? 0 : 4)); // reportable on discovery
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &fl, 1);
    pc_ber_put_integer(&e, 3);
    pc_ber_seq_end(&e, hdr);
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, secp, se2.len);
    size_t sec_value_pos = e.len - se2.len;
    if (priv)
    {
        pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, data, data_len);
    }
    else
    {
        pc_ber_put_raw(&e, data, data_len);
    }
    pc_ber_seq_end(&e, msg);

    if (auth)
    {
        uint8_t mac[PC_HMAC_SHA256_LEN];
        pc_hmac_sha256(authkey, SNMP_USM_KEY_LEN, out, e.len, mac);
        memcpy(out + sec_value_pos + auth_off, mac, SNMP_V3_AUTH_PARAM_LEN);
    }
    return e.ok ? e.len : 0;
}

static proto_bool parse_v3(const uint8_t *buf, size_t len, const uint8_t *privkey, V3View *v)
{
    memset(v, 0, sizeof(*v));
    BerDec d;
    pc_ber_dec_init(&d, buf, len);
    uint8_t tag;
    size_t l;
    if (!pc_ber_read_header(&d, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    long version;
    if (!pc_ber_read_integer(&d, &version))
    {
        return PROTO_FALSE;
    }
    if (!pc_ber_read_header(&d, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE) // global data
    {
        return PROTO_FALSE;
    }
    long msgid, maxsize, secmodel;
    if (!pc_ber_read_integer(&d, &msgid) || !pc_ber_read_integer(&d, &maxsize))
    {
        return PROTO_FALSE;
    }
    size_t fl;
    if (!pc_ber_read_header(&d, &tag, &fl) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    v->flags = d.buf[d.pos];
    d.pos += fl;
    if (!pc_ber_read_integer(&d, &secmodel))
    {
        return PROTO_FALSE;
    }
    // secparams
    size_t seclen;
    if (!pc_ber_read_header(&d, &tag, &seclen) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    BerDec sd;
    pc_ber_dec_init(&sd, d.buf + d.pos, seclen);
    d.pos += seclen;
    if (!pc_ber_read_header(&sd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    size_t eidl;
    if (!pc_ber_read_header(&sd, &tag, &eidl) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    memcpy(v->engine_id, sd.buf + sd.pos, eidl < sizeof(v->engine_id) ? eidl : sizeof(v->engine_id));
    v->engine_id_len = eidl;
    sd.pos += eidl;
    if (!pc_ber_read_integer(&sd, &v->boots) || !pc_ber_read_integer(&sd, &v->time))
    {
        return PROTO_FALSE;
    }
    size_t ul;
    if (!pc_ber_read_header(&sd, &tag, &ul) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING) // user
    {
        return PROTO_FALSE;
    }
    sd.pos += ul;
    size_t al;
    if (!pc_ber_read_header(&sd, &tag, &al) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING) // authparams
    {
        return PROTO_FALSE;
    }
    sd.pos += al;
    size_t pl;
    if (!pc_ber_read_header(&sd, &tag, &pl) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING) // privparams
    {
        return PROTO_FALSE;
    }
    const uint8_t *privparm = sd.buf + sd.pos;

    const uint8_t *scoped;
    size_t scoped_len;
    if (v->flags & 0x02)
    {
        size_t ctl;
        if (!pc_ber_read_header(&d, &tag, &ctl) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
        {
            return PROTO_FALSE;
        }
        uint8_t iv[16];
        put_be32(iv, (uint32_t)v->boots);
        put_be32(iv + 4, (uint32_t)v->time);
        memcpy(iv + 8, privparm, 8);
        pc_snmp_aes128_cfb(privkey, iv, d.buf + d.pos, g_dec, ctl, PROTO_FALSE);
        scoped = g_dec;
        scoped_len = ctl;
    }
    else
    {
        scoped = d.buf + d.pos;
        scoped_len = len - d.pos;
    }

    // scopedPDU -> inner PDU
    BerDec cd;
    pc_ber_dec_init(&cd, scoped, scoped_len);
    if (!pc_ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    if (!pc_ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING) // ctxEngineID
    {
        return PROTO_FALSE;
    }
    cd.pos += l;
    if (!pc_ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING) // ctxName
    {
        return PROTO_FALSE;
    }
    cd.pos += l;
    if (!pc_ber_read_header(&cd, &v->pdu_tag, &l)) // PDU
    {
        return PROTO_FALSE;
    }
    if (!pc_ber_read_integer(&cd, &v->request_id) || !pc_ber_read_integer(&cd, &v->err_status))
    {
        return PROTO_FALSE;
    }
    long erridx;
    if (!pc_ber_read_integer(&cd, &erridx))
    {
        return PROTO_FALSE;
    }
    if (!pc_ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE) // varbind list
    {
        return PROTO_FALSE;
    }
    if (cd.pos < cd.len)
    {
        if (!pc_ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE) // first varbind
        {
            return PROTO_FALSE;
        }
        if (!pc_ber_read_oid(&cd, v->oid, SNMP_MAX_OID_LEN, &v->oid_len))
        {
            return PROTO_FALSE;
        }
        size_t vl;
        if (!pc_ber_read_header(&cd, &v->val_tag, &vl))
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

// Discover the agent's authoritative engineID / boots / time via a probe.
static void discover(V3View *v)
{
    uint8_t req[256], resp[512];
    uint32_t empty_oid[] = {1, 3, 6, 1, 2, 1, 1, 1, 0};
    size_t rl =
        build_get(req, sizeof(req), PROTO_FALSE, PROTO_FALSE, NULL, 0, 0, 0, "", NULL, NULL, 100, 1, empty_oid, 9);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, v));
}

// ==================== v3 message-flow tests ====================

void test_discovery_reports_engine_id()
{
    V3View v;
    discover(&v);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, v.pdu_tag);
    TEST_ASSERT_TRUE(v.engine_id_len >= 5); // a real authoritative engine ID
    // usmStatsUnknownEngineIDs = 1.3.6.1.6.3.15.1.1.4.0
    TEST_ASSERT_EQUAL_UINT(11, v.oid_len);
    TEST_ASSERT_EQUAL_UINT32(15u, v.oid[6]);
    TEST_ASSERT_EQUAL_UINT32(4u, v.oid[9]);
}

void test_authnopriv_get()
{
    V3View disc;
    discover(&disc);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", disc.engine_id, disc.engine_id_len, authkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time, "myuser", authkey, NULL, 200, 42, OID_SYSDESCR, 9);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);

    V3View v;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &v));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_RESPONSE, v.pdu_tag);
    TEST_ASSERT_EQUAL_INT(42, v.request_id);
    TEST_ASSERT_EQUAL_INT((int)SNMP_ERR_NO_ERROR, v.err_status);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_OCTET_STRING, v.val_tag);
    TEST_ASSERT_EQUAL_STRING(SYSDESCR_VAL, v.str);
    TEST_ASSERT_EQUAL_UINT8(0x01, v.flags & 0x03); // response is authNoPriv
}

void test_authpriv_get()
{
    V3View disc;
    discover(&disc);
    uint8_t authkey[SNMP_USM_KEY_LEN], privkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", disc.engine_id, disc.engine_id_len, authkey);
    pc_snmp_usm_localize_key("privpass12", disc.engine_id, disc.engine_id_len, privkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_TRUE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time, "myuser", authkey, privkey, 201, 77, OID_SYSDESCR, 9);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);

    V3View v;
    TEST_ASSERT_TRUE(parse_v3(resp, n, privkey, &v));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_RESPONSE, v.pdu_tag);
    TEST_ASSERT_EQUAL_INT(77, v.request_id);
    TEST_ASSERT_EQUAL_STRING(SYSDESCR_VAL, v.str);
    TEST_ASSERT_EQUAL_UINT8(0x03, v.flags & 0x03); // response is authPriv
}

void test_wrong_auth_password_reports_wrong_digest()
{
    V3View disc;
    discover(&disc);
    uint8_t badkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("wrongpass99", disc.engine_id, disc.engine_id_len, badkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time, "myuser", badkey, NULL, 202, 5, OID_SYSDESCR, 9);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);

    V3View v;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &v));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, v.pdu_tag);
    // usmStatsWrongDigests = ...15.1.1.5.0
    TEST_ASSERT_EQUAL_UINT32(5u, v.oid[9]);
}

void test_unknown_user_reports()
{
    V3View disc;
    discover(&disc);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", disc.engine_id, disc.engine_id_len, authkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time, "nobody", authkey, NULL, 203, 9, OID_SYSDESCR, 9);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);

    V3View v;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &v));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, v.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(3u, v.oid[9]); // usmStatsUnknownUserNames
}

void test_not_in_time_window_reports()
{
    V3View disc;
    discover(&disc);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", disc.engine_id, disc.engine_id_len, authkey);

    // Send a far-future engineTime to fall outside the +/-150s window.
    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time + 100000, "myuser", authkey, NULL, 204, 11, OID_SYSDESCR, 9);
    size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);

    V3View v;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &v));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, v.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(2u, v.oid[9]);        // usmStatsNotInTimeWindows
    TEST_ASSERT_EQUAL_UINT8(0x01, v.flags & 0x03); // time reports are authenticated
}

// Scan a captured datagram for an InformRequest PDU (tag 0xA6) whose request-id
// matches @p reqid. Robust against a stray 0xA6 in the auth digest: a real match
// must be followed by a well-formed INTEGER request-id equal to reqid.
static proto_bool find_inform_with_reqid(const uint8_t *d, size_t n, uint32_t reqid)
{
    for (size_t i = 0; i + 2 < n; i++)
    {
        if (d[i] != 0xA6) // InformRequest PDU tag
        {
            continue;
        }
        // Skip the PDU length field (the BER encoder always emits long-form 0x82 XX XX).
        size_t j = i + 1;
        uint8_t l = d[j++];
        if (l & 0x80)
        {
            j += (l & 0x7F);
        }
        // The first field of the PDU is the request-id INTEGER.
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

// SNMPv3 USM InformRequest (the confirmed counterpart to the v3 trap): with
// authNoPriv the scopedPDU is plaintext, so the captured datagram must be a v3
// message (version 3) carrying an InformRequest PDU with our request-id.
void test_inform_v3_builds_informrequest()
{
    pc_snmp_v3_set_user("myuser", "authpass12", ""); // auth-only -> plaintext scopedPDU
    pc_udp_capture_enable();
    pc_udp_capture_reset();

    const uint32_t reqid = 0x4321;
    proto_bool ok = pc_snmp_inform_v3("127.0.0.1", 162, reqid, OID_SYSDESCR, 9, NULL, 0);
    TEST_ASSERT_TRUE(ok); // built + "sent" through the capturing stub

    const uint8_t *d = pc_udp_captured();
    size_t n = pc_udp_captured_len();
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_EQUAL_HEX8(0x30, d[0]); // outer SEQUENCE
    // Skip the (possibly long-form) outer length to reach the msgVersion INTEGER.
    size_t off = 1;
    uint8_t l0 = d[off++];
    if (l0 & 0x80)
    {
        off += (l0 & 0x7F);
    }
    TEST_ASSERT_TRUE(d[off] == 0x02 && d[off + 1] == 0x01 && d[off + 2] == 0x03); // msgVersion = 3
    TEST_ASSERT_TRUE(find_inform_with_reqid(d, n, reqid));                        // InformRequest PDU + our request-id
}

// A noAuthNoPriv message with the matching engine ID is a no-op (returns 0), and every
// truncated prefix of it fails closed at its parse boundary - sweeping the message-layer
// and security-parameter BER rejections.
void test_v3_message_structure_rejections()
{
    V3View v;
    discover(&v);
    uint8_t req[300], resp[512];
    size_t full = build_get(req, sizeof(req), PROTO_FALSE, PROTO_FALSE, v.engine_id, v.engine_id_len, v.boots, v.time,
                            "myuser", NULL, NULL, 300, 7, OID_SYSDESCR, 9);
    TEST_ASSERT_TRUE(full > 0);
    TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(req, full, resp, sizeof(resp))); // noAuthNoPriv non-discovery
    for (size_t L = 0; L < full; L++)
    {
        TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(req, L, resp, sizeof(resp)));
    }

    // A message asserting privacy without authentication is invalid (RFC 3414).
    uint8_t pk[SNMP_USM_KEY_LEN] = {0};
    size_t pl = build_get(req, sizeof(req), PROTO_FALSE, PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                          "myuser", NULL, pk, 300, 8, OID_SYSDESCR, 9);
    TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(req, pl, resp, sizeof(resp)));
}

// pc_snmp_v3_init with a valid engine ID copies it; the boots accessor round-trips.
void test_v3_init_and_boots_accessors()
{
    V3View v;
    discover(&v); // capture the default engine ID
    uint8_t custom[8] = {0x80, 0, 0, 0, 1, 2, 3, 4};
    pc_snmp_v3_init(custom, sizeof(custom));       // valid engine ID -> memcpy path
    pc_snmp_v3_init(v.engine_id, v.engine_id_len); // restore the default engine ID
    pc_snmp_v3_set_user("myuser", "authpass12", "privpass12");

    uint32_t saved = pc_snmp_v3_get_boots();
    pc_snmp_v3_set_boots(0xABCD);
    TEST_ASSERT_EQUAL_UINT32(0xABCD, pc_snmp_v3_get_boots());
    pc_snmp_v3_set_boots(saved);
}

// An authPriv message to a wrong engine ID takes the discovery path (with the private
// request-id read short-circuited); a discovery whose Report will not fit fails closed.
void test_v3_discovery_variants()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t wrong_eid[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33};
    uint8_t privkey[SNMP_USM_KEY_LEN] = {0};

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_TRUE, wrong_eid, sizeof(wrong_eid), v.boots, v.time,
                          "myuser", authkey, privkey, 300, 9, OID_SYSDESCR, 9);
    TEST_ASSERT_TRUE(pc_snmp_v3_process(req, rl, resp, sizeof(resp)) > 0); // discovery Report

    rl = build_get(req, sizeof(req), PROTO_FALSE, PROTO_FALSE, wrong_eid, sizeof(wrong_eid), 0, 0, "", NULL, NULL, 300,
                   10, OID_SYSDESCR, 9);
    TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(req, rl, resp, 20)); // Report does not fit -> fail closed
}

// An authPriv request when privacy is not configured on the agent is a decryptionError.
void test_v3_priv_not_configured()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", v.engine_id, v.engine_id_len, authkey);
    pc_snmp_v3_set_user("myuser", "authpass12", ""); // auth only
    uint8_t privkey[SNMP_USM_KEY_LEN] = {0};

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                          "myuser", authkey, privkey, 300, 11, OID_SYSDESCR, 9);
    size_t n = pc_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    pc_snmp_v3_set_user("myuser", "authpass12", "privpass12"); // restore
}

// A v3 notification is refused without authentication, and builds + sends with it.
void test_v3_notify_paths()
{
    uint32_t trap_oid[] = {1, 3, 6, 1, 4, 1, 49374, 0, 1};
    pc_snmp_v3_set_user("myuser", "", ""); // no auth
    TEST_ASSERT_FALSE(pc_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, NULL, 0));

    pc_snmp_v3_set_user("myuser", "authpass12", "privpass12");
    pc_udp_capture_enable();
    TEST_ASSERT_TRUE(pc_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, NULL, 0));
    TEST_ASSERT_TRUE(pc_udp_captured_len() > 0);
}

// Build a v3 message wrapping a caller-supplied (possibly malformed) *plaintext*
// scopedPDU, with a valid outer frame + USM secparams and, when auth==true, a valid
// HMAC digest. Lets a test drive the scopedPDU-parse rejects that a well-formed
// scopedPDU (as build_get emits) can never reach.
static size_t build_v3_raw_scoped(uint8_t *out, size_t cap, proto_bool auth, const uint8_t *eid, size_t eid_len,
                                  long boots, long time, const char *user, const uint8_t *authkey, long msg_id,
                                  const uint8_t *scoped, size_t scoped_len, proto_bool priv, size_t auth_plen,
                                  size_t priv_plen)
{
    proto_bool digest =
        auth && auth_plen == SNMP_V3_AUTH_PARAM_LEN; // a non-standard authParams length is rejected pre-HMAC
    uint8_t salt[SNMP_V3_PRIV_PARAM_LEN] = {0, 0, 0, 0, 0, 0, 0, 7};
    uint8_t secp[128];
    BerEnc se2;
    pc_ber_enc_init(&se2, secp, sizeof(secp));
    size_t s2 = pc_ber_seq_begin(&se2, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, eid, eid_len);
    pc_ber_put_integer(&se2, boots);
    pc_ber_put_integer(&se2, time);
    pc_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)user, strlen(user));
    size_t auth_off = 0;
    if (auth)
    {
        auth_off = se2.len + 2;
        uint8_t z[SNMP_V3_AUTH_PARAM_LEN] = {0};
        pc_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, z, auth_plen);
    }
    else
    {
        pc_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (priv)
    {
        pc_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, salt, priv_plen);
    }
    else
    {
        pc_ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    pc_ber_seq_end(&se2, s2);

    BerEnc e;
    pc_ber_enc_init(&e, out, cap);
    size_t msg = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_integer(&e, (int)SNMP_V3);
    size_t hdr = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_integer(&e, msg_id);
    pc_ber_put_integer(&e, 65507);
    uint8_t fl = (uint8_t)((auth ? 0x01 : 0) | (priv ? 0x02 : 0) | (auth ? 0 : 0x04)); // reportable on discovery
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &fl, 1);
    pc_ber_put_integer(&e, 3);
    pc_ber_seq_end(&e, hdr);
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, secp, se2.len);
    size_t sec_value_pos = e.len - se2.len;
    pc_ber_put_raw(&e, scoped, scoped_len); // scopedPDU / msgData carried verbatim (may be deliberately malformed)
    pc_ber_seq_end(&e, msg);
    if (!e.ok)
    {
        return 0;
    }
    if (digest)
    {
        uint8_t mac[PC_HMAC_SHA256_LEN];
        pc_hmac_sha256(authkey, SNMP_USM_KEY_LEN, out, e.len, mac);
        memcpy(out + sec_value_pos + auth_off, mac, SNMP_V3_AUTH_PARAM_LEN);
    }
    return e.len;
}

// Corrupt exactly one field of a well-formed noAuthNoPriv message (keeping the outer
// length valid) so the message-layer / secparams parse fails closed at that precise
// step. Truncation can't reach these: it trips the outer-length bound-check first.
void test_v3_field_tag_corruption(void)
{
    V3View v;
    discover(&v);
    uint8_t req[300], resp[512];
    size_t full = build_get(req, sizeof(req), PROTO_FALSE, PROTO_FALSE, v.engine_id, v.engine_id_len, v.boots, v.time,
                            "myuser", NULL, NULL, 300, 7, OID_SYSDESCR, 9);
    TEST_ASSERT_TRUE(full > 0);

    // Walk the message exactly as pc_snmp_v3_process does, recording each field's offset.
    BerDec d;
    pc_ber_dec_init(&d, req, full);
    uint8_t tag;
    size_t l;
    long tmp;
    pc_ber_read_header(&d, &tag, &l); // outer SEQUENCE
    size_t ver_tag = d.pos;
    pc_ber_read_integer(&d, &tmp);
    size_t gdata_tag = d.pos;
    pc_ber_read_header(&d, &tag, &l); // msgGlobalData SEQUENCE
    size_t msgid_tag = d.pos;
    pc_ber_read_integer(&d, &tmp); // msgID
    pc_ber_read_integer(&d, &tmp); // msgMaxSize
    size_t flags_tag = d.pos;
    pc_ber_read_header(&d, &tag, &l);
    d.pos += l; // msgFlags OCTET STRING
    size_t secmodel_tag = d.pos;
    pc_ber_read_integer(&d, &tmp); // msgSecurityModel
    size_t secp_tag = d.pos;
    pc_ber_read_header(&d, &tag, &l); // msgSecurityParameters OCTET STRING
    size_t base = d.pos;              // secparams content begins here (== inner BerDec buf)
    BerDec sd;
    pc_ber_dec_init(&sd, req + base, l);
    size_t sseq_tag = base + sd.pos;
    pc_ber_read_header(&sd, &tag, &l); // secparams SEQUENCE
    size_t eid_tag = base + sd.pos;
    pc_ber_read_header(&sd, &tag, &l);
    sd.pos += l; // engineID OCTET STRING
    size_t boots_tag = base + sd.pos;
    pc_ber_read_integer(&sd, &tmp); // engineBoots
    pc_ber_read_integer(&sd, &tmp); // engineTime
    size_t uname_tag = base + sd.pos;
    pc_ber_read_header(&sd, &tag, &l);
    sd.pos += l; // userName OCTET STRING
    size_t aparm_tag = base + sd.pos;
    pc_ber_read_header(&sd, &tag, &l);
    sd.pos += l; // authParams OCTET STRING
    size_t pparm_tag = base + sd.pos;

    // (offset, mutated-byte value) pairs. 0xFF as a tag is an unexpected tag; a value
    // byte set to 0x04 flips version/securityModel off their required constants.
    struct
    {
        size_t off;
        uint8_t val;
    } muts[] = {
        {ver_tag, 0xFF},          // msgVersion !INTEGER     -> read_integer rejects the tag
        {ver_tag + 2, 0x04},      // msgVersion != 3        -> line 319
        {gdata_tag, 0xFF},        // msgGlobalData !SEQUENCE -> line 323
        {msgid_tag, 0xFF},        // msgID !INTEGER          -> line 326
        {flags_tag, 0xFF},        // msgFlags !OCTET STRING  -> line 330
        {secmodel_tag + 2, 0x04}, // securityModel != 3  -> line 334
        {secp_tag, 0xFF},         // secParams !OCTET STRING -> line 343
        {sseq_tag, 0xFF},         // secParams inner !SEQUENCE-> line 350
        {eid_tag, 0xFF},          // engineID !OCTET STRING  -> line 353
        {boots_tag, 0xFF},        // engineBoots !INTEGER    -> line 358
        {uname_tag, 0xFF},        // userName !OCTET STRING  -> line 361
        {aparm_tag, 0xFF},        // authParams !OCTET STRING-> line 366
        {pparm_tag, 0xFF},        // privParams !OCTET STRING-> line 372
    };
    for (size_t i = 0; i < sizeof(muts) / sizeof(muts[0]); i++)
    {
        uint8_t bad[300];
        memcpy(bad, req, full);
        bad[muts[i].off] = muts[i].val;
        TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(bad, full, resp, sizeof(resp)));
    }
}

// An authenticated message whose (valid-HMAC) scopedPDU is malformed is rejected at
// each layer of the scopedPDU split.
void test_v3_scoped_parse_rejections(void)
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t req[320], resp[512];

    const uint8_t not_seq[] = {0x04, 0x01, 0x00};                                // scopedPDU not a SEQUENCE
    const uint8_t bad_eid[] = {0x30, 0x03, 0x02, 0x01, 0x00};                    // contextEngineID not OCTET STRING
    const uint8_t bad_ctx[] = {0x30, 0x06, 0x04, 0x01, 0x00, 0x02, 0x01, 0x00};  // contextName not OCTET STRING
    const uint8_t no_pdu[] = {0x30, 0x06, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00};   // missing inner PDU
    const uint8_t empty_pdu[] = {0x30, 0x08, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, // eid, ctxName,
                                 0xA0, 0x00}; // GET PDU with empty body -> dispatch 0
    const uint8_t *scopeds[] = {not_seq, bad_eid, bad_ctx, no_pdu, empty_pdu};
    const size_t lens[] = {sizeof(not_seq), sizeof(bad_eid), sizeof(bad_ctx), sizeof(no_pdu), sizeof(empty_pdu)};
    for (size_t i = 0; i < sizeof(scopeds) / sizeof(scopeds[0]); i++)
    {
        size_t rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                                        "myuser", authkey, 400 + (long)i, scopeds[i], lens[i], PROTO_FALSE,
                                        SNMP_V3_AUTH_PARAM_LEN, SNMP_V3_PRIV_PARAM_LEN);
        TEST_ASSERT_TRUE(rl > 0);
        TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(req, rl, resp, sizeof(resp)));
    }
}

// The best-effort inner request-id probe on the discovery path tolerates a malformed
// or non-INTEGER scopedPDU (a Report is still emitted, with request-id 0).
void test_v3_discovery_malformed_scoped(void)
{
    V3View v;
    discover(&v);
    uint8_t wrong_eid[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33};
    uint8_t req[320], resp[512];

    const uint8_t not_seq[] = {0x04, 0x01, 0x00};                                  // parse_scoped fails -> request-id 0
    const uint8_t non_int_rid[] = {0x30, 0x0B, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, // eid, ctxName,
                                   0xA0, 0x03, 0x04, 0x01, 0x00}; // PDU whose request-id is not INTEGER
    const uint8_t *scopeds[] = {not_seq, non_int_rid};
    const size_t lens[] = {sizeof(not_seq), sizeof(non_int_rid)};
    for (size_t i = 0; i < 2; i++)
    {
        size_t rl = build_v3_raw_scoped(req, sizeof(req), PROTO_FALSE, wrong_eid, sizeof(wrong_eid), v.boots, v.time,
                                        "", NULL, 410 + (long)i, scopeds[i], lens[i], PROTO_FALSE,
                                        SNMP_V3_AUTH_PARAM_LEN, SNMP_V3_PRIV_PARAM_LEN);
        TEST_ASSERT_TRUE(rl > 0);
        // Engine mismatch -> a discovery Report is emitted regardless of the probe result.
        TEST_ASSERT_TRUE(pc_snmp_v3_process(req, rl, resp, sizeof(resp)) > 0);
    }
}

// USM authentication edge rejects: an authParams field of the wrong length is a
// wrong-digest (rejected before the HMAC is even computed), and an authPriv message
// whose encrypted msgData is not wrapped in an OCTET STRING is dropped.
void test_v3_auth_edge_rejections(void)
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t req[320], resp[512];

    // authParams length != 24 -> usmStatsWrongDigests Report (short-circuits before HMAC).
    const uint8_t any_scoped[] = {0x30, 0x02, 0x04, 0x00};
    size_t rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                                    "myuser", authkey, 420, any_scoped, sizeof(any_scoped), PROTO_FALSE,
                                    16 /*bad authParams len*/, SNMP_V3_PRIV_PARAM_LEN);
    TEST_ASSERT_TRUE(rl > 0);
    size_t n = pc_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(5u, r.oid[9]); // usmStatsWrongDigests

    // authPriv with a valid digest but msgData that is not an OCTET STRING -> dropped (0).
    const uint8_t not_octet[] = {0x02, 0x01, 0x00};
    rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time, "myuser",
                             authkey, 421, not_octet, sizeof(not_octet), PROTO_TRUE /*priv*/, SNMP_V3_AUTH_PARAM_LEN,
                             SNMP_V3_PRIV_PARAM_LEN);
    TEST_ASSERT_TRUE(rl > 0);
    TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(req, rl, resp, sizeof(resp)));
}

// A notification whose varbind value is too large fails the inner PDU / scopedPDU build. Sweeping the
// value length across the 1472-byte inner-buffer boundary drives both overflow guards: below it the
// PDU fits but the scopedPDU wrapper overruns; above it the PDU itself overruns.
void test_v3_notify_overflow_guards()
{
    pc_snmp_v3_set_user("myuser", "authpass12", "privpass12");
    pc_udp_capture_enable();
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

    vb.blen = 32; // small: builds + sends fine
    TEST_ASSERT_TRUE(pc_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, &vb, 1));
    vb.blen = 1550; // huge: the inner PDU overflows
    TEST_ASSERT_FALSE(pc_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, &vb, 1));
    for (size_t blen = 1360; blen <= 1480; blen += 2) // crosses the fit/overflow boundary for both buffers
    {
        vb.blen = blen;
        (void)pc_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, &vb, 1);
    }
}

// A GET whose value is large enough that the *response* PDU still fits the inner buffer but
// wrapping it into the response scopedPDU (our engineID + contextName + response PDU) overruns
// v3_c[SNMP_MSG_BUF_SIZE] exercises pc_snmp_v3_process's response-scopedPDU overflow guard. The
// value length is served by a dynamic getter so the test controls it exactly; sweeping across
// the 1472-byte boundary hits the wrapper-overflow branch (return 0) regardless of the precise
// BER overhead: below the window the whole response fits, above it the inner PDU itself overruns,
// and in between the response fits but its scopedPDU wrapper does not.
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
    TEST_ASSERT_TRUE(pc_snmp_agent_add_dynamic(big_oid, 9, (uint8_t)SNMP_TAG_BER_OCTET_STRING, big_getter));

    V3View disc;
    discover(&disc);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", disc.engine_id, disc.engine_id_len, authkey);

    uint8_t req[300], resp[2048];
    proto_bool saw_overflow = PROTO_FALSE, saw_ok = PROTO_FALSE;
    for (size_t vlen = 1400; vlen <= 1472; vlen++) // crosses the fit/overflow boundary for both inner buffers
    {
        g_big_len = vlen;
        size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                              disc.time, "myuser", authkey, NULL, 500, 88, big_oid, 9);
        TEST_ASSERT_TRUE(rl > 0);
        size_t n = pc_snmp_agent_process(req, rl, resp, sizeof(resp));
        if (n == 0)
        {
            saw_overflow = PROTO_TRUE; // response fits v3_b but its scopedPDU wrapper overruns v3_c -> guarded return 0
        }
        else
        {
            saw_ok = PROTO_TRUE; // a smaller value still yields a valid authenticated response
        }
    }
    TEST_ASSERT_TRUE(saw_overflow);
    TEST_ASSERT_TRUE(saw_ok);
}

// ==================== message-frame rejection helpers ====================

// Build a v3 message whose msgSecurityParameters content and msgData tail are supplied
// verbatim, so a test can truncate the USM parameters at any field boundary.
static size_t build_v3_frame(uint8_t *out, size_t cap, long msg_id, uint8_t flags, const uint8_t *sec, size_t sec_len,
                             const uint8_t *tail, size_t tail_len)
{
    BerEnc e;
    pc_ber_enc_init(&e, out, cap);
    size_t msg = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_integer(&e, (int)SNMP_V3);
    size_t hdr = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_integer(&e, msg_id);
    pc_ber_put_integer(&e, 65507);
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &flags, 1);
    pc_ber_put_integer(&e, 3);
    pc_ber_seq_end(&e, hdr);
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, sec, sec_len);
    pc_ber_put_raw(&e, tail, tail_len);
    pc_ber_seq_end(&e, msg);
    return e.ok ? e.len : 0;
}

// msgSecurityParameters holding only the first @p nfields of the six USM fields
// (engineID, engineBoots, engineTime, userName, authParams, privParams).
static size_t build_secparams_prefix(uint8_t *out, size_t cap, unsigned nfields)
{
    BerEnc e;
    pc_ber_enc_init(&e, out, cap);
    size_t s = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    if (nfields >= 1)
    {
        pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (nfields >= 2)
    {
        pc_ber_put_integer(&e, 1);
    }
    if (nfields >= 3)
    {
        pc_ber_put_integer(&e, 0);
    }
    if (nfields >= 4)
    {
        pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (nfields >= 5)
    {
        pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    pc_ber_seq_end(&e, s);
    return e.ok ? e.len : 0;
}

// A v3 message that ends inside msgGlobalData, or whose msgSecurityParameters run out
// part-way through the six USM fields, is dropped at exactly that field - the parse never
// reads past the bytes the datagram actually carries.
void test_v3_truncated_fields_fail_closed()
{
    uint8_t req[320], resp[512];

    // Truncated after version / msgID / msgMaxSize / msgFlags / msgSecurityModel.
    for (int stop = 0; stop <= 4; stop++)
    {
        BerEnc e;
        pc_ber_enc_init(&e, req, sizeof(req));
        size_t msg = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
        pc_ber_put_integer(&e, (int)SNMP_V3);
        if (stop >= 1)
        {
            size_t hdr = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
            pc_ber_put_integer(&e, 500 + stop); // msgID
            if (stop >= 2)
            {
                pc_ber_put_integer(&e, 65507); // msgMaxSize
            }
            if (stop >= 3)
            {
                uint8_t fl = 0x04;
                pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &fl, 1);
            }
            if (stop >= 4)
            {
                pc_ber_put_integer(&e, 3); // msgSecurityModel
            }
            pc_ber_seq_end(&e, hdr);
        }
        pc_ber_seq_end(&e, msg);
        TEST_ASSERT_TRUE(e.ok);
        TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(req, e.len, resp, sizeof(resp)));
    }

    // msgSecurityParameters that is not even a SEQUENCE (zero-length), then one truncated
    // after each of the first five USM fields.
    const uint8_t tail[] = {0x30, 0x02, 0x04, 0x00};
    size_t fl = build_v3_frame(req, sizeof(req), 510, 0x04, NULL, 0, tail, sizeof(tail));
    TEST_ASSERT_TRUE(fl > 0);
    TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(req, fl, resp, sizeof(resp)));
    for (unsigned nf = 0; nf <= 5; nf++)
    {
        uint8_t sec[64];
        size_t sl = build_secparams_prefix(sec, sizeof(sec), nf);
        TEST_ASSERT_TRUE(sl > 0);
        size_t n = build_v3_frame(req, sizeof(req), 520 + (long)nf, 0x04, sec, sl, tail, sizeof(tail));
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(req, n, resp, sizeof(resp)));
    }
}

// The outer TLV must be a SEQUENCE, and msgFlags must carry at least the one flag octet
// the parse reads - a zero-length msgFlags is rejected instead of read out of bounds.
void test_v3_outer_tag_and_empty_flags()
{
    uint8_t resp[512];
    const uint8_t not_seq[] = {0x04, 0x02, 0x00, 0x00}; // a well-formed OCTET STRING, not a SEQUENCE
    TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(not_seq, sizeof(not_seq), resp, sizeof(resp)));

    uint8_t req[320];
    BerEnc e;
    pc_ber_enc_init(&e, req, sizeof(req));
    size_t msg = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_integer(&e, (int)SNMP_V3);
    size_t hdr = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_integer(&e, 530);
    pc_ber_put_integer(&e, 65507);
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0); // msgFlags, zero length
    pc_ber_put_integer(&e, 3);
    pc_ber_seq_end(&e, hdr);
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    pc_ber_seq_end(&e, msg);
    TEST_ASSERT_TRUE(e.ok);
    TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(req, e.len, resp, sizeof(resp)));
}

// parse_scoped rejects a scopedPDU that ends inside any of its three headers (the
// SEQUENCE, contextEngineID or contextName), not just one carrying a wrong tag.
void test_v3_scoped_truncated_headers()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t req[320], resp[512];

    const uint8_t no_len[] = {0x30};                     // SEQUENCE tag, no length octet
    const uint8_t empty_seq[] = {0x30, 0x00};            // SEQUENCE, no contextEngineID
    const uint8_t eid_only[] = {0x30, 0x02, 0x04, 0x00}; // contextEngineID, no contextName
    const uint8_t *scopeds[] = {no_len, empty_seq, eid_only};
    const size_t lens[] = {sizeof(no_len), sizeof(empty_seq), sizeof(eid_only)};
    for (unsigned i = 0; i < 3; i++)
    {
        size_t rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                                        "myuser", authkey, 540 + (long)i, scopeds[i], lens[i], PROTO_FALSE,
                                        SNMP_V3_AUTH_PARAM_LEN, SNMP_V3_PRIV_PARAM_LEN);
        TEST_ASSERT_TRUE(rl > 0);
        TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(req, rl, resp, sizeof(resp)));
    }
}

// A wrong engine ID of the *same length* as ours is still a mismatch (the compare is on
// the bytes, not just the length) and takes the discovery path.
void test_v3_same_length_wrong_engine_id()
{
    V3View v;
    discover(&v);
    uint8_t wrong[SNMP_V3_ENGINEID_MAX];
    TEST_ASSERT_TRUE(v.engine_id_len <= sizeof(wrong));
    memcpy(wrong, v.engine_id, v.engine_id_len);
    wrong[v.engine_id_len - 1] ^= 0xFF; // same length, one byte different

    uint8_t req[320], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_FALSE, PROTO_FALSE, wrong, v.engine_id_len, 0, 0, "", NULL, NULL, 550,
                          1, OID_SYSDESCR, 9);
    size_t n = pc_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(4u, r.oid[9]); // usmStatsUnknownEngineIDs
}

// Every way of failing the USM user match reports usmStatsUnknownUserNames: no auth user
// configured at all, a name of the wrong length, and an empty configured user name.
void test_v3_unknown_user_variants()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t req[320], resp[512];
    V3View r;

    struct
    {
        const char *cfg_user;
        const char *cfg_auth;
        const char *req_user;
    } cases[] = {
        {"myuser", "", "myuser"},        // no auth secret configured -> auth_set false
        {"myuser", "authpass12", "bob"}, // configured name is a different length
        {"", "authpass12", ""},          // configured user name is empty
    };
    for (unsigned i = 0; i < 3; i++)
    {
        pc_snmp_v3_set_user(cases[i].cfg_user, cases[i].cfg_auth, "");
        size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, v.engine_id, v.engine_id_len, v.boots, v.time,
                              cases[i].req_user, authkey, NULL, 560 + (long)i, 3, OID_SYSDESCR, 9);
        TEST_ASSERT_TRUE(rl > 0);
        size_t n = pc_snmp_v3_process(req, rl, resp, sizeof(resp));
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
        TEST_ASSERT_EQUAL_UINT32(3u, r.oid[9]); // usmStatsUnknownUserNames
    }
}

// A message larger than the agent's authentication scratch cannot be digest-checked, so it
// is refused as a wrong-digest rather than truncated into the buffer.
void test_v3_oversized_message_is_wrong_digest()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", v.engine_id, v.engine_id_len, authkey);

    static uint8_t scoped[SNMP_MSG_BUF_SIZE + 64];
    memset(scoped, 0x00, sizeof(scoped));
    scoped[0] = 0x30; // shape is irrelevant: the size check fires before any parse
    static uint8_t req[SNMP_MSG_BUF_SIZE + 256];
    uint8_t resp[512];
    size_t rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                                    "myuser", authkey, 570, scoped, sizeof(scoped), PROTO_FALSE, SNMP_V3_AUTH_PARAM_LEN,
                                    SNMP_V3_PRIV_PARAM_LEN);
    TEST_ASSERT_TRUE(rl > SNMP_MSG_BUF_SIZE); // genuinely past the scratch buffer
    size_t n = pc_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(5u, r.oid[9]); // usmStatsWrongDigests
}

// Timeliness rejects a boots counter that does not match ours even when engineTime is
// current (RFC 3414 3.2: both must agree, not just the time).
void test_v3_boots_mismatch_not_in_time()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", v.engine_id, v.engine_id_len, authkey);

    uint8_t req[320], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, v.engine_id, v.engine_id_len, v.boots + 7, v.time,
                          "myuser", authkey, NULL, 580, 4, OID_SYSDESCR, 9);
    size_t n = pc_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(2u, r.oid[9]);        // usmStatsNotInTimeWindows
    TEST_ASSERT_EQUAL_UINT8(0x01, r.flags & 0x03); // and it is authenticated
}

// Privacy edges on an otherwise valid authPriv request: a salt that is not the 8-byte
// usmAesCfb128 privacy parameter is a decryptionError, and encrypted msgData too short to
// even hold a TLV header is dropped.
void test_v3_privacy_parameter_edges()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t req[320], resp[512];

    const uint8_t any[] = {0x04, 0x02, 0x00, 0x00};
    size_t rl =
        build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time, "myuser",
                            authkey, 590, any, sizeof(any), PROTO_TRUE, SNMP_V3_AUTH_PARAM_LEN, 4 /* short salt */);
    TEST_ASSERT_TRUE(rl > 0);
    size_t n = pc_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(6u, r.oid[9]); // usmStatsDecryptionErrors

    const uint8_t stub[] = {0x04}; // OCTET STRING tag with no length octet
    rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time, "myuser",
                             authkey, 591, stub, sizeof(stub), PROTO_TRUE, SNMP_V3_AUTH_PARAM_LEN,
                             SNMP_V3_PRIV_PARAM_LEN);
    TEST_ASSERT_TRUE(rl > 0);
    TEST_ASSERT_EQUAL_UINT(0, pc_snmp_v3_process(req, rl, resp, sizeof(resp)));
}

// pc_snmp_v3_init only adopts an engine ID of a legal length (5..SNMP_V3_ENGINEID_MAX);
// anything shorter or longer leaves the previous engine ID in place. Configuring a null
// user clears the user and both secrets.
void test_v3_init_length_guards_and_null_user()
{
    V3View before;
    discover(&before);

    uint8_t tooshort[4] = {0x80, 0x00, 0x00, 0x01};
    pc_snmp_v3_init(tooshort, sizeof(tooshort));
    V3View after;
    discover(&after);
    TEST_ASSERT_EQUAL_UINT(before.engine_id_len, after.engine_id_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before.engine_id, after.engine_id, before.engine_id_len);

    uint8_t toolong[SNMP_V3_ENGINEID_MAX + 1];
    memset(toolong, 0xA5, sizeof(toolong));
    pc_snmp_v3_init(toolong, sizeof(toolong));
    discover(&after);
    TEST_ASSERT_EQUAL_UINT(before.engine_id_len, after.engine_id_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before.engine_id, after.engine_id, before.engine_id_len);

    // A null user name / null secrets leave no usable USM user: an authenticated request
    // for any name is answered with usmStatsUnknownUserNames.
    pc_snmp_v3_set_user(NULL, NULL, NULL);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    pc_snmp_usm_localize_key("authpass12", after.engine_id, after.engine_id_len, authkey);
    uint8_t req[320], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, after.engine_id, after.engine_id_len, after.boots,
                          after.time, "myuser", authkey, NULL, 600, 5, OID_SYSDESCR, 9);
    size_t n = pc_snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(3u, r.oid[9]);
}

// A v3 trap reports failure when the transport refuses the datagram. The host UDP stub
// only accepts a send while capture is armed, so this test MUST run before any test that
// calls pc_udp_capture_enable() - there is no way to disarm it again.
void test_v3_trap_reports_transport_failure()
{
    uint32_t trap_oid[] = {1, 3, 6, 1, 4, 1, 49374, 0, 1};
    TEST_ASSERT_EQUAL_size_t(0, pc_udp_captured_len()); // capture still disarmed
    TEST_ASSERT_FALSE(pc_snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, NULL, 0));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_v3_trap_reports_transport_failure); // must precede any pc_udp_capture_enable()
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
