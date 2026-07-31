// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_client.cpp
 * @brief Outbound SSH client + reverse tunnel state machine (see ssh_client.h).
 *
 * Client-role driver over the shipped transport primitives: it reuses the role-aware binary packet
 * layer (ssh_pkt_* with ssh_pkt_set_client), the curve25519 / ed25519 / chacha-poly crypto, and the
 * RFC 4253 §7.2 KDF. Only the client-side handshake, auth, and forward logic lives here.
 */

#include "network_drivers/presentation/ssh/connection/ssh_client.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder
#include "server/mmgr/secure.h"

#if PC_ENABLE_SSH_CLIENT

#include "crypto/asymmetric/bignum.h"     // bn_expmod_group14 (dh-group14 client)
#include "crypto/asymmetric/curve25519.h" // pc_x25519 (curve25519-sha256)
#include "crypto/asymmetric/ecdsa.h"      // ecdh-sha2-nistp256 + ecdsa host-key verify
#include "crypto/asymmetric/ed25519.h"    // ssh-ed25519 host key + client auth
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/ssh/crypto/ssh_kexhash.h"   // SshKexHash (SHA-256/SHA-512 by method)
#include "network_drivers/presentation/ssh/crypto/ssh_rsa.h"       // rsa-sha2-256/512 host-key verify
#include "network_drivers/presentation/ssh/transport/ssh_dh.h"     // ssh_dh_derive_keys_sid, ssh_rng_fill
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h" // ssh_keys[], SshKeyMat, SSH_CIPHER_*, SSH_MAC_*
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "shared_primitives/log.h"
#include <string.h>

#if PC_ENABLE_PQC_KEX
#include "crypto/pqc/mlkem.h" // mlkem768x25519-sha256 hybrid (client: KeyGen + Decaps)
#endif
#if PC_ENABLE_SSH_SNTRUP761
#include "crypto/pqc/sntrup761.h" // sntrup761x25519-sha512 hybrid (client: KeyGen + Decaps)
#endif
#if PC_ENABLE_PQC_KEX || PC_ENABLE_SSH_SNTRUP761
#include "server/mmgr/scratch.h" // scratch_alloc for the large hybrid C_INIT
#endif

#if defined(ARDUINO)
#include "network_drivers/session/worker.h"   // pc_worker_set_self (own scratch slot)
#include "network_drivers/transport/client.h" // pc_client_*
#include "services/system/clock.h"            // pc_millis, pcdelay
#endif

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const char CLIENT_BANNER[] = "SSH-2.0-PC_client_1.0";

// Algorithm names, in the client's preference order per category (first = most preferred). The
// client offers every algorithm and negotiates whatever the relay supports. It reuses the transport
// crypto, so each primitive uses whatever hardware the target provides: RSA/DH on the MPI unit,
// AES/SHA on their units, and NIST P-256 (ecdh-sha2-nistp256 + ecdsa-sha2-nistp256) on the ECC /
// ECDSA accelerators where they exist - on the P4 mbedTLS's ecc_alt/ecdsa_alt route to them
// (esp_ecc_point_multiply / esp_ecdsa_verify), HW-measured ~5 ms/point-mul. curve25519/ed25519 and
// ML-KEM-768 have no hardware path on any ESP32 (the ECC unit does only NIST prime curves), so those
// run in software on every variant.
static const char NAME_ED25519[] = "ssh-ed25519";
// KEX_NAMES and KEX_OF (defined after CliKex, below) are index-aligned: negotiate() returns an index
// into KEX_NAMES, KEX_OF maps it to the CliKex. The PQ/T hybrid leads when built (PQC-preferred).
static const char *const KEX_NAMES[] = {
#if PC_ENABLE_PQC_KEX
    "mlkem768x25519-sha256",
#endif
#if PC_ENABLE_SSH_SNTRUP761
    "sntrup761x25519-sha512@openssh.com",
#endif
    "curve25519-sha256",
    "curve25519-sha256@libssh.org",
    "ecdh-sha2-nistp256",
    "diffie-hellman-group14-sha256"};
static const char *const HOSTKEY_NAMES[] = {"ssh-ed25519", "ecdsa-sha2-nistp256", "rsa-sha2-512", "rsa-sha2-256"};
static const char *const CIPHER_NAMES[] = {"chacha20-poly1305@openssh.com", "aes256-gcm@openssh.com", "aes256-ctr"};
static const char *const MAC_NAMES[] = {"hmac-sha2-256-etm@openssh.com", "hmac-sha2-256",
                                        "hmac-sha2-512-etm@openssh.com", "hmac-sha2-512"};

/** @brief Negotiated key-exchange method. */
enum class CliKex : uint8_t
{
    CURVE25519,
    ECDH_P256,
    DH_GROUP14,
#if PC_ENABLE_PQC_KEX
    MLKEM768_X25519, ///< mlkem768x25519-sha256 PQ/T hybrid (PC_ENABLE_PQC_KEX).
#endif
#if PC_ENABLE_SSH_SNTRUP761
    SNTRUP761_X25519, ///< sntrup761x25519-sha512@openssh.com PQ/T hybrid (PC_ENABLE_SSH_SNTRUP761).
#endif
};
// Index-aligned with KEX_NAMES above (two curve25519 spellings map to the same method).
static const CliKex KEX_OF[] = {
#if PC_ENABLE_PQC_KEX
    CliKex::MLKEM768_X25519,
#endif
#if PC_ENABLE_SSH_SNTRUP761
    CliKex::SNTRUP761_X25519,
#endif
    CliKex::CURVE25519,       CliKex::CURVE25519, CliKex::ECDH_P256, CliKex::DH_GROUP14};
// The exchange hash + RFC 4253 sec 7.2 KDF run over SHA-512 for the -sha512 methods
// (sntrup761x25519-sha512), SHA-256 for every other method (RFC 4253 sec 8).
static inline bool cli_kex_is_sha512(CliKex k)
{
#if PC_ENABLE_SSH_SNTRUP761
    return k == CliKex::SNTRUP761_X25519;
#else
    (void)k;
    return false;
#endif
}
/** @brief Negotiated host-key / signature algorithm. */
enum class CliHostkey : uint8_t
{
    ED25519,
    ECDSA_P256,
    RSA_SHA512,
    RSA_SHA256
};

#define SSH_CLI_SLOT 0 // the SSH packet/key pool slot the client borrows (MAX_SSH_CONNS >= 1)

// ---------------------------------------------------------------------------
// Wire helpers (SSH data types, RFC 4251 §5)
// ---------------------------------------------------------------------------

