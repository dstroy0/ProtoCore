// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_v3.c
 * @brief SNMPv3 and USM (RFC 3412 sec 6, RFC 3414) - implementation. See snmp_v3.h.
 */

#include "services/net/snmp/snmp_v3/snmp_v3.h"
#include "mmgr/protomem/protomem.h" // mem.cpy / mem.set / mem.cmp
#include "mmgr/protostr/protostr.h" // str.len / str.copy
#include "mmgr/secure/secure.h"   // the persistent end this module's key material is taken from

static uint8_t snmp_crypto_work[16]; // the borrow an entry takes; SnmpCrypto never reads it

static uint8_t snmp_ber_work[16]; // the borrow an entry takes; SnmpBer never reads it

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

#if PROTOCORE_ENABLE_SNMP_V3

#include "crypto/mac/hmac_sha256/hmac_sha256.h"
#include "mmgr/endian/endian.h" // endian.wr32be: the big-endian fields of the IV and the salt
#include "services/net/snmp/snmp_agent/snmp_agent.h"
#include "services/net/snmp/snmp_ber/snmp_ber.h"
#include "services/net/snmp/snmp_crypto/snmp_crypto.h"

#if PROTOCORE_ENABLE_SNMP_TRAP
#include "network_drivers/transport/udp/client/client.h" // UdpClient: the notification out
#include "services/net/snmp/snmp_notify/snmp_notify.h"               // SnmpNotify.build_pdu: the notification PDU
#include "shared/ip/ip.h"                                // Ip.parse: the receiver's address, once
#endif

#if PROTOCORE_HAS_NET_STACK
#include "server/clock/clock.h" // protocore_millis(): the library's clock seam
#endif

// The usmStats subtree, 1.3.6.1.6.3.15.1.1 (RFC 3414 sec 5). Each counter is reported as
// usmStats<n>.0 in a Report PDU.
static const uint32_t kUsmStatsBase[] = {1, 3, 6, 1, 6, 3, 15, 1, 1};

/** @brief The usmStats counter a Report names (RFC 3414 sec 5). */
typedef enum PROTO_ENUM_PACKED
{
    USM_STAT_NOT_IN_TIME = 2,    ///< usmStatsNotInTimeWindows
    USM_STAT_UNKNOWN_USER = 3,   ///< usmStatsUnknownUserNames
    USM_STAT_UNKNOWN_ENGINE = 4, ///< usmStatsUnknownEngineIDs
    USM_STAT_WRONG_DIGEST = 5,   ///< usmStatsWrongDigests
    USM_STAT_DECRYPT = 6,        ///< usmStatsDecryptionErrors
} UsmStat;

/**
 * @brief The engine's compile-time storage: its identity, its user, its counters, its buffers.
 *
 * All of it BSS, so a message costs no heap and none of it lands on a task stack. The working
 * buffers have staggered lifetimes and never alias within one message.
 *
 * @var SnmpV3Storage::engine_id      the authoritative snmpEngineID (RFC 3414 sec 2.4)
 * @var SnmpV3Storage::engine_id_len  how many octets
 * @var SnmpV3Storage::boots          msgAuthoritativeEngineBoots (RFC 3414 sec 2.2.2)
 * @var SnmpV3Storage::user           msgUserName of the configured user
 * @var SnmpV3Storage::auth_key       its localized authentication key (RFC 7860 sec 9.3)
 * @var SnmpV3Storage::priv_key       its localized privacy key
 * @var SnmpV3Storage::auth_set       an authentication password was configured
 * @var SnmpV3Storage::priv_set       a privacy password was configured
 * @var SnmpV3Storage::salt_ctr       the low half of the next msgPrivacyParameters salt
 * @var SnmpV3Storage::stat_unknown_engine  usmStatsUnknownEngineIDs
 * @var SnmpV3Storage::stat_unknown_user    usmStatsUnknownUserNames
 * @var SnmpV3Storage::stat_wrong_digest    usmStatsWrongDigests
 * @var SnmpV3Storage::stat_not_in_time     usmStatsNotInTimeWindows
 * @var SnmpV3Storage::stat_decrypt         usmStatsDecryptionErrors
 * @var SnmpV3Storage::v3_a           the auth-verify copy, then the decrypted ScopedPDU
 * @var SnmpV3Storage::v3_b           the inner response PDU
 * @var SnmpV3Storage::v3_c           the outgoing ScopedPDU
 * @var SnmpV3Storage::v3_d           the privacy ciphertext
 * @var SnmpV3Storage::v3_sec         the msgSecurityParameters SEQUENCE
 * @var SnmpV3Storage::v3_tx          one outgoing notification message
 * @var SnmpV3Storage::mac_work       the scratch the hash and the MAC borrow, one at a time
 */
struct SnmpV3Storage
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

    uint32_t stat_unknown_engine;
    uint32_t stat_unknown_user;
    uint32_t stat_wrong_digest;
    uint32_t stat_not_in_time;
    uint32_t stat_decrypt;

    uint8_t v3_a[SNMP_MSG_BUF_SIZE];
    uint8_t v3_b[SNMP_MSG_BUF_SIZE];
    uint8_t v3_c[SNMP_MSG_BUF_SIZE];
    uint8_t v3_d[SNMP_MSG_BUF_SIZE];
    uint8_t v3_sec[256];
    uint8_t v3_tx[SNMP_MSG_BUF_SIZE];
    uint8_t mac_work[PROTOCORE_HMAC_SHA256_BORROW];
};

