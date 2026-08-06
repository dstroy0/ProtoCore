// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_transport.cpp
 * @brief SSH transport handshake - banner exchange and KEXINIT negotiation.
 */

#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include "crypto/asymmetric/bignum.h"     // bn_*, pc_bignum
#include "crypto/asymmetric/curve25519.h" // pc_x25519 (curve25519-sha256 KEX)
#include "crypto/asymmetric/ecdsa.h"      // pc_ecdsa_p256_* (ecdsa-sha2-nistp256 host key)
#include "crypto/asymmetric/ed25519.h"    // pc_ed25519 host-key sign
#include "crypto/hash/sha256.h"
#include "crypto/rng/rng.h" // pc_rand_fill
#include "mmgr/bytes.h"     // pc_rd_str() - a name-list is an RFC 4251 sec 5 string
#include "mmgr/membuild.h"  // pc_sb frame builder
#include "mmgr/secure.h"
#include "network_drivers/presentation/ssh/transport/ssh_dh.h" // pc_rand_fill(), ssh_dh[], SshDh.generate/derive_keys
#include "network_drivers/presentation/ssh/transport/ssh_packet.h" // SSH_MSG_KEXINIT, ssh_pkt[]
#include "network_drivers/tls/ssh_rsa.h" // ssh_rsa_encode_pubkey/sign, ssh_host_pubkey, SSH_RSA_*
#include "server/clock/clock.h"          // pc_millis() (re-key timer)
#if PC_ENABLE_PQC_KEX
#include "crypto/pqc/mlkem.h" // pc_mlkem768_encaps (PQ/T hybrid KEX responder)
#endif
#if PC_ENABLE_SSH_SNTRUP761
#include "crypto/pqc/sntrup761.h" // pc_sntrup761_enc (sntrup761x25519 responder)
#endif
#if PC_ENABLE_SSH_ZLIB
#include "network_drivers/presentation/ssh/transport/ssh_comp.h" // s2c compression negotiation
#endif
#include <stdio.h> // snprintf (name-list assembly)

#ifdef PC_SSH_KEX_BENCH
#include <esp_timer.h> // esp_timer_get_time() - microsecond wall clock for the KEX span probe

// Called above their definitions; static made the header's declaration unavailable.
static void ssh_kex_set_prefer_rsa(proto_bool prefer);
static int ssh_kexdh_handle(uint8_t i, const uint8_t *payload, size_t len, uint8_t *reply_out, size_t *reply_len,
                            size_t cap);
// The one owned KEX-bench context (see ssh_transport.h). Set by ssh_kex_generate / ssh_kexdh_handle; the
// rig firmware prints it. Single active connection during a bench run, so a plain instance suffices.
SshKexBenchCtx pc_ssh_kex_bench = {0, 0, 0};
#endif

SshSession ssh_sess[MAX_SSH_CONNS];

// ---------------------------------------------------------------------------
// Negotiable algorithms. The server supports two KEX methods and two host-key
// types; cipher / MAC / compression are fixed. Negotiation is crypto-agnostic and
// steers toward whichever suite ssh_kex_set_prefer_rsa() selects (default: RSA/DH,
// hardware-accelerated on ESP32), while still advertising both suites so a client
// that supports only one still connects.
// ---------------------------------------------------------------------------

static const char *const KEX_DH = "diffie-hellman-group14-sha256";
static const char *const KEX_C25519 = "curve25519-sha256";
static const char *const KEX_C25519_LIBSSH = "curve25519-sha256@libssh.org"; // identical wire protocol
static const char *const KEX_ECDH_NISTP256 = "ecdh-sha2-nistp256";           // NIST P-256 ECDH (RFC 5656 §4)
#if PC_ENABLE_PQC_KEX
static const char *const KEX_MLKEM768 = "mlkem768x25519-sha256"; // PQ/T hybrid (ML-KEM-768 + X25519)
#endif
#if PC_ENABLE_SSH_SNTRUP761
static const char *const KEX_SNTRUP761 = "sntrup761x25519-sha512@openssh.com"; // PQ/T hybrid (NTRU Prime + X25519)
#endif
static const char *const HOSTKEY_RSA_SHA256 = "rsa-sha2-256";
static const char *const HOSTKEY_RSA_SHA512 = "rsa-sha2-512";
static const char HOSTKEY_ED[] = "ssh-ed25519";
static const char HOSTKEY_ECDSA[] = "ecdsa-sha2-nistp256";
static const char *const ALG_CIPHER = "aes256-ctr";
static const char *const ALG_CIPHER_GCM = "aes256-gcm@openssh.com";
// Advertised cipher preference (OpenSSH's default order): chacha20-poly1305@openssh.com (AEAD)
// first, aes256-gcm@openssh.com (AEAD, HW-accelerated) second, aes256-ctr (HW fallback) last.
static const char *const ALG_CIPHER_LIST = "chacha20-poly1305@openssh.com,aes256-gcm@openssh.com,aes256-ctr";
static const char *const ALG_MAC = "hmac-sha2-256";
// Advertised MAC preference (aes256-ctr only; the chacha AEAD needs none): encrypt-then-MAC first
// (OpenSSH's default), then plain encrypt-and-MAC.
static const char *const ALG_MAC_LIST = "hmac-sha2-256-etm@openssh.com,hmac-sha2-512-etm@openssh.com,"
                                        "hmac-sha2-256,hmac-sha2-512";
static const char *const ALG_COMP = "none";
#if PC_ENABLE_SSH_ZLIB
// Compression preference (both directions): zlib@openssh.com (delayed, OpenSSH's default) first, then
// zlib (immediate), then none. s2c deflates (ssh_zlib); c2s inflates OpenSSH's Z_PARTIAL_FLUSH stream
// (ssh_inflate). Advertised for c2s and s2c alike.
static const char *const ALG_COMP_ZLIB = "zlib@openssh.com,zlib,none";
#else
static const char *const ALG_COMP_ZLIB = "none";
#endif
// RFC 8308 indicator a client sets in its kex_algorithms to request EXT_INFO.
static const char *const EXT_INFO_C = "ext-info-c";

// All SSH transport host-key/KEX state, owned by one instance (internal linkage): the runtime
// KEX preference (true = prefer the hardware-accelerated RSA/DH suite) and the optional
// ssh-ed25519 host key (the RSA host key is loaded via ssh_rsa). One named owner, cross-TU
// unreachable.
typedef struct
{
    proto_bool prefer_rsa;
    uint8_t ed_seed[32];
    uint8_t ed_pub[32];
    proto_bool ed_have;
    uint8_t ecdsa_priv[PC_ECDSA_P256_PRIV_LEN]; ///< P-256 host private scalar d.
    uint8_t ecdsa_pub[PC_ECDSA_P256_PUB_LEN];   ///< P-256 host public point (0x04||X||Y).
    proto_bool ecdsa_have;
} SshTransportCtx;
// prefer_rsa starts set; every other field starts at the zero it would take anyway.
static SshTransportCtx s_sshtr = {PROTO_TRUE, {0}, {0}, PROTO_FALSE, {0}, {0}, PROTO_FALSE};

static void ssh_kex_set_prefer_rsa(proto_bool prefer)
{
    s_sshtr.prefer_rsa = prefer;
}
static proto_bool ssh_kex_prefer_rsa(void)
{
    return s_sshtr.prefer_rsa;
}

static void pc_ssh_hostkey_ed25519_set(const uint8_t seed[32])
{
    memcpy(s_sshtr.ed_seed, seed, 32);
    pc_ed25519_pubkey(s_sshtr.ed_pub, s_sshtr.ed_seed);
    s_sshtr.ed_have = PROTO_TRUE;
}
static proto_bool pc_ssh_hostkey_ed25519_available(void)
{
    return s_sshtr.ed_have;
}
static void pc_ssh_hostkey_ecdsa_set(const uint8_t priv[PC_ECDSA_P256_PRIV_LEN])
{
    // Derive and cache the public point; reject an invalid scalar (leaves ecdsa_have false).
    if (!pc_ecdsa_p256_pubkey(s_sshtr.ecdsa_pub, priv))
    {
        return;
    }
    memcpy(s_sshtr.ecdsa_priv, priv, PC_ECDSA_P256_PRIV_LEN);
    s_sshtr.ecdsa_have = PROTO_TRUE;
}
static proto_bool pc_ssh_hostkey_ecdsa_available(void)
{
    return s_sshtr.ecdsa_have;
}

