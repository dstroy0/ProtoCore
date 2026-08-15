// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file client.c
 * @brief The client engine: RFC 4253 handshake, RFC 4252 auth, RFC 4254 channels, outbound.
 */

#include "network_drivers/presentation/ssh/client/client.h"
#include "crypto/asymmetric/bignum.h"     // bn_expmod_group14 (dh-group14 client)
#include "crypto/asymmetric/curve25519.h" // protocore_x25519 (curve25519-sha256)
#include "crypto/asymmetric/ecdsa.h"      // ecdh-sha2-nistp256 + ecdsa host-key verify
#include "crypto/asymmetric/ed25519.h"    // ssh-ed25519 host key + client auth
#include "crypto/hash/sha256.h"
#include "crypto/rng/rng.h"  // protocore_rand_fill
#include "mmgr/arena.h"      // protocore_worker_set_self (own scratch slot)
#include "mmgr/bytes.h"      // bytes.* writers / bytes.rd_str() - the byte verbs this file frames with
#include "mmgr/plaintext.h"  // protocore_plaintext_alloc for the large hybrid C_INIT
#include "mmgr/protoframe.h" // the one frame engine
#include "mmgr/protomem.h"
#include "mmgr/rawmemcpy.h" // raw.put_u32 - one unaligned store, not four
#include "mmgr/secure.h"
#include "network_drivers/presentation/ssh/auth/auth.h"
#include "network_drivers/presentation/ssh/connection/connection.h"
#include "network_drivers/presentation/ssh/network/network.h"
#include "network_drivers/presentation/ssh/ssh.h"
#include "network_drivers/presentation/ssh/transport/ssh_kexhash.h" // SshKexHash (SHA-256/SHA-512 by method)
#include "network_drivers/presentation/ssh/transport/ssh_rsa.h"     // rsa-sha2-256/512 host-key verify
#include "network_drivers/presentation/ssh/transport/transport.h"
#include "network_drivers/transport/tcp/client/client.h" // TcpClient: the dialed connection
#include "network_drivers/transport/tcp/tcp.h" // protocore_client_*
#include "server/clock/clock.h"                // protocore_millis, pcdelay
#include "shared/log/log.h"
#if PROTOCORE_ENABLE_PQC_KEX
#include "crypto/pqc/mlkem.h" // mlkem768x25519-sha256 hybrid (client: KeyGen + Decaps)
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
#include "crypto/pqc/sntrup761.h" // sntrup761x25519-sha512 hybrid (client: KeyGen + Decaps)
#endif

#if PROTOCORE_ENABLE_SSH_CLIENT

// ---------------------------------------------------------------------------
// Port-forward logging (RFC 4254 sec 7)
// ---------------------------------------------------------------------------

// Log frames: each message's shape is fixed here, so nothing is parsed when one is emitted.
static const protocore_field LOG_FWD_FAIL[] = {{PROTOCORE_FK_LIT, 0, 9, "ssh-fwd: "}, PROTOCORE_STR, PROTOCORE_END};
static const protocore_field LOG_FWD_OPEN[] = {
    {PROTOCORE_FK_LIT, 0, 46, "ssh-fwd: forwarded-tcpip open, local connect(:"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 6, ") cid="},
    PROTOCORE_I64,
    PROTOCORE_END};
static const protocore_field LOG_FWD_UP[] = {{PROTOCORE_FK_LIT, 0, 40, "ssh-fwd: tcpip-forward up (remote port :"},
                                             PROTOCORE_U32,
                                             {PROTOCORE_FK_LIT, 0, 1, ")"},
                                             PROTOCORE_END};

// ---------------------------------------------------------------------------
// Client session state
// ---------------------------------------------------------------------------

#define SSH_CLI_SLOT 0 // the SSH packet/key pool slot the client borrows (MAX_SSH_CONNS >= 1)

typedef struct
{
    protocore_ssh_client_cfg cfg;
    protocore_ssh_client_state state;

    int cid; ///< relay TCP connection (protocore_client), or -1.
    uint32_t deadline_ms;

    SshKexAlg kex;         ///< negotiated key exchange.
    SshHostkeyAlg hostkey; ///< negotiated host-key / signature type.
    uint8_t cipher;        ///< negotiated SSH_CIPHER_*.
    uint8_t mac;           ///< negotiated SSH_MAC_* (used only when cipher == aes256-ctr).

    uint8_t kex_priv[32]; ///< our KEX private: X25519 scalar / P-256 d / DH exponent (wiped after K).
    uint8_t qc[256];      ///< our KEX public Q_C: 32 (curve/hybrid X25519) / 65 (ecdh) / 256 (DH e, big-endian).
    size_t qc_len;
#if PROTOCORE_ENABLE_PQC_KEX || PROTOCORE_ENABLE_SSH_SNTRUP761
    // Hybrid decapsulation key: persists from C_INIT (KeyGen) to S_REPLY (Decaps) across round-trips,
    // so it cannot live in the per-dispatch scratch arena. Only one hybrid is negotiated per session, so
    // ML-KEM's dk and sntrup761's sk share storage. Each embeds its public key (ek at [1152..], pk at
    // PROTOCORE_SNTRUP761_SK_PK_OFFSET), so the C_INIT / exchange-hash reconstruct it from here, not stored twice.
    union {
#if PROTOCORE_ENABLE_PQC_KEX
        uint8_t mlkem_dk[MLKEM768_DK_BYTES];
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
        uint8_t sntrup_sk[PROTOCORE_SNTRUP761_SK_BYTES];
#endif
    } hyb;
#endif

} SshClientStorage;

/**
 * @brief The dialling role's state and the calls that reach it - what SshClientNs points at.
 *
 * @var SshClientInternal::store  the session: the relay connection, the negotiated methods, and the
 *                                key material that must persist across round-trips
 * @var SshClientInternal::ns     the handle a caller sets a call's members on
 */