// Three fields start non-zero; static storage zeroes every counter, key and buffer, which is what
// each of them wants.
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SNMP_V3_OFF_CTX 0u
static_assert(SNMP_V3_OFF_CTX + sizeof(struct SnmpV3Storage) <= PROTOCORE_SNMP_V3_BORROW,
              "PROTOCORE_SNMP_V3_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SNMP_V3_CTX(w) ((struct SnmpV3Storage *)(void *)((w) + SNMP_V3_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SNMP_V3_BORROW persistent bytes
} SnmpV3OwnCtx;
static SnmpV3OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_snmp_v3_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_SNMP_V3_BORROW).buf;
        // A borrow arrives zeroed, and these do not start at zero.
        SNMP_V3_CTX(s_own.span)->boots = 1;
        SNMP_V3_CTX(s_own.span)->engine_id_len = 9;
        static const uint8_t engine_id0[] = {0x80, 0x00, 0xC0, 0xDE, 0x05, 0x01, 0x02, 0x03, 0x04};
        mem.cpy(SNMP_V3_CTX(s_own.span)->engine_id, engine_id0, sizeof(engine_id0));
    }
    return s_own.span;
}

// v3_sec is a fixed 256 octets and what build_message() packs into it scales with two overridable
// maxima. The msgSecurityParameters SEQUENCE is 4 (tag + back-patched 3-octet length) + 3 +
// SNMP_V3_ENGINEID_MAX + 7 + 7 (two INTEGERs of at most 5 content octets) + 3 + SNMP_V3_USER_MAX +
// 2 + SNMP_V3_AUTH_PARAM_LEN + 2 + SNMP_V3_PRIV_PARAM_LEN. Raising either maximum past that turns
// the encoder's overflow guard into a live path, so it is a build error here.
_Static_assert(4 + 3 + SNMP_V3_ENGINEID_MAX + 7 + 7 + 3 + SNMP_V3_USER_MAX + 2 + SNMP_V3_AUTH_PARAM_LEN + 2 +
                       SNMP_V3_PRIV_PARAM_LEN <=
                   sizeof(((struct SnmpV3Storage *)0)->v3_sec),
               "v3_sec is too small for SNMP_V3_ENGINEID_MAX + SNMP_V3_USER_MAX: raise v3_sec or lower the maxima");

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// msgAuthoritativeEngineTime: seconds since this engine was last re-initialized (RFC 3414 sec 2.2.2).
static uint32_t v3_uptime_s(void)
{
#if PROTOCORE_HAS_NET_STACK
    return (uint32_t)(Clock.ms / 1000ULL);
#else
    return 0; // no clock in this build; boots and time come through the discovery handshake
#endif
}

static void v3_init(uint8_t *restrict work)
{
    const uint8_t *engine_id = SnmpV3.engine.engine_id;
    const size_t engine_id_len = SnmpV3.engine.engine_id_len;
    if (engine_id && engine_id_len >= 5 && engine_id_len <= SNMP_V3_ENGINEID_MAX)
    {
        mem.cpy(SNMP_V3_CTX(work)->engine_id, engine_id, engine_id_len);
        SNMP_V3_CTX(work)->engine_id_len = engine_id_len;
    }
    SNMP_V3_CTX(work)->user[0] = '\0';
    SNMP_V3_CTX(work)->auth_set = PROTO_FALSE;
    SNMP_V3_CTX(work)->priv_set = PROTO_FALSE;
    SnmpV3.ok = PROTO_TRUE;
}

// The localized keys depend on the snmpEngineID, so they are derived here, once, rather than per
// message (RFC 3414 sec 2.6, derivation per RFC 7860 sec 9.3).
static void v3_set_user(uint8_t *restrict work)
{
    const char *user = SnmpV3.user.user;
    const char *auth_pass = SnmpV3.user.auth_pass;
    const char *priv_pass = SnmpV3.user.priv_pass;

    (void)str.copy(SNMP_V3_CTX(work)->user, user ? user : "", sizeof(SNMP_V3_CTX(work)->user));
    SNMP_V3_CTX(work)->auth_set = auth_pass && auth_pass[0];
    SNMP_V3_CTX(work)->priv_set = priv_pass && priv_pass[0];

    SnmpCrypto.work = SNMP_V3_CTX(work)->mac_work;
    SnmpCrypto.key.engine_id = SNMP_V3_CTX(work)->engine_id;
    SnmpCrypto.key.engine_id_len = SNMP_V3_CTX(work)->engine_id_len;
    if (SNMP_V3_CTX(work)->auth_set)
    {
        SnmpCrypto.key.password = auth_pass;
        SnmpCrypto.key.out = SNMP_V3_CTX(work)->auth_key;
        SnmpCrypto.localize_key(snmp_crypto_work);
    }
    if (SNMP_V3_CTX(work)->priv_set)
    {
        SnmpCrypto.key.password = priv_pass;
        SnmpCrypto.key.out = SNMP_V3_CTX(work)->priv_key;
        SnmpCrypto.localize_key(snmp_crypto_work);
    }
    SnmpV3.ok = SNMP_V3_CTX(work)->auth_set;
}

