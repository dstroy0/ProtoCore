// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file transport.c
 * @brief RFC 4253: identification exchange, algorithm negotiation, key exchange, binary packet.
 */

#include "network_drivers/presentation/ssh/transport/transport/transport.h"
#include "crypto/aead/aesgcm/aesgcm.h"
#include "crypto/aead/chachapoly/chachapoly.h"
#include "crypto/asymmetric/bignum/bignum.h"         // Bignum, protocore_bignum
#include "crypto/asymmetric/curve25519/curve25519.h" // Curve25519 (curve25519-sha256 KEX)
#include "crypto/asymmetric/ecdsa/ecdsa.h"           // protocore_ecdsa_p256_* (ecdsa-sha2-nistp256 host key)
#include "crypto/asymmetric/ed25519/ed25519.h"       // protocore_ed25519 host-key sign
#include "crypto/asymmetric/rsa/rsa.h"               // PROTOCORE_RSA_KEY_BYTES / _SIG_BYTES - the host-key sizes
#include "crypto/cipher/aes256ctr/aes256ctr.h"       // PROTOCORE_AES256CTR_KEY_LEN / _CTR_LEN - the cipher key and IV
#include "crypto/ct_eq.h"                            // protocore_ct_eq
#include "crypto/hash/sha256/sha256.h"
#include "crypto/mac/hmac_sha256/hmac_sha256.h"
#include "crypto/mac/hmac_sha512/hmac_sha512.h"
#include "crypto/rng/rng.h"           // protocore_rand_fill
#include "mmgr/bytes/bytes.h"         // bytes.* writers / bytes.rd_str() - the byte verbs this file frames with
#include "mmgr/membuild/membuild.h"   // protocore_sb frame builder
#include "mmgr/plaintext/plaintext.h" // protocore_plaintext_span - host-key staging off the worker stack
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h" // str.len / str.eq - the bounded string verbs
#include "mmgr/secure/secure.h"
#include "network_drivers/presentation/ssh/auth/auth.h" // ssh_auth_dispatch()
#include "network_drivers/presentation/ssh/common.h"
#include "network_drivers/presentation/ssh/network/network.h"               // SshNetwork.emit()
#include "network_drivers/presentation/ssh/ssh.h"                           // ssh_conn_slot() + the memory map
#include "network_drivers/presentation/ssh/transport/extension/extension.h" // ssh_extinfo_build()
#include "network_drivers/presentation/ssh/transport/ssh_kexhash/ssh_kexhash.h"
#include "network_drivers/presentation/ssh/transport/ssh_rsa/ssh_rsa.h" // ssh_rsa_encode_pubkey/sign, ssh_host_pubkey, SSH_RSA_*
#include "server/clock/clock.h"                                         // protocore_millis() (re-key timer)
static uint8_t comp_work[16]; // the borrow an entry takes; Comp never reads it

static uint8_t phase_machine_work[16]; // the borrow an entry takes; PhaseMachine never reads it

static uint8_t extension_work[16]; // the borrow an entry takes; Extension never reads it

#if PROTOCORE_ENABLE_PQC_KEX
#include "crypto/pqc/mlkem/mlkem.h" // MlKem (PQ/T hybrid KEX responder)
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
#include "crypto/pqc/sntrup761/sntrup761.h" // Sntrup761 (sntrup761x25519 responder)
#endif
#if PROTOCORE_ENABLE_SSH_ZLIB
#include "network_drivers/presentation/ssh/transport/comp/comp.h" // compression negotiation
#include "network_drivers/presentation/ssh/transport/zlib/zlib.h" // ssh_deflate_bound
#endif
#ifdef PROTOCORE_SSH_KEX_BENCH
// The one owned KEX-bench context (see ssh_transport.h). Set by ssh_kex_generate / ssh_kexdh_handle; the
// rig firmware prints it. Single active connection during a bench run, so a plain instance suffices.
SshKexBenchCtx protocore_ssh_kex_bench = {0, 0, 0};
#endif

SshSession ssh_sess[MAX_SSH_CONNS];
// ---------------------------------------------------------------------------
// Negotiable algorithms. The server supports two KEX methods and two host-key
// types; cipher / MAC / compression are fixed. Negotiation is crypto-agnostic and
// steers toward whichever suite ssh_kex_set_prefer_rsa() selects (default: RSA/DH,
// hardware-accelerated on ESP32), while still advertising both suites so a client
// that supports only one still connects.
// ---------------------------------------------------------------------------

// Arrays, not pointers: sizeof() over these is what the KEXINIT worst case below sums.
static const char KEX_DH[] = "diffie-hellman-group14-sha256";
static const char KEX_C25519[] = "curve25519-sha256";
static const char KEX_C25519_LIBSSH[] = "curve25519-sha256@libssh.org"; // identical wire protocol
static const char KEX_ECDH_NISTP256[] = "ecdh-sha2-nistp256";           // NIST P-256 ECDH (RFC 5656 §4)
#if PROTOCORE_ENABLE_PQC_KEX
static const char KEX_MLKEM768[] = "mlkem768x25519-sha256"; // PQ/T hybrid (ML-KEM-768 + X25519)
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
static const char KEX_SNTRUP761[] = "sntrup761x25519-sha512@openssh.com"; // PQ/T hybrid (NTRU Prime + X25519)
#endif
static const char HOSTKEY_RSA_SHA256[] = "rsa-sha2-256";
static const char HOSTKEY_RSA_SHA512[] = "rsa-sha2-512";
static const char HOSTKEY_ED[] = "ssh-ed25519";
static const char HOSTKEY_ECDSA[] = "ecdsa-sha2-nistp256";
static const char ALG_CIPHER[] = "aes256-ctr";
static const char ALG_CIPHER_GCM[] = "aes256-gcm@openssh.com";
// Advertised cipher preference (OpenSSH's default order): chacha20-poly1305@openssh.com (AEAD)
// first, aes256-gcm@openssh.com (AEAD, HW-accelerated) second, aes256-ctr (HW fallback) last.
static const char ALG_CIPHER_LIST[] = "chacha20-poly1305@openssh.com,aes256-gcm@openssh.com,aes256-ctr";
static const char ALG_MAC[] = "hmac-sha2-256";
// Advertised MAC preference (aes256-ctr only; the chacha AEAD needs none): encrypt-then-MAC first
// (OpenSSH's default), then plain encrypt-and-MAC.
static const char ALG_MAC_LIST[] = "hmac-sha2-256-etm@openssh.com,hmac-sha2-512-etm@openssh.com,"
                                   "hmac-sha2-256,hmac-sha2-512";
static const char ALG_COMP[] = "none";
#if PROTOCORE_ENABLE_SSH_ZLIB
// Compression preference (both directions): zlib@openssh.com (delayed, OpenSSH's default) first, then
// zlib (immediate), then none. s2c deflates (ssh_zlib); c2s inflates OpenSSH's Z_PARTIAL_FLUSH stream
// (ssh_inflate). Advertised for c2s and s2c alike.
static const char ALG_COMP_ZLIB[] = "zlib@openssh.com,zlib,none";
#else
static const char ALG_COMP_ZLIB[] = "none";
#endif
// RFC 8308 indicator a client sets in its kex_algorithms to request EXT_INFO.
// ---------------------------------------------------------------------------
// KEXINIT worst case (RFC 4253 sec 7.1), summed from the names above.
// ---------------------------------------------------------------------------

// One name in a comma-separated list: its text plus the separator that may precede it.
#define SSH_ADV(name) (sizeof(name)) // sizeof includes the NUL, which pays for the comma

// kex_algorithms: every method this build advertises, plus ",ext-info-s".
#if PROTOCORE_ENABLE_PQC_KEX
#define SSH_ADV_MLKEM SSH_ADV(KEX_MLKEM768)
#else
#define SSH_ADV_MLKEM 0u
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
#define SSH_ADV_SNTRUP SSH_ADV(KEX_SNTRUP761)
#else
#define SSH_ADV_SNTRUP 0u
#endif
#define SSH_KEXLIST_MAX                                                                                                \
    (SSH_ADV_MLKEM + SSH_ADV_SNTRUP + SSH_ADV(KEX_DH) + SSH_ADV(KEX_ECDH_NISTP256) + SSH_ADV(KEX_C25519) +             \
     SSH_ADV(KEX_C25519_LIBSSH) + sizeof(",ext-info-s"))

// server_host_key_algorithms: all four, which is what a build holding every key type advertises.
#define SSH_HKLIST_MAX                                                                                                 \
    (SSH_ADV(HOSTKEY_RSA_SHA512) + SSH_ADV(HOSTKEY_RSA_SHA256) + SSH_ADV(HOSTKEY_ECDSA) + SSH_ADV(HOSTKEY_ED))

// The payload: msg + cookie + ten name-lists each with its uint32 + the guess flag + the reserved uint32.
#define SSH_KEXINIT_S_WORST                                                                                            \
    (1u + 16u + (4u + SSH_KEXLIST_MAX) + (4u + SSH_HKLIST_MAX) + (2u * (4u + sizeof(ALG_CIPHER_LIST))) +               \
     (2u * (4u + sizeof(ALG_MAC_LIST))) + (2u * (4u + sizeof(ALG_COMP_ZLIB))) + (2u * 4u) + 1u + 4u)

static_assert(PROTOCORE_SSH_KEXINIT_S_MAX >= SSH_KEXINIT_S_WORST,
              "PROTOCORE_SSH_KEXINIT_S_MAX must cover every name this build advertises: raise it in ssh_transport.h");

// The two lists are joined in these before they reach the payload, so each holds its own worst case.
#define SSH_KEXLIST_BUF 192
#define SSH_HKLIST_BUF 64
static_assert(SSH_KEXLIST_BUF >= SSH_KEXLIST_MAX, "kexlist[] drops an advertised KEX name: raise SSH_KEXLIST_BUF");
static_assert(SSH_HKLIST_BUF >= SSH_HKLIST_MAX, "hklist[] drops an advertised host-key name: raise SSH_HKLIST_BUF");

