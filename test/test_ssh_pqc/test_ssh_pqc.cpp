// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// End-to-end test of the mlkem768x25519-sha256 SSH hybrid key exchange (draft-ietf-sshm-mlkem-hybrid-
// kex), verified the way a conforming client would: the server runs the real KEX, then the test acts
// as the client - decapsulating the server's ML-KEM ciphertext (mlkem_ref), redoing the X25519, forming
// K = SHA256(K_PQ || K_CL), rebuilding the exchange hash H with C_INIT / S_REPLY / K as SSH strings, and
// verifying the server's ed25519 signature over H. It also replays the RFC 4253 §7.2 KDF with a string-
// encoded K to confirm the derived session key. A wrong byte anywhere - C_INIT/S_REPLY order, the K_PQ||
// K_CL order, string-vs-mpint K, K alignment - fails the check. Pure host test; needs a valid ML-KEM
// key pair, so it reuses the kyber-py KAT fixture (ek, dk).

#include "../test_pqc_mlkem/mlkem_kat.h" // kat_ek, kat_dk, kat_ct, kat_ss
#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ed25519.h"
#include "crypto/hash/sha256.h"
#include "crypto/hash/sha512.h"   // sntrup761x25519-sha512 exchange hash
#include "crypto/pqc/mlkem.h"     // MLKEM768_EK_BYTES / MLKEM768_CT_BYTES
#include "crypto/pqc/sntrup761.h" // the other PQ/T hybrid (NTRU Prime + X25519)
#include "mlkem_ref.h"
#include "network_drivers/presentation/ssh/transport/ssh_dh.h"
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h" // SSH_MSG_KEXDH_INIT / _REPLY
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static size_t put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    return 4;
}
static size_t put_string(uint8_t *p, const uint8_t *d, uint32_t n)
{
    put_u32(p, n);
    memcpy(p + 4, d, n);
    return 4 + n;
}
static size_t put_mpint(uint8_t *p, const uint8_t *be, size_t n)
{
    size_t off = 0;
    while (off < n && be[off] == 0)
    {
        off++;
    }
    if (off == n)
    {
        return put_u32(p, 0);
    }
    int pad = (be[off] & 0x80) ? 1 : 0;
    put_u32(p, (uint32_t)(n - off) + (uint32_t)pad);
    size_t o = 4;
    if (pad)
    {
        p[o++] = 0;
    }
    memcpy(p + o, be + off, n - off);
    return o + (n - off);
}
static bool rd_string(const uint8_t *b, size_t len, size_t *off, const uint8_t **d, uint32_t *dl)
{
    if (*off + 4 > len)
    {
        return false;
    }
    uint32_t n = ((uint32_t)b[*off] << 24) | ((uint32_t)b[*off + 1] << 16) | ((uint32_t)b[*off + 2] << 8) | b[*off + 3];
    *off += 4;
    if (*off + n > len)
    {
        return false;
    }
    *d = b + *off;
    *dl = n;
    *off += n;
    return true;
}
static size_t put_namelist(uint8_t *p, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    put_u32(p, n);
    memcpy(p + 4, s, n);
    return 4 + n;
}

// A minimal client KEXINIT offering @p kex first (chacha20-poly1305 needs no separate MAC).
static size_t build_client_kexinit(uint8_t *out, const char *kex)
{
    size_t o = 0;
    out[o++] = SSH_MSG_KEXINIT;
    memset(out + o, 0x11, 16); // cookie
    o += 16;
    o += put_namelist(out + o, kex);
    o += put_namelist(out + o, "ssh-ed25519,rsa-sha2-256");
    o += put_namelist(out + o, "chacha20-poly1305@openssh.com,aes256-ctr");
    o += put_namelist(out + o, "chacha20-poly1305@openssh.com,aes256-ctr");
    o += put_namelist(out + o, "hmac-sha2-256");
    o += put_namelist(out + o, "hmac-sha2-256");
    o += put_namelist(out + o, "none");
    o += put_namelist(out + o, "none");
    o += put_namelist(out + o, "");
    o += put_namelist(out + o, "");
    out[o++] = 0; // first_kex_packet_follows
    o += put_u32(out + o, 0);
    return o;
}

