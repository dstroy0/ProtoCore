// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/mac/hmac_sha256/hmac_sha256.h"
#include "network_drivers/transport/udp/udp.h"
#include "services/net/snmp/snmp_agent/snmp_agent.h"
#include "services/net/snmp/snmp_ber/snmp_ber.h"
#include "services/net/snmp/snmp_crypto/snmp_crypto.h"
#include "services/net/snmp/snmp_notify/snmp_notify.h"
#include "services/net/snmp/snmp_v3/snmp_v3.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>

static uint8_t snmp_crypto_work[16]; // the borrow an entry takes; SnmpCrypto never reads it

static uint8_t snmp_ber_work[16]; // the borrow an entry takes; SnmpBer never reads it

// USM key localization (RFC 3414 sec 2.6), reached through the crypto namespace. The hash borrows
// the caller's scratch, so the work region is a member like everything else.
static proto_bool snmp_localize_key(uint8_t *work, const char *password, const uint8_t *engine_id, size_t engine_id_len,
                                    uint8_t *out)
{
    SnmpCryptoV.work = work;
    SnmpCryptoV.key.password = password;
    SnmpCryptoV.key.engine_id = engine_id;
    SnmpCryptoV.key.engine_id_len = engine_id_len;
    SnmpCryptoV.key.out = out;
    SnmpCrypto.localize_key(snmp_crypto_work);
    return SnmpCryptoV.ok;
}

// The SNMPv3 / USM message path, reached through its namespace.
static proto_bool snmp_v3_init(const uint8_t *engine_id, size_t engine_id_len)
{
    SnmpV3V.engine.engine_id = engine_id;
    SnmpV3V.engine.engine_id_len = engine_id_len;
    SnmpV3.init(protocore_snmp_v3_span());
    return SnmpV3V.ok;
}

static proto_bool snmp_v3_set_user(const char *user, const char *auth_pass, const char *priv_pass)
{
    SnmpV3V.user.user = user;
    SnmpV3V.user.auth_pass = auth_pass;
    SnmpV3V.user.priv_pass = priv_pass;
    SnmpV3.set_user(protocore_snmp_v3_span());
    return SnmpV3V.ok;
}

static void snmp_v3_set_boots(uint32_t boots)
{
    SnmpV3V.engine.boots = boots;
    SnmpV3.set_boots(protocore_snmp_v3_span());
}

static uint32_t snmp_v3_get_boots(void)
{
    SnmpV3.get_boots(protocore_snmp_v3_span());
    return SnmpV3V.u32;
}

static size_t snmp_v3_process(const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap)
{
    SnmpV3V.msg.req = req;
    SnmpV3V.msg.req_len = req_len;
    SnmpV3V.msg.resp = resp;
    SnmpV3V.msg.resp_cap = resp_cap;
    SnmpV3.process(protocore_snmp_v3_span());
    return SnmpV3V.n;
}

static void snmp_v3_notify(const char *dst_ip, uint16_t port, uint32_t request_id, const uint32_t *trap_oid,
                           size_t trap_oid_len, const SnmpVarbind *vbs, size_t vb_count)
{
    SnmpV3V.notify.dst_ip = dst_ip;
    SnmpV3V.notify.port = port;
    SnmpV3V.notify.request_id = request_id;
    SnmpV3V.notify.trap_oid = trap_oid;
    SnmpV3V.notify.trap_oid_len = trap_oid_len;
    SnmpV3V.notify.vbs = vbs;
    SnmpV3V.notify.vb_count = vb_count;
}

static proto_bool snmp_trap_v3(const char *dst_ip, uint16_t port, const uint32_t *trap_oid, size_t trap_oid_len,
                               const SnmpVarbind *vbs, size_t vb_count)
{
    snmp_v3_notify(dst_ip, port, 0u, trap_oid, trap_oid_len, vbs, vb_count);
    SnmpV3.trap(protocore_snmp_v3_span());
    return SnmpV3V.ok;
}

static proto_bool snmp_inform_v3(const char *dst_ip, uint16_t port, uint32_t request_id, const uint32_t *trap_oid,
                                 size_t trap_oid_len, const SnmpVarbind *vbs, size_t vb_count)
{
    snmp_v3_notify(dst_ip, port, request_id, trap_oid, trap_oid_len, vbs, vb_count);
    SnmpV3.inform(protocore_snmp_v3_span());
    return SnmpV3V.ok;
}

// The BER codec, reached through its namespace. The cursor stays the caller's: several encodings
// are open at once here, so each call names the one it acts on.
static void ber_enc_init(BerEnc *e, uint8_t *buf, size_t cap)
{
    SnmpBerV.enc = e;
    SnmpBerV.buf.out = buf;
    SnmpBerV.buf.cap = cap;
    SnmpBer.enc_init(snmp_ber_work);
}

static void ber_dec_init(BerDec *d, const uint8_t *buf, size_t len)
{
    SnmpBerV.dec = d;
    SnmpBerV.buf.in = buf;
    SnmpBerV.buf.cap = len;
    SnmpBer.dec_init(snmp_ber_work);
}