// All SSH transport host-key/KEX state, owned by one instance (internal linkage): the runtime
// KEX preference (true = prefer the hardware-accelerated RSA/DH suite) and the optional
// ssh-ed25519 host key (the RSA host key is loaded via ssh_rsa). One named owner, cross-TU
// unreachable.
struct SshTransportStorage
{
    uint8_t ed_seed[32];                               ///< ssh-ed25519 host signing seed.
    uint8_t ed_pub[32];                                ///< its raw public key.
    proto_bool ed_have;                                ///< that key is loaded.
    uint8_t ecdsa_priv[PROTOCORE_ECDSA_P256_PRIV_LEN]; ///< P-256 host private scalar d.
    uint8_t ecdsa_pub[PROTOCORE_ECDSA_P256_PUB_LEN];   ///< P-256 host public point (0x04||X||Y).
    proto_bool ecdsa_have;                             ///< that key is loaded.
    proto_bool prefer_rsa; ///< the runtime KEX preference: prefer the accelerated RSA/DH suite.
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SSH_TRANSPORT_OFF_CTX 0u
static_assert(SSH_TRANSPORT_OFF_CTX + sizeof(struct SshTransportStorage) <= PROTOCORE_SSH_TRANSPORT_BORROW,
              "PROTOCORE_SSH_TRANSPORT_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SSH_TRANSPORT_CTX(w) ((struct SshTransportStorage *)(void *)((w) + SSH_TRANSPORT_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SSH_TRANSPORT_BORROW persistent bytes, or null while the pool was short
} SshTransportOwnCtx;
static SshTransportOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ssh_transport_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_secure_persist_span(PROTOCORE_SSH_TRANSPORT_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
            // A borrow arrives zeroed, and these do not start at zero.
            SSH_TRANSPORT_CTX(s_own.span)->prefer_rsa = PROTO_TRUE;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

void ssh_kex_set_prefer_rsa(proto_bool prefer)
{
    SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->prefer_rsa = prefer;
}
proto_bool ssh_kex_prefer_rsa(void)
{
    return SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->prefer_rsa;
}

void protocore_ssh_hostkey_ed25519_set(const uint8_t seed[32])
{
    // The public key derives through SHA-512, out of slot 0's bytes.
    if (!ssh_pkt_slot_storage(&ssh_pkt[0]))
    {
        return;
    }
    mem.cpy(SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ed_seed, seed, 32);
    Ed25519.pubkey_args.seed = SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ed_seed;
    Ed25519.pubkey_args.pub = SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ed_pub;
    Ed25519.pubkey(ssh_pkt[0].crypto_work);
    SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ed_have = PROTO_TRUE;
}
proto_bool protocore_ssh_hostkey_ed25519_available(void)
{
    return SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ed_have;
}
void protocore_ssh_hostkey_ecdsa_set(const uint8_t priv[PROTOCORE_ECDSA_P256_PRIV_LEN])
{
    // Derive and cache the public point; reject an invalid scalar (leaves ecdsa_have false).
    Ecdsa.pubkey_args.priv = priv;
    Ecdsa.pubkey_args.pub = SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ecdsa_pub;
    Ecdsa.pubkey(ssh_pkt[0].crypto_work);
    if (!Ecdsa.ok)
    {
        return;
    }
    mem.cpy(SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ecdsa_priv, priv, PROTOCORE_ECDSA_P256_PRIV_LEN);
    SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ecdsa_have = PROTO_TRUE;
}
proto_bool protocore_ssh_hostkey_ecdsa_available(void)
{
    return SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ecdsa_have;
}
static proto_bool hostkey_rsa_available(void)
{
    return ssh_host_pubkey.loaded;
}

// Build the KEX / host-key advertise lists in preference order (RFC 4253 §7.1 name-
// lists). A server lists the host-key types it holds, a client the ones it accepts.
// The kex_algorithms list carries this role's RFC 8308 sec 2.2 indicator as its last name.
// server-sig-algs (RFC 8308) uses the same host-key ordering. Written into a caller buffer.
static void build_kex_list(char *out, size_t cap, proto_bool as_client)
{
    // The indicator is the list's last name, so it carries the separator that precedes it.
    char ext_info[SSH_EXT_INFO_INDICATOR_MAX];
    ext_info[0] = ',';
    Extension.info_indicator_args.client_role = as_client;
    Extension.info_indicator(extension_work);
    str.copy(ext_info + 1, Extension.text, sizeof(ext_info) - 1);
    const char *c1 = KEX_C25519;
    const char *c2 = KEX_C25519_LIBSSH;
    const char *dh = KEX_DH;
    const char *ec = KEX_ECDH_NISTP256; // NIST P-256 ECDH (RFC 5656)
    size_t n = 0;
    // Post-quantum hybrids advertised first: a PQC-capable peer (OpenSSH 9.9+, which also lists them
    // first) negotiates one over classical X25519, closing the harvest-now-decrypt-later gap. Each
    // is gated independently so a footprint-bound build can offer ML-KEM only (kexlist[192] holds
    // the full both-hybrid list with margin, so the appends never truncate).
#if PROTOCORE_ENABLE_PQC_KEX
    protocore_sb sb_mlkem = {out + n, cap - n, 0, PROTO_TRUE};
    Sb.put(&sb_mlkem, KEX_MLKEM768);
    protocore_sb_lit(&sb_mlkem, ",");
    n += Sb.finish(&sb_mlkem);
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
    protocore_sb sb_sntrup = {out + n, cap - n, 0, PROTO_TRUE};
    Sb.put(&sb_sntrup, KEX_SNTRUP761);
    protocore_sb_lit(&sb_sntrup, ",");
    n += Sb.finish(&sb_sntrup);
#endif
    if (SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->prefer_rsa)
    {
        protocore_sb sb157 = {out + n, cap - n, 0, PROTO_TRUE};
        Sb.put(&sb157, dh);
        Sb.put(&sb157, ",");
        Sb.put(&sb157, ec);
        Sb.put(&sb157, ",");
        Sb.put(&sb157, c1);
        Sb.put(&sb157, ",");
        Sb.put(&sb157, c2);
        protocore_sb_lit(&sb157, ",");
        Sb.put(&sb157, ext_info);
        if (Sb.finish(&sb157) == 0)
        {
            sb157.p[0] = '\0';
        }
    }
    else
    {
        protocore_sb sb159 = {out + n, cap - n, 0, PROTO_TRUE};
        Sb.put(&sb159, c1);
        Sb.put(&sb159, ",");
        Sb.put(&sb159, c2);
        Sb.put(&sb159, ",");
        Sb.put(&sb159, ec);
        Sb.put(&sb159, ",");
        Sb.put(&sb159, dh);
        protocore_sb_lit(&sb159, ",");
        Sb.put(&sb159, ext_info);
        if (Sb.finish(&sb159) == 0)
        {
            sb159.p[0] = '\0';
        }
    }
}
static void build_hostkey_list(char *out, size_t cap, proto_bool as_client)
{
    // Both rsa-sha2-512 and rsa-sha2-256 are backed by the one "ssh-rsa" host key
    // (RFC 8332): advertise 512 before 256 (OpenSSH's order). A server lists the keys it holds,
    // a client every algorithm verify_host_sig accepts.
    proto_bool rsa = hostkey_rsa_available();
    proto_bool ed = protocore_ssh_hostkey_ed25519_available();
    proto_bool ec = protocore_ssh_hostkey_ecdsa_available();
    if (as_client)
    {
        rsa = PROTO_TRUE;
        ed = PROTO_TRUE;
        ec = PROTO_TRUE;
    }
    // A host key we can offer: the wire name and whether the key is on the device.
    typedef struct
    {
        const char *name;
        proto_bool ok;
    } HostkeyCand;
    HostkeyCand cand[4] = {{0, PROTO_FALSE}, {0, PROTO_FALSE}, {0, PROTO_FALSE}, {0, PROTO_FALSE}};
    if (SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->prefer_rsa)
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
        size_t l = str.len(out, cap);
        protocore_sb sb194 = {out + l, cap - l, 0, PROTO_TRUE};
        Sb.put(&sb194, l ? "," : "");
        Sb.put(&sb194, cand[k].name);
        if (Sb.finish(&sb194) == 0)
        {
            sb194.p[0] = '\0';
        }
    }
}

// RFC 4253 sec 7.1: "The first algorithm in each name-list MUST be the preferred (guessed)
// algorithm." Its length, which is the whole list when there is no comma.
static uint32_t namelist_first_len(const uint8_t *list, uint32_t len)
{
    uint32_t k = 0;
    while (k < len && list[k] != ',')
    {
        k++;
    }
    return k;
}

// Whether a peer's first-listed name is the same name this end lists first.
static proto_bool first_name_is(const uint8_t *peer, uint32_t peer_len, const char *ours)
{
    if (ours == NULL || ours[0] == '\0')
    {
        return PROTO_FALSE;
    }
    const size_t ol = str.len(ours, (size_t)peer_len + 1u);
    return peer_len == ol && mem.cmp(peer, ours, ol) == 0;
}

// RFC 4253 sec 7.1: "If both sides make the same guess, that algorithm MUST be used." Each side's
// guess is the first name in its kex_algorithms and its server_host_key_algorithms, so the guesses
// agree only when the peer's two first names are the two this end lists first. Built from the same
// two builders that write our KEXINIT, so the preference order has one owner.
static proto_bool same_guess(const uint8_t *peer_kex, uint32_t peer_kex_len, const uint8_t *peer_hk,
                             uint32_t peer_hk_len, proto_bool as_client)
{
    char kexlist[SSH_KEXLIST_BUF];
    char hklist[SSH_HKLIST_BUF];
    build_kex_list(kexlist, sizeof(kexlist), as_client);
    build_hostkey_list(hklist, sizeof(hklist), as_client);
    kexlist[namelist_first_len((const uint8_t *)kexlist, (uint32_t)str.len(kexlist, sizeof(kexlist)))] = '\0';
    hklist[namelist_first_len((const uint8_t *)hklist, (uint32_t)str.len(hklist, sizeof(hklist)))] = '\0';
    return first_name_is(peer_kex, peer_kex_len, kexlist) && first_name_is(peer_hk, peer_hk_len, hklist);
}

// ---------------------------------------------------------------------------
// name-list membership test (RFC 4253 §7.1 - comma-separated, no spaces)
// ---------------------------------------------------------------------------

proto_bool namelist_contains(const uint8_t *list, uint32_t len, const char *want)
{
    size_t wl = str.len(want, (size_t)len + 1);
    uint32_t start = 0;
    for (uint32_t i = 0; i <= len; i++)
    {
        if (i == len || list[i] == ',')
        {
            uint32_t elen = i - start;
            if (elen == wl && mem.cmp(list + start, want, wl) == 0)
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
        size_t cl = str.len(cands[c].name, (size_t)tlen + 1);
        if (cands[c].avail && cl == tlen && mem.cmp(tok, cands[c].name, tlen) == 0)
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
// Returns the position of the winning entry in the client's list, or -1 when nothing matched. The
// position is what RFC 4253 sec 7.1 calls the guess: a client guesses with its first-listed algorithm,
// so a win at 0 means it guessed right.
static int negotiate_alg(const uint8_t *peer_list, uint32_t nlen, const AlgCand *cands, int n, int *out,
                         proto_bool as_client)
{
    // As the client the list to iterate is ours, in the order we advertised it, and the peer's is
    // the membership test. As the server the two swap.
    if (as_client)
    {
        for (int c = 0; c < n; c++)
        {
            if (cands[c].avail && namelist_contains(peer_list, nlen, cands[c].name))
            {
                *out = cands[c].tag;
                return c;
            }
        }
        return -1;
    }

    uint32_t start = 0;
    int idx = 0;
    for (uint32_t i = 0; i <= nlen; i++)
    {
        if (i == nlen || peer_list[i] == ',')
        {
            int c = cand_match(peer_list + start, i - start, cands, n);
            if (c >= 0)
            {
                *out = cands[c].tag;
                return idx;
            }
            start = i + 1;
            idx++;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void ssh_transport_init(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    Ssh.conn_slot_args.i = i;
    Ssh.conn_slot(protocore_ssh_span());
    uint8_t *base = Ssh.ptr;
    if (base == NULL)
    {
        return;
    }

    // The session's buffers, at their offsets in the connection's storage.
    SshSession *s = &ssh_sess[i];
    mem.set(s, 0, sizeof(*s));
    s->v_c = (char *)(base + SSH_OFF_V_C);
    s->v_s = (char *)(base + SSH_OFF_V_S);
    s->ident_buf = base + SSH_OFF_IDENT;
    s->i_c = base + SSH_OFF_I_C;
    s->i_s = base + SSH_OFF_I_S;
    s->cpub = base + SSH_OFF_CPUB;
    s->session_id = base + SSH_OFF_SESSION_ID;
    s->ecdh_sk = base + SSH_OFF_ECDH_SK;
    s->ecdh_pk = base + SSH_OFF_ECDH_PK;
    PhaseMachine.reset_args.i = i;
    PhaseMachine.reset(phase_machine_work);
    s->kex_active = PROTO_TRUE; // the first exchange is running from the moment the slot opens

    // Both key epochs, one stride apart; epoch_in / epoch_out select which one a site reads.
    for (uint8_t e = 0; e < 2u; e++)
    {
        SshKeyMat *km = &ssh_keys[i][e];
        uint8_t *k = base + SSH_OFF_EPOCH_0 + ((size_t)e * SSH_EPOCH_STRIDE);
        km->gcm_ctx_c2s = k + SSH_OFF_GCM_C2S;
        km->gcm_ctx_s2c = k + SSH_OFF_GCM_S2C;
        km->chacha_key_c2s = k + SSH_OFF_CHACHA_C2S;
        km->chacha_key_s2c = k + SSH_OFF_CHACHA_S2C;
        km->mac_key_c2s = k + SSH_OFF_MAC_C2S;
        km->mac_key_s2c = k + SSH_OFF_MAC_S2C;
        km->aes_key_c2s = k + SSH_OFF_AES_KEY_C2S;
        km->aes_key_s2c = k + SSH_OFF_AES_KEY_S2C;
        km->aes_iv_c2s = k + SSH_OFF_AES_IV_C2S;
        km->aes_iv_s2c = k + SSH_OFF_AES_IV_S2C;
        km->active = PROTO_FALSE;
    }

    SshDhState *dh = &ssh_dh[i];
    dh->y = (protocore_bignum *)(base + SSH_OFF_DH_Y);
    dh->f = (protocore_bignum *)(base + SSH_OFF_DH_F);
    dh->K = (protocore_bignum *)(base + SSH_OFF_DH_K);
}

// ---------------------------------------------------------------------------
// Identification string exchange (RFC 4253 §4.2)
// ---------------------------------------------------------------------------

void ssh_transport_send_ident(uint8_t *restrict work)
{
    (void)work;
    const uint8_t i = SshTransport.slot;
    uint8_t *out = SshTransport.out_args.out;
    size_t *out_len = &SshTransport.out_args.out_len;
    const size_t cap = SshTransport.out_args.cap;
    if (i >= MAX_SSH_CONNS)
    {
        SshTransport.i32 = -1;
        return;
    }
    SshSession *s = &ssh_sess[i];

    // Which string is ours, and which of the two H terms holds it.
    const char *ours = SSH_SERVER_VERSION;
    size_t vlen = sizeof(SSH_SERVER_VERSION) - 1;
    char *kept = s->v_s;
    uint16_t *kept_len = &s->v_s_len;
    if (ssh_pkt[i].is_client)
    {
        ours = SSH_CLIENT_VERSION;
        vlen = sizeof(SSH_CLIENT_VERSION) - 1;
        kept = s->v_c;
        kept_len = &s->v_c_len;
    }

    if (vlen + 2 > cap)
    {
        SshTransport.i32 = -1;
        return;
    }
    mem.cpy(out, ours, vlen);
    out[vlen] = '\r';
    out[vlen + 1] = '\n';
    *out_len = vlen + 2;

    mem.cpy(kept, ours, vlen);
    kept[vlen] = '\0';
    *kept_len = (uint16_t)vlen;
    SshTransport.i32 = 0;
    return;
}

// SSH-protoversion-softwareversion: the protoversion runs from past "SSH-" to the next '-'.
// RFC 4253 sec 4.2 fixes it at "2.0"; sec 5.1 makes a server's "1.99" identical to "2.0" here.
static proto_bool ident_protoversion_ok(const uint8_t *line, uint16_t n)
{
    uint16_t p = 4;
    while (p < n && line[p] != '-')
    {
        p++;
    }
    if (p >= n)
    {
        return PROTO_FALSE; // no '-' closing the protoversion
    }
    const uint16_t vlen = (uint16_t)(p - 4);
    return (vlen == 3 && mem.cmp(line + 4, "2.0", 3) == 0) || (vlen == 4 && mem.cmp(line + 4, "1.99", 4) == 0);
}

void ssh_transport_recv_ident(uint8_t *restrict work)
{
    (void)work;
    const uint8_t i = SshTransport.slot;
    const uint8_t *data = SshTransport.pkt.data;
    const size_t len = SshTransport.pkt.len;
    size_t *consumed = &SshTransport.pkt.consumed;
    if (i >= MAX_SSH_CONNS)
    {
        SshTransport.i32 = -1;
        return;
    }
    SshSession *s = &ssh_sess[i];

    // The peer's string is the H term this role does not send.
    char *theirs = s->v_c;
    uint16_t *theirs_len = &s->v_c_len;
    if (ssh_pkt[i].is_client)
    {
        theirs = s->v_s;
        theirs_len = &s->v_s_len;
    }

    size_t k = 0;
    while (k < len)
    {
        uint8_t c = data[k++];
        // RFC 4253 sec 4.2: "The null character MUST NOT be sent."
        if (c == '\0')
        {
            SshTransport.i32 = -1;
            return;
        }
        if (c == '\n')
        {
            // End of a line. Strip a trailing CR if present.
            uint16_t n = s->ident_len;
            if (n > 0 && s->ident_buf[n - 1] == '\r')
            {
                n--;
            }

            if (n >= 4 && mem.cmp(s->ident_buf, "SSH-", 4) == 0)
            {
                if (n > SSH_VERSION_CONTENT_MAX || !ident_protoversion_ok(s->ident_buf, n))
                {
                    SshTransport.i32 = -1;
                    return;
                }
                mem.cpy(theirs, s->ident_buf, n);
                theirs[n] = '\0';
                *theirs_len = n;
                s->ident_len = 0;
                PhaseMachine.ident_done_args.i = i;
                PhaseMachine.ident_done(phase_machine_work);
                *consumed = k;
                SshTransport.i32 = 1;
                return;
            }
            // RFC 4253 sec 4.2 lets the server send other lines before its identification string
            // and requires the client to process them, so only the client skips a non-SSH line.
            if (!ssh_pkt[i].is_client)
            {
                SshTransport.i32 = -1;
                return;
            }
            s->ident_len = 0;
            continue;
        }

        if (s->ident_len >= SSH_VERSION_MAX)
        {
            SshTransport.i32 = -1;
            return; // line too long
        }
        s->ident_buf[s->ident_len++] = c;
    }

    *consumed = k;
    SshTransport.i32 = 0;
    return; // need more data
}

// ---------------------------------------------------------------------------
// KEXINIT (RFC 4253 §7.1)
// ---------------------------------------------------------------------------

void ssh_kexinit_build(uint8_t *restrict work)
{
    (void)work;
    const uint8_t i = SshTransport.slot;
    uint8_t *payload = SshTransport.out_args.out;
    size_t *len = &SshTransport.out_args.out_len;
    const size_t cap = SshTransport.out_args.cap;
    if (i >= MAX_SSH_CONNS)
    {
        SshTransport.i32 = -1;
        return;
    }
    SshSession *s = &ssh_sess[i];
    const proto_bool as_client = ssh_pkt[i].is_client;

    protocore_span w = span.from(payload, cap);
    bytes.put(&w, SSH_MSG_KEXINIT);

    uint8_t cookie[16];
    Rng.fill_args.out = cookie;
    Rng.fill_args.len = sizeof(cookie);
    Rng.fill(protocore_rng_span());
    bytes.raw(&w, cookie, sizeof(cookie));

    char kexlist[SSH_KEXLIST_BUF];
    // All four host-key algs = "ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-512,rsa-sha2-256" is 57
    // chars + NUL; a smaller buffer silently drops rsa-sha2-256 when all three key types are loaded.
    char hklist[SSH_HKLIST_BUF];
    build_kex_list(kexlist, sizeof(kexlist), as_client);
    build_hostkey_list(hklist, sizeof(hklist), as_client);
    protocore_ssh_wr_cstr(&w, kexlist);         // kex_algorithms (preference-ordered, + ext-info-s)
    protocore_ssh_wr_cstr(&w, hklist);          // server_host_key_algorithms (only keys we hold)
    protocore_ssh_wr_cstr(&w, ALG_CIPHER_LIST); // encryption c2s (chacha20-poly1305 preferred, aes256-ctr fallback)
    protocore_ssh_wr_cstr(&w, ALG_CIPHER_LIST); // encryption s2c
    protocore_ssh_wr_cstr(&w, ALG_MAC_LIST);    // mac c2s (used only with aes256-ctr; ignored for the AEAD cipher)
    protocore_ssh_wr_cstr(&w, ALG_MAC_LIST);    // mac s2c
    protocore_ssh_wr_cstr(&w, ALG_COMP_ZLIB);   // compression c2s (zlib@openssh.com / zlib when built in, else none)
    protocore_ssh_wr_cstr(&w, ALG_COMP_ZLIB);   // compression s2c (zlib@openssh.com / zlib when built in, else none)
    protocore_ssh_wr_cstr(&w, "");              // languages c2s
    protocore_ssh_wr_cstr(&w, "");              // languages s2c
    bytes.put(&w, 0);                           // first_kex_packet_follows = false
    bytes.put_be(&w, 0, 4);                     // reserved

    if (!span.ok(w))
    {
        SshTransport.i32 = -1;
        return;
    }

    // Retain a copy for the exchange hash: ours is I_C as the client, I_S as the server.
    uint8_t *kept = s->i_s;
    uint16_t *kept_len = &s->i_s_len;
    size_t kept_cap = PROTOCORE_SSH_I_S_MAX;
    if (as_client)
    {
        kept = s->i_c;
        kept_len = &s->i_c_len;
        kept_cap = PROTOCORE_SSH_I_C_MAX;
    }
    if (w.pos > kept_cap)
    {
        SshTransport.i32 = -1;
        return;
    }
    mem.cpy(kept, payload, w.pos);
    *kept_len = (uint16_t)w.pos;

    *len = w.pos;
    SshTransport.i32 = 0;
    return;
}

// A KEXINIT name-list is an RFC 4251 sec 5 string holding comma-separated names, so reading one is
// bytes.rd_str(); what makes it a name-list is what negotiate_alg()/namelist_contains() do with it.

// Negotiate the key-exchange method from the client's kex_algorithms name-list, in our preference order
// (PQC hybrid first when enabled; RSA group first when prefer_rsa). false = no mutual method.
static int negotiate_kex(const uint8_t *list, uint32_t nlen, SshKexAlg *out, proto_bool as_client)
{
    AlgCand kc[6];
    int nk = 0;
#if PROTOCORE_ENABLE_PQC_KEX
    kc[nk++] = (AlgCand){KEX_MLKEM768, SSH_KEX_MLKEM768_X25519, PROTO_TRUE}; // hybrid first (PQC-preferred)
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
    kc[nk++] = (AlgCand){KEX_SNTRUP761, SSH_KEX_SNTRUP761_X25519, PROTO_TRUE}; // OpenSSH's other PQC default
#endif
    if (SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->prefer_rsa)
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
    int idx = negotiate_alg(list, nlen, kc, nk, &tag, as_client);
    if (idx < 0)
    {
        return -1;
    }
    *out = tag;
    return idx;
}

// Negotiate the host-key algorithm, restricted to keys we actually hold. rsa-sha2-512/256 share the one
// RSA key (RFC 8332), ecdsa-sha2-nistp256 is a distinct P-256 key. false = no mutual algorithm.
static int negotiate_hostkey(const uint8_t *list, uint32_t nlen, SshHostkeyAlg *out, proto_bool as_client)
{
    proto_bool rsa = hostkey_rsa_available();
    proto_bool ed = protocore_ssh_hostkey_ed25519_available();
    proto_bool ec = protocore_ssh_hostkey_ecdsa_available();
    if (as_client)
    {
        rsa = PROTO_TRUE;
        ed = PROTO_TRUE;
        ec = PROTO_TRUE;
    }
    AlgCand hc[4];
    if (SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->prefer_rsa)
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
    int idx = negotiate_alg(list, nlen, hc, 4, &tag, as_client);
    if (idx < 0)
    {
        return -1;
    }
    *out = tag;
    return idx;
}

void ssh_kexinit_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t i = SshTransport.slot;
    const uint8_t *payload = SshTransport.pkt.payload;
    const size_t len = SshTransport.pkt.len;
    if (i >= MAX_SSH_CONNS)
    {
        SshTransport.i32 = -1;
        return;
    }
    SshSession *s = &ssh_sess[i];

    if (len < 1 + 16 || payload[0] != SSH_MSG_KEXINIT)
    {
        SshTransport.i32 = -1;
        return;
    }

    // RFC 4253 sec 9: an exchange starts only when one is not already running. Between our KEXINIT
    // and NEWKEYS one is, and before the identification strings none can, so a KEXINIT arriving in
    // those phases is refused instead of discarding the state in flight.
    PhaseMachine.admits_kexinit_args.i = i;
    PhaseMachine.admits_kexinit(phase_machine_work);
    if (!PhaseMachine.ok)
    {
        SshTransport.i32 = -1;
        return;
    }
    ssh_sess[i].kex_active = PROTO_TRUE; // an exchange is running from here to NEWKEYS
    const proto_bool as_client = ssh_pkt[i].is_client;

    // Retain a copy as I_C for the exchange hash.
    uint8_t *peer = s->i_c;
    uint16_t *peer_len = &s->i_c_len;
    size_t peer_cap = PROTOCORE_SSH_I_C_MAX;
    if (as_client)
    {
        peer = s->i_s;
        peer_len = &s->i_s_len;
        peer_cap = PROTOCORE_SSH_I_S_MAX;
    }
    if (len > peer_cap)
    {
        SshTransport.i32 = -1;
        return;
    }
    mem.cpy(peer, payload, len);
    *peer_len = (uint16_t)len;

    size_t off = 1 + 16; // skip msg type + 16-byte cookie

    const uint8_t *list;
    uint32_t nlen;

    // kex_algorithms: negotiate the KEX method by the CLIENT's preference (RFC 4253 §7.1).
    if (!bytes.rd_str(payload, len, &off, &list, &nlen))
    {
        SshTransport.i32 = -1;
        return;
    }
    // RFC 8308 sec 2.2: "If a server receives an 'ext-info-c', or a client receives an
    // 'ext-info-s', it MAY send an SSH_MSG_EXT_INFO message." The indicator is the peer's role's,
    // which is the opposite of this end's.
    Extension.info_indicator_args.client_role = !as_client;
    Extension.info_indicator(extension_work);
    s->ext_info_enabled = namelist_contains(list, nlen, Extension.text);
    // sec 7.1: "The first algorithm in each name-list MUST be the preferred (guessed) algorithm."
    const uint8_t *peer_kex_first = list;
    const uint32_t peer_kex_first_len = namelist_first_len(list, nlen);
    const int kex_idx = negotiate_kex(list, nlen, &s->kex_alg, as_client);
    if (kex_idx < 0)
    {
        SshTransport.i32 = -1;
        return; // no mutual KEX
    }
    // server_host_key_algorithms: negotiate, restricted to keys we actually hold.
    if (!bytes.rd_str(payload, len, &off, &list, &nlen))
    {
        SshTransport.i32 = -1;
        return;
    }
    const uint8_t *peer_hk_first = list;
    const uint32_t peer_hk_first_len = namelist_first_len(list, nlen);
    const int hostkey_idx = negotiate_hostkey(list, nlen, &s->hostkey_alg, as_client);
    if (hostkey_idx < 0)
    {
        SshTransport.i32 = -1;
        return; // no mutual host-key algorithm
    }
    // encryption c2s / s2c: negotiate chacha20-poly1305@openssh.com or aes256-gcm@openssh.com (both
    // AEADs) or aes256-ctr, in that preference order.
    const AlgCand cc[3] = {{"chacha20-poly1305@openssh.com", SSH_CIPHER_CHACHA20POLY1305, PROTO_TRUE},
                           {ALG_CIPHER_GCM, SSH_CIPHER_AES256GCM, PROTO_TRUE},
                           {ALG_CIPHER, SSH_CIPHER_AES256CTR, PROTO_TRUE}};
    if (!bytes.rd_str(payload, len, &off, &list, &nlen))
    {
        SshTransport.i32 = -1;
        return;
    }
    int c2s = 0;
    int s2c = 0;
    if (negotiate_alg(list, nlen, cc, 3, &c2s, as_client) < 0)
    {
        SshTransport.i32 = -1;
        return;
    }
    if (!bytes.rd_str(payload, len, &off, &list, &nlen))
    {
        SshTransport.i32 = -1;
        return;
    }
    if (negotiate_alg(list, nlen, cc, 3, &s2c, as_client) < 0)
    {
        SshTransport.i32 = -1;
        return;
    }
    s->cipher_alg_c2s = (uint8_t)c2s;
    s->cipher_alg_s2c = (uint8_t)s2c;
    // mac c2s / s2c: negotiated only for aes256-ctr (both AEAD ciphers carry their own MAC). Prefer
    // the encrypt-then-MAC variants (OpenSSH's default), require the same MAC both directions.
    const AlgCand mc[4] = {{"hmac-sha2-256-etm@openssh.com", SSH_MAC_HMAC_SHA256_ETM, PROTO_TRUE},
                           {"hmac-sha2-512-etm@openssh.com", SSH_MAC_HMAC_SHA512_ETM, PROTO_TRUE},
                           {ALG_MAC, SSH_MAC_HMAC_SHA256, PROTO_TRUE},
                           {"hmac-sha2-512", SSH_MAC_HMAC_SHA512, PROTO_TRUE}};
    // A MAC is negotiated only for the direction that runs aes256-ctr; both AEAD ciphers carry their own.
    proto_bool need_mac_c2s = (s->cipher_alg_c2s == SSH_CIPHER_AES256CTR);
    proto_bool need_mac_s2c = (s->cipher_alg_s2c == SSH_CIPHER_AES256CTR);
    int m_c2s = SSH_MAC_HMAC_SHA256;
    int m_s2c = SSH_MAC_HMAC_SHA256;
    if (!bytes.rd_str(payload, len, &off, &list, &nlen))
    {
        SshTransport.i32 = -1;
        return;
    }
    if (need_mac_c2s && negotiate_alg(list, nlen, mc, 4, &m_c2s, as_client) < 0)
    {
        SshTransport.i32 = -1;
        return;
    }
    if (!bytes.rd_str(payload, len, &off, &list, &nlen))
    {
        SshTransport.i32 = -1;
        return;
    }
    if (need_mac_s2c && negotiate_alg(list, nlen, mc, 4, &m_s2c, as_client) < 0)
    {
        SshTransport.i32 = -1;
        return;
    }
    s->mac_alg_c2s = (uint8_t)m_c2s;
    s->mac_alg_s2c = (uint8_t)m_s2c;
    // compression c2s + s2c: negotiate zlib@openssh.com > zlib > none per direction. c2s: the client
    // compresses, we inflate (ssh_inflate); s2c: we compress, the client inflates (ssh_zlib).
#if PROTOCORE_ENABLE_SSH_ZLIB
    {
        const AlgCand compc[3] = {{"zlib@openssh.com", SSH_COMP_ZLIB_DELAYED, PROTO_TRUE},
                                  {"zlib", SSH_COMP_ZLIB, PROTO_TRUE},
                                  {"none", SSH_COMP_NONE, PROTO_TRUE}};
        int comp = 0;
        if (!bytes.rd_str(payload, len, &off, &list, &nlen) ||
            negotiate_alg(list, nlen, compc, 3, &comp, as_client) < 0)
        {
            SshTransport.i32 = -1;
            return;
        }
        Comp.set_c2s_args.i = i;
        Comp.set_c2s_args.alg = comp;
        Comp.set_c2s(comp_work);
        if (!bytes.rd_str(payload, len, &off, &list, &nlen) ||
            negotiate_alg(list, nlen, compc, 3, &comp, as_client) < 0)
        {
            SshTransport.i32 = -1;
            return;
        }
        Comp.set_s2c_args.i = i;
        Comp.set_s2c_args.alg = comp;
        Comp.set_s2c(comp_work);
    }
#else
    // Both directions must offer "none" (no compression built in).
    if (!bytes.rd_str(payload, len, &off, &list, &nlen) || !namelist_contains(list, nlen, ALG_COMP))
    {
        SshTransport.i32 = -1;
        return;
    }
    if (!bytes.rd_str(payload, len, &off, &list, &nlen) || !namelist_contains(list, nlen, ALG_COMP))
    {
        SshTransport.i32 = -1;
        return;
    }
#endif

    // Two language name-lists (RFC 4253 sec 7.1, ignored), then the guess flag and the reserved uint32.
    // Each read advances off, so these are the first list and then the second.
    if (!bytes.rd_str(payload, len, &off, &list, &nlen))
    {
        SshTransport.i32 = -1;
        return;
    }
    if (!bytes.rd_str(payload, len, &off, &list, &nlen))
    {
        SshTransport.i32 = -1;
        return;
    }
    if (off + 1 + 4 > len)
    {
        SshTransport.i32 = -1;
        return;
    }
    // "If both sides make the same guess, that algorithm MUST be used", and "If the other party's
    // guess was wrong, and this field was TRUE, the next packet MUST be silently ignored." The guess
    // is each side's first-listed kex and host-key algorithm, so it held only when the peer's first
    // names are also this end's first names - not merely when negotiation settled on the peer's.
    const proto_bool guessed = payload[off] != 0;
    s->drop_guessed_kex_pkt =
        guessed && !same_guess(peer_kex_first, peer_kex_first_len, peer_hk_first, peer_hk_first_len, as_client);
    (void)kex_idx;
    (void)hostkey_idx;

    PhaseMachine.kexinit_done_args.i = i;
    PhaseMachine.kexinit_done(phase_machine_work);
    SshTransport.i32 = 0;
    return;
}
// ---------------------------------------------------------------------------
// Exchange hash H (RFC 4253 §8) - streamed into the negotiated KEX hash (SHA-256 or SHA-512 via
// SshKexHash), no large buffer.
// ---------------------------------------------------------------------------

// The exchange hash runs in the slot's crypto_work, which spans PROTOCORE_CRYPTO_BORROW_MAX.
static_assert(PROTOCORE_SSH_KEXHASH_BORROW <= PROTOCORE_CRYPTO_BORROW_MAX,
              "the slot's crypto_work is short of one exchange-hash digest - raise "
              "PROTOCORE_CRYPTO_BORROW_MAX in protocore_config.h");

// Absorb @p len bytes of @p data into the digest running in @p work.
static void hash_bytes(uint8_t *work, const uint8_t *data, size_t len)
{
    SshKexHash.update_args.data = data;
    SshKexHash.update_args.len = len;
    SshKexHash.update(work);
}

// Hash a 4-byte big-endian length prefix.
static void hash_u32(uint8_t *work, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    hash_bytes(work, b, 4);
}

// Hash an SSH string: uint32 length + raw bytes.
static void hash_string(uint8_t *work, const uint8_t *data, size_t len)
{
    hash_u32(work, (uint32_t)len);
    hash_bytes(work, data, len);
}

// Hash an SSH mpint from a fixed-width big-endian integer: strip leading zero
// bytes, prepend a 0x00 if the top bit is set (to keep it positive).
static void hash_mpint(uint8_t *work, const uint8_t *be, size_t len)
{
    size_t off = 0;
    while (off < len && be[off] == 0)
    {
        off++;
    }
    if (off == len)
    {
        // Value is zero → mpint is an empty string.
        hash_u32(work, 0);
        return;
    }
    proto_bool pad = (be[off] & 0x80u) != 0;
    uint32_t mlen = (uint32_t)(len - off);
    if (pad)
    {
        mlen++;
    }
    hash_u32(work, mlen);
    if (pad)
    {
        uint8_t zero = 0;
        hash_bytes(work, &zero, 1);
    }
    hash_bytes(work, be + off, len - off);
}

proto_bool ssh_kex_is_sha512(SshKexAlg a)
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
void ssh_kex_exchange_hash(uint8_t *restrict work)
{
    (void)work;
    const uint8_t i = SshTransport.slot;
    const proto_bool pub_is_string = SshTransport.kexhash.pub_is_string;
    const uint8_t *cpub = SshTransport.kexhash.cpub;
    const size_t cpub_len = SshTransport.kexhash.cpub_len;
    const uint8_t *spub = SshTransport.kexhash.spub;
    const size_t spub_len = SshTransport.kexhash.spub_len;
    const uint8_t *k_be = SshTransport.kexhash.k_be;
    const size_t k_len = SshTransport.kexhash.k_len;
    const uint8_t *ks = SshTransport.kexhash.ks;
    const size_t ks_len = SshTransport.kexhash.ks_len;
    uint8_t *out = SshTransport.kexhash.hash;
    size_t *out_len = &SshTransport.kexhash.hash_len;
    const proto_bool k_is_string = SshTransport.kexhash.k_is_string;
    const proto_bool is512 = SshTransport.kexhash.is512;
    if (i >= MAX_SSH_CONNS)
    {
        SshTransport.i32 = -1;
        return;
    }
    SshSession *s = &ssh_sess[i];
    if (!ssh_pkt_slot_storage(&ssh_pkt[i]))
    {
        SshTransport.i32 = -1;
        return;
    }

    uint8_t *hash_work = ssh_pkt[i].crypto_work;
    SshKexHash.init_args.is512 = is512;
    SshKexHash.init(hash_work);
    hash_string(hash_work, (const uint8_t *)s->v_c, s->v_c_len); // V_C
    hash_string(hash_work, (const uint8_t *)s->v_s, s->v_s_len); // V_S
    hash_string(hash_work, s->i_c, s->i_c_len);                  // I_C
    hash_string(hash_work, s->i_s, s->i_s_len);                  // I_S
    hash_string(hash_work, ks, ks_len);                          // K_S
    if (pub_is_string)
    {
        hash_string(hash_work, cpub, cpub_len); // Q_C
        hash_string(hash_work, spub, spub_len); // Q_S
    }
    else
    {
        hash_mpint(hash_work, cpub, cpub_len); // e
        hash_mpint(hash_work, spub, spub_len); // f
    }
    if (k_is_string)
    {
        hash_string(hash_work, k_be, k_len); // hybrid: K is a fixed-length HASH output (RFC 4251 string)
    }
    else
    {
        hash_mpint(hash_work, k_be, k_len); // classical: K is an mpint
    }
    SshKexHash.final_args.out = out;
    SshKexHash.final(hash_work);
    *out_len = SshKexHash.len;
    SshTransport.i32 = 0;
    return;
}

// ---------------------------------------------------------------------------
// Host key validator (RFC 4253 §6.6 key and signature encodings)
// ---------------------------------------------------------------------------

// Parse an "ssh-rsa" public-key blob: string("ssh-rsa") mpint(e) mpint(n).
static proto_bool parse_rsa_pubkey(const uint8_t *blob, uint32_t blen, uint8_t *n_be, uint8_t *e_be)
{
    size_t off = 0;
    const uint8_t *type;
    uint32_t type_len;
    if (!bytes.rd_str(blob, blen, &off, &type, &type_len))
    {
        return PROTO_FALSE;
    }
    if (type_len != 7 || mem.cmp(type, "ssh-rsa", 7) != 0)
    {
        return PROTO_FALSE;
    }
    const uint8_t *e_mp;
    uint32_t e_len;
    if (!bytes.rd_str(blob, blen, &off, &e_mp, &e_len) || !bytes.mpint_fixed(e_mp, e_len, e_be, 4))
    {
        return PROTO_FALSE;
    }
    const uint8_t *n_mp;
    uint32_t n_len;
    if (!bytes.rd_str(blob, blen, &off, &n_mp, &n_len) ||
        !bytes.mpint_fixed(n_mp, n_len, n_be, PROTOCORE_RSA_KEY_BYTES))
    {
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

// Parse an "ssh-ed25519" public-key blob: string("ssh-ed25519") string(pub32). (RFC 8709 §4)
static proto_bool parse_ed25519_pubkey(const uint8_t *blob, uint32_t blen, uint8_t *pub)
{
    size_t off = 0;
    const uint8_t *type;
    uint32_t type_len;
    if (!bytes.rd_str(blob, blen, &off, &type, &type_len))
    {
        return PROTO_FALSE;
    }
    if (type_len != 11 || mem.cmp(type, "ssh-ed25519", 11) != 0)
    {
        return PROTO_FALSE;
    }
    const uint8_t *pk;
    uint32_t pk_len;
    if (!bytes.rd_str(blob, blen, &off, &pk, &pk_len) || pk_len != 32)
    {
        return PROTO_FALSE;
    }
    mem.cpy(pub, pk, 32);
    return PROTO_TRUE;
}

// Parse an "ecdsa-sha2-nistp256" public-key blob (RFC 5656 §3.1):
//   string("ecdsa-sha2-nistp256") string("nistp256") string(Q = 0x04||X||Y, 65 bytes).
static proto_bool parse_ecdsa_pubkey(const uint8_t *blob, uint32_t blen, uint8_t *pub)
{
    size_t off = 0;
    const uint8_t *type;
    uint32_t type_len;
    if (!bytes.rd_str(blob, blen, &off, &type, &type_len))
    {
        return PROTO_FALSE;
    }
    if (type_len != 19 || mem.cmp(type, "ecdsa-sha2-nistp256", 19) != 0)
    {
        return PROTO_FALSE;
    }
    const uint8_t *curve;
    uint32_t curve_len;
    if (!bytes.rd_str(blob, blen, &off, &curve, &curve_len))
    {
        return PROTO_FALSE;
    }
    if (curve_len != 8 || mem.cmp(curve, "nistp256", 8) != 0)
    {
        return PROTO_FALSE;
    }
    const uint8_t *q;
    uint32_t q_len;
    if (!bytes.rd_str(blob, blen, &off, &q, &q_len))
    {
        return PROTO_FALSE;
    }
    if (q_len != PROTOCORE_ECDSA_P256_PUB_LEN || q[0] != 0x04) // uncompressed point only
    {
        return PROTO_FALSE;
    }
    mem.cpy(pub, q, PROTOCORE_ECDSA_P256_PUB_LEN);
    return PROTO_TRUE;
}

// Parse an ECDSA signature blob (RFC 5656 §3.1.2): mpint(r) || mpint(s) -> raw r || s (32 + 32).
static proto_bool parse_ecdsa_sig(const uint8_t *sig, uint32_t slen, uint8_t *out)
{
    size_t off = 0;
    const uint8_t *r;
    const uint8_t *s;
    uint32_t r_len;
    uint32_t s_len;
    if (!bytes.rd_str(sig, slen, &off, &r, &r_len) || !bytes.rd_str(sig, slen, &off, &s, &s_len))
    {
        return PROTO_FALSE;
    }
    return bytes.mpint_fixed(r, r_len, out, PROTOCORE_ECDSA_P256_COORD_LEN) &&
           bytes.mpint_fixed(s, s_len, out + PROTOCORE_ECDSA_P256_COORD_LEN, PROTOCORE_ECDSA_P256_COORD_LEN);
}

proto_bool ssh_hostkey_verify(uint8_t i, const uint8_t *ks, size_t ks_len, const uint8_t *sig, size_t sig_len,
                              const uint8_t *h, size_t h_len)
{
    if (i >= MAX_SSH_CONNS || !ssh_pkt_slot_storage(&ssh_pkt[i]))
    {
        return PROTO_FALSE;
    }
    // The signature is string(algorithm) || string(raw) (RFC 4253 §6.6); the raw bytes are what the
    // algorithm verifies.
    size_t soff = 0;
    const uint8_t *stype;
    uint32_t stype_len;
    const uint8_t *raw;
    uint32_t raw_len;
    if (!bytes.rd_str(sig, (uint32_t)sig_len, &soff, &stype, &stype_len) ||
        !bytes.rd_str(sig, (uint32_t)sig_len, &soff, &raw, &raw_len))
    {
        return PROTO_FALSE;
    }

    // The key material is staged in the arena rather than on the worker stack: this runs at the
    // deepest point of the handshake, under the curve and bignum paths.
    size_t mark = protocore_plaintext_mark();
    protocore_span n_be = protocore_plaintext_span(PROTOCORE_RSA_KEY_BYTES, 4);
    protocore_span e_be = protocore_plaintext_span(4, 4);
    protocore_span pub = protocore_plaintext_span(PROTOCORE_ECDSA_P256_PUB_LEN, 4);
    protocore_span ec_sig = protocore_plaintext_span(PROTOCORE_ECDSA_P256_SIG_LEN, 4);
    if (!span.ok(n_be) || !span.ok(e_be) || !span.ok(pub) || !span.ok(ec_sig))
    {
        protocore_plaintext_release(mark);
        return PROTO_FALSE;
    }

    uint8_t *work = ssh_pkt[i].crypto_work;
    proto_bool ok = PROTO_FALSE;
    switch (ssh_sess[i].hostkey_alg)
    {
    case SSH_HOSTKEY_ED25519:
        ok = parse_ed25519_pubkey(ks, (uint32_t)ks_len, pub.buf) && raw_len == 64 &&
             (Ed25519.verify_args.pub = pub.buf, Ed25519.verify_args.msg = h, Ed25519.verify_args.msg_len = h_len,
              Ed25519.verify_args.sig = raw, Ed25519.verify(work), Ed25519.ok);
        break;
    case SSH_HOSTKEY_ECDSA_NISTP256:
        ok = parse_ecdsa_pubkey(ks, (uint32_t)ks_len, pub.buf) && parse_ecdsa_sig(raw, raw_len, ec_sig.buf) &&
             (Ecdsa.verify_args.pub = pub.buf, Ecdsa.verify_args.msg = h, Ecdsa.verify_args.mlen = h_len,
              Ecdsa.verify_args.sig = ec_sig.buf, Ecdsa.verify(work), Ecdsa.ok);
        break;
    case SSH_HOSTKEY_RSA_SHA256:
    case SSH_HOSTKEY_RSA_SHA512: {
        // RFC 8332: the signature hash comes from the negotiated algorithm name, not the key blob.
        protocore_rsa_hash rh = PROTOCORE_RSA_HASH_SHA256;
        if (ssh_sess[i].hostkey_alg == SSH_HOSTKEY_RSA_SHA512)
        {
            rh = PROTOCORE_RSA_HASH_SHA512;
        }
        ok = parse_rsa_pubkey(ks, (uint32_t)ks_len, n_be.buf, e_be.buf) &&
             (Rsa.verify_args.n = n_be.buf, Rsa.verify_args.e = e_be.buf, Rsa.verify_args.msg = h,
              Rsa.verify_args.msg_len = h_len, Rsa.verify_args.sig = raw, Rsa.verify_args.sig_len = raw_len,
              Rsa.verify_args.hash = rh, Rsa.verify(work), Rsa.ok);
        break;
    }
    }
    protocore_plaintext_release(mark);
    return ok;
}

// ---------------------------------------------------------------------------
// KEXDH (RFC 4253 §8)
// ---------------------------------------------------------------------------

int ssh_kexdh_parse_init(const uint8_t *payload, size_t len, uint8_t e_be[256])
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

    mem.set(e_be, 0, 256);
    mem.cpy(e_be + (256 - vlen), m + off, vlen);
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
    mem.cpy(qc, payload + 5, 32);
    return 0;
}

// Parse SSH_MSG_KEX_ECDH_INIT for ecdh-sha2-nistp256 (RFC 5656 §4): byte(30) || string(Q_C),
// where Q_C is the 65-byte uncompressed client point 0x04 || X || Y.
static int parse_ecdh_init_p256(const uint8_t *payload, size_t len, uint8_t qc[PROTOCORE_ECDSA_P256_PUB_LEN])
{
    if (len < 1 + 4 || payload[0] != SSH_MSG_KEXDH_INIT)
    {
        return -1;
    }
    uint32_t n = ((uint32_t)payload[1] << 24) | ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 8) |
                 (uint32_t)payload[4];
    if (n != PROTOCORE_ECDSA_P256_PUB_LEN || (size_t)5 + n > len)
    {
        return -1;
    }
    mem.cpy(qc, payload + 5, PROTOCORE_ECDSA_P256_PUB_LEN);
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
        protocore_span w = span.from(ks, cap);
        protocore_ssh_wr_str(&w, (const uint8_t *)HOSTKEY_ED, sizeof(HOSTKEY_ED) - 1);
        protocore_ssh_wr_str(&w, SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ed_pub, 32);
        if (!span.ok(w))
        {
            return -1;
        }
        *ks_len = w.pos;
        return 0;
    }
    if (ssh_sess[i].hostkey_alg == SSH_HOSTKEY_ECDSA_NISTP256)
    {
        protocore_span w = span.from(ks, cap);
        protocore_ssh_wr_str(&w, (const uint8_t *)HOSTKEY_ECDSA, sizeof(HOSTKEY_ECDSA) - 1);
        protocore_ssh_wr_str(&w, (const uint8_t *)"nistp256", 8); // RFC 5656 curve identifier
        protocore_ssh_wr_str(&w, SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ecdsa_pub,
                             PROTOCORE_ECDSA_P256_PUB_LEN);
        if (!span.ok(w))
        {
            return -1;
        }
        *ks_len = w.pos;
        return 0;
    }
    SshRsa.encode_pubkey_args.out = ks;
    SshRsa.encode_pubkey_args.out_len = ks_len;
    SshRsa.encode_pubkey_args.out_cap = cap;
    SshRsa.encode_pubkey(protocore_ssh_rsa_span());
    return SshRsa.n;
}

// Sign the exchange hash H with the negotiated host key. Writes the raw signature (no
// SSH framing) plus its algorithm name; the caller wraps it as the signature blob.
static int sign_hash(uint8_t i, const uint8_t *H, size_t h_len, uint8_t *sig, size_t *sig_len, size_t sig_cap,
                     const char **sig_name)
{
    if (ssh_sess[i].hostkey_alg == SSH_HOSTKEY_ED25519)
    {
        if (sig_cap < 64 || !ssh_pkt_slot_storage(&ssh_pkt[i]))
        {
            return -1;
        }
        Ed25519.sign_args.seed = SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ed_seed;
        Ed25519.sign_args.msg = H;
        Ed25519.sign_args.msg_len = h_len;
        Ed25519.sign_args.sig = sig;
        Ed25519.sign(ssh_pkt[i].crypto_work);
        *sig_len = 64;
        *sig_name = HOSTKEY_ED; // "ssh-ed25519"
        return 0;
    }
    if (ssh_sess[i].hostkey_alg == SSH_HOSTKEY_ECDSA_NISTP256)
    {
        uint8_t raw[PROTOCORE_ECDSA_P256_SIG_LEN]; // r || s (32 + 32)
        if (!ssh_pkt_slot_storage(&ssh_pkt[i]))
        {
            return -1;
        }
        Ecdsa.sign_args.msg = H;
        Ecdsa.sign_args.mlen = h_len;
        Ecdsa.sign_args.priv = SSH_TRANSPORT_CTX(protocore_ssh_transport_span())->ecdsa_priv;
        Ecdsa.sign_args.sig = raw;
        Ecdsa.sign(ssh_pkt[i].crypto_work);
        if (!Ecdsa.ok)
        {
            return -1;
        }
        // ECDSA signature blob is mpint(r) || mpint(s) (RFC 5656 §3.1.2).
        protocore_span w = span.from(sig, sig_cap);
        protocore_ssh_wr_mpint(&w, raw, PROTOCORE_ECDSA_P256_COORD_LEN);
        protocore_ssh_wr_mpint(&w, raw + PROTOCORE_ECDSA_P256_COORD_LEN, PROTOCORE_ECDSA_P256_COORD_LEN);
        if (!span.ok(w))
        {
            return -1;
        }
        *sig_len = w.pos;
        *sig_name = HOSTKEY_ECDSA; // "ecdsa-sha2-nistp256"
        return 0;
    }
    // rsa-sha2-512 and rsa-sha2-256 share the one "ssh-rsa" key; the negotiated
    // algorithm only chooses the signature hash (RFC 8332).
    const proto_bool is_sha512 = (ssh_sess[i].hostkey_alg == SSH_HOSTKEY_RSA_SHA512);
    protocore_rsa_hash rh = PROTOCORE_RSA_HASH_SHA256;
    if (is_sha512)
    {
        rh = PROTOCORE_RSA_HASH_SHA512;
    }
    if (!ssh_pkt_slot_storage(&ssh_pkt[i]))
    {
        return -1;
    }
    // Staged inside the guard: as the right operand of `||` the signature must not be computed when
    // the caller's buffer is already too small for it.
    if (sig_cap < PROTOCORE_RSA_SIG_BYTES)
    {
        return -1;
    }
    SshRsa.sign_args.crypto_work = ssh_pkt[i].crypto_work;
    SshRsa.sign_args.msg = H;
    SshRsa.sign_args.msg_len = h_len;
    SshRsa.sign_args.hash = rh;
    SshRsa.sign_args.sig = sig;
    SshRsa.sign(protocore_ssh_rsa_span());
    if (SshRsa.n != 0)
    {
        return -1;
    }
    *sig_len = PROTOCORE_RSA_SIG_BYTES;
    *sig_name = HOSTKEY_RSA_SHA256;
    if (is_sha512)
    {
        *sig_name = HOSTKEY_RSA_SHA512;
    }
    return 0;
}

// Assemble SSH_MSG_KEXDH_REPLY (== KEX_ECDH_REPLY / KEX_HYBRID_REPLY, msg 31):
//   byte(31) || string(K_S) || (mpint f | string Q_S | string S_REPLY) || string( string(sig_name) || string(sig) )
static int build_kex_reply(uint8_t i, const uint8_t *ks, size_t ks_len, const uint8_t *spub, size_t spub_len,
                           const char *sig_name, const uint8_t *sig, size_t sig_len, uint8_t *out, size_t *out_len,
                           size_t cap)
{
    protocore_span w = span.from(out, cap);
    bytes.put(&w, SSH_MSG_KEXDH_REPLY);
    protocore_ssh_wr_str(&w, ks, ks_len); // K_S
    if (ssh_sess[i].kex_alg == SSH_KEX_DH_GROUP14)
    {
        protocore_ssh_wr_mpint(&w, spub, spub_len); // f (mpint)
    }
    else
    {
        protocore_ssh_wr_str(&w, spub, spub_len); // Q_S (curve25519) or S_REPLY (hybrid), a raw string
    }
    uint32_t nl = (uint32_t)str.len(sig_name, w.cap);
    bytes.put_be(&w, 4 + nl + 4 + (uint32_t)sig_len, 4); // signature blob length
    protocore_ssh_wr_str(&w, (const uint8_t *)sig_name, nl);
    protocore_ssh_wr_str(&w, sig, sig_len);
    if (!span.ok(w))
    {
        return -1;
    }
    *out_len = w.pos;
    return 0;
}

void ssh_kex_generate(uint8_t *restrict work)
{
    (void)work;
    const uint8_t i = SshTransport.slot;
    if (i >= MAX_SSH_CONNS)
    {
        SshTransport.i32 = -1;
        return;
    }
    SshKexAlg a = ssh_sess[i].kex_alg;
    proto_bool curve = (a == SSH_KEX_CURVE25519);
#if PROTOCORE_ENABLE_PQC_KEX
    // both PQ/T hybrids run X25519 as the classical half.
    curve = curve || (a == SSH_KEX_MLKEM768_X25519) || (a == SSH_KEX_SNTRUP761_X25519);
#endif
    if (curve)
    {
        // X25519 ephemeral: random 32-byte scalar, public = X25519(scalar, base point).
#ifdef PROTOCORE_SSH_KEX_BENCH
        uint32_t kexgen_t0 = protocore_platform_micros();
#endif
        Rng.fill_args.out = ssh_sess[i].ecdh_sk;
        Rng.fill_args.len = 32;
        Rng.fill(protocore_rng_span());
        Curve25519.x25519_base_args.scalar = ssh_sess[i].ecdh_sk;
        Curve25519.x25519_base_args.out = ssh_sess[i].ecdh_pk;
        Curve25519.x25519_base(ssh_pkt[i].crypto_work);
#ifdef PROTOCORE_SSH_KEX_BENCH
        protocore_ssh_kex_bench.last_kexgen_us = (long long)(protocore_platform_micros() - kexgen_t0);
#endif
        SshTransport.i32 = 0;
        return;
    }
    if (a == SSH_KEX_ECDH_NISTP256)
    {
        // P-256 ECDH ephemeral: a random scalar d in [1, n) stored in ecdh_sk. The 65-byte public
        // point Q_S = d*G is re-derived in ssh_kexdh_handle (avoids a curve-specific session field).
        // Re-draw on the negligible chance a raw 32-byte value is 0 or >= n (an invalid P-256 scalar).
        uint8_t qtmp[PROTOCORE_ECDSA_P256_PUB_LEN];
        for (int t = 0; t < 8; t++)
        { // hands back an invalid P-256 scalar, which no host build can provoke
            Rng.fill_args.out = ssh_sess[i].ecdh_sk;
            Rng.fill_args.len = 32;
            Rng.fill(protocore_rng_span());
            Ecdsa.pubkey_args.priv = ssh_sess[i].ecdh_sk;
            Ecdsa.pubkey_args.pub = qtmp;
            Ecdsa.pubkey(ssh_pkt[i].crypto_work);
            if (Ecdsa.ok)
            {
                SshTransport.i32 = 0;
                return; // valid scalar with overwhelming probability
            }
        }
        SshTransport.i32 = -1;
        return;
    }
    SshTransport.i32 = ssh_dh_generate(i);
}

#if PROTOCORE_ENABLE_PQC_KEX
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
    Rng.fill_args.out = m;
    Rng.fill_args.len = sizeof(m);
    Rng.fill(protocore_rng_span());
    uint8_t k_pq[32];
    MlKem.encaps_args.ek = ek;
    MlKem.encaps_args.m = m;
    MlKem.encaps_args.ct = s_reply; // ciphertext -> s_reply[0..1087]
    MlKem.encaps_args.ss = k_pq;
    MlKem.encaps(ssh_pkt[i].crypto_work);
    proto_bool ok = MlKem.ok;
    protocore_secure_wipe(m, sizeof(m));
    if (!ok)
    {
        return -1; // malformed encapsulation key (FIPS 203 modulus check)
    }

    uint8_t k_cl[32];
    Curve25519.x25519_args.scalar = ssh_sess[i].ecdh_sk;
    Curve25519.x25519_args.point = qc;
    Curve25519.x25519_args.out = k_cl;
    Curve25519.x25519(ssh_pkt[i].crypto_work);
    uint8_t zacc = 0;
    for (int b = 0; b < 32; b++)
    {
        zacc |= k_cl[b];
    }
    if (zacc == 0) // low-order X25519 point (RFC 7748 §6.1)
    {
        protocore_secure_wipe(k_pq, sizeof(k_pq));
        protocore_secure_wipe(k_cl, sizeof(k_cl));
        return -1;
    }
    mem.cpy(s_reply + MLKEM768_CT_BYTES, ssh_sess[i].ecdh_pk, 32); // S_PK1: server X25519 public

    if (!ssh_pkt_slot_storage(&ssh_pkt[i]))
    {
        protocore_secure_wipe(k_pq, sizeof(k_pq));
        protocore_secure_wipe(k_cl, sizeof(k_cl));
        return -1;
    }
    uint8_t *hw = ssh_pkt[i].crypto_work;
    Sha256.init(hw);
    Sha256.update_args.data = k_pq; // K = SHA256(K_PQ || K_CL) (RFC 9370 concat combiner)
    Sha256.update_args.len = sizeof(k_pq);
    Sha256.update(hw);
    Sha256.update_args.data = k_cl;
    Sha256.update_args.len = sizeof(k_cl);
    Sha256.update(hw);
    Sha256.final_args.out = k_out;
    Sha256.final(hw);
    protocore_secure_wipe(k_pq, sizeof(k_pq));
    protocore_secure_wipe(k_cl, sizeof(k_cl));
    return 0;
}
#endif // PROTOCORE_ENABLE_PQC_KEX (ML-KEM hybrid)

#if PROTOCORE_ENABLE_SSH_SNTRUP761
// sntrup761x25519-sha512@openssh.com: from C_INIT (byte 30 || string, string = sntrup761_pk(1158) ||
// Q_C(32)), sntrup761-Encaps to the peer's key and X25519 against Q_C, then combine K = SHA512(K_PQ ||
// K_CL) (64 bytes). Writes S_REPLY = ciphertext(1039) || Q_S(32) and the 64-byte shared secret. Returns
// 0, or -1 on a malformed C_INIT or a low-order X25519 point.
static int hybrid_sntrup761_x25519(uint8_t *work, uint8_t i, const uint8_t *payload, size_t len,
                                   uint8_t s_reply[PROTOCORE_SNTRUP761_CT_BYTES + 32], uint8_t k_out[64])
{
    if (len < 1 + 4 || payload[0] != SSH_MSG_KEXDH_INIT)
    {
        return -1;
    }
    uint32_t n = ((uint32_t)payload[1] << 24) | ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 8) |
                 (uint32_t)payload[4];
    if (n != PROTOCORE_SNTRUP761_PK_BYTES + 32 || (size_t)5 + n > len)
    {
        return -1;
    }
    const uint8_t *pk = payload + 5;                                // C_PK2: sntrup761 public key
    const uint8_t *qc = payload + 5 + PROTOCORE_SNTRUP761_PK_BYTES; // C_PK1: client X25519 public

    uint8_t k_pq[PROTOCORE_SNTRUP761_SS_BYTES];
    Sntrup761.enc_args.pk = pk;
    Sntrup761.enc_args.ct = s_reply; // ciphertext -> s_reply[0..1038]
    Sntrup761.enc_args.ss = k_pq;
    Sntrup761.enc(work);

    uint8_t k_cl[32];
    Curve25519.x25519_args.scalar = ssh_sess[i].ecdh_sk;
    Curve25519.x25519_args.point = qc;
    Curve25519.x25519_args.out = k_cl;
    Curve25519.x25519(work);
    uint8_t zacc = 0;
    for (int b = 0; b < 32; b++)
    {
        zacc |= k_cl[b];
    }
    if (zacc == 0) // low-order X25519 point (RFC 7748 §6.1)
    {
        protocore_secure_wipe(k_pq, sizeof(k_pq));
        protocore_secure_wipe(k_cl, sizeof(k_cl));
        return -1;
    }
    mem.cpy(s_reply + PROTOCORE_SNTRUP761_CT_BYTES, ssh_sess[i].ecdh_pk, 32); // S_PK1: server X25519 public

    Sha512.init(work);
    Sha512.update_args.data = k_pq; // K = SHA512(K_PQ || K_CL) (RFC 9370 concat combiner)
    Sha512.update_args.len = sizeof(k_pq);
    Sha512.update(work);
    Sha512.update_args.data = k_cl;
    Sha512.update_args.len = sizeof(k_cl);
    Sha512.update(work);
    Sha512.final_args.out = k_out;
    Sha512.final(work);
    protocore_secure_wipe(k_pq, sizeof(k_pq));
    protocore_secure_wipe(k_cl, sizeof(k_cl));
    return 0;
}
#endif // PROTOCORE_ENABLE_SSH_SNTRUP761

void ssh_kexdh_handle(uint8_t *restrict work)
{
    const uint8_t i = SshTransport.slot;
    const uint8_t *payload = SshTransport.pkt.payload;
    const size_t len = SshTransport.pkt.len;
    uint8_t *reply_out = SshTransport.out_args.out;
    size_t *reply_len = &SshTransport.out_args.out_len;
    const size_t cap = SshTransport.out_args.cap;
    if (i >= MAX_SSH_CONNS)
    {
        SshTransport.i32 = -1;
        return;
    }
    SshSession *s = &ssh_sess[i];
#ifdef PROTOCORE_SSH_KEX_BENCH
    uint32_t kexreply_t0 = protocore_platform_micros();
#endif

    // 1. Shared secret K + the two public values, per negotiated KEX method. cpub_p / spub_p point at
    //    the values hashed into H (local buffers for DH / curve, the larger C_INIT / S_REPLY blobs for
    //    the hybrid); k_hash / k_hash_len select K's encoding (an mpint for DH / curve, a fixed 32-byte
    //    string for the hybrid). k_be holds K right-aligned so hash_mpint / the KDF strip to minimal.
    uint8_t k_be[256];
    mem.set(k_be, 0, sizeof(k_be));
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
#if PROTOCORE_ENABLE_PQC_KEX
    uint8_t s_reply[MLKEM768_CT_BYTES + 32]; // hybrid S_REPLY = ciphertext(1088) || Q_S(32)
#endif

    if (s->kex_alg == SSH_KEX_CURVE25519)
    {
        // curve25519-sha256 (RFC 8731): K = X25519(sk, Q_C); Q_C/Q_S hashed as strings.
        uint8_t qc[32];
        uint8_t kk[32];
        if (parse_ecdh_init(payload, len, qc) != 0)
        {
            SshTransport.i32 = -1;
            return;
        }
        Curve25519.x25519_args.scalar = s->ecdh_sk;
        Curve25519.x25519_args.point = qc;
        Curve25519.x25519_args.out = kk;
        Curve25519.x25519(ssh_pkt[i].crypto_work);
        // Reject a low-order client point (all-zero shared secret) - RFC 7748 §6.1.
        uint8_t zacc = 0;
        for (int b = 0; b < 32; b++)
        {
            zacc |= kk[b];
        }
        if (zacc == 0)
        {
            protocore_secure_wipe(kk, sizeof(kk));
            SshTransport.i32 = -1;
            return;
        }
        mem.cpy(k_be + (256 - 32), kk, 32);
        mem.cpy(cpub, qc, 32);
        mem.cpy(spub, s->ecdh_pk, 32);
        cpub_len = spub_len = 32;
        pub_is_string = PROTO_TRUE;
        protocore_secure_wipe(kk, sizeof(kk));
    }
#if PROTOCORE_ENABLE_PQC_KEX
    else if (s->kex_alg == SSH_KEX_MLKEM768_X25519)
    {
        // mlkem768x25519-sha256: K = SHA256(K_PQ || K_CL); C_INIT / S_REPLY and K hashed as strings.
        if (hybrid_mlkem_x25519(i, payload, len, s_reply, k_be + (256 - 32)) != 0)
        {
            SshTransport.i32 = -1;
            return;
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
#endif // PROTOCORE_ENABLE_PQC_KEX (ML-KEM dispatch)
#if PROTOCORE_ENABLE_SSH_SNTRUP761
    else if (s->kex_alg == SSH_KEX_SNTRUP761_X25519)
    {
        // sntrup761x25519-sha512: K = SHA512(K_PQ || K_CL) (64 bytes); C_INIT / S_REPLY and K are strings.
        if (!ssh_pkt_slot_storage(&ssh_pkt[i]) ||
            hybrid_sntrup761_x25519(ssh_pkt[i].crypto_work, i, payload, len, s_reply, k_be + (256 - 64)) != 0)
        {
            SshTransport.i32 = -1;
            return;
        }
        cpub_p = payload + 5; // C_INIT (sntrup761_pk || Q_C), hashed verbatim as a string
        cpub_len = PROTOCORE_SNTRUP761_PK_BYTES + 32;
        spub_p = s_reply; // S_REPLY (ciphertext || Q_S)
        spub_len = PROTOCORE_SNTRUP761_CT_BYTES + 32;
        k_hash = k_be + (256 - 64); // K is exactly 64 bytes (SHA-512), string-encoded
        k_hash_len = 64;
        pub_is_string = PROTO_TRUE;
        k_is_string = PROTO_TRUE;
    }
#endif // PROTOCORE_ENABLE_SSH_SNTRUP761
    else if (s->kex_alg == SSH_KEX_ECDH_NISTP256)
    {
        // ecdh-sha2-nistp256 (RFC 5656 §4): K = X(d_S * Q_C). Q_C/Q_S are 65-byte point strings; K an mpint.
        uint8_t qc[PROTOCORE_ECDSA_P256_PUB_LEN];
        if (parse_ecdh_init_p256(payload, len, qc) != 0)
        {
            SshTransport.i32 = -1;
            return;
        }
        uint8_t qs[PROTOCORE_ECDSA_P256_PUB_LEN];
        uint8_t kk[PROTOCORE_ECDSA_P256_COORD_LEN];
        // Re-derive our ephemeral public Q_S, then the shared secret. Ecdsa.ecdh validates Q_C is
        // on-curve and the product is not the identity (RFC 5656 §4 point checks).
        uint8_t *ecw = ssh_pkt[i].crypto_work;
        Ecdsa.pubkey_args.priv = s->ecdh_sk;
        Ecdsa.pubkey_args.pub = qs;
        Ecdsa.pubkey(ecw);
        proto_bool ec_ok = Ecdsa.ok;
        Ecdsa.ecdh_args.peer_pub = qc;
        Ecdsa.ecdh_args.priv = s->ecdh_sk;
        Ecdsa.ecdh_args.shared_x = kk;
        Ecdsa.ecdh(ecw);
        if (!ec_ok || !Ecdsa.ok)
        {
            SshTransport.i32 = -1;
            return;
        }
        mem.cpy(k_be + (256 - PROTOCORE_ECDSA_P256_COORD_LEN), kk, PROTOCORE_ECDSA_P256_COORD_LEN);
        mem.cpy(cpub, qc, PROTOCORE_ECDSA_P256_PUB_LEN);
        mem.cpy(spub, qs, PROTOCORE_ECDSA_P256_PUB_LEN);
        cpub_len = PROTOCORE_ECDSA_P256_PUB_LEN;
        spub_len = PROTOCORE_ECDSA_P256_PUB_LEN;
        pub_is_string = PROTO_TRUE;
        protocore_secure_wipe(kk, sizeof(kk));
    }
    else
    {
        // diffie-hellman-group14-sha256 (RFC 4253 §8): K = e^y mod p; e/f are mpints.
        uint8_t e_be[256];
        if (ssh_kexdh_parse_init(payload, len, e_be) != 0)
        {
            SshTransport.i32 = -1;
            return;
        }
        protocore_bignum e;
        Bignum.from_bytes_args.out = &e;
        Bignum.from_bytes_args.bytes = e_be;
        Bignum.from_bytes_args.len = 256;
        Bignum.from_bytes(ssh_pkt[i].crypto_work);
        Bignum.validate_args.v = &e;
        Bignum.dh_validate(ssh_pkt[i].crypto_work);
        if (!Bignum.ok)
        {
            SshTransport.i32 = -1;
            return;
        }
        protocore_bignum K;
        Bignum.expmod_args.out = &K;
        Bignum.expmod_args.base = &e;
        Bignum.expmod_args.exp = ssh_dh[i].y;
        Bignum.expmod_group14(ssh_pkt[i].crypto_work);
        Bignum.to_bytes_args.bytes = k_be;
        Bignum.to_bytes_args.in = &K;
        Bignum.to_bytes(ssh_pkt[i].crypto_work);
        protocore_secure_wipe(&K, sizeof(K));
        mem.cpy(cpub, e_be, 256);
        Bignum.to_bytes_args.bytes = spub;
        Bignum.to_bytes_args.in = ssh_dh[i].f;
        Bignum.to_bytes(ssh_pkt[i].crypto_work);
    }

    // 2. Host-key blob K_S (per negotiated host-key algorithm).
    uint8_t ks[SSH_RSA_PUBKEY_BLOB_MAX];
    size_t ks_len = 0;
    if (encode_hostkey(i, ks, &ks_len, sizeof(ks)) != 0)
    {
        protocore_secure_wipe(k_be, sizeof(k_be));
        SshTransport.i32 = -1;
        return;
    }

    // 3. Exchange hash H (SHA-256 or SHA-512 per the KEX method); capture the session id on first KEX.
    const proto_bool is512 = ssh_kex_is_sha512(s->kex_alg);
    SshTransport.slot = i;
    SshTransport.kexhash.pub_is_string = pub_is_string;
    SshTransport.kexhash.cpub = cpub_p;
    SshTransport.kexhash.cpub_len = cpub_len;
    SshTransport.kexhash.spub = spub_p;
    SshTransport.kexhash.spub_len = spub_len;
    SshTransport.kexhash.k_be = k_hash;
    SshTransport.kexhash.k_len = k_hash_len;
    SshTransport.kexhash.ks = ks;
    SshTransport.kexhash.ks_len = ks_len;
    SshTransport.kexhash.k_is_string = k_is_string;
    SshTransport.kexhash.is512 = is512;
    ssh_kex_exchange_hash(work);
    uint8_t *const H = SshTransport.kexhash.hash;
    const size_t h_len = SshTransport.kexhash.hash_len;
    ssh_session_id_latch(i, H, h_len);

    // 4. Sign H with the negotiated host key (rsa-sha2-512/256 or ssh-ed25519).
    uint8_t sig[PROTOCORE_RSA_SIG_BYTES]; // 256 bytes: fits an RSA-2048 sig and a 64-byte ed25519 sig
    size_t sig_len = 0;
    const char *sig_name = NULL;
    if (sign_hash(i, H, h_len, sig, &sig_len, sizeof(sig), &sig_name) != 0)
    {
        protocore_secure_wipe(k_be, sizeof(k_be));
        SshTransport.i32 = -1;
        return;
    }

    // 5. Assemble the reply, then derive the six session keys (id fixed at first KEX's H).
    if (build_kex_reply(i, ks, ks_len, spub_p, spub_len, sig_name, sig, sig_len, reply_out, reply_len, cap) != 0)
    {
        protocore_secure_wipe(k_be, sizeof(k_be));
        SshTransport.i32 = -1;
        return;
    }
    const SshKdfInputs kdf_in = {.work = ssh_pkt[i].crypto_work,
                                 .K_be = k_be,
                                 .H = H,
                                 .session_id = s->session_id,
                                 .h_len = h_len,
                                 .sid_len = s->session_id_len,
                                 .k_is_string = k_is_string,
                                 .is512 = is512};
    ssh_kex_install_keys(i, &kdf_in);
    protocore_secure_wipe(k_be, sizeof(k_be));

    PhaseMachine.kex_done_args.i = i;
    PhaseMachine.kex_done(phase_machine_work);
#ifdef PROTOCORE_SSH_KEX_BENCH
    protocore_ssh_kex_bench.last_kexreply_us = (long long)(protocore_platform_micros() - kexreply_t0);
    protocore_ssh_kex_bench.kex_count++;
#endif
    SshTransport.i32 = 0;
    return;
}

void ssh_newkeys_sent(uint8_t *restrict work)
{
    (void)work;
    const uint8_t i = SshTransport.slot;
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    // We have emitted our SSH_MSG_NEWKEYS: our outbound direction is now encrypted and reads the
    // epoch this exchange derived (RFC 4253 sec 7.3). Sending it also closes the sec 7.1 window this
    // end opened with its KEXINIT.
    ssh_sess[i].out.epoch = 1u - ssh_sess[i].out.epoch;
    ssh_sess[i].out.enc = PROTO_TRUE;
    ssh_sess[i].kexinit_sent = PROTO_FALSE;
#if PROTOCORE_ENABLE_SSH_ZLIB
    // "zlib" (non-delayed) starts its s2c (outbound) stream here; idempotent, so a re-key does not restart it.
    Comp.on_newkeys_args.i = i;
    Comp.on_newkeys(comp_work);
#endif
}

void ssh_newkeys_complete(uint8_t *restrict work)
{
    (void)work;
    const uint8_t i = SshTransport.slot;
    if (i >= MAX_SSH_CONNS)
    {
        SshTransport.i32 = -1;
        return;
    }
    // RFC 4253 sec 7.3: NEWKEYS ends a key exchange, so one arriving in any other phase ends nothing
    // and the connection goes down.
    PhaseMachine.admits_newkeys_args.i = i;
    PhaseMachine.admits_newkeys(phase_machine_work);
    if (!PhaseMachine.ok)
    {
        SshTransport.i32 = -1;
        return;
    }
    // We have received the peer's SSH_MSG_NEWKEYS: our inbound direction is now encrypted and reads
    // the epoch this exchange derived (RFC 4253 sec 7.3). Both directions are keyed once we get here
    // (the server always sends its NEWKEYS first), so the KEX is complete.
    ssh_sess[i].in.epoch = 1u - ssh_sess[i].in.epoch;
    ssh_sess[i].in.enc = PROTO_TRUE;
    ssh_sess[i].kex_active = PROTO_FALSE;
    // Both directions are on the new epoch, so the old one is released: its keyed GCM contexts go
    // back and its whole stride in the connection's span is zeroed (RFC 4253 sec 7.3).
    if (ssh_sess[i].in.epoch == ssh_sess[i].out.epoch)
    {
        SshKeyMat *old = &ssh_keys[i][1u - ssh_sess[i].in.epoch];
        if (old->active && old->cipher_mode_c2s == SSH_CIPHER_AES256GCM)
        {
            AesGcm.key_wipe(old->gcm_ctx_c2s);
        }
        if (old->active && old->cipher_mode_s2c == SSH_CIPHER_AES256GCM)
        {
            AesGcm.key_wipe(old->gcm_ctx_s2c);
        }
        if (old->gcm_ctx_c2s != NULL)
        {
            protocore_secure_wipe(old->gcm_ctx_c2s, SSH_EPOCH_STRIDE); // the epoch opens the run it names
        }
        old->mac_mode_c2s = 0;
        old->mac_mode_s2c = 0;
        old->cipher_mode_c2s = 0;
        old->cipher_mode_s2c = 0;
        old->active = PROTO_FALSE;
    }
    // On the first KEX advance to the service phase; on a re-key the connection
    // is already authenticated, so resume the open (channel) phase.
    PhaseMachine.newkeys_done_args.i = i;
    PhaseMachine.newkeys_done(phase_machine_work);
    // Reset the re-key timer: the volume/time budget is measured from this completed KEX.
    ssh_sess[i].last_kex_ms = Clock.ms;
    SshTransport.i32 = 0;
    return;
}

void ssh_rekey_due(uint8_t *restrict work)
{
    (void)work;
    const uint32_t seq_send = SshTransport.rekey.seq_send;
    const uint32_t seq_recv = SshTransport.rekey.seq_recv;
    const uint32_t elapsed_ms = SshTransport.rekey.elapsed_ms;
    const uint32_t pkt_threshold = SshTransport.rekey.pkt_threshold;
    const uint32_t time_threshold_ms = SshTransport.rekey.time_threshold_ms;
    if (seq_send >= pkt_threshold || seq_recv >= pkt_threshold)
    {
        SshTransport.ok = PROTO_TRUE;
        return; // volume budget (RFC 4253 sec 9: ~1 GB)
    }
    if (time_threshold_ms && elapsed_ms >= time_threshold_ms)
    {
        SshTransport.ok = PROTO_TRUE;
        return; // time budget (~1 hour)
    }
    SshTransport.ok = PROTO_FALSE;
    return;
}

void ssh_transport_begin_rekey(uint8_t *restrict work)
{
    const uint8_t i = SshTransport.slot;
    uint8_t *out = SshTransport.out_args.out;
    size_t *out_len = &SshTransport.out_args.out_len;
    const size_t cap = SshTransport.out_args.cap;
    if (i >= MAX_SSH_CONNS)
    {
        SshTransport.i32 = -1;
        return;
    }
    // Fresh server KEXINIT (re-stores I_S for the new exchange hash).
    SshTransport.slot = i;
    SshTransport.out_args.out = out;
    SshTransport.out_args.cap = cap;
    ssh_kexinit_build(work);
    *out_len = SshTransport.out_args.out_len;
    if (SshTransport.i32 != 0)
    {
        SshTransport.i32 = -1;
        return;
    }
    // New ephemeral for forward secrecy across the re-key (re-generated for the finally
    // negotiated method once the peer's KEXINIT arrives; see the KEXINIT dispatch).
    SshTransport.slot = i;
    ssh_kex_generate(work);
    if (SshTransport.i32 != 0)
    {
        SshTransport.i32 = -1;
        return;
    }
    ssh_sess[i].kex_active = PROTO_TRUE; // an exchange is running from here to NEWKEYS
    PhaseMachine.rekey_begin_args.i = i;
    PhaseMachine.rekey_begin(phase_machine_work);
    SshTransport.i32 = 0;
    return;
}

// The RFC 4253 transitions, named. One instance.
// Designated, so a member's position in the struct does not decide what it binds to.
SshTransportNs SshTransport = {.recv_ident = ssh_transport_recv_ident,
                               .send_ident = ssh_transport_send_ident,
                               .kexinit_build = ssh_kexinit_build,
                               .kexinit_parse = ssh_kexinit_parse,
                               .kex_generate = ssh_kex_generate,
                               .exchange_hash = ssh_kex_exchange_hash,
                               .kexdh_reply = ssh_kexdh_handle,
                               .newkeys_sent = ssh_newkeys_sent,
                               .newkeys_complete = ssh_newkeys_complete,
                               .rekey_due = ssh_rekey_due,
                               .begin_rekey = ssh_transport_begin_rekey};
// ---------------------------------------------------------------------------
// RFC 4253 sec 6 - binary packet protocol: framing, encryption, MAC, reassembly
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// BSS allocation
// ---------------------------------------------------------------------------

SshPacketState ssh_pkt[MAX_SSH_CONNS];

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Compute the padding needed so that (5 + payload_len + padding) is a
// multiple of 16 (AES block size).  Minimum padding = 4 bytes (RFC 4253 §6).
static size_t compute_padding(size_t payload_len)
{
    size_t total = 5 + payload_len; // 4-byte length + 1-byte padding_len + payload
    size_t remainder = total % 16;
    size_t padding = (remainder == 0) ? 0 : (16 - remainder);
    if (padding < 4)
    {
        padding += 16;
    }
    return padding;
}

// The slot index of @p s, which points into ssh_pkt[]: what the connection's memory map is keyed on.
static inline uint8_t pkt_slot(const SshPacketState *s)
{
    return (uint8_t)(s - ssh_pkt);
}

proto_bool ssh_pkt_slot_storage(SshPacketState *s)
{
    if (s->tx_wire != NULL)
    {
        return PROTO_TRUE;
    }
    Ssh.conn_slot_args.i = pkt_slot(s);
    Ssh.conn_slot(protocore_ssh_span());
    uint8_t *base = Ssh.ptr;
    if (base == NULL)
    {
        return PROTO_FALSE;
    }
    s->tx_wire = base + SSH_OFF_WIRE;
    s->mac_work = base + SSH_OFF_MAC_WORK;
    s->cipher_work = base + SSH_OFF_CIPHER_WORK;
    s->crypto_work = base + SSH_OFF_CRYPTO_WORK;
    s->rx_buf = base + SSH_OFF_RX_ASM;
    return PROTO_TRUE;
}

// Compute the MAC over 4-byte seq_no || buf using the HMAC named by mac_mode, working out of the
// slot's own bytes. For E&M the buf is the plaintext packet; for ETM it is the length field ||
// ciphertext. Writes ssh_mac_len() bytes.
static void compute_mac_mode(uint8_t mac_mode, uint8_t *work, const uint8_t *mac_key, uint32_t seq_no,
                             const uint8_t *buf, size_t buf_len, uint8_t *mac_out)
{
    uint8_t seq_be[4];
    write_u32_be(seq_be, seq_no);
    if (mac_mode == SSH_MAC_HMAC_SHA512 || mac_mode == SSH_MAC_HMAC_SHA512_ETM)
    {
        HmacSha512.key_args.key = mac_key;
        HmacSha512.key_args.key_len = 64;
        HmacSha512.init(work);
        HmacSha512.update_args.data = seq_be;
        HmacSha512.update_args.len = 4;
        HmacSha512.update(work);
        HmacSha512.update_args.data = buf;
        HmacSha512.update_args.len = buf_len;
        HmacSha512.update(work);
        HmacSha512.final_args.out = mac_out;
        HmacSha512.final(work);
    }
    else
    {
        HmacSha256.key_args.key = mac_key;
        HmacSha256.key_args.key_len = 32;
        HmacSha256.init(work);
        HmacSha256.update_args.data = seq_be;
        HmacSha256.update_args.len = 4;
        HmacSha256.update(work);
        HmacSha256.update_args.data = buf;
        HmacSha256.update_args.len = buf_len;
        HmacSha256.update(work);
        HmacSha256.final_args.out = mac_out;
        HmacSha256.final(work);
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void ssh_pkt_init(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    SshPacketState *s = &ssh_pkt[i];
    // The wire buffer is a persistent borrow bound to the slot, not to the connection on it: it is
    // never released, so it carries across to the next connection this slot serves.
    uint8_t *wire = s->tx_wire;
    uint8_t *macw = s->mac_work;
    uint8_t *ciphw = s->cipher_work;
    uint8_t *kexw = s->crypto_work;
    uint8_t *rx = s->rx_buf;
    mem.set(s, 0, sizeof(*s)); // is_client defaults false = server role
    s->tx_wire = wire;
    s->mac_work = macw;
    s->cipher_work = ciphw;
    s->crypto_work = kexw;
    s->rx_buf = rx;
}

void ssh_pkt_set_client(uint8_t i)
{
    if (i < MAX_SSH_CONNS)
    {
        ssh_pkt[i].is_client = PROTO_TRUE;
    }
}

// ---------------------------------------------------------------------------
// Emit: frame one packet into the secure pool and raise the flag a worker drains
// ---------------------------------------------------------------------------

// RFC 4253 sec 7.1: "Once a party has sent a SSH_MSG_KEXINIT message for key exchange or
// re-exchange, until it has sent a SSH_MSG_NEWKEYS message ... it MUST NOT send any messages other
// than: Transport layer generic messages (1 to 19) (but SSH_MSG_SERVICE_REQUEST and
// SSH_MSG_SERVICE_ACCEPT MUST NOT be sent); Algorithm negotiation messages (20 to 29) (but further
// SSH_MSG_KEXINIT messages MUST NOT be sent); Specific key exchange method messages (30 to 49)."
static proto_bool kex_window_admits(uint8_t msg)
{
    if (msg >= 30u && msg <= 49u)
    {
        return PROTO_TRUE; // key exchange method messages
    }
    if (msg >= 20u && msg <= 29u)
    {
        return msg != SSH_MSG_KEXINIT; // negotiation, but not a further KEXINIT
    }
    if (msg >= 1u && msg <= 19u)
    {
        return msg != SSH_MSG_SERVICE_REQUEST && msg != SSH_MSG_SERVICE_ACCEPT;
    }
    return PROTO_FALSE; // 50 and up: userauth and the connection protocol wait for NEWKEYS
}

// True when slot @p i may put a message of this number on the wire right now. The window opens when
// this end sends its KEXINIT, not when an exchange starts: at that point it has yet to send its own.
static proto_bool ssh_may_send(uint8_t i, uint8_t msg)
{
    return i < MAX_SSH_CONNS && (!ssh_sess[i].kexinit_sent || kex_window_admits(msg));
}

int ssh_pkt_emit(uint8_t i, const uint8_t *payload, size_t len, const SshDir *dir)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    if (len < 1 || !ssh_may_send(i, payload[0]))
    {
        return -1;
    }
    SshPacketState *s = &ssh_pkt[i];

    // The wire lives in the secure pool because the payload it carries is the session's own
    // plaintext until the cipher runs over it.
    if (!ssh_pkt_slot_storage(s))
    {
        return -1;
    }

    // A packet already framed and not yet drained: append this one after it. The slot holds two
    // (SSH_WIRE_CAP), so a server pair framed back-to-back leaves on the same drain rather than the
    // second being dropped. The fill point is tx_len; ssh_pkt_send frames from there and bumps the
    // send sequence, so the two packets carry consecutive seq numbers.
    size_t off = 0;
    if (s->tx_ready)
    {
        off = s->tx_len;
    }
    size_t wlen = 0;
    if (ssh_pkt_send(i, payload, len, s->tx_wire + off, &wlen, SSH_WIRE_CAP - off, dir) != 0)
    {
        return -1;
    }
    s->tx_len = off + wlen;
    if (!s->tx_ready)
    {
        s->tx_off = 0;
        s->tx_ready = PROTO_TRUE;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Direction-aware key selection (RFC 4253 §7.2 names keys by direction, not role)
// ---------------------------------------------------------------------------
// A client sends c2s / receives s2c; a server is the mirror. Selecting the key set through these
// keeps one send and one receive implementation correct for both roles.

static inline const uint8_t *km_send_chacha(const SshKeyMat *km, proto_bool cli)
{
    return cli ? km->chacha_key_c2s : km->chacha_key_s2c;
}
static inline const uint8_t *km_recv_chacha(const SshKeyMat *km, proto_bool cli)
{
    return cli ? km->chacha_key_s2c : km->chacha_key_c2s;
}
static inline const uint8_t *km_send_aes_key(const SshKeyMat *km, proto_bool cli)
{
    return cli ? km->aes_key_c2s : km->aes_key_s2c;
}
static inline uint8_t *km_send_aes_iv(SshKeyMat *km, proto_bool cli)
{
    return cli ? km->aes_iv_c2s : km->aes_iv_s2c;
}
static inline const uint8_t *km_recv_aes_key(const SshKeyMat *km, proto_bool cli)
{
    return cli ? km->aes_key_s2c : km->aes_key_c2s;
}
// GCM keeps a keyed context per direction rather than a raw key: the schedule is built once at install
// (ssh_dh.c) and reused per packet, because standing one up costs ~9,200 cycles regardless of packet
// size and would otherwise dominate small interactive traffic.
static inline uint8_t *km_send_gcm(SshKeyMat *km, proto_bool cli)
{
    return cli ? km->gcm_ctx_c2s : km->gcm_ctx_s2c;
}
static inline uint8_t *km_recv_gcm(SshKeyMat *km, proto_bool cli)
{
    return cli ? km->gcm_ctx_s2c : km->gcm_ctx_c2s;
}
static inline uint8_t *km_recv_aes_iv(SshKeyMat *km, proto_bool cli)
{
    return cli ? km->aes_iv_s2c : km->aes_iv_c2s;
}
static inline const uint8_t *km_send_mac(const SshKeyMat *km, proto_bool cli)
{
    return cli ? km->mac_key_c2s : km->mac_key_s2c;
}
static inline const uint8_t *km_recv_mac(const SshKeyMat *km, proto_bool cli)
{
    return cli ? km->mac_key_s2c : km->mac_key_c2s;
}
// The cipher and the MAC are negotiated per direction (RFC 4253 sec 7.1), so the mode travels with the
// key set: what we send under, and what we expect to receive under.
static inline uint8_t km_send_cipher(const SshKeyMat *km, proto_bool cli)
{
    if (cli)
    {
        return km->cipher_mode_c2s;
    }
    return km->cipher_mode_s2c;
}
static inline uint8_t km_recv_cipher(const SshKeyMat *km, proto_bool cli)
{
    if (cli)
    {
        return km->cipher_mode_s2c;
    }
    return km->cipher_mode_c2s;
}
static inline uint8_t km_send_mac_mode(const SshKeyMat *km, proto_bool cli)
{
    if (cli)
    {
        return km->mac_mode_c2s;
    }
    return km->mac_mode_s2c;
}
static inline uint8_t km_recv_mac_mode(const SshKeyMat *km, proto_bool cli)
{
    if (cli)
    {
        return km->mac_mode_s2c;
    }
    return km->mac_mode_c2s;
}

// ---------------------------------------------------------------------------
// Send
// ---------------------------------------------------------------------------

int ssh_pkt_send_at(uint8_t i, uint8_t *out, size_t payload_len, size_t *out_len, size_t out_cap, const SshDir *dir)
{
    size_t comp_scope = protocore_plaintext_mark();
    if (i >= MAX_SSH_CONNS)
    {
        protocore_plaintext_release(comp_scope);
        return -1;
    }
    // The payload was built in place at SSH_WIRE_PAYLOAD_OFF, so its message number is there.
    const uint8_t msg_no = (payload_len >= 1) ? out[SSH_WIRE_PAYLOAD_OFF] : 0u;
    if (payload_len < 1 || !ssh_may_send(i, msg_no))
    {
        protocore_plaintext_release(comp_scope);
        return -1;
    }
    // Framing our KEXINIT is what opens the sec 7.1 window; ssh_newkeys_sent() closes it.
    if (msg_no == SSH_MSG_KEXINIT)
    {
        ssh_sess[i].kexinit_sent = PROTO_TRUE;
    }
    SshPacketState *s = &ssh_pkt[i];
    SshKeyMat *km = &ssh_keys[i][dir->epoch];

    // Sequence overflow guard.
    if (s->seq_no_send >= SSH_SEQ_CLOSE_THRESHOLD)
    {
        protocore_plaintext_release(comp_scope);
        return -1;
    }

    if (!ssh_pkt_slot_storage(s))
    {
        protocore_plaintext_release(comp_scope);
        return -1;
    }

#if PROTOCORE_ENABLE_SSH_ZLIB
    // Compression (RFC 4253 §6.2) transforms the payload BEFORE padding/encryption, once the s2c
    // stream is active. The compressor is stateful (context takeover), so this call must be followed
    // by a full send - the same atomicity the stateful cipher below already requires. The wire buffer
    // is sized (SSH_WIRE_CAP) so the compressed payload can never overflow out_cap and desync.
    Comp.s2c_active_args.i = i;
    Comp.s2c_active(comp_work);
    if (Comp.ok)
    {
        // TODO(slot): the compressor's output still borrows; it has no named offset yet.
        size_t bound = ssh_deflate_bound(payload_len);
        uint8_t *cbuf = (uint8_t *)protocore_plaintext_alloc(bound, 16);
        size_t clen = 0;
        // Staged inside the guard: as the right operand of `||` this must not run when the
        // allocation above it failed.
        proto_bool comp_failed = (cbuf == NULL);
        if (!comp_failed)
        {
            Comp.s2c_args.i = i;
            Comp.s2c_args.src = out + SSH_WIRE_PAYLOAD_OFF;
            Comp.s2c_args.src_len = payload_len;
            Comp.s2c_args.dst = cbuf;
            Comp.s2c_args.dst_cap = bound;
            Comp.s2c_args.out_len = &clen;
            Comp.s2c(comp_work);
            comp_failed = (Comp.n != 0);
        }
        if (comp_failed)
        {
            protocore_plaintext_release(comp_scope);
            return -1;
        }
        if (SSH_WIRE_PAYLOAD_OFF + clen > out_cap)
        {
            protocore_plaintext_release(comp_scope);
            return -1;
        }
        mem.cpy(out + SSH_WIRE_PAYLOAD_OFF, cbuf, clen);
        payload_len = clen;
    }
#endif

    // Padding block size and base differ by mode. chacha/gcm (AEAD) and aes-ETM exclude the 4-byte
    // length from the block-alignment (it is AAD / sent in clear); plain aes-E&M includes it.
    //   chacha    : block 8,  base = padding_length + payload
    //   aes GCM   : block 16, base = padding_length + payload   (RFC 5647 sec 7.3)
    //   aes ETM   : block 16, base = padding_length + payload
    //   aes E&M / plaintext : block 16, base = length + padding_length + payload  (compute_padding)
    const proto_bool cli = s->is_client; // send direction: client uses c2s, server uses s2c
    const uint8_t send_cipher = km_send_cipher(km, cli);
    const uint8_t send_mac_mode = km_send_mac_mode(km, cli);
    proto_bool chacha = dir->enc && send_cipher == SSH_CIPHER_CHACHA20POLY1305;
    proto_bool gcm = dir->enc && send_cipher == SSH_CIPHER_AES256GCM;
    proto_bool etm = dir->enc && send_cipher == SSH_CIPHER_AES256CTR && ssh_mac_is_etm(send_mac_mode);
    size_t pad_len;
    size_t tag_len;
    if (chacha)
    {
        size_t base = 1 + payload_len;
        pad_len = 8 - (base % 8);
        if (pad_len < 4)
        {
            pad_len += 8;
        }
        tag_len = PROTOCORE_CHACHAPOLY_TAG_LEN;
    }
    else if (gcm)
    {
        size_t base = 1 + payload_len;
        pad_len = 16 - (base % 16);
        if (pad_len < 4)
        {
            pad_len += 16;
        }
        tag_len = PROTOCORE_AESGCM_TAG_LEN;
    }
    else if (etm)
    {
        size_t base = 1 + payload_len;
        pad_len = 16 - (base % 16);
        if (pad_len < 4)
        {
            pad_len += 16;
        }
        tag_len = ssh_mac_len(send_mac_mode);
    }
    else
    {
        pad_len = compute_padding(payload_len);
        tag_len = dir->enc ? ssh_mac_len(send_mac_mode) : 0;
    }
    size_t pkt_len = 1 + payload_len + pad_len; // padding_length + payload + padding
    size_t wire_len = 4 + pkt_len + tag_len;

    if (wire_len > out_cap)
    {
        protocore_plaintext_release(comp_scope);
        return -1;
    }

    // Frame around the payload already sitting at out + SSH_WIRE_PAYLOAD_OFF.
    write_u32_be(out, (uint32_t)pkt_len); // packet_length
    out[4] = (uint8_t)pad_len;            // padding_length
    Rng.fill_args.out = out + 5 + payload_len;
    Rng.fill_args.len = pad_len;
    Rng.fill(protocore_rng_span()); // random padding

    if (chacha)
    {
        // Encrypt length (header key) + payload (main key) and append the Poly1305 tag.
        ChachaPoly.encrypt_args.key = km_send_chacha(km, cli);
        ChachaPoly.encrypt_args.src = out;
        ChachaPoly.encrypt_args.dest = out;
        ChachaPoly.encrypt_args.seqnr = s->seq_no_send;
        ChachaPoly.encrypt_args.payload_len = (uint32_t)pkt_len;
        ChachaPoly.encrypt(s->cipher_work);
    }
    else if (gcm)
    {
        // aes256-gcm@openssh.com: length stays in clear (it is the AAD); seal the packet body in
        // place and append the 16-byte GCM tag. The context's invocation counter advances by one.
        // Seal in place (tag appended after the ciphertext), then advance the RFC 5647 invocation counter.
        uint8_t *iv = km_send_aes_iv(km, cli);
        AesGcm.seal_args.nonce = iv;
        AesGcm.seal_args.aad = out;
        AesGcm.seal_args.aad_len = 4;
        AesGcm.seal_args.pt = out + 4;
        AesGcm.seal_args.pt_len = pkt_len;
        AesGcm.seal_args.ct_out = out + 4;
        AesGcm.seal_args.tag_out = out + 4 + pkt_len;
        AesGcm.seal(km_send_gcm(km, cli));
        AesGcm.iv_args.iv = iv;
        AesGcm.iv_increment(km_send_gcm(km, cli));
    }
    else if (etm)
    {
        // Encrypt-then-MAC: length stays in clear; encrypt the payload, then MAC over (length||ct).
        Aes256Ctr.crypt_args.key = km_send_aes_key(km, cli);
        Aes256Ctr.crypt_args.counter = km_send_aes_iv(km, cli);
        Aes256Ctr.crypt_args.in = out + 4;
        Aes256Ctr.crypt_args.out = out + 4;
        Aes256Ctr.crypt_args.len = pkt_len;
        Aes256Ctr.crypt(s->cipher_work);
        compute_mac_mode(send_mac_mode, s->mac_work, km_send_mac(km, cli), s->seq_no_send, out, 4 + pkt_len,
                         out + 4 + pkt_len);
    }
    else if (dir->enc)
    {
        // Encrypt-and-MAC: MAC over plaintext (seq || unencrypted packet), then AES-256-CTR.
        uint8_t mac[64];
        compute_mac_mode(send_mac_mode, s->mac_work, km_send_mac(km, cli), s->seq_no_send, out, 4 + pkt_len, mac);
        Aes256Ctr.crypt_args.key = km_send_aes_key(km, cli);
        Aes256Ctr.crypt_args.counter = km_send_aes_iv(km, cli);
        Aes256Ctr.crypt_args.in = out;
        Aes256Ctr.crypt_args.out = out;
        Aes256Ctr.crypt_args.len = 4 + pkt_len;
        Aes256Ctr.crypt(s->cipher_work);
        mem.cpy(out + 4 + pkt_len, mac, tag_len);
        protocore_secure_wipe(mac, sizeof(mac));
    }

    *out_len = wire_len;
    s->seq_no_send++;
    protocore_plaintext_release(comp_scope);
    return 0;
}

int ssh_pkt_send(uint8_t i, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t *out_len, size_t out_cap,
                 const SshDir *dir)
{
    if (SSH_WIRE_PAYLOAD_OFF + payload_len > out_cap)
    {
        return -1;
    }
    mem.cpy(out + SSH_WIRE_PAYLOAD_OFF, payload, payload_len);
    return ssh_pkt_send_at(i, out, payload_len, out_len, out_cap, dir);
}

// ---------------------------------------------------------------------------
// Receive
// ---------------------------------------------------------------------------

// Dispatch one decrypted packet payload (message-type byte + data) to @p handler, first decompressing
// it when the client-to-server compression stream is active (RFC 4253 sec 6.2). Compression only runs
// after NEWKEYS / auth success, so the pre-auth plaintext path never enters the c2s branch.
// @return 0 on success (handler invoked, or skipped for a flush-only packet), -1 on a malformed
//         compressed stream / decompression overflow (the caller must wipe + disconnect).
static int ssh_dispatch_payload(uint8_t i, const uint8_t *payload, size_t payload_len, ssh_msg_handler_t handler)
{
    size_t inflate_scope = protocore_plaintext_mark();
#if PROTOCORE_ENABLE_SSH_ZLIB
    Comp.c2s_active_args.i = i;
    Comp.c2s_active(comp_work);
    if (Comp.ok)
    {
        uint8_t *dbuf = (uint8_t *)protocore_plaintext_alloc(SSH_PKT_BUF_SIZE, 16);
        size_t dlen = 0;
        // Staged inside the guard: as the right operand of `||` this must not run when the
        // allocation above it failed.
        proto_bool inflate_failed = (dbuf == NULL);
        if (!inflate_failed)
        {
            Comp.c2s_args.i = i;
            Comp.c2s_args.src = payload;
            Comp.c2s_args.src_len = payload_len;
            Comp.c2s_args.dst = dbuf;
            Comp.c2s_args.dst_cap = SSH_PKT_BUF_SIZE;
            Comp.c2s_args.out_len = &dlen;
            Comp.c2s(comp_work);
            inflate_failed = (Comp.n != 0);
        }
        if (inflate_failed)
        {
            protocore_plaintext_release(inflate_scope);
            return -1; // malformed stream, or a payload that decompresses beyond the uncompressed limit
        }
        if (dlen == 0)
        {
            protocore_plaintext_release(inflate_scope);
            return 0; // the packet carried only flush bits (no message); consume it and move on
        }
        handler(i, dbuf[0], dbuf, dlen);
        protocore_plaintext_release(inflate_scope);
        return 0;
    }
#endif
    handler(i, payload[0], payload, payload_len);
    protocore_plaintext_release(inflate_scope);
    return 0;
}

// Extract one packet from the head of the RX buffer and dispatch it to @p handler. Every cipher path
// returns the same tri-state so ssh_pkt_recv's extract loop can stay flat: 1 = one packet consumed (keep
// extracting), 0 = incomplete (need more bytes, stop), -1 = fatal (the buffer is already wiped on the paths
// that require it; the caller must disconnect). One function per cipher mode keeps each within the nesting
// and cognitive-complexity budget that the single inline switch blew past.

static int ssh_recv_chachapoly(uint8_t i, SshPacketState *s, const SshKeyMat *km, ssh_msg_handler_t handler)
{
    // chacha20-poly1305@openssh.com. Keyed by the sequence number, so decrypting the
    // length is stateless/repeatable - no cipher-state peek/restore is needed.
    const uint8_t *rk = km_recv_chacha(km, s->is_client); // recv: client s2c, server c2s
    ChachaPoly.length_args.key = rk;
    ChachaPoly.length_args.enc_len = s->rx_buf;
    ChachaPoly.length_args.seqnr = s->seq_no_recv;
    ChachaPoly.get_length(s->cipher_work);
    uint32_t pkt_len = ChachaPoly.length;
    // The whole packet must fit the reassembly region (RFC 4253 sec 6.1: 35000 bytes including the
    // tag), which is what rx_buf spans.
    if (pkt_len < 1 || pkt_len > SSH_RX_ASM_CAP - 4 - PROTOCORE_CHACHAPOLY_TAG_LEN)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    size_t wire_need = 4 + pkt_len + PROTOCORE_CHACHAPOLY_TAG_LEN;
    if (s->rx_len < wire_need)
    {
        return 0; // incomplete packet
    }

    // Verify the Poly1305 tag over the ciphertext, then decrypt in place. The tag is checked before
    // a single byte is written, so a failure leaves the ciphertext untouched and no plaintext.
    ChachaPoly.decrypt_args.key = rk;
    ChachaPoly.decrypt_args.src = s->rx_buf;
    ChachaPoly.decrypt_args.dest = s->rx_buf;
    ChachaPoly.decrypt_args.seqnr = s->seq_no_recv;
    ChachaPoly.decrypt_args.payload_len = pkt_len;
    ChachaPoly.decrypt(s->cipher_work);
    if (!ChachaPoly.ok)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1; // caller must close connection
    }

    if (s->seq_no_recv >= SSH_SEQ_CLOSE_THRESHOLD)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    s->seq_no_recv++;

    uint8_t pad_len_byte = s->rx_buf[4];
    if (pad_len_byte < 4 || pad_len_byte >= pkt_len)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    size_t payload_len = pkt_len - 1 - pad_len_byte;
    if (ssh_dispatch_payload(i, s->rx_buf + 5, payload_len, handler) < 0)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }

    // Consume, then wipe the plaintext the shift left behind.
    size_t consumed = wire_need;
    size_t left = s->rx_len - consumed;
    mem.move(s->rx_buf, s->rx_buf + consumed, left);
    protocore_secure_wipe(s->rx_buf + left, consumed);
    s->rx_len = left;
    return 1;
}

static int ssh_recv_aesgcm(uint8_t i, SshPacketState *s, SshKeyMat *km, ssh_msg_handler_t handler)
{
    size_t scratch_scope = protocore_plaintext_mark();
    // aes256-gcm@openssh.com (RFC 5647): the 4-byte packet_length is sent in the clear and is
    // the AEAD's additional authenticated data; the 16-byte GCM tag is verified over
    // (length || ciphertext) BEFORE any plaintext is produced.
    uint32_t pkt_len = read_u32_be(s->rx_buf);
    // The encrypted portion (pkt_len) must be a positive whole number of AES blocks, and the whole
    // packet must fit the reassembly region (RFC 4253 sec 6.1: 35000 bytes including the tag).
    if (pkt_len < 1 || pkt_len > SSH_RX_ASM_CAP - 4 - PROTOCORE_AESGCM_TAG_LEN || (pkt_len % 16) != 0)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        protocore_plaintext_release(scratch_scope);
        return -1;
    }
    size_t wire_need = 4 + pkt_len + PROTOCORE_AESGCM_TAG_LEN;
    if (s->rx_len < wire_need)
    {
        protocore_plaintext_release(scratch_scope);
        return 0; // incomplete packet
    }

    const size_t scratch_sz = pkt_len; // plaintext = padding_length || payload || padding
    uint8_t *scratch = (uint8_t *)protocore_plaintext_alloc(scratch_sz, 16);
    if (!scratch)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        protocore_plaintext_release(scratch_scope);
        return -1;
    }

    // Verify the GCM tag over (length || ciphertext), then decrypt. No plaintext on failure.
    uint8_t *iv = km_recv_aes_iv(km, s->is_client);
    AesGcm.open_args.nonce = iv;
    AesGcm.open_args.aad = s->rx_buf;
    AesGcm.open_args.aad_len = 4;
    AesGcm.open_args.ct = s->rx_buf + 4;
    AesGcm.open_args.ct_len = pkt_len;
    AesGcm.open_args.tag = s->rx_buf + 4 + pkt_len;
    AesGcm.open_args.out = scratch;
    AesGcm.open(km_recv_gcm(km, s->is_client));
    if (!AesGcm.ok)
    {
        protocore_secure_wipe(scratch, scratch_sz);
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        protocore_plaintext_release(scratch_scope);
        return -1; // caller must close connection
    }
    // Tag verified: advance the RFC 5647 invocation counter (recv success).
    AesGcm.iv_args.iv = iv;
    AesGcm.iv_increment(km_recv_gcm(km, s->is_client));

    if (s->seq_no_recv >= SSH_SEQ_CLOSE_THRESHOLD)
    {
        protocore_secure_wipe(scratch, scratch_sz);
        protocore_plaintext_release(scratch_scope);
        return -1;
    }
    s->seq_no_recv++;

    uint8_t pad_len_byte = scratch[0];
    if (pad_len_byte < 4 || pad_len_byte >= pkt_len)
    {
        protocore_secure_wipe(scratch, scratch_sz);
        protocore_plaintext_release(scratch_scope);
        return -1;
    }
    size_t payload_len = pkt_len - 1 - pad_len_byte;
    if (ssh_dispatch_payload(i, scratch + 1, payload_len, handler) < 0)
    {
        protocore_secure_wipe(scratch, scratch_sz);
        protocore_plaintext_release(scratch_scope);
        return -1;
    }

    size_t consumed = wire_need;
    mem.move(s->rx_buf, s->rx_buf + consumed, s->rx_len - consumed);
    s->rx_len -= consumed;
    protocore_secure_wipe(scratch, scratch_sz);
    protocore_plaintext_release(scratch_scope);
    return 1;
}

static int ssh_recv_ctr_etm(uint8_t i, SshPacketState *s, SshKeyMat *km, ssh_msg_handler_t handler)
{
    // aes256-ctr + encrypt-then-MAC: the 4-byte packet_length is sent in the clear, and the
    // MAC is verified over (length || ciphertext) BEFORE anything is decrypted.
    uint32_t pkt_len = read_u32_be(s->rx_buf);
    const uint8_t recv_mac_mode = km_recv_mac_mode(km, s->is_client);
    size_t mac_tag = ssh_mac_len(recv_mac_mode);
    // The encrypted portion (pkt_len) must be a positive whole number of AES blocks, and the whole
    // packet must fit the reassembly region (RFC 4253 sec 6.1: 35000 bytes including the mac).
    if (pkt_len < 1 || pkt_len > SSH_RX_ASM_CAP - 4 - mac_tag || (pkt_len % 16) != 0)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    size_t wire_need = 4 + pkt_len + mac_tag;
    if (s->rx_len < wire_need)
    {
        return 0; // incomplete packet
    }

    uint8_t expected_mac[64];
    compute_mac_mode(recv_mac_mode, s->mac_work, km_recv_mac(km, s->is_client), s->seq_no_recv, s->rx_buf, 4 + pkt_len,
                     expected_mac);
    if (!protocore_ct_eq(expected_mac, s->rx_buf + 4 + pkt_len, mac_tag))
    {
        protocore_secure_wipe(expected_mac, sizeof(expected_mac));
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1; // caller must close connection
    }
    protocore_secure_wipe(expected_mac, sizeof(expected_mac));

    if (s->seq_no_recv >= SSH_SEQ_CLOSE_THRESHOLD)
    {
        return -1;
    }
    s->seq_no_recv++;

    // MAC verified -> decrypt in place (advances c2s_ctx by exactly pkt_len/16 blocks). The MAC
    // covered length || ciphertext, so nothing still needs the ciphertext and the packet is
    // unwrapped where it already sits.
    Aes256Ctr.crypt_args.key = km_recv_aes_key(km, s->is_client);
    Aes256Ctr.crypt_args.counter = km_recv_aes_iv(km, s->is_client);
    Aes256Ctr.crypt_args.in = s->rx_buf + 4;
    Aes256Ctr.crypt_args.out = s->rx_buf + 4;
    Aes256Ctr.crypt_args.len = pkt_len;
    Aes256Ctr.crypt(s->cipher_work);

    // rx_buf + 4 = padding_length || payload || padding.
    uint8_t pad_len_byte = s->rx_buf[4];
    if (pad_len_byte < 4 || pad_len_byte >= pkt_len)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    size_t payload_len = pkt_len - 1 - pad_len_byte;
    if (ssh_dispatch_payload(i, s->rx_buf + 5, payload_len, handler) < 0)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }

    size_t consumed = wire_need;
    mem.move(s->rx_buf, s->rx_buf + consumed, s->rx_len - consumed);
    // Wipe what the shift left behind: this packet was decrypted in place, so the tail past the new
    // length still holds its plaintext.
    protocore_secure_wipe(s->rx_buf + (s->rx_len - consumed), consumed);
    s->rx_len -= consumed;
    return 1;
}

static int ssh_recv_ctr_emac(uint8_t i, SshPacketState *s, SshKeyMat *km, ssh_msg_handler_t handler)
{
    // aes256-ctr + encrypt-and-MAC. We need the first cipher block (16 bytes) for the length.
    if (s->rx_len < 16)
    {
        return 0; // wait for more data
    }

    // --- Peek packet_length WITHOUT advancing the cipher ---
    // Decrypt only the 4-byte length prefix against the current counter block; the counter is not advanced
    // and no cipher state touches the stack (all working memory stays in the shared crypto scratch).
    const uint8_t *rk = km_recv_aes_key(km, s->is_client); // recv: client s2c, server c2s
    uint8_t *rctr = km_recv_aes_iv(km, s->is_client);
    Aes256Ctr.get_length_args.key = rk;
    Aes256Ctr.get_length_args.counter = rctr;
    Aes256Ctr.get_length_args.enc4 = s->rx_buf;
    Aes256Ctr.get_length(s->cipher_work);
    uint32_t pkt_len = Aes256Ctr.length;

    // Validate length.  The encrypted portion (4 + pkt_len) must be a
    // whole number of AES blocks (RFC 4253 §6 padding guarantees this).
    size_t enc_len = 4 + pkt_len;
    const uint8_t recv_mac_mode = km_recv_mac_mode(km, s->is_client);
    size_t mac_tag = ssh_mac_len(recv_mac_mode);
    // The whole packet must fit the reassembly region (RFC 4253 sec 6.1: 35000 bytes including the
    // mac), and the encrypted portion must be a whole number of AES blocks.
    if (pkt_len < 1 || enc_len > SSH_RX_ASM_CAP - mac_tag || (enc_len % 16) != 0)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }

    size_t wire_need = enc_len + mac_tag;
    if (s->rx_len < wire_need)
    {
        return 0; // incomplete packet; cipher state already restored
    }

    // Full packet present.  Decrypt EXACTLY the encrypted portion in place, which advances the recv
    // counter by exactly enc_len/16 blocks and leaves it aligned on the next packet boundary. The
    // mac is sent in the clear past enc_len, so decrypting here does not disturb it.
    const uint8_t *rx_mac = s->rx_buf + enc_len;
    Aes256Ctr.crypt_args.key = rk;
    Aes256Ctr.crypt_args.counter = rctr;
    Aes256Ctr.crypt_args.in = s->rx_buf;
    Aes256Ctr.crypt_args.out = s->rx_buf;
    Aes256Ctr.crypt_args.len = enc_len;
    Aes256Ctr.crypt(s->cipher_work);

    // Verify MAC over seq_no || plaintext(rx_buf[0..enc_len)).
    uint8_t expected_mac[64];
    compute_mac_mode(recv_mac_mode, s->mac_work, km_recv_mac(km, s->is_client), s->seq_no_recv, s->rx_buf, enc_len,
                     expected_mac);

    if (!protocore_ct_eq(expected_mac, rx_mac, mac_tag))
    {
        // MAC failure: zero everything and disconnect.
        protocore_secure_wipe(expected_mac, sizeof(expected_mac));
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1; // caller must close connection
    }
    protocore_secure_wipe(expected_mac, sizeof(expected_mac));

    // MAC verified.  Sequence overflow guard.
    if (s->seq_no_recv >= SSH_SEQ_CLOSE_THRESHOLD)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    s->seq_no_recv++;

    // Extract payload: rx_buf[5 .. 5 + payload_len - 1]
    uint8_t pad_len_byte = s->rx_buf[4];
    // RFC 4253 6: there MUST be at least 4 bytes of padding, and it cannot
    // exceed the packet (which would underflow payload_len).
    if (pad_len_byte < 4 || pad_len_byte >= pkt_len)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    size_t payload_len = pkt_len - 1 - pad_len_byte;
    if (ssh_dispatch_payload(i, s->rx_buf + 5, payload_len, handler) < 0)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }

    // Consume from rx_buf, then wipe the plaintext the shift left behind.
    size_t consumed = wire_need;
    size_t left = s->rx_len - consumed;
    mem.move(s->rx_buf, s->rx_buf + consumed, left);
    protocore_secure_wipe(s->rx_buf + left, consumed);
    s->rx_len = left;
    return 1;
}

static int ssh_recv_plain(uint8_t i, SshPacketState *s, const SshKeyMat *km, ssh_msg_handler_t handler)
{
    (void)km; // no keys before NEWKEYS
    // Unencrypted path (during initial handshake / before NEWKEYS).
    uint32_t pkt_len = read_u32_be(s->rx_buf);
    // The whole packet must fit the reassembly region (RFC 4253 sec 6.1: 35000 bytes).
    if (pkt_len < 1 || pkt_len > SSH_RX_ASM_CAP - 4)
    {
        protocore_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    size_t wire_need = 4 + pkt_len; // no MAC before NEWKEYS
    if (s->rx_len < wire_need)
    {
        return 0;
    }

    if (s->seq_no_recv >= SSH_SEQ_CLOSE_THRESHOLD)
    {
        return -1;
    }
    s->seq_no_recv++;

    uint8_t pad_len_byte = s->rx_buf[4];
    // RFC 4253 sec 6: at least four bytes of padding, the same bound the four encrypted paths hold.
    if (pad_len_byte < 4 || pad_len_byte >= pkt_len)
    {
        return -1;
    }
    size_t payload_len = pkt_len - 1 - pad_len_byte;
    if (ssh_dispatch_payload(i, s->rx_buf + 5, payload_len, handler) < 0)
    {
        return -1;
    }

    size_t consumed = wire_need;
    mem.move(s->rx_buf, s->rx_buf + consumed, s->rx_len - consumed);
    s->rx_len -= consumed;
    return 1;
}

int ssh_pkt_recv(uint8_t i, const uint8_t *data, size_t len, ssh_msg_handler_t handler, const SshDir *dir)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshPacketState *s = &ssh_pkt[i];
    SshKeyMat *km = &ssh_keys[i][dir->epoch];

    if (!ssh_pkt_slot_storage(s))
    {
        return -1;
    }

    // Consume the input incrementally: append as much as fits the (single-packet) receive buffer, extract
    // every complete packet to drain it, then append more. So one TCP read carrying several pipelined packets
    // - e.g. a large SFTP write fragmented into back-to-back CHANNEL_DATA messages - is processed instead of
    // being rejected when the read exceeds the region.
    while (len > 0)
    {
        size_t space = SSH_RX_ASM_CAP - s->rx_len;
        if (space == 0)
        {
            // The buffer is full yet no complete packet could be extracted -> a single packet larger than the
            // buffer. Discard and disconnect.
            protocore_secure_wipe(s->rx_buf, s->rx_len);
            s->rx_len = 0;
            return -1;
        }
        size_t take = len < space ? len : space;
        mem.cpy(s->rx_buf + s->rx_len, data, take);
        s->rx_len += take;
        data += take;
        len -= take;

        // Extract complete packets.
        while (s->rx_len >= 4)
        {
            int r = 0;
            const uint8_t recv_cipher = km_recv_cipher(km, s->is_client);
            if (dir->enc && recv_cipher == SSH_CIPHER_CHACHA20POLY1305)
            {
                r = ssh_recv_chachapoly(i, s, km, handler);
            }
            else if (dir->enc && recv_cipher == SSH_CIPHER_AES256GCM)
            {
                r = ssh_recv_aesgcm(i, s, km, handler);
            }
            else if (dir->enc && ssh_mac_is_etm(km_recv_mac_mode(km, s->is_client)))
            {
                r = ssh_recv_ctr_etm(i, s, km, handler);
            }
            else if (dir->enc)
            {
                r = ssh_recv_ctr_emac(i, s, km, handler);
            }
            else
            {
                r = ssh_recv_plain(i, s, km, handler);
            }

            if (r < 0)
            {
                return -1;
            }
            if (r == 0)
            {
                break; // incomplete packet - append more input and retry
            }
        } // extract-complete-packets loop
    } // incremental-append loop

    return 0;
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------

int ssh_pkt_disconnect(uint8_t i, uint32_t reason_code, uint8_t *out, size_t *out_len, size_t out_cap,
                       const SshDir *dir)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }

    // Build SSH_MSG_DISCONNECT payload (RFC 4253 §11.1):
    //   byte    SSH_MSG_DISCONNECT
    //   uint32  reason code
    //   string  description (empty)
    //   string  language tag (empty)
    uint8_t payload[13];
    payload[0] = SSH_MSG_DISCONNECT;
    payload[1] = (uint8_t)(reason_code >> 24);
    payload[2] = (uint8_t)(reason_code >> 16);
    payload[3] = (uint8_t)(reason_code >> 8);
    payload[4] = (uint8_t)(reason_code);
    payload[5] = 0;
    payload[6] = 0;
    payload[7] = 0;
    payload[8] = 0; // empty description
    payload[9] = 0;
    payload[10] = 0;
    payload[11] = 0;
    payload[12] = 0; // empty language

    int rc = ssh_pkt_send(i, payload, sizeof(payload), out, out_len, out_cap, dir);

    // Zero packet state and key material regardless of send success.
    ssh_pkt_init(i);
    ssh_keymat_wipe(i);
    ssh_dh_wipe(i);

    return rc;
}

// ---------------------------------------------------------------------------
// RFC 4253 sec 10 - what a service above this layer may read
// ---------------------------------------------------------------------------

// sec 7.2: "The exchange hash H from the first key exchange is additionally used as the session
// identifier". Later exchanges recompute H and leave this alone, so the identifier a service bound
// to stays bound.
void ssh_session_id_latch(uint8_t i, const uint8_t *h, size_t h_len)
{
    if (i >= MAX_SSH_CONNS || h == NULL || h_len == 0 || h_len > SSH_KEXHASH_MAX_LEN)
    {
        return;
    }
    SshSession *s = &ssh_sess[i];
    if (s->have_session_id || s->session_id == NULL)
    {
        return;
    }
    mem.cpy(s->session_id, h, h_len);
    s->session_id_len = (uint8_t)h_len;
    s->have_session_id = PROTO_TRUE;
}