// Independent RFC 4253 §7.2 KDF with K encoded as a plain string (the hybrid rule): K1 = HASH(string(K)
// || H || label || sid), Ki+1 = HASH(string(K) || H || K1..Ki).
static void kdf_ref_string(const uint8_t Kb[32], const uint8_t H[32], const uint8_t sid[32], char label, uint8_t *out,
                           size_t outlen)
{
    uint8_t acc[128];
    size_t have = 0;
    uint8_t buf[4 + 32 + 32 + 1 + 32 + 128];
    size_t o = 0;
    o += put_string(buf, Kb, 32);
    memcpy(buf + o, H, 32);
    o += 32;
    buf[o++] = (uint8_t)label;
    memcpy(buf + o, sid, 32);
    o += 32;
    pc_sha256(buf, o, acc);
    have = 32;
    while (have < outlen)
    {
        o = put_string(buf, Kb, 32);
        memcpy(buf + o, H, 32);
        o += 32;
        memcpy(buf + o, acc, have);
        o += have;
        pc_sha256(buf, o, acc + have);
        have += 32;
    }
    memcpy(out, acc, outlen);
}

// The reference decaps recovers the pinned shared secret from the pinned key + ciphertext, so it is a
// trustworthy client for the end-to-end test.
void test_decaps_ref_matches_kat()
{
    uint8_t ss[32];
    pc_mlkem768_decaps_ref(kat_dk, kat_ct, ss);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kat_ss, ss, 32);
}

// A client that offers the hybrid gets it negotiated over classical curve25519.
void test_hybrid_negotiated()
{
    ssh_transport_init(0);
    pc_ssh_hostkey_ed25519_set(kat_m); // any 32-byte seed; just enables ed25519 host-key negotiation
    uint8_t buf[512];
    size_t n = build_client_kexinit(buf, "mlkem768x25519-sha256,curve25519-sha256");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SshKexAlg::SSH_KEX_MLKEM768_X25519, ssh_sess[0].kex_alg);
}

// A client that does not offer the hybrid still negotiates a classical method (no PQC is forced).
void test_hybrid_absent_falls_back()
{
    ssh_transport_init(0);
    pc_ssh_hostkey_ed25519_set(kat_m);
    uint8_t buf[512];
    size_t n = build_client_kexinit(buf, "curve25519-sha256");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SshKexAlg::SSH_KEX_CURVE25519, ssh_sess[0].kex_alg);
}

