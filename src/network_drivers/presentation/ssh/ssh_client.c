// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_client.c
 * @brief Outbound SSH client + reverse tunnel state machine (see ssh_client.h).
 *
 * Client-role driver over the shipped transport primitives: it reuses the role-aware binary packet
 * layer (ssh_pkt_* with ssh_pkt_set_client), the curve25519 / ed25519 / chacha-poly crypto, and the
 * RFC 4253 §7.2 KDF. Only the client-side handshake, auth, and forward logic lives here.
 */

#include "network_drivers/presentation/ssh/ssh_client.h"
#include "mmgr/bytes.h"     // pc_mpint_to_fixed
#include "mmgr/protoframe.h" // the one frame engine
#include "mmgr/rawmemcpy.h"  // proto_raw_put_u32 - one unaligned store, not four
#include "mmgr/protomem.h"
#include "mmgr/secure.h"

#if PC_ENABLE_SSH_CLIENT

#include "crypto/asymmetric/bignum.h"     // bn_expmod_group14 (dh-group14 client)
#include "crypto/asymmetric/curve25519.h" // pc_x25519 (curve25519-sha256)
#include "crypto/asymmetric/ecdsa.h"      // ecdh-sha2-nistp256 + ecdsa host-key verify
#include "crypto/asymmetric/ed25519.h"    // ssh-ed25519 host key + client auth
#include "crypto/hash/sha256.h"
#include "crypto/rng/rng.h"                                               // pc_rand_fill
#include "network_drivers/presentation/ssh/auth/ssh_auth.h"               // SSH_MSG_USERAUTH_*
#include "network_drivers/presentation/ssh/connection/ssh_channel.h" // ssh_chan[], pc_ssh_chan_by_id/_alloc
#include "network_drivers/presentation/ssh/connection/ssh_flow_control.h" // SSH_MSG_CHANNEL_*, SSH_MSG_GLOBAL_REQUEST
#include "network_drivers/presentation/ssh/transport/ssh_dh.h"            // ssh_dh_derive_keys_sid
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h" // ssh_keys[], SshKeyMat, SSH_CIPHER_*, SSH_MAC_*
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h" // SshTransport, the RFC 4253 member set
#include "network_drivers/tls/ssh_kexhash.h" // SshKexHash (SHA-256/SHA-512 by method)
#include "network_drivers/tls/ssh_rsa.h"     // rsa-sha2-256/512 host-key verify
#include "shared_primitives/log.h"

#if PC_ENABLE_PQC_KEX
#include "crypto/pqc/mlkem.h" // mlkem768x25519-sha256 hybrid (client: KeyGen + Decaps)
#endif
#if PC_ENABLE_SSH_SNTRUP761
#include "crypto/pqc/sntrup761.h" // sntrup761x25519-sha512 hybrid (client: KeyGen + Decaps)
#endif
#if PC_ENABLE_PQC_KEX || PC_ENABLE_SSH_SNTRUP761
#include "mmgr/plaintext.h" // pc_plaintext_alloc for the large hybrid C_INIT
#endif

#include "mmgr/arena.h"                    // pc_worker_set_self (own scratch slot)
#include "network_drivers/transport/tcp.h" // pc_client_*
#include "server/clock/clock.h"            // pc_millis, pcdelay

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const char CLIENT_BANNER[] = "SSH-2.0-PC_client_1.0";

#define SSH_CLI_SLOT 0 // the SSH packet/key pool slot the client borrows (MAX_SSH_CONNS >= 1)