static void v3_set_boots(uint8_t *restrict work)
{
    SNMP_V3_CTX(work)->boots = SnmpV3.engine.boots;
    SnmpV3.ok = PROTO_TRUE;
}

static void v3_get_boots(uint8_t *restrict work)
{
    SnmpV3.u32 = SNMP_V3_CTX(work)->boots;
    SnmpV3.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Message helpers
// ---------------------------------------------------------------------------

// Compare n octets without an early exit, so a forged digest never reveals how many octets matched.
static proto_bool ct_eq(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t r = 0;
    for (size_t i = 0; i < n; i++)
    {
        r |= (uint8_t)(a[i] ^ b[i]);
    }
    return r == 0;
}

// Split a ScopedPDU SEQUENCE (RFC 3412 sec 6) into its contextName and the PDU TLV that follows.
// Reads the octets it is given and no engine state.
static proto_bool parse_scoped(const uint8_t *buf, size_t len, const uint8_t **ctxname, size_t *ctxname_len,
                               const uint8_t **pdu, size_t *pdu_len)
{
    BerDec d;
    SnmpBer.dec = &d;
    SnmpBer.buf.in = buf;
    SnmpBer.buf.cap = len;
    SnmpBer.dec_init(snmp_ber_work);

    SnmpBer.read_header(snmp_ber_work);
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return PROTO_FALSE;
    }
    SnmpBer.read_header(snmp_ber_work); // contextEngineID
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    d.pos += SnmpBer.vlen;
    SnmpBer.read_header(snmp_ber_work); // contextName
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    *ctxname = d.buf + d.pos;
    *ctxname_len = SnmpBer.vlen;
    d.pos += SnmpBer.vlen;

    const size_t pdu_start = d.pos;
    SnmpBer.read_header(snmp_ber_work); // the PDU header, which stays part of the TLV
    if (!SnmpBer.ok)
    {
        return PROTO_FALSE;
    }
    *pdu = buf + pdu_start;
    *pdu_len = (size_t)(d.pos - pdu_start) + SnmpBer.vlen;
    return d.ok;
}

// The inner request-id of a plaintext ScopedPDU, so a Report can name the request it answers. An
// encrypted msgData has none to read, and reports 0.
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
    SnmpBer.dec = &d;
    SnmpBer.buf.in = pdu;
    SnmpBer.buf.cap = pdu_len;
    SnmpBer.dec_init(snmp_ber_work);
    // parse_scoped read this exact header and sized pdu_len as its header octets plus its content,
    // so only the request-id INTEGER below can fail.
    SnmpBer.read_header(snmp_ber_work);
    if (!SnmpBer.ok)
    {
        return 0;
    }
    SnmpBer.read_integer(snmp_ber_work);
    return SnmpBer.ok ? SnmpBer.ival : 0;
}