void test_hybrid_kex_end_to_end()
{
    static const uint8_t seed[32] = {0x9a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60, 0x71, 0x82, 0x93, 0xa4,
                                     0xb5, 0xc6, 0xd7, 0xe8, 0xf9, 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f,
                                     0x60, 0x71, 0x82, 0x93, 0xa4, 0xb5, 0xc6, 0xd7, 0xe8, 0xf9};
    ssh_transport_init(0);
    pc_ssh_hostkey_ed25519_set(seed);
    SshSession *s = &ssh_sess[0];
    s->kex_alg = SshKexAlg::SSH_KEX_MLKEM768_X25519;
    s->hostkey_alg = SshHostkeyAlg::SSH_HOSTKEY_ED25519;
    s->cipher_alg = SSH_CIPHER_CHACHA20POLY1305;

    const char *vc = "SSH-2.0-HybridClient";
    strcpy(s->v_c, vc);
    s->v_c_len = (uint16_t)strlen(vc);
    for (int j = 0; j < 30; j++)
    {
        s->i_c[j] = (uint8_t)(j + 1);
        s->i_s[j] = (uint8_t)(j + 100);
    }
    s->i_c_len = s->i_s_len = 30;

    // Server ephemeral (X25519 half). Client uses the KAT ML-KEM key pair + a fixed X25519 scalar.
    TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));
    uint8_t client_sk[32];
    uint8_t qc[32];
    for (int j = 0; j < 32; j++)
    {
        client_sk[j] = (uint8_t)(0x40 + j);
    }
    pc_x25519_base(qc, client_sk);

    // SSH_MSG_KEX_HYBRID_INIT: byte(30) || string(C_INIT), C_INIT = ek(1184) || Q_C(32).
    static uint8_t c_init[MLKEM768_EK_BYTES + 32];
    memcpy(c_init, kat_ek, MLKEM768_EK_BYTES);
    memcpy(c_init + MLKEM768_EK_BYTES, qc, 32);
    static uint8_t pkt[1 + 4 + MLKEM768_EK_BYTES + 32];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    size_t plen = 1 + put_string(pkt + 1, c_init, sizeof(c_init));

    static uint8_t reply[2048];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexdh_handle(0, pkt, plen, reply, &rlen, sizeof(reply)));
    TEST_ASSERT_EQUAL(SSH_MSG_KEXDH_REPLY, reply[0]);
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_NEWKEYS, s->phase);
    TEST_ASSERT_TRUE(ssh_keys[0].active);

    // Parse reply: string(K_S) || string(S_REPLY) || string(sigblob).
    size_t off = 1;
    const uint8_t *ks, *s_reply, *sigblob;
    uint32_t ks_len, sr_len, sb_len;
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &ks, &ks_len));
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &s_reply, &sr_len));
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &sigblob, &sb_len));
    TEST_ASSERT_EQUAL_UINT32(MLKEM768_CT_BYTES + 32, sr_len); // S_REPLY = ciphertext(1088) || Q_S(32)

    const uint8_t *ct = s_reply;
    const uint8_t *qs = s_reply + MLKEM768_CT_BYTES;

    // Client combiner: K_PQ from decaps, K_CL from X25519, K = SHA256(K_PQ || K_CL).
    uint8_t k_pq[32];
    pc_mlkem768_decaps_ref(kat_dk, ct, k_pq);
    uint8_t k_cl[32];
    pc_x25519(k_cl, client_sk, qs);
    uint8_t kin[64];
    memcpy(kin, k_pq, 32);
    memcpy(kin + 32, k_cl, 32);
    uint8_t K[32];
    pc_sha256(kin, sizeof(kin), K);

    // Recover the host public key from K_S = string("ssh-ed25519") || string(pub32).
    size_t ko = 0;
    const uint8_t *kt, *hostpub;
    uint32_t kt_len, hp_len;
    TEST_ASSERT_TRUE(rd_string(ks, ks_len, &ko, &kt, &kt_len));
    TEST_ASSERT_EQUAL_MEMORY("ssh-ed25519", kt, 11);
    TEST_ASSERT_TRUE(rd_string(ks, ks_len, &ko, &hostpub, &hp_len));
    TEST_ASSERT_EQUAL_UINT32(32, hp_len);

    // Rebuild H = HASH(V_C, V_S, I_C, I_S, K_S, string(C_INIT), string(S_REPLY), string(K)).
    static uint8_t pre[4096];
    size_t o = 0;
    o += put_string(pre + o, (const uint8_t *)vc, (uint32_t)strlen(vc));
    o += put_string(pre + o, (const uint8_t *)SSH_SERVER_VERSION, (uint32_t)strlen(SSH_SERVER_VERSION));
    o += put_string(pre + o, s->i_c, s->i_c_len);
    o += put_string(pre + o, s->i_s, s->i_s_len);
    o += put_string(pre + o, ks, ks_len);
    o += put_string(pre + o, c_init, sizeof(c_init)); // C_INIT (string)
    o += put_string(pre + o, s_reply, sr_len);        // S_REPLY (string)
    o += put_string(pre + o, K, 32);                  // K (string, NOT mpint)
    uint8_t H[PC_SHA256_DIGEST_LEN];
    pc_sha256(pre, o, H);
    TEST_ASSERT_EQUAL_MEMORY(H, s->session_id, PC_SHA256_DIGEST_LEN);

    // sigblob = string("ssh-ed25519") || string(sig64); it verifies over the reconstructed H.
    size_t so = 0;
    const uint8_t *st, *sig;
    uint32_t st_len, sl;
    TEST_ASSERT_TRUE(rd_string(sigblob, sb_len, &so, &st, &st_len));
    TEST_ASSERT_TRUE(rd_string(sigblob, sb_len, &so, &sig, &sl));
    TEST_ASSERT_EQUAL_UINT32(64, sl);
    TEST_ASSERT_TRUE(pc_ed25519_verify(hostpub, H, PC_SHA256_DIGEST_LEN, sig));

    // The string-K KDF must yield the same c2s cipher key the server installed.
    uint8_t expect_c2s[PC_CHACHAPOLY_KEY_LEN];
    kdf_ref_string(K, H, s->session_id, 'C', expect_c2s, sizeof(expect_c2s));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect_c2s, ssh_keys[0].chacha_key_c2s, PC_CHACHAPOLY_KEY_LEN);
}