static size_t ber_seq_begin(BerEnc *e, uint8_t tag)
{
    SnmpBerV.enc = e;
    SnmpBerV.tlv.tag = tag;
    SnmpBer.seq_begin(snmp_ber_work);
    return SnmpBerV.tlv.token;
}

static void ber_seq_end(BerEnc *e, size_t token)
{
    SnmpBerV.enc = e;
    SnmpBerV.tlv.token = token;
    SnmpBer.seq_end(snmp_ber_work);
}

static void ber_put_octet_string(BerEnc *e, uint8_t tag, const uint8_t *bytes, size_t len)
{
    SnmpBerV.enc = e;
    SnmpBerV.tlv.tag = tag;
    SnmpBerV.tlv.bytes = bytes;
    SnmpBerV.tlv.len = len;
    SnmpBer.put_octet_string(snmp_ber_work);
}

static void ber_put_null(BerEnc *e)
{
    SnmpBerV.enc = e;
    SnmpBer.put_null(snmp_ber_work);
}

static void ber_put_oid(BerEnc *e, const uint32_t *arcs, size_t arc_count)
{
    SnmpBerV.enc = e;
    SnmpBerV.tlv.arcs = arcs;
    SnmpBerV.tlv.arc_count = arc_count;
    SnmpBer.put_oid(snmp_ber_work);
}

static void ber_put_raw(BerEnc *e, const uint8_t *bytes, size_t len)
{
    SnmpBerV.enc = e;
    SnmpBerV.tlv.bytes = bytes;
    SnmpBerV.tlv.len = len;
    SnmpBer.put_raw(snmp_ber_work);
}

static void ber_put_uint(BerEnc *e, uint8_t tag, uint32_t uval)
{
    SnmpBerV.enc = e;
    SnmpBerV.tlv.tag = tag;
    SnmpBerV.tlv.uval = uval;
    SnmpBer.put_uint(snmp_ber_work);
}

static void ber_put_integer(BerEnc *e, long ival)
{
    SnmpBerV.enc = e;
    SnmpBerV.tlv.ival = ival;
    SnmpBer.put_integer(snmp_ber_work);
}

static proto_bool ber_read_header(BerDec *d, uint8_t *tag, size_t *vlen)
{
    SnmpBerV.dec = d;
    SnmpBer.read_header(snmp_ber_work);
    if (tag)
    {
        *tag = SnmpBerV.tag;
    }
    if (vlen)
    {
        *vlen = SnmpBerV.vlen;
    }
    return SnmpBerV.ok;
}

static proto_bool ber_read_integer(BerDec *d, long *out)
{
    SnmpBerV.dec = d;
    SnmpBer.read_integer(snmp_ber_work);
    if (out)
    {
        *out = SnmpBerV.ival;
    }
    return SnmpBerV.ok;
}

static proto_bool ber_read_oid(BerDec *d, uint32_t *arc_out, size_t arc_cap, size_t *count)
{
    SnmpBerV.dec = d;
    SnmpBerV.read_args.arc_out = arc_out;
    SnmpBerV.read_args.arc_cap = arc_cap;
    SnmpBer.read_oid(snmp_ber_work);
    if (count)
    {
        *count = SnmpBerV.n;
    }
    return SnmpBerV.ok;
}

static proto_bool ber_skip(BerDec *d, size_t len)
{
    SnmpBerV.dec = d;
    SnmpBerV.read_args.skip = len;
    SnmpBerV.skip(snmp_ber_work);
    return SnmpBerV.ok;
}

// The SNMP agent, reached through its namespace: set the members a call takes, invoke it, read the
// outcome off the same handle.
static void snmp_init(const char *ro)
{
    SnmpAgentV.community.ro = ro;
    SnmpAgent.init(protocore_snmp_agent_span());
}

static void snmp_set_rw_community(const char *rw)
{
    SnmpAgentV.community.rw = rw;
    SnmpAgent.set_rw_community(protocore_snmp_agent_span());
}

static void snmp_set_system(const char *descr, const char *contact, const char *name, const char *location,
                            long services)
{
    SnmpAgentV.system.descr = descr;
    SnmpAgentV.system.contact = contact;
    SnmpAgentV.system.name = name;
    SnmpAgentV.system.location = location;
    SnmpAgentV.system.services = services;
    SnmpAgent.set_system(protocore_snmp_agent_span());
}

static proto_bool snmp_add_integer(const uint32_t *oid, size_t oid_len, long ival, SnmpSetFn setter)
{
    SnmpAgentV.object.oid = oid;
    SnmpAgentV.object.oid_len = oid_len;
    SnmpAgentV.object.ival = ival;
    SnmpAgentV.object.setter = setter;
    SnmpAgent.add_integer(protocore_snmp_agent_span());
    return SnmpAgentV.ok;
}