const SshHostkeyNs SshHostkey = {pc_ssh_hostkey_ed25519_set, pc_ssh_hostkey_ed25519_available, pc_ssh_hostkey_ecdsa_set,
                                 pc_ssh_hostkey_ecdsa_available};
static proto_bool hostkey_rsa_available(void)
{
    return ssh_host_pubkey.loaded;
}

// Build the KEX / host-key advertise lists in preference order (RFC 4253 §7.1 name-
// lists), filtering host-key types to the keys we actually hold. server-sig-algs
// (RFC 8308) uses the same host-key ordering. Written into a caller buffer.
static void build_kex_list(char *out, size_t cap)
{
    const char *c1 = KEX_C25519;
    const char *c2 = KEX_C25519_LIBSSH;
    const char *dh = KEX_DH;
    const char *ec = KEX_ECDH_NISTP256; // NIST P-256 ECDH (RFC 5656)
    size_t n = 0;
    // Post-quantum hybrids advertised first: a PQC-capable peer (OpenSSH 9.9+, which also lists them
    // first) negotiates one over classical X25519, closing the harvest-now-decrypt-later gap. Each
    // is gated independently so a footprint-bound build can offer ML-KEM only (kexlist[192] holds
    // the full both-hybrid list with margin, so the appends never truncate).
#if PC_ENABLE_PQC_KEX
    pc_sb sb_mlkem = {out + n, cap - n, 0, PROTO_TRUE};
    pc_sb_put(&sb_mlkem, KEX_MLKEM768);
    pc_sb_lit(&sb_mlkem, ",");
    n += pc_sb_finish(&sb_mlkem);
#endif
#if PC_ENABLE_SSH_SNTRUP761
    pc_sb sb_sntrup = {out + n, cap - n, 0, PROTO_TRUE};
    pc_sb_put(&sb_sntrup, KEX_SNTRUP761);
    pc_sb_lit(&sb_sntrup, ",");
    n += pc_sb_finish(&sb_sntrup);
#endif
    if (s_sshtr.prefer_rsa)
    {
        pc_sb sb157 = {out + n, cap - n, 0, PROTO_TRUE};
        pc_sb_put(&sb157, dh);
        pc_sb_put(&sb157, ",");
        pc_sb_put(&sb157, ec);
        pc_sb_put(&sb157, ",");
        pc_sb_put(&sb157, c1);
        pc_sb_put(&sb157, ",");
        pc_sb_put(&sb157, c2);
        pc_sb_put(&sb157, ",ext-info-s");
        if (pc_sb_finish(&sb157) == 0)
        {
            sb157.p[0] = '\0';
        }
    }
    else
    {
        pc_sb sb159 = {out + n, cap - n, 0, PROTO_TRUE};
        pc_sb_put(&sb159, c1);
        pc_sb_put(&sb159, ",");
        pc_sb_put(&sb159, c2);
        pc_sb_put(&sb159, ",");
        pc_sb_put(&sb159, ec);
        pc_sb_put(&sb159, ",");
        pc_sb_put(&sb159, dh);
        pc_sb_put(&sb159, ",ext-info-s");
        if (pc_sb_finish(&sb159) == 0)
        {
            sb159.p[0] = '\0';
        }
    }
}
static void build_hostkey_list(char *out, size_t cap)
{
    // Both rsa-sha2-512 and rsa-sha2-256 are backed by the one "ssh-rsa" host key
    // (RFC 8332): advertise 512 before 256 (OpenSSH's order). Filter to keys we hold.
    const proto_bool rsa = hostkey_rsa_available();
    const proto_bool ed = pc_ssh_hostkey_ed25519_available();
    const proto_bool ec = pc_ssh_hostkey_ecdsa_available();
    // A host key we can offer: the wire name and whether the key is on the device.
    typedef struct
    {
        const char *name;
        proto_bool ok;
    } HostkeyCand;
    HostkeyCand cand[4] = {{0, PROTO_FALSE}, {0, PROTO_FALSE}, {0, PROTO_FALSE}, {0, PROTO_FALSE}};
    if (s_sshtr.prefer_rsa)
    {
        cand[0] = (HostkeyCand){HOSTKEY_RSA_SHA512, rsa};
        cand[1] = (HostkeyCand){HOSTKEY_RSA_SHA256, rsa};
        cand[2] = (HostkeyCand){HOSTKEY_ECDSA, ec};
        cand[3] = (HostkeyCand){HOSTKEY_ED, ed};
    }
    else
    {
        cand[0] = (HostkeyCand){HOSTKEY_ED, ed};
        cand[1] = (HostkeyCand){HOSTKEY_ECDSA, ec};
        cand[2] = (HostkeyCand){HOSTKEY_RSA_SHA512, rsa};
        cand[3] = (HostkeyCand){HOSTKEY_RSA_SHA256, rsa};
    }
    out[0] = '\0';
    for (int k = 0; k < 4; k++)
    {
        if (!cand[k].ok)
        {
            continue;
        }
        size_t l = strnlen(out, cap);
        pc_sb sb194 = {out + l, cap - l, 0, PROTO_TRUE};
        pc_sb_put(&sb194, l ? "," : "");
        pc_sb_put(&sb194, cand[k].name);
        if (pc_sb_finish(&sb194) == 0)
        {
            sb194.p[0] = '\0';
        }
    }
}

// ---------------------------------------------------------------------------
// Byte-writer helpers
// ---------------------------------------------------------------------------

typedef struct
{
    uint8_t *p;
    size_t cap;
    size_t len;
    proto_bool ok;
} Writer;

static void w_bytes(Writer *w, const void *src, size_t n)
{
    if (!w->ok || w->len + n > w->cap)
    {
        w->ok = PROTO_FALSE;
        return;
    }
    memcpy(w->p + w->len, src, n); // NOSONAR - bound proven above; analyzer follows an infeasible path
    w->len += n;
}

static void w_u8(Writer *w, uint8_t v)
{
    w_bytes(w, &v, 1);
}

static void w_u32(Writer *w, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    w_bytes(w, b, 4);
}

// Write an SSH name-list: uint32 length + comma-separated names.
static void w_namelist(Writer *w, const char *list)
{
    uint32_t n = (uint32_t)strnlen(list, w->cap);
    w_u32(w, n);
    w_bytes(w, list, n);
}

// Write an SSH string: uint32 length + raw bytes.
static void w_string(Writer *w, const uint8_t *data, size_t n)
{
    w_u32(w, (uint32_t)n);
    w_bytes(w, data, n);
}

// Write an SSH mpint from a fixed-width big-endian integer: strip leading zero
// bytes, prepend 0x00 if the top bit is set.
static void w_mpint(Writer *w, const uint8_t *be, size_t len)
{
    size_t off = 0;
    while (off < len && be[off] == 0)
    {
        off++;
    }
    if (off == len)
    {
        w_u32(w, 0); // zero → empty string
        return;
    }
    proto_bool pad = (be[off] & 0x80u) != 0;
    w_u32(w, (uint32_t)(len - off) + (pad ? 1u : 0u));
    if (pad)
    {
        w_u8(w, 0x00);
    }
    w_bytes(w, be + off, len - off);
}

// ---------------------------------------------------------------------------
// name-list membership test (RFC 4253 §7.1 - comma-separated, no spaces)
// ---------------------------------------------------------------------------

// Returns true if @p want appears as a complete element of the comma-separated
// list [list, list+len).
static proto_bool namelist_contains(const uint8_t *list, uint32_t len, const char *want)
{
    size_t wl = strnlen(want, (size_t)len + 1);
    uint32_t start = 0;
    for (uint32_t i = 0; i <= len; i++)
    {
        if (i == len || list[i] == ',')
        {
            uint32_t elen = i - start;
            if (elen == wl && memcmp(list + start, want, wl) == 0)
            {
                return PROTO_TRUE;
            }
            start = i + 1;
        }
    }
    return PROTO_FALSE;
}

// One negotiation candidate: an algorithm name, the tag we store if it is chosen, and
// whether we can actually perform it (e.g. we hold the matching host key).
// A negotiation candidate: the wire name, the algorithm it selects, and whether we can perform it.
// One type serves every family (kex / hostkey / cipher / mac / compression): each family is an enum,
// an enum constant is an int, and the selected value lands in a uint8_t session field either way.
typedef struct
{
    const char *name;
    int tag;
    proto_bool avail;
} AlgCand;