// ---------------------------------------------------------------------------
// sntrup761x25519-sha512, the classical fallback, and the hybrid rejection paths
// ---------------------------------------------------------------------------

// Largest C_INIT across the two hybrids (ML-KEM's encapsulation key is the bigger of the two).
enum : size_t
{
    PQC_MAX_CINIT = (MLKEM768_EK_BYTES > PC_SNTRUP761_PK_BYTES ? MLKEM768_EK_BYTES : PC_SNTRUP761_PK_BYTES) + 32
};

static const uint8_t PQC_ED_SEED[32] = {0x9a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60, 0x71, 0x82, 0x93, 0xa4,
                                        0xb5, 0xc6, 0xd7, 0xe8, 0xf9, 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f,
                                        0x60, 0x71, 0x82, 0x93, 0xa4, 0xb5, 0xc6, 0xd7, 0xe8, 0xf9};

// A session ready to run @p alg as the server, with an ed25519 host key and a fresh ephemeral.
static void prepare_session(SshKexAlg alg)
{
    ssh_transport_init(0);
    pc_ssh_hostkey_ed25519_set(PQC_ED_SEED);
    ssh_sess[0].kex_alg = alg;
    ssh_sess[0].hostkey_alg = SshHostkeyAlg::SSH_HOSTKEY_ED25519;
    ssh_sess[0].cipher_alg = SSH_CIPHER_CHACHA20POLY1305;
    ssh_sess[0].v_c_len = 0;
    ssh_sess[0].i_c_len = 0;
    ssh_sess[0].i_s_len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));
}

// ssh_kex_generate produces the right ephemeral for every method a PQC build offers: the three
// X25519-based ones derive an X25519 public, the two others do not.
void test_kex_generate_per_method()
{
    const SshKexAlg algs[5] = {SshKexAlg::SSH_KEX_CURVE25519, SshKexAlg::SSH_KEX_MLKEM768_X25519,
                               SshKexAlg::SSH_KEX_SNTRUP761_X25519, SshKexAlg::SSH_KEX_ECDH_NISTP256,
                               SshKexAlg::SSH_KEX_DH_GROUP14};
    for (int a = 0; a < 5; a++)
    {
        ssh_transport_init(0);
        ssh_sess[0].kex_alg = algs[a];
        TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));
        if (a < 3) // curve25519 and both hybrids share the X25519 ephemeral
        {
            uint8_t acc = 0;
            for (int b = 0; b < 32; b++)
            {
                acc |= ssh_sess[0].ecdh_pk[b];
            }
            TEST_ASSERT_NOT_EQUAL(0, acc);
        }
    }
    TEST_ASSERT_EQUAL_INT(-1, ssh_kex_generate(MAX_SSH_CONNS));
}