static proto_bool snmp_add_string(const uint32_t *oid, size_t oid_len, const char *text, SnmpSetFn setter)
{
    SnmpAgentV.object.oid = oid;
    SnmpAgentV.object.oid_len = oid_len;
    SnmpAgentV.object.text = text;
    SnmpAgentV.object.setter = setter;
    SnmpAgent.add_string(protocore_snmp_agent_span());
    return SnmpAgentV.ok;
}

static proto_bool snmp_add_dynamic(const uint32_t *oid, size_t oid_len, uint8_t type, SnmpGetFn getter)
{
    SnmpAgentV.object.oid = oid;
    SnmpAgentV.object.oid_len = oid_len;
    SnmpAgentV.object.type = type;
    SnmpAgentV.object.getter = getter;
    SnmpAgentV.object.setter = NULL;
    SnmpAgent.add_dynamic(protocore_snmp_agent_span());
    return SnmpAgentV.ok;
}

static proto_bool snmp_listen(uint16_t port)
{
    SnmpAgentV.port = port;
    SnmpAgent.listen(protocore_snmp_agent_span());
    return SnmpAgentV.ok;
}

static size_t snmp_process(const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap)
{
    SnmpAgentV.msg.req = req;
    SnmpAgentV.msg.req_len = req_len;
    SnmpAgentV.msg.resp = resp;
    SnmpAgentV.msg.resp_cap = resp_cap;
    SnmpAgent.process(protocore_snmp_agent_span());
    return SnmpAgentV.n;
}

// CFB128-AES-128 over the USM privacy args (RFC 3826 sec 3.1.4).
static proto_bool snmp_aes_cfb(const uint8_t *key, const uint8_t *iv, const uint8_t *in, uint8_t *out, size_t len,
                               proto_bool encrypt)
{
    SnmpCryptoV.priv.key = key;
    SnmpCryptoV.priv.iv = iv;
    SnmpCryptoV.priv.in = in;
    SnmpCryptoV.priv.out = out;
    SnmpCryptoV.priv.len = len;
    SnmpCryptoV.priv.encrypt = encrypt;
    SnmpCrypto.aes_cfb128(snmp_crypto_work);
    return SnmpCryptoV.ok;
}

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
    snmp_init("public");
    snmp_set_system(SYSDESCR_VAL, "admin", "esp32", "lab", 72);
    snmp_v3_init(NULL, 0);
    snmp_v3_set_boots(1);
    snmp_v3_set_user("myuser", "authpass12", "privpass12");
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
    snmp_localize_key(tw, "maplesyrup", engine, sizeof(engine), key);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, key, 32);
}

void test_localize_key_empty_password()
{
    const uint8_t engine[] = {0x80, 0x00, 0xC0, 0xDE, 0x05, 0x01, 0x02, 0x03, 0x04};
    const uint8_t zero[SNMP_USM_KEY_LEN] = {0};
    uint8_t key[SNMP_USM_KEY_LEN];

    memset(key, 0xAA, sizeof(key));
    snmp_localize_key(tw, "", engine, sizeof(engine), key);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, key, SNMP_USM_KEY_LEN);

    memset(key, 0xBB, sizeof(key));
    snmp_localize_key(tw, NULL, engine, sizeof(engine), key);
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
    snmp_aes_cfb(key, pt, zero, out, 16, PROTO_TRUE);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 16);
}