// Index of the first available candidate whose name is exactly [tok, tok+tlen), or -1.
static int cand_match(const uint8_t *tok, uint32_t tlen, const AlgCand *cands, int n)
{
    for (int c = 0; c < n; c++)
    {
        size_t cl = strnlen(cands[c].name, (size_t)tlen + 1);
        if (cands[c].avail && cl == tlen && memcmp(tok, cands[c].name, tlen) == 0)
        {
            return c;
        }
    }
    return -1;
}

// RFC 4253 §7.1: the chosen algorithm is the first name on the CLIENT's name-list that the server also
// supports - CLIENT preference, not ours. Two peers whose preference orders differ must still converge, so
// the rule is fixed to the client's order. Iterate the client's comma-separated list in order; for each
// name take the first available server candidate that matches. (Steering to our preferred algorithm is
// done by the order WE advertise in KEXINIT, which a client that has no strong preference will follow.)
static proto_bool negotiate_alg(const uint8_t *client_list, uint32_t nlen, const AlgCand *cands, int n, int *out)
{
    uint32_t start = 0;
    for (uint32_t i = 0; i <= nlen; i++)
    {
        if (i == nlen || client_list[i] == ',')
        {
            int c = cand_match(client_list + start, i - start, cands, n);
            if (c >= 0)
            {
                *out = cands[c].tag;
                return PROTO_TRUE;
            }
            start = i + 1;
        }
    }
    return PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

static void ssh_transport_init(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    SshSession *s = &ssh_sess[i];
    memset(s, 0, sizeof(*s));
    s->phase = SSH_PHASE_BANNER;
}

// ---------------------------------------------------------------------------
// Identification string exchange (RFC 4253 §4.2)
// ---------------------------------------------------------------------------

static int ssh_transport_server_banner(uint8_t *out, size_t *out_len, size_t cap)
{
    size_t vlen = sizeof(SSH_SERVER_VERSION) - 1;
    if (vlen + 2 > cap)
    {
        return -1;
    }
    memcpy(out, SSH_SERVER_VERSION, vlen);
    out[vlen] = '\r';
    out[vlen + 1] = '\n';
    *out_len = vlen + 2;
    return 0;
}

static int ssh_transport_recv_banner(uint8_t i, const uint8_t *data, size_t len, size_t *consumed)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshSession *s = &ssh_sess[i];

    size_t k = 0;
    while (k < len)
    {
        uint8_t c = data[k++];
        if (c == '\n')
        {
            // End of a line. Strip a trailing CR if present.
            uint16_t n = s->banner_len;
            if (n > 0 && s->banner_buf[n - 1] == '\r')
            {
                n--;
            }

            // RFC 4253 §4.2: the server may receive other lines before the
            // identification string; only the line starting with "SSH-" counts.
            if (n >= 4 && memcmp(s->banner_buf, "SSH-", 4) == 0)
            {
                if (n >= SSH_VERSION_MAX)
                {
                    return -1;
                }
                memcpy(s->v_c, s->banner_buf, n);
                s->v_c[n] = '\0';
                s->v_c_len = n;
                s->banner_len = 0;
                s->phase = SSH_PHASE_KEXINIT;
                *consumed = k;
                return 1;
            }
            // Not the SSH line - discard and keep scanning.
            s->banner_len = 0;
            continue;
        }

        if (s->banner_len >= SSH_VERSION_MAX)
        {
            return -1; // line too long
        }
        s->banner_buf[s->banner_len++] = c;
    }

    *consumed = k;
    return 0; // need more data
}

// ---------------------------------------------------------------------------
// KEXINIT (RFC 4253 §7.1)
// ---------------------------------------------------------------------------

static int ssh_kexinit_build(uint8_t i, uint8_t *payload, size_t *len, size_t cap)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshSession *s = &ssh_sess[i];

    Writer w = {payload, cap, 0, PROTO_TRUE};
    w_u8(&w, SSH_MSG_KEXINIT);

    uint8_t cookie[16];
    pc_rand_fill(cookie, sizeof(cookie));
    w_bytes(&w, cookie, sizeof(cookie));

    char kexlist[192];
    // All four host-key algs = "ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-512,rsa-sha2-256" is 57
    // chars + NUL; a smaller buffer silently drops rsa-sha2-256 when all three key types are loaded.
    char hklist[64];
    build_kex_list(kexlist, sizeof(kexlist));
    build_hostkey_list(hklist, sizeof(hklist));
    w_namelist(&w, kexlist);         // kex_algorithms (preference-ordered, + ext-info-s)
    w_namelist(&w, hklist);          // server_host_key_algorithms (only keys we hold)
    w_namelist(&w, ALG_CIPHER_LIST); // encryption c2s (chacha20-poly1305 preferred, aes256-ctr fallback)
    w_namelist(&w, ALG_CIPHER_LIST); // encryption s2c
    w_namelist(&w, ALG_MAC_LIST);    // mac c2s (used only with aes256-ctr; ignored for the AEAD cipher)
    w_namelist(&w, ALG_MAC_LIST);    // mac s2c
    w_namelist(&w, ALG_COMP_ZLIB);   // compression c2s (zlib@openssh.com / zlib when built in, else none)
    w_namelist(&w, ALG_COMP_ZLIB);   // compression s2c (zlib@openssh.com / zlib when built in, else none)
    w_namelist(&w, "");              // languages c2s
    w_namelist(&w, "");              // languages s2c
    w_u8(&w, 0);                     // first_kex_packet_follows = false
    w_u32(&w, 0);                    // reserved

    if (!w.ok)
    {
        return -1;
    }

    // Retain a copy as I_S for the exchange hash.
    if (w.len > PC_SSH_KEXINIT_S_MAX)
    {
        return -1;
    }
    memcpy(s->i_s, payload, w.len);
    s->i_s_len = (uint16_t)w.len;

    *len = w.len;
    return 0;
}

// A KEXINIT name-list is an RFC 4251 sec 5 string holding comma-separated names, so reading one is
// pc_rd_str(); what makes it a name-list is what negotiate_alg()/namelist_contains() do with it.

// Negotiate the key-exchange method from the client's kex_algorithms name-list, in our preference order
// (PQC hybrid first when enabled; RSA group first when prefer_rsa). false = no mutual method.
static proto_bool negotiate_kex(const uint8_t *list, uint32_t nlen, SshKexAlg *out)
{
    AlgCand kc[6];
    int nk = 0;
#if PC_ENABLE_PQC_KEX
    kc[nk++] = (AlgCand){KEX_MLKEM768, SSH_KEX_MLKEM768_X25519, PROTO_TRUE}; // hybrid first (PQC-preferred)
#endif
#if PC_ENABLE_SSH_SNTRUP761
    kc[nk++] = (AlgCand){KEX_SNTRUP761, SSH_KEX_SNTRUP761_X25519, PROTO_TRUE}; // OpenSSH's other PQC default
#endif
    if (s_sshtr.prefer_rsa)
    {
        kc[nk++] = (AlgCand){KEX_DH, SSH_KEX_DH_GROUP14, PROTO_TRUE};
        kc[nk++] = (AlgCand){KEX_ECDH_NISTP256, SSH_KEX_ECDH_NISTP256, PROTO_TRUE};
        kc[nk++] = (AlgCand){KEX_C25519, SSH_KEX_CURVE25519, PROTO_TRUE};
        kc[nk++] = (AlgCand){KEX_C25519_LIBSSH, SSH_KEX_CURVE25519, PROTO_TRUE};
    }
    else
    {
        kc[nk++] = (AlgCand){KEX_C25519, SSH_KEX_CURVE25519, PROTO_TRUE};
        kc[nk++] = (AlgCand){KEX_C25519_LIBSSH, SSH_KEX_CURVE25519, PROTO_TRUE};
        kc[nk++] = (AlgCand){KEX_ECDH_NISTP256, SSH_KEX_ECDH_NISTP256, PROTO_TRUE};
        kc[nk++] = (AlgCand){KEX_DH, SSH_KEX_DH_GROUP14, PROTO_TRUE};
    }
    int tag = 0;
    if (!negotiate_alg(list, nlen, kc, nk, &tag))
    {
        return PROTO_FALSE;
    }
    *out = tag;
    return PROTO_TRUE;
}