// The advertised kex_algorithms list leads with the post-quantum hybrids (OpenSSH 9.9+ order), so a
// PQC-capable peer negotiates one over classical X25519 without being forced to.
void test_kexinit_advertises_both_hybrids_first()
{
    ssh_transport_init(0);
    pc_ssh_hostkey_ed25519_set(PQC_ED_SEED);
    static uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_build(0, buf, &n, sizeof(buf)));
    size_t off = 1 + 16; // skip the message byte and the cookie
    const uint8_t *list;
    uint32_t ll;
    TEST_ASSERT_TRUE(rd_string(buf, n, &off, &list, &ll));
    char kex[256];
    TEST_ASSERT_TRUE(ll < sizeof(kex));
    memcpy(kex, list, ll);
    kex[ll] = '\0';
    TEST_ASSERT_EQUAL_INT(0, strncmp(kex, "mlkem768x25519-sha256,", 22));
    TEST_ASSERT_NOT_NULL(strstr(kex, "sntrup761x25519-sha512@openssh.com"));
    TEST_ASSERT_NOT_NULL(strstr(kex, "curve25519-sha256"));
}

// sntrup761x25519-sha512@openssh.com end to end, checked the way a conforming client would: the
// server's ciphertext is decapsulated with the client's real secret key, the X25519 half redone,
// K = SHA512(K_PQ || K_CL), and the ed25519 host signature verified over the rebuilt SHA-512
// exchange hash (C_INIT / S_REPLY / K all string-encoded).
void test_sntrup761_hybrid_kex_end_to_end()
{
    prepare_session(SshKexAlg::SSH_KEX_SNTRUP761_X25519);
    SshSession *s = &ssh_sess[0];
    const char *vc = "SSH-2.0-NtruClient";
    strcpy(s->v_c, vc);
    s->v_c_len = (uint16_t)strlen(vc);
    for (int j = 0; j < 30; j++)
    {
        s->i_c[j] = (uint8_t)(j + 1);
        s->i_s[j] = (uint8_t)(j + 100);
    }
    s->i_c_len = s->i_s_len = 30;

    static uint8_t pk[PC_SNTRUP761_PK_BYTES];
    static uint8_t sk[PC_SNTRUP761_SK_BYTES];
    pc_sntrup761_keypair(pk, sk);
    uint8_t client_sk[32];
    uint8_t qc[32];
    for (int j = 0; j < 32; j++)
    {
        client_sk[j] = (uint8_t)(0x70 + j);
    }
    pc_x25519_base(qc, client_sk);

    static uint8_t c_init[PC_SNTRUP761_PK_BYTES + 32];
    memcpy(c_init, pk, PC_SNTRUP761_PK_BYTES);
    memcpy(c_init + PC_SNTRUP761_PK_BYTES, qc, 32);
    static uint8_t pkt[1 + 4 + PC_SNTRUP761_PK_BYTES + 32];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    size_t plen = 1 + put_string(pkt + 1, c_init, sizeof(c_init));

    static uint8_t reply[2048];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexdh_handle(0, pkt, plen, reply, &rlen, sizeof(reply)));
    TEST_ASSERT_EQUAL(SSH_MSG_KEXDH_REPLY, reply[0]);
    TEST_ASSERT_EQUAL_UINT8(PC_SHA512_DIGEST_LEN, s->session_id_len); // SHA-512 exchange hash
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_NEWKEYS, s->phase);

    size_t off = 1;
    const uint8_t *ks;
    const uint8_t *s_reply;
    const uint8_t *sigblob;
    uint32_t ks_len, sr_len, sb_len;
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &ks, &ks_len));
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &s_reply, &sr_len));
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &sigblob, &sb_len));
    TEST_ASSERT_EQUAL_UINT32(PC_SNTRUP761_CT_BYTES + 32, sr_len); // ciphertext(1039) || Q_S(32)

    uint8_t k_pq[PC_SNTRUP761_SS_BYTES];
    pc_sntrup761_dec(sk, s_reply, k_pq);
    uint8_t k_cl[32];
    pc_x25519(k_cl, client_sk, s_reply + PC_SNTRUP761_CT_BYTES);
    uint8_t kin[PC_SNTRUP761_SS_BYTES + 32];
    memcpy(kin, k_pq, PC_SNTRUP761_SS_BYTES);
    memcpy(kin + PC_SNTRUP761_SS_BYTES, k_cl, 32);
    uint8_t K[PC_SHA512_DIGEST_LEN];
    pc_sha512(kin, sizeof(kin), K);

    size_t ko = 0;
    const uint8_t *kt;
    const uint8_t *hostpub;
    uint32_t kt_len, hp_len;
    TEST_ASSERT_TRUE(rd_string(ks, ks_len, &ko, &kt, &kt_len));
    TEST_ASSERT_EQUAL_MEMORY("ssh-ed25519", kt, 11);
    TEST_ASSERT_TRUE(rd_string(ks, ks_len, &ko, &hostpub, &hp_len));
    TEST_ASSERT_EQUAL_UINT32(32, hp_len);

    static uint8_t pre[4096];
    size_t o = 0;
    o += put_string(pre + o, (const uint8_t *)vc, (uint32_t)strlen(vc));
    o += put_string(pre + o, (const uint8_t *)SSH_SERVER_VERSION, (uint32_t)strlen(SSH_SERVER_VERSION));
    o += put_string(pre + o, s->i_c, s->i_c_len);
    o += put_string(pre + o, s->i_s, s->i_s_len);
    o += put_string(pre + o, ks, ks_len);
    o += put_string(pre + o, c_init, sizeof(c_init));
    o += put_string(pre + o, s_reply, sr_len);
    o += put_string(pre + o, K, PC_SHA512_DIGEST_LEN); // K is a string, not an mpint
    uint8_t H[PC_SHA512_DIGEST_LEN];
    pc_sha512(pre, o, H);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(H, s->session_id, PC_SHA512_DIGEST_LEN);

    size_t so = 0;
    const uint8_t *st;
    const uint8_t *sig;
    uint32_t st_len, sl;
    TEST_ASSERT_TRUE(rd_string(sigblob, sb_len, &so, &st, &st_len));
    TEST_ASSERT_TRUE(rd_string(sigblob, sb_len, &so, &sig, &sl));
    TEST_ASSERT_EQUAL_UINT32(64, sl);
    TEST_ASSERT_TRUE(pc_ed25519_verify(hostpub, H, PC_SHA512_DIGEST_LEN, sig));
}