void test_aes_cfb_roundtrip_partial_block()
{
    const uint8_t key[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint8_t iv[16] = {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    const char *msg = "SNMPv3 scopedPDU spanning more than one AES block plus a tail.";
    size_t n = strlen(msg);
    uint8_t ct[128], pt[128];
    snmp_aes_cfb(key, iv, (const uint8_t *)msg, ct, n, PROTO_TRUE);
    snmp_aes_cfb(key, iv, ct, pt, n, PROTO_FALSE);
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
    ber_enc_init(&pe, pdu, sizeof(pdu));
    size_t pp = ber_seq_begin(&pe, (uint8_t)SNMP_TAG_SNMP_PDU_GET);
    SnmpBerV.enc = &pe;
    SnmpBerV.tlv.ival = req_id;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.enc = &pe;
    SnmpBerV.tlv.ival = 0;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.enc = &pe;
    SnmpBerV.tlv.ival = 0;
    SnmpBer.put_integer(snmp_ber_work);
    size_t vbl = ber_seq_begin(&pe, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    size_t vb = ber_seq_begin(&pe, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    ber_put_oid(&pe, oid, oid_len);
    ber_put_null(&pe);
    ber_seq_end(&pe, vb);
    ber_seq_end(&pe, vbl);
    ber_seq_end(&pe, pp);

    uint8_t scoped[320];
    BerEnc se;
    ber_enc_init(&se, scoped, sizeof(scoped));
    size_t ss = ber_seq_begin(&se, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    ber_put_octet_string(&se, (uint8_t)SNMP_TAG_BER_OCTET_STRING, eid, eid_len);
    ber_put_octet_string(&se, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    ber_put_raw(&se, pdu, pe.len);
    ber_seq_end(&se, ss);

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
        snmp_aes_cfb(privkey, iv, scoped, cipher, se.len, PROTO_TRUE);
        data = cipher;
    }

    uint8_t secp[128];
    BerEnc se2;
    ber_enc_init(&se2, secp, sizeof(secp));
    size_t s2 = ber_seq_begin(&se2, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, eid, eid_len);
    SnmpBerV.enc = &se2;
    SnmpBerV.tlv.ival = boots;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.enc = &se2;
    SnmpBerV.tlv.ival = time;
    SnmpBer.put_integer(snmp_ber_work);
    ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)user, strlen(user));
    size_t auth_off = 0;
    if (auth)
    {
        auth_off = se2.len + 2;
        uint8_t z[SNMP_V3_AUTH_PARAM_LEN] = {0};
        ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, z, SNMP_V3_AUTH_PARAM_LEN);
    }
    else
    {
        ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (priv)
    {
        ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, salt, 8);
    }
    else
    {
        ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    ber_seq_end(&se2, s2);

    BerEnc e;
    ber_enc_init(&e, out, cap);
    size_t msg = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = (int)SNMP_V3;
    SnmpBer.put_integer(snmp_ber_work);
    size_t hdr = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = msg_id;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = 65507;
    SnmpBer.put_integer(snmp_ber_work);
    uint8_t fl = (uint8_t)((auth ? 1 : 0) | (priv ? 2 : 0) | (auth ? 0 : 4));
    ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &fl, 1);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = 3;
    SnmpBer.put_integer(snmp_ber_work);
    ber_seq_end(&e, hdr);
    ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, secp, se2.len);
    size_t sec_value_pos = e.len - se2.len;
    if (priv)
    {
        ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, data, data_len);
    }
    else
    {
        ber_put_raw(&e, data, data_len);
    }
    ber_seq_end(&e, msg);

    if (auth)
    {
        uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
        HmacSha256V.mac_args.key = authkey;
        HmacSha256V.mac_args.key_len = SNMP_USM_KEY_LEN;
        HmacSha256V.mac_args.data = out;
        HmacSha256V.mac_args.len = e.len;
        HmacSha256V.mac_args.out = mac;
        HmacSha256.mac(tw);
        memcpy(out + sec_value_pos + auth_off, mac, SNMP_V3_AUTH_PARAM_LEN);
    }
    return e.ok ? e.len : 0;
}