// Negotiate the host-key algorithm, restricted to keys we actually hold. rsa-sha2-512/256 share the one
// RSA key (RFC 8332), ecdsa-sha2-nistp256 is a distinct P-256 key. false = no mutual algorithm.
static proto_bool negotiate_hostkey(const uint8_t *list, uint32_t nlen, SshHostkeyAlg *out)
{
    const proto_bool rsa = hostkey_rsa_available();
    const proto_bool ed = pc_ssh_hostkey_ed25519_available();
    const proto_bool ec = pc_ssh_hostkey_ecdsa_available();
    AlgCand hc[4];
    if (s_sshtr.prefer_rsa)
    {
        hc[0] = (AlgCand){HOSTKEY_RSA_SHA512, SSH_HOSTKEY_RSA_SHA512, rsa};
        hc[1] = (AlgCand){HOSTKEY_RSA_SHA256, SSH_HOSTKEY_RSA_SHA256, rsa};
        hc[2] = (AlgCand){HOSTKEY_ECDSA, SSH_HOSTKEY_ECDSA_NISTP256, ec};
        hc[3] = (AlgCand){HOSTKEY_ED, SSH_HOSTKEY_ED25519, ed};
    }
    else
    {
        hc[0] = (AlgCand){HOSTKEY_ED, SSH_HOSTKEY_ED25519, ed};
        hc[1] = (AlgCand){HOSTKEY_ECDSA, SSH_HOSTKEY_ECDSA_NISTP256, ec};
        hc[2] = (AlgCand){HOSTKEY_RSA_SHA512, SSH_HOSTKEY_RSA_SHA512, rsa};
        hc[3] = (AlgCand){HOSTKEY_RSA_SHA256, SSH_HOSTKEY_RSA_SHA256, rsa};
    }
    int tag = 0;
    if (!negotiate_alg(list, nlen, hc, 4, &tag))
    {
        return PROTO_FALSE;
    }
    *out = tag;
    return PROTO_TRUE;
}

static int ssh_kexinit_parse(uint8_t i, const uint8_t *payload, size_t len)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshSession *s = &ssh_sess[i];

    if (len < 1 + 16 || payload[0] != SSH_MSG_KEXINIT)
    {
        return -1;
    }

    // Retain a copy as I_C for the exchange hash.
    if (len > SSH_KEXINIT_MAX)
    {
        return -1;
    }
    memcpy(s->i_c, payload, len);
    s->i_c_len = (uint16_t)len;

    size_t off = 1 + 16; // skip msg type + 16-byte cookie

    const uint8_t *list;
    uint32_t nlen;

    // kex_algorithms: negotiate the KEX method by the CLIENT's preference (RFC 4253 §7.1).
    if (!pc_rd_str(payload, len, &off, &list, &nlen))
    {
        return -1;
    }
    // RFC 8308: if the client offers ext-info-c we will send SSH_MSG_EXT_INFO.
    s->ext_info_c = namelist_contains(list, nlen, EXT_INFO_C);
    if (!negotiate_kex(list, nlen, &s->kex_alg))
    {
        return -1; // no mutual KEX
    }
    // server_host_key_algorithms: negotiate, restricted to keys we actually hold.
    if (!pc_rd_str(payload, len, &off, &list, &nlen))
    {
        return -1;
    }
    if (!negotiate_hostkey(list, nlen, &s->hostkey_alg))
    {
        return -1; // no mutual host-key algorithm
    }
    // encryption c2s / s2c: negotiate chacha20-poly1305@openssh.com or aes256-gcm@openssh.com (both
    // AEADs) or aes256-ctr, in that preference order.
    const AlgCand cc[3] = {{"chacha20-poly1305@openssh.com", SSH_CIPHER_CHACHA20POLY1305, PROTO_TRUE},
                           {ALG_CIPHER_GCM, SSH_CIPHER_AES256GCM, PROTO_TRUE},
                           {ALG_CIPHER, SSH_CIPHER_AES256CTR, PROTO_TRUE}};
    if (!pc_rd_str(payload, len, &off, &list, &nlen))
    {
        return -1;
    }
    int c2s = 0;
    int s2c = 0;
    if (!negotiate_alg(list, nlen, cc, 3, &c2s))
    {
        return -1;
    }
    if (!pc_rd_str(payload, len, &off, &list, &nlen))
    {
        return -1;
    }
    if (!negotiate_alg(list, nlen, cc, 3, &s2c) || s2c != c2s) // require the same cipher both directions
    {
        return -1;
    }
    s->cipher_alg = c2s;
    // mac c2s / s2c: negotiated only for aes256-ctr (both AEAD ciphers carry their own MAC). Prefer
    // the encrypt-then-MAC variants (OpenSSH's default), require the same MAC both directions.
    const AlgCand mc[4] = {{"hmac-sha2-256-etm@openssh.com", SSH_MAC_HMAC_SHA256_ETM, PROTO_TRUE},
                           {"hmac-sha2-512-etm@openssh.com", SSH_MAC_HMAC_SHA512_ETM, PROTO_TRUE},
                           {ALG_MAC, SSH_MAC_HMAC_SHA256, PROTO_TRUE},
                           {"hmac-sha2-512", SSH_MAC_HMAC_SHA512, PROTO_TRUE}};
    proto_bool need_mac = (s->cipher_alg == SSH_CIPHER_AES256CTR);
    int m_c2s = SSH_MAC_HMAC_SHA256;
    int m_s2c = SSH_MAC_HMAC_SHA256;
    if (!pc_rd_str(payload, len, &off, &list, &nlen))
    {
        return -1;
    }
    if (need_mac && !negotiate_alg(list, nlen, mc, 4, &m_c2s))
    {
        return -1;
    }
    if (!pc_rd_str(payload, len, &off, &list, &nlen))
    {
        return -1;
    }
    if (need_mac && (!negotiate_alg(list, nlen, mc, 4, &m_s2c) || m_s2c != m_c2s))
    {
        return -1;
    }
    s->mac_alg = m_c2s;
    // compression c2s + s2c: negotiate zlib@openssh.com > zlib > none per direction. c2s: the client
    // compresses, we inflate (ssh_inflate); s2c: we compress, the client inflates (ssh_zlib).
#if PC_ENABLE_SSH_ZLIB
    {
        const AlgCand compc[3] = {{"zlib@openssh.com", SSH_COMP_ZLIB_DELAYED, PROTO_TRUE},
                                  {"zlib", SSH_COMP_ZLIB, PROTO_TRUE},
                                  {"none", SSH_COMP_NONE, PROTO_TRUE}};
        int comp = 0;
        if (!pc_rd_str(payload, len, &off, &list, &nlen) || !negotiate_alg(list, nlen, compc, 3, &comp))
        {
            return -1;
        }
        SshComp.set_c2s(i, comp);
        if (!pc_rd_str(payload, len, &off, &list, &nlen) || !negotiate_alg(list, nlen, compc, 3, &comp))
        {
            return -1;
        }
        SshComp.set_s2c(i, comp);
    }
#else
    // Both directions must offer "none" (no compression built in).
    if (!pc_rd_str(payload, len, &off, &list, &nlen) || !namelist_contains(list, nlen, ALG_COMP))
    {
        return -1;
    }
    if (!pc_rd_str(payload, len, &off, &list, &nlen) || !namelist_contains(list, nlen, ALG_COMP))
    {
        return -1;
    }
#endif

    s->phase = SSH_PHASE_DH_INIT;
    return 0;
}

static int ssh_extinfo_build(uint8_t *out, size_t *len, size_t cap)
{
    // byte SSH_MSG_EXT_INFO || uint32 nr-extensions || (string name, string value)*
    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, SSH_MSG_EXT_INFO);
    w_u32(&w, 1);                      // one extension
    w_namelist(&w, "server-sig-algs"); // extension name
    // Accepted client public-key signature algorithms for userauth. All are always
    // verifiable (independent of which host key we hold); ordered by our preference so a
    // modern client picks the steered-to type. A client uses this to choose a key to offer.
    // Both RSA hashes are offered (rsa-sha2-512 first, RFC 8332); pc_rsa_verify picks the
    // hash from the client's chosen algorithm name. ecdsa-sha2-nistp256 (RFC 5656) and
    // ssh-ed25519 are also verifiable, so all four are advertised in preference order.
    const char *siglist = s_sshtr.prefer_rsa ? "rsa-sha2-512,rsa-sha2-256,ecdsa-sha2-nistp256,ssh-ed25519"
                                             : "ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-512,rsa-sha2-256";
    w_namelist(&w, siglist); // value: accepted client-sig algorithms
    if (!w.ok)
    {
        return -1;
    }
    *len = w.len;
    return 0;
}

// ---------------------------------------------------------------------------
// Exchange hash H (RFC 4253 §8) - streamed into the negotiated KEX hash (SHA-256 or SHA-512 via
// SshKexHash), no large buffer.
// ---------------------------------------------------------------------------

// Hash a 4-byte big-endian length prefix.
static void hash_u32(SshKexHash *h, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    ssh_kexhash_update(h, b, 4);
}