const uint8_t *ssh_session_id(uint8_t i, size_t *len)
{
    if (i >= MAX_SSH_CONNS || len == NULL || !ssh_sess[i].have_session_id)
    {
        return NULL;
    }
    *len = ssh_sess[i].session_id_len;
    return ssh_sess[i].session_id;
}

// ---------------------------------------------------------------------------
// RFC 4253 sec 11.4 - Reserved Messages
// ---------------------------------------------------------------------------

// byte SSH_MSG_UNIMPLEMENTED, uint32 packet sequence number of rejected message. ssh_pkt_recv has
// already counted the rejected packet, so its number is one below the next one expected.
int ssh_pkt_unimplemented(uint8_t i, uint8_t *out, size_t *out_len, size_t out_cap)
{
    if (i >= MAX_SSH_CONNS || out == NULL || out_len == NULL || out_cap < SSH_UNIMPLEMENTED_LEN)
    {
        return -1;
    }
    const uint32_t rejected = ssh_pkt[i].seq_no_recv - 1u;
    out[0] = SSH_MSG_UNIMPLEMENTED;
    out[1] = (uint8_t)(rejected >> 24);
    out[2] = (uint8_t)(rejected >> 16);
    out[3] = (uint8_t)(rejected >> 8);
    out[4] = (uint8_t)(rejected);
    *out_len = SSH_UNIMPLEMENTED_LEN;
    return 0;
}