static proto_bool parse_v3(const uint8_t *buf, size_t len, const uint8_t *privkey, V3View *v)
{
    memset(v, 0, sizeof(*v));
    BerDec d;
    ber_dec_init(&d, buf, len);
    uint8_t tag;
    size_t l;
    if (!ber_read_header(&d, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    long version;
    if (!ber_read_integer(&d, &version))
    {
        return PROTO_FALSE;
    }
    if (!ber_read_header(&d, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    long msgid, maxsize, secmodel;
    if (!ber_read_integer(&d, &msgid) || !ber_read_integer(&d, &maxsize))
    {
        return PROTO_FALSE;
    }
    size_t fl;
    if (!ber_read_header(&d, &tag, &fl) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    v->flags = d.buf[d.pos];
    d.pos += fl;
    if (!ber_read_integer(&d, &secmodel))
    {
        return PROTO_FALSE;
    }

    size_t seclen;
    if (!ber_read_header(&d, &tag, &seclen) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    BerDec sd;
    ber_dec_init(&sd, d.buf + d.pos, seclen);
    d.pos += seclen;
    if (!ber_read_header(&sd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    size_t eidl;
    if (!ber_read_header(&sd, &tag, &eidl) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    memcpy(v->engine_id, sd.buf + sd.pos, eidl < sizeof(v->engine_id) ? eidl : sizeof(v->engine_id));
    v->engine_id_len = eidl;
    sd.pos += eidl;
    if (!ber_read_integer(&sd, &v->boots) || !ber_read_integer(&sd, &v->time))
    {
        return PROTO_FALSE;
    }
    size_t ul;
    if (!ber_read_header(&sd, &tag, &ul) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    sd.pos += ul;
    size_t al;
    if (!ber_read_header(&sd, &tag, &al) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    sd.pos += al;
    size_t pl;
    if (!ber_read_header(&sd, &tag, &pl) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    const uint8_t *privparm = sd.buf + sd.pos;

    const uint8_t *scoped;
    size_t scoped_len;
    if (v->flags & 0x02)
    {
        size_t ctl;
        if (!ber_read_header(&d, &tag, &ctl) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
        {
            return PROTO_FALSE;
        }
        uint8_t iv[16];
        put_be32(iv, (uint32_t)v->boots);
        put_be32(iv + 4, (uint32_t)v->time);
        memcpy(iv + 8, privparm, 8);
        snmp_aes_cfb(privkey, iv, d.buf + d.pos, g_dec, ctl, PROTO_FALSE);
        scoped = g_dec;
        scoped_len = ctl;
    }
    else
    {
        scoped = d.buf + d.pos;
        scoped_len = len - d.pos;
    }

    BerDec cd;
    ber_dec_init(&cd, scoped, scoped_len);
    if (!ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    if (!ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    cd.pos += l;
    if (!ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    cd.pos += l;
    if (!ber_read_header(&cd, &v->pdu_tag, &l))
    {
        return PROTO_FALSE;
    }
    if (!ber_read_integer(&cd, &v->request_id) || !ber_read_integer(&cd, &v->err_status))
    {
        return PROTO_FALSE;
    }
    long erridx;
    if (!ber_read_integer(&cd, &erridx))
    {
        return PROTO_FALSE;
    }
    if (!ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    if (cd.pos < cd.len)
    {
        if (!ber_read_header(&cd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
        {
            return PROTO_FALSE;
        }
        if (!ber_read_oid(&cd, v->oid, SNMP_MAX_OID_LEN, &v->oid_len))
        {
            return PROTO_FALSE;
        }
        size_t vl;
        if (!ber_read_header(&cd, &v->val_tag, &vl))
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
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
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
    snmp_localize_key(tw, "authpass12", disc.engine_id, disc.engine_id_len, authkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time, "myuser", authkey, NULL, 200, 42, OID_SYSDESCR, 9);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
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
    snmp_localize_key(tw, "authpass12", disc.engine_id, disc.engine_id_len, authkey);
    snmp_localize_key(tw, "privpass12", disc.engine_id, disc.engine_id_len, privkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_TRUE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time, "myuser", authkey, privkey, 201, 77, OID_SYSDESCR, 9);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
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
    snmp_localize_key(tw, "wrongpass99", disc.engine_id, disc.engine_id_len, badkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time, "myuser", badkey, NULL, 202, 5, OID_SYSDESCR, 9);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
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
    snmp_localize_key(tw, "authpass12", disc.engine_id, disc.engine_id_len, authkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time, "nobody", authkey, NULL, 203, 9, OID_SYSDESCR, 9);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
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
    snmp_localize_key(tw, "authpass12", disc.engine_id, disc.engine_id_len, authkey);

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                          disc.time + 100000, "myuser", authkey, NULL, 204, 11, OID_SYSDESCR, 9);
    size_t n = snmp_process(req, rl, resp, sizeof(resp));
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
    snmp_v3_set_user("myuser", "authpass12", "");
    protocore_net_host_udp_reset();

    const uint32_t reqid = 0x4321;
    proto_bool ok = snmp_inform_v3("127.0.0.1", 162, reqid, OID_SYSDESCR, 9, NULL, 0);
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
    TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(req, full, resp, sizeof(resp)));
    for (size_t L = 0; L < full; L++)
    {
        TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(req, L, resp, sizeof(resp)));
    }

    uint8_t pk[SNMP_USM_KEY_LEN] = {0};
    size_t pl = build_get(req, sizeof(req), PROTO_FALSE, PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                          "myuser", NULL, pk, 300, 8, OID_SYSDESCR, 9);
    TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(req, pl, resp, sizeof(resp)));
}

void test_v3_init_and_boots_accessors()
{
    V3View v;
    discover(&v);
    uint8_t custom[8] = {0x80, 0, 0, 0, 1, 2, 3, 4};
    snmp_v3_init(custom, sizeof(custom));
    snmp_v3_init(v.engine_id, v.engine_id_len);
    snmp_v3_set_user("myuser", "authpass12", "privpass12");

    uint32_t saved = snmp_v3_get_boots();
    snmp_v3_set_boots(0xABCD);
    TEST_ASSERT_EQUAL_UINT32(0xABCD, snmp_v3_get_boots());
    snmp_v3_set_boots(saved);
}

void test_v3_discovery_variants()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    snmp_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t wrong_eid[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33};
    uint8_t privkey[SNMP_USM_KEY_LEN] = {0};

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_TRUE, wrong_eid, sizeof(wrong_eid), v.boots, v.time,
                          "myuser", authkey, privkey, 300, 9, OID_SYSDESCR, 9);
    TEST_ASSERT_TRUE(snmp_v3_process(req, rl, resp, sizeof(resp)) > 0);

    rl = build_get(req, sizeof(req), PROTO_FALSE, PROTO_FALSE, wrong_eid, sizeof(wrong_eid), 0, 0, "", NULL, NULL, 300,
                   10, OID_SYSDESCR, 9);
    TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(req, rl, resp, 20));
}

void test_v3_priv_not_configured()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    snmp_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
    snmp_v3_set_user("myuser", "authpass12", "");
    uint8_t privkey[SNMP_USM_KEY_LEN] = {0};

    uint8_t req[300], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                          "myuser", authkey, privkey, 300, 11, OID_SYSDESCR, 9);
    size_t n = snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    snmp_v3_set_user("myuser", "authpass12", "privpass12");
}

void test_v3_notify_paths()
{
    uint32_t trap_oid[] = {1, 3, 6, 1, 4, 1, 49374, 0, 1};
    snmp_v3_set_user("myuser", "", "");
    TEST_ASSERT_FALSE(snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, NULL, 0));

    snmp_v3_set_user("myuser", "authpass12", "privpass12");
    TEST_ASSERT_TRUE(snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, NULL, 0));
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
    ber_enc_init(&se2, secp, sizeof(secp));
    size_t s2 = ber_seq_begin(&se2, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, eid, eid_len);
    SnmpBerV.enc = &se2;
    SnmpBerV.tlv.ival = boots;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.enc = &se2;
    SnmpBerV.tlv.ival = time;
    SnmpBer.put_integer(snmp_ber_work);
    ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)user, strlen(user));
    size_t auth_off = 0;
    if (auth)
    {
        auth_off = se2.len + 2;
        uint8_t z[SNMP_V3_AUTH_PARAM_LEN] = {0};
        ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, z, auth_plen);
    }
    else
    {
        ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (priv)
    {
        ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, salt, priv_plen);
    }
    else
    {
        ber_put_octet_string(&se2, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    ber_seq_end(&se2, s2);

    BerEnc e;
    ber_enc_init(&e, out, cap);
    size_t msg = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = (int)SNMP_V3;
    SnmpBer.put_integer(snmp_ber_work);
    size_t hdr = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = msg_id;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = 65507;
    SnmpBer.put_integer(snmp_ber_work);
    uint8_t fl = (uint8_t)((auth ? 0x01 : 0) | (priv ? 0x02 : 0) | (auth ? 0 : 0x04));
    ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &fl, 1);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = 3;
    SnmpBer.put_integer(snmp_ber_work);
    ber_seq_end(&e, hdr);
    ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, secp, se2.len);
    size_t sec_value_pos = e.len - se2.len;
    ber_put_raw(&e, scoped, scoped_len);
    ber_seq_end(&e, msg);
    if (!e.ok)
    {
        return 0;
    }
    if (digest)
    {
        uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
        HmacSha256V.mac_args.key = authkey;
        HmacSha256V.mac_args.key_len = SNMP_USM_KEY_LEN;
        HmacSha256V.mac_args.data = out;
        HmacSha256V.mac_args.len = e.len;
        HmacSha256V.mac_args.out = mac;
        HmacSha256.mac(tw);
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
    ber_dec_init(&d, req, full);
    uint8_t tag;
    size_t l;
    long tmp;
    ber_read_header(&d, &tag, &l);
    size_t ver_tag = d.pos;
    ber_read_integer(&d, &tmp);
    size_t gdata_tag = d.pos;
    ber_read_header(&d, &tag, &l);
    size_t msgid_tag = d.pos;
    ber_read_integer(&d, &tmp);
    ber_read_integer(&d, &tmp);
    size_t flags_tag = d.pos;
    ber_read_header(&d, &tag, &l);
    d.pos += l;
    size_t secmodel_tag = d.pos;
    ber_read_integer(&d, &tmp);
    size_t secp_tag = d.pos;
    ber_read_header(&d, &tag, &l);
    size_t base = d.pos;
    BerDec sd;
    ber_dec_init(&sd, req + base, l);
    size_t sseq_tag = base + sd.pos;
    ber_read_header(&sd, &tag, &l);
    size_t eid_tag = base + sd.pos;
    ber_read_header(&sd, &tag, &l);
    sd.pos += l;
    size_t boots_tag = base + sd.pos;
    ber_read_integer(&sd, &tmp);
    ber_read_integer(&sd, &tmp);
    size_t uname_tag = base + sd.pos;
    ber_read_header(&sd, &tag, &l);
    sd.pos += l;
    size_t aparm_tag = base + sd.pos;
    ber_read_header(&sd, &tag, &l);
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
        TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(bad, full, resp, sizeof(resp)));
    }
}

void test_v3_scoped_parse_rejections(void)
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    snmp_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
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
        TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(req, rl, resp, sizeof(resp)));
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

        TEST_ASSERT_TRUE(snmp_v3_process(req, rl, resp, sizeof(resp)) > 0);
    }
}

void test_v3_auth_edge_rejections(void)
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    snmp_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t req[320], resp[512];

    const uint8_t any_scoped[] = {0x30, 0x02, 0x04, 0x00};
    size_t rl =
        build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time, "myuser",
                            authkey, 420, any_scoped, sizeof(any_scoped), PROTO_FALSE, 16, SNMP_V3_PRIV_PARAM_LEN);
    TEST_ASSERT_TRUE(rl > 0);
    size_t n = snmp_v3_process(req, rl, resp, sizeof(resp));
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
    TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(req, rl, resp, sizeof(resp)));
}