// Hash an SSH string: uint32 length + raw bytes.
static void hash_string(SshKexHash *h, const uint8_t *data, size_t len)
{
    hash_u32(h, (uint32_t)len);
    ssh_kexhash_update(h, data, len);
}

// Hash an SSH mpint from a fixed-width big-endian integer: strip leading zero
// bytes, prepend a 0x00 if the top bit is set (to keep it positive).
static void hash_mpint(SshKexHash *h, const uint8_t *be, size_t len)
{
    size_t off = 0;
    while (off < len && be[off] == 0)
    {
        off++;
    }
    if (off == len)
    {
        // Value is zero → mpint is an empty string.
        hash_u32(h, 0);
        return;
    }
    proto_bool pad = (be[off] & 0x80u) != 0;
    uint32_t mlen = (uint32_t)(len - off) + (pad ? 1u : 0u);
    hash_u32(h, mlen);
    if (pad)
    {
        uint8_t zero = 0;
        ssh_kexhash_update(h, &zero, 1);
    }
    ssh_kexhash_update(h, be + off, len - off);
}

// The exchange-hash algorithm for a KEX method: SHA-512 for sntrup761x25519-sha512, else SHA-256.
static inline proto_bool kex_is_sha512(SshKexAlg a)
{
    return a == SSH_KEX_SNTRUP761_X25519;
}

// Method-neutral exchange hash. The client/server public values are hashed as SSH
// strings for an ECDH KEX (Q_C, Q_S; RFC 8731) or the PQ/T hybrid (C_INIT, S_REPLY), or as mpints
// for a finite-field DH KEX (e, f; RFC 4253 §8). K is an mpint for the classical methods but a plain
// string for the hybrid (its K is a fixed-length HASH output, RFC 4251 §5 / draft-ietf-sshm). cpub/
// spub are big-endian, right-aligned in their buffers, so hash_mpint / hash_string produce the
// canonical minimal encoding.
// @p out holds SSH_KEXHASH_MAX_LEN; the exchange-hash length (32 or 64) is written to @p out_len and
// selected by @p is512 (the negotiated KEX's hash). Returns 0, or -1 on a bad slot.
static int compute_exchange_hash(uint8_t i, proto_bool pub_is_string, const uint8_t *cpub, size_t cpub_len,
                                 const uint8_t *spub, size_t spub_len, const uint8_t *k_be, size_t k_len,
                                 const uint8_t *ks, size_t ks_len, uint8_t out[SSH_KEXHASH_MAX_LEN], size_t *out_len,
                                 proto_bool k_is_string, proto_bool is512)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshSession *s = &ssh_sess[i];

    static const char *const v_s = SSH_SERVER_VERSION;

    SshKexHash h;
    ssh_kexhash_init(&h, is512);
    hash_string(&h, (const uint8_t *)s->v_c, s->v_c_len);                  // V_C
    hash_string(&h, (const uint8_t *)v_s, sizeof(SSH_SERVER_VERSION) - 1); // V_S
    hash_string(&h, s->i_c, s->i_c_len);                                   // I_C
    hash_string(&h, s->i_s, s->i_s_len);                                   // I_S
    hash_string(&h, ks, ks_len);                                           // K_S
    if (pub_is_string)
    {
        hash_string(&h, cpub, cpub_len); // Q_C
        hash_string(&h, spub, spub_len); // Q_S
    }
    else
    {
        hash_mpint(&h, cpub, cpub_len); // e
        hash_mpint(&h, spub, spub_len); // f
    }
    if (k_is_string)
    {
        hash_string(&h, k_be, k_len); // hybrid: K is a fixed-length HASH output (RFC 4251 string)
    }
    else
    {
        hash_mpint(&h, k_be, k_len); // classical: K is an mpint
    }
    *out_len = ssh_kexhash_final(&h, out);
    return 0;
}

static int ssh_kex_exchange_hash(uint8_t i, const uint8_t *e_be, const uint8_t *f_be, const uint8_t *k_be,
                                 const uint8_t *ks, size_t ks_len, uint8_t out[PC_SHA256_DIGEST_LEN])
{
    size_t out_len = 0; // dh-group14-sha256 is always SHA-256
    return compute_exchange_hash(i, PROTO_FALSE, e_be, 256, f_be, 256, k_be, 256, ks, ks_len, out, &out_len,
                                 PROTO_FALSE, PROTO_FALSE);
}

// ---------------------------------------------------------------------------
// KEXDH (RFC 4253 §8)
// ---------------------------------------------------------------------------

static int ssh_kexdh_parse_init(const uint8_t *payload, size_t len, uint8_t e_be[256])
{
    if (len < 1 + 4 || payload[0] != SSH_MSG_KEXDH_INIT)
    {
        return -1;
    }

    uint32_t n = ((uint32_t)payload[1] << 24) | ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 8) |
                 (uint32_t)payload[4];
    if ((size_t)5 + n > len)
    {
        return -1;
    }

    const uint8_t *m = payload + 5;
    size_t off = 0;
    while (off < n && m[off] == 0) // strip sign/leading-zero bytes
    {
        off++;
    }
    size_t vlen = n - off;
    if (vlen > 256)
    {
        return -1; // e exceeds 2048 bits
    }

    memset(e_be, 0, 256);
    memcpy(e_be + (256 - vlen), m + off, vlen);
    return 0;
}

static int ssh_kexdh_build_reply(const uint8_t *ks, size_t ks_len, const uint8_t *f_be, const uint8_t *sig,
                                 size_t sig_len, uint8_t *out, size_t *out_len, size_t cap)
{
    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, SSH_MSG_KEXDH_REPLY);
    w_string(&w, ks, ks_len); // K_S
    w_mpint(&w, f_be, 256);   // f

    // signature = string( string("rsa-sha2-256") || string(sig) )
    uint32_t inner = 4 + 12 + 4 + (uint32_t)sig_len;
    w_u32(&w, inner);
    w_string(&w, (const uint8_t *)"rsa-sha2-256", 12);
    w_string(&w, sig, sig_len);

    if (!w.ok)
    {
        return -1;
    }
    *out_len = w.len;
    return 0;
}

// Parse SSH_MSG_KEX_ECDH_INIT (msg 30, shares the number with KEXDH_INIT):
// byte(30) || string(Q_C). Q_C must be exactly 32 bytes for X25519 (RFC 8731).
static int parse_ecdh_init(const uint8_t *payload, size_t len, uint8_t qc[32])
{
    if (len < 1 + 4 || payload[0] != SSH_MSG_KEXDH_INIT)
    {
        return -1;
    }
    uint32_t n = ((uint32_t)payload[1] << 24) | ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 8) |
                 (uint32_t)payload[4];
    if (n != 32 || (size_t)5 + n > len)
    {
        return -1;
    }
    memcpy(qc, payload + 5, 32);
    return 0;
}

// Parse SSH_MSG_KEX_ECDH_INIT for ecdh-sha2-nistp256 (RFC 5656 §4): byte(30) || string(Q_C),
// where Q_C is the 65-byte uncompressed client point 0x04 || X || Y.
static int parse_ecdh_init_p256(const uint8_t *payload, size_t len, uint8_t qc[PC_ECDSA_P256_PUB_LEN])
{
    if (len < 1 + 4 || payload[0] != SSH_MSG_KEXDH_INIT)
    {
        return -1;
    }
    uint32_t n = ((uint32_t)payload[1] << 24) | ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 8) |
                 (uint32_t)payload[4];
    if (n != PC_ECDSA_P256_PUB_LEN || (size_t)5 + n > len)
    {
        return -1;
    }
    memcpy(qc, payload + 5, PC_ECDSA_P256_PUB_LEN);
    return 0;
}