namespace
{
struct Wr
{
    uint8_t *buf;
    size_t cap;
    size_t off;
    bool ok;
};

void w_u8(Wr *w, uint8_t v)
{
    if (w->off + 1 > w->cap)
    {
        w->ok = false;
        return;
    }
    w->buf[w->off++] = v;
}
void w_u32(Wr *w, uint32_t v)
{
    if (w->off + 4 > w->cap)
    {
        w->ok = false;
        return;
    }
    w->buf[w->off++] = (uint8_t)(v >> 24);
    w->buf[w->off++] = (uint8_t)(v >> 16);
    w->buf[w->off++] = (uint8_t)(v >> 8);
    w->buf[w->off++] = (uint8_t)v;
}
void w_bytes(Wr *w, const uint8_t *d, size_t n)
{
    if (w->off + n > w->cap)
    {
        w->ok = false;
        return;
    }
    memcpy(w->buf + w->off, d, n);
    w->off += n;
}
void w_string(Wr *w, const void *d, size_t n)
{
    w_u32(w, (uint32_t)n);
    w_bytes(w, (const uint8_t *)d, n);
}
void w_cstr(Wr *w, const char *s)
{
    w_string(w, s, strnlen(s, w->cap)); // a field can never exceed the writer's own capacity
}

// Reader over a payload with bounds checking.
struct Rd
{
    const uint8_t *buf;
    size_t len;
    size_t off;
    bool ok;
};
uint8_t r_u8(Rd *r)
{
    if (r->off + 1 > r->len)
    {
        r->ok = false;
        return 0;
    }
    return r->buf[r->off++];
}
uint32_t r_u32(Rd *r)
{
    if (r->off + 4 > r->len)
    {
        r->ok = false;
        return 0;
    }
    uint32_t v = ((uint32_t)r->buf[r->off] << 24) | ((uint32_t)r->buf[r->off + 1] << 16) |
                 ((uint32_t)r->buf[r->off + 2] << 8) | (uint32_t)r->buf[r->off + 3];
    r->off += 4;
    return v;
}
// Returns a pointer to an in-place string of length *n; advances past it. Fails closed on overflow.
const uint8_t *r_string(Rd *r, uint32_t *n)
{
    uint32_t l = r_u32(r);
    if (!r->ok || r->off + l > r->len)
    {
        r->ok = false;
        *n = 0;
        return nullptr;
    }
    const uint8_t *p = r->buf + r->off;
    r->off += l;
    *n = l;
    return p;
}

// Does a comma-separated SSH name-list contain @p want as a whole entry?
bool namelist_has(const uint8_t *list, uint32_t len, const char *want)
{
    size_t wl = strnlen(want, (size_t)len + 1); // a whole-entry match cannot exceed the list
    uint32_t start = 0;
    for (uint32_t i = 0; i <= len; i++)
    {
        if (i == len || list[i] == ',')
        {
            if (i - start == wl && memcmp(list + start, want, wl) == 0)
            {
                return true;
            }
            start = i + 1;
        }
    }
    return false;
}

// Hash an SSH string (u32 length + bytes) into the running exchange hash (SHA-256 or SHA-512).
void hash_string(SshKexHash *c, const uint8_t *d, size_t n)
{
    uint8_t l[4] = {(uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n};
    ssh_kexhash_update(c, l, 4);
    ssh_kexhash_update(c, d, n);
}
// Hash an SSH mpint (two's-complement, minimal, leading 0x00 when the high bit is set).
void hash_mpint(SshKexHash *c, const uint8_t *be, size_t n)
{
    size_t i = 0;
    while (i < n && be[i] == 0)
    {
        i++; // strip leading zeros
    }
    size_t mlen = n - i;
    bool pad = (mlen > 0 && (be[i] & 0x80) != 0);
    uint8_t l4[4] = {0, 0, 0, (uint8_t)(mlen + (pad ? 1 : 0))};
    l4[2] = (uint8_t)((mlen + (pad ? 1 : 0)) >> 8);
    ssh_kexhash_update(c, l4, 4);
    if (pad)
    {
        uint8_t z = 0;
        ssh_kexhash_update(c, &z, 1);
    }
    if (mlen)
    {
        ssh_kexhash_update(c, be + i, mlen);
    }
}
} // namespace

// ---------------------------------------------------------------------------
// Client session state
// ---------------------------------------------------------------------------

enum class CliPhase : uint8_t
{
    IDLE,
    BANNER,   ///< reading the server identification string.
    KEXINIT,  ///< sent our KEXINIT; awaiting the server's.
    KEXREPLY, ///< sent KEXDH_INIT; awaiting KEXDH_REPLY.
    NEWKEYS,  ///< sent our NEWKEYS; awaiting the server's.
    SERVICE,  ///< sent SERVICE_REQUEST; awaiting SERVICE_ACCEPT.
    AUTH,     ///< sent USERAUTH_REQUEST; awaiting SUCCESS/FAILURE.
    FORWARD,  ///< sent tcpip-forward; awaiting REQUEST_SUCCESS.
    OPEN,     ///< tunnel up; servicing forwarded-tcpip channels.
    FAILED
};

// How many forwarded-tcpip connections the tunnel bridges at once (PC_SSH_CLIENT_MAX_CHANNELS,
// defaulted per variant in protocore_config.h / the board profile): a relay forwarding to a web UI opens
// one channel per inbound TCP connection, so a pool (not a single slot) avoids rapid / concurrent
// requests getting "administratively prohibited". Each slot costs a CliChannel + a pc_client conn.

// One forwarded-tcpip channel bridged to a local TCP connection.
struct CliChannel
{
    bool used;
    uint32_t local_id;  ///< our channel id.
    uint32_t remote_id; ///< the relay's channel id.
    uint32_t send_win;  ///< bytes we may still send to the relay (their window to us).
    uint32_t recv_win;  ///< bytes the relay may still send us before we WINDOW_ADJUST.
    int local_cid;      ///< pc_client id of the bridged local TCP connection, or -1.
    bool eof_sent;
    bool relay_eof; ///< the relay half-closed (peer done sending); tear down once the response drains.
};

struct SshClientCtx
{
    pc_ssh_tunnel_cfg cfg;
    CliPhase phase;
    pc_ssh_tunnel_state state;

    int cid; ///< relay TCP connection (pc_client), or -1.
    uint32_t deadline_ms;

    CliKex kex;         ///< negotiated key exchange.
    CliHostkey hostkey; ///< negotiated host-key / signature type.
    uint8_t cipher;     ///< negotiated SSH_CIPHER_*.
    uint8_t mac;        ///< negotiated SSH_MAC_* (used only when cipher == aes256-ctr).

    uint8_t kex_priv[32]; ///< our KEX private: X25519 scalar / P-256 d / DH exponent (wiped after K).
    uint8_t qc[256];      ///< our KEX public Q_C: 32 (curve/hybrid X25519) / 65 (ecdh) / 256 (DH e, big-endian).
    size_t qc_len;
#if PC_ENABLE_PQC_KEX || PC_ENABLE_SSH_SNTRUP761
    // Hybrid decapsulation key: persists from C_INIT (KeyGen) to S_REPLY (Decaps) across round-trips,
    // so it cannot live in the per-dispatch scratch arena. Only one hybrid is negotiated per session, so
    // ML-KEM's dk and sntrup761's sk share storage. Each embeds its public key (ek at [1152..], pk at
    // PC_SNTRUP761_SK_PK_OFFSET), so the C_INIT / exchange-hash reconstruct it from here, not stored twice.
    union {
#if PC_ENABLE_PQC_KEX
        uint8_t mlkem_dk[MLKEM768_DK_BYTES];
#endif
#if PC_ENABLE_SSH_SNTRUP761
        uint8_t sntrup_sk[PC_SNTRUP761_SK_BYTES];
#endif
    } hyb;
#endif

    char v_s[256]; ///< server identification string (no CR LF).
    uint16_t v_s_len;
    uint8_t banner[256]; ///< inbound banner accumulator.
    uint16_t banner_len;

    uint8_t i_c[768]; ///< our KEXINIT payload (for H) - the full advertised suite is ~520 bytes.
    uint16_t i_c_len;
    uint8_t i_s[SSH_KEXINIT_MAX]; ///< server KEXINIT payload (for H); OpenSSH's is ~1.1 KB.
    uint16_t i_s_len;

    uint8_t session_id[SSH_KEXHASH_MAX_LEN]; ///< 32 (SHA-256 methods) or 64 (sntrup761 SHA-512).
    uint8_t session_id_len;
    bool have_sid;

    CliChannel chan[PC_SSH_CLIENT_MAX_CHANNELS]; ///< the active forwarded channels.
    uint32_t next_chan_id;                       ///< id to assign the next channel.