// The client is one connection on one slot, so its handshake crypto works out of that slot's bytes -
// the same borrow the wire and the packet MAC come from. Null when the pool cannot cover the slot.
static uint8_t *cli_crypto_work(void)
{
    if (!ssh_pkt_slot_storage(&ssh_pkt[SSH_CLI_SLOT]))
    {
        return NULL;
    }
    return ssh_pkt[SSH_CLI_SLOT].crypto_work;
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

// The exchange hash + RFC 4253 sec 7.2 KDF run over SHA-512 for the -sha512 methods
// (sntrup761x25519-sha512), SHA-256 for every other method (RFC 4253 sec 8).
static inline proto_bool cli_kex_is_sha512(SshKexAlg k)
{
#if PC_ENABLE_SSH_SNTRUP761
    return k == SSH_KEX_SNTRUP761_X25519;
#else
    (void)k;
    return PROTO_FALSE;
#endif
}

// ---------------------------------------------------------------------------
// Wire helpers (SSH data types, RFC 4251 §5)
// ---------------------------------------------------------------------------

// Reader over a payload with bounds checking.
typedef struct
{
    const uint8_t *buf;
    size_t len;
    size_t off;
    proto_bool ok;
} Rd;
static uint8_t r_u8(Rd *r)
{
    if (r->off + 1 > r->len)
    {
        r->ok = PROTO_FALSE;
        return 0;
    }
    return r->buf[r->off++];
}
static uint32_t r_u32(Rd *r)
{
    uint32_t v = 0;
    if (!pc_rd_u32(r->buf, r->len, &r->off, &v))
    {
        r->ok = PROTO_FALSE;
        return 0;
    }
    return v;
}
// Returns a pointer to an in-place string of length *n; advances past it. Fails closed on overflow.
static const uint8_t *r_string(Rd *r, uint32_t *n)
{
    const uint8_t *p = NULL;
    if (!pc_rd_str(r->buf, r->len, &r->off, &p, n))
    {
        r->ok = PROTO_FALSE;
        *n = 0;
        return NULL;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Client session state
// ---------------------------------------------------------------------------

// How many forwarded-tcpip connections the tunnel bridges at once (PC_SSH_CLIENT_MAX_CHANNELS,
// defaulted per variant in protocore_config.h / the board profile): a relay forwarding to a web UI opens
// one channel per inbound TCP connection, so a pool (not a single slot) avoids rapid / concurrent
// requests getting "administratively prohibited". Each slot costs a CliChannel + a pc_client conn.

// One forwarded-tcpip channel bridged to a local TCP connection. The channel itself - its ids and its
// RFC 4254 sec 5.2 window pair - lives in the channel layer's table; this is the local socket glued
// to it, the same split ssh_forward.c makes with SshRfwdBridge.
typedef struct
{
    proto_bool used;
    uint32_t channel; ///< the shared channel's local id (== its slot index).
    int local_cid;    ///< pc_client id of the bridged local TCP connection, or -1.
    proto_bool eof_sent;
    proto_bool relay_eof; ///< the relay half-closed (peer done sending); tear down once the response drains.
} CliChannel;

typedef struct
{
    pc_ssh_tunnel_cfg cfg;
    pc_ssh_tunnel_state state;

    int cid; ///< relay TCP connection (pc_client), or -1.
    uint32_t deadline_ms;

    SshKexAlg kex;         ///< negotiated key exchange.
    SshHostkeyAlg hostkey; ///< negotiated host-key / signature type.
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
    proto_bool have_sid;

    CliChannel chan[PC_SSH_CLIENT_MAX_CHANNELS]; ///< the active forwarded channels.

    uint8_t wire[SSH_WIRE_CAP]; ///< staging buffer for one outgoing (framed/encrypted) packet.
} SshClientCtx;

static SshClientCtx s_cli;

// Find the bridge on the channel a message addresses (inbound messages carry our local id), or null.
static CliChannel *bridge_by_channel(uint32_t local_id)
{
    for (int i = 0; i < PC_SSH_CLIENT_MAX_CHANNELS; i++)
    {
        if (s_cli.chan[i].used && s_cli.chan[i].channel == local_id)
        {
            return &s_cli.chan[i];
        }
    }
    return NULL;
}

// Claim a free bridge slot, or null if all are in use.
static CliChannel *bridge_alloc(void)
{
    for (int i = 0; i < PC_SSH_CLIENT_MAX_CHANNELS; i++)
    {
        if (!s_cli.chan[i].used)
        {
            return &s_cli.chan[i];
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Transmit
// ---------------------------------------------------------------------------

// Frame @p payload as a binary packet (encrypted once NEWKEYS is active) and write it to the relay.
static proto_bool cli_send(const uint8_t *payload, size_t len)
{
    size_t wlen = 0;
    if (ssh_pkt_send(SSH_CLI_SLOT, payload, len, s_cli.wire, &wlen, sizeof(s_cli.wire), &ssh_sess[SSH_CLI_SLOT].out) != 0)
    {
        return PROTO_FALSE;
    }
    return Tcp.client->send(s_cli.cid, s_cli.wire, wlen);
}

// The identification line RFC 4253 4.2 puts on the wire first: the version string, then CR LF.
static const pc_field CLI_BANNER[] = {PC_STR, {PC_FK_LIT, 0, 2, "\r\n"}, PC_END};

// Log frames: each message's shape is fixed here, so nothing is parsed when one is emitted.
static const pc_field LOG_TUNNEL_FAIL[] = {{PC_FK_LIT, 0, 12, "ssh-tunnel: "}, PC_STR, PC_END};
static const pc_field LOG_TUNNEL_NEGOTIATED[] = {{PC_FK_LIT, 0, 27, "ssh-tunnel: negotiated kex="},
                                                 PC_STR,
                                                 {PC_FK_LIT, 0, 9, " hostkey="},
                                                 PC_STR,
                                                 {PC_FK_LIT, 0, 8, " cipher="},
                                                 PC_STR,
                                                 PC_END};
static const pc_field LOG_TUNNEL_FWD_OPEN[] = {{PC_FK_LIT, 0, 49, "ssh-tunnel: forwarded-tcpip open, local connect(:"},
                                               PC_U32,
                                               {PC_FK_LIT, 0, 6, ") cid="},
                                               PC_I64,
                                               PC_END};
static const pc_field LOG_TUNNEL_UP[] = {
    {PC_FK_LIT, 0, 31, "ssh-tunnel: up (relay forward :"}, PC_U32, {PC_FK_LIT, 0, 1, ")"}, PC_END};

static void cli_fail(const char *why)
{
    PC_LOGW(LOG_TUNNEL_FAIL, ((const pc_fval[]){PC_VSTR(why)}), 1);
    s_cli.state = PC_TUN_FAILED;
    for (int i = 0; i < PC_SSH_CLIENT_MAX_CHANNELS; i++)
    {
        if (s_cli.chan[i].used && s_cli.chan[i].local_cid >= 0)
        {
            Tcp.client->close(s_cli.chan[i].local_cid);
        }
    }
    if (s_cli.cid >= 0)
    {
        Tcp.client->close(s_cli.cid);
    }
    s_cli.cid = -1;
    ssh_keymat_wipe(SSH_CLI_SLOT);
    pc_secure_wipe(s_cli.kex_priv, sizeof(s_cli.kex_priv));
}

// ---------------------------------------------------------------------------
// Algorithm negotiation + KEXINIT (client)
// ---------------------------------------------------------------------------

// Write a comma-joined SSH name-list from @p names into the span as one string.
static void w_namelist(pc_span *w, const char *const *names, size_t n)
{
    char tmp[256];
    size_t o = 0;
    for (size_t i = 0; i < n; i++)
    {
        size_t l = str.len(names[i], sizeof(tmp));
        if (i && o + 1 <= sizeof(tmp))
        {
            tmp[o++] = ',';
        }
        if (o + l <= sizeof(tmp))
        {
            mem.cpy(tmp + o, names[i], l);
            o += l;
        }
    }
    pc_ssh_wr_str(w, tmp, o);
}

static proto_bool build_kexinit(void)
{
    pc_span w = pc_span_from(s_cli.i_c, sizeof(s_cli.i_c));
    pc_bw_put(&w, SSH_MSG_KEXINIT);
    uint8_t cookie[16];
    pc_rand_fill(cookie, 16);
    pc_bw_bytes(&w, cookie, 16);
    w_namelist(&w, KEX_NAMES, sizeof(KEX_NAMES) / sizeof(KEX_NAMES[0]));             // kex
    w_namelist(&w, HOSTKEY_NAMES, sizeof(HOSTKEY_NAMES) / sizeof(HOSTKEY_NAMES[0])); // host key
    w_namelist(&w, CIPHER_NAMES, sizeof(CIPHER_NAMES) / sizeof(CIPHER_NAMES[0]));    // enc c2s
    w_namelist(&w, CIPHER_NAMES, sizeof(CIPHER_NAMES) / sizeof(CIPHER_NAMES[0]));    // enc s2c
    w_namelist(&w, MAC_NAMES, sizeof(MAC_NAMES) / sizeof(MAC_NAMES[0]));             // mac c2s
    w_namelist(&w, MAC_NAMES, sizeof(MAC_NAMES) / sizeof(MAC_NAMES[0]));             // mac s2c
    pc_ssh_wr_cstr(&w, "none");                                                              // comp c2s
    pc_ssh_wr_cstr(&w, "none");                                                              // comp s2c
    pc_ssh_wr_cstr(&w, "");                                                                  // lang c2s
    pc_ssh_wr_cstr(&w, "");                                                                  // lang s2c
    pc_bw_put(&w, 0);                                                                     // first_kex_packet_follows
    pc_bw_put_be(&w, 0, 4);                                                                    // reserved
    if (!pc_span_ok(w))
    {
        return PROTO_FALSE;
    }
    s_cli.i_c_len = (uint16_t)w.pos;
    return cli_send(s_cli.i_c, s_cli.i_c_len);
}

// Generate our KEX ephemeral for the negotiated method and build Q_C / e into s_cli.qc.
static proto_bool build_kex_public(void)
{
    switch (s_cli.kex)
    {
    case SSH_KEX_CURVE25519:
        pc_rand_fill(s_cli.kex_priv, 32);
        pc_x25519_base(s_cli.qc, s_cli.kex_priv);
        s_cli.qc_len = 32;
        return PROTO_TRUE;
    case SSH_KEX_ECDH_NISTP256:
        // Draw a valid P-256 scalar (pubkey derivation rejects 0 / >= group order).
        for (int tries = 0; tries < 8; tries++)
        {
            pc_rand_fill(s_cli.kex_priv, 32);
            if (pc_ecdsa_p256_pubkey(s_cli.qc, s_cli.kex_priv))
            {
                s_cli.qc_len = PC_ECDSA_P256_PUB_LEN; // 65
                return PROTO_TRUE;
            }
        }
        return PROTO_FALSE;
    case SSH_KEX_DH_GROUP14: {
        // e = g^x mod p, g = 2 (RFC 3526 group 14). x is a 256-bit exponent.
        pc_rand_fill(s_cli.kex_priv, 32);
        pc_bignum g, x, e;
        uint8_t two = 2;
        bn_from_bytes(&g, &two, 1);
        bn_from_bytes(&x, s_cli.kex_priv, 32);
        bn_expmod_group14(&e, &g, &x);
        bn_to_bytes(s_cli.qc, &e);
        s_cli.qc_len = 256;
        pc_secure_wipe(&x, sizeof(x));
        pc_secure_wipe(&e, sizeof(e));
        return PROTO_TRUE;
    }
#if PC_ENABLE_PQC_KEX
    case SSH_KEX_MLKEM768_X25519: {
        // ML-KEM-768 keypair (dk kept for Decaps; ek is embedded in dk) + an X25519 ephemeral. C_INIT
        // (ek || Q_C) is assembled at send time; Q_C lives in qc[0..31].
        uint8_t d[32], z[32], ek[MLKEM768_EK_BYTES];
        pc_rand_fill(d, sizeof(d));
        pc_rand_fill(z, sizeof(z));
        pc_mlkem768_keygen(d, z, ek, s_cli.hyb.mlkem_dk);
        pc_secure_wipe(d, sizeof(d));
        pc_secure_wipe(z, sizeof(z));
        pc_secure_wipe(ek, sizeof(ek)); // ek persists inside mlkem_dk
        pc_rand_fill(s_cli.kex_priv, 32);
        pc_x25519_base(s_cli.qc, s_cli.kex_priv);
        s_cli.qc_len = 32;
        return PROTO_TRUE;
    }
#endif
#if PC_ENABLE_SSH_SNTRUP761
    case SSH_KEX_SNTRUP761_X25519: {
        // sntrup761 keypair (sk kept for Decaps; pk is embedded in sk) + an X25519 ephemeral. C_INIT
        // (pk || Q_C) is assembled at send time from sk; Q_C lives in qc[0..31]. pk is only needed
        // transiently here (sk embeds a copy), so it borrows the scratch arena.
        uint8_t *work = cli_crypto_work();
        if (work == NULL)
        {
            return PROTO_FALSE;
        }
        size_t mark = pc_plaintext_mark();
        uint8_t *pk = (uint8_t *)pc_plaintext_alloc(PC_SNTRUP761_PK_BYTES, 1);
        if (!pk)
        {
            return PROTO_FALSE;
        }
        pc_sntrup761_keypair(work, pk, s_cli.hyb.sntrup_sk);
        pc_plaintext_release(mark); // pk persists inside sntrup_sk at PC_SNTRUP761_SK_PK_OFFSET
        pc_rand_fill(s_cli.kex_priv, 32);
        pc_x25519_base(s_cli.qc, s_cli.kex_priv);
        s_cli.qc_len = 32;
        return PROTO_TRUE;
    }
#endif
    }
    return PROTO_FALSE;
}

// Parse the server KEXINIT, negotiate every category, store I_S, and send KEXDH_INIT.
static proto_bool handle_server_kexinit(const uint8_t *p, size_t len)
{
    if (len > sizeof(s_cli.i_s))
    {
        return PROTO_FALSE;
    }
    mem.cpy(s_cli.i_s, p, len);
    s_cli.i_s_len = (uint16_t)len;

    // RFC 4253 sec 7.1 parse + negotiate, the one implementation both roles consume.
    if (SSH_TRANSPORT->kexinit_parse(SSH_CLI_SLOT, p, len) != 0)
    {
        return PROTO_FALSE;
    }
    const SshSession *ns = &ssh_sess[SSH_CLI_SLOT];
    s_cli.kex = ns->kex_alg;
    s_cli.hostkey = ns->hostkey_alg;
    s_cli.cipher = ns->cipher_alg_s2c;
    s_cli.mac = ns->mac_alg_s2c;

    if (!build_kex_public())
    {
        return PROTO_FALSE;
    }

#if PC_ENABLE_PQC_KEX
    if (s_cli.kex == SSH_KEX_MLKEM768_X25519)
    {
        // KEX_HYBRID_INIT (msg 30): string(C_INIT) where C_INIT = ek || Q_C (1216 B). Too large for
        // the stack packet buffer, so build it in the client's scratch arena.
        const uint8_t *ek = s_cli.hyb.mlkem_dk + 1152; // ek follows the 1152-byte dk_pke in dk
        const size_t clen = MLKEM768_EK_BYTES + 32;
        const size_t plen = 1 + 4 + clen;
        size_t mark = pc_plaintext_mark();
        uint8_t *out = (uint8_t *)pc_plaintext_alloc(plen, 1);
        if (!out)
        {
            return PROTO_FALSE;
        }
        pc_span w = pc_span_from(out, plen);
        pc_bw_put(&w, SSH_MSG_KEXDH_INIT);
        pc_bw_put_be(&w, (uint32_t)clen, 4);
        pc_bw_bytes(&w, ek, MLKEM768_EK_BYTES);
        pc_bw_bytes(&w, s_cli.qc, 32);
        proto_bool ok = pc_span_ok(w) && cli_send(out, w.pos);
        pc_plaintext_release(mark);
        return ok;
    }
#endif
#if PC_ENABLE_SSH_SNTRUP761
    if (s_cli.kex == SSH_KEX_SNTRUP761_X25519)
    {
        // KEX_HYBRID_INIT (msg 30): string(C_INIT) where C_INIT = sntrup761_pk || Q_C (1190 B). pk is
        // reconstructed from sk (it is embedded there); too large for the stack packet buffer, so the
        // packet is built in the client's scratch arena.
        const uint8_t *pk = s_cli.hyb.sntrup_sk + PC_SNTRUP761_SK_PK_OFFSET;
        const size_t clen = PC_SNTRUP761_PK_BYTES + 32;
        const size_t plen = 1 + 4 + clen;
        size_t mark = pc_plaintext_mark();
        uint8_t *out = (uint8_t *)pc_plaintext_alloc(plen, 1);
        if (!out)
        {
            return PROTO_FALSE;
        }
        pc_span w = pc_span_from(out, plen);
        pc_bw_put(&w, SSH_MSG_KEXDH_INIT);
        pc_bw_put_be(&w, (uint32_t)clen, 4);
        pc_bw_bytes(&w, pk, PC_SNTRUP761_PK_BYTES);
        pc_bw_bytes(&w, s_cli.qc, 32);
        proto_bool ok = pc_span_ok(w) && cli_send(out, w.pos);
        pc_plaintext_release(mark);
        return ok;
    }
#endif

    // KEXDH_INIT (msg 30): string(Q_C) for curve/ecdh, mpint(e) for DH.
    uint8_t out[1 + 4 + 260];
    pc_span w = pc_span_from(out, sizeof(out));
    pc_bw_put(&w, SSH_MSG_KEXDH_INIT);
    if (s_cli.kex == SSH_KEX_DH_GROUP14)
    {
        // mpint(e): minimal, with a sign byte if the top bit is set.
        uint32_t i = 0;
        while (i < s_cli.qc_len && s_cli.qc[i] == 0)
        {
            i++;
        }
        size_t mag = s_cli.qc_len - i;
        proto_bool pad = (mag > 0 && (s_cli.qc[i] & 0x80) != 0);
        pc_bw_put_be(&w, (uint32_t)(mag + (pad ? 1 : 0)), 4);
        if (pad)
        {
            pc_bw_put(&w, 0);
        }
        pc_bw_bytes(&w, s_cli.qc + i, mag);
    }
    else
    {
        pc_ssh_wr_str(&w, s_cli.qc, s_cli.qc_len);
    }
    return pc_span_ok(w) && cli_send(out, w.pos);
}

// ---------------------------------------------------------------------------
// KEXDH_REPLY: compute K, exchange hash H, verify the host signature, derive keys, send NEWKEYS
// ---------------------------------------------------------------------------

// Compute the shared secret K (right-aligned into k_be[256]) for the negotiated method.
static proto_bool compute_k(const uint8_t *srv_pub, uint32_t srv_pub_len, uint8_t k_be[256])
{
    mem.set(k_be, 0, 256);
    switch (s_cli.kex)
    {
    case SSH_KEX_CURVE25519: {
        if (srv_pub_len != 32)
        {
            return PROTO_FALSE;
        }
        uint8_t k32[32];
        pc_x25519(k32, s_cli.kex_priv, srv_pub);
        // Reject a low-order server point (all-zero shared secret) - RFC 7748 §6.1.
        uint8_t zacc = 0;
        for (int b = 0; b < 32; b++)
        {
            zacc |= k32[b];
        }
        if (zacc == 0)
        {
            pc_secure_wipe(k32, 32);
            return PROTO_FALSE;
        }
        mem.cpy(k_be + (256 - 32), k32, 32);
        pc_secure_wipe(k32, 32);
        return PROTO_TRUE;
    }
    case SSH_KEX_ECDH_NISTP256: {
        if (srv_pub_len != PC_ECDSA_P256_PUB_LEN)
        {
            return PROTO_FALSE;
        }
        uint8_t k32[PC_ECDSA_P256_COORD_LEN];
        if (!pc_ecdsa_p256_ecdh(k32, srv_pub, s_cli.kex_priv))
        {
            return PROTO_FALSE;
        }
        mem.cpy(k_be + (256 - 32), k32, 32);
        pc_secure_wipe(k32, 32);
        return PROTO_TRUE;
    }
    case SSH_KEX_DH_GROUP14: {
        pc_bignum f, x, K;
        bn_from_bytes(&f, srv_pub, srv_pub_len);
        if (bn_dh_validate(&f) != 0) // 0 = valid (1 < f < p-1)
        {
            return PROTO_FALSE;
        }
        bn_from_bytes(&x, s_cli.kex_priv, 32);
        bn_expmod_group14(&K, &f, &x);
        bn_to_bytes(k_be, &K);
        pc_secure_wipe(&x, sizeof(x));
        pc_secure_wipe(&K, sizeof(K));
        return PROTO_TRUE;
    }
#if PC_ENABLE_PQC_KEX
    case SSH_KEX_MLKEM768_X25519: {
        // S_REPLY = ciphertext(1088) || Q_S(32). Decaps recovers K_PQ; X25519 gives K_CL. The hybrid's
        // combined secret K = SHA256(K_PQ || K_CL) is a fixed 32-byte string (right-aligned in k_be).
        if (srv_pub_len != MLKEM768_CT_BYTES + 32)
        {
            return PROTO_FALSE;
        }
        uint8_t *work = cli_crypto_work();
        if (work == NULL)
        {
            return PROTO_FALSE;
        }
        uint8_t k_pq[32], k_cl[32];
        pc_mlkem768_decaps(s_cli.hyb.mlkem_dk, srv_pub, k_pq);
        pc_x25519(k_cl, s_cli.kex_priv, srv_pub + MLKEM768_CT_BYTES);
        // Reject a low-order server point (all-zero shared secret) - RFC 7748 §6.1.
        uint8_t zacc = 0;
        for (int b = 0; b < 32; b++)
        {
            zacc |= k_cl[b];
        }
        if (zacc == 0)
        {
            pc_secure_wipe(k_pq, sizeof(k_pq));
            pc_secure_wipe(k_cl, sizeof(k_cl));
            return PROTO_FALSE;
        }
        pc_sha256_ctx c;
        pc_sha256_init(&c, work);
        pc_sha256_update(&c, k_pq, 32);
        pc_sha256_update(&c, k_cl, 32);
        pc_sha256_final(&c, k_be + (256 - 32));
        pc_secure_wipe(k_pq, sizeof(k_pq));
        pc_secure_wipe(k_cl, sizeof(k_cl));
        return PROTO_TRUE;
    }
#endif
#if PC_ENABLE_SSH_SNTRUP761
    case SSH_KEX_SNTRUP761_X25519: {
        // S_REPLY = ciphertext(1039) || Q_S(32). Decaps recovers K_PQ; X25519 gives K_CL. The combined
        // secret K = SHA512(K_PQ || K_CL) is a fixed 64-byte string (right-aligned in k_be).
        if (srv_pub_len != PC_SNTRUP761_CT_BYTES + 32)
        {
            return PROTO_FALSE;
        }
        uint8_t *work = cli_crypto_work();
        if (work == NULL)
        {
            return PROTO_FALSE;
        }
        uint8_t k_pq[PC_SNTRUP761_SS_BYTES], k_cl[32];
        pc_sntrup761_dec(work, s_cli.hyb.sntrup_sk, srv_pub, k_pq);
        pc_x25519(k_cl, s_cli.kex_priv, srv_pub + PC_SNTRUP761_CT_BYTES);
        // Reject a low-order server point (all-zero shared secret) - RFC 7748 §6.1.
        uint8_t zacc = 0;
        for (int b = 0; b < 32; b++)
        {
            zacc |= k_cl[b];
        }
        if (zacc == 0)
        {
            pc_secure_wipe(k_pq, sizeof(k_pq));
            pc_secure_wipe(k_cl, sizeof(k_cl));
            return PROTO_FALSE;
        }
        pc_sha512_ctx c;
        pc_sha512_init(&c, work);
        pc_sha512_update(&c, k_pq, sizeof(k_pq));
        pc_sha512_update(&c, k_cl, 32);
        pc_sha512_final(&c, k_be + (256 - 64));
        pc_secure_wipe(k_pq, sizeof(k_pq));
        pc_secure_wipe(k_cl, sizeof(k_cl));
        return PROTO_TRUE;
    }
#endif
    }
    return PROTO_FALSE;
}

// Compute the exchange hash H over the negotiated method's field encodings (RFC 4253 §8 / RFC 8731),
// under the method's hash (SHA-256, or SHA-512 for sntrup761x25519-sha512). Returns the digest length.
static size_t compute_h(const uint8_t *ks, uint32_t ks_len, const uint8_t *srv_pub, uint32_t srv_pub_len,
                        const uint8_t *k_be, uint8_t H[SSH_KEXHASH_MAX_LEN])
{
    uint8_t *work = cli_crypto_work();
    if (work == NULL)
    {
        return 0;
    }
    const proto_bool is512 = cli_kex_is_sha512(s_cli.kex);
    SshKexHash c;
    ssh_kexhash_init(&c, work, is512);
    hash_string(&c, (const uint8_t *)CLIENT_BANNER, strnlen(CLIENT_BANNER, sizeof(CLIENT_BANNER))); // V_C
    hash_string(&c, (const uint8_t *)s_cli.v_s, s_cli.v_s_len);                                     // V_S
    hash_string(&c, s_cli.i_c, s_cli.i_c_len);                                                      // I_C
    hash_string(&c, s_cli.i_s, s_cli.i_s_len);                                                      // I_S
    hash_string(&c, ks, ks_len);                                                                    // K_S
#if PC_ENABLE_PQC_KEX || PC_ENABLE_SSH_SNTRUP761
    proto_bool hybrid = PROTO_FALSE;
    const uint8_t *cpk = NULL; // the hybrid public embedded in the C_INIT string (ek / sntrup761 pk)
    size_t cpk_len = 0, k_slen = 0;
#if PC_ENABLE_PQC_KEX
    if (s_cli.kex == SSH_KEX_MLKEM768_X25519)
    {
        hybrid = PROTO_TRUE;
        cpk = s_cli.hyb.mlkem_dk + 1152; // ek follows the 1152-byte dk_pke
        cpk_len = MLKEM768_EK_BYTES;
        k_slen = 32; // K = SHA256(K_PQ || K_CL), 32-byte string
    }
#endif
#if PC_ENABLE_SSH_SNTRUP761
    if (s_cli.kex == SSH_KEX_SNTRUP761_X25519)
    {
        hybrid = PROTO_TRUE;
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
        if (s_cli.kex == SSH_KEX_DH_GROUP14)
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
static proto_bool verify_host_sig(const uint8_t *ks, uint32_t ks_len, const uint8_t *sig, uint32_t sig_len,
                                  const uint8_t *H, size_t h_len)
{
    Rd rk = {ks, ks_len, 0, PROTO_TRUE};
    uint32_t tn;
    const uint8_t *ktype = r_string(&rk, &tn);
    Rd rs = {sig, sig_len, 0, PROTO_TRUE};
    uint32_t sn;
    const uint8_t *stype = r_string(&rs, &sn);
    if (!rk.ok || !rs.ok)
    {
        return PROTO_FALSE;
    }

    switch (s_cli.hostkey)
    {
    case SSH_HOSTKEY_ED25519: {
        uint32_t pn;
        const uint8_t *pub = r_string(&rk, &pn);
        uint32_t rl;
        const uint8_t *raw = r_string(&rs, &rl);
        uint8_t *vwork = cli_crypto_work();
        return vwork != NULL && rk.ok && rs.ok && pn == 32 && rl == 64 && pc_ed25519_verify(vwork, pub, H, h_len, raw);
    }
    case SSH_HOSTKEY_ECDSA_NISTP256: {
        uint32_t cn;
        r_string(&rk, &cn); // "nistp256"
        uint32_t qn;
        const uint8_t *q = r_string(&rk, &qn);
        // signature: string( mpint(r) || mpint(s) ) -> 64-byte raw r||s.
        uint32_t bl;
        const uint8_t *blob = r_string(&rs, &bl);
        if (!rk.ok || !rs.ok || qn != PC_ECDSA_P256_PUB_LEN)
        {
            return PROTO_FALSE;
        }
        Rd rb = {blob, bl, 0, PROTO_TRUE};
        uint32_t rlen, slen;
        const uint8_t *rr = r_string(&rb, &rlen);
        const uint8_t *ss = r_string(&rb, &slen);
        uint8_t raw[64];
        if (!rb.ok || !pc_mpint_to_fixed(rr, rlen, raw, 32) || !pc_mpint_to_fixed(ss, slen, raw + 32, 32))
        {
            return PROTO_FALSE;
        }
        uint8_t *work = cli_crypto_work();
        return work != NULL && pc_ecdsa_p256_verify(q, work, H, h_len, raw);
    }
    case SSH_HOSTKEY_RSA_SHA256:
    case SSH_HOSTKEY_RSA_SHA512: {
        // K_S = string("ssh-rsa") || mpint(e) || mpint(n).
        uint32_t elen, nlen;
        const uint8_t *e = r_string(&rk, &elen);
        const uint8_t *n = r_string(&rk, &nlen);
        uint32_t rawlen;
        const uint8_t *raw = r_string(&rs, &rawlen); // the RSA signature bytes
        (void)ktype;
        (void)stype;
        uint8_t e4[4], n256[256];
        if (!rk.ok || !rs.ok || !pc_mpint_to_fixed(e, elen, e4, 4) || !pc_mpint_to_fixed(n, nlen, n256, 256))
        {
            return PROTO_FALSE;
        }
        pc_rsa_hash h = (s_cli.hostkey == SSH_HOSTKEY_RSA_SHA512) ? PC_RSA_HASH_SHA512 : PC_RSA_HASH_SHA256;
        uint8_t *work = cli_crypto_work();
        return work != NULL && pc_rsa_verify(n256, e4, work, H, h_len, raw, rawlen, h) == 0;
    }
    }
    return PROTO_FALSE;
}

static proto_bool handle_kexdh_reply(const uint8_t *p, size_t len)
{
    Rd r = {p, len, 0, PROTO_TRUE};
    if (r_u8(&r) != SSH_MSG_KEXDH_REPLY)
    {
        return PROTO_FALSE;
    }
    uint32_t ks_len;
    const uint8_t *ks = r_string(&r, &ks_len); // K_S host-key blob
    uint32_t sp_len;
    const uint8_t *srv_pub = r_string(&r, &sp_len); // Q_S (string) or f (mpint)
    uint32_t sig_len;
    const uint8_t *sig = r_string(&r, &sig_len); // signature blob
    if (!r.ok)
    {
        return PROTO_FALSE;
    }

    // Pin the relay by the SHA-256 fingerprint of its host-key blob (type-agnostic, like known_hosts).
    uint8_t *fwork = cli_crypto_work();
    if (fwork == NULL)
    {
        return PROTO_FALSE;
    }
    uint8_t fp[32];
    pc_sha256_ctx fc;
    pc_sha256_init(&fc, fwork);
    pc_sha256_update(&fc, ks, ks_len);
    pc_sha256_final(&fc, fp);
    if (mem.cmp(fp, s_cli.cfg.host_pin, 32) != 0)
    {
        cli_fail("relay host key does not match the pin");
        return PROTO_FALSE;
    }

    uint8_t k_be[256];
    if (!compute_k(srv_pub, sp_len, k_be))
    {
        return PROTO_FALSE;
    }

    uint8_t H[SSH_KEXHASH_MAX_LEN];
    const size_t h_len = compute_h(ks, ks_len, srv_pub, sp_len, k_be, H); // 32 or 64 by method

    if (!verify_host_sig(ks, ks_len, sig, sig_len, H, h_len))
    {
        pc_secure_wipe(k_be, sizeof(k_be));
        cli_fail("relay signature verification failed");
        return PROTO_FALSE;
    }

    if (!s_cli.have_sid)
    {
        mem.cpy(s_cli.session_id, H, h_len);
        s_cli.session_id_len = (uint8_t)h_len;
        s_cli.have_sid = PROTO_TRUE;
    }

    // ssh_dh_derive_keys_sid populates c2s/s2c per the RFC 4253 §7.2 letters for the negotiated
    // cipher/MAC; the packet layer's is_client flag selects the send/receive direction. The hybrids
    // encode K as a fixed 32/64-byte string (k_is_string); the classical methods as an mpint. The
    // -sha512 method derives over SHA-512 (is512), so H and the session_id are 64 bytes.
    const proto_bool is512 = cli_kex_is_sha512(s_cli.kex);
    proto_bool k_is_string = PROTO_FALSE;
#if PC_ENABLE_PQC_KEX
    k_is_string = k_is_string || (s_cli.kex == SSH_KEX_MLKEM768_X25519);
#endif
#if PC_ENABLE_SSH_SNTRUP761
    k_is_string = k_is_string || (s_cli.kex == SSH_KEX_SNTRUP761_X25519);
#endif
    const SshKdfInputs kdf_in = {.work = cli_crypto_work(),
                                 .K_be = k_be,
                                 .H = H,
                                 .session_id = s_cli.session_id,
                                 .h_len = h_len,
                                 .sid_len = s_cli.session_id_len,
                                 .k_is_string = k_is_string,
                                 .is512 = is512};
    ssh_dh_derive_keys_sid(SSH_CLI_SLOT, &kdf_in);
    pc_secure_wipe(k_be, sizeof(k_be));
    pc_secure_wipe(s_cli.kex_priv, sizeof(s_cli.kex_priv));
#if PC_ENABLE_PQC_KEX || PC_ENABLE_SSH_SNTRUP761
    pc_secure_wipe((uint8_t *)&s_cli.hyb, sizeof(s_cli.hyb));
#endif

    uint8_t nk = SSH_MSG_NEWKEYS;
    if (!cli_send(&nk, 1))
    {
        return PROTO_FALSE;
    }
    SSH_TRANSPORT->newkeys_sent(SSH_CLI_SLOT); // outbound switches to the epoch this exchange derived
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Auth (publickey, ssh-ed25519)
// ---------------------------------------------------------------------------

static proto_bool send_service_request(void)
{
    uint8_t out[1 + 4 + 12];
    pc_span w = pc_span_from(out, sizeof(out));
    pc_bw_put(&w, SSH_MSG_SERVICE_REQUEST);
    pc_ssh_wr_cstr(&w, "ssh-userauth");
    return pc_span_ok(w) && cli_send(out, w.pos);
}

static proto_bool send_userauth_publickey(void)
{
    const char *user = s_cli.cfg.user;
    uint8_t *work = cli_crypto_work();
    if (work == NULL)
    {
        return PROTO_FALSE;
    }
    uint8_t pub[32];
    pc_ed25519_pubkey(work, pub, s_cli.cfg.auth_seed);

    // The device's public-key blob: string("ssh-ed25519") || string(pub32).
    uint8_t pkblob[4 + 11 + 4 + 32];
    pc_span pw = pc_span_from(pkblob, sizeof(pkblob));
    pc_ssh_wr_cstr(&pw, NAME_ED25519);
    pc_ssh_wr_str(&pw, pub, 32);
    if (!pc_span_ok(pw))
    {
        return PROTO_FALSE;
    }

    // Data to sign (RFC 4252 §7): string(session_id) || the userauth request up to (and including)
    // the public-key blob, with the "signature present" flag set. session_id is 32 or 64 bytes (the
    // -sha512 KEX), so the buffer carries SSH_KEXHASH_MAX_LEN of headroom over the 32-byte base.
    uint8_t signed_data[256 + SSH_KEXHASH_MAX_LEN];
    pc_span sd = pc_span_from(signed_data, sizeof(signed_data));
    pc_ssh_wr_str(&sd, s_cli.session_id, s_cli.session_id_len);
    pc_bw_put(&sd, SSH_MSG_USERAUTH_REQUEST);
    pc_ssh_wr_cstr(&sd, user);
    pc_ssh_wr_cstr(&sd, "ssh-connection");
    pc_ssh_wr_cstr(&sd, "publickey");
    pc_bw_put(&sd, 1); // signature present
    pc_ssh_wr_cstr(&sd, NAME_ED25519);
    pc_ssh_wr_str(&sd, pkblob, pw.pos);
    if (!pc_span_ok(sd))
    {
        return PROTO_FALSE;
    }

    uint8_t sig[64];
    pc_ed25519_sign(work, sig, signed_data, sd.pos, s_cli.cfg.auth_seed);

    // Signature blob: string("ssh-ed25519") || string(sig64).
    uint8_t sigblob[4 + 11 + 4 + 64];
    pc_span sg = pc_span_from(sigblob, sizeof(sigblob));
    pc_ssh_wr_cstr(&sg, NAME_ED25519);
    pc_ssh_wr_str(&sg, sig, 64);
    if (!pc_span_ok(sg))
    {
        return PROTO_FALSE;
    }

    // The full USERAUTH_REQUEST is the signed prefix (minus the session_id) plus the signature.
    uint8_t out[300];
    pc_span w = pc_span_from(out, sizeof(out));
    pc_bw_put(&w, SSH_MSG_USERAUTH_REQUEST);
    pc_ssh_wr_cstr(&w, user);
    pc_ssh_wr_cstr(&w, "ssh-connection");
    pc_ssh_wr_cstr(&w, "publickey");
    pc_bw_put(&w, 1);
    pc_ssh_wr_cstr(&w, NAME_ED25519);
    pc_ssh_wr_str(&w, pkblob, pw.pos);
    pc_ssh_wr_str(&w, sigblob, sg.pos);
    return pc_span_ok(w) && cli_send(out, w.pos);
}

// ---------------------------------------------------------------------------
// tcpip-forward
// ---------------------------------------------------------------------------

static proto_bool send_tcpip_forward(void)
{
    uint8_t out[128];
    pc_span w = pc_span_from(out, sizeof(out));
    pc_bw_put(&w, SSH_MSG_GLOBAL_REQUEST);
    pc_ssh_wr_cstr(&w, "tcpip-forward");
    pc_bw_put(&w, 1); // want reply
    pc_ssh_wr_cstr(&w, s_cli.cfg.bind_addr ? s_cli.cfg.bind_addr : "");
    pc_bw_put_be(&w, s_cli.cfg.bind_port, 4);
    return pc_span_ok(w) && cli_send(out, w.pos);
}

// ---------------------------------------------------------------------------
// forwarded-tcpip channel + bridge
// ---------------------------------------------------------------------------

#define SSH_CLI_WINDOW 32768u
#define SSH_CLI_MAXPKT 16384u

static void handle_channel_open(const uint8_t *p, size_t len)
{
    Rd r = {p, len, 0, PROTO_TRUE};
    r_u8(&r); // msg
    uint32_t tn;
    const uint8_t *type = r_string(&r, &tn);
    uint32_t their_id = r_u32(&r);
    uint32_t their_win = r_u32(&r);
    uint32_t their_maxpkt = r_u32(&r);
    if (!r.ok || tn != 15 || mem.cmp(type, "forwarded-tcpip", 15) != 0)
    {
        // Refuse anything else.
        uint8_t out[17];
        size_t n = 0;
        if (pc_ssh_sig_build_open_failure(out, sizeof(out), their_id, 1, &n) == 0)
        {
            cli_send(out, n);
        }
        return;
    }

    // Claim a channel in the shared table plus the bridge that carries its local socket; refuse if
    // either pool is full.
    const int cid = pc_ssh_chan_alloc(SSH_CLI_SLOT);
    CliChannel *ch = NULL;
    if (cid >= 0)
    {
        ch = bridge_alloc();
    }
    if (ch == NULL)
    {
        uint8_t out[17];
        size_t n = 0;
        if (pc_ssh_sig_build_open_failure(out, sizeof(out), their_id, 4, &n) == 0)
        {
            cli_send(out, n);
        }
        return;
    }

    // Open the local bridge connection (to the device's own service).
    int lc = Tcp.client->open("127.0.0.1", s_cli.cfg.local_port, 3000);
    PC_LOGD(LOG_TUNNEL_FWD_OPEN, ((const pc_fval[]){PC_VU32((uint32_t)s_cli.cfg.local_port), PC_VI64((int64_t)lc)}), 2);
    if (lc < 0)
    {
        uint8_t out[17];
        size_t n = 0;
        if (pc_ssh_sig_build_open_failure(out, sizeof(out), their_id, 2, &n) == 0)
        {
            cli_send(out, n);
        }
        return;
    }

    SshChannel *c = &ssh_chan[SSH_CLI_SLOT][cid];
    c->open = PROTO_TRUE;
    c->pending = PROTO_FALSE;
    c->type = SSH_CHAN_FORWARDED_TCPIP;
    c->local_id = (uint32_t)cid;
    c->peer_id = their_id;
    pc_ssh_flow_init(&c->flow, SSH_CLI_WINDOW, their_win, their_maxpkt);

    ch->used = PROTO_TRUE;
    ch->channel = c->local_id;
    ch->local_cid = lc;
    ch->eof_sent = PROTO_FALSE;
    ch->relay_eof = PROTO_FALSE; // fully self-init this slot; do not lean on channel_close having zeroed it

    uint8_t out[17];
    size_t n = 0;
    if (pc_ssh_sig_build_open_confirm(&c->flow, their_id, c->local_id, out, sizeof(out), &n) == 0)
    {
        cli_send(out, n);
    }
}

static void channel_close(CliChannel *ch)
{
    if (!ch || !ch->used)
    {
        return;
    }
    SshChannel *c = pc_ssh_chan_by_id(SSH_CLI_SLOT, ch->channel);
    if (c != NULL)
    {
        uint8_t out[10];
        size_t n = 0;
        if (pc_ssh_sig_build_close(c->peer_id, out, sizeof(out), &n) == 0)
        {
            cli_send(out, n);
        }
        c->open = PROTO_FALSE;
    }
    if (ch->local_cid >= 0)
    {
        Tcp.client->close(ch->local_cid);
    }
    mem.set(ch, 0, sizeof(*ch));
    ch->local_cid = -1;
}

// Relay -> device: CHANNEL_DATA is written to the addressed channel's local service.
static void handle_channel_data(const uint8_t *p, size_t len)
{
    Rd r = {p, len, 0, PROTO_TRUE};
    r_u8(&r);
    uint32_t rid = r_u32(&r); // our channel id (recipient)
    uint32_t dn;
    const uint8_t *d = r_string(&r, &dn);
    CliChannel *ch = bridge_by_channel(rid);
    SshChannel *c = pc_ssh_chan_by_id(SSH_CLI_SLOT, rid);
    if (!r.ok || ch == NULL || c == NULL)
    {
        return;
    }
    if (ch->local_cid >= 0 && dn)
    {
        Tcp.client->send(ch->local_cid, d, dn);
    }

    // Refill the relay's window as we consume, so it can keep sending.
    if (!pc_ssh_flow_recv_take(&c->flow, dn))
    {
        channel_close(ch);
        return;
    }
    uint32_t add = 0;
    if (pc_ssh_flow_replenish_due(&c->flow, &add))
    {
        uint8_t out[9];
        size_t n = 0;
        if (pc_ssh_sig_build_window_adjust(c->peer_id, add, out, sizeof(out), &n) == 0 && cli_send(out, n))
        {
            pc_ssh_flow_local_credit(&c->flow, add);
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
    SshChannel *c = pc_ssh_chan_by_id(SSH_CLI_SLOT, ch->channel);
    if (c == NULL)
    {
        return;
    }
    uint8_t buf[1024];
    for (int burst = 0; burst < 4; burst++)
    {
        uint32_t want = pc_ssh_flow_send_cap(&c->flow, (uint32_t)sizeof(buf));
        if (want == 0)
        {
            break;
        }
        size_t got = Tcp.client->read(ch->local_cid, buf, want);
        if (got == 0)
        {
            break;
        }
        uint8_t payload[9 + sizeof(buf)];
        size_t n = 0;
        if (pc_ssh_sig_build_data(&c->flow, c->peer_id, buf, got, payload, sizeof(payload), &n) != 0)
        {
            break;
        }
        if (!cli_send(payload, n))
        {
            break;
        }
    }
    // Tear the channel down once its reply has drained and either side is done: the local service
    // closed, or the relay half-closed (relay_eof - the forwarded peer finished, e.g. an HTTP client
    // that already has the full response). The drain loop above exits with got==0, so all currently
    // available local bytes have been forwarded before we half-close.
    proto_bool local_done = Tcp.client->is_closed(ch->local_cid) && Tcp.client->available(ch->local_cid) == 0;
    if ((local_done || ch->relay_eof) && !ch->eof_sent)
    {
        ch->eof_sent = PROTO_TRUE;
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
    // Set by every arm that consumes the message; what is left is answered per RFC 4253 sec 11.4.
    proto_bool handled = PROTO_FALSE;
    switch (ssh_sess[SSH_CLI_SLOT].phase)
    {
    case SSH_PHASE_KEXINIT:
        if (type == SSH_MSG_KEXINIT)
        {
            handled = PROTO_TRUE;
            if (handle_server_kexinit(payload, len))
            {
                ssh_sess[SSH_CLI_SLOT].phase = SSH_PHASE_DH_INIT;
            }
            else
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
                ssh_sess[SSH_CLI_SLOT].phase = SSH_PHASE_NEWKEYS;
            }
            else if (s_cli.state != PC_TUN_FAILED)
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
            if (SSH_TRANSPORT->newkeys_recvd(SSH_CLI_SLOT) != 0) // -> SSH_PHASE_SERVICE
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
                ssh_sess[SSH_CLI_SLOT].phase = SSH_PHASE_AUTH;
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
                ssh_sess[SSH_CLI_SLOT].phase = SSH_PHASE_OPEN;
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
        // the tunnel reports; the phase is already OPEN (RFC 4254 sec 7.1 is a channel-layer request).
        if (s_cli.state != PC_TUN_UP)
        {
            if (type == SSH_MSG_REQUEST_SUCCESS)
            {
                handled = PROTO_TRUE;
                s_cli.state = PC_TUN_UP;
                PC_LOGI(LOG_TUNNEL_UP, ((const pc_fval[]){PC_VU32((uint32_t)s_cli.cfg.bind_port)}), 1);
            }
            else if (type == SSH_MSG_REQUEST_FAILURE)
            {
                handled = PROTO_TRUE;
                cli_fail("relay refused the remote forward");
            }
            break;
        }
        switch (type)
        {
        case SSH_MSG_CHANNEL_OPEN:
            handled = PROTO_TRUE;
            handle_channel_open(payload, len);
            break;
        case SSH_MSG_CHANNEL_DATA:
            handled = PROTO_TRUE;
            handle_channel_data(payload, len);
            break;
        case SSH_MSG_CHANNEL_WINDOW_ADJUST: {
            handled = PROTO_TRUE;
            Rd r = {payload, len, 0, PROTO_TRUE};
            r_u8(&r);
            uint32_t rid = r_u32(&r);
            uint32_t add = r_u32(&r);
            SshChannel *c = pc_ssh_chan_by_id(SSH_CLI_SLOT, rid);
            if (r.ok && c != NULL)
            {
                pc_ssh_flow_peer_add(&c->flow, add);
            }
            break;
        }
        case SSH_MSG_CHANNEL_EOF: {
            handled = PROTO_TRUE;
            // The relay's write side closed - the forwarded peer is done sending (for a request/
            // response bridge, the response has already been delivered). Mark it so the channel tears
            // down as soon as the local reply drains, instead of lingering until the relay's CLOSE and
            // starving the channel pool. A keep-alive local server never closes on its own.
            Rd r = {payload, len, 0, PROTO_TRUE};
            r_u8(&r);
            uint32_t rid = r_u32(&r);
            CliChannel *ch = bridge_by_channel(rid);
            if (r.ok && ch != NULL)
            {
                ch->relay_eof = PROTO_TRUE;
            }
            break;
        }
        case SSH_MSG_CHANNEL_CLOSE: {
            handled = PROTO_TRUE;
            Rd r = {payload, len, 0, PROTO_TRUE};
            r_u8(&r);
            uint32_t rid = r_u32(&r);
            if (r.ok)
            {
                channel_close(bridge_by_channel(rid));
            }
            break;
        }
        case SSH_MSG_GLOBAL_REQUEST: {
            handled = PROTO_TRUE;
            // A want_reply keepalive gets a REQUEST_FAILURE (we support no global requests inbound).
            Rd r = {payload, len, 0, PROTO_TRUE};
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

    // RFC 4253 sec 11.4: a message no arm took is answered with SSH_MSG_UNIMPLEMENTED carrying its
    // sequence number. ssh_pkt_recv has already counted this packet, so it is seq_no_recv - 1.
    if (!handled)
    {
        uint8_t out[5];
        pc_span w = pc_span_from(out, sizeof(out));
        pc_bw_put(&w, SSH_MSG_UNIMPLEMENTED);
        pc_bw_put_be(&w, ssh_pkt[SSH_CLI_SLOT].seq_no_recv - 1u, 4);
        if (pc_span_ok(w))
        {
            (void)cli_send(out, w.pos);
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

proto_bool pc_ssh_tunnel_begin(const pc_ssh_tunnel_cfg *cfg)
{
    if (!cfg || !cfg->host || !cfg->user || !cfg->auth_seed || !cfg->host_pin)
    {
        return PROTO_FALSE;
    }

    pc_ssh_tunnel_end();
    mem.set(&s_cli, 0, sizeof(s_cli));
    s_cli.cfg = *cfg;
    for (int i = 0; i < PC_SSH_CLIENT_MAX_CHANNELS; i++)
    {
        s_cli.chan[i].local_cid = -1;
    }
    pc_ssh_channel_init(SSH_CLI_SLOT); // the channel table this slot's bridges hang off

    // Own a dedicated scratch arena, distinct from the server's worker(s): packet decryption borrows
    // from the shared scratch, and that arena is single-accessor-per-task. begin() and poll() run in
    // the same task, so claiming the slot here makes every later decrypt use the client's own arena.
    pc_worker_set_self(PC_GHOST_WORKER_SLOT);

    uint16_t port = cfg->port ? cfg->port : 22;
    s_cli.cid = Tcp.client->open(cfg->host, port, 8000);
    if (s_cli.cid < 0)
    {
        s_cli.state = PC_TUN_FAILED;
        return PROTO_FALSE;
    }

    ssh_transport_init(SSH_CLI_SLOT); // -> SSH_PHASE_BANNER
    ssh_pkt_init(SSH_CLI_SLOT);
    ssh_pkt_set_client(SSH_CLI_SLOT);
    ssh_keymat_wipe(SSH_CLI_SLOT);

    // Send our identification string, then our KEXINIT.
    char banner[64];
    size_t n = frame.build(banner, sizeof(banner), CLI_BANNER, (const pc_fval[]){PC_VSTR(CLIENT_BANNER)}, 1);
    if (n == 0 || !Tcp.client->send(s_cli.cid, (const uint8_t *)banner, n))
    {
        cli_fail("banner send failed");
        return PROTO_FALSE;
    }
    s_cli.state = PC_TUN_CONNECTING;
    s_cli.deadline_ms = pc_millis() + 15000;
    return PROTO_TRUE;
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
            if (l >= 4 && mem.cmp(s_cli.banner, "SSH-", 4) == 0)
            {
                mem.cpy(s_cli.v_s, s_cli.banner, l);
                s_cli.v_s_len = l;
                *consumed = i + 1;
                ssh_sess[SSH_CLI_SLOT].phase = SSH_PHASE_KEXINIT;
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
    if (s_cli.cid < 0 || s_cli.state == PC_TUN_IDLE || s_cli.state == PC_TUN_FAILED)
    {
        return;
    }

    if (Tcp.client->is_closed(s_cli.cid) && Tcp.client->available(s_cli.cid) == 0)
    {
        cli_fail("relay closed the connection");
        return;
    }
    if (s_cli.state == PC_TUN_CONNECTING && (int32_t)(pc_millis() - s_cli.deadline_ms) > 0)
    {
        cli_fail("handshake timed out");
        return;
    }

    uint8_t buf[1024];
    size_t got = Tcp.client->read(s_cli.cid, buf, sizeof(buf));
    if (got)
    {
        size_t off = 0;
        if (ssh_sess[SSH_CLI_SLOT].phase == SSH_PHASE_BANNER)
        {
            size_t consumed = 0;
            drain_banner(buf, got, &consumed);
            off = consumed;
        }
        if (off < got && s_cli.state != PC_TUN_FAILED)
        {
            if (ssh_pkt_recv(SSH_CLI_SLOT, buf + off, got - off, cli_msg_handler, &ssh_sess[SSH_CLI_SLOT].in) != 0)
            {
                cli_fail("packet error (MAC / framing)");
            }
        }
    }

    if (s_cli.state == PC_TUN_UP)
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
            Tcp.client->close(s_cli.chan[i].local_cid);
        }
    }
    if (s_cli.cid >= 0)
    {
        Tcp.client->close(s_cli.cid);
    }
    ssh_keymat_wipe(SSH_CLI_SLOT);
    pc_secure_wipe(s_cli.kex_priv, sizeof(s_cli.kex_priv));
    mem.set(&s_cli, 0, sizeof(s_cli));
    s_cli.cid = -1;
    s_cli.state = PC_TUN_IDLE;
}

pc_ssh_tunnel_state pc_ssh_tunnel_state_get(void)
{
    return s_cli.state;
}

proto_bool pc_ssh_tunnel_up(void)
{
    return s_cli.state == PC_TUN_UP;
}

// Key derivation for provisioning: the seed's public half, without a tunnel.
void pc_ssh_tunnel_pubkey(const uint8_t seed[32], uint8_t pub[32])
{
    uint8_t *work = cli_crypto_work();
    if (work == NULL)
    {
        mem.zero(pub, 32);
        return;
    }
    pc_ed25519_pubkey(work, pub, seed);
}

#endif // PC_ENABLE_SSH_CLIENT