// ---------------------------------------------------------------------------
// RFC 4253 sec 8 - diffie-hellman-group14 engine, sec 7.2 key derivation
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// DH key generation
// ---------------------------------------------------------------------------

int ssh_dh_generate(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshDhState *dh = &ssh_dh[i];

    // Generate a random 2048-bit private scalar y.
    // RFC 4253 §8 does not specify a minimum bit-length for y beyond requiring
    // it to be in [1, p-1].  Common practice is a full 2048-bit random value,
    // which ensures the discrete-log is as hard as the group order.
    Rng.fill_args.out = (uint8_t *)dh->y->d;
    Rng.fill_args.len = sizeof(protocore_bignum);
    Rng.fill(protocore_rng_span());

    // Ensure y < p by clearing the two MSBs (conservative; not strictly
    // required since rejection sampling would also work, but a single mask
    // is sufficient because p ≈ 2^2048 and clearing 2 bits keeps y in range).
    dh->y->d[PROTOCORE_BN_LIMBS - 1] &= 0x3FFFFFFFu;
    // Also ensure y > 1 (set bit 1 of the LSB limb to avoid pathological y=0,1).
    dh->y->d[0] |= 0x00000002u;

    // f = g^y mod p  (g = 2 for group-14)
    Bignum.expmod_args.out = dh->f;
    Bignum.expmod_args.base = &group14_g;
    Bignum.expmod_args.exp = dh->y;
    Bignum.expmod_group14(ssh_pkt[i].crypto_work);

    return 0;
}