// Encode the server host-key blob K_S for the negotiated host-key algorithm.
//   rsa-sha2-256/512     → "ssh-rsa" blob (ssh_rsa_encode_pubkey)
//   ssh-ed25519          → string("ssh-ed25519") || string(pub32)          (RFC 8709 §4)
//   ecdsa-sha2-nistp256  → string(name) || string("nistp256") || string(Q) (RFC 5656 §3.1)
static int encode_hostkey(uint8_t i, uint8_t *ks, size_t *ks_len, size_t cap)
{
    if (ssh_sess[i].hostkey_alg == SSH_HOSTKEY_ED25519)
    {
        Writer w = {ks, cap, 0, PROTO_TRUE};
        w_string(&w, (const uint8_t *)HOSTKEY_ED, sizeof(HOSTKEY_ED) - 1);
        w_string(&w, s_sshtr.ed_pub, 32);
        if (!w.ok)
        {
            return -1;
        }
        *ks_len = w.len;
        return 0;
    }
    if (ssh_sess[i].hostkey_alg == SSH_HOSTKEY_ECDSA_NISTP256)
    {
        Writer w = {ks, cap, 0, PROTO_TRUE};
        w_string(&w, (const uint8_t *)HOSTKEY_ECDSA, sizeof(HOSTKEY_ECDSA) - 1);
        w_string(&w, (const uint8_t *)"nistp256", 8); // RFC 5656 curve identifier
        w_string(&w, s_sshtr.ecdsa_pub, PC_ECDSA_P256_PUB_LEN);
        if (!w.ok)
        {
            return -1;
        }
        *ks_len = w.len;
        return 0;
    }
    return ssh_rsa_encode_pubkey(ks, ks_len, cap);
}

// Sign the exchange hash H with the negotiated host key. Writes the raw signature (no
// SSH framing) plus its algorithm name; the caller wraps it as the signature blob.
static int sign_hash(uint8_t i, const uint8_t *H, size_t h_len, uint8_t *sig, size_t *sig_len, size_t sig_cap,
                     const char **sig_name)
{
    if (ssh_sess[i].hostkey_alg == SSH_HOSTKEY_ED25519)
    {
        if (sig_cap < 64)
        {
            return -1;
        }
        pc_ed25519_sign(sig, H, h_len, s_sshtr.ed_seed);
        *sig_len = 64;
        *sig_name = HOSTKEY_ED; // "ssh-ed25519"
        return 0;
    }
    if (ssh_sess[i].hostkey_alg == SSH_HOSTKEY_ECDSA_NISTP256)
    {
        uint8_t raw[PC_ECDSA_P256_SIG_LEN]; // r || s (32 + 32)
        if (!pc_ecdsa_p256_sign(raw, H, h_len, s_sshtr.ecdsa_priv))
        {
            return -1;
        }
        // ECDSA signature blob is mpint(r) || mpint(s) (RFC 5656 §3.1.2).
        Writer w = {sig, sig_cap, 0, PROTO_TRUE};
        w_mpint(&w, raw, PC_ECDSA_P256_COORD_LEN);
        w_mpint(&w, raw + PC_ECDSA_P256_COORD_LEN, PC_ECDSA_P256_COORD_LEN);
        if (!w.ok)
        {
            return -1;
        }
        *sig_len = w.len;
        *sig_name = HOSTKEY_ECDSA; // "ecdsa-sha2-nistp256"
        return 0;
    }
    // rsa-sha2-512 and rsa-sha2-256 share the one "ssh-rsa" key; the negotiated
    // algorithm only chooses the signature hash (RFC 8332).
    const proto_bool sha512 = (ssh_sess[i].hostkey_alg == SSH_HOSTKEY_RSA_SHA512);
    const pc_rsa_hash rh = sha512 ? PC_RSA_HASH_SHA512 : PC_RSA_HASH_SHA256;
    if (sig_cap < PC_RSA_SIG_BYTES || ssh_rsa_sign(H, h_len, rh, sig) != 0)
    {
        return -1;
    }
    *sig_len = PC_RSA_SIG_BYTES;
    *sig_name = sha512 ? HOSTKEY_RSA_SHA512 : HOSTKEY_RSA_SHA256;
    return 0;
}

// Assemble SSH_MSG_KEXDH_REPLY (== KEX_ECDH_REPLY / KEX_HYBRID_REPLY, msg 31):
//   byte(31) || string(K_S) || (mpint f | string Q_S | string S_REPLY) || string( string(sig_name) || string(sig) )
static int build_kex_reply(uint8_t i, const uint8_t *ks, size_t ks_len, const uint8_t *spub, size_t spub_len,
                           const char *sig_name, const uint8_t *sig, size_t sig_len, uint8_t *out, size_t *out_len,
                           size_t cap)
{
    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, SSH_MSG_KEXDH_REPLY);
    w_string(&w, ks, ks_len); // K_S
    if (ssh_sess[i].kex_alg == SSH_KEX_DH_GROUP14)
    {
        w_mpint(&w, spub, spub_len); // f (mpint)
    }
    else
    {
        w_string(&w, spub, spub_len); // Q_S (curve25519) or S_REPLY (hybrid), a raw string
    }
    uint32_t nl = (uint32_t)strnlen(sig_name, w.cap);
    w_u32(&w, 4 + nl + 4 + (uint32_t)sig_len); // signature blob length
    w_string(&w, (const uint8_t *)sig_name, nl);
    w_string(&w, sig, sig_len);
    if (!w.ok)
    {
        return -1;
    }
    *out_len = w.len;
    return 0;
}

static int ssh_kex_generate(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshKexAlg a = ssh_sess[i].kex_alg;
    proto_bool curve = (a == SSH_KEX_CURVE25519);
#if PC_ENABLE_PQC_KEX
    // both PQ/T hybrids run X25519 as the classical half.
    curve = curve || (a == SSH_KEX_MLKEM768_X25519) || (a == SSH_KEX_SNTRUP761_X25519);
#endif
    if (curve)
    {
        // X25519 ephemeral: random 32-byte scalar, public = X25519(scalar, base point).
#ifdef PC_SSH_KEX_BENCH
        int64_t kexgen_t0 = esp_timer_get_time();
#endif
        pc_rand_fill(ssh_sess[i].ecdh_sk, 32);
        pc_x25519_base(ssh_sess[i].ecdh_pk, ssh_sess[i].ecdh_sk);
#ifdef PC_SSH_KEX_BENCH
        pc_ssh_kex_bench.last_kexgen_us = (long long)(esp_timer_get_time() - kexgen_t0);
#endif
        return 0;
    }
    if (a == SSH_KEX_ECDH_NISTP256)
    {
        // P-256 ECDH ephemeral: a random scalar d in [1, n) stored in ecdh_sk. The 65-byte public
        // point Q_S = d*G is re-derived in ssh_kexdh_handle (avoids a curve-specific session field).
        // Re-draw on the negligible chance a raw 32-byte value is 0 or >= n (an invalid P-256 scalar).
        uint8_t qtmp[PC_ECDSA_P256_PUB_LEN];
        for (int t = 0; t < 8; t++)
        { // hands back an invalid P-256 scalar, which no host build can provoke
            pc_rand_fill(ssh_sess[i].ecdh_sk, 32);
            if (pc_ecdsa_p256_pubkey(qtmp, ssh_sess[i].ecdh_sk))
            {
                return 0; // valid scalar with overwhelming probability
            }
        }
        return -1;
    }
    return SshDh.generate(i);
}

#if PC_ENABLE_PQC_KEX
// mlkem768x25519-sha256 (draft-ietf-sshm-mlkem-hybrid-kex): from the client's SSH_MSG_KEX_HYBRID_INIT
// (byte 30 || string C_INIT, C_INIT = ek(1184) || Q_C(32)), ML-KEM-Encaps to the peer's key and X25519
// against Q_C, then combine K = SHA256(K_PQ || K_CL). Writes S_REPLY = ciphertext(1088) || Q_S(32) and
// the 32-byte shared secret. Returns 0, or -1 on a malformed C_INIT, bad ML-KEM key, or low-order point.
static int hybrid_mlkem_x25519(uint8_t i, const uint8_t *payload, size_t len, uint8_t s_reply[MLKEM768_CT_BYTES + 32],
                               uint8_t k_out[32])
{
    if (len < 1 + 4 || payload[0] != SSH_MSG_KEXDH_INIT)
    {
        return -1;
    }
    uint32_t n = ((uint32_t)payload[1] << 24) | ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 8) |
                 (uint32_t)payload[4];
    if (n != MLKEM768_EK_BYTES + 32 || (size_t)5 + n > len)
    {
        return -1;
    }
    const uint8_t *ek = payload + 5;                     // C_PK2: ML-KEM-768 encapsulation key
    const uint8_t *qc = payload + 5 + MLKEM768_EK_BYTES; // C_PK1: client X25519 public

    uint8_t m[32];
    pc_rand_fill(m, sizeof(m));
    uint8_t k_pq[32];
    proto_bool ok = pc_mlkem768_encaps(ek, m, s_reply, k_pq); // ciphertext -> s_reply[0..1087]
    pc_secure_wipe(m, sizeof(m));
    if (!ok)
    {
        return -1; // malformed encapsulation key (FIPS 203 modulus check)
    }

    uint8_t k_cl[32];
    pc_x25519(k_cl, ssh_sess[i].ecdh_sk, qc);
    uint8_t zacc = 0;
    for (int b = 0; b < 32; b++)
    {
        zacc |= k_cl[b];
    }
    if (zacc == 0) // low-order X25519 point (RFC 7748 §6.1)
    {
        pc_secure_wipe(k_pq, sizeof(k_pq));
        pc_secure_wipe(k_cl, sizeof(k_cl));
        return -1;
    }
    memcpy(s_reply + MLKEM768_CT_BYTES, ssh_sess[i].ecdh_pk, 32); // S_PK1: server X25519 public

    pc_sha256_ctx hc;
    pc_sha256_init(&hc);
    pc_sha256_update(&hc, k_pq, sizeof(k_pq)); // K = SHA256(K_PQ || K_CL) (RFC 9370 concat combiner)
    pc_sha256_update(&hc, k_cl, sizeof(k_cl));
    pc_sha256_final(&hc, k_out);
    pc_secure_wipe(k_pq, sizeof(k_pq));
    pc_secure_wipe(k_cl, sizeof(k_cl));
    return 0;
}
#endif // PC_ENABLE_PQC_KEX (ML-KEM hybrid)