// A classical finite-field KEX still works in a PQC-enabled build: neither hybrid branch is taken
// and K is hashed as an mpint into a SHA-256 exchange hash, not as the hybrid's fixed-length string.
void test_classical_dh_kex_in_pqc_build()
{
    prepare_session(SshKexAlg::SSH_KEX_DH_GROUP14);
    SshSession *s = &ssh_sess[0];
    const char *vc = "SSH-2.0-ClassicClient";
    strcpy(s->v_c, vc);
    s->v_c_len = (uint16_t)strlen(vc);

    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be));
    e_be[255] = 0x02; // a valid, small e
    static uint8_t pkt[300];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    size_t n = 1 + put_mpint(pkt + 1, e_be, 256);

    static uint8_t reply[2048];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexdh_handle(0, pkt, n, reply, &rlen, sizeof(reply)));
    TEST_ASSERT_EQUAL(SSH_MSG_KEXDH_REPLY, reply[0]);
    TEST_ASSERT_EQUAL_UINT8(PC_SHA256_DIGEST_LEN, s->session_id_len); // not the SHA-512 hybrid hash
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_NEWKEYS, s->phase);
    TEST_ASSERT_TRUE(ssh_keys[0].active);
}

// Both hybrids reject a malformed KEX_HYBRID_INIT: a payload too short for the length header, the
// wrong message number, a C_INIT of the wrong size, and one whose bytes never all arrive.
void test_hybrid_init_malformed_rejected()
{
    static uint8_t buf[8 + PQC_MAX_CINIT];
    static uint8_t reply[2048];
    size_t rlen = 0;
    const SshKexAlg algs[2] = {SshKexAlg::SSH_KEX_MLKEM768_X25519, SshKexAlg::SSH_KEX_SNTRUP761_X25519};
    const uint32_t want[2] = {MLKEM768_EK_BYTES + 32, PC_SNTRUP761_PK_BYTES + 32};

    for (int a = 0; a < 2; a++)
    {
        uint8_t tooshort[4] = {SSH_MSG_KEXDH_INIT, 0, 0, 0};
        prepare_session(algs[a]);
        TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, tooshort, sizeof(tooshort), reply, &rlen, sizeof(reply)));

        memset(buf, 0, sizeof(buf));
        buf[0] = SSH_MSG_KEXDH_REPLY; // right size, wrong message number
        put_u32(buf + 1, want[a]);
        prepare_session(algs[a]);
        TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, buf, 5 + want[a], reply, &rlen, sizeof(reply)));

        buf[0] = SSH_MSG_KEXDH_INIT;
        put_u32(buf + 1, want[a] - 1); // C_INIT of the wrong length
        prepare_session(algs[a]);
        TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, buf, 5 + want[a], reply, &rlen, sizeof(reply)));

        put_u32(buf + 1, want[a]); // right length, but one byte short on the wire
        prepare_session(algs[a]);
        TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, buf, 5 + want[a] - 1, reply, &rlen, sizeof(reply)));
    }
}