// ---------------------------------------------------------------------------
// Session key derivation (RFC 4253 §7.2)
// ---------------------------------------------------------------------------

// Hash the shared secret K as an SSH mpint into @p ctx (RFC 4251 §5 / RFC 4253
// §7.2): big-endian, 4-byte length prefix, a leading 0x00 only if the MSB is set,
// and all UNNECESSARY leading 0x00 bytes stripped (canonical form). The exchange
// hash encodes K the same way (hash_mpint), so the KDF must too: if K has any
// high-order zero bytes (~1/256 of handshakes) a spec-compliant peer strips them
// and would otherwise derive different keys.
static void hash_mpint_K(uint8_t *work, const uint8_t K_be[256])
{
    size_t off = 0;
    while (off < 256 && K_be[off] == 0x00u)
    {
        off++;
    }
    if (off == 256) // K == 0: empty mpint (not reachable for a real DH secret)
    {
        uint8_t len_be[4] = {0, 0, 0, 0};
        hash_bytes(work, len_be, 4);
        return;
    }
    proto_bool pad = (K_be[off] & 0x80u) != 0;
    uint32_t mlen = (uint32_t)(256 - off);
    if (pad)
    {
        mlen++;
    }
    uint8_t len_be[4] = {(uint8_t)(mlen >> 24), (uint8_t)(mlen >> 16), (uint8_t)(mlen >> 8), (uint8_t)mlen};
    hash_bytes(work, len_be, 4);
    if (pad)
    {
        uint8_t zero = 0x00u;
        hash_bytes(work, &zero, 1);
    }
    hash_bytes(work, K_be + off, 256 - off);
}