#if PC_ENABLE_SSH_SNTRUP761
// sntrup761x25519-sha512@openssh.com: from C_INIT (byte 30 || string, string = sntrup761_pk(1158) ||
// Q_C(32)), sntrup761-Encaps to the peer's key and X25519 against Q_C, then combine K = SHA512(K_PQ ||
// K_CL) (64 bytes). Writes S_REPLY = ciphertext(1039) || Q_S(32) and the 64-byte shared secret. Returns
// 0, or -1 on a malformed C_INIT or a low-order X25519 point.
static int hybrid_sntrup761_x25519(uint8_t i, const uint8_t *payload, size_t len,
                                   uint8_t s_reply[PC_SNTRUP761_CT_BYTES + 32], uint8_t k_out[64])
{
    if (len < 1 + 4 || payload[0] != SSH_MSG_KEXDH_INIT)
    {
        return -1;
    }
    uint32_t n = ((uint32_t)payload[1] << 24) | ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 8) |
                 (uint32_t)payload[4];
    if (n != PC_SNTRUP761_PK_BYTES + 32 || (size_t)5 + n > len)
    {
        return -1;
    }
    const uint8_t *pk = payload + 5;                         // C_PK2: sntrup761 public key
    const uint8_t *qc = payload + 5 + PC_SNTRUP761_PK_BYTES; // C_PK1: client X25519 public

    uint8_t k_pq[PC_SNTRUP761_SS_BYTES];
    pc_sntrup761_enc(pk, s_reply, k_pq); // ciphertext -> s_reply[0..1038], shared -> k_pq

    uint8_t k_cl[32];
    pc_x25519(k_cl, ssh_sess[i].ecdh_sk, qc);
    uint8_t zacc = 0;
    for (int b = 0; b < 32; b++)
    {
        zacc |= k_cl[b];
    }
    if (zacc == 0) // low-order X25519 point (RFC 7748 §6.1)
    {
        pc_secure_wipe(k_pq, sizeof(k_pq));
        pc_secure_wipe(k_cl, sizeof(k_cl));
        return -1;
    }
    memcpy(s_reply + PC_SNTRUP761_CT_BYTES, ssh_sess[i].ecdh_pk, 32); // S_PK1: server X25519 public

    pc_sha512_ctx hc;
    pc_sha512_init(&hc);
    pc_sha512_update(&hc, k_pq, sizeof(k_pq)); // K = SHA512(K_PQ || K_CL) (RFC 9370 concat combiner)
    pc_sha512_update(&hc, k_cl, sizeof(k_cl));
    pc_sha512_final(&hc, k_out);
    pc_secure_wipe(k_pq, sizeof(k_pq));
    pc_secure_wipe(k_cl, sizeof(k_cl));
    return 0;
}
#endif // PC_ENABLE_SSH_SNTRUP761

static int ssh_kexdh_handle(uint8_t i, const uint8_t *payload, size_t len, uint8_t *reply_out, size_t *reply_len,
                            size_t cap)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshSession *s = &ssh_sess[i];
#ifdef PC_SSH_KEX_BENCH
    int64_t kexreply_t0 = esp_timer_get_time();
#endif

    // 1. Shared secret K + the two public values, per negotiated KEX method. cpub_p / spub_p point at
    //    the values hashed into H (local buffers for DH / curve, the larger C_INIT / S_REPLY blobs for
    //    the hybrid); k_hash / k_hash_len select K's encoding (an mpint for DH / curve, a fixed 32-byte
    //    string for the hybrid). k_be holds K right-aligned so hash_mpint / the KDF strip to minimal.
    uint8_t k_be[256];
    memset(k_be, 0, sizeof(k_be));
    uint8_t cpub[256];
    uint8_t spub[256]; // client / server public value (right-aligned) for DH / curve25519
    const uint8_t *cpub_p = cpub;
    const uint8_t *spub_p = spub;
    size_t cpub_len = 256;
    size_t spub_len = 256;
    const uint8_t *k_hash = k_be;
    size_t k_hash_len = 256;
    proto_bool pub_is_string = PROTO_FALSE;
    proto_bool k_is_string = PROTO_FALSE;
#if PC_ENABLE_PQC_KEX
    uint8_t s_reply[MLKEM768_CT_BYTES + 32]; // hybrid S_REPLY = ciphertext(1088) || Q_S(32)
#endif

    if (s->kex_alg == SSH_KEX_CURVE25519)
    {
        // curve25519-sha256 (RFC 8731): K = X25519(sk, Q_C); Q_C/Q_S hashed as strings.
        uint8_t qc[32];
        uint8_t kk[32];
        if (parse_ecdh_init(payload, len, qc) != 0)
        {
            return -1;
        }
        pc_x25519(kk, s->ecdh_sk, qc);
        // Reject a low-order client point (all-zero shared secret) - RFC 7748 §6.1.
        uint8_t zacc = 0;
        for (int b = 0; b < 32; b++)
        {
            zacc |= kk[b];
        }
        if (zacc == 0)
        {
            pc_secure_wipe(kk, sizeof(kk));
            return -1;
        }
        memcpy(k_be + (256 - 32), kk, 32);
        memcpy(cpub, qc, 32);
        memcpy(spub, s->ecdh_pk, 32);
        cpub_len = spub_len = 32;
        pub_is_string = PROTO_TRUE;
        pc_secure_wipe(kk, sizeof(kk));
    }
#if PC_ENABLE_PQC_KEX
    else if (s->kex_alg == SSH_KEX_MLKEM768_X25519)
    {
        // mlkem768x25519-sha256: K = SHA256(K_PQ || K_CL); C_INIT / S_REPLY and K hashed as strings.
        if (hybrid_mlkem_x25519(i, payload, len, s_reply, k_be + (256 - 32)) != 0)
        {
            return -1;
        }
        cpub_p = payload + 5; // C_INIT (ek || Q_C), hashed verbatim as a string
        cpub_len = MLKEM768_EK_BYTES + 32;
        spub_p = s_reply; // S_REPLY (ciphertext || Q_S)
        spub_len = MLKEM768_CT_BYTES + 32;
        k_hash = k_be + (256 - 32); // K is exactly 32 bytes, string-encoded (not mpint)
        k_hash_len = 32;
        pub_is_string = PROTO_TRUE;
        k_is_string = PROTO_TRUE;
    }
#endif // PC_ENABLE_PQC_KEX (ML-KEM dispatch)
#if PC_ENABLE_SSH_SNTRUP761
    else if (s->kex_alg == SSH_KEX_SNTRUP761_X25519)
    {
        // sntrup761x25519-sha512: K = SHA512(K_PQ || K_CL) (64 bytes); C_INIT / S_REPLY and K are strings.
        if (hybrid_sntrup761_x25519(i, payload, len, s_reply, k_be + (256 - 64)) != 0)
        {
            return -1;
        }
        cpub_p = payload + 5; // C_INIT (sntrup761_pk || Q_C), hashed verbatim as a string
        cpub_len = PC_SNTRUP761_PK_BYTES + 32;
        spub_p = s_reply; // S_REPLY (ciphertext || Q_S)
        spub_len = PC_SNTRUP761_CT_BYTES + 32;
        k_hash = k_be + (256 - 64); // K is exactly 64 bytes (SHA-512), string-encoded
        k_hash_len = 64;
        pub_is_string = PROTO_TRUE;
        k_is_string = PROTO_TRUE;
    }