struct SshClientInternal
{
    SshClientStorage *store;
    SshClientNs *ns;
};

static SshClientStorage s_store;

static struct SshClientInternal s_cli = {.store = &s_store, .ns = &SshClient};

// The client engine is one translation unit; these are defined below in dependency order.
static void cli_send(struct SshClientInternal *restrict ctx);
static void cli_crypto_work(struct SshClientInternal *restrict ctx);
static void cli_fail(const char *why);
static proto_bool build_kexinit(void);
static proto_bool build_kex_public(void);
static proto_bool compute_k(const uint8_t *srv_pub, uint32_t srv_pub_len, uint8_t k_be[256]);
static proto_bool send_service_request(void);
static proto_bool handle_server_kexinit(const uint8_t *p, size_t len);
static proto_bool handle_kexdh_reply(const uint8_t *p, size_t len);
static void cli_wipe(void);

// ---------------------------------------------------------------------------
// RFC 4253 sec 10 - the outbound role's service request
// ---------------------------------------------------------------------------

static proto_bool send_service_request(void)
{
    uint8_t out[1 + 4 + 12];
    protocore_span w = span.from(out, sizeof(out));
    bytes.put(&w, SSH_MSG_SERVICE_REQUEST);
    protocore_ssh_wr_cstr(&w, "ssh-userauth");
    return span.ok(w) && (SshClient.payload = out, SshClient.len = w.pos, cli_send(\& s_cli), SshClient.ok);
}

// ---------------------------------------------------------------------------
// RFC 4253 sec 8 - the outbound role's shared secret, exchange hash and host-key check
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// KEXDH_REPLY: compute K, exchange hash H, verify the host signature, derive keys, send NEWKEYS
// ---------------------------------------------------------------------------

// Compute the shared secret K (right-aligned into k_be[256]) for the negotiated method.
// RFC 4253 sec 8: K comes from the transport, which owns the method switch for both roles.
static proto_bool compute_k(const uint8_t *srv_pub, uint32_t srv_pub_len, uint8_t k_be[256])
{
    SshKexEphemeral e = {.alg = s_store.kex,
                         .priv = s_store.kex_priv,
                         .hybrid_sk = NULL,
                         .work = (cli_crypto_work(&s_cli), SshClient.work)};
#if PROTOCORE_ENABLE_PQC_KEX
    if (s_store.kex == SSH_KEX_MLKEM768_X25519)
    {
        e.hybrid_sk = s_store.hyb.mlkem_dk;
    }
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
    if (s_store.kex == SSH_KEX_SNTRUP761_X25519)
    {
        e.hybrid_sk = s_store.hyb.sntrup_sk;
    }
#endif
    return ssh_kex_shared_secret(&e, srv_pub, srv_pub_len, k_be);
}