void test_v3_notify_overflow_guards()
{
    snmp_v3_set_user("myuser", "authpass12", "privpass12");
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
    TEST_ASSERT_TRUE(snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, &vb, 1));
    vb.blen = 1550;
    TEST_ASSERT_FALSE(snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, &vb, 1));
    for (size_t blen = 1360; blen <= 1480; blen += 2)
    {
        vb.blen = blen;
        (void)snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, &vb, 1);
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
    TEST_ASSERT_TRUE(snmp_add_dynamic(big_oid, 9, (uint8_t)SNMP_TAG_BER_OCTET_STRING, big_getter));

    V3View disc;
    discover(&disc);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    snmp_localize_key(tw, "authpass12", disc.engine_id, disc.engine_id_len, authkey);

    uint8_t req[300], resp[2048];
    proto_bool saw_overflow = PROTO_FALSE, saw_ok = PROTO_FALSE;
    for (size_t vlen = 1400; vlen <= 1472; vlen++)
    {
        g_big_len = vlen;
        size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, disc.engine_id, disc.engine_id_len, disc.boots,
                              disc.time, "myuser", authkey, NULL, 500, 88, big_oid, 9);
        TEST_ASSERT_TRUE(rl > 0);
        size_t n = snmp_process(req, rl, resp, sizeof(resp));
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
    ber_enc_init(&e, out, cap);
    size_t msg = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = (int)SNMP_V3;
    SnmpBer.put_integer(snmp_ber_work);
    size_t hdr = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = msg_id;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = 65507;
    SnmpBer.put_integer(snmp_ber_work);
    ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &flags, 1);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = 3;
    SnmpBer.put_integer(snmp_ber_work);
    ber_seq_end(&e, hdr);
    ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, sec, sec_len);
    ber_put_raw(&e, tail, tail_len);
    ber_seq_end(&e, msg);
    return e.ok ? e.len : 0;
}