// Both hybrids reject a client X25519 public that yields an all-zero shared secret (a low-order
// point, RFC 7748 §6.1), and ML-KEM additionally rejects an encapsulation key whose coefficients
// are out of range (the FIPS 203 modulus check).
void test_hybrid_rejects_low_order_point_and_bad_ek()
{
    static uint8_t c_init[PQC_MAX_CINIT];
    static uint8_t pkt[8 + PQC_MAX_CINIT];
    static uint8_t reply[2048];
    size_t rlen = 0;
    uint8_t client_sk[32];
    uint8_t qc[32];
    for (int j = 0; j < 32; j++)
    {
        client_sk[j] = (uint8_t)(0x50 + j);
    }
    pc_x25519_base(qc, client_sk);

    // ML-KEM: a genuine encapsulation key, but Q_C is the all-zero point.
    prepare_session(SshKexAlg::SSH_KEX_MLKEM768_X25519);
    memcpy(c_init, kat_ek, MLKEM768_EK_BYTES);
    memset(c_init + MLKEM768_EK_BYTES, 0, 32);
    pkt[0] = SSH_MSG_KEXDH_INIT;
    size_t plen = 1 + put_string(pkt + 1, c_init, MLKEM768_EK_BYTES + 32);
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, pkt, plen, reply, &rlen, sizeof(reply)));

    // ML-KEM: a well-formed Q_C, but an encapsulation key that fails the modulus check.
    prepare_session(SshKexAlg::SSH_KEX_MLKEM768_X25519);
    memset(c_init, 0xFF, MLKEM768_EK_BYTES);
    memcpy(c_init + MLKEM768_EK_BYTES, qc, 32);
    plen = 1 + put_string(pkt + 1, c_init, MLKEM768_EK_BYTES + 32);
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, pkt, plen, reply, &rlen, sizeof(reply)));

    // sntrup761: a real public key, but Q_C is the all-zero point.
    static uint8_t pk[PC_SNTRUP761_PK_BYTES];
    static uint8_t sk[PC_SNTRUP761_SK_BYTES];
    pc_sntrup761_keypair(pk, sk);
    prepare_session(SshKexAlg::SSH_KEX_SNTRUP761_X25519);
    memcpy(c_init, pk, PC_SNTRUP761_PK_BYTES);
    memset(c_init + PC_SNTRUP761_PK_BYTES, 0, 32);
    plen = 1 + put_string(pkt + 1, c_init, PC_SNTRUP761_PK_BYTES + 32);
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, pkt, plen, reply, &rlen, sizeof(reply)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_decaps_ref_matches_kat);
    RUN_TEST(test_hybrid_negotiated);
    RUN_TEST(test_hybrid_absent_falls_back);
    RUN_TEST(test_hybrid_kex_end_to_end);
    RUN_TEST(test_kex_generate_per_method);
    RUN_TEST(test_kexinit_advertises_both_hybrids_first);
    RUN_TEST(test_sntrup761_hybrid_kex_end_to_end);
    RUN_TEST(test_classical_dh_kex_in_pqc_build);
    RUN_TEST(test_hybrid_init_malformed_rejected);
    RUN_TEST(test_hybrid_rejects_low_order_point_and_bad_ek);
    return UNITY_END();
}
