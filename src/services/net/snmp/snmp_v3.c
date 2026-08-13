// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_snmp_v3.c
 * @brief SNMPv3 USM: message framing, engine discovery, timeliness, auth, privacy.
 */

#include "services/net/snmp/snmp_v3.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_SNMP_V3

#include "crypto/mac/hmac_sha256.h"
#include "mmgr/endian.h"
#include "services/net/snmp/snmp_agent.h"
#include "services/net/snmp/snmp_ber.h"
#include "services/net/snmp/snmp_crypto.h"

#if PROTOCORE_ENABLE_SNMP_TRAP
#include "network_drivers/transport/udp/udp.h"
#include "services/net/snmp/snmp_notify.h"
#endif
#if PROTOCORE_HAS_NET_STACK
#include "server/clock/clock.h" // protocore_millis() - the library's clock seam (ban 5: never bare millis)
static uint32_t protocore_snmp_v3_uptime_s()
{
    return (uint32_t)(protocore_millis() / 1000ULL);
}
#else
static uint32_t protocore_snmp_v3_uptime_s()
{
    return 0; // no clock in this build; tests drive boots/time via the discovery handshake
}
#endif

// usmStats... subtree: 1.3.6.1.6.3.15.1.1.<n>.0
static const uint32_t kUsmStatsBase[] = {1, 3, 6, 1, 6, 3, 15, 1, 1};
typedef enum PROTO_ENUM_PACKED
{
    USM_STAT_NOT_IN_TIME = 2,
    USM_STAT_UNKNOWN_USER = 3,
    USM_STAT_UNKNOWN_ENGINE = 4,
    USM_STAT_WRONG_DIGEST = 5,
    USM_STAT_DECRYPT = 6,
} UsmStat;

// ---------------------------------------------------------------------------
// Engine / user state
// ---------------------------------------------------------------------------

// All SNMPv3 USM engine state, owned by one instance (internal linkage): the engine id/boots,
// the configured user + localized auth/priv keys, the USM stats counters, and the per-request
// working buffers (staggered lifetimes; see protocore_snmp_v3_process), grouped so it is one named owner,
// unreachable cross-TU. Single-threaded (the lwIP callback or a test, never reentrant).
typedef struct
{
    uint8_t engine_id[SNMP_V3_ENGINEID_MAX];
    size_t engine_id_len;
    uint32_t boots;

    char user[SNMP_V3_USER_MAX];
    uint8_t auth_key[SNMP_USM_KEY_LEN];
    uint8_t priv_key[SNMP_USM_KEY_LEN];
    proto_bool auth_set;
    proto_bool priv_set;
    uint32_t salt_ctr;

    // USM stats counters (reported in discovery / error Reports)
    uint32_t stat_unknown_engine;
    uint32_t stat_unknown_user;
    uint32_t stat_wrong_digest;
    uint32_t stat_not_in_time;
    uint32_t stat_decrypt;

    // Working buffers; lifetimes staggered so they never alias within a single request.
    uint8_t v3_a[SNMP_MSG_BUF_SIZE]; // auth-verify copy / decrypted scopedPDU
    uint8_t v3_b[SNMP_MSG_BUF_SIZE]; // inner response PDU
    uint8_t v3_c[SNMP_MSG_BUF_SIZE]; // outgoing scopedPDU
    uint8_t v3_d[SNMP_MSG_BUF_SIZE]; // privacy ciphertext
    uint8_t v3_sec[256];             // msgSecurityParameters scratch
    // The USM key localization and the per-message auth MAC work out of these; they run in sequence.
    uint8_t mac_work[PROTOCORE_HMAC_SHA256_BORROW];
} SnmpV3Ctx;

// Only the three fields with a non-zero start are named; static storage zeroes the rest, which is
// what every counter, key and working buffer wants anyway.
static SnmpV3Ctx s_v3 = {
    .engine_id = {0x80, 0x00, 0xC0, 0xDE, 0x05, 0x01, 0x02, 0x03, 0x04},
    .engine_id_len = 9,
    .boots = 1,
};