static size_t build_secparams_prefix(uint8_t *out, size_t cap, unsigned nfields)
{
    BerEnc e;
    ber_enc_init(&e, out, cap);
    size_t s = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    if (nfields >= 1)
    {
        ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (nfields >= 2)
    {
        SnmpBerV.enc = &e;
        SnmpBerV.tlv.ival = 1;
        SnmpBer.put_integer(snmp_ber_work);
    }
    if (nfields >= 3)
    {
        SnmpBerV.enc = &e;
        SnmpBerV.tlv.ival = 0;
        SnmpBer.put_integer(snmp_ber_work);
    }
    if (nfields >= 4)
    {
        ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (nfields >= 5)
    {
        ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    ber_seq_end(&e, s);
    return e.ok ? e.len : 0;
}

void test_v3_truncated_fields_fail_closed()
{
    uint8_t req[320], resp[512];

    for (int stop = 0; stop <= 4; stop++)
    {
        BerEnc e;
        ber_enc_init(&e, req, sizeof(req));
        size_t msg = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
        SnmpBerV.enc = &e;
        SnmpBerV.tlv.ival = (int)SNMP_V3;
        SnmpBer.put_integer(snmp_ber_work);
        if (stop >= 1)
        {
            size_t hdr = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
            SnmpBerV.enc = &e;
            SnmpBerV.tlv.ival = 500 + stop;
            SnmpBer.put_integer(snmp_ber_work);
            if (stop >= 2)
            {
                SnmpBerV.enc = &e;
                SnmpBerV.tlv.ival = 65507;
                SnmpBer.put_integer(snmp_ber_work);
            }
            if (stop >= 3)
            {
                uint8_t fl = 0x04;
                ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &fl, 1);
            }
            if (stop >= 4)
            {
                SnmpBerV.enc = &e;
                SnmpBerV.tlv.ival = 3;
                SnmpBer.put_integer(snmp_ber_work);
            }
            ber_seq_end(&e, hdr);
        }
        ber_seq_end(&e, msg);
        TEST_ASSERT_TRUE(e.ok);
        TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(req, e.len, resp, sizeof(resp)));
    }

    const uint8_t tail[] = {0x30, 0x02, 0x04, 0x00};
    size_t fl = build_v3_frame(req, sizeof(req), 510, 0x04, NULL, 0, tail, sizeof(tail));
    TEST_ASSERT_TRUE(fl > 0);
    TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(req, fl, resp, sizeof(resp)));
    for (unsigned nf = 0; nf <= 5; nf++)
    {
        uint8_t sec[64];
        size_t sl = build_secparams_prefix(sec, sizeof(sec), nf);
        TEST_ASSERT_TRUE(sl > 0);
        size_t n = build_v3_frame(req, sizeof(req), 520 + (long)nf, 0x04, sec, sl, tail, sizeof(tail));
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(req, n, resp, sizeof(resp)));
    }
}

void test_v3_outer_tag_and_empty_flags()
{
    uint8_t resp[512];
    const uint8_t not_seq[] = {0x04, 0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(not_seq, sizeof(not_seq), resp, sizeof(resp)));

    uint8_t req[320];
    BerEnc e;
    ber_enc_init(&e, req, sizeof(req));
    size_t msg = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = (int)SNMP_V3;
    SnmpBer.put_integer(snmp_ber_work);
    size_t hdr = ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = 530;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = 65507;
    SnmpBer.put_integer(snmp_ber_work);
    ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = 3;
    SnmpBer.put_integer(snmp_ber_work);
    ber_seq_end(&e, hdr);
    ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    ber_seq_end(&e, msg);
    TEST_ASSERT_TRUE(e.ok);
    TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(req, e.len, resp, sizeof(resp)));
}