// Hybrid KEX: K is a fixed HASH output (32 for mlkem-sha256, 64 for sntrup761-sha512), hashed as a
// plain SSH string (RFC 4251 §5) - length prefix then the bytes verbatim, NO mpint sign/strip. It
// lives in the last @p klen octets of the right-aligned K_be buffer. H and this KDF encode K the same.
static void hash_string_K(uint8_t *work, const uint8_t K_be[256], size_t klen)
{
    uint8_t len_be[4] = {(uint8_t)(klen >> 24), (uint8_t)(klen >> 16), (uint8_t)(klen >> 8), (uint8_t)klen};
    hash_bytes(work, len_be, 4);
    hash_bytes(work, K_be + (256 - klen), klen);
}

static inline void hash_K(uint8_t *work, const uint8_t K_be[256], proto_bool k_is_string, size_t k_str_len)
{
    if (k_is_string)
    {
        hash_string_K(work, K_be, k_str_len);
    }
    else
    {
        hash_mpint_K(work, K_be);
    }
}

// The caller's region, split by offset the way the slot's own borrow is: the exchange hash works out
// of the front, the K1 || K2 chain accumulates behind it. A caller hands in PROTOCORE_SSH_KDF_BORROW bytes.
#define SSH_KDF_OFF_HASH 0u
#define SSH_KDF_OFF_ACC (SSH_KDF_OFF_HASH + PROTOCORE_SSH_KEXHASH_BORROW)
static_assert(SSH_KDF_OFF_ACC + SSH_KDF_MAX <= PROTOCORE_SSH_KDF_BORROW,
              "PROTOCORE_SSH_KDF_BORROW is short of the exchange-hash region and the K1 || K2 chain - "
              "raise it in protocore_config.h, which sums it into the secure arena");

// RFC 4253 §7.2 key derivation extended to any length, over the KEX method's hash (SHA-256 or
// SHA-512 via SshKexHash / @p is512):
//   K1 = HASH(K || H || X || session_id)   (X = label byte); Ki+1 = HASH(K || H || K1..Ki)
//   key = K1 || K2 || ...   For the first KEX session_id == H; on a re-key it is the first KEX's H.
// @p h_len / @p sid_len are the exchange-hash / session-id lengths. When K is a hybrid string it is
// @p k_str_len octets (the KEX hash length). @p out_len up to SSH_KDF_MAX.
void ssh_kdf_derive(const SshKdfInputs *in, char label, uint8_t *out, size_t out_len)
{
    if (out_len > SSH_KDF_MAX)
    {
        out_len = SSH_KDF_MAX; // bounded: every negotiated algorithm needs <= 64 B today
    }
    uint8_t *work = in->work + SSH_KDF_OFF_HASH;
    uint8_t *acc = in->work + SSH_KDF_OFF_ACC; // K1 || K2 || ... accumulated for the chain hash
    size_t have = 0;

    SshKexHash.init_args.is512 = in->is512;
    SshKexHash.init(work);
    const size_t blk = SshKexHash.len;       // 32 or 64, the bound hash's own length
    const size_t k_str_len = SshKexHash.len; // a hybrid K is one HASH output wide
    hash_K(work, in->K_be, in->k_is_string, k_str_len);
    hash_bytes(work, in->H, in->h_len);
    uint8_t lbl = (uint8_t)label;
    hash_bytes(work, &lbl, 1);
    hash_bytes(work, in->session_id, in->sid_len);
    SshKexHash.final_args.out = acc; // acc[0..blk-1] = K1
    SshKexHash.final(work);
    have = blk;

    // have + blk > SSH_KDF_MAX (loop exit via the right operand) is unreachable: blk is only ever 32 or
    // 64 (the bound hash's length) and SSH_KDF_MAX is 128, an exact multiple of both; out_len is already
    // clamped to <= SSH_KDF_MAX above, and have only grows in whole increments of blk starting at blk -
    // so whenever have < out_len (<= SSH_KDF_MAX) it is at most SSH_KDF_MAX - blk, and this half is
    // always true.
    while (have < out_len && have + blk <= SSH_KDF_MAX)
    {
        SshKexHash.init_args.is512 = in->is512;
        SshKexHash.init(work);
        hash_K(work, in->K_be, in->k_is_string, k_str_len);
        hash_bytes(work, in->H, in->h_len);
        hash_bytes(work, acc, have); // all prior blocks
        SshKexHash.final_args.out = acc + have;
        SshKexHash.final(work);
        have += blk;
    }
    mem.cpy(out, acc, out_len);
    protocore_secure_wipe(acc, SSH_KDF_MAX); // the cipher key, the IV and both MAC keys pass through here
}

// Install one direction's key material into the connection's own keymat. RFC 4253 sec 7.2 fixes the
// labels by direction - client to server takes 'A' (IV), 'C' (key) and 'E' (MAC), server to client
// takes 'B', 'D' and 'F' - and that direction's negotiated cipher decides which of them it needs.
// Every value is derived into the slot that owns it, so none of it is staged anywhere else.
static void install_direction(SshKeyMat *km, const SshKdfInputs *in, proto_bool c2s, uint8_t cipher_alg,
                              uint8_t mac_alg)
{
    char iv_label = 'B';
    char key_label = 'D';
    char mac_label = 'F';
    uint8_t *chacha_key = km->chacha_key_s2c;
    uint8_t *gcm_ctx = km->gcm_ctx_s2c;
    uint8_t *aes_key = km->aes_key_s2c;
    uint8_t *aes_iv = km->aes_iv_s2c;
    uint8_t *mac_key = km->mac_key_s2c;
    if (c2s)
    {
        iv_label = 'A';
        key_label = 'C';
        mac_label = 'E';
        chacha_key = km->chacha_key_c2s;
        gcm_ctx = km->gcm_ctx_c2s;
        aes_key = km->aes_key_c2s;
        aes_iv = km->aes_iv_c2s;
        mac_key = km->mac_key_c2s;
    }

    if (cipher_alg == SSH_CIPHER_CHACHA20POLY1305)
    {
        // A 512-bit key (K_main || K_header) from the sec 7.2 extension chain; no IV, no MAC key.
        ssh_kdf_derive(in, key_label, chacha_key, PROTOCORE_CHACHAPOLY_KEY_LEN);
        return;
    }

    // The IV field takes the leading bytes of the derived stream, which is what the KDF copies.
    ssh_kdf_derive(in, iv_label, aes_iv, PROTOCORE_AES256CTR_CTR_LEN);

    if (cipher_alg == SSH_CIPHER_AES256GCM)
    {
        // RFC 5647: this mode keeps only the schedule, so the key lands in aes_key - which GCM does not
        // otherwise use - becomes the keyed context, and is wiped. The nonce is the low 12 IV bytes.
        ssh_kdf_derive(in, key_label, aes_key, PROTOCORE_AES256CTR_KEY_LEN);
        AesGcm.key_args.key = aes_key;
        AesGcm.key_init(gcm_ctx);
        protocore_secure_wipe(aes_key, PROTOCORE_AES256CTR_KEY_LEN);
        return;
    }

    // aes256-ctr keeps the raw key and the running counter; the schedule is rebuilt per packet in the
    // shared crypto scratch. It is the only cipher that also needs a separate MAC key.
    ssh_kdf_derive(in, key_label, aes_key, PROTOCORE_AES256CTR_KEY_LEN);
    ssh_kdf_derive(in, mac_label, mac_key, ssh_mac_len(mac_alg));
}

void ssh_kex_install_keys(uint8_t i, const SshKdfInputs *in)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    // The exchange hash and this KDF work out of the slot's own bytes, the same borrow the wire and
    // the packet MAC come from.
    if (!ssh_pkt_slot_storage(&ssh_pkt[i]))
    {
        return;
    }
    // RFC 4253 sec 7.3: install into the epoch neither direction is reading, so the live keys keep
    // decrypting until each direction's NEWKEYS moves it across.
    SshKeyMat *km = &ssh_keys[i][1u - ssh_sess[i].out.epoch];
    const SshSession *s = &ssh_sess[i]; // the connection holds what its two directions negotiated
    // The working bytes come from the slot, not the caller: they exist only once the borrow above has
    // been taken, so a caller could not name them.
    SshKdfInputs kin = *in;
    kin.work = ssh_pkt[i].crypto_work;
    // An abandoned re-key leaves this epoch's contexts standing. Release each before its mode is
    // overwritten, after which the outgoing mode is no longer knowable.
    if (km->active && km->cipher_mode_c2s == SSH_CIPHER_AES256GCM)
    {
        AesGcm.key_wipe(km->gcm_ctx_c2s);
    }
    if (km->active && km->cipher_mode_s2c == SSH_CIPHER_AES256GCM)
    {
        AesGcm.key_wipe(km->gcm_ctx_s2c);
    }
    km->cipher_mode_c2s = s->cipher_alg_c2s;
    km->cipher_mode_s2c = s->cipher_alg_s2c;
    km->mac_mode_c2s = s->mac_alg_c2s;
    km->mac_mode_s2c = s->mac_alg_s2c;

    install_direction(km, &kin, PROTO_TRUE, s->cipher_alg_c2s, s->mac_alg_c2s);
    install_direction(km, &kin, PROTO_FALSE, s->cipher_alg_s2c, s->mac_alg_s2c);
    km->active = PROTO_TRUE;
}
// ---------------------------------------------------------------------------
// RFC 4253 sec 7.2 - key material
// ---------------------------------------------------------------------------
SshKeyMat ssh_keys[MAX_SSH_CONNS][2];
SshDhState ssh_dh[MAX_SSH_CONNS];

// ---------------------------------------------------------------------------
// RFC 4253 sec 6 - the outbound seam every layer above frames through
// ---------------------------------------------------------------------------

// All SSH server-layer state, owned by one instance (internal linkage): the packet-emit
// Build and emit SSH_MSG_DISCONNECT("too many authentication failures") - the reason code, the
// description string, and an empty language tag (RFC 4253 §11.1) - then the caller closes.
// RFC 4253 sec 11.1: byte SSH_MSG_DISCONNECT, uint32 reason code, string description, string
// language tag. The language tag is empty, per sec 11.1's note that it SHOULD be sent as such.
int ssh_pkt_build_disconnect(uint32_t reason_code, const char *desc, size_t desc_len, uint8_t *out, size_t *out_len,
                             size_t cap)
{
    if (out == NULL || out_len == NULL || desc == NULL)
    {
        return -1;
    }
    const size_t need = 13u + desc_len; // 1 msg + 4 reason + 4 desc length + desc + 4 language tag
    if (cap < need)
    {
        return -1; // fail closed: no room for the whole message, so build none of it
    }
    size_t o = 0;
    out[o++] = SSH_MSG_DISCONNECT;
    out[o++] = (uint8_t)(reason_code >> 24); // reason code (uint32, big-endian)
    out[o++] = (uint8_t)(reason_code >> 16);
    out[o++] = (uint8_t)(reason_code >> 8);
    out[o++] = (uint8_t)(reason_code);
    out[o++] = (uint8_t)(desc_len >> 24); // description (uint32 length + bytes)
    out[o++] = (uint8_t)(desc_len >> 16);
    out[o++] = (uint8_t)(desc_len >> 8);
    out[o++] = (uint8_t)(desc_len);
    mem.cpy(out + o, desc, desc_len);
    o += desc_len;
    out[o++] = 0; // language tag: empty string (4-byte length 0)
    out[o++] = 0;
    out[o++] = 0;
    out[o++] = 0;
    *out_len = o;
    return 0;
}

// ---------------------------------------------------------------------------
// RFC 4253 - message numbers 1 to 49
// ---------------------------------------------------------------------------

int ssh_transport_dispatch(uint8_t i, uint8_t msg_type, const uint8_t *payload, size_t len)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshSession *s = &ssh_sess[i];

    // The reply buffer is borrowed for this dispatch, not carried on the worker stack: it is the
    // single largest frame on the SSH path and the handshake below it is the deepest call chain in
    // the library. protocore_plaintext_span binds the capacity to the allocation.
    size_t mark = protocore_plaintext_mark();
    protocore_span reply = protocore_plaintext_span(SSH_PKT_BUF_SIZE, 16);
    if (!span.ok(reply))
    {
        protocore_plaintext_release(mark);
        return -1; // arena exhausted: fail closed, the caller drops the connection
    }
    size_t n = 0;

    switch (msg_type)
    {
    case SSH_MSG_IGNORE:
    case SSH_MSG_DEBUG:         // RFC 4253 sec 11.3: display or discard; never answered
    case SSH_MSG_UNIMPLEMENTED: // sec 11.4: answering it with another would loop
        protocore_plaintext_release(mark);
        return 0;

    case SSH_MSG_DISCONNECT:
        protocore_plaintext_release(mark);
        return -1; // peer is closing

    case SSH_MSG_KEXINIT:
        // Initial KEX or an in-session re-key request. Negotiate, reply with our own KEXINIT unless
        // this one already was the reply to ours, generate a fresh ephemeral, and await KEXDH_INIT.
        SshTransport.slot = i;
        SshTransport.pkt.payload = payload;
        SshTransport.pkt.len = len;
        ssh_kexinit_parse(protocore_ssh_transport_span());
        if (SshTransport.i32 != 0)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        // RFC 4253 sec 7.1: "a party MUST respond with its own SSH_MSG_KEXINIT message, except when
        // the received SSH_MSG_KEXINIT already was a reply." Answering a reply would both break that
        // and rebuild I_S with a fresh cookie after the peer has hashed the old one.
        PhaseMachine.kexinit_needs_reply_args.i = i;
        PhaseMachine.kexinit_needs_reply(phase_machine_work);
        if (!PhaseMachine.ok)
        {
            SshTransport.slot = i;
            ssh_kex_generate(protocore_ssh_transport_span());
            if (SshTransport.i32 != 0)
            {
                protocore_plaintext_release(mark);
                return -1;
            }
            protocore_plaintext_release(mark);
            return 0;
        }
        SshTransport.slot = i;
        SshTransport.out_args.out = reply.buf;
        SshTransport.out_args.cap = reply.cap;
        ssh_kexinit_build(protocore_ssh_transport_span());
        n = SshTransport.out_args.out_len;
        if (SshTransport.i32 != 0)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        SshNetwork.ssh_slot = i;
        SshNetwork.msg.payload = reply.buf;
        SshNetwork.msg.len = n;
        SshNetwork.emit(protocore_ssh_network_span());
        SshTransport.slot = i;
        ssh_kex_generate(protocore_ssh_transport_span());
        if (SshTransport.i32 != 0)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        protocore_plaintext_release(mark);
        return 0;

    case SSH_MSG_KEXDH_INIT:
        PhaseMachine.admits_kexdh_init_args.i = i;
        PhaseMachine.admits_kexdh_init(phase_machine_work);
        if (!PhaseMachine.ok)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        // RFC 4253 sec 7.1: the client sent this ahead of the reply on a guess that lost negotiation,
        // so it belongs to an algorithm nobody agreed on. Drop it and wait for the real one.
        if (s->drop_guessed_kex_pkt)
        {
            s->drop_guessed_kex_pkt = PROTO_FALSE;
            protocore_plaintext_release(mark);
            return 0;
        }
        SshTransport.slot = i;
        SshTransport.pkt.payload = payload;
        SshTransport.pkt.len = len;
        SshTransport.out_args.out = reply.buf;
        SshTransport.out_args.cap = reply.cap;
        ssh_kexdh_handle(protocore_ssh_transport_span());
        n = SshTransport.out_args.out_len;
        if (SshTransport.i32 != 0)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        SshNetwork.ssh_slot = i;
        SshNetwork.msg.payload = reply.buf;
        SshNetwork.msg.len = n;
        SshNetwork.emit(protocore_ssh_network_span()); // KEXDH_REPLY
        {
            uint8_t newkeys = SSH_MSG_NEWKEYS;
            SshNetwork.ssh_slot = i;
            SshNetwork.msg.payload = &newkeys;
            SshNetwork.msg.len = 1;
            SshNetwork.emit(protocore_ssh_network_span()); // server NEWKEYS (this one still goes out unencrypted)
            SshTransport.slot = i;
            ssh_newkeys_sent(
                protocore_ssh_transport_span()); // ...but our outbound is encrypted from the next packet on
        }
        protocore_plaintext_release(mark);
        return 0; // ssh_kexdh_handle advanced phase to NEWKEYS

    case SSH_MSG_NEWKEYS:
        // RFC 4253 sec 7.3: a NEWKEYS that ends no key exchange is a protocol error, so the
        // connection goes down rather than switching keys.
        SshTransport.slot = i;
        ssh_newkeys_complete(protocore_ssh_transport_span());
        if (SshTransport.i32 != 0) // activate encryption; → SERVICE or OPEN
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        // RFC 8308: with encryption now active, advertise the signature algorithms
        // we accept for pubkey userauth (server-sig-algs) so a modern client will
        // sign an RSA key - it otherwise reports "no mutual signature algorithm".
        // First encrypted message, before the client's SERVICE_REQUEST.
        // RFC 8308 sec 2.4 gives a server two places to send it, and the first NEWKEYS is the one used
        // here. A re-key re-reads ext-info-c from the new KEXINIT, so without this the message would go
        // out again on every re-key.
        // Staged and invoked ahead of the condition: hoisting the call into the `&&` would run it
        // even when the two flags say the message is not due.
        proto_bool ext_info_built = PROTO_FALSE;
        if (s->ext_info_enabled && !s->ext_info_sent)
        {
            Extension.build_args.out = reply.buf;
            Extension.build_args.len = &n;
            Extension.build_args.cap = reply.cap;
            Extension.build(extension_work);
            ext_info_built = (Extension.n == 0);
        }
        if (ext_info_built)
        {
            s->ext_info_sent = PROTO_TRUE;
            SshNetwork.ssh_slot = i;
            SshNetwork.msg.payload = reply.buf;
            SshNetwork.msg.len = n;
            SshNetwork.emit(protocore_ssh_network_span()); // fail: EXT_INFO is ~90 bytes and buf is SSH_PKT_BUF_SIZE
        }
        protocore_plaintext_release(mark);
        return 0;

    case SSH_MSG_EXT_INFO:
        protocore_plaintext_release(mark);
        return 0; // RFC 8308: a client may send its own EXT_INFO; we ignore it

    case SSH_MSG_SERVICE_REQUEST:
        // RFC 4253 §10: a service request is only valid once the key exchange has completed (NEWKEYS
        // advances the phase to SSH_PHASE_SERVICE and turns on encryption). Rejecting it in any earlier
        // phase stops a client from jumping from DH_INIT straight to userauth in cleartext, skipping the
        // whole key exchange + host-key verification. Found by the pentest's ssh_msgtype_abuse.
        PhaseMachine.admits_service_request_args.i = i;
        PhaseMachine.admits_service_request(phase_machine_work);
        if (!PhaseMachine.ok)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        // sec 10: "If the server rejects the service request, it SHOULD send an appropriate
        // SSH_MSG_DISCONNECT message and MUST disconnect."
        if (ssh_transport_service_request(payload, len, reply.buf, &n, reply.cap) != 0)
        {
            static const char desc[] = "service not available";
            size_t dn = 0;
            if (ssh_pkt_build_disconnect(SSH_DISCONNECT_SERVICE_NOT_AVAILABLE, desc, sizeof(desc) - 1, reply.buf, &dn,
                                         reply.cap) == 0)
            {
                SshNetwork.ssh_slot = i;
                SshNetwork.msg.payload = reply.buf;
                SshNetwork.msg.len = dn;
                SshNetwork.emit(protocore_ssh_network_span());
            }
            protocore_plaintext_release(mark);
            return -1;
        }
        SshNetwork.ssh_slot = i;
        SshNetwork.msg.payload = reply.buf;
        SshNetwork.msg.len = n;
        SshNetwork.emit(protocore_ssh_network_span());
        PhaseMachine.service_done_args.i = i;
        PhaseMachine.service_done(phase_machine_work);
        protocore_plaintext_release(mark);
        return 0;

    default:
        break;
    }
    protocore_plaintext_release(mark);
    // 50 and above are not ours: sec 6 hands them to the authentication protocol.
    SshAuth.slot = i;
    SshAuth.msg_type = msg_type;
    SshAuth.msg.payload = payload;
    SshAuth.msg.len = len;
    SshAuth.dispatch(protocore_ssh_auth_span());
    return SshAuth.i32;
}