// Wrap an already-built ScopedPDU in a complete SNMPv3Message (RFC 3412 sec 6). With privacy the
// ScopedPDU is encrypted first under the RFC 3826 sec 3.1.2.1 IV and carried as an OCTET STRING;
// with authentication the digest is computed over the finished message with
// msgAuthenticationParameters zeroed, then written back into that field (RFC 7860 sec 4.2.1).
static size_t build_message(uint8_t *restrict work, long msg_id, proto_bool auth, proto_bool priv,
                            const uint8_t *scoped, size_t scoped_len, uint8_t *resp, size_t resp_cap)
{
    const uint32_t now = v3_uptime_s();
    const uint8_t *data_ptr = scoped;
    size_t data_len = scoped_len;
    uint8_t salt[SNMP_V3_PRIV_PARAM_LEN];

    if (priv)
    {
        // msgPrivacyParameters: snmpEngineBoots and a per-message counter, both big-endian.
        endian.wr32be(salt, SNMP_V3_CTX(work)->boots);
        endian.wr32be(salt + 4, ++SNMP_V3_CTX(work)->salt_ctr);
        // IV: snmpEngineBoots, snmpEngineTime, then the salt (RFC 3826 sec 3.1.2.1).
        uint8_t iv[16];
        endian.wr32be(iv, SNMP_V3_CTX(work)->boots);
        endian.wr32be(iv + 4, now);
        mem.cpy(iv + 8, salt, SNMP_V3_PRIV_PARAM_LEN);
        if (scoped_len > sizeof(SNMP_V3_CTX(work)->v3_d))
        {
            return 0;
        }
        SnmpCrypto.priv.key = SNMP_V3_CTX(work)->priv_key;
        SnmpCrypto.priv.iv = iv;
        SnmpCrypto.priv.in = scoped;
        SnmpCrypto.priv.out = SNMP_V3_CTX(work)->v3_d;
        SnmpCrypto.priv.len = scoped_len;
        SnmpCrypto.priv.encrypt = PROTO_TRUE;
        SnmpCrypto.aes_cfb128(snmp_crypto_work);
        data_ptr = SNMP_V3_CTX(work)->v3_d;
    }

    // msgSecurityParameters: a SEQUENCE that the message carries inside an OCTET STRING
    // (RFC 3414 sec 2.4).
    BerEnc se;
    SnmpBer.enc = &se;
    SnmpBer.buf.out = SNMP_V3_CTX(work)->v3_sec;
    SnmpBer.buf.cap = sizeof(SNMP_V3_CTX(work)->v3_sec);
    SnmpBer.enc_init(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t ss = SnmpBer.tlv.token;
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBer.tlv.bytes = SNMP_V3_CTX(work)->engine_id; // msgAuthoritativeEngineID
    SnmpBer.tlv.len = SNMP_V3_CTX(work)->engine_id_len;
    SnmpBer.put_octet_string(snmp_ber_work);
    SnmpBer.tlv.ival = (long)SNMP_V3_CTX(work)->boots; // msgAuthoritativeEngineBoots
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.tlv.ival = (long)now; // msgAuthoritativeEngineTime
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBer.tlv.bytes = (const uint8_t *)SNMP_V3_CTX(work)->user; // msgUserName
    SnmpBer.tlv.len = str.len(SNMP_V3_CTX(work)->user, SNMP_V3_USER_MAX);
    SnmpBer.put_octet_string(snmp_ber_work);

    // msgAuthenticationParameters: the field is written as zero octets and the digest lands in it
    // after the message is finished (RFC 7860 sec 4.2.1). zeros stays in scope across the write.
    uint8_t zeros[SNMP_V3_AUTH_PARAM_LEN];
    mem.set(zeros, 0, sizeof(zeros));
    size_t auth_off = 0;
    if (auth)
    {
        auth_off = se.len + 2; // the value follows a tag and a one-octet length, 24 being under 128
        SnmpBer.tlv.bytes = zeros;
        SnmpBer.tlv.len = SNMP_V3_AUTH_PARAM_LEN;
    }
    else
    {
        SnmpBer.tlv.bytes = NULL;
        SnmpBer.tlv.len = 0;
    }
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBer.put_octet_string(snmp_ber_work);

    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBer.tlv.bytes = priv ? salt : NULL; // msgPrivacyParameters
    SnmpBer.tlv.len = priv ? SNMP_V3_PRIV_PARAM_LEN : 0;
    SnmpBer.put_octet_string(snmp_ber_work);
    SnmpBer.tlv.token = ss;
    SnmpBer.seq_end(snmp_ber_work);
    if (!se.ok)
    {
        return 0;
    }
    const size_t sec_len = se.len;

    // The message itself.
    BerEnc e;
    SnmpBer.enc = &e;
    SnmpBer.buf.out = resp;
    SnmpBer.buf.cap = resp_cap;
    SnmpBer.enc_init(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t msg = SnmpBer.tlv.token;
    SnmpBer.tlv.ival = (int)SNMP_V3; // msgVersion
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work); // msgGlobalData
    const size_t hdr = SnmpBer.tlv.token;
    SnmpBer.tlv.ival = msg_id; // msgID
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.tlv.ival = 65507; // msgMaxSize
    SnmpBer.put_integer(snmp_ber_work);
    // msgFlags: authFlag and privFlag, reportableFlag clear on a response (RFC 3412 sec 6.4).
    uint8_t fl = (uint8_t)((auth ? 0x01 : 0) | (priv ? 0x02 : 0));
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBer.tlv.bytes = &fl;
    SnmpBer.tlv.len = 1;
    SnmpBer.put_octet_string(snmp_ber_work);
    SnmpBer.tlv.ival = 3; // msgSecurityModel: USM (RFC 3414)
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.tlv.token = hdr;
    SnmpBer.seq_end(snmp_ber_work);

    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBer.tlv.bytes = SNMP_V3_CTX(work)->v3_sec;
    SnmpBer.tlv.len = sec_len;
    SnmpBer.put_octet_string(snmp_ber_work);
    const size_t sec_value_pos = e.len - sec_len;

    // msgData: an encryptedPDU OCTET STRING, or the plaintext ScopedPDU as it stands.
    SnmpBer.tlv.bytes = data_ptr;
    SnmpBer.tlv.len = data_len;
    if (priv)
    {
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
        SnmpBer.put_octet_string(snmp_ber_work);
    }
    else
    {
        SnmpBer.put_raw(snmp_ber_work);
    }
    SnmpBer.tlv.token = msg;
    SnmpBer.seq_end(snmp_ber_work);
    if (!e.ok)
    {
        return 0;
    }
    const size_t total = e.len;

    if (auth)
    {
        uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
        HmacSha256.mac_args.key = SNMP_V3_CTX(work)->auth_key;
        HmacSha256.mac_args.key_len = SNMP_USM_KEY_LEN;
        HmacSha256.mac_args.data = resp;
        HmacSha256.mac_args.len = total;
        HmacSha256.mac_args.out = mac;
        HmacSha256.mac(SNMP_V3_CTX(work)->mac_work);
        mem.cpy(resp + sec_value_pos + auth_off, mac, SNMP_V3_AUTH_PARAM_LEN);
    }
    return total;
}

// A Report PDU carrying usmStats<stat>.0 = Counter32, wrapped in a v3 message. RFC 3414 sec 4 uses
// it for discovery and RFC 3414 sec 5 names the counters.
static size_t build_report(uint8_t *restrict work, long msg_id, proto_bool auth, uint32_t stat, uint32_t count,
                           long request_id, uint8_t *resp, size_t resp_cap)
{
    uint32_t oid[11];
    for (int i = 0; i < 9; i++)
    {
        oid[i] = kUsmStatsBase[i];
    }
    oid[9] = stat;
    oid[10] = 0; // the instance subidentifier

    BerEnc e;
    SnmpBer.enc = &e;
    SnmpBer.buf.out = SNMP_V3_CTX(work)->v3_b;
    SnmpBer.buf.cap = sizeof(SNMP_V3_CTX(work)->v3_b);
    SnmpBer.enc_init(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_PDU_REPORT;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t pdu = SnmpBer.tlv.token;
    SnmpBer.tlv.ival = request_id;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.tlv.ival = 0; // error-status
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.tlv.ival = 0; // error-index
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t vbl = SnmpBer.tlv.token;
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t vb = SnmpBer.tlv.token;
    SnmpBer.tlv.arcs = oid;
    SnmpBer.tlv.arc_count = 11;
    SnmpBer.put_oid(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_COUNTER32;
    SnmpBer.tlv.uval = count;
    SnmpBer.put_uint(snmp_ber_work);
    SnmpBer.tlv.token = vb;
    SnmpBer.seq_end(snmp_ber_work);
    SnmpBer.tlv.token = vbl;
    SnmpBer.seq_end(snmp_ber_work);
    SnmpBer.tlv.token = pdu;
    SnmpBer.seq_end(snmp_ber_work);
    if (!e.ok)
    {
        return 0;
    }
    const size_t pdu_len = e.len;

    // ScopedPDU { contextEngineID = this engine, contextName = "", the Report PDU }.
    BerEnc sc;
    SnmpBer.enc = &sc;
    SnmpBer.buf.out = SNMP_V3_CTX(work)->v3_c;
    SnmpBer.buf.cap = sizeof(SNMP_V3_CTX(work)->v3_c);
    SnmpBer.enc_init(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t s = SnmpBer.tlv.token;
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBer.tlv.bytes = SNMP_V3_CTX(work)->engine_id;
    SnmpBer.tlv.len = SNMP_V3_CTX(work)->engine_id_len;
    SnmpBer.put_octet_string(snmp_ber_work);
    SnmpBer.tlv.bytes = NULL;
    SnmpBer.tlv.len = 0;
    SnmpBer.put_octet_string(snmp_ber_work);
    SnmpBer.tlv.bytes = SNMP_V3_CTX(work)->v3_b;
    SnmpBer.tlv.len = pdu_len;
    SnmpBer.put_raw(snmp_ber_work);
    SnmpBer.tlv.token = s;
    SnmpBer.seq_end(snmp_ber_work);
    if (!sc.ok)
    {
        return 0;
    }

    return build_message(work, msg_id, auth, PROTO_FALSE, SNMP_V3_CTX(work)->v3_c, sc.len, resp, resp_cap);
}

// ---------------------------------------------------------------------------
// Message processing
// ---------------------------------------------------------------------------

static void v3_process(uint8_t *restrict work)
{
    const uint8_t *req = SnmpV3.msg.req;
    const size_t req_len = SnmpV3.msg.req_len;
    uint8_t *resp = SnmpV3.msg.resp;
    const size_t resp_cap = SnmpV3.msg.resp_cap;
    SnmpV3.n = 0;
    SnmpV3.ok = PROTO_FALSE;

    BerDec d;
    SnmpBer.dec = &d;
    SnmpBer.buf.in = req;
    SnmpBer.buf.cap = req_len;
    SnmpBer.dec_init(snmp_ber_work);
    SnmpBer.read_header(snmp_ber_work);
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return;
    }
    SnmpBer.read_integer(snmp_ber_work); // msgVersion
    if (!SnmpBer.ok || SnmpBer.ival != (int)SNMP_V3)
    {
        return;
    }

    // msgGlobalData (RFC 3412 sec 6).
    SnmpBer.read_header(snmp_ber_work);
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return;
    }
    SnmpBer.read_integer(snmp_ber_work); // msgID
    if (!SnmpBer.ok)
    {
        return;
    }
    const long msg_id = SnmpBer.ival;
    SnmpBer.read_integer(snmp_ber_work); // msgMaxSize, which this responder does not shrink to
    if (!SnmpBer.ok)
    {
        return;
    }
    SnmpBer.read_header(snmp_ber_work); // msgFlags
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING || SnmpBer.vlen < 1)
    {
        return;
    }
    const uint8_t flags = d.buf[d.pos];
    d.pos += SnmpBer.vlen;
    SnmpBer.read_integer(snmp_ber_work);  // msgSecurityModel
    if (!SnmpBer.ok || SnmpBer.ival != 3) // USM only (RFC 3414)
    {
        return;
    }
    const proto_bool req_auth = flags & 0x01;
    const proto_bool req_priv = flags & 0x02;
    if (req_priv && !req_auth)
    {
        return; // RFC 3412 sec 6.4: privFlag without authFlag is not a defined securityLevel
    }

    // msgSecurityParameters: an OCTET STRING wrapping the USM SEQUENCE (RFC 3414 sec 2.4).
    SnmpBer.read_header(snmp_ber_work);
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return;
    }
    const uint8_t *sec = d.buf + d.pos;
    const size_t sec_len = SnmpBer.vlen;
    d.pos += sec_len;

    BerDec sd;
    SnmpBer.dec = &sd;
    SnmpBer.buf.in = sec;
    SnmpBer.buf.cap = sec_len;
    SnmpBer.dec_init(snmp_ber_work);
    SnmpBer.read_header(snmp_ber_work);
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return;
    }
    SnmpBer.read_header(snmp_ber_work); // msgAuthoritativeEngineID
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return;
    }
    const uint8_t *eid = sd.buf + sd.pos;
    const size_t eid_len = SnmpBer.vlen;
    sd.pos += eid_len;
    SnmpBer.read_integer(snmp_ber_work); // msgAuthoritativeEngineBoots
    if (!SnmpBer.ok)
    {
        return;
    }
    const long req_boots = SnmpBer.ival;
    SnmpBer.read_integer(snmp_ber_work); // msgAuthoritativeEngineTime
    if (!SnmpBer.ok)
    {
        return;
    }
    const long req_time = SnmpBer.ival;
    SnmpBer.read_header(snmp_ber_work); // msgUserName
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return;
    }
    const uint8_t *uname = sd.buf + sd.pos;
    const size_t uname_len = SnmpBer.vlen;
    sd.pos += uname_len;
    SnmpBer.read_header(snmp_ber_work); // msgAuthenticationParameters
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return;
    }
    const uint8_t *aparm = sd.buf + sd.pos;
    const size_t aparm_len = SnmpBer.vlen;
    const size_t aparm_off = (size_t)(aparm - req); // its offset in the message the digest covers
    sd.pos += aparm_len;
    SnmpBer.read_header(snmp_ber_work); // msgPrivacyParameters
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return;
    }
    const uint8_t *pparm = sd.buf + sd.pos;
    const size_t pparm_len = SnmpBer.vlen;
    sd.pos += pparm_len;
    if (!sd.ok)
    {
        return;
    }

    const uint8_t *mdata = d.buf + d.pos;
    const size_t mdata_len = req_len - d.pos;

    // Discovery (RFC 3414 sec 4): a request naming an engine this is not is answered with a Report
    // carrying usmStatsUnknownEngineIDs and this engine's snmpEngineID, boots and time.
    const proto_bool engine_match =
        (eid_len == SNMP_V3_CTX(work)->engine_id_len) && (mem.cmp(eid, SNMP_V3_CTX(work)->engine_id, eid_len) == 0);
    if (!engine_match)
    {
        SNMP_V3_CTX(work)->stat_unknown_engine++;
        SnmpV3.n = build_report(work, msg_id, PROTO_FALSE, (int)USM_STAT_UNKNOWN_ENGINE,
                                SNMP_V3_CTX(work)->stat_unknown_engine, inner_request_id(mdata, mdata_len, req_priv),
                                resp, resp_cap);
        return;
    }

    if (!req_auth)
    {
        return; // noAuthNoPriv outside discovery is not served
    }

    // usmStatsUnknownUserNames (RFC 3414 sec 5): msgUserName names no configured user.
    if (!SNMP_V3_CTX(work)->auth_set ||
        !(uname_len == str.len(SNMP_V3_CTX(work)->user, SNMP_V3_USER_MAX) && SNMP_V3_CTX(work)->user[0] &&
          mem.cmp(uname, SNMP_V3_CTX(work)->user, uname_len) == 0))
    {
        SNMP_V3_CTX(work)->stat_unknown_user++;
        SnmpV3.n = build_report(work, msg_id, PROTO_FALSE, (int)USM_STAT_UNKNOWN_USER,
                                SNMP_V3_CTX(work)->stat_unknown_user, 0, resp, resp_cap);
        return;
    }

    // Authentication (RFC 7860 sec 4.2.2): the digest is recomputed over the whole message with
    // msgAuthenticationParameters replaced by zero octets, then compared.
    if (aparm_len != SNMP_V3_AUTH_PARAM_LEN || req_len > sizeof(SNMP_V3_CTX(work)->v3_a))
    {
        SNMP_V3_CTX(work)->stat_wrong_digest++;
        SnmpV3.n = build_report(work, msg_id, PROTO_FALSE, (int)USM_STAT_WRONG_DIGEST,
                                SNMP_V3_CTX(work)->stat_wrong_digest, 0, resp, resp_cap);
        return;
    }
    mem.cpy(SNMP_V3_CTX(work)->v3_a, req, req_len);
    mem.set(SNMP_V3_CTX(work)->v3_a + aparm_off, 0, SNMP_V3_AUTH_PARAM_LEN);
    uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
    HmacSha256.mac_args.key = SNMP_V3_CTX(work)->auth_key;
    HmacSha256.mac_args.key_len = SNMP_USM_KEY_LEN;
    HmacSha256.mac_args.data = SNMP_V3_CTX(work)->v3_a;
    HmacSha256.mac_args.len = req_len;
    HmacSha256.mac_args.out = mac;
    HmacSha256.mac(SNMP_V3_CTX(work)->mac_work);
    if (!ct_eq(mac, aparm, SNMP_V3_AUTH_PARAM_LEN))
    {
        SNMP_V3_CTX(work)->stat_wrong_digest++;
        SnmpV3.n = build_report(work, msg_id, PROTO_FALSE, (int)USM_STAT_WRONG_DIGEST,
                                SNMP_V3_CTX(work)->stat_wrong_digest, 0, resp, resp_cap);
        return;
    }

    // Timeliness (RFC 3414 sec 2.2.3): the boot count must match and the time must fall inside the
    // 150-second window.
    const uint32_t now = v3_uptime_s();
    long dt = (long)now - req_time;
    if (dt < 0)
    {
        dt = -dt;
    }
    if ((uint32_t)req_boots != SNMP_V3_CTX(work)->boots || dt > 150)
    {
        SNMP_V3_CTX(work)->stat_not_in_time++;
        SnmpV3.n = build_report(work, msg_id, PROTO_TRUE, (int)USM_STAT_NOT_IN_TIME,
                                SNMP_V3_CTX(work)->stat_not_in_time, 0, resp, resp_cap);
        return;
    }

    // Privacy: an encryptedPDU is decrypted under the IV of RFC 3826 sec 3.1.2.1, whose salt is the
    // sender's msgPrivacyParameters.
    const uint8_t *scoped;
    size_t scoped_len;
    if (req_priv)
    {
        if (!SNMP_V3_CTX(work)->priv_set || pparm_len != SNMP_V3_PRIV_PARAM_LEN)
        {
            SNMP_V3_CTX(work)->stat_decrypt++;
            SnmpV3.n = build_report(work, msg_id, PROTO_TRUE, (int)USM_STAT_DECRYPT, SNMP_V3_CTX(work)->stat_decrypt, 0,
                                    resp, resp_cap);
            return;
        }
        BerDec md;
        SnmpBer.dec = &md;
        SnmpBer.buf.in = mdata;
        SnmpBer.buf.cap = mdata_len;
        SnmpBer.dec_init(snmp_ber_work);
        SnmpBer.read_header(snmp_ber_work);
        if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
        {
            return;
        }
        const uint8_t *ct = md.buf + md.pos;
        const size_t ct_len = SnmpBer.vlen;
        if (ct_len > sizeof(SNMP_V3_CTX(work)->v3_a))
        {
            return;
        }
        uint8_t iv[16];
        endian.wr32be(iv, (uint32_t)req_boots);
        endian.wr32be(iv + 4, (uint32_t)req_time);
        mem.cpy(iv + 8, pparm, SNMP_V3_PRIV_PARAM_LEN);
        SnmpCrypto.priv.key = SNMP_V3_CTX(work)->priv_key;
        SnmpCrypto.priv.iv = iv;
        SnmpCrypto.priv.in = ct;
        SnmpCrypto.priv.out = SNMP_V3_CTX(work)->v3_a;
        SnmpCrypto.priv.len = ct_len;
        SnmpCrypto.priv.encrypt = PROTO_FALSE;
        SnmpCrypto.aes_cfb128(snmp_crypto_work);
        scoped = SNMP_V3_CTX(work)->v3_a;
        scoped_len = ct_len;
    }
    else
    {
        scoped = mdata;
        scoped_len = mdata_len;
    }

    // The inner PDU goes to the shared MIB core, authenticated, so writes are allowed and the v2c
    // per-binding exceptions apply.
    const uint8_t *ctxname;
    const uint8_t *pdu;
    size_t ctxname_len;
    size_t pdu_len;
    if (!parse_scoped(scoped, scoped_len, &ctxname, &ctxname_len, &pdu, &pdu_len))
    {
        return;
    }
    SnmpAgent.pdu.req = pdu;
    SnmpAgent.pdu.req_len = pdu_len;
    SnmpAgent.pdu.allow_write = PROTO_TRUE;
    SnmpAgent.pdu.v2c = PROTO_TRUE;
    SnmpAgent.pdu.out = SNMP_V3_CTX(work)->v3_b;
    SnmpAgent.pdu.out_cap = sizeof(SNMP_V3_CTX(work)->v3_b);
    SnmpAgent.dispatch_pdu(protocore_snmp_agent_span());
    const size_t rpdu = SnmpAgent.n;
    if (rpdu == 0)
    {
        return;
    }

    // The response ScopedPDU: this engine's ID, the contextName echoed back, the Response-PDU.
    BerEnc sc;
    SnmpBer.enc = &sc;
    SnmpBer.buf.out = SNMP_V3_CTX(work)->v3_c;
    SnmpBer.buf.cap = sizeof(SNMP_V3_CTX(work)->v3_c);
    SnmpBer.enc_init(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t s = SnmpBer.tlv.token;
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBer.tlv.bytes = SNMP_V3_CTX(work)->engine_id;
    SnmpBer.tlv.len = SNMP_V3_CTX(work)->engine_id_len;
    SnmpBer.put_octet_string(snmp_ber_work);
    SnmpBer.tlv.bytes = ctxname;
    SnmpBer.tlv.len = ctxname_len;
    SnmpBer.put_octet_string(snmp_ber_work);
    SnmpBer.tlv.bytes = SNMP_V3_CTX(work)->v3_b;
    SnmpBer.tlv.len = rpdu;
    SnmpBer.put_raw(snmp_ber_work);
    SnmpBer.tlv.token = s;
    SnmpBer.seq_end(snmp_ber_work);
    if (!sc.ok)
    {
        return;
    }

    SnmpV3.n = build_message(work, msg_id, PROTO_TRUE, req_priv, SNMP_V3_CTX(work)->v3_c, sc.len, resp, resp_cap);
    SnmpV3.ok = (SnmpV3.n != 0);
}