void test_v3_scoped_truncated_headers()
{
    V3View v;
    discover(&v);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    snmp_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
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
        TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(req, rl, resp, sizeof(resp)));
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
    size_t n = snmp_v3_process(req, rl, resp, sizeof(resp));
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
    snmp_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
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
        snmp_v3_set_user(cases[i].cfg_user, cases[i].cfg_auth, "");
        size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, v.engine_id, v.engine_id_len, v.boots, v.time,
                              cases[i].req_user, authkey, NULL, 560 + (long)i, 3, OID_SYSDESCR, 9);
        TEST_ASSERT_TRUE(rl > 0);
        size_t n = snmp_v3_process(req, rl, resp, sizeof(resp));
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
    snmp_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);

    static uint8_t scoped[SNMP_MSG_BUF_SIZE + 64];
    memset(scoped, 0x00, sizeof(scoped));
    scoped[0] = 0x30;
    static uint8_t req[SNMP_MSG_BUF_SIZE + 256];
    uint8_t resp[512];
    size_t rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                                    "myuser", authkey, 570, scoped, sizeof(scoped), PROTO_FALSE, SNMP_V3_AUTH_PARAM_LEN,
                                    SNMP_V3_PRIV_PARAM_LEN);
    TEST_ASSERT_TRUE(rl > SNMP_MSG_BUF_SIZE);
    size_t n = snmp_v3_process(req, rl, resp, sizeof(resp));
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
    snmp_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);

    uint8_t req[320], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, v.engine_id, v.engine_id_len, v.boots + 7, v.time,
                          "myuser", authkey, NULL, 580, 4, OID_SYSDESCR, 9);
    size_t n = snmp_v3_process(req, rl, resp, sizeof(resp));
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
    snmp_localize_key(tw, "authpass12", v.engine_id, v.engine_id_len, authkey);
    uint8_t req[320], resp[512];

    const uint8_t any[] = {0x04, 0x02, 0x00, 0x00};
    size_t rl = build_v3_raw_scoped(req, sizeof(req), PROTO_TRUE, v.engine_id, v.engine_id_len, v.boots, v.time,
                                    "myuser", authkey, 590, any, sizeof(any), PROTO_TRUE, SNMP_V3_AUTH_PARAM_LEN, 4);
    TEST_ASSERT_TRUE(rl > 0);
    size_t n = snmp_v3_process(req, rl, resp, sizeof(resp));
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
    TEST_ASSERT_EQUAL_UINT(0, snmp_v3_process(req, rl, resp, sizeof(resp)));
}

void test_v3_init_length_guards_and_null_user()
{
    V3View before;
    discover(&before);

    uint8_t tooshort[4] = {0x80, 0x00, 0x00, 0x01};
    snmp_v3_init(tooshort, sizeof(tooshort));
    V3View after;
    discover(&after);
    TEST_ASSERT_EQUAL_UINT(before.engine_id_len, after.engine_id_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before.engine_id, after.engine_id, before.engine_id_len);

    uint8_t toolong[SNMP_V3_ENGINEID_MAX + 1];
    memset(toolong, 0xA5, sizeof(toolong));
    snmp_v3_init(toolong, sizeof(toolong));
    discover(&after);
    TEST_ASSERT_EQUAL_UINT(before.engine_id_len, after.engine_id_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before.engine_id, after.engine_id, before.engine_id_len);

    snmp_v3_set_user(NULL, NULL, NULL);
    uint8_t authkey[SNMP_USM_KEY_LEN];
    snmp_localize_key(tw, "authpass12", after.engine_id, after.engine_id_len, authkey);
    uint8_t req[320], resp[512];
    size_t rl = build_get(req, sizeof(req), PROTO_TRUE, PROTO_FALSE, after.engine_id, after.engine_id_len, after.boots,
                          after.time, "myuser", authkey, NULL, 600, 5, OID_SYSDESCR, 9);
    size_t n = snmp_v3_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    V3View r;
    TEST_ASSERT_TRUE(parse_v3(resp, n, NULL, &r));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_SNMP_PDU_REPORT, r.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(3u, r.oid[9]);
}

void test_v3_trap_reports_transport_failure()
{
    uint32_t trap_oid[] = {1, 3, 6, 1, 4, 1, 49374, 0, 1};

    TEST_ASSERT_FALSE(snmp_trap_v3("not.an.address", 162, trap_oid, 9, NULL, 0));
    TEST_ASSERT_FALSE(snmp_trap_v3(NULL, 162, trap_oid, 9, NULL, 0));

    protocore_net_host_udp_reset();
    mock_udp_send_fail_after(0);
    TEST_ASSERT_FALSE(snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_net_host_udp_count());

    mock_udp_send_fail_after(-1);
    TEST_ASSERT_TRUE(snmp_trap_v3("192.168.1.1", 162, trap_oid, 9, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(1, protocore_net_host_udp_count());
    TEST_ASSERT_EQUAL_UINT16(162, protocore_net_host_udp_at(0)->dst_port);
}