// v3_sec is a fixed 256 bytes but what build_message() packs into it scales with two overridable
// macros, so pin the worst case at compile time instead of assuming it. The msgSecurityParameters
// SEQUENCE is: 4 (seq tag + back-patched 3-byte length) + 3 + SNMP_V3_ENGINEID_MAX (engineID
// OCTET STRING) + 7 + 7 (engineBoots/engineTime INTEGERs, <=5 content octets each) + 3 +
// SNMP_V3_USER_MAX (userName) + 2 + SNMP_V3_AUTH_PARAM_LEN (authParams) + 2 +
// SNMP_V3_PRIV_PARAM_LEN (privParams). Raising either max past this turns the "cannot overflow"
// guard below into a live path, so make that a build error rather than a silent behavior change.
_Static_assert(4 + 3 + SNMP_V3_ENGINEID_MAX + 7 + 7 + 3 + SNMP_V3_USER_MAX + 2 + SNMP_V3_AUTH_PARAM_LEN + 2 +
                       SNMP_V3_PRIV_PARAM_LEN <=
                   sizeof(s_v3.v3_sec),
               "v3_sec is too small for SNMP_V3_ENGINEID_MAX + SNMP_V3_USER_MAX: raise v3_sec or lower the maxima");

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void protocore_snmp_v3_init(const uint8_t *engine_id, size_t engine_id_len)
{
    if (engine_id && engine_id_len >= 5 && engine_id_len <= SNMP_V3_ENGINEID_MAX)
    {
        mem.cpy(s_v3.engine_id, engine_id, engine_id_len);
        s_v3.engine_id_len = engine_id_len;
    }
    s_v3.user[0] = '\0';
    s_v3.auth_set = PROTO_FALSE;
    s_v3.priv_set = PROTO_FALSE;
}

void protocore_snmp_v3_set_user(const char *user, const char *auth_pass, const char *priv_pass)
{
    strncpy(s_v3.user, user ? user : "", sizeof(s_v3.user) - 1);
    s_v3.user[sizeof(s_v3.user) - 1] = '\0';
    s_v3.auth_set = auth_pass && auth_pass[0];
    s_v3.priv_set = priv_pass && priv_pass[0];
    if (s_v3.auth_set)
    {
        protocore_snmp_usm_localize_key(s_v3.mac_work, auth_pass, s_v3.engine_id, s_v3.engine_id_len, s_v3.auth_key);
    }
    if (s_v3.priv_set)
    {
        protocore_snmp_usm_localize_key(s_v3.mac_work, priv_pass, s_v3.engine_id, s_v3.engine_id_len, s_v3.priv_key);
    }
}