    uint8_t wire[SSH_WIRE_CAP]; ///< staging buffer for one outgoing (framed/encrypted) packet.
};

static SshClientCtx s_cli;

// Find an active channel by the id we assigned it (inbound messages address our local_id), or nullptr.
static CliChannel *chan_by_local(uint32_t local_id)
{
    for (int i = 0; i < PC_SSH_CLIENT_MAX_CHANNELS; i++)
    {
        if (s_cli.chan[i].used && s_cli.chan[i].local_id == local_id)
        {
            return &s_cli.chan[i];
        }
    }
    return nullptr;
}

// Claim a free channel slot, or nullptr if all are in use.
static CliChannel *chan_alloc(void)
{
    for (int i = 0; i < PC_SSH_CLIENT_MAX_CHANNELS; i++)
    {
        if (!s_cli.chan[i].used)
        {
            return &s_cli.chan[i];
        }
    }
    return nullptr;
}

#if defined(ARDUINO)

// ---------------------------------------------------------------------------
// Transmit
// ---------------------------------------------------------------------------

// Frame @p payload as a binary packet (encrypted once NEWKEYS is active) and write it to the relay.
static bool cli_send(const uint8_t *payload, size_t len)
{
    size_t wlen = 0;
    if (ssh_pkt_send(SSH_CLI_SLOT, payload, len, s_cli.wire, &wlen, sizeof(s_cli.wire)) != 0)
    {
        return false;
    }
    return pc_client_send(s_cli.cid, s_cli.wire, wlen);
}

static void cli_fail(const char *why)
{
    PC_LOGW("ssh-tunnel: %s", why);
    s_cli.phase = CliPhase::FAILED;
    s_cli.state = pc_ssh_tunnel_state::PC_TUN_FAILED;
    for (int i = 0; i < PC_SSH_CLIENT_MAX_CHANNELS; i++)
    {
        if (s_cli.chan[i].used && s_cli.chan[i].local_cid >= 0)
        {
            pc_client_close(s_cli.chan[i].local_cid);
        }
    }
    if (s_cli.cid >= 0)
    {
        pc_client_close(s_cli.cid);
    }
    s_cli.cid = -1;
    ssh_keymat_wipe(SSH_CLI_SLOT);
    pc_secure_wipe(s_cli.kex_priv, sizeof(s_cli.kex_priv));
}

// ---------------------------------------------------------------------------
// Algorithm negotiation + KEXINIT (client)
// ---------------------------------------------------------------------------

// Write a comma-joined SSH name-list from @p names into the writer as one string.
static void w_namelist(Wr *w, const char *const *names, size_t n)
{
    char tmp[256];
    size_t o = 0;
    for (size_t i = 0; i < n; i++)
    {
        size_t l = strnlen(names[i], sizeof(tmp));
        if (i && o + 1 <= sizeof(tmp))
        {
            tmp[o++] = ',';
        }
        if (o + l <= sizeof(tmp))
        {
            memcpy(tmp + o, names[i], l);
            o += l;
        }
    }
    w_string(w, tmp, o);
}

// RFC 4253 §7.1: the negotiated algorithm is the first on the CLIENT's list that the server also
// offers. Returns the client-preference index, or -1 if there is no overlap.
static int negotiate(const uint8_t *slist, uint32_t slen, const char *const *prefs, size_t nprefs)
{
    for (size_t i = 0; i < nprefs; i++)
    {
        if (namelist_has(slist, slen, prefs[i]))
        {
            return (int)i;
        }
    }
    return -1;
}

// Copy an mpint's value (the string content) right-aligned, big-endian, into out[width]; strips a
// leading 0x00 sign byte. Returns false if the magnitude does not fit @p width.
static bool mpint_to_fixed(const uint8_t *v, uint32_t vlen, uint8_t *out, size_t width)
{
    uint32_t i = 0;
    while (i < vlen && v[i] == 0)
    {
        i++;
    }
    uint32_t mag = vlen - i;
    if (mag > width)
    {
        return false;
    }
    memset(out, 0, width);
    memcpy(out + (width - mag), v + i, mag);
    return true;
}

static bool build_kexinit(void)
{
    Wr w = {s_cli.i_c, sizeof(s_cli.i_c), 0, true};
    w_u8(&w, SSH_MSG_KEXINIT);
    uint8_t cookie[16];
    ssh_rng_fill(cookie, 16);
    w_bytes(&w, cookie, 16);
    w_namelist(&w, KEX_NAMES, sizeof(KEX_NAMES) / sizeof(KEX_NAMES[0]));             // kex
    w_namelist(&w, HOSTKEY_NAMES, sizeof(HOSTKEY_NAMES) / sizeof(HOSTKEY_NAMES[0])); // host key
    w_namelist(&w, CIPHER_NAMES, sizeof(CIPHER_NAMES) / sizeof(CIPHER_NAMES[0]));    // enc c2s
    w_namelist(&w, CIPHER_NAMES, sizeof(CIPHER_NAMES) / sizeof(CIPHER_NAMES[0]));    // enc s2c
    w_namelist(&w, MAC_NAMES, sizeof(MAC_NAMES) / sizeof(MAC_NAMES[0]));             // mac c2s
    w_namelist(&w, MAC_NAMES, sizeof(MAC_NAMES) / sizeof(MAC_NAMES[0]));             // mac s2c
    w_cstr(&w, "none");                                                              // comp c2s
    w_cstr(&w, "none");                                                              // comp s2c
    w_cstr(&w, "");                                                                  // lang c2s
    w_cstr(&w, "");                                                                  // lang s2c
    w_u8(&w, 0);                                                                     // first_kex_packet_follows
    w_u32(&w, 0);                                                                    // reserved
    if (!w.ok)
    {
        return false;
    }
    s_cli.i_c_len = (uint16_t)w.off;
    return cli_send(s_cli.i_c, s_cli.i_c_len);
}

// Generate our KEX ephemeral for the negotiated method and build Q_C / e into s_cli.qc.
static bool build_kex_public(void)
{
    switch (s_cli.kex)
    {
    case CliKex::CURVE25519:
        ssh_rng_fill(s_cli.kex_priv, 32);
        pc_x25519_base(s_cli.qc, s_cli.kex_priv);
        s_cli.qc_len = 32;
        return true;
    case CliKex::ECDH_P256:
        // Draw a valid P-256 scalar (pubkey derivation rejects 0 / >= group order).
        for (int tries = 0; tries < 8; tries++)
        {
            ssh_rng_fill(s_cli.kex_priv, 32);
            if (pc_ecdsa_p256_pubkey(s_cli.qc, s_cli.kex_priv))
            {
                s_cli.qc_len = PC_ECDSA_P256_PUB_LEN; // 65
                return true;
            }
        }
        return false;
    case CliKex::DH_GROUP14: {
        // e = g^x mod p, g = 2 (RFC 3526 group 14). x is a 256-bit exponent.
        ssh_rng_fill(s_cli.kex_priv, 32);
        pc_bignum g, x, e;
        uint8_t two = 2;
        bn_from_bytes(&g, &two, 1);
        bn_from_bytes(&x, s_cli.kex_priv, 32);
        bn_expmod_group14(&e, &g, &x);
        bn_to_bytes(s_cli.qc, &e);
        s_cli.qc_len = 256;
        pc_secure_wipe(&x, sizeof(x));
        pc_secure_wipe(&e, sizeof(e));
        return true;
    }
#if PC_ENABLE_PQC_KEX
    case CliKex::MLKEM768_X25519: {
        // ML-KEM-768 keypair (dk kept for Decaps; ek is embedded in dk) + an X25519 ephemeral. C_INIT
        // (ek || Q_C) is assembled at send time; Q_C lives in qc[0..31].
        uint8_t d[32], z[32], ek[MLKEM768_EK_BYTES];
        ssh_rng_fill(d, sizeof(d));
        ssh_rng_fill(z, sizeof(z));
        pc_mlkem768_keygen(d, z, ek, s_cli.hyb.mlkem_dk);
        pc_secure_wipe(d, sizeof(d));
        pc_secure_wipe(z, sizeof(z));
        pc_secure_wipe(ek, sizeof(ek)); // ek persists inside mlkem_dk
        ssh_rng_fill(s_cli.kex_priv, 32);
        pc_x25519_base(s_cli.qc, s_cli.kex_priv);
        s_cli.qc_len = 32;
        return true;
    }
#endif
#if PC_ENABLE_SSH_SNTRUP761
    case CliKex::SNTRUP761_X25519: {
        // sntrup761 keypair (sk kept for Decaps; pk is embedded in sk) + an X25519 ephemeral. C_INIT
        // (pk || Q_C) is assembled at send time from sk; Q_C lives in qc[0..31]. pk is only needed
        // transiently here (sk embeds a copy), so it borrows the scratch arena.
        size_t mark = scratch_mark();
        uint8_t *pk = (uint8_t *)scratch_alloc(PC_SNTRUP761_PK_BYTES, 1);
        if (!pk)
        {
            return false;
        }
        pc_sntrup761_keypair(pk, s_cli.hyb.sntrup_sk);
        scratch_release(mark); // pk persists inside sntrup_sk at PC_SNTRUP761_SK_PK_OFFSET
        ssh_rng_fill(s_cli.kex_priv, 32);
        pc_x25519_base(s_cli.qc, s_cli.kex_priv);
        s_cli.qc_len = 32;
        return true;
    }
#endif
    }
    return false;
}

// Parse the server KEXINIT, negotiate every category, store I_S, and send KEXDH_INIT.
static bool handle_server_kexinit(const uint8_t *p, size_t len)
{
    if (len > sizeof(s_cli.i_s))
    {
        return false;
    }
    memcpy(s_cli.i_s, p, len);
    s_cli.i_s_len = (uint16_t)len;

    Rd r = {p, len, 0, true};
    r_u8(&r);    // msg type
    r.off += 16; // cookie
    uint32_t kn, hn, cn, mn;
    const uint8_t *kex = r_string(&r, &kn);
    const uint8_t *hk = r_string(&r, &hn);
    const uint8_t *ec = r_string(&r, &cn); // enc c2s
    r_string(&r, &cn);                     // enc s2c (same set advertised)
    const uint8_t *mc = r_string(&r, &mn); // mac c2s
    if (!r.ok)
    {
        return false;
    }

    int ki = negotiate(kex, kn, KEX_NAMES, sizeof(KEX_NAMES) / sizeof(KEX_NAMES[0]));
    int hi = negotiate(hk, hn, HOSTKEY_NAMES, sizeof(HOSTKEY_NAMES) / sizeof(HOSTKEY_NAMES[0]));
    int ci = negotiate(ec, cn, CIPHER_NAMES, sizeof(CIPHER_NAMES) / sizeof(CIPHER_NAMES[0]));
    if (ki < 0 || hi < 0 || ci < 0)
    {
        return false;
    }

    s_cli.kex = KEX_OF[ki];
    s_cli.hostkey = (CliHostkey)hi; // HOSTKEY_NAMES order == CliHostkey order
    static const uint8_t cipher_of[] = {SSH_CIPHER_CHACHA20POLY1305, SSH_CIPHER_AES256GCM, SSH_CIPHER_AES256CTR};
    s_cli.cipher = cipher_of[ci];
    PC_LOGI("ssh-tunnel: negotiated kex=%s hostkey=%s cipher=%s", KEX_NAMES[ki], HOSTKEY_NAMES[hi], CIPHER_NAMES[ci]);

    s_cli.mac = SSH_MAC_HMAC_SHA256;
    if (s_cli.cipher == SSH_CIPHER_AES256CTR)
    {
        int mi = negotiate(mc, mn, MAC_NAMES, sizeof(MAC_NAMES) / sizeof(MAC_NAMES[0]));
        if (mi < 0)
        {
            return false;
        }
        static const uint8_t mac_of[] = {SSH_MAC_HMAC_SHA256_ETM, SSH_MAC_HMAC_SHA256, SSH_MAC_HMAC_SHA512_ETM,
                                         SSH_MAC_HMAC_SHA512};
        s_cli.mac = mac_of[mi];
    }

    if (!build_kex_public())
    {
        return false;
    }

#if PC_ENABLE_PQC_KEX
    if (s_cli.kex == CliKex::MLKEM768_X25519)
    {
        // KEX_HYBRID_INIT (msg 30): string(C_INIT) where C_INIT = ek || Q_C (1216 B). Too large for
        // the stack packet buffer, so build it in the client's scratch arena.
        const uint8_t *ek = s_cli.hyb.mlkem_dk + 1152; // ek follows the 1152-byte dk_pke in dk
        const size_t clen = MLKEM768_EK_BYTES + 32;
        const size_t plen = 1 + 4 + clen;
        size_t mark = scratch_mark();
        uint8_t *out = (uint8_t *)scratch_alloc(plen, 1);
        if (!out)
        {
            return false;
        }
        Wr w = {out, plen, 0, true};
        w_u8(&w, SSH_MSG_KEXDH_INIT);
        w_u32(&w, (uint32_t)clen);
        w_bytes(&w, ek, MLKEM768_EK_BYTES);
        w_bytes(&w, s_cli.qc, 32);
        bool ok = w.ok && cli_send(out, w.off);
        scratch_release(mark);
        return ok;
    }
#endif
#if PC_ENABLE_SSH_SNTRUP761
    if (s_cli.kex == CliKex::SNTRUP761_X25519)
    {
        // KEX_HYBRID_INIT (msg 30): string(C_INIT) where C_INIT = sntrup761_pk || Q_C (1190 B). pk is
        // reconstructed from sk (it is embedded there); too large for the stack packet buffer, so the
        // packet is built in the client's scratch arena.
        const uint8_t *pk = s_cli.hyb.sntrup_sk + PC_SNTRUP761_SK_PK_OFFSET;
        const size_t clen = PC_SNTRUP761_PK_BYTES + 32;
        const size_t plen = 1 + 4 + clen;
        size_t mark = scratch_mark();
        uint8_t *out = (uint8_t *)scratch_alloc(plen, 1);
        if (!out)
        {
            return false;
        }
        Wr w = {out, plen, 0, true};
        w_u8(&w, SSH_MSG_KEXDH_INIT);
        w_u32(&w, (uint32_t)clen);
        w_bytes(&w, pk, PC_SNTRUP761_PK_BYTES);
        w_bytes(&w, s_cli.qc, 32);
        bool ok = w.ok && cli_send(out, w.off);
        scratch_release(mark);
        return ok;
    }
#endif

    // KEXDH_INIT (msg 30): string(Q_C) for curve/ecdh, mpint(e) for DH.
    uint8_t out[1 + 4 + 260];
    Wr w = {out, sizeof(out), 0, true};
    w_u8(&w, SSH_MSG_KEXDH_INIT);
    if (s_cli.kex == CliKex::DH_GROUP14)
    {
        // mpint(e): minimal, with a sign byte if the top bit is set.
        uint32_t i = 0;
        while (i < s_cli.qc_len && s_cli.qc[i] == 0)
        {
            i++;
        }
        size_t mag = s_cli.qc_len - i;
        bool pad = (mag > 0 && (s_cli.qc[i] & 0x80) != 0);
        w_u32(&w, (uint32_t)(mag + (pad ? 1 : 0)));
        if (pad)
        {
            w_u8(&w, 0);
        }
        w_bytes(&w, s_cli.qc + i, mag);
    }
    else
    {
        w_string(&w, s_cli.qc, s_cli.qc_len);
    }
    return w.ok && cli_send(out, w.off);
}

// ---------------------------------------------------------------------------
// KEXDH_REPLY: compute K, exchange hash H, verify the host signature, derive keys, send NEWKEYS
// ---------------------------------------------------------------------------

// Compute the shared secret K (right-aligned into k_be[256]) for the negotiated method.
static bool compute_k(const uint8_t *srv_pub, uint32_t srv_pub_len, uint8_t k_be[256])
{
    memset(k_be, 0, 256);
    switch (s_cli.kex)
    {
    case CliKex::CURVE25519: {
        if (srv_pub_len != 32)
        {
            return false;
        }
        uint8_t k32[32];
        pc_x25519(k32, s_cli.kex_priv, srv_pub);
        memcpy(k_be + (256 - 32), k32, 32);
        pc_secure_wipe(k32, 32);
        return true;
    }
    case CliKex::ECDH_P256: {
        if (srv_pub_len != PC_ECDSA_P256_PUB_LEN)
        {
            return false;
        }
        uint8_t k32[PC_ECDSA_P256_COORD_LEN];
        if (!pc_ecdsa_p256_ecdh(k32, srv_pub, s_cli.kex_priv))
        {
            return false;
        }
        memcpy(k_be + (256 - 32), k32, 32);
        pc_secure_wipe(k32, 32);
        return true;
    }
    case CliKex::DH_GROUP14: {
        pc_bignum f, x, K;
        bn_from_bytes(&f, srv_pub, srv_pub_len);
        if (bn_dh_validate(&f) != 0) // 0 = valid (1 < f < p-1)
        {
            return false;
        }
        bn_from_bytes(&x, s_cli.kex_priv, 32);
        bn_expmod_group14(&K, &f, &x);
        bn_to_bytes(k_be, &K);
        pc_secure_wipe(&x, sizeof(x));
        pc_secure_wipe(&K, sizeof(K));
        return true;
    }
#if PC_ENABLE_PQC_KEX
    case CliKex::MLKEM768_X25519: {
        // S_REPLY = ciphertext(1088) || Q_S(32). Decaps recovers K_PQ; X25519 gives K_CL. The hybrid's
        // combined secret K = SHA256(K_PQ || K_CL) is a fixed 32-byte string (right-aligned in k_be).
        if (srv_pub_len != MLKEM768_CT_BYTES + 32)
        {
            return false;
        }
        uint8_t k_pq[32], k_cl[32];
        pc_mlkem768_decaps(s_cli.hyb.mlkem_dk, srv_pub, k_pq);
        pc_x25519(k_cl, s_cli.kex_priv, srv_pub + MLKEM768_CT_BYTES);
        pc_sha256_ctx c;
        pc_sha256_init(&c);
        pc_sha256_update(&c, k_pq, 32);
        pc_sha256_update(&c, k_cl, 32);
        pc_sha256_final(&c, k_be + (256 - 32));
        pc_secure_wipe(k_pq, sizeof(k_pq));
        pc_secure_wipe(k_cl, sizeof(k_cl));
        return true;
    }
#endif
#if PC_ENABLE_SSH_SNTRUP761
    case CliKex::SNTRUP761_X25519: {
        // S_REPLY = ciphertext(1039) || Q_S(32). Decaps recovers K_PQ; X25519 gives K_CL. The combined
        // secret K = SHA512(K_PQ || K_CL) is a fixed 64-byte string (right-aligned in k_be).
        if (srv_pub_len != PC_SNTRUP761_CT_BYTES + 32)
        {
            return false;
        }
        uint8_t k_pq[PC_SNTRUP761_SS_BYTES], k_cl[32];
        pc_sntrup761_dec(s_cli.hyb.sntrup_sk, srv_pub, k_pq);
        pc_x25519(k_cl, s_cli.kex_priv, srv_pub + PC_SNTRUP761_CT_BYTES);
        pc_sha512_ctx c;
        pc_sha512_init(&c);
        pc_sha512_update(&c, k_pq, sizeof(k_pq));
        pc_sha512_update(&c, k_cl, 32);
        pc_sha512_final(&c, k_be + (256 - 64));
        pc_secure_wipe(k_pq, sizeof(k_pq));
        pc_secure_wipe(k_cl, sizeof(k_cl));
        return true;
    }
#endif
    }
    return false;
}

// Compute the exchange hash H over the negotiated method's field encodings (RFC 4253 §8 / RFC 8731),
// under the method's hash (SHA-256, or SHA-512 for sntrup761x25519-sha512). Returns the digest length.
static size_t compute_h(const uint8_t *ks, uint32_t ks_len, const uint8_t *srv_pub, uint32_t srv_pub_len,
                        const uint8_t *k_be, uint8_t H[SSH_KEXHASH_MAX_LEN])
{
    const bool is512 = cli_kex_is_sha512(s_cli.kex);
    SshKexHash c;
    ssh_kexhash_init(&c, is512);
    hash_string(&c, (const uint8_t *)CLIENT_BANNER, strnlen(CLIENT_BANNER, sizeof(CLIENT_BANNER))); // V_C
    hash_string(&c, (const uint8_t *)s_cli.v_s, s_cli.v_s_len);                                     // V_S
    hash_string(&c, s_cli.i_c, s_cli.i_c_len);                                                      // I_C
    hash_string(&c, s_cli.i_s, s_cli.i_s_len);                                                      // I_S
    hash_string(&c, ks, ks_len);                                                                    // K_S
#if PC_ENABLE_PQC_KEX || PC_ENABLE_SSH_SNTRUP761
    bool hybrid = false;
    const uint8_t *cpk = nullptr; // the hybrid public embedded in the C_INIT string (ek / sntrup761 pk)
    size_t cpk_len = 0, k_slen = 0;
#if PC_ENABLE_PQC_KEX
    if (s_cli.kex == CliKex::MLKEM768_X25519)
    {
        hybrid = true;
        cpk = s_cli.hyb.mlkem_dk + 1152; // ek follows the 1152-byte dk_pke
        cpk_len = MLKEM768_EK_BYTES;
        k_slen = 32; // K = SHA256(K_PQ || K_CL), 32-byte string
    }
#endif
#if PC_ENABLE_SSH_SNTRUP761
    if (s_cli.kex == CliKex::SNTRUP761_X25519)
    {
        hybrid = true;
        cpk = s_cli.hyb.sntrup_sk + PC_SNTRUP761_SK_PK_OFFSET;
        cpk_len = PC_SNTRUP761_PK_BYTES;
        k_slen = 64; // K = SHA512(K_PQ || K_CL), 64-byte string
    }
#endif
    if (hybrid)
    {
        // C_INIT = cpk || Q_C hashed as one SSH string; S_REPLY (ct || Q_S) is srv_pub; K is a fixed
        // 32/64-byte string, not an mpint (draft-ietf-sshm-mlkem-hybrid-kex / RFC 9370).
        uint32_t clen = (uint32_t)(cpk_len + 32);
        uint8_t lb[4] = {(uint8_t)(clen >> 24), (uint8_t)(clen >> 16), (uint8_t)(clen >> 8), (uint8_t)clen};
        ssh_kexhash_update(&c, lb, 4);
        ssh_kexhash_update(&c, cpk, cpk_len);
        ssh_kexhash_update(&c, s_cli.qc, 32);
        hash_string(&c, srv_pub, srv_pub_len);          // S_REPLY
        hash_string(&c, k_be + (256 - k_slen), k_slen); // K (32/64-byte string)
    }
    else
#endif
        if (s_cli.kex == CliKex::DH_GROUP14)
    {
        hash_mpint(&c, s_cli.qc, s_cli.qc_len); // e
        hash_mpint(&c, srv_pub, srv_pub_len);   // f
        hash_mpint(&c, k_be, 256);              // K
    }
    else
    {
        hash_string(&c, s_cli.qc, s_cli.qc_len); // Q_C
        hash_string(&c, srv_pub, srv_pub_len);   // Q_S
        hash_mpint(&c, k_be, 256);               // K
    }
    return ssh_kexhash_final(&c, H);
}

// Verify the relay's signature over H (h_len bytes) with the host key from K_S, per the host-key type.
static bool verify_host_sig(const uint8_t *ks, uint32_t ks_len, const uint8_t *sig, uint32_t sig_len, const uint8_t *H,
                            size_t h_len)
{
    Rd rk = {ks, ks_len, 0, true};
    uint32_t tn;
    const uint8_t *ktype = r_string(&rk, &tn);
    Rd rs = {sig, sig_len, 0, true};
    uint32_t sn;
    const uint8_t *stype = r_string(&rs, &sn);
    if (!rk.ok || !rs.ok)
    {
        return false;
    }

    switch (s_cli.hostkey)
    {
    case CliHostkey::ED25519: {
        uint32_t pn;
        const uint8_t *pub = r_string(&rk, &pn);
        uint32_t rl;
        const uint8_t *raw = r_string(&rs, &rl);
        return rk.ok && rs.ok && pn == 32 && rl == 64 && pc_ed25519_verify(pub, H, h_len, raw);
    }
    case CliHostkey::ECDSA_P256: {
        uint32_t cn;
        r_string(&rk, &cn); // "nistp256"
        uint32_t qn;
        const uint8_t *q = r_string(&rk, &qn);
        // signature: string( mpint(r) || mpint(s) ) -> 64-byte raw r||s.
        uint32_t bl;
        const uint8_t *blob = r_string(&rs, &bl);
        if (!rk.ok || !rs.ok || qn != PC_ECDSA_P256_PUB_LEN)
        {
            return false;
        }
        Rd rb = {blob, bl, 0, true};
        uint32_t rlen, slen;
        const uint8_t *rr = r_string(&rb, &rlen);
        const uint8_t *ss = r_string(&rb, &slen);
        uint8_t raw[64];
        if (!rb.ok || !mpint_to_fixed(rr, rlen, raw, 32) || !mpint_to_fixed(ss, slen, raw + 32, 32))
        {
            return false;
        }
        return pc_ecdsa_p256_verify(q, H, h_len, raw);
    }
    case CliHostkey::RSA_SHA256:
    case CliHostkey::RSA_SHA512: {
        // K_S = string("ssh-rsa") || mpint(e) || mpint(n).
        uint32_t elen, nlen;
        const uint8_t *e = r_string(&rk, &elen);
        const uint8_t *n = r_string(&rk, &nlen);
        uint32_t rawlen;
        const uint8_t *raw = r_string(&rs, &rawlen); // the RSA signature bytes
        (void)ktype;
        (void)stype;
        uint8_t e4[4], n256[256];
        if (!rk.ok || !rs.ok || !mpint_to_fixed(e, elen, e4, 4) || !mpint_to_fixed(n, nlen, n256, 256))
        {
            return false;
        }
        pc_rsa_hash h = (s_cli.hostkey == CliHostkey::RSA_SHA512) ? pc_rsa_hash::SHA512 : pc_rsa_hash::SHA256;
        return pc_rsa_verify(n256, e4, H, h_len, raw, rawlen, h) == 0;
    }
    }
    return false;
}

static bool handle_kexdh_reply(const uint8_t *p, size_t len)
{
    Rd r = {p, len, 0, true};
    if (r_u8(&r) != SSH_MSG_KEXDH_REPLY)
    {
        return false;
    }
    uint32_t ks_len;
    const uint8_t *ks = r_string(&r, &ks_len); // K_S host-key blob
    uint32_t sp_len;
    const uint8_t *srv_pub = r_string(&r, &sp_len); // Q_S (string) or f (mpint)
    uint32_t sig_len;
    const uint8_t *sig = r_string(&r, &sig_len); // signature blob
    if (!r.ok)
    {
        return false;
    }

    // Pin the relay by the SHA-256 fingerprint of its host-key blob (type-agnostic, like known_hosts).
    uint8_t fp[32];
    pc_sha256_ctx fc;
    pc_sha256_init(&fc);
    pc_sha256_update(&fc, ks, ks_len);
    pc_sha256_final(&fc, fp);
    if (memcmp(fp, s_cli.cfg.host_pin, 32) != 0)
    {
        cli_fail("relay host key does not match the pin");
        return false;
    }

    uint8_t k_be[256];
    if (!compute_k(srv_pub, sp_len, k_be))
    {
        return false;
    }

    uint8_t H[SSH_KEXHASH_MAX_LEN];
    const size_t h_len = compute_h(ks, ks_len, srv_pub, sp_len, k_be, H); // 32 or 64 by method

    if (!verify_host_sig(ks, ks_len, sig, sig_len, H, h_len))
    {
        pc_secure_wipe(k_be, sizeof(k_be));
        cli_fail("relay signature verification failed");
        return false;
    }

    if (!s_cli.have_sid)
    {
        memcpy(s_cli.session_id, H, h_len);
        s_cli.session_id_len = (uint8_t)h_len;
        s_cli.have_sid = true;
    }

    // ssh_dh_derive_keys_sid populates c2s/s2c per the RFC 4253 §7.2 letters for the negotiated
    // cipher/MAC; the packet layer's is_client flag selects the send/receive direction. The hybrids
    // encode K as a fixed 32/64-byte string (k_is_string); the classical methods as an mpint. The
    // -sha512 method derives over SHA-512 (is512), so H and the session_id are 64 bytes.
    const bool is512 = cli_kex_is_sha512(s_cli.kex);
    bool k_is_string = false;
#if PC_ENABLE_PQC_KEX
    k_is_string = k_is_string || (s_cli.kex == CliKex::MLKEM768_X25519);
#endif
#if PC_ENABLE_SSH_SNTRUP761
    k_is_string = k_is_string || (s_cli.kex == CliKex::SNTRUP761_X25519);
#endif
    ssh_dh_derive_keys_sid(SSH_CLI_SLOT, k_be, H, s_cli.session_id, s_cli.cipher, s_cli.mac, k_is_string, h_len,
                           s_cli.session_id_len, is512);
    pc_secure_wipe(k_be, sizeof(k_be));
    pc_secure_wipe(s_cli.kex_priv, sizeof(s_cli.kex_priv));
#if PC_ENABLE_PQC_KEX || PC_ENABLE_SSH_SNTRUP761
    pc_secure_wipe((uint8_t *)&s_cli.hyb, sizeof(s_cli.hyb));
#endif

    uint8_t nk = SSH_MSG_NEWKEYS;
    if (!cli_send(&nk, 1))
    {
        return false;
    }
    ssh_pkt[SSH_CLI_SLOT].enc_out = true;
    return true;
}

// ---------------------------------------------------------------------------
// Auth (publickey, ssh-ed25519)
// ---------------------------------------------------------------------------

static bool send_service_request(void)
{
    uint8_t out[1 + 4 + 12];
    Wr w = {out, sizeof(out), 0, true};
    w_u8(&w, SSH_MSG_SERVICE_REQUEST);
    w_cstr(&w, "ssh-userauth");
    return w.ok && cli_send(out, w.off);
}

static bool send_userauth_publickey(void)
{
    const char *user = s_cli.cfg.user;
    uint8_t pub[32];
    pc_ed25519_pubkey(pub, s_cli.cfg.auth_seed);

    // The device's public-key blob: string("ssh-ed25519") || string(pub32).
    uint8_t pkblob[4 + 11 + 4 + 32];
    Wr pw = {pkblob, sizeof(pkblob), 0, true};
    w_cstr(&pw, NAME_ED25519);
    w_string(&pw, pub, 32);
    if (!pw.ok)
    {
        return false;
    }

    // Data to sign (RFC 4252 §7): string(session_id) || the userauth request up to (and including)
    // the public-key blob, with the "signature present" flag set. session_id is 32 or 64 bytes (the
    // -sha512 KEX), so the buffer carries SSH_KEXHASH_MAX_LEN of headroom over the 32-byte base.
    uint8_t signed_data[256 + SSH_KEXHASH_MAX_LEN];
    Wr sd = {signed_data, sizeof(signed_data), 0, true};
    w_string(&sd, s_cli.session_id, s_cli.session_id_len);
    w_u8(&sd, SSH_MSG_USERAUTH_REQUEST);
    w_cstr(&sd, user);
    w_cstr(&sd, "ssh-connection");
    w_cstr(&sd, "publickey");
    w_u8(&sd, 1); // signature present
    w_cstr(&sd, NAME_ED25519);
    w_string(&sd, pkblob, pw.off);
    if (!sd.ok)
    {
        return false;
    }

    uint8_t sig[64];
    pc_ed25519_sign(sig, signed_data, sd.off, s_cli.cfg.auth_seed);

    // Signature blob: string("ssh-ed25519") || string(sig64).
    uint8_t sigblob[4 + 11 + 4 + 64];
    Wr sg = {sigblob, sizeof(sigblob), 0, true};
    w_cstr(&sg, NAME_ED25519);
    w_string(&sg, sig, 64);
    if (!sg.ok)
    {
        return false;
    }

    // The full USERAUTH_REQUEST is the signed prefix (minus the session_id) plus the signature.
    uint8_t out[300];
    Wr w = {out, sizeof(out), 0, true};
    w_u8(&w, SSH_MSG_USERAUTH_REQUEST);
    w_cstr(&w, user);
    w_cstr(&w, "ssh-connection");
    w_cstr(&w, "publickey");
    w_u8(&w, 1);
    w_cstr(&w, NAME_ED25519);
    w_string(&w, pkblob, pw.off);
    w_string(&w, sigblob, sg.off);
    return w.ok && cli_send(out, w.off);
}

// ---------------------------------------------------------------------------
// tcpip-forward
// ---------------------------------------------------------------------------

static bool send_tcpip_forward(void)
{
    uint8_t out[128];
    Wr w = {out, sizeof(out), 0, true};
    w_u8(&w, SSH_MSG_GLOBAL_REQUEST);
    w_cstr(&w, "tcpip-forward");
    w_u8(&w, 1); // want reply
    w_cstr(&w, s_cli.cfg.bind_addr ? s_cli.cfg.bind_addr : "");
    w_u32(&w, s_cli.cfg.bind_port);
    return w.ok && cli_send(out, w.off);
}

// ---------------------------------------------------------------------------
// forwarded-tcpip channel + bridge
// ---------------------------------------------------------------------------

#define SSH_CLI_WINDOW 32768u
#define SSH_CLI_MAXPKT 16384u

static void handle_channel_open(const uint8_t *p, size_t len)
{
    Rd r = {p, len, 0, true};
    r_u8(&r); // msg
    uint32_t tn;
    const uint8_t *type = r_string(&r, &tn);
    uint32_t their_id = r_u32(&r);
    uint32_t their_win = r_u32(&r);
    r_u32(&r); // their max packet
    if (!r.ok || tn != 15 || memcmp(type, "forwarded-tcpip", 15) != 0)
    {
        // Refuse anything else.
        uint8_t out[64];
        Wr w = {out, sizeof(out), 0, true};
        w_u8(&w, SSH_MSG_CHANNEL_OPEN_FAILURE);
        w_u32(&w, their_id);
        w_u32(&w, 1); // administratively prohibited
        w_cstr(&w, "only forwarded-tcpip");
        w_cstr(&w, "");
        if (w.ok)
        {
            cli_send(out, w.off);
        }
        return;
    }

    // Claim a channel slot; refuse if the pool is full.
    CliChannel *ch = chan_alloc();
    if (!ch)
    {
        uint8_t out[48];
        Wr w = {out, sizeof(out), 0, true};
        w_u8(&w, SSH_MSG_CHANNEL_OPEN_FAILURE);
        w_u32(&w, their_id);
        w_u32(&w, 4); // resource shortage
        w_cstr(&w, "busy");
        w_cstr(&w, "");
        if (w.ok)
        {
            cli_send(out, w.off);
        }
        return;
    }

    // Open the local bridge connection (to the device's own service).
    int lc = pc_client_open("127.0.0.1", s_cli.cfg.local_port, 3000);
    PC_LOGD("ssh-tunnel: forwarded-tcpip open, local connect(:%u) cid=%d", (unsigned)s_cli.cfg.local_port, lc);
    if (lc < 0)
    {
        uint8_t out[64];
        Wr w = {out, sizeof(out), 0, true};
        w_u8(&w, SSH_MSG_CHANNEL_OPEN_FAILURE);
        w_u32(&w, their_id);
        w_u32(&w, 2); // connect failed
        w_cstr(&w, "local connect failed");
        w_cstr(&w, "");
        if (w.ok)
        {
            cli_send(out, w.off);
        }
        return;
    }

    ch->used = true;
    ch->remote_id = their_id;
    ch->local_id = s_cli.next_chan_id++;
    ch->send_win = their_win;
    ch->recv_win = SSH_CLI_WINDOW;
    ch->local_cid = lc;
    ch->eof_sent = false;
    ch->relay_eof = false; // fully self-init this slot; do not lean on channel_close having zeroed it

    uint8_t out[64];
    Wr w = {out, sizeof(out), 0, true};
    w_u8(&w, SSH_MSG_CHANNEL_OPEN_CONFIRM);
    w_u32(&w, their_id);
    w_u32(&w, ch->local_id);
    w_u32(&w, SSH_CLI_WINDOW);
    w_u32(&w, SSH_CLI_MAXPKT);
    if (w.ok)
    {
        cli_send(out, w.off);
    }
}

static void channel_close(CliChannel *ch)
{
    if (!ch || !ch->used)
    {
        return;
    }
    uint8_t out[8];
    Wr w = {out, sizeof(out), 0, true};
    w_u8(&w, SSH_MSG_CHANNEL_CLOSE);
    w_u32(&w, ch->remote_id);
    if (w.ok)
    {
        cli_send(out, w.off);
    }
    if (ch->local_cid >= 0)
    {
        pc_client_close(ch->local_cid);
    }
    memset(ch, 0, sizeof(*ch));
    ch->local_cid = -1;
}

// Relay -> device: CHANNEL_DATA is written to the addressed channel's local service.
static void handle_channel_data(const uint8_t *p, size_t len)
{
    Rd r = {p, len, 0, true};
    r_u8(&r);
    uint32_t rid = r_u32(&r); // our channel id (recipient)
    uint32_t dn;
    const uint8_t *d = r_string(&r, &dn);
    CliChannel *ch = chan_by_local(rid);
    if (!r.ok || !ch)
    {
        return;
    }
    if (ch->local_cid >= 0 && dn)
    {
        pc_client_send(ch->local_cid, d, dn);
    }

    // Refill the relay's window as we consume, so it can keep sending.
    if (ch->recv_win >= dn)
    {
        ch->recv_win -= dn;
    }
    else
    {
        ch->recv_win = 0;
    }
    if (ch->recv_win < SSH_CLI_WINDOW / 2)
    {
        uint32_t add = SSH_CLI_WINDOW - ch->recv_win;
        uint8_t out[16];
        Wr w = {out, sizeof(out), 0, true};
        w_u8(&w, SSH_MSG_CHANNEL_WINDOW_ADJUST);
        w_u32(&w, ch->remote_id);
        w_u32(&w, add);
        if (w.ok && cli_send(out, w.off))
        {
            ch->recv_win += add;
        }
    }
}

// Device -> relay: drain one channel's local service and forward as CHANNEL_DATA, honoring the peer
// window; when its local side has closed and drained, half-close (EOF) then CLOSE that channel.
static void pump_channel(CliChannel *ch)
{
    if (!ch->used || ch->local_cid < 0)
    {
        return;
    }
    uint8_t buf[1024];
    while (ch->send_win > 0)
    {
        size_t want = sizeof(buf);
        if (want > ch->send_win)
        {
            want = ch->send_win;
        }
        if (want > SSH_CLI_MAXPKT)
        {
            want = SSH_CLI_MAXPKT;
        }
        size_t got = pc_client_read(ch->local_cid, buf, want);
        if (got == 0)
        {
            break;
        }
        uint8_t hdr[16];
        Wr w = {hdr, sizeof(hdr), 0, true};
        w_u8(&w, SSH_MSG_CHANNEL_DATA);
        w_u32(&w, ch->remote_id);
        w_u32(&w, (uint32_t)got);
        // Assemble header + data into the wire staging buffer via one payload.
        uint8_t payload[9 + sizeof(buf)];
        memcpy(payload, hdr, w.off);
        memcpy(payload + w.off, buf, got);
        if (!cli_send(payload, w.off + got))
        {
            break;
        }
        ch->send_win -= (uint32_t)got;
    }
    // Tear the channel down once its reply has drained and either side is done: the local service
    // closed, or the relay half-closed (relay_eof - the forwarded peer finished, e.g. an HTTP client
    // that already has the full response). The drain loop above exits with got==0, so all currently
    // available local bytes have been forwarded before we half-close.
    bool local_done = pc_client_is_closed(ch->local_cid) && pc_client_available(ch->local_cid) == 0;
    if ((local_done || ch->relay_eof) && !ch->eof_sent)
    {
        uint8_t out[8];
        Wr w = {out, sizeof(out), 0, true};
        w_u8(&w, SSH_MSG_CHANNEL_EOF);
        w_u32(&w, ch->remote_id);
        if (w.ok)
        {
            cli_send(out, w.off);
        }
        ch->eof_sent = true;
        channel_close(ch);
    }
}

// Pump every active channel once per poll.
static void pump_local_to_relay(void)
{
    for (int i = 0; i < PC_SSH_CLIENT_MAX_CHANNELS; i++)
    {
        if (s_cli.chan[i].used)
        {
            pump_channel(&s_cli.chan[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// Inbound message dispatch (called by ssh_pkt_recv per verified packet)
// ---------------------------------------------------------------------------

static void cli_msg_handler(uint8_t slot, uint8_t type, const uint8_t *payload, size_t len)
{
    (void)slot;
    switch (s_cli.phase)
    {
    case CliPhase::KEXINIT:
        if (type == SSH_MSG_KEXINIT)
        {
            if (handle_server_kexinit(payload, len))
            {
                s_cli.phase = CliPhase::KEXREPLY;
            }
            else
            {
                cli_fail("KEXINIT negotiation failed");
            }
        }
        break;
    case CliPhase::KEXREPLY:
        if (type == SSH_MSG_KEXDH_REPLY)
        {
            if (handle_kexdh_reply(payload, len))
            {
                s_cli.phase = CliPhase::NEWKEYS;
            }
            else if (s_cli.phase != CliPhase::FAILED)
            {
                cli_fail("KEXDH_REPLY invalid");
            }
        }
        break;
    case CliPhase::NEWKEYS:
        if (type == SSH_MSG_NEWKEYS)
        {
            ssh_pkt[SSH_CLI_SLOT].enc_in = true;
            ssh_pkt[SSH_CLI_SLOT].kex_active = false;
            if (send_service_request())
            {
                s_cli.phase = CliPhase::SERVICE;
            }
            else
            {
                cli_fail("service request send failed");
            }
        }
        break;
    case CliPhase::SERVICE:
        if (type == SSH_MSG_SERVICE_ACCEPT)
        {
            if (send_userauth_publickey())
            {
                s_cli.phase = CliPhase::AUTH;
            }
            else
            {
                cli_fail("userauth send failed");
            }
        }
        break;
    case CliPhase::AUTH:
        if (type == SSH_MSG_USERAUTH_SUCCESS)
        {
            if (send_tcpip_forward())
            {
                s_cli.phase = CliPhase::FORWARD;
            }
            else
            {
                cli_fail("tcpip-forward send failed");
            }
        }
        else if (type == SSH_MSG_USERAUTH_FAILURE)
        {
            cli_fail("authentication rejected by the relay");
        }
        break;
    case CliPhase::FORWARD:
        if (type == SSH_MSG_REQUEST_SUCCESS)
        {
            s_cli.phase = CliPhase::OPEN;
            s_cli.state = pc_ssh_tunnel_state::PC_TUN_UP;
            PC_LOGI("ssh-tunnel: up (relay forward :%u)", (unsigned)s_cli.cfg.bind_port);
        }
        else if (type == SSH_MSG_REQUEST_FAILURE)
        {
            cli_fail("relay refused the remote forward");
        }
        break;
    case CliPhase::OPEN:
        switch (type)
        {
        case SSH_MSG_CHANNEL_OPEN:
            handle_channel_open(payload, len);
            break;
        case SSH_MSG_CHANNEL_DATA:
            handle_channel_data(payload, len);
            break;
        case SSH_MSG_CHANNEL_WINDOW_ADJUST: {
            Rd r = {payload, len, 0, true};
            r_u8(&r);
            uint32_t rid = r_u32(&r);
            uint32_t add = r_u32(&r);
            CliChannel *ch = chan_by_local(rid);
            if (r.ok && ch)
            {
                ch->send_win += add;
            }
            break;
        }
        case SSH_MSG_CHANNEL_EOF: {
            // The relay's write side closed - the forwarded peer is done sending (for a request/
            // response bridge, the response has already been delivered). Mark it so the channel tears
            // down as soon as the local reply drains, instead of lingering until the relay's CLOSE and
            // starving the channel pool. A keep-alive local server never closes on its own.
            Rd r = {payload, len, 0, true};
            r_u8(&r);
            uint32_t rid = r_u32(&r);
            CliChannel *ch = chan_by_local(rid);
            if (r.ok && ch)
            {
                ch->relay_eof = true;
            }
            break;
        }
        case SSH_MSG_CHANNEL_CLOSE: {
            Rd r = {payload, len, 0, true};
            r_u8(&r);
            uint32_t rid = r_u32(&r);
            if (r.ok)
            {
                channel_close(chan_by_local(rid));
            }
            break;
        }
        case SSH_MSG_GLOBAL_REQUEST: {
            // A want_reply keepalive gets a REQUEST_FAILURE (we support no global requests inbound).
            Rd r = {payload, len, 0, true};
            r_u8(&r);
            uint32_t nn;
            r_string(&r, &nn);
            uint8_t wr = r_u8(&r);
            if (r.ok && wr)
            {
                uint8_t f = SSH_MSG_REQUEST_FAILURE;
                cli_send(&f, 1);
            }
            break;
        }
        default:
            break;
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool pc_ssh_tunnel_begin(const pc_ssh_tunnel_cfg *cfg)
{
    if (!cfg || !cfg->host || !cfg->user || !cfg->auth_seed || !cfg->host_pin)
    {
        return false;
    }

    pc_ssh_tunnel_end();
    memset(&s_cli, 0, sizeof(s_cli));
    s_cli.cfg = *cfg;
    for (int i = 0; i < PC_SSH_CLIENT_MAX_CHANNELS; i++)
    {
        s_cli.chan[i].local_cid = -1;
    }
    s_cli.next_chan_id = 1;

    // Own a dedicated scratch arena, distinct from the server's worker(s): packet decryption borrows
    // from the shared scratch, and that arena is single-accessor-per-task. begin() and poll() run in
    // the same task, so claiming the slot here makes every later decrypt use the client's own arena.
    pc_worker_set_self(PC_SSH_CLIENT_SCRATCH_SLOT);

    uint16_t port = cfg->port ? cfg->port : 22;
    s_cli.cid = pc_client_open(cfg->host, port, 8000);
    if (s_cli.cid < 0)
    {
        s_cli.state = pc_ssh_tunnel_state::PC_TUN_FAILED;
        return false;
    }

    ssh_pkt_init(SSH_CLI_SLOT);
    ssh_pkt_set_client(SSH_CLI_SLOT);
    ssh_keymat_wipe(SSH_CLI_SLOT);

    // Send our identification string, then our KEXINIT.
    char banner[64];
    pc_sb sb_banner = {banner, sizeof(banner), 0, true};
    pc_sb_put(&sb_banner, CLIENT_BANNER);
    pc_sb_put(&sb_banner, "\r\n");
    int n = (int)pc_sb_finish(&sb_banner);
    if (n <= 0 || !pc_client_send(s_cli.cid, (const uint8_t *)banner, (size_t)n))
    {
        cli_fail("banner send failed");
        return false;
    }
    s_cli.phase = CliPhase::BANNER;
    s_cli.state = pc_ssh_tunnel_state::PC_TUN_CONNECTING;
    s_cli.deadline_ms = pc_millis() + 15000;
    return true;
}

// Accumulate the server identification line, then hand the rest to the packet layer.
static void drain_banner(const uint8_t *data, size_t len, size_t *consumed)
{
    *consumed = 0;
    for (size_t i = 0; i < len; i++)
    {
        uint8_t ch = data[i];
        if (s_cli.banner_len < sizeof(s_cli.banner))
        {
            s_cli.banner[s_cli.banner_len++] = ch;
        }
        if (ch == '\n')
        {
            // A complete line. Only "SSH-..." is the identification; earlier lines are allowed banners.
            uint16_t l = s_cli.banner_len;
            while (l && (s_cli.banner[l - 1] == '\n' || s_cli.banner[l - 1] == '\r'))
            {
                l--;
            }
            if (l >= 4 && memcmp(s_cli.banner, "SSH-", 4) == 0)
            {
                memcpy(s_cli.v_s, s_cli.banner, l);
                s_cli.v_s_len = l;
                *consumed = i + 1;
                s_cli.phase = CliPhase::KEXINIT;
                if (!build_kexinit())
                {
                    cli_fail("KEXINIT send failed");
                }
                return;
            }
            s_cli.banner_len = 0; // skip a pre-banner line, keep reading
        }
    }
    *consumed = len; // whole chunk consumed into the accumulator
}

void pc_ssh_tunnel_poll(void)
{
    if (s_cli.cid < 0 || s_cli.phase == CliPhase::IDLE || s_cli.phase == CliPhase::FAILED)
    {
        return;
    }

    if (pc_client_is_closed(s_cli.cid) && pc_client_available(s_cli.cid) == 0)
    {
        cli_fail("relay closed the connection");
        return;
    }
    if (s_cli.state == pc_ssh_tunnel_state::PC_TUN_CONNECTING && (int32_t)(pc_millis() - s_cli.deadline_ms) > 0)
    {
        cli_fail("handshake timed out");
        return;
    }

    uint8_t buf[1024];
    size_t got = pc_client_read(s_cli.cid, buf, sizeof(buf));
    if (got)
    {
        size_t off = 0;
        if (s_cli.phase == CliPhase::BANNER)
        {
            size_t consumed = 0;
            drain_banner(buf, got, &consumed);
            off = consumed;
        }
        if (off < got && s_cli.phase != CliPhase::FAILED)
        {
            if (ssh_pkt_recv(SSH_CLI_SLOT, buf + off, got - off, cli_msg_handler) != 0)
            {
                cli_fail("packet error (MAC / framing)");
            }
        }
    }

    if (s_cli.phase == CliPhase::OPEN)
    {
        pump_local_to_relay();
    }
}

void pc_ssh_tunnel_end(void)
{
    for (int i = 0; i < PC_SSH_CLIENT_MAX_CHANNELS; i++)
    {
        if (s_cli.chan[i].used && s_cli.chan[i].local_cid >= 0)
        {
            pc_client_close(s_cli.chan[i].local_cid);
        }
    }
    if (s_cli.cid >= 0)
    {
        pc_client_close(s_cli.cid);
    }
    ssh_keymat_wipe(SSH_CLI_SLOT);
    pc_secure_wipe(s_cli.kex_priv, sizeof(s_cli.kex_priv));
    memset(&s_cli, 0, sizeof(s_cli));
    s_cli.cid = -1;
    s_cli.phase = CliPhase::IDLE;
    s_cli.state = pc_ssh_tunnel_state::PC_TUN_IDLE;
}

pc_ssh_tunnel_state pc_ssh_tunnel_state_get(void)
{
    return s_cli.state;
}

bool pc_ssh_tunnel_up(void)
{
    return s_cli.state == pc_ssh_tunnel_state::PC_TUN_UP;
}

#else // !ARDUINO - host builds have no lwIP client transport; the tunnel is device-only.

bool pc_ssh_tunnel_begin(const pc_ssh_tunnel_cfg *)
{
    return false;
}
void pc_ssh_tunnel_poll(void)
{
}
void pc_ssh_tunnel_end(void)
{
}
pc_ssh_tunnel_state pc_ssh_tunnel_state_get(void)
{
    return pc_ssh_tunnel_state::PC_TUN_IDLE;
}
bool pc_ssh_tunnel_up(void)
{
    return false;
}

#endif // ARDUINO

// Available on both host and device: pure key derivation for provisioning.
void pc_ssh_tunnel_pubkey(const uint8_t seed[32], uint8_t pub[32])
{
    pc_ed25519_pubkey(pub, seed);
}

#endif // PC_ENABLE_SSH_CLIENT