#endif // PC_ENABLE_SSH_SNTRUP761
    else if (s->kex_alg == SSH_KEX_ECDH_NISTP256)
    {
        // ecdh-sha2-nistp256 (RFC 5656 §4): K = X(d_S * Q_C). Q_C/Q_S are 65-byte point strings; K an mpint.
        uint8_t qc[PC_ECDSA_P256_PUB_LEN];
        if (parse_ecdh_init_p256(payload, len, qc) != 0)
        {
            return -1;
        }
        uint8_t qs[PC_ECDSA_P256_PUB_LEN];
        uint8_t kk[PC_ECDSA_P256_COORD_LEN];
        // Re-derive our ephemeral public Q_S, then the shared secret. pc_ecdsa_p256_ecdh validates
        // Q_C is on-curve and the product is not the identity (RFC 5656 §4 point checks).
        if (!pc_ecdsa_p256_pubkey(qs, s->ecdh_sk) || !pc_ecdsa_p256_ecdh(kk, qc, s->ecdh_sk))
        {
            return -1;
        }
        memcpy(k_be + (256 - PC_ECDSA_P256_COORD_LEN), kk, PC_ECDSA_P256_COORD_LEN);
        memcpy(cpub, qc, PC_ECDSA_P256_PUB_LEN);
        memcpy(spub, qs, PC_ECDSA_P256_PUB_LEN);
        cpub_len = PC_ECDSA_P256_PUB_LEN;
        spub_len = PC_ECDSA_P256_PUB_LEN;
        pub_is_string = PROTO_TRUE;
        pc_secure_wipe(kk, sizeof(kk));
    }
    else
    {
        // diffie-hellman-group14-sha256 (RFC 4253 §8): K = e^y mod p; e/f are mpints.
        uint8_t e_be[256];
        if (ssh_kexdh_parse_init(payload, len, e_be) != 0)
        {
            return -1;
        }
        pc_bignum e;
        bn_from_bytes(&e, e_be, 256);
        if (bn_dh_validate(&e) != 0)
        {
            return -1;
        }
        pc_bignum K;
        bn_expmod_group14(&K, &e, &ssh_dh[i].y);
        bn_to_bytes(k_be, &K);
        pc_secure_wipe(&K, sizeof(K));
        memcpy(cpub, e_be, 256);
        bn_to_bytes(spub, &ssh_dh[i].f);
    }

    // 2. Host-key blob K_S (per negotiated host-key algorithm).
    uint8_t ks[SSH_RSA_PUBKEY_BLOB_MAX];
    size_t ks_len = 0;
    if (encode_hostkey(i, ks, &ks_len, sizeof(ks)) != 0)
    {
        pc_secure_wipe(k_be, sizeof(k_be));
        return -1;
    }

    // 3. Exchange hash H (SHA-256 or SHA-512 per the KEX method); capture the session id on first KEX.
    const proto_bool is512 = kex_is_sha512(s->kex_alg);
    uint8_t H[SSH_KEXHASH_MAX_LEN];
    size_t h_len = 0;
    compute_exchange_hash(i, pub_is_string, cpub_p, cpub_len, spub_p, spub_len, k_hash, k_hash_len, ks, ks_len, H,
                          &h_len, k_is_string, is512);
    if (!s->have_session_id)
    {
        memcpy(s->session_id, H, h_len);
        s->session_id_len = (uint8_t)h_len;
        s->have_session_id = PROTO_TRUE;
    }

    // 4. Sign H with the negotiated host key (rsa-sha2-512/256 or ssh-ed25519).
    uint8_t sig[PC_RSA_SIG_BYTES]; // 256 bytes: fits an RSA-2048 sig and a 64-byte ed25519 sig
    size_t sig_len = 0;
    const char *sig_name = NULL;
    if (sign_hash(i, H, h_len, sig, &sig_len, sizeof(sig), &sig_name) != 0)
    {
        pc_secure_wipe(k_be, sizeof(k_be));
        return -1;
    }

    // 5. Assemble the reply, then derive the six session keys (id fixed at first KEX's H).
    if (build_kex_reply(i, ks, ks_len, spub_p, spub_len, sig_name, sig, sig_len, reply_out, reply_len, cap) != 0)
    {
        pc_secure_wipe(k_be, sizeof(k_be));
        return -1;
    }
    SshDh.derive_keys_sid(i, k_be, H, s->session_id, s->cipher_alg, s->mac_alg, k_is_string, h_len, s->session_id_len,
                          is512);
    pc_secure_wipe(k_be, sizeof(k_be));

    s->phase = SSH_PHASE_NEWKEYS;
#ifdef PC_SSH_KEX_BENCH
    pc_ssh_kex_bench.last_kexreply_us = (long long)(esp_timer_get_time() - kexreply_t0);
    pc_ssh_kex_bench.kex_count++;
#endif
    return 0;
}

const SshKexNs SshKex = {
    ssh_kexinit_build,     ssh_kexinit_parse,    ssh_kex_set_prefer_rsa, ssh_kex_prefer_rsa, ssh_kex_generate,
    ssh_kex_exchange_hash, ssh_kexdh_parse_init, ssh_kexdh_build_reply,  ssh_kexdh_handle,   &SshDh};

static void ssh_newkeys_sent(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    // We have emitted our SSH_MSG_NEWKEYS: our outbound direction is now encrypted (RFC 4253 sec 7.3).
    ssh_pkt[i].enc_out = PROTO_TRUE;
#if PC_ENABLE_SSH_ZLIB
    // "zlib" (non-delayed) starts its s2c (outbound) stream here; idempotent, so a re-key does not restart it.
    SshComp.on_newkeys(i);
#endif
}

static void ssh_newkeys_complete(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    // We have received the peer's SSH_MSG_NEWKEYS: our inbound direction is now encrypted. Both directions
    // are keyed once we get here (the server always sends its NEWKEYS first), so the KEX is complete.
    ssh_pkt[i].enc_in = PROTO_TRUE;
    ssh_pkt[i].kex_active = PROTO_FALSE;
    // On the first KEX advance to the service phase; on a re-key the connection
    // is already authenticated, so resume the open (channel) phase.
    ssh_sess[i].phase = ssh_sess[i].authed ? SSH_PHASE_OPEN : SSH_PHASE_SERVICE;
    // Reset the re-key timer: the volume/time budget is measured from this completed KEX.
    ssh_sess[i].last_kex_ms = pc_millis();
}

static proto_bool ssh_rekey_needed(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return PROTO_FALSE;
    }
    return ssh_pkt[i].seq_no_send >= SSH_REKEY_PACKET_THRESHOLD || ssh_pkt[i].seq_no_recv >= SSH_REKEY_PACKET_THRESHOLD;
}

static proto_bool ssh_rekey_due(uint32_t seq_send, uint32_t seq_recv, uint32_t elapsed_ms, uint32_t pkt_threshold,
                                uint32_t time_threshold_ms)
{
    if (seq_send >= pkt_threshold || seq_recv >= pkt_threshold)
    {
        return PROTO_TRUE; // volume budget (RFC 4253 sec 9: ~1 GB)
    }
    if (time_threshold_ms && elapsed_ms >= time_threshold_ms)
    {
        return PROTO_TRUE; // time budget (~1 hour)
    }
    return PROTO_FALSE;
}

static int ssh_transport_begin_rekey(uint8_t i, uint8_t *out, size_t *out_len, size_t cap)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    // Fresh server KEXINIT (re-stores I_S for the new exchange hash).
    if (ssh_kexinit_build(i, out, out_len, cap) != 0)
    {
        return -1;
    }
    // New ephemeral for forward secrecy across the re-key (re-generated for the finally
    // negotiated method once the peer's KEXINIT arrives; see the KEXINIT dispatch).
    if (ssh_kex_generate(i) != 0)
    {
        return -1;
    }
    ssh_sess[i].phase = SSH_PHASE_KEXINIT;
    return 0;
}

const SshTransportNs SshTransport = {
    ssh_transport_init,
    ssh_transport_server_banner,
    ssh_transport_recv_banner,
    ssh_extinfo_build,
    ssh_newkeys_sent,
    ssh_newkeys_complete,
    ssh_rekey_needed,
    ssh_rekey_due,
    ssh_transport_begin_rekey,
    &SshKex,
    &SshHostkey,
    &SshPacket,
#if PC_ENABLE_SSH_ZLIB
    &SshComp,
#endif
};