void protocore_snmp_v3_set_boots(uint32_t boots)
{
    s_v3.boots = boots;
}
uint32_t protocore_snmp_v3_get_boots()
{
    return s_v3.boots;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static proto_bool ct_eq(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t r = 0;
    for (size_t i = 0; i < n; i++)
    {
        r |= (uint8_t)(a[i] ^ b[i]);
    }
    return r == 0;
}

// Split a scopedPDU SEQUENCE into its contextName and inner PDU TLV.
static proto_bool parse_scoped(const uint8_t *buf, size_t len, const uint8_t **ctxname, size_t *ctxname_len,
                               const uint8_t **pdu, size_t *pdu_len)
{
    BerDec d;
    protocore_ber_dec_init(&d, buf, len);
    uint8_t tag;
    size_t l;
    if (!protocore_ber_read_header(&d, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    if (!protocore_ber_read_header(&d, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING) // contextEngineID
    {
        return PROTO_FALSE;
    }
    d.pos += l;
    if (!protocore_ber_read_header(&d, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING) // contextName
    {
        return PROTO_FALSE;
    }
    *ctxname = d.buf + d.pos;
    *ctxname_len = l;
    d.pos += l;
    size_t pdu_start = d.pos;
    if (!protocore_ber_read_header(&d, &tag, &l)) // PDU header (kept)
    {
        return PROTO_FALSE;
    }
    *pdu = buf + pdu_start;
    *pdu_len = (size_t)(d.pos - pdu_start) + l;
    return d.ok;
}

// Best-effort read of the inner request-id from a plaintext probe (for Reports).
static long inner_request_id(const uint8_t *mdata, size_t mdata_len, proto_bool priv)
{
    if (priv)
    {
        return 0;
    }
    const uint8_t *ctxname;
    const uint8_t *pdu;
    size_t ctxname_len;
    size_t pdu_len;
    if (!parse_scoped(mdata, mdata_len, &ctxname, &ctxname_len, &pdu, &pdu_len))
    {
        return 0;
    }
    BerDec d;
    protocore_ber_dec_init(&d, pdu, pdu_len);
    uint8_t tag;
    size_t l;
    long rid;
    // parse_scoped already read this exact header and sized pdu_len as (header bytes + content
    // length), so re-reading it from the start of pdu always succeeds; only the request-id
    // INTEGER read below can fail.
    if (!protocore_ber_read_header(&d, &tag, &l) || !protocore_ber_read_integer(&d, &rid))
    {
        return 0; // re-read cannot fail (see above)
    }
    return rid;
}

// Build a complete v3 message wrapping the already-built scopedPDU bytes.
static size_t build_message(long msg_id, proto_bool auth, proto_bool priv, const uint8_t *scoped, size_t scoped_len,
                            uint8_t *resp, size_t protocore_resp_cap)
{
    uint32_t now = protocore_snmp_v3_uptime_s();
    const uint8_t *data_ptr = scoped;
    size_t data_len = scoped_len;
    uint8_t salt[SNMP_V3_PRIV_PARAM_LEN];

    if (priv)
    {
        protocore_wr32be(salt, s_v3.boots);
        protocore_wr32be(salt + 4, ++s_v3.salt_ctr);
        uint8_t iv[16];
        protocore_wr32be(iv, s_v3.boots);
        protocore_wr32be(iv + 4, now);
        mem.cpy(iv + 8, salt, SNMP_V3_PRIV_PARAM_LEN);
        if (scoped_len > sizeof(s_v3.v3_d))
        {
            return 0;
        }
        protocore_snmp_aes128_cfb(s_v3.priv_key, iv, scoped, s_v3.v3_d, scoped_len, PROTO_TRUE);
        data_ptr = s_v3.v3_d;
    }

    // msgSecurityParameters (a SEQUENCE, later wrapped in an OCTET STRING).
    BerEnc se;
    protocore_ber_enc_init(&se, s_v3.v3_sec, sizeof(s_v3.v3_sec));
    size_t ss = protocore_ber_seq_begin(&se, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    protocore_ber_put_octet_string(&se, (uint8_t)SNMP_TAG_BER_OCTET_STRING, s_v3.engine_id, s_v3.engine_id_len);
    protocore_ber_put_integer(&se, (long)s_v3.boots);
    protocore_ber_put_integer(&se, (long)now);
    protocore_ber_put_octet_string(&se, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)s_v3.user,
                            strnlen(s_v3.user, SNMP_V3_USER_MAX));
    size_t auth_off = 0;
    if (auth)
    {
        auth_off = se.len + 2; // value follows tag + 1-byte length (24 < 128)
        uint8_t zeros[SNMP_V3_AUTH_PARAM_LEN];
        mem.set(zeros, 0, sizeof(zeros));
        protocore_ber_put_octet_string(&se, (uint8_t)SNMP_TAG_BER_OCTET_STRING, zeros, SNMP_V3_AUTH_PARAM_LEN);
    }
    else
    {
        protocore_ber_put_octet_string(&se, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    if (priv)
    {
        protocore_ber_put_octet_string(&se, (uint8_t)SNMP_TAG_BER_OCTET_STRING, salt, SNMP_V3_PRIV_PARAM_LEN);
    }
    else
    {
        protocore_ber_put_octet_string(&se, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    }
    protocore_ber_seq_end(&se, ss);
    if (!se.ok)
    {
        return 0;
        // so se.ok always holds here
    }
    size_t sec_len = se.len;

    // Full message.
    BerEnc e;
    protocore_ber_enc_init(&e, resp, protocore_resp_cap);
    size_t msg = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    protocore_ber_put_integer(&e, (int)SNMP_V3);
    size_t hdr = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    protocore_ber_put_integer(&e, msg_id);
    protocore_ber_put_integer(&e, 65507); // msgMaxSize
    uint8_t fl = (uint8_t)((auth ? 0x01 : 0) | (priv ? 0x02 : 0));
    protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, &fl, 1);
    protocore_ber_put_integer(&e, 3); // msgSecurityModel = USM
    protocore_ber_seq_end(&e, hdr);
    protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, s_v3.v3_sec, sec_len);
    size_t sec_value_pos = e.len - sec_len;
    if (priv)
    {
        protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, data_ptr, data_len);
    }
    else
    {
        protocore_ber_put_raw(&e, data_ptr, data_len);
    }
    protocore_ber_seq_end(&e, msg);
    if (!e.ok)
    {
        return 0;
    }
    size_t total = e.len;

    if (auth)
    {
        uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
        protocore_hmac_sha256(s_v3.mac_work, s_v3.auth_key, SNMP_USM_KEY_LEN, resp, total, mac);
        mem.cpy(resp + sec_value_pos + auth_off, mac, SNMP_V3_AUTH_PARAM_LEN);
    }
    return total;
}

// Build a Report PDU (usmStats<stat>.0 = Counter32) and wrap it in a v3 message.
static size_t build_report(long msg_id, proto_bool auth, uint32_t stat, uint32_t count, long request_id, uint8_t *resp,
                           size_t protocore_resp_cap)
{
    uint32_t oid[11];
    for (int i = 0; i < 9; i++)
    {
        oid[i] = kUsmStatsBase[i];
    }
    oid[9] = stat;
    oid[10] = 0;

    BerEnc e;
    protocore_ber_enc_init(&e, s_v3.v3_b, sizeof(s_v3.v3_b));
    size_t pdu = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_SNMP_PDU_REPORT);
    protocore_ber_put_integer(&e, request_id);
    protocore_ber_put_integer(&e, 0);
    protocore_ber_put_integer(&e, 0);
    size_t vbl = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    size_t vb = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    protocore_ber_put_oid(&e, oid, 11);
    protocore_ber_put_uint(&e, (uint8_t)SNMP_TAG_SNMP_COUNTER32, count);
    protocore_ber_seq_end(&e, vb);
    protocore_ber_seq_end(&e, vbl);
    protocore_ber_seq_end(&e, pdu);
    if (!e.ok)
    {
        return 0;
    }

    BerEnc sc;
    protocore_ber_enc_init(&sc, s_v3.v3_c, sizeof(s_v3.v3_c));
    size_t s = protocore_ber_seq_begin(&sc, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    protocore_ber_put_octet_string(&sc, (uint8_t)SNMP_TAG_BER_OCTET_STRING, s_v3.engine_id, s_v3.engine_id_len);
    protocore_ber_put_octet_string(&sc, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    protocore_ber_put_raw(&sc, s_v3.v3_b, e.len);
    protocore_ber_seq_end(&sc, s);
    if (!sc.ok)
    {
        return 0;
    }

    return build_message(msg_id, auth, PROTO_FALSE, s_v3.v3_c, sc.len, resp, protocore_resp_cap);
}

// ---------------------------------------------------------------------------
// Message processing
// ---------------------------------------------------------------------------

size_t protocore_snmp_v3_process(const uint8_t *req, size_t req_len, uint8_t *resp, size_t protocore_resp_cap)
{
    BerDec d;
    protocore_ber_dec_init(&d, req, req_len);
    uint8_t tag;
    size_t l;
    if (!protocore_ber_read_header(&d, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return 0;
    }
    long version;
    if (!protocore_ber_read_integer(&d, &version) || version != (int)SNMP_V3)
    {
        return 0;
    }

    // msgGlobalData
    if (!protocore_ber_read_header(&d, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return 0;
    }
    long msg_id;
    long msg_max;
    long sec_model;
    if (!protocore_ber_read_integer(&d, &msg_id) || !protocore_ber_read_integer(&d, &msg_max))
    {
        return 0;
    }
    (void)msg_max;
    size_t flags_len;
    if (!protocore_ber_read_header(&d, &tag, &flags_len) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING || flags_len < 1)
    {
        return 0;
    }
    uint8_t flags = d.buf[d.pos];
    d.pos += flags_len;
    if (!protocore_ber_read_integer(&d, &sec_model) || sec_model != 3) // USM only
    {
        return 0;
    }
    proto_bool req_auth = flags & 0x01;
    proto_bool req_priv = flags & 0x02;
    if (req_priv && !req_auth)
    {
        return 0;
    }

    // msgSecurityParameters: OCTET STRING wrapping a SEQUENCE.
    size_t sec_len;
    if (!protocore_ber_read_header(&d, &tag, &sec_len) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return 0;
    }
    const uint8_t *sec = d.buf + d.pos;
    d.pos += sec_len;

    BerDec sd;
    protocore_ber_dec_init(&sd, sec, sec_len);
    if (!protocore_ber_read_header(&sd, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return 0;
    }
    size_t eid_len;
    if (!protocore_ber_read_header(&sd, &tag, &eid_len) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return 0;
    }
    const uint8_t *eid = sd.buf + sd.pos;
    sd.pos += eid_len;
    long req_boots;
    long req_time;
    if (!protocore_ber_read_integer(&sd, &req_boots) || !protocore_ber_read_integer(&sd, &req_time))
    {
        return 0;
    }
    size_t uname_len;
    if (!protocore_ber_read_header(&sd, &tag, &uname_len) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return 0;
    }
    const uint8_t *uname = sd.buf + sd.pos;
    sd.pos += uname_len;
    size_t aparm_len;
    if (!protocore_ber_read_header(&sd, &tag, &aparm_len) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return 0;
    }
    const uint8_t *aparm = sd.buf + sd.pos;
    size_t aparm_off = (size_t)(aparm - req);
    sd.pos += aparm_len;
    size_t pparm_len;
    if (!protocore_ber_read_header(&sd, &tag, &pparm_len) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return 0;
    }
    const uint8_t *pparm = sd.buf + sd.pos;
    sd.pos += pparm_len;
    if (!sd.ok)
    {
        return 0;
        // here
    }

    const uint8_t *mdata = d.buf + d.pos;
    size_t mdata_len = req_len - d.pos;

    // Engine discovery: unknown/empty authoritative engine ID.
    proto_bool engine_match = (eid_len == s_v3.engine_id_len) && (mem.cmp(eid, s_v3.engine_id, eid_len) == 0);
    if (!engine_match)
    {
        s_v3.stat_unknown_engine++;
        return build_report(msg_id, PROTO_FALSE, (int)USM_STAT_UNKNOWN_ENGINE, s_v3.stat_unknown_engine,
                            inner_request_id(mdata, mdata_len, req_priv), resp, protocore_resp_cap);
    }

    if (!req_auth) // non-discovery noAuthNoPriv is not supported
    {
        return 0;
    }

    // Known user?
    if (!s_v3.auth_set || !(uname_len == strnlen(s_v3.user, SNMP_V3_USER_MAX) && s_v3.user[0] &&
                            mem.cmp(uname, s_v3.user, uname_len) == 0))
    {
        s_v3.stat_unknown_user++;
        return build_report(msg_id, PROTO_FALSE, (int)USM_STAT_UNKNOWN_USER, s_v3.stat_unknown_user, 0, resp,
                            protocore_resp_cap);
    }

    // Authenticate: HMAC over the whole message with the auth field zeroed.
    if (aparm_len != SNMP_V3_AUTH_PARAM_LEN || req_len > sizeof(s_v3.v3_a))
    {
        s_v3.stat_wrong_digest++;
        return build_report(msg_id, PROTO_FALSE, (int)USM_STAT_WRONG_DIGEST, s_v3.stat_wrong_digest, 0, resp,
                            protocore_resp_cap);
    }
    mem.cpy(s_v3.v3_a, req, req_len);
    mem.set(s_v3.v3_a + aparm_off, 0, SNMP_V3_AUTH_PARAM_LEN);
    uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
    protocore_hmac_sha256(s_v3.mac_work, s_v3.auth_key, SNMP_USM_KEY_LEN, s_v3.v3_a, req_len, mac);
    if (!ct_eq(mac, aparm, SNMP_V3_AUTH_PARAM_LEN))
    {
        s_v3.stat_wrong_digest++;
        return build_report(msg_id, PROTO_FALSE, (int)USM_STAT_WRONG_DIGEST, s_v3.stat_wrong_digest, 0, resp,
                            protocore_resp_cap);
    }

    // Timeliness window (RFC 3414 §3.2): boots must match, time within +/-150s.
    uint32_t now = protocore_snmp_v3_uptime_s();
    long dt = (long)now - req_time;
    if (dt < 0)
    {
        dt = -dt;
    }
    if ((uint32_t)req_boots != s_v3.boots || dt > 150)
    {
        s_v3.stat_not_in_time++;
        return build_report(msg_id, PROTO_TRUE, (int)USM_STAT_NOT_IN_TIME, s_v3.stat_not_in_time, 0, resp, protocore_resp_cap);
    }

    // Privacy: decrypt the scopedPDU if the priv flag is set.
    const uint8_t *scoped;
    size_t scoped_len;
    if (req_priv)
    {
        if (!s_v3.priv_set || pparm_len != SNMP_V3_PRIV_PARAM_LEN)
        {
            s_v3.stat_decrypt++;
            return build_report(msg_id, PROTO_TRUE, (int)USM_STAT_DECRYPT, s_v3.stat_decrypt, 0, resp, protocore_resp_cap);
        }
        BerDec md;
        protocore_ber_dec_init(&md, mdata, mdata_len);
        if (!protocore_ber_read_header(&md, &tag, &l) || tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
        {
            return 0;
        }
        const uint8_t *ct = md.buf + md.pos;
        size_t ct_len = l;
        if (ct_len > sizeof(s_v3.v3_a))
        {
            return 0;
            // digest step
        }
        uint8_t iv[16];
        protocore_wr32be(iv, (uint32_t)req_boots);
        protocore_wr32be(iv + 4, (uint32_t)req_time);
        mem.cpy(iv + 8, pparm, SNMP_V3_PRIV_PARAM_LEN);
        protocore_snmp_aes128_cfb(s_v3.priv_key, iv, ct, s_v3.v3_a, ct_len, PROTO_FALSE);
        scoped = s_v3.v3_a;
        scoped_len = ct_len;
    }
    else
    {
        scoped = mdata;
        scoped_len = mdata_len;
    }

    // Inner PDU -> dispatch (authenticated: writes allowed; v2c-style semantics).
    const uint8_t *ctxname;
    const uint8_t *pdu;
    size_t ctxname_len;
    size_t pdu_len;
    if (!parse_scoped(scoped, scoped_len, &ctxname, &ctxname_len, &pdu, &pdu_len))
    {
        return 0;
    }
    size_t rpdu = protocore_snmp_dispatch_pdu(pdu, pdu_len, PROTO_TRUE, PROTO_TRUE, s_v3.v3_b, sizeof(s_v3.v3_b));
    if (rpdu == 0)
    {
        return 0;
    }

    // Response scopedPDU { our engineID, echoed contextName, response PDU }.
    BerEnc sc;
    protocore_ber_enc_init(&sc, s_v3.v3_c, sizeof(s_v3.v3_c));
    size_t s = protocore_ber_seq_begin(&sc, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    protocore_ber_put_octet_string(&sc, (uint8_t)SNMP_TAG_BER_OCTET_STRING, s_v3.engine_id, s_v3.engine_id_len);
    protocore_ber_put_octet_string(&sc, (uint8_t)SNMP_TAG_BER_OCTET_STRING, ctxname, ctxname_len);
    protocore_ber_put_raw(&sc, s_v3.v3_b, rpdu);
    protocore_ber_seq_end(&sc, s);
    if (!sc.ok)
    {
        return 0;
    }

    return build_message(msg_id, PROTO_TRUE, req_priv, s_v3.v3_c, sc.len, resp, protocore_resp_cap);
}

#if PROTOCORE_ENABLE_SNMP_TRAP

// Shared SNMPv3 USM notification path: authenticated, and encrypted when a
// privacy password is configured. Reuses the engine ID + localized keys from
// protocore_snmp_v3_init()/protocore_snmp_v3_set_user() and the same build_message() as responses.
// @p pdu_tag selects Trap ((uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2) vs InformRequest (0xA6); @p request_id
// is the inner PDU request-id (the inform receiver echoes it in its Response).
static proto_bool send_v3_notify(const char *dst_ip, uint16_t port, uint8_t pdu_tag, uint32_t request_id,
                                 const uint32_t *trap_oid, size_t trap_oid_len, const SnmpVarbind *vbs, size_t n)
{
    if (!s_v3.auth_set) // a v3 notification must be authenticated
    {
        return PROTO_FALSE;
    }

    // Notification PDU into the inner-PDU scratch.
    BerEnc e;
    protocore_ber_enc_init(&e, s_v3.v3_b, sizeof(s_v3.v3_b));
    protocore_snmp_notify_build_pdu(&e, pdu_tag, request_id, trap_oid, trap_oid_len, protocore_snmp_v3_uptime_s() * 100, vbs, n);
    if (!e.ok)
    {
        return PROTO_FALSE;
    }

    // scopedPDU { contextEngineID = our engine, contextName = "", PDU }.
    BerEnc sc;
    protocore_ber_enc_init(&sc, s_v3.v3_c, sizeof(s_v3.v3_c));
    size_t s = protocore_ber_seq_begin(&sc, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    protocore_ber_put_octet_string(&sc, (uint8_t)SNMP_TAG_BER_OCTET_STRING, s_v3.engine_id, s_v3.engine_id_len);
    protocore_ber_put_octet_string(&sc, (uint8_t)SNMP_TAG_BER_OCTET_STRING, NULL, 0);
    protocore_ber_put_raw(&sc, s_v3.v3_b, e.len);
    protocore_ber_seq_end(&sc, s);
    if (!sc.ok)
    {
        return PROTO_FALSE;
    }

    uint8_t out[SNMP_MSG_BUF_SIZE];
    size_t len = build_message((long)request_id, s_v3.auth_set, s_v3.priv_set, s_v3.v3_c, sc.len, out, sizeof(out));
    protocore_ip dst = {PROTOCORE_IP_NONE, {0}};
    return len && Ip.parse(dst_ip, &dst) && Udp.client->sendto(&dst, port, out, len);
}

proto_bool protocore_snmp_trap_v3(const char *dst_ip, uint16_t port, const uint32_t *trap_oid, size_t trap_oid_len,
                           const SnmpVarbind *vbs, size_t n)
{
    static uint32_t s_v3_trap_id = 1; // a trap is unconfirmed; the id is informational
    return send_v3_notify(dst_ip, port, (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2, s_v3_trap_id++, trap_oid, trap_oid_len, vbs,
                          n);
}

// SNMPv3 USM InformRequest (confirmed notification, RFC 3416 4.2.7). Symmetric to
// protocore_snmp_inform_v2c: builds + sends the InformRequest. The caller owns the
// request_id and, for confirmed delivery, retransmits until the receiver's
// Response (echoing request_id) arrives.
proto_bool protocore_snmp_inform_v3(const char *dst_ip, uint16_t port, uint32_t request_id, const uint32_t *trap_oid,
                             size_t trap_oid_len, const SnmpVarbind *vbs, size_t n)
{
    return send_v3_notify(dst_ip, port, 0xA6 /* InformRequest */, request_id, trap_oid, trap_oid_len, vbs, n);
}
#endif // PROTOCORE_ENABLE_SNMP_TRAP

#endif // PROTOCORE_ENABLE_SNMP_V3