#if PROTOCORE_ENABLE_SNMP_TRAP

// A v3 notification: authenticated always, encrypted when a privacy password is configured. The
// PDU is the notify module's, the ScopedPDU and the message are this one's. pdu_tag selects the
// SNMPv2-Trap-PDU or the InformRequest-PDU.
static void send_notify(uint8_t *restrict work, uint8_t pdu_tag)
{
    SnmpV3.ok = PROTO_FALSE;
    SnmpV3.n = 0;
    if (!SNMP_V3_CTX(work)->auth_set)
    {
        return; // a v3 notification carries a digest or it does not leave
    }

    // The notification PDU into the inner-PDU scratch.
    BerEnc e;
    SnmpBer.enc = &e;
    SnmpBer.buf.out = SNMP_V3_CTX(work)->v3_b;
    SnmpBer.buf.cap = sizeof(SNMP_V3_CTX(work)->v3_b);
    SnmpBer.enc_init(snmp_ber_work);
    SnmpNotify.buf.enc = &e;
    SnmpNotify.pdu.pdu_tag = pdu_tag;
    SnmpNotify.pdu.request_id = SnmpV3.notify.request_id;
    SnmpNotify.pdu.trap_oid = SnmpV3.notify.trap_oid;
    SnmpNotify.pdu.trap_oid_len = SnmpV3.notify.trap_oid_len;
    SnmpNotify.pdu.uptime_ticks = v3_uptime_s() * 100; // TimeTicks: hundredths of a second
    SnmpNotify.pdu.vbs = SnmpV3.notify.vbs;
    SnmpNotify.pdu.vb_count = SnmpV3.notify.vb_count;
    SnmpNotify.build_pdu(protocore_snmp_notify_span());
    if (!e.ok)
    {
        return;
    }
    const size_t pdu_len = e.len;

    // ScopedPDU { contextEngineID = this engine, contextName = "", the notification PDU }.
    BerEnc sc;
    SnmpBer.enc = &sc;
    SnmpBer.buf.out = SNMP_V3_CTX(work)->v3_c;
    SnmpBer.buf.cap = sizeof(SNMP_V3_CTX(work)->v3_c);
    SnmpBer.enc_init(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t s = SnmpBer.tlv.token;
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBer.tlv.bytes = SNMP_V3_CTX(work)->engine_id;
    SnmpBer.tlv.len = SNMP_V3_CTX(work)->engine_id_len;
    SnmpBer.put_octet_string(snmp_ber_work);
    SnmpBer.tlv.bytes = NULL;
    SnmpBer.tlv.len = 0;
    SnmpBer.put_octet_string(snmp_ber_work);
    SnmpBer.tlv.bytes = SNMP_V3_CTX(work)->v3_b;
    SnmpBer.tlv.len = pdu_len;
    SnmpBer.put_raw(snmp_ber_work);
    SnmpBer.tlv.token = s;
    SnmpBer.seq_end(snmp_ber_work);
    if (!sc.ok)
    {
        return;
    }

    const size_t len =
        build_message(work, (long)SnmpV3.notify.request_id, SNMP_V3_CTX(work)->auth_set, SNMP_V3_CTX(work)->priv_set,
                      SNMP_V3_CTX(work)->v3_c, sc.len, SNMP_V3_CTX(work)->v3_tx, sizeof(SNMP_V3_CTX(work)->v3_tx));
    SnmpV3.n = len;
    if (len == 0)
    {
        return;
    }
    protocore_ip dst = {PROTOCORE_IP_NONE, {0}};
    Ip.args.text = SnmpV3.notify.dst_ip;
    Ip.args.out = &dst;
    Ip.parse(ip_work);
    if (!Ip.ok)
    {
        return;
    }
    UdpClient.dst = &dst;
    UdpClient.dst_port = SnmpV3.notify.port;
    UdpClient.data = SNMP_V3_CTX(work)->v3_tx;
    UdpClient.len = len;
    UdpClient.sendto(protocore_udp_client_span());
    SnmpV3.ok = UdpClient.ok;
}

// SNMPv2-Trap-PDU in a v3 message (RFC 3416 sec 4.2.6): unacknowledged, so the request-id is
// informational and the caller may leave it at whatever it last set.
static void v3_trap(uint8_t *restrict work)
{
    send_notify(work, (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2);
}

// InformRequest-PDU in a v3 message (RFC 3416 sec 4.2.7): confirmed, so the caller owns the
// request-id the receiver's Response-PDU echoes and retransmits until that Response arrives.
static void v3_inform(uint8_t *restrict work)
{
    send_notify(work, (uint8_t)SNMP_TAG_SNMP_PDU_INFORM);
}

#endif // PROTOCORE_ENABLE_SNMP_TRAP

// Designated, so a member's position in the struct does not decide what it binds to.
SnmpV3Ns SnmpV3 = {.init = v3_init,
                   .set_user = v3_set_user,
                   .set_boots = v3_set_boots,
                   .get_boots = v3_get_boots,
                   .process = v3_process,
#if PROTOCORE_ENABLE_SNMP_TRAP
                   .trap = v3_trap,
                   .inform = v3_inform,
#endif
};

#endif // PROTOCORE_ENABLE_SNMP_V3