static proto_bool handle_kexdh_reply(const uint8_t *p, size_t len)
{
    Rd r = {p, len, 0, PROTO_TRUE};
    if (protocore_ssh_rd_u8(&r) != SSH_MSG_KEXDH_REPLY)
    {
        return PROTO_FALSE;
    }
    uint32_t ks_len;
    const uint8_t *ks = protocore_ssh_rd_string(&r, &ks_len); // K_S host-key blob
    uint32_t sp_len;
    const uint8_t *srv_pub = protocore_ssh_rd_string(&r, &sp_len); // Q_S (string) or f (mpint)
    uint32_t sig_len;
    const uint8_t *sig = protocore_ssh_rd_string(&r, &sig_len); // signature blob
    if (!r.ok)
    {
        return PROTO_FALSE;
    }

    // Pin the relay by the SHA-256 fingerprint of its host-key blob (type-agnostic, like known_hosts).
    uint8_t *fwork = (cli_crypto_work(&s_cli), SshClient.work);
    if (fwork == NULL)
    {
        return PROTO_FALSE;
    }
    uint8_t fp[32];
    protocore_sha256_ctx fc;
    protocore_sha256_init(&fc, fwork);
    protocore_sha256_update(&fc, ks, ks_len);
    protocore_sha256_final(&fc, fp);
    if (mem.cmp(fp, s_store.cfg.host_pin, 32) != 0)
    {
        cli_fail("relay host key does not match the pin");
        return PROTO_FALSE;
    }

    uint8_t k_be[256];
    if (!compute_k(srv_pub, sp_len, k_be))
    {
        return PROTO_FALSE;
    }

    // RFC 4253 sec 8 H, the one implementation both roles consume. The exchange values are SSH
    // strings for an ECDH or hybrid KEX and mpints for dh-group14; K is a string only for a hybrid,
    // where it is a fixed 32/64-byte hash sitting at the tail of k_be.
    const SshSession *hs = &ssh_sess[SSH_CLI_SLOT];
    proto_bool pub_is_string = s_store.kex != SSH_KEX_DH_GROUP14;
    proto_bool k_is_string = PROTO_FALSE;
    const uint8_t *k_hash = k_be;
    size_t k_hash_len = 256;
#if PROTOCORE_ENABLE_PQC_KEX
    if (s_store.kex == SSH_KEX_MLKEM768_X25519)
    {
        k_is_string = PROTO_TRUE;
        k_hash_len = 32;
        k_hash = k_be + (256 - k_hash_len);
    }
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
    if (s_store.kex == SSH_KEX_SNTRUP761_X25519)
    {
        k_is_string = PROTO_TRUE;
        k_hash_len = 64;
        k_hash = k_be + (256 - k_hash_len);
    }
#endif
    uint8_t H[SSH_KEXHASH_MAX_LEN];
    size_t h_len = 0;
    if (SSH_TRANSPORT->exchange_hash(SSH_CLI_SLOT, pub_is_string, hs->cpub, hs->cpub_len, srv_pub, sp_len, k_hash,
                                     k_hash_len, ks, ks_len, H, &h_len, k_is_string,
                                     ssh_kex_is_sha512(s_store.kex)) != 0)
    {
        protocore_secure_wipe(k_be, sizeof(k_be));
        return PROTO_FALSE;
    }

    if (!ssh_hostkey_verify(SSH_CLI_SLOT, ks, ks_len, sig, sig_len, H, h_len))
    {
        protocore_secure_wipe(k_be, sizeof(k_be));
        cli_fail("relay signature verification failed");
        return PROTO_FALSE;
    }

    // RFC 4253 sec 7.2: the first exchange's H becomes the session identifier, which the transport
    // owns for both roles.
    ssh_session_id_latch(SSH_CLI_SLOT, H, h_len);
    size_t sid_len = 0;
    const uint8_t *sid = ssh_session_id(SSH_CLI_SLOT, &sid_len);
    if (sid == NULL)
    {
        protocore_secure_wipe(k_be, sizeof(k_be));
        cli_fail("no session identifier after key exchange");
        return PROTO_FALSE;
    }

    // ssh_kex_install_keys populates c2s/s2c per the RFC 4253 §7.2 letters for the negotiated
    // cipher/MAC; the packet layer's is_client flag selects the send/receive direction. The hybrids
    // encode K as a fixed 32/64-byte string (k_is_string); the classical methods as an mpint. The
    // -sha512 method derives over SHA-512 (is512), so H and the session_id are 64 bytes.
    const proto_bool is512 = ssh_kex_is_sha512(s_store.kex);
    const SshKdfInputs kdf_in = {.work = (cli_crypto_work(&s_cli), SshClient.work),
                                 .K_be = k_be,
                                 .H = H,
                                 .session_id = sid,
                                 .h_len = h_len,
                                 .sid_len = sid_len,
                                 .k_is_string = k_is_string,
                                 .is512 = is512};
    ssh_kex_install_keys(SSH_CLI_SLOT, &kdf_in);
    protocore_secure_wipe(k_be, sizeof(k_be));
    protocore_secure_wipe(s_store.kex_priv, sizeof(s_store.kex_priv));
#if PROTOCORE_ENABLE_PQC_KEX || PROTOCORE_ENABLE_SSH_SNTRUP761
    protocore_secure_wipe((uint8_t *)&s_store.hyb, sizeof(s_store.hyb));
#endif

    uint8_t nk = SSH_MSG_NEWKEYS;
    if (!(SshClient.payload = &nk, SshClient.len = 1, cli_send(\& s_cli), SshClient.ok))
    {
        return PROTO_FALSE;
    }
    SSH_TRANSPORT->newkeys_sent(SSH_CLI_SLOT); // outbound switches to the epoch this exchange derived
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// RFC 4253 sec 7.1 and sec 8 - the outbound role's KEXINIT and ephemeral
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Algorithm negotiation + KEXINIT (client)
// ---------------------------------------------------------------------------

// RFC 4253 sec 7.1 build, the one implementation both roles consume. The payload is built in the
// connection's own storage and kept as I_C by the transport.
static proto_bool build_kexinit(void)
{
    uint8_t *base = ssh_conn_slot(SSH_CLI_SLOT);
    if (base == NULL)
    {
        return PROTO_FALSE;
    }
    uint8_t *out = base + SSH_OFF_KEXINIT;
    size_t n = 0;
    if (SSH_TRANSPORT->kexinit_build(SSH_CLI_SLOT, out, &n, PROTOCORE_SSH_KEXINIT_S_MAX) != 0)
    {
        return PROTO_FALSE;
    }
    return (SshClient.payload = out, SshClient.len = n, cli_send(\& s_cli), SshClient.ok);
}

// Generate our KEX ephemeral for the negotiated method and build Q_C / e into s_store.qc.
static proto_bool build_kex_public(void)
{
    switch (s_store.kex)
    {
    case SSH_KEX_CURVE25519:
        protocore_rand_fill(s_store.kex_priv, 32);
        protocore_x25519_base(s_store.qc, s_store.kex_priv);
        s_store.qc_len = 32;
        return PROTO_TRUE;
    case SSH_KEX_ECDH_NISTP256:
        // Draw a valid P-256 scalar (pubkey derivation rejects 0 / >= group order).
        for (int tries = 0; tries < 8; tries++)
        {
            protocore_rand_fill(s_store.kex_priv, 32);
            if (protocore_ecdsa_p256_pubkey(s_store.qc, s_store.kex_priv))
            {
                s_store.qc_len = PROTOCORE_ECDSA_P256_PUB_LEN; // 65
                return PROTO_TRUE;
            }
        }
        return PROTO_FALSE;
    case SSH_KEX_DH_GROUP14: {
        // e = g^x mod p, g = 2 (RFC 3526 group 14). x is a 256-bit exponent.
        protocore_rand_fill(s_store.kex_priv, 32);
        protocore_bignum g, x, e;
        uint8_t two = 2;
        bn_from_bytes(&g, &two, 1);
        bn_from_bytes(&x, s_store.kex_priv, 32);
        bn_expmod_group14(&e, &g, &x);
        bn_to_bytes(s_store.qc, &e);
        s_store.qc_len = 256;
        protocore_secure_wipe(&x, sizeof(x));
        protocore_secure_wipe(&e, sizeof(e));
        return PROTO_TRUE;
    }
#if PROTOCORE_ENABLE_PQC_KEX
    case SSH_KEX_MLKEM768_X25519: {
        // ML-KEM-768 keypair (dk kept for Decaps; ek is embedded in dk) + an X25519 ephemeral. C_INIT
        // (ek || Q_C) is assembled at send time; Q_C lives in qc[0..31].
        uint8_t d[32], z[32], ek[MLKEM768_EK_BYTES];
        protocore_rand_fill(d, sizeof(d));
        protocore_rand_fill(z, sizeof(z));
        protocore_mlkem768_keygen(d, z, ek, s_store.hyb.mlkem_dk);
        protocore_secure_wipe(d, sizeof(d));
        protocore_secure_wipe(z, sizeof(z));
        protocore_secure_wipe(ek, sizeof(ek)); // ek persists inside mlkem_dk
        protocore_rand_fill(s_store.kex_priv, 32);
        protocore_x25519_base(s_store.qc, s_store.kex_priv);
        s_store.qc_len = 32;
        return PROTO_TRUE;
    }
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
    case SSH_KEX_SNTRUP761_X25519: {
        // sntrup761 keypair (sk kept for Decaps; pk is embedded in sk) + an X25519 ephemeral. C_INIT
        // (pk || Q_C) is assembled at send time from sk; Q_C lives in qc[0..31]. pk is only needed
        // transiently here (sk embeds a copy), so it borrows the scratch arena.
        uint8_t *work = (cli_crypto_work(&s_cli), SshClient.work);
        if (work == NULL)
        {
            return PROTO_FALSE;
        }
        size_t mark = protocore_plaintext_mark();
        uint8_t *pk = (uint8_t *)protocore_plaintext_alloc(PROTOCORE_SNTRUP761_PK_BYTES, 1);
        if (!pk)
        {
            return PROTO_FALSE;
        }
        protocore_sntrup761_keypair(work, pk, s_store.hyb.sntrup_sk);
        protocore_plaintext_release(mark); // pk persists inside sntrup_sk at PROTOCORE_SNTRUP761_SK_PK_OFFSET
        protocore_rand_fill(s_store.kex_priv, 32);
        protocore_x25519_base(s_store.qc, s_store.kex_priv);
        s_store.qc_len = 32;
        return PROTO_TRUE;
    }
#endif
    }
    return PROTO_FALSE;
}

// Parse the server KEXINIT, negotiate every category, store I_S, and send KEXDH_INIT.
static proto_bool handle_server_kexinit(const uint8_t *p, size_t len)
{
    // RFC 4253 sec 7.1 parse + negotiate, the one implementation both roles consume.
    if (SSH_TRANSPORT->kexinit_parse(SSH_CLI_SLOT, p, len) != 0)
    {
        return PROTO_FALSE;
    }
    const SshSession *ns = &ssh_sess[SSH_CLI_SLOT];
    s_store.kex = ns->kex_alg;
    s_store.hostkey = ns->hostkey_alg;
    s_store.cipher = ns->cipher_alg_s2c;
    s_store.mac = ns->mac_alg_s2c;

    if (!build_kex_public())
    {
        return PROTO_FALSE;
    }

#if PROTOCORE_ENABLE_PQC_KEX
    if (s_store.kex == SSH_KEX_MLKEM768_X25519)
    {
        // KEX_HYBRID_INIT (msg 30): string(C_INIT) where C_INIT = ek || Q_C (1216 B). Too large for
        // the stack packet buffer, so build it in the client's scratch arena.
        SshSession *hs = &ssh_sess[SSH_CLI_SLOT];
        const uint8_t *ek = s_store.hyb.mlkem_dk + 1152; // ek follows the 1152-byte dk_pke in dk
        const size_t clen = MLKEM768_EK_BYTES + 32;
        mem.cpy(hs->cpub, ek, MLKEM768_EK_BYTES);
        mem.cpy(hs->cpub + MLKEM768_EK_BYTES, s_store.qc, 32);
        hs->cpub_len = (uint16_t)clen;
        const size_t plen = 1 + 4 + clen;
        size_t mark = protocore_plaintext_mark();
        uint8_t *out = (uint8_t *)protocore_plaintext_alloc(plen, 1);
        if (!out)
        {
            return PROTO_FALSE;
        }
        protocore_span w = span.from(out, plen);
        bytes.put(&w, SSH_MSG_KEXDH_INIT);
        bytes.put_be(&w, (uint32_t)clen, 4);
        bytes.raw(&w, hs->cpub, clen);
        proto_bool ok =
            span.ok(w) && (SshClient.payload = out, SshClient.len = w.pos, cli_send(\& s_cli), SshClient.ok);
        protocore_plaintext_release(mark);
        return ok;
    }
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
    if (s_store.kex == SSH_KEX_SNTRUP761_X25519)
    {
        // KEX_HYBRID_INIT (msg 30): string(C_INIT) where C_INIT = sntrup761_pk || Q_C (1190 B). pk is
        // reconstructed from sk (it is embedded there); too large for the stack packet buffer, so the
        // packet is built in the client's scratch arena.
        SshSession *hs = &ssh_sess[SSH_CLI_SLOT];
        const uint8_t *pk = s_store.hyb.sntrup_sk + PROTOCORE_SNTRUP761_SK_PK_OFFSET;
        const size_t clen = PROTOCORE_SNTRUP761_PK_BYTES + 32;
        mem.cpy(hs->cpub, pk, PROTOCORE_SNTRUP761_PK_BYTES);
        mem.cpy(hs->cpub + PROTOCORE_SNTRUP761_PK_BYTES, s_store.qc, 32);
        hs->cpub_len = (uint16_t)clen;
        const size_t plen = 1 + 4 + clen;
        size_t mark = protocore_plaintext_mark();
        uint8_t *out = (uint8_t *)protocore_plaintext_alloc(plen, 1);
        if (!out)
        {
            return PROTO_FALSE;
        }
        protocore_span w = span.from(out, plen);
        bytes.put(&w, SSH_MSG_KEXDH_INIT);
        bytes.put_be(&w, (uint32_t)clen, 4);
        bytes.raw(&w, hs->cpub, clen);
        proto_bool ok =
            span.ok(w) && (SshClient.payload = out, SshClient.len = w.pos, cli_send(\& s_cli), SshClient.ok);
        protocore_plaintext_release(mark);
        return ok;
    }
#endif

    // KEXDH_INIT (msg 30): string(Q_C) for curve/ecdh, mpint(e) for DH. The exchange hash takes the
    // value as it stands here, before either encoding is applied.
    SshSession *hs = &ssh_sess[SSH_CLI_SLOT];
    mem.cpy(hs->cpub, s_store.qc, s_store.qc_len);
    hs->cpub_len = (uint16_t)s_store.qc_len;

    uint8_t out[1 + 4 + 260];
    protocore_span w = span.from(out, sizeof(out));
    bytes.put(&w, SSH_MSG_KEXDH_INIT);
    if (s_store.kex == SSH_KEX_DH_GROUP14)
    {
        // mpint(e): minimal, with a sign byte if the top bit is set.
        uint32_t i = 0;
        while (i < s_store.qc_len && s_store.qc[i] == 0)
        {
            i++;
        }
        size_t mag = s_store.qc_len - i;
        proto_bool pad = (mag > 0 && (s_store.qc[i] & 0x80) != 0);
        bytes.put_be(&w, (uint32_t)(mag + (pad ? 1 : 0)), 4);
        if (pad)
        {
            bytes.put(&w, 0);
        }
        bytes.raw(&w, s_store.qc + i, mag);
    }
    else
    {
        protocore_ssh_wr_str(&w, s_store.qc, s_store.qc_len);
    }
    return span.ok(w) && (SshClient.payload = out, SshClient.len = w.pos, cli_send(\& s_cli), SshClient.ok);
}

// ---------------------------------------------------------------------------
// RFC 4253 - the outbound role's slot, scratch and framing
// ---------------------------------------------------------------------------

// The client is one connection on one slot, so its handshake crypto works out of that slot's bytes -
// the same borrow the wire and the packet MAC come from. Null when the pool cannot cover the slot.
static void cli_crypto_work(struct SshClientInternal *restrict ctx)
{
    if (!ssh_pkt_slot_storage(&ssh_pkt[SSH_CLI_SLOT]))
    {
        ctx->ns->work = NULL;
        return;
    }
    ctx->ns->work = ssh_pkt[SSH_CLI_SLOT].crypto_work;
}

// Algorithm names, in the client's preference order per category (first = most preferred). The
// client offers every algorithm and negotiates whatever the relay supports. It reuses the transport
// crypto, so each primitive uses whatever hardware the target provides: RSA/DH on the MPI unit,
// AES/SHA on their units, and NIST P-256 (ecdh-sha2-nistp256 + ecdsa-sha2-nistp256) on the ECC /
// ECDSA accelerators where they exist - on the P4 mbedTLS's ecc_alt/ecdsa_alt route to them
// (esp_ecc_point_multiply / esp_ecdsa_verify), HW-measured ~5 ms/point-mul. curve25519/ed25519 and
// ML-KEM-768 have no hardware path on any ESP32 (the ECC unit does only NIST prime curves), so those
// run in software on every variant.
static const char NAME_ED25519[] = "ssh-ed25519";

// Frame @p payload as a binary packet (encrypted once NEWKEYS is active) and write it to the relay.
static void cli_send(struct SshClientInternal *restrict ctx)
{
    const uint8_t *payload = ctx->ns->msg.payload;
    const size_t len = ctx->ns->msg.len;
    uint8_t *wire = ssh_conn_slot(SSH_CLI_SLOT);
    if (wire == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    wire += SSH_OFF_WIRE;
    size_t wlen = 0;
    if (ssh_pkt_send(SSH_CLI_SLOT, payload, len, wire, &wlen, SSH_WIRE_CAP, &ssh_sess[SSH_CLI_SLOT].out) != 0)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    TcpClient.cid = s_store.cid;
    TcpClient.io.data = wire;
    TcpClient.io.len = wlen;
    TcpClient.send(TcpClient.internal);
    ctx->ns->ok = TcpClient.ok;
    return;
}

static void cli_state(struct SshClientInternal *restrict ctx)
{
    ctx->ns->state_of = s_store.state;
    return;
}

// Designated, so a member's position in the struct does not decide what it binds to.
SshClientNs SshClient = {.send = cli_send,
                         .crypto_work = cli_crypto_work,
                         .state = cli_state,
                         .begin = protocore_ssh_client_begin,
                         .poll = protocore_ssh_client_poll,
                         .end = protocore_ssh_client_end,
                         .internal = &s_cli};
// ---------------------------------------------------------------------------
// RFC 4254 sec 7.1 - the outbound role's tcpip-forward request
// ---------------------------------------------------------------------------

static proto_bool send_tcpip_forward(void)
{
    uint8_t out[128];
    protocore_span w = span.from(out, sizeof(out));
    bytes.put(&w, SSH_MSG_GLOBAL_REQUEST);
    protocore_ssh_wr_cstr(&w, "tcpip-forward");
    bytes.put(&w, 1); // want reply
    protocore_ssh_wr_cstr(&w, s_store.cfg.bind_addr ? s_store.cfg.bind_addr : "");
    bytes.put_be(&w, s_store.cfg.bind_port, 4);
    return span.ok(w) && (SshClient.payload = out, SshClient.len = w.pos, cli_send(\& s_cli), SshClient.ok);
}
// ---------------------------------------------------------------------------
// RFC 4252 sec 7 - the outbound role's publickey request
// ---------------------------------------------------------------------------

static proto_bool send_userauth_publickey(void)
{
    const char *user = s_store.cfg.user;
    uint8_t *work = SshClient.crypto_work();
    if (work == NULL)
    {
        return PROTO_FALSE;
    }
    uint8_t pub[32];
    protocore_ed25519_pubkey(work, pub, s_store.cfg.auth_seed);

    // The device's public-key blob: string("ssh-ed25519") || string(pub32).
    uint8_t pkblob[4 + 11 + 4 + 32];
    protocore_span pw = span.from(pkblob, sizeof(pkblob));
    protocore_ssh_wr_cstr(&pw, NAME_ED25519);
    protocore_ssh_wr_str(&pw, pub, 32);
    if (!span.ok(pw))
    {
        return PROTO_FALSE;
    }

    // Data to sign (RFC 4252 §7): string(session_id) || the userauth request up to (and including)
    // the public-key blob, with the "signature present" flag set. session_id is 32 or 64 bytes (the
    // -sha512 KEX), so the buffer carries SSH_KEXHASH_MAX_LEN of headroom over the 32-byte base.
    size_t sid_len = 0;
    const uint8_t *sid = ssh_session_id(SSH_CLI_SLOT, &sid_len);
    if (sid == NULL)
    {
        return PROTO_FALSE; // no completed exchange: the signature would bind to no session
    }
    uint8_t signed_data[256 + SSH_KEXHASH_MAX_LEN];
    protocore_span sd = span.from(signed_data, sizeof(signed_data));
    SshAuth.out_args.w = &sd;
    SshAuth.userauth.sid = sid;
    SshAuth.userauth.sid_len = sid_len;
    SshAuth.userauth.user = user;
    SshAuth.userauth.service = "ssh-connection";
    SshAuth.userauth.pk_algo = NAME_ED25519;
    SshAuth.userauth.pk_blob = pkblob;
    SshAuth.userauth.pk_len = pw.pos;
    SshAuth.write_publickey_request(SshAuth.internal);
    if (!span.ok(sd))
    {
        return PROTO_FALSE;
    }

    uint8_t sig[64];
    protocore_ed25519_sign(work, sig, signed_data, sd.pos, s_store.cfg.auth_seed);

    // Signature blob: string("ssh-ed25519") || string(sig64).
    uint8_t sigblob[4 + 11 + 4 + 64];
    protocore_span sg = span.from(sigblob, sizeof(sigblob));
    protocore_ssh_wr_cstr(&sg, NAME_ED25519);
    protocore_ssh_wr_str(&sg, sig, 64);
    if (!span.ok(sg))
    {
        return PROTO_FALSE;
    }

    // The full USERAUTH_REQUEST is the signed prefix (minus the session_id) plus the signature.
    uint8_t out[300];
    protocore_span w = span.from(out, sizeof(out));
    SshAuth.out_args.w = &w;
    SshAuth.userauth.sid = NULL;
    SshAuth.userauth.sid_len = 0;
    SshAuth.userauth.user = user;
    SshAuth.userauth.service = "ssh-connection";
    SshAuth.userauth.pk_algo = NAME_ED25519;
    SshAuth.userauth.pk_blob = pkblob;
    SshAuth.userauth.pk_len = pw.pos;
    SshAuth.write_publickey_request(SshAuth.internal);
    protocore_ssh_wr_str(&w, sigblob, sg.pos);
    if (!span.ok(w))
    {
        return PROTO_FALSE;
    }
    SshClient.msg.payload = out;
    SshClient.msg.len = w.pos;
    SshClient.send(SshClient.internal);
    return SshClient.ok;
}

// ---------------------------------------------------------------------------
// Inbound message dispatch (called by ssh_pkt_recv per verified packet)
// ---------------------------------------------------------------------------

static void cli_msg_handler(uint8_t slot, uint8_t type, const uint8_t *payload, size_t len)
{
    (void)slot;
    // RFC 4253 sec 11: legal in every phase. DISCONNECT terminates immediately (11.1); IGNORE (11.2),
    // DEBUG (11.3) and UNIMPLEMENTED (11.4) are consumed and never answered.
    if (type == SSH_MSG_DISCONNECT)
    {
        cli_fail("relay sent DISCONNECT");
        return;
    }
    if (type == SSH_MSG_IGNORE || type == SSH_MSG_DEBUG || type == SSH_MSG_UNIMPLEMENTED)
    {
        return;
    }
    // RFC 4252 sec 5.4: the server may send a banner at any time before authentication succeeds.
    // This end has no screen to display it on, so it is consumed rather than answered.
    if (type == SSH_MSG_USERAUTH_BANNER)
    {
        return;
    }
    // Set by every arm that consumes the message; what is left is answered per RFC 4253 sec 11.4.
    proto_bool handled = PROTO_FALSE;

    // RFC 8308 sec 2.2: "If a client or server offers 'ext-info-c' or 'ext-info-s' respectively, it
    // MUST be prepared to accept an SSH_MSG_EXT_INFO message from the peer", and sec 2.4 allows it
    // at two points - after NEWKEYS and after USERAUTH_SUCCESS. This end offers ext-info-c on every
    // KEXINIT, so it is accepted whatever phase it lands in. sec 2.5: an extension this end does not
    // implement is ignored, not an error.
    if (type == SSH_MSG_EXT_INFO)
    {
        return;
    }

    // RFC 4253 sec 9: a re-exchange may be started by either end at any time once the connection is
    // running, so an inbound KEXINIT is not confined to the opening handshake. sec 7.1: "a party
    // MUST respond with its own SSH_MSG_KEXINIT message, except when the received SSH_MSG_KEXINIT
    // already was a reply."
    if (type == SSH_MSG_KEXINIT && !ssh_phase_is(SSH_CLI_SLOT, SSH_PHASE_KEXINIT))
    {
        if (!handle_server_kexinit(payload, len))
        {
            cli_fail("KEXINIT negotiation failed");
            return;
        }
        if (ssh_kexinit_needs_reply(SSH_CLI_SLOT) && !build_kexinit())
        {
            cli_fail("KEXINIT reply send failed");
        }
        return;
    }

    switch (ssh_sess[SSH_CLI_SLOT].phase)
    {
    case SSH_PHASE_KEXINIT:
        if (type == SSH_MSG_KEXINIT)
        {
            handled = PROTO_TRUE;
            if (!handle_server_kexinit(payload, len))
            {
                cli_fail("KEXINIT negotiation failed");
            }
        }
        break;
    case SSH_PHASE_DH_INIT:
        if (type == SSH_MSG_KEXDH_REPLY)
        {
            handled = PROTO_TRUE;
            if (handle_kexdh_reply(payload, len))
            {
                ssh_phase_kex_done(SSH_CLI_SLOT);
            }
            else if (s_store.state != PROTOCORE_SSH_CLIENT_FAILED)
            {
                cli_fail("KEXDH_REPLY invalid");
            }
        }
        break;
    case SSH_PHASE_NEWKEYS:
        if (type == SSH_MSG_NEWKEYS)
        {
            handled = PROTO_TRUE;
            // RFC 4253 sec 7.3: a NEWKEYS that ends no key exchange is a protocol error.
            if (SSH_TRANSPORT->newkeys_complete(SSH_CLI_SLOT) != 0) // -> SSH_PHASE_SERVICE
            {
                cli_fail("NEWKEYS outside a key exchange");
            }
            else if (!send_service_request())
            {
                cli_fail("service request send failed");
            }
        }
        break;
    case SSH_PHASE_SERVICE:
        if (type == SSH_MSG_SERVICE_ACCEPT)
        {
            handled = PROTO_TRUE;
            if (send_userauth_publickey())
            {
                ssh_phase_service_done(SSH_CLI_SLOT);
            }
            else
            {
                cli_fail("userauth send failed");
            }
        }
        break;
    case SSH_PHASE_AUTH:
        if (type == SSH_MSG_USERAUTH_SUCCESS)
        {
            handled = PROTO_TRUE;
            if (send_tcpip_forward())
            {
                ssh_phase_auth_done(SSH_CLI_SLOT);
            }
            else
            {
                cli_fail("tcpip-forward send failed");
            }
        }
        else if (type == SSH_MSG_USERAUTH_FAILURE)
        {
            handled = PROTO_TRUE;
            cli_fail("authentication rejected by the relay");
        }
        break;
    case SSH_PHASE_OPEN:
        // The forward is asked for and not yet confirmed until REQUEST_SUCCESS, which is the state
        // this end reports; the phase is already OPEN (RFC 4254 sec 7.1 is a channel-layer request).
        if (s_store.state != PROTOCORE_SSH_CLIENT_UP)
        {
            if (type == SSH_MSG_REQUEST_SUCCESS)
            {
                handled = PROTO_TRUE;
                s_store.state = PROTOCORE_SSH_CLIENT_UP;
                PROTOCORE_LOGI(LOG_FWD_UP, ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)s_store.cfg.bind_port)}),
                               1);
            }
            else if (type == SSH_MSG_REQUEST_FAILURE)
            {
                handled = PROTO_TRUE;
                cli_fail("relay refused the remote forward");
            }
            break;
        }
        // RFC 4254 messages 80 and above belong to the connection layer, which owns them for both
        // roles; a zero return means it consumed the message.
        SshConnection.chan.slot = SSH_CLI_SLOT;
        SshConnection.msg_type = type;
        SshConnection.chan.payload = payload;
        SshConnection.chan.len = len;
        SshConnection.dispatch(SshConnection.internal);
        handled = (SshConnection.i32 == 0);
        break;
    default:
        break;
    }

    // RFC 4253 sec 11.4: an implementation MUST answer an unrecognized message with
    // SSH_MSG_UNIMPLEMENTED, which the transport builds - message 3 and the receive counter are
    // both its.
    if (!handled)
    {
        uint8_t out[SSH_UNIMPLEMENTED_LEN];
        size_t n = 0;
        if (ssh_pkt_unimplemented(SSH_CLI_SLOT, out, &n, sizeof(out)) == 0)
        {
            (void)(SshClient.payload = out, SshClient.len = n, cli_send(\& s_cli), SshClient.ok);
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void protocore_ssh_client_begin(struct SshClientInternal *restrict ctx)
{
    const protocore_ssh_client_cfg *cfg = ctx->ns->cfg;
    if (!cfg || !cfg->host || !cfg->user || !cfg->auth_seed || !cfg->host_pin)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }

    protocore_ssh_client_end();
    mem.set(&s_cli, 0, sizeof(s_cli));
    s_store.cfg = *cfg;
    SshConnection.chan.slot = SSH_CLI_SLOT;
    SshConnection.channel_init(SshConnection.internal); // the channel table this slot's bridges hang off

    // Own a dedicated scratch arena, distinct from the server's worker(s): packet decryption borrows
    // from the shared scratch, and that arena is single-accessor-per-task. begin() and poll() run in
    // the same task, so claiming the slot here makes every later decrypt use the client's own arena.
    protocore_worker_set_self(PROTOCORE_GHOST_WORKER_SLOT);

    uint16_t port = cfg->port ? cfg->port : 22;
    TcpClient.dial.host = cfg->host;
    TcpClient.dial.port = port;
    TcpClient.dial.timeout_ms = 8000;
    TcpClient.open(TcpClient.internal);
    s_store.cid = TcpClient.i32;
    if (s_store.cid < 0)
    {
        s_store.state = PROTOCORE_SSH_CLIENT_FAILED;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    // The slot is ours for the life of the forward, so an inbound accept passes over it.
    SshNetwork.ssh_slot = SSH_CLI_SLOT;
    SshNetwork.handle = s_store.cid;
    SshNetwork.stream.kind = SSH_STREAM_DIALED;
    SshNetwork.claim(SshNetwork.internal);
    if (SshNetwork.i32 != 0)
    {
        TcpClient.cid = s_store.cid;
        TcpClient.close(TcpClient.internal);
        s_store.cid = -1;
        s_store.state = PROTOCORE_SSH_CLIENT_FAILED;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }

    ssh_transport_init(SSH_CLI_SLOT); // -> SSH_PHASE_IDENT
    ssh_pkt_init(SSH_CLI_SLOT);
    ssh_pkt_set_client(SSH_CLI_SLOT);
    ssh_keymat_wipe(SSH_CLI_SLOT);

    // Send our identification string, then our KEXINIT.
    uint8_t banner[SSH_VERSION_MAX + 2];
    size_t n = 0;
    const int ident = SSH_TRANSPORT->send_ident(SSH_CLI_SLOT, banner, &n, sizeof(banner));
    TcpClient.cid = s_store.cid;
    TcpClient.io.data = banner;
    TcpClient.io.len = n;
    TcpClient.send(TcpClient.internal);
    if (ident != 0 || !TcpClient.ok)
    {
        cli_fail("banner send failed");
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    s_store.state = PROTOCORE_SSH_CLIENT_CONNECTING;
    s_store.deadline_ms = Clock.ms + 15000;
    ctx->ns->ok = PROTO_TRUE;
    return;
}

void protocore_ssh_client_poll(struct SshClientInternal *restrict ctx)
{
    if (s_store.cid < 0 || s_store.state == PROTOCORE_SSH_CLIENT_IDLE || s_store.state == PROTOCORE_SSH_CLIENT_FAILED)
    {
        return;
    }

    TcpClient.cid = s_store.cid;
    TcpClient.is_closed(TcpClient.internal);
    const proto_bool peer_closed = TcpClient.ok;
    TcpClient.cid = s_store.cid;
    TcpClient.available(TcpClient.internal);
    if (peer_closed && TcpClient.n == 0)
    {
        cli_fail("relay closed the connection");
        return;
    }
    if (s_store.state == PROTOCORE_SSH_CLIENT_CONNECTING && (int32_t)(Clock.ms - s_store.deadline_ms) > 0)
    {
        cli_fail("handshake timed out");
        return;
    }

    uint8_t buf[1024];
    TcpClient.cid = s_store.cid;
    TcpClient.io.buf = buf;
    TcpClient.io.cap = sizeof(buf);
    TcpClient.read(TcpClient.internal);
    size_t got = TcpClient.n;
    if (got)
    {
        size_t off = 0;
        if (ssh_phase_admits_ident(SSH_CLI_SLOT))
        {
            int ident = ssh_transport_version_exchange_recv(SSH_CLI_SLOT, buf, got, &off);
            if (ident < 0)
            {
                cli_fail("relay identification string invalid");
                return;
            }
            if (ident == 1 && !build_kexinit())
            {
                cli_fail("KEXINIT send failed");
                return;
            }
        }
        if (off < got && s_store.state != PROTOCORE_SSH_CLIENT_FAILED)
        {
            if (ssh_pkt_recv(SSH_CLI_SLOT, buf + off, got - off, cli_msg_handler, &ssh_sess[SSH_CLI_SLOT].in) != 0)
            {
                cli_fail("packet error (MAC / framing)");
            }
        }
    }

#if PROTOCORE_SSH_PORT_FORWARD
    if (s_store.state == PROTOCORE_SSH_CLIENT_UP)
    {
        SshConnection.fwd.slot = SSH_CLI_SLOT;
        SshConnection.forward_pump(SshConnection.internal);
    }
#endif
}

void protocore_ssh_client_end(struct SshClientInternal *restrict ctx)
{
    SshNetwork.ssh_slot = SSH_CLI_SLOT;
    SshNetwork.chan_close_all(SshNetwork.internal);
    if (s_store.cid >= 0)
    {
        TcpClient.cid = s_store.cid;
        TcpClient.close(TcpClient.internal);
    }
    SshNetwork.ssh_slot = SSH_CLI_SLOT;
    SshNetwork.release(SshNetwork.internal);
    ssh_keymat_wipe(SSH_CLI_SLOT);
    cli_wipe();
    mem.set(&s_cli, 0, sizeof(s_cli));
    s_store.cid = -1;
    s_store.state = PROTOCORE_SSH_CLIENT_IDLE;
}

static void cli_fail(const char *why)
{
    PROTOCORE_LOGW(LOG_FWD_FAIL, ((const protocore_fval[]){PROTOCORE_VSTR(why)}), 1);
    s_store.state = PROTOCORE_SSH_CLIENT_FAILED;
    SshNetwork.ssh_slot = SSH_CLI_SLOT;
    SshNetwork.chan_close_all(SshNetwork.internal);
    if (s_store.cid >= 0)
    {
        TcpClient.cid = s_store.cid;
        TcpClient.close(TcpClient.internal);
    }
    s_store.cid = -1;
    SshNetwork.ssh_slot = SSH_CLI_SLOT;
    SshNetwork.release(SshNetwork.internal);
    ssh_keymat_wipe(SSH_CLI_SLOT);
    cli_wipe();
}

// Wipe the client's state: the KEX private, its public, the hybrid decapsulation key and the
// session id all live here for the life of one connection.
static void cli_wipe(void)
{
    protocore_secure_wipe(&s_cli, sizeof(s_cli));
}

#endif // PROTOCORE_ENABLE_SSH_CLIENT