// ---------------------------------------------------------------------------
// Service request (RFC 4253 §10)
// ---------------------------------------------------------------------------

int ssh_transport_service_request(const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap)
{
    if (len < 1 || payload[0] != SSH_MSG_SERVICE_REQUEST)
    {
        return -1;
    }

    size_t off = 1;
    char svc[32];
    if (!read_string(payload, len, &off, svc, sizeof(svc)))
    {
        return -1;
    }
    if (!str.eq(svc, "ssh-userauth", sizeof(svc), PROTO_FALSE))
    {
        return -1;
    }

    // SERVICE_ACCEPT: byte(6) || string("ssh-userauth")
    static const char name[] = "ssh-userauth";
    uint32_t nl = (uint32_t)(sizeof(name) - 1);
    if (cap < 1 + 4 + nl)
    {
        return -1;
    }
    out[0] = SSH_MSG_SERVICE_ACCEPT;
    endian.wr32be(out + 1, nl);
    mem.cpy(out + 5, name, nl);
    *out_len = 5 + nl;
    return 0;
}

// ---------------------------------------------------------------------------
// Public key algorithms (RFC 4253 sec 6.6)
// ---------------------------------------------------------------------------

// True when @p blob holds a public key in one of the formats this build decodes.
// RFC 4252 sec 7: "Any public key algorithm may be offered for use in authentication. In particular,
// the list is not constrained by what was negotiated during key exchange. If the server does not
// support some algorithm, it MUST simply reject the request." These four are what this end verifies,
// and they are what ssh_extinfo_build advertises as server-sig-algs.
//
// The name also has to agree with the blob it arrived with, since the blob's own type string is what
// selects the parser: parse_rsa_pubkey reads a string("ssh-rsa") blob under either RSA signature
// name, parse_ed25519_pubkey a string("ssh-ed25519"), parse_ecdsa_pubkey a string("ecdsa-sha2-nistp256").
proto_bool ssh_kex_shared_secret(const SshKexEphemeral *e, const uint8_t *peer_pub, uint32_t peer_pub_len,
                                 uint8_t k_be[256])
{
    if (e == NULL || peer_pub == NULL)
    {
        return PROTO_FALSE;
    }
    mem.set(k_be, 0, 256);
    switch (e->alg)
    {
    case SSH_KEX_CURVE25519: {
        if (peer_pub_len != 32)
        {
            return PROTO_FALSE;
        }
        uint8_t k32[32];
        Curve25519.x25519_args.scalar = e->priv;
        Curve25519.x25519_args.point = peer_pub;
        Curve25519.x25519_args.out = k32;
        Curve25519.x25519(e->work);
        // Reject a low-order peer point (all-zero shared secret) - RFC 7748 sec 6.1.
        uint8_t zacc = 0;
        for (int b = 0; b < 32; b++)
        {
            zacc |= k32[b];
        }
        if (zacc == 0)
        {
            protocore_secure_wipe(k32, 32);
            return PROTO_FALSE;
        }
        mem.cpy(k_be + (256 - 32), k32, 32);
        protocore_secure_wipe(k32, 32);
        return PROTO_TRUE;
    }
    case SSH_KEX_ECDH_NISTP256: {
        if (peer_pub_len != PROTOCORE_ECDSA_P256_PUB_LEN)
        {
            return PROTO_FALSE;
        }
        uint8_t k32[PROTOCORE_ECDSA_P256_COORD_LEN];
        Ecdsa.ecdh_args.peer_pub = peer_pub;
        Ecdsa.ecdh_args.priv = e->priv;
        Ecdsa.ecdh_args.shared_x = k32;
        Ecdsa.ecdh(e->work);
        if (!Ecdsa.ok)
        {
            return PROTO_FALSE;
        }
        mem.cpy(k_be + (256 - 32), k32, 32);
        protocore_secure_wipe(k32, 32);
        return PROTO_TRUE;
    }
    case SSH_KEX_DH_GROUP14: {
        protocore_bignum f, x, K;
        Bignum.from_bytes_args.out = &f;
        Bignum.from_bytes_args.bytes = peer_pub;
        Bignum.from_bytes_args.len = peer_pub_len;
        Bignum.from_bytes(e->work);
        Bignum.validate_args.v = &f;
        Bignum.dh_validate(e->work); // ok = valid (1 < f < p-1)
        if (!Bignum.ok)
        {
            return PROTO_FALSE;
        }
        Bignum.from_bytes_args.out = &x;
        Bignum.from_bytes_args.bytes = e->priv;
        Bignum.from_bytes_args.len = 32;
        Bignum.from_bytes(e->work);
        Bignum.expmod_args.out = &K;
        Bignum.expmod_args.base = &f;
        Bignum.expmod_args.exp = &x;
        Bignum.expmod_group14(e->work);
        Bignum.to_bytes_args.bytes = k_be;
        Bignum.to_bytes_args.in = &K;
        Bignum.to_bytes(e->work);
        protocore_secure_wipe(&x, sizeof(x));
        protocore_secure_wipe(&K, sizeof(K));
        return PROTO_TRUE;
    }
#if PROTOCORE_ENABLE_PQC_KEX
    case SSH_KEX_MLKEM768_X25519: {
        // S_REPLY = ciphertext(1088) || Q_S(32). Decaps recovers K_PQ; X25519 gives K_CL. The hybrid's
        // combined secret K = SHA256(K_PQ || K_CL) is a fixed 32-byte string (right-aligned in k_be).
        if (peer_pub_len != MLKEM768_CT_BYTES + 32 || e->work == NULL || e->hybrid_sk == NULL)
        {
            return PROTO_FALSE;
        }
        uint8_t k_pq[32], k_cl[32];
        MlKem.decaps_args.dk = e->hybrid_sk;
        MlKem.decaps_args.ct = peer_pub;
        MlKem.decaps_args.ss = k_pq;
        MlKem.decaps(e->work);
        Curve25519.x25519_args.scalar = e->priv;
        Curve25519.x25519_args.point = peer_pub + MLKEM768_CT_BYTES;
        Curve25519.x25519_args.out = k_cl;
        Curve25519.x25519(e->work);
        // Reject a low-order peer point (all-zero shared secret) - RFC 7748 sec 6.1.
        uint8_t zacc = 0;
        for (int b = 0; b < 32; b++)
        {
            zacc |= k_cl[b];
        }
        if (zacc == 0)
        {
            protocore_secure_wipe(k_pq, sizeof(k_pq));
            protocore_secure_wipe(k_cl, sizeof(k_cl));
            return PROTO_FALSE;
        }
        Sha256.init(e->work);
        Sha256.update_args.data = k_pq;
        Sha256.update_args.len = 32;
        Sha256.update(e->work);
        Sha256.update_args.data = k_cl;
        Sha256.update_args.len = 32;
        Sha256.update(e->work);
        Sha256.final_args.out = k_be + (256 - 32);
        Sha256.final(e->work);
        protocore_secure_wipe(k_pq, sizeof(k_pq));
        protocore_secure_wipe(k_cl, sizeof(k_cl));
        return PROTO_TRUE;
    }
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
    case SSH_KEX_SNTRUP761_X25519: {
        // S_REPLY = ciphertext(1039) || Q_S(32). Decaps recovers K_PQ; X25519 gives K_CL. The combined
        // secret K = SHA512(K_PQ || K_CL) is a fixed 64-byte string (right-aligned in k_be).
        if (peer_pub_len != PROTOCORE_SNTRUP761_CT_BYTES + 32 || e->work == NULL || e->hybrid_sk == NULL)
        {
            return PROTO_FALSE;
        }
        uint8_t k_pq[PROTOCORE_SNTRUP761_SS_BYTES], k_cl[32];
        Sntrup761.dec_args.sk = e->hybrid_sk;
        Sntrup761.dec_args.ct = peer_pub;
        Sntrup761.dec_args.ss = k_pq;
        Sntrup761.dec(e->work);
        Curve25519.x25519_args.scalar = e->priv;
        Curve25519.x25519_args.point = peer_pub + PROTOCORE_SNTRUP761_CT_BYTES;
        Curve25519.x25519_args.out = k_cl;
        Curve25519.x25519(e->work);
        // Reject a low-order peer point (all-zero shared secret) - RFC 7748 sec 6.1.
        uint8_t zacc = 0;
        for (int b = 0; b < 32; b++)
        {
            zacc |= k_cl[b];
        }
        if (zacc == 0)
        {
            protocore_secure_wipe(k_pq, sizeof(k_pq));
            protocore_secure_wipe(k_cl, sizeof(k_cl));
            return PROTO_FALSE;
        }
        Sha512.init(e->work);
        Sha512.update_args.data = k_pq;
        Sha512.update_args.len = sizeof(k_pq);
        Sha512.update(e->work);
        Sha512.update_args.data = k_cl;
        Sha512.update_args.len = 32;
        Sha512.update(e->work);
        Sha512.final_args.out = k_be + (256 - 64);
        Sha512.final(e->work);
        protocore_secure_wipe(k_pq, sizeof(k_pq));
        protocore_secure_wipe(k_cl, sizeof(k_cl));
        return PROTO_TRUE;
    }
#endif
    }
    return PROTO_FALSE;
}

proto_bool ssh_pubkey_algo_supported(const char *pk_algo, const uint8_t *blob, uint32_t blob_len)
{
    if (pk_algo == NULL || blob == NULL || blob_len < 4)
    {
        return PROTO_FALSE;
    }
    const char *blob_type = NULL;
    if (str.eq(pk_algo, HOSTKEY_ED, SSH_AUTH_ALGO_MAX, PROTO_FALSE))
    {
        blob_type = "ssh-ed25519";
    }
    else if (str.eq(pk_algo, HOSTKEY_ECDSA, SSH_AUTH_ALGO_MAX, PROTO_FALSE))
    {
        blob_type = "ecdsa-sha2-nistp256";
    }
    else if (str.eq(pk_algo, SSH_RSA_SIG_ALG_SHA256, SSH_AUTH_ALGO_MAX, PROTO_FALSE) ||
             str.eq(pk_algo, SSH_RSA_SIG_ALG_SHA512, SSH_AUTH_ALGO_MAX, PROTO_FALSE))
    {
        blob_type = "ssh-rsa";
    }
    else
    {
        return PROTO_FALSE; // an algorithm this end does not verify
    }

    const uint32_t tlen = (uint32_t)str.len(blob_type, SSH_AUTH_ALGO_MAX);
    return blob_len >= 4u + tlen && endian.rd32be(blob) == tlen && mem.cmp(blob + 4, blob_type, tlen) == 0;
}

proto_bool ssh_pubkey_blob_valid(const uint8_t *blob, uint32_t blob_len)
{
    proto_bool is_ed = blob_len >= 4 + 11 && mem.cmp(blob,
                                                     "\x00\x00\x00\x0b"
                                                     "ssh-ed25519",
                                                     4 + 11) == 0;
    proto_bool is_ecdsa = blob_len >= 4 + 19 && mem.cmp(blob,
                                                        "\x00\x00\x00\x13"
                                                        "ecdsa-sha2-nistp256",
                                                        4 + 19) == 0;
    size_t mark = protocore_plaintext_mark();
    protocore_span n_be = protocore_plaintext_span(PROTOCORE_RSA_KEY_BYTES, 4);
    protocore_span e_be = protocore_plaintext_span(4, 4);
    protocore_span ed_pub = protocore_plaintext_span(32, 4);
    protocore_span ec_pub = protocore_plaintext_span(PROTOCORE_ECDSA_P256_PUB_LEN, 4);
    if (!span.ok(n_be) || !span.ok(e_be) || !span.ok(ed_pub) || !span.ok(ec_pub))
    {
        protocore_plaintext_release(mark);
        return PROTO_FALSE;
    }
    proto_bool parsed = PROTO_FALSE;
    if (is_ed)
    {
        parsed = parse_ed25519_pubkey(blob, blob_len, ed_pub.buf);
    }
    else if (is_ecdsa)
    {
        parsed = parse_ecdsa_pubkey(blob, blob_len, ec_pub.buf);
    }
    else
    {
        parsed = parse_rsa_pubkey(blob, blob_len, n_be.buf, e_be.buf);
    }
    protocore_plaintext_release(mark);
    return parsed;
}

// Verify @p sig over @p signed_data against the public key in @p blob, out of slot @p i's crypto_work.
// The key type comes from the blob; @p pk_algo only steers the RSA signature hash (RFC 8332).
proto_bool ssh_pubkey_verify(uint8_t i, const char *pk_algo, const uint8_t *blob, uint32_t blob_len, const uint8_t *sig,
                             uint32_t sig_len, const uint8_t *signed_data, size_t signed_len)
{
    if (i >= MAX_SSH_CONNS)
    {
        return PROTO_FALSE;
    }
    proto_bool is_ed = blob_len >= 4 + 11 && mem.cmp(blob,
                                                     "\x00\x00\x00\x0b"
                                                     "ssh-ed25519",
                                                     4 + 11) == 0;
    proto_bool is_ecdsa = blob_len >= 4 + 19 && mem.cmp(blob,
                                                        "\x00\x00\x00\x13"
                                                        "ecdsa-sha2-nistp256",
                                                        4 + 19) == 0;
    // Borrowed for this dispatch rather than carried on the worker stack. This sits on the deepest
    // call chain in the library (dispatch -> auth -> ed25519 verify -> ed_add), so the key material
    // and the signature staging buffer are what drive the worker stack requirement.
    size_t mark = protocore_plaintext_mark();
    protocore_span n_be = protocore_plaintext_span(PROTOCORE_RSA_KEY_BYTES, 4);
    protocore_span e_be = protocore_plaintext_span(4, 4);
    protocore_span ed_pub = protocore_plaintext_span(32, 4);
    protocore_span ec_pub = protocore_plaintext_span(PROTOCORE_ECDSA_P256_PUB_LEN, 4);
    if (!span.ok(n_be) || !span.ok(e_be) || !span.ok(ed_pub) || !span.ok(ec_pub))
    {
        protocore_plaintext_release(mark);
        return PROTO_FALSE;
    }
    proto_bool parsed = PROTO_FALSE;
    if (is_ed)
    {
        parsed = parse_ed25519_pubkey(blob, blob_len, ed_pub.buf);
    }
    else if (is_ecdsa)
    {
        parsed = parse_ecdsa_pubkey(blob, blob_len, ec_pub.buf);
    }
    else
    {
        parsed = parse_rsa_pubkey(blob, blob_len, n_be.buf, e_be.buf);
    }
    if (!parsed)
    {
        protocore_plaintext_release(mark);
        return PROTO_FALSE;
    }

    // For RSA the signature hash is chosen by the client's algorithm name (RFC 8332), not the key
    // blob: rsa-sha2-512 -> SHA-512, otherwise SHA-256.
    protocore_rsa_hash rh = PROTOCORE_RSA_HASH_SHA256;
    if (str.eq(pk_algo, SSH_RSA_SIG_ALG_SHA512, SSH_AUTH_ALGO_MAX, PROTO_FALSE))
    {
        rh = PROTOCORE_RSA_HASH_SHA512;
    }
    if (!ssh_pkt_slot_storage(&ssh_pkt[i]))
    {
        protocore_plaintext_release(mark);
        return PROTO_FALSE;
    }
    uint8_t *work = ssh_pkt[i].crypto_work;
    proto_bool sig_ok = PROTO_FALSE;
    if (is_ed)
    {
        Ed25519.verify_args.pub = ed_pub.buf;
        Ed25519.verify_args.msg = signed_data;
        Ed25519.verify_args.msg_len = signed_len;
        Ed25519.verify_args.sig = sig;
        Ed25519.verify(work);
        sig_ok = sig_len == 64 && Ed25519.ok;
    }
    else if (is_ecdsa)
    {
        protocore_span ec_sig = protocore_plaintext_span(PROTOCORE_ECDSA_P256_SIG_LEN, 4);
        sig_ok =
            span.ok(ec_sig) && parse_ecdsa_sig(sig, sig_len, ec_sig.buf) &&
            (Ecdsa.verify_args.pub = ec_pub.buf, Ecdsa.verify_args.msg = signed_data,
             Ecdsa.verify_args.mlen = signed_len, Ecdsa.verify_args.sig = ec_sig.buf, Ecdsa.verify(work), Ecdsa.ok);
    }
    else
    {
        Rsa.verify_args.n = n_be.buf;
        Rsa.verify_args.e = e_be.buf;
        Rsa.verify_args.msg = signed_data;
        Rsa.verify_args.msg_len = signed_len;
        Rsa.verify_args.sig = sig;
        Rsa.verify_args.sig_len = sig_len;
        Rsa.verify_args.hash = rh;
        Rsa.verify(work);
        sig_ok = Rsa.ok;
    }
    protocore_plaintext_release(mark);
    return sig_ok;
}

// ---------------------------------------------------------------------------
// Key re-exchange (RFC 4253 sec 9)
// ---------------------------------------------------------------------------

// Once the volume (packet-count proxy) or time budget since the last KEX is spent and the channel is
// not already re-keying, emit a fresh KEXINIT so a long-lived / high-throughput session re-keys in
// place instead of being dropped at the sequence-number wrap. The KEXINIT dispatch carries it on.
void ssh_transport_key_re_exchange(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    SshSession *s = &ssh_sess[i];
    PhaseMachine.admits_rekey_args.i = i;
    PhaseMachine.admits_rekey(phase_machine_work);
    if (!PhaseMachine.ok)
    {
        return;
    }
    uint32_t elapsed = Clock.ms - s->last_kex_ms;
    SshTransport.rekey.seq_send = ssh_pkt[i].seq_no_send;
    SshTransport.rekey.seq_recv = ssh_pkt[i].seq_no_recv;
    SshTransport.rekey.elapsed_ms = elapsed;
    SshTransport.rekey.pkt_threshold = SSH_REKEY_PACKET_THRESHOLD;
    SshTransport.rekey.time_threshold_ms = SSH_REKEY_TIME_MS;
    ssh_rekey_due(protocore_ssh_transport_span());
    if (!SshTransport.ok)
    {
        return;
    }
    size_t mark = protocore_secure_mark();
    uint8_t *buf = (uint8_t *)protocore_secure_alloc(SSH_PKT_BUF_SIZE, 16);
    size_t n = 0;
    SshTransport.slot = i;
    SshTransport.out_args.out = buf;
    SshTransport.out_args.cap = SSH_PKT_BUF_SIZE;
    if (buf)
    {
        ssh_transport_begin_rekey(protocore_ssh_transport_span());
        n = SshTransport.out_args.out_len;
    }
    if (buf && SshTransport.i32 == 0)
    {
        SshNetwork.ssh_slot = i;
        SshNetwork.msg.payload = buf;
        SshNetwork.msg.len = n;
        SshNetwork.emit(protocore_ssh_network_span());
    }
    protocore_secure_release(mark);
}

// ---------------------------------------------------------------------------
// Protocol version exchange (RFC 4253 sec 4.2)
// ---------------------------------------------------------------------------

// Take the peer identification string off @p buf. Returns 1 once it is whole and the phase has
// advanced, 0 while more bytes are needed, -1 on a string the section does not admit. @p off is
// left at the first byte of the binary packet protocol that follows.
int ssh_transport_version_exchange_recv(uint8_t i, const uint8_t *buf, size_t n, size_t *off)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    PhaseMachine.admits_ident_args.i = i;
    PhaseMachine.admits_ident(phase_machine_work);
    if (!PhaseMachine.ok)
    {
        return 1;
    }
    SshTransport.slot = i;
    SshTransport.pkt.data = buf;
    SshTransport.pkt.len = n;
    ssh_transport_recv_ident(protocore_ssh_transport_span());
    const size_t consumed = SshTransport.pkt.consumed;
    const int rc = SshTransport.i32;
    if (rc < 0)
    {
        return -1;
    }
    if (rc == 0)
    {
        return 0;
    }
    *off = consumed;
    return 1;
}
