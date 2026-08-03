// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SSH transport handshake tests (RFC 4253): identification-string exchange and
// KEXINIT algorithm negotiation.

#include "baseline_keys.h"
#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ecdsa.h"
#include "crypto/asymmetric/ed25519.h"
#include "crypto/hash/sha256.h"
#include "cyclone_kex_bytes.h"
#include "network_drivers/presentation/ssh/transport/ssh_dh.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include "network_drivers/tls/ssh_rsa.h"
#include "throwaway_key.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

// The keyed api needs a context, not a key. These are one-shot vectors, so one scratch context is
// enough - rebuilt per call, and released first so a backend that attaches vendor resources to a
// context (ESP's mbedtls does) does not leak one per vector.
static uint8_t g_gcm_ws[PC_WORK_AESGCM] __attribute__((aligned(8)));
static bool g_gcm_live = false;
static pc_aesgcm_key *gcm_key(const uint8_t *key)
{
    if (g_gcm_live)
    {
        pc_aesgcm_key_wipe(reinterpret_cast<pc_aesgcm_key *>(g_gcm_ws));
    }
    g_gcm_live = true;
    return pc_aesgcm_key_init(g_gcm_ws, key);
}

// Native RSA test fixture (defined in ssh_rsa.cpp native path).
extern uint8_t _test_rsa_n[256];
extern uint8_t _test_rsa_d[256];
extern uint8_t _test_rsa_e[4];

static void setup_rsa_fixture();

void setUp()
{
    ssh_transport_init(0);
    ssh_kex_set_prefer_rsa(true); // deterministic default for negotiation tests
    setup_rsa_fixture();          // a host key must be available for host-key negotiation
}
void tearDown()
{
}

// ---- name-list / KEXINIT builder for crafting client messages -------------

static size_t put_namelist(uint8_t *p, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
    memcpy(p + 4, s, n);
    return 4 + n;
}

// Build a client KEXINIT with the given algorithm lists.
static size_t build_client_kexinit(uint8_t *out, const char *kex, const char *hostkey, const char *cipher,
                                   const char *mac, const char *comp)
{
    size_t o = 0;
    out[o++] = SSH_MSG_KEXINIT;
    for (int j = 0; j < 16; j++)
    {
        out[o++] = (uint8_t)j; // cookie
    }
    o += put_namelist(out + o, kex);
    o += put_namelist(out + o, hostkey);
    o += put_namelist(out + o, cipher); // enc c2s
    o += put_namelist(out + o, cipher); // enc s2c
    o += put_namelist(out + o, mac);    // mac c2s
    o += put_namelist(out + o, mac);    // mac s2c
    o += put_namelist(out + o, comp);   // comp c2s
    o += put_namelist(out + o, comp);   // comp s2c
    o += put_namelist(out + o, "");     // lang c2s
    o += put_namelist(out + o, "");     // lang s2c
    out[o++] = 0;                       // first_kex_packet_follows
    for (int j = 0; j < 4; j++)
    {
        out[o++] = 0; // reserved
    }
    return o;
}

// Build a KEXINIT (msg + cookie) with only the first @p nlists name-lists present, so parsing fails
// on the read of the next one. The lists that ARE present carry algorithms that negotiate cleanly.
static size_t build_partial_kexinit(uint8_t *out, int nlists)
{
    static const char *L[] = {
        "curve25519-sha256,diffie-hellman-group14-sha256", // kex
        "rsa-sha2-256,ssh-ed25519",                        // host key
        "aes256-ctr",                                      // cipher c2s
        "aes256-ctr",                                      // cipher s2c
        "hmac-sha2-256",                                   // mac c2s
        "hmac-sha2-256",                                   // mac s2c
        "none",                                            // comp c2s
    };
    size_t o = 0;
    out[o++] = SSH_MSG_KEXINIT;
    for (int j = 0; j < 16; j++)
    {
        out[o++] = (uint8_t)j; // cookie
    }
    for (int i = 0; i < nlists && i < (int)(sizeof(L) / sizeof(L[0])); i++)
    {
        o += put_namelist(out + o, L[i]);
    }
    return o;
}

// ---- banner ---------------------------------------------------------------

void test_server_banner_format()
{
    uint8_t buf[64];
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_transport_server_banner(buf, &n, sizeof(buf)));
    TEST_ASSERT_TRUE(n > 8);
    TEST_ASSERT_EQUAL_MEMORY("SSH-2.0-", buf, 8);
    TEST_ASSERT_EQUAL('\r', buf[n - 2]);
    TEST_ASSERT_EQUAL('\n', buf[n - 1]);
}

void test_recv_banner_complete()
{
    const char *banner = "SSH-2.0-OpenSSH_9.6\r\n";
    size_t consumed = 0;
    int rc = ssh_transport_recv_banner(0, (const uint8_t *)banner, strlen(banner), &consumed);
    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_size_t(strlen(banner), consumed);
    TEST_ASSERT_EQUAL_STRING("SSH-2.0-OpenSSH_9.6", ssh_sess[0].v_c);
    TEST_ASSERT_EQUAL(SSH_PHASE_KEXINIT, ssh_sess[0].phase);
}

void test_recv_banner_bare_lf()
{
    const char *banner = "SSH-2.0-x\n"; // some clients omit CR
    size_t consumed = 0;
    TEST_ASSERT_EQUAL_INT(1, ssh_transport_recv_banner(0, (const uint8_t *)banner, strlen(banner), &consumed));
    TEST_ASSERT_EQUAL_STRING("SSH-2.0-x", ssh_sess[0].v_c);
}

void test_recv_banner_split_across_reads()
{
    const char *p1 = "SSH-2.0-Cli";
    const char *p2 = "ent_1\r\n";
    size_t consumed = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_transport_recv_banner(0, (const uint8_t *)p1, strlen(p1), &consumed));
    TEST_ASSERT_EQUAL_INT(1, ssh_transport_recv_banner(0, (const uint8_t *)p2, strlen(p2), &consumed));
    TEST_ASSERT_EQUAL_STRING("SSH-2.0-Client_1", ssh_sess[0].v_c);
}

void test_recv_banner_skips_preamble_lines()
{
    // RFC 4253 §4.2 allows lines before the SSH identification string.
    const char *data = "hello from server\r\nnotice: terms\r\nSSH-2.0-Real\r\n";
    size_t consumed = 0;
    TEST_ASSERT_EQUAL_INT(1, ssh_transport_recv_banner(0, (const uint8_t *)data, strlen(data), &consumed));
    TEST_ASSERT_EQUAL_STRING("SSH-2.0-Real", ssh_sess[0].v_c);
}

// ---- KEXINIT --------------------------------------------------------------

void test_kexinit_build_starts_with_msg_and_stores_is()
{
    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_build(0, buf, &n, sizeof(buf)));
    TEST_ASSERT_EQUAL(SSH_MSG_KEXINIT, buf[0]);
    TEST_ASSERT_EQUAL_size_t(n, ssh_sess[0].i_s_len);
    TEST_ASSERT_EQUAL_MEMORY(buf, ssh_sess[0].i_s, n);
}

// Regression (found via ESP32-P4 OpenSSH interop): with all three host-key types loaded, the server
// KEXINIT's host-key name-list must carry all four algorithms. A too-small build buffer once truncated
// the last entry (rsa-sha2-256 -> "rs"), so a client that forced rsa-sha2-256 got "no matching host
// key type". The full list "ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-512,rsa-sha2-256" is 57 bytes.
void test_kexinit_hostkey_list_carries_all_four_when_all_keys_loaded()
{
    static const uint8_t EC_SCALAR[32] = {0x42, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                          0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                          0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    pc_ssh_hostkey_ed25519_set(BASELINE_ED25519_SEEDS[0]);
    pc_ssh_hostkey_ecdsa_set(EC_SCALAR);
    ssh_kex_set_prefer_rsa(false); // modern-first order (RSA entries last, so truncation hits them)
    // RSA is already available via setup_rsa_fixture() in setUp().

    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_build(0, buf, &n, sizeof(buf)));

    // The host-key name-list is the 2nd name-list, after msg(1) + cookie(16) + the kex name-list.
    size_t o = 1 + 16;
    uint32_t kex_len = ((uint32_t)buf[o] << 24) | (buf[o + 1] << 16) | (buf[o + 2] << 8) | buf[o + 3];
    o += 4 + kex_len;
    uint32_t hk_len = ((uint32_t)buf[o] << 24) | (buf[o + 1] << 16) | (buf[o + 2] << 8) | buf[o + 3];
    o += 4;
    char hk[128];
    TEST_ASSERT_TRUE(hk_len < sizeof(hk));
    memcpy(hk, buf + o, hk_len);
    hk[hk_len] = '\0';
    TEST_ASSERT_NOT_NULL(strstr(hk, "ssh-ed25519"));
    TEST_ASSERT_NOT_NULL(strstr(hk, "ecdsa-sha2-nistp256"));
    TEST_ASSERT_NOT_NULL(strstr(hk, "rsa-sha2-512"));
    TEST_ASSERT_NOT_NULL(strstr(hk, "rsa-sha2-256")); // the entry the undersized buffer used to drop
    ssh_kex_set_prefer_rsa(true);                     // restore the setUp default for later tests
}

void test_kexinit_parse_accepts_supported()
{
    ssh_sess[0].phase = SSH_PHASE_KEXINIT;
    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = build_client_kexinit(buf, "diffie-hellman-group14-sha256", "rsa-sha2-256", "aes256-ctr", "hmac-sha2-256",
                                    "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_PHASE_DH_INIT, ssh_sess[0].phase);
    TEST_ASSERT_EQUAL_size_t(n, ssh_sess[0].i_c_len);
}

void test_kexinit_parse_accepts_when_ours_listed_among_others()
{
    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n =
        build_client_kexinit(buf, "curve25519-sha256,diffie-hellman-group14-sha256", "ssh-ed25519,rsa-sha2-256",
                             "chacha20-poly1305@openssh.com,aes256-ctr", "hmac-sha2-512,hmac-sha2-256", "none,zlib");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
}

void test_kexinit_parse_rejects_missing_kex()
{
    // Only a KEX method we do not implement (nistp521) -> no mutual KEX -> reject. (nistp256 IS
    // implemented now, so it can no longer stand in for an unsupported method here.)
    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = build_client_kexinit(buf, "ecdh-sha2-nistp521", "rsa-sha2-256", "aes256-ctr", "hmac-sha2-256", "none");
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, n));
}

// Client offers only ssh-ed25519 host keys but we hold only an RSA key -> no mutual
// host-key algorithm -> reject (host-key negotiation is gated on keys we actually hold).
void test_kexinit_parse_rejects_hostkey_we_lack()
{
    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = build_client_kexinit(buf, "diffie-hellman-group14-sha256", "ssh-ed25519", "aes256-ctr", "hmac-sha2-256",
                                    "none");
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, n));
}

// With prefer-RSA off and an ed25519 host key installed, the server steers negotiation to
// curve25519-sha256 + ssh-ed25519 when the client offers both suites.
void test_kexinit_parse_steers_to_curve_ed25519()
{
    pc_ssh_hostkey_ed25519_set(BASELINE_ED25519_SEEDS[0]); // deterministic baseline host key
    ssh_kex_set_prefer_rsa(false);
    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = build_client_kexinit(buf, "curve25519-sha256,diffie-hellman-group14-sha256", "ssh-ed25519,rsa-sha2-256",
                                    "aes256-ctr", "hmac-sha2-256", "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_KEX_CURVE25519, ssh_sess[0].kex_alg);
    TEST_ASSERT_EQUAL(SSH_HOSTKEY_ED25519, ssh_sess[0].hostkey_alg);
    ssh_kex_set_prefer_rsa(true); // restore default for later tests
}

void test_kexinit_parse_rejects_missing_cipher()
{
    // Only ciphers we do not implement -> no mutual cipher -> reject.
    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = build_client_kexinit(buf, "diffie-hellman-group14-sha256", "rsa-sha2-256", "aes128-ctr,3des-cbc",
                                    "hmac-sha2-256", "none");
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, n));
}

// chacha20-poly1305@openssh.com is now a supported cipher: a client offering only it is accepted,
// and the AEAD cipher is selected (no separate MAC required).
void test_kexinit_parse_selects_chacha20poly1305()
{
    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = build_client_kexinit(buf, "diffie-hellman-group14-sha256", "rsa-sha2-256",
                                    "chacha20-poly1305@openssh.com", "hmac-sha2-256", "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_CIPHER_CHACHA20POLY1305, ssh_sess[0].cipher_alg);
}

// aes256-gcm@openssh.com is a supported AEAD cipher: a client offering only it is accepted and the
// GCM cipher is selected (no separate MAC required). Per RFC 4253 §7.1 the CLIENT's order decides, so a
// client that lists gcm first gets gcm.
void test_kexinit_parse_selects_aes256gcm()
{
    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = build_client_kexinit(buf, "diffie-hellman-group14-sha256", "rsa-sha2-256", "aes256-gcm@openssh.com",
                                    "hmac-sha2-256", "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_CIPHER_AES256GCM, ssh_sess[0].cipher_alg);

    // Client preference (RFC 4253 §7.1): the client's first offered cipher we support wins - gcm before ctr.
    n = build_client_kexinit(buf, "diffie-hellman-group14-sha256", "rsa-sha2-256", "aes256-gcm@openssh.com,aes256-ctr",
                             "hmac-sha2-256", "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_CIPHER_AES256GCM, ssh_sess[0].cipher_alg);
}

// RFC 4253 §7.1: negotiation follows the CLIENT's preference order, not ours. A client that lists
// aes256-ctr before chacha20-poly1305 gets aes256-ctr - even though our KEXINIT advertises chacha first.
// (This is exactly the mismatch a server-preference bug caused with CycloneSSH: our top pick differed
// from the client's, so the two sides disagreed on the algorithm.)
void test_kexinit_parse_honors_client_cipher_preference()
{
    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = build_client_kexinit(buf, "diffie-hellman-group14-sha256", "rsa-sha2-256",
                                    "aes256-ctr,chacha20-poly1305@openssh.com", "hmac-sha2-256", "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_CIPHER_AES256CTR, ssh_sess[0].cipher_alg);
}

// rsa-sha2-512 and rsa-sha2-256 are both backed by the one "ssh-rsa" host key (RFC 8332). The
// server prefers rsa-sha2-512; each is selected when offered alone (both gated on the RSA key).
void test_kexinit_parse_selects_rsa_sha512()
{
    uint8_t buf[SSH_KEXINIT_MAX];

    // Both offered -> rsa-sha2-512 wins (server preference).
    size_t n = build_client_kexinit(buf, "diffie-hellman-group14-sha256", "rsa-sha2-512,rsa-sha2-256", "aes256-ctr",
                                    "hmac-sha2-256", "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_HOSTKEY_RSA_SHA512, ssh_sess[0].hostkey_alg);

    // Only rsa-sha2-256 offered -> SHA-256 selected.
    n = build_client_kexinit(buf, "diffie-hellman-group14-sha256", "rsa-sha2-256", "aes256-ctr", "hmac-sha2-256",
                             "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_HOSTKEY_RSA_SHA256, ssh_sess[0].hostkey_alg);

    // Only rsa-sha2-512 offered -> accepted (same key), SHA-512 selected.
    n = build_client_kexinit(buf, "diffie-hellman-group14-sha256", "rsa-sha2-512", "aes256-ctr", "hmac-sha2-256",
                             "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_HOSTKEY_RSA_SHA512, ssh_sess[0].hostkey_alg);
}

// ecdsa-sha2-nistp256 (RFC 5656) is a distinct P-256 host key: once installed it is negotiable,
// and a client offering only it selects it.
void test_kexinit_parse_selects_ecdsa()
{
    uint8_t ec_priv[32];
    memset(ec_priv, 0, 32);
    ec_priv[31] = 0x42;
    pc_ssh_hostkey_ecdsa_set(ec_priv);
    TEST_ASSERT_TRUE(pc_ssh_hostkey_ecdsa_available());

    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = build_client_kexinit(buf, "diffie-hellman-group14-sha256", "ecdsa-sha2-nistp256", "aes256-ctr",
                                    "hmac-sha2-256", "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_HOSTKEY_ECDSA_NISTP256, ssh_sess[0].hostkey_alg);
}

// ecdh-sha2-nistp256 (RFC 5656 §4) is a negotiable P-256 key-exchange method: a client offering
// only it selects it (independent of the host-key type).
void test_kexinit_parse_selects_ecdh_nistp256()
{
    pc_ssh_hostkey_ed25519_set(BASELINE_ED25519_SEEDS[0]); // any host key so negotiation can complete
    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = build_client_kexinit(buf, "ecdh-sha2-nistp256", "ssh-ed25519", "aes256-ctr", "hmac-sha2-256", "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_KEX_ECDH_NISTP256, ssh_sess[0].kex_alg);
}

// With aes256-ctr, the encrypt-then-MAC variants are preferred: a client offering an -etm MAC gets
// it selected and its exact mode recorded.
void test_kexinit_parse_selects_etm_mac()
{
    uint8_t buf[SSH_KEXINIT_MAX];
    size_t n = build_client_kexinit(buf, "diffie-hellman-group14-sha256", "ssh-ed25519,rsa-sha2-256", "aes256-ctr",
                                    "hmac-sha2-512-etm@openssh.com,hmac-sha2-256", "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_CIPHER_AES256CTR, ssh_sess[0].cipher_alg);
    TEST_ASSERT_EQUAL(SSH_MAC_HMAC_SHA512_ETM, ssh_sess[0].mac_alg);
}

void test_kexinit_parse_rejects_truncated()
{
    uint8_t buf[8] = {SSH_MSG_KEXINIT, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, sizeof(buf)));
}

// ---- exchange hash (RFC 4253 §8) ------------------------------------------

// Independent SSH string / mpint encoders for cross-checking the production
// exchange-hash assembly.
static size_t put_string(uint8_t *p, const uint8_t *d, size_t n)
{
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
    memcpy(p + 4, d, n);
    return 4 + n;
}

static size_t put_mpint(uint8_t *p, const uint8_t *be, size_t len)
{
    size_t off = 0;
    while (off < len && be[off] == 0)
    {
        off++;
    }
    if (off == len)
    {
        return put_string(p, NULL, 0);
    }
    bool pad = (be[off] & 0x80u) != 0;
    size_t mlen = (len - off) + (pad ? 1 : 0);
    p[0] = (uint8_t)(mlen >> 24);
    p[1] = (uint8_t)(mlen >> 16);
    p[2] = (uint8_t)(mlen >> 8);
    p[3] = (uint8_t)mlen;
    size_t o = 4;
    if (pad)
    {
        p[o++] = 0x00;
    }
    memcpy(p + o, be + off, len - off);
    return o + (len - off);
}

void test_exchange_hash_matches_independent_assembly()
{
    // Populate the session fields the hash reads.
    SshSession *s = &ssh_sess[0];
    const char *vc = "SSH-2.0-TestClient";
    strcpy(s->v_c, vc);
    s->v_c_len = (uint16_t)strlen(vc);
    for (int j = 0; j < 40; j++)
    {
        s->i_c[j] = (uint8_t)(j + 1);
    }
    s->i_c_len = 40;
    for (int j = 0; j < 50; j++)
    {
        s->i_s[j] = (uint8_t)(100 + j);
    }
    s->i_s_len = 50;

    uint8_t e_be[256], f_be[256], k_be[256], ks[64];
    memset(e_be, 0, sizeof(e_be));
    memset(f_be, 0, sizeof(f_be));
    memset(k_be, 0, sizeof(k_be));
    e_be[0] = 0x80; // high bit set → mpint padding exercised
    e_be[255] = 0x11;
    f_be[1] = 0x01; // leading zero byte stripped
    f_be[255] = 0x22;
    k_be[255] = 0x05; // small value, many leading zeros
    for (int j = 0; j < 64; j++)
    {
        ks[j] = (uint8_t)(0xA0 + j);
    }

    uint8_t got[PC_SHA256_DIGEST_LEN];
    TEST_ASSERT_EQUAL_INT(0, ssh_kex_exchange_hash(0, e_be, f_be, k_be, ks, sizeof(ks), got));

    // Build the same pre-image independently and hash it.
    static uint8_t pre[2048];
    size_t o = 0;
    o += put_string(pre + o, (const uint8_t *)vc, strlen(vc));
    o += put_string(pre + o, (const uint8_t *)SSH_SERVER_VERSION, strlen(SSH_SERVER_VERSION));
    o += put_string(pre + o, s->i_c, s->i_c_len);
    o += put_string(pre + o, s->i_s, s->i_s_len);
    o += put_string(pre + o, ks, sizeof(ks));
    o += put_mpint(pre + o, e_be, 256);
    o += put_mpint(pre + o, f_be, 256);
    o += put_mpint(pre + o, k_be, 256);

    uint8_t expected[PC_SHA256_DIGEST_LEN];
    pc_sha256(pre, o, expected);

    TEST_ASSERT_EQUAL_MEMORY(expected, got, PC_SHA256_DIGEST_LEN);
}

void test_exchange_hash_changes_with_input()
{
    SshSession *s = &ssh_sess[0];
    strcpy(s->v_c, "SSH-2.0-A");
    s->v_c_len = 9;
    s->i_c_len = 0;
    s->i_s_len = 0;

    uint8_t e_be[256] = {0}, f_be[256] = {0}, k_be[256] = {0}, ks[4] = {1, 2, 3, 4};
    k_be[255] = 1;
    uint8_t h1[PC_SHA256_DIGEST_LEN], h2[PC_SHA256_DIGEST_LEN];
    ssh_kex_exchange_hash(0, e_be, f_be, k_be, ks, sizeof(ks), h1);
    k_be[255] = 2; // different shared secret
    ssh_kex_exchange_hash(0, e_be, f_be, k_be, ks, sizeof(ks), h2);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(h1, h2, PC_SHA256_DIGEST_LEN));
}

// ---- KEXDH (RFC 4253 §8) --------------------------------------------------

void test_kexdh_parse_init_extracts_e_with_padding()
{
    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be));
    e_be[0] = 0x80; // high bit set → mpint carries a 0x00 sign byte
    e_be[255] = 0xAB;

    uint8_t pkt[300];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    size_t n = 1 + put_mpint(pkt + 1, e_be, 256);

    uint8_t got[256];
    TEST_ASSERT_EQUAL_INT(0, ssh_kexdh_parse_init(pkt, n, got));
    TEST_ASSERT_EQUAL_MEMORY(e_be, got, 256);
}

void test_kexdh_parse_init_extracts_small_e()
{
    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be));
    e_be[255] = 0x05; // small value → many leading zeros stripped on the wire

    uint8_t pkt[300];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    size_t n = 1 + put_mpint(pkt + 1, e_be, 256);

    uint8_t got[256];
    TEST_ASSERT_EQUAL_INT(0, ssh_kexdh_parse_init(pkt, n, got));
    TEST_ASSERT_EQUAL_MEMORY(e_be, got, 256);
}

void test_kexdh_parse_init_rejects_wrong_type()
{
    uint8_t pkt[8] = {99, 0, 0, 0, 1, 0x05};
    uint8_t got[256];
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_parse_init(pkt, sizeof(pkt), got));
}

void test_kexdh_parse_init_rejects_oversized_e()
{
    // mpint with 300 magnitude bytes → exceeds 2048 bits.
    uint8_t pkt[320];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    pkt[1] = 0;
    pkt[2] = 0;
    pkt[3] = 0x01;
    pkt[4] = 0x2C; // length = 300
    for (int j = 0; j < 300; j++)
    {
        pkt[5 + j] = 0x01;
    }
    uint8_t got[256];
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_parse_init(pkt, 5 + 300, got));
}

void test_kexdh_build_reply_structure()
{
    uint8_t ks[12];
    for (int j = 0; j < 12; j++)
    {
        ks[j] = (uint8_t)(0x30 + j);
    }
    uint8_t f_be[256];
    memset(f_be, 0, sizeof(f_be));
    f_be[255] = 0x09;
    uint8_t sig[256];
    for (int j = 0; j < 256; j++)
    {
        sig[j] = (uint8_t)j;
    }

    uint8_t out[1024];
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexdh_build_reply(ks, sizeof(ks), f_be, sig, sizeof(sig), out, &n, sizeof(out)));

    TEST_ASSERT_EQUAL(SSH_MSG_KEXDH_REPLY, out[0]);
    uint32_t kslen = ((uint32_t)out[1] << 24) | ((uint32_t)out[2] << 16) | ((uint32_t)out[3] << 8) | out[4];
    TEST_ASSERT_EQUAL_UINT32(12, kslen);
    TEST_ASSERT_EQUAL_MEMORY(ks, out + 5, 12);

    bool alg = false;
    for (size_t k = 0; k + 12 <= n; k++)
    {
        if (memcmp(out + k, "rsa-sha2-256", 12) == 0)
        {
            alg = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(alg);

    bool found_sig = false;
    for (size_t k = 0; k + 256 <= n; k++)
    {
        if (memcmp(out + k, sig, 256) == 0)
        {
            found_sig = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found_sig);
}

// ---- KEXDH orchestration (compute K, sign H, reply, derive keys) ----------

static void setup_rsa_fixture()
{
    memset(_test_rsa_n, 0, 256);
    _test_rsa_n[0] = 0xFF; // 2048-bit modulus, larger than any PKCS#1 em
    _test_rsa_n[255] = 0x01;
    memset(_test_rsa_d, 0, 256);
    _test_rsa_d[255] = 0x01; // d = 1 (native test stub)
    _test_rsa_e[0] = 0x00;
    _test_rsa_e[1] = 0x01;
    _test_rsa_e[2] = 0x00;
    _test_rsa_e[3] = 0x01; // e = 65537
    pc_ssh_rsa_load_pubkey();
}

void test_kexdh_handle_produces_reply_and_installs_keys()
{
    ssh_transport_init(0);
    setup_rsa_fixture();

    SshSession *s = &ssh_sess[0];
    const char *vc = "SSH-2.0-TestClient";
    strcpy(s->v_c, vc);
    s->v_c_len = (uint16_t)strlen(vc);
    for (int j = 0; j < 30; j++)
    {
        s->i_c[j] = (uint8_t)(j + 1);
        s->i_s[j] = (uint8_t)(j + 100);
    }
    s->i_c_len = 30;
    s->i_s_len = 30;

    // Generate the server ephemeral (y, f). One 2048-bit exponentiation.
    TEST_ASSERT_EQUAL_INT(0, ssh_dh_generate(0));

    // Client KEXDH_INIT with e = 2 (a valid public value: 1 < 2 < p-1).
    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be));
    e_be[255] = 0x02;
    uint8_t pkt[300];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    size_t n = 1 + put_mpint(pkt + 1, e_be, 256);

    uint8_t reply[1024];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexdh_handle(0, pkt, n, reply, &rlen, sizeof(reply)));

    TEST_ASSERT_EQUAL(SSH_MSG_KEXDH_REPLY, reply[0]);
    TEST_ASSERT_TRUE(ssh_sess[0].have_session_id);
    TEST_ASSERT_EQUAL(SSH_PHASE_NEWKEYS, ssh_sess[0].phase);
    TEST_ASSERT_TRUE(ssh_keys[0].active);

    bool alg = false;
    for (size_t k = 0; k + 12 <= rlen; k++)
    {
        if (memcmp(reply + k, "rsa-sha2-256", 12) == 0)
        {
            alg = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(alg);

    // Receiving the peer's NEWKEYS activates the inbound direction and advances to the service phase.
    ssh_newkeys_complete(0);
    TEST_ASSERT_TRUE(ssh_pkt[0].enc_in);
    TEST_ASSERT_EQUAL(SSH_PHASE_SERVICE, ssh_sess[0].phase);
}

void test_kexdh_handle_rejects_invalid_e()
{
    ssh_transport_init(0);
    setup_rsa_fixture();
    ssh_sess[0].v_c_len = 0;
    ssh_sess[0].i_c_len = 0;
    ssh_sess[0].i_s_len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_dh_generate(0));

    // e = 1 is invalid (must be > 1).
    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be));
    e_be[255] = 0x01;
    uint8_t pkt[300];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    size_t n = 1 + put_mpint(pkt + 1, e_be, 256);

    uint8_t reply[1024];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, pkt, n, reply, &rlen, sizeof(reply)));
}

// ---- curve25519-sha256 + ssh-ed25519 KEX (RFC 8731 / RFC 8709) ------------

// Read an SSH string field at *off; point *d/*dl into buf and advance *off.
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

// A full server-side curve25519 + ed25519 key exchange, verified end-to-end the way a
// conforming client would: recompute the shared secret, rebuild the exchange hash, and
// verify the server's ed25519 signature over it. Nothing here is a shortcut - a wrong
// byte anywhere (K_S encoding, Q ordering, string-vs-mpint, K alignment) fails the check.
void test_kexdh_handle_curve25519_ed25519_end_to_end()
{
    // Fixed baseline host keys for deterministic regression, plus one fresh throwaway
    // for coverage - the full sign/verify path is where a key-specific bug would hide.
    uint8_t throwaway[32];
    throwaway_ed25519_seed(throwaway);
    const uint8_t *seeds[BASELINE_ED25519_COUNT + 1];
    for (size_t k = 0; k < BASELINE_ED25519_COUNT; k++)
    {
        seeds[k] = BASELINE_ED25519_SEEDS[k];
    }
    seeds[BASELINE_ED25519_COUNT] = throwaway;

    for (size_t ki = 0; ki <= BASELINE_ED25519_COUNT; ki++)
    {
        const uint8_t *seed = seeds[ki];
        ssh_transport_init(0);
        pc_ssh_hostkey_ed25519_set(seed);
        SshSession *s = &ssh_sess[0];
        s->kex_alg = SSH_KEX_CURVE25519;
        s->hostkey_alg = SSH_HOSTKEY_ED25519;

        const char *vc = "SSH-2.0-CurveClient";
        strcpy(s->v_c, vc);
        s->v_c_len = (uint16_t)strlen(vc);
        for (int j = 0; j < 30; j++)
        {
            s->i_c[j] = (uint8_t)(j + 1);
            s->i_s[j] = (uint8_t)(j + 100);
        }
        s->i_c_len = s->i_s_len = 30;

        // Server ephemeral, then a client ephemeral + ECDH_INIT: byte(30) || string(Q_C).
        TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));
        uint8_t client_sk[32], qc[32];
        for (int j = 0; j < 32; j++)
        {
            client_sk[j] = (uint8_t)(0x40 + j);
        }
        pc_x25519_base(qc, client_sk);
        uint8_t pkt[64];
        pkt[0] = SSH_MSG_KEXDH_INIT;
        put_string(pkt + 1, qc, 32);
        size_t plen = 1 + 4 + 32;

        uint8_t reply[512];
        size_t rlen = 0;
        TEST_ASSERT_EQUAL_INT(0, ssh_kexdh_handle(0, pkt, plen, reply, &rlen, sizeof(reply)));
        TEST_ASSERT_EQUAL(SSH_MSG_KEXDH_REPLY, reply[0]);
        TEST_ASSERT_TRUE(s->have_session_id);
        TEST_ASSERT_EQUAL(SSH_PHASE_NEWKEYS, s->phase);
        TEST_ASSERT_TRUE(ssh_keys[0].active);

        // Parse reply: string(K_S) || string(Q_S) || string(sigblob).
        size_t off = 1;
        const uint8_t *ks, *qs, *sigblob;
        uint32_t ks_len, qs_len, sig_len;
        TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &ks, &ks_len));
        TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &qs, &qs_len));
        TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &sigblob, &sig_len));
        TEST_ASSERT_EQUAL_UINT32(32, qs_len); // Q_S is a raw 32-byte string, not an mpint

        // K_S = string("ssh-ed25519") || string(pub32); recover host pub and check it.
        size_t ko = 0;
        const uint8_t *kt, *hostpub;
        uint32_t kt_len, hp_len;
        TEST_ASSERT_TRUE(rd_string(ks, ks_len, &ko, &kt, &kt_len));
        TEST_ASSERT_EQUAL_MEMORY("ssh-ed25519", kt, 11);
        TEST_ASSERT_TRUE(rd_string(ks, ks_len, &ko, &hostpub, &hp_len));
        TEST_ASSERT_EQUAL_UINT32(32, hp_len);
        uint8_t expect_pub[32];
        pc_ed25519_pubkey(expect_pub, seed);
        TEST_ASSERT_EQUAL_MEMORY(expect_pub, hostpub, 32);

        // sigblob = string("ssh-ed25519") || string(sig64).
        size_t so = 0;
        const uint8_t *st, *sig;
        uint32_t st_len, sl;
        TEST_ASSERT_TRUE(rd_string(sigblob, sig_len, &so, &st, &st_len));
        TEST_ASSERT_EQUAL_MEMORY("ssh-ed25519", st, 11);
        TEST_ASSERT_TRUE(rd_string(sigblob, sig_len, &so, &sig, &sl));
        TEST_ASSERT_EQUAL_UINT32(64, sl);

        // Client recomputes K = X25519(client_sk, Q_S) and rebuilds the exchange hash.
        uint8_t K[32];
        pc_x25519(K, client_sk, qs);
        static uint8_t pre[1024];
        size_t o = 0;
        o += put_string(pre + o, (const uint8_t *)vc, strlen(vc));
        o += put_string(pre + o, (const uint8_t *)SSH_SERVER_VERSION, strlen(SSH_SERVER_VERSION));
        o += put_string(pre + o, s->i_c, s->i_c_len);
        o += put_string(pre + o, s->i_s, s->i_s_len);
        o += put_string(pre + o, ks, ks_len);
        o += put_string(pre + o, qc, 32); // Q_C (string)
        o += put_string(pre + o, qs, 32); // Q_S (string)
        o += put_mpint(pre + o, K, 32);   // K (mpint)
        uint8_t H[PC_SHA256_DIGEST_LEN];
        pc_sha256(pre, o, H);
        TEST_ASSERT_EQUAL_MEMORY(H, s->session_id, PC_SHA256_DIGEST_LEN); // server captured this H

        // The signature verifies against the host key over the reconstructed H.
        TEST_ASSERT_TRUE(pc_ed25519_verify(hostpub, H, PC_SHA256_DIGEST_LEN, sig));
    }
}

// A low-order client point (all-zero X25519 output) is rejected (RFC 7748 §6.1).
void test_kexdh_handle_curve25519_rejects_low_order()
{
    static const uint8_t zero_seed[32] = {0};
    ssh_transport_init(0);
    pc_ssh_hostkey_ed25519_set(zero_seed);
    ssh_sess[0].kex_alg = SSH_KEX_CURVE25519;
    ssh_sess[0].hostkey_alg = SSH_HOSTKEY_ED25519;
    ssh_sess[0].v_c_len = ssh_sess[0].i_c_len = ssh_sess[0].i_s_len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));

    uint8_t qc[32] = {0}; // all-zero public → zero shared secret
    uint8_t pkt[64];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    put_string(pkt + 1, qc, 32);
    uint8_t reply[512];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, pkt, 1 + 4 + 32, reply, &rlen, sizeof(reply)));
}

// ecdh-sha2-nistp256 (RFC 5656 §4) key exchange end to end: a real P-256 client ephemeral, the
// server's KEXDH_REPLY parsed, the shared secret recomputed by the client, the exchange hash
// rebuilt (Q_C/Q_S as 65-byte strings, K as an mpint), and the host signature verified over it.
void test_kexdh_handle_ecdh_nistp256_end_to_end()
{
    uint8_t seed[32];
    throwaway_ed25519_seed(seed);
    ssh_transport_init(0);
    pc_ssh_hostkey_ed25519_set(seed);
    SshSession *s = &ssh_sess[0];
    s->kex_alg = SSH_KEX_ECDH_NISTP256;
    s->hostkey_alg = SSH_HOSTKEY_ED25519;

    const char *vc = "SSH-2.0-EcdhClient";
    strcpy(s->v_c, vc);
    s->v_c_len = (uint16_t)strlen(vc);
    for (int j = 0; j < 30; j++)
    {
        s->i_c[j] = (uint8_t)(j + 1);
        s->i_s[j] = (uint8_t)(j + 100);
    }
    s->i_c_len = s->i_s_len = 30;

    // Server ephemeral (P-256), then a client ephemeral Q_C = client_sk * G (65-byte point).
    TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));
    uint8_t client_sk[32];
    memset(client_sk, 0, 32);
    client_sk[31] = 0x09; // d = 9, a valid small P-256 scalar
    uint8_t qc[65];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(qc, client_sk));

    uint8_t pkt[128];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    put_string(pkt + 1, qc, 65);
    size_t plen = 1 + 4 + 65;

    uint8_t reply[512];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexdh_handle(0, pkt, plen, reply, &rlen, sizeof(reply)));
    TEST_ASSERT_EQUAL(SSH_MSG_KEXDH_REPLY, reply[0]);
    TEST_ASSERT_TRUE(s->have_session_id);
    TEST_ASSERT_EQUAL(SSH_PHASE_NEWKEYS, s->phase);
    TEST_ASSERT_TRUE(ssh_keys[0].active);

    // Parse reply: string(K_S) || string(Q_S) || string(sigblob).
    size_t off = 1;
    const uint8_t *ks, *qs, *sigblob;
    uint32_t ks_len, qs_len, sig_len;
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &ks, &ks_len));
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &qs, &qs_len));
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &sigblob, &sig_len));
    TEST_ASSERT_EQUAL_UINT32(65, qs_len); // Q_S is the 65-byte uncompressed point, not an mpint

    // K_S = string("ssh-ed25519") || string(pub32); recover and check the host public.
    size_t ko = 0;
    const uint8_t *kt, *hostpub;
    uint32_t kt_len, hp_len;
    TEST_ASSERT_TRUE(rd_string(ks, ks_len, &ko, &kt, &kt_len));
    TEST_ASSERT_EQUAL_MEMORY("ssh-ed25519", kt, 11);
    TEST_ASSERT_TRUE(rd_string(ks, ks_len, &ko, &hostpub, &hp_len));
    TEST_ASSERT_EQUAL_UINT32(32, hp_len);
    uint8_t expect_pub[32];
    pc_ed25519_pubkey(expect_pub, seed);
    TEST_ASSERT_EQUAL_MEMORY(expect_pub, hostpub, 32);

    // sigblob = string("ssh-ed25519") || string(sig64).
    size_t so = 0;
    const uint8_t *st, *sig;
    uint32_t st_len, sl;
    TEST_ASSERT_TRUE(rd_string(sigblob, sig_len, &so, &st, &st_len));
    TEST_ASSERT_EQUAL_MEMORY("ssh-ed25519", st, 11);
    TEST_ASSERT_TRUE(rd_string(sigblob, sig_len, &so, &sig, &sl));
    TEST_ASSERT_EQUAL_UINT32(64, sl);

    // Client recomputes K = X(client_sk * Q_S) and rebuilds the exchange hash exactly.
    uint8_t K[32];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_ecdh(K, qs, client_sk));
    static uint8_t pre[1024];
    size_t o = 0;
    o += put_string(pre + o, (const uint8_t *)vc, strlen(vc));
    o += put_string(pre + o, (const uint8_t *)SSH_SERVER_VERSION, strlen(SSH_SERVER_VERSION));
    o += put_string(pre + o, s->i_c, s->i_c_len);
    o += put_string(pre + o, s->i_s, s->i_s_len);
    o += put_string(pre + o, ks, ks_len);
    o += put_string(pre + o, qc, 65); // Q_C (string)
    o += put_string(pre + o, qs, 65); // Q_S (string)
    o += put_mpint(pre + o, K, 32);   // K (mpint)
    uint8_t H[PC_SHA256_DIGEST_LEN];
    pc_sha256(pre, o, H);
    TEST_ASSERT_EQUAL_MEMORY(H, s->session_id, PC_SHA256_DIGEST_LEN); // server captured this same H

    // The host signature verifies against the reconstructed H.
    TEST_ASSERT_TRUE(pc_ed25519_verify(hostpub, H, PC_SHA256_DIGEST_LEN, sig));
}

// An off-curve client point is rejected (RFC 5656 §4 point validation).
void test_kexdh_handle_ecdh_nistp256_rejects_bad_point()
{
    uint8_t seed[32];
    throwaway_ed25519_seed(seed);
    ssh_transport_init(0);
    pc_ssh_hostkey_ed25519_set(seed);
    ssh_sess[0].kex_alg = SSH_KEX_ECDH_NISTP256;
    ssh_sess[0].hostkey_alg = SSH_HOSTKEY_ED25519;
    ssh_sess[0].v_c_len = ssh_sess[0].i_c_len = ssh_sess[0].i_s_len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));

    uint8_t client_sk[32];
    memset(client_sk, 0, 32);
    client_sk[31] = 0x09;
    uint8_t qc[65];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(qc, client_sk));
    qc[1] ^= 0x01; // corrupt X -> off curve

    uint8_t pkt[128];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    put_string(pkt + 1, qc, 65);
    uint8_t reply[512];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, pkt, 1 + 4 + 65, reply, &rlen, sizeof(reply)));
}

// End-to-end DH-group14 + rsa-sha2-512: when SSH_HOSTKEY_RSA_SHA512 is negotiated, the reply
// signature blob must carry "rsa-sha2-512" (not -256) and be a genuine PKCS#1 v1.5 SHA-512 block
// over the exchange hash H. With the fixture's d = 1 the signature s = EM^1 mod n = EM, so we can
// rebuild the padded EM and byte-compare - proving the SHA-512 DigestInfo/padding reached the wire.
void test_kexdh_handle_rsa_sha512_signature()
{
    ssh_transport_init(0);
    setup_rsa_fixture();
    SshSession *s = &ssh_sess[0];
    s->hostkey_alg = SSH_HOSTKEY_RSA_SHA512; // as if negotiated
    const char *vc = "SSH-2.0-TestClient";
    strcpy(s->v_c, vc);
    s->v_c_len = (uint16_t)strlen(vc);
    for (int j = 0; j < 30; j++)
    {
        s->i_c[j] = (uint8_t)(j + 1);
        s->i_s[j] = (uint8_t)(j + 100);
    }
    s->i_c_len = s->i_s_len = 30;
    TEST_ASSERT_EQUAL_INT(0, ssh_dh_generate(0));

    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be));
    e_be[255] = 0x02; // valid client e
    uint8_t pkt[300];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    size_t n = 1 + put_mpint(pkt + 1, e_be, 256);

    uint8_t reply[1024];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexdh_handle(0, pkt, n, reply, &rlen, sizeof(reply)));

    // The negotiated signature name must be rsa-sha2-512, and rsa-sha2-256 absent.
    bool has512 = false;
    bool has256 = false;
    for (size_t k = 0; k + 12 <= rlen; k++)
    {
        if (memcmp(reply + k, "rsa-sha2-512", 12) == 0)
        {
            has512 = true;
        }
        if (memcmp(reply + k, "rsa-sha2-256", 12) == 0)
        {
            has256 = true;
        }
    }
    TEST_ASSERT_TRUE(has512);
    TEST_ASSERT_FALSE(has256);

    // Parse: byte(31) || string(K_S) || mpint(f) || string(sigblob{ string(name) string(sig) }).
    size_t off = 1;
    const uint8_t *ks;
    const uint8_t *f;
    const uint8_t *sigblob;
    uint32_t ks_len;
    uint32_t f_len;
    uint32_t sb_len;
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &ks, &ks_len));
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &f, &f_len));
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &sigblob, &sb_len));
    size_t so = 0;
    const uint8_t *name;
    const uint8_t *sig;
    uint32_t name_len;
    uint32_t sig_len;
    TEST_ASSERT_TRUE(rd_string(sigblob, sb_len, &so, &name, &name_len));
    TEST_ASSERT_EQUAL_UINT32(12, name_len);
    TEST_ASSERT_EQUAL_MEMORY("rsa-sha2-512", name, 12);
    TEST_ASSERT_TRUE(rd_string(sigblob, sb_len, &so, &sig, &sig_len));
    TEST_ASSERT_EQUAL_UINT32(256, sig_len);

    // Rebuild the expected PKCS#1 v1.5 SHA-512 block over H (= session_id) and byte-compare.
    uint8_t d512[PC_SHA512_DIGEST_LEN];
    pc_sha512(s->session_id, PC_SHA256_DIGEST_LEN, d512);
    uint8_t em[256];
    const size_t total = PC_PKCS1_SHA512_DIGESTINFO_LEN + PC_SHA512_DIGEST_LEN; // 83
    const size_t pad = 256 - 3 - total;                                         // 170
    em[0] = 0x00;
    em[1] = 0x01;
    memset(em + 2, 0xFF, pad);
    em[2 + pad] = 0x00;
    memcpy(em + 3 + pad, pc_pkcs1_sha512_digestinfo, PC_PKCS1_SHA512_DIGESTINFO_LEN);
    memcpy(em + 3 + pad + PC_PKCS1_SHA512_DIGESTINFO_LEN, d512, PC_SHA512_DIGEST_LEN);
    TEST_ASSERT_EQUAL_MEMORY(em, sig, 256);
}

// End-to-end curve25519 + ecdsa-sha2-nistp256 (RFC 5656): the reply K_S carries the P-256
// public point, and the signature blob (string(name) || string(mpint r || mpint s)) verifies
// against the host key over the reconstructed exchange hash H. Nothing here is a shortcut.
void test_kexdh_handle_ecdsa_end_to_end()
{
    ssh_transport_init(0);
    uint8_t ec_priv[32];
    memset(ec_priv, 0, 32);
    ec_priv[31] = 0x07;
    pc_ssh_hostkey_ecdsa_set(ec_priv);
    uint8_t ec_pub[PC_ECDSA_P256_PUB_LEN];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(ec_pub, ec_priv));

    SshSession *s = &ssh_sess[0];
    s->kex_alg = SSH_KEX_CURVE25519;
    s->hostkey_alg = SSH_HOSTKEY_ECDSA_NISTP256;
    const char *vc = "SSH-2.0-EcdsaClient";
    strcpy(s->v_c, vc);
    s->v_c_len = (uint16_t)strlen(vc);
    for (int j = 0; j < 30; j++)
    {
        s->i_c[j] = (uint8_t)(j + 1);
        s->i_s[j] = (uint8_t)(j + 100);
    }
    s->i_c_len = s->i_s_len = 30;

    TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));
    uint8_t client_sk[32];
    uint8_t qc[32];
    for (int j = 0; j < 32; j++)
    {
        client_sk[j] = (uint8_t)(0x40 + j);
    }
    pc_x25519_base(qc, client_sk);
    uint8_t pkt[64];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    put_string(pkt + 1, qc, 32);
    size_t plen = 1 + 4 + 32;

    uint8_t reply[512];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexdh_handle(0, pkt, plen, reply, &rlen, sizeof(reply)));
    TEST_ASSERT_EQUAL(SSH_MSG_KEXDH_REPLY, reply[0]);

    size_t off = 1;
    const uint8_t *ks;
    const uint8_t *qs;
    const uint8_t *sigblob;
    uint32_t ks_len;
    uint32_t qs_len;
    uint32_t sig_len;
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &ks, &ks_len));
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &qs, &qs_len));
    TEST_ASSERT_TRUE(rd_string(reply, rlen, &off, &sigblob, &sig_len));
    TEST_ASSERT_EQUAL_UINT32(32, qs_len);

    // K_S = string("ecdsa-sha2-nistp256") || string("nistp256") || string(Q = 0x04||X||Y).
    size_t ko = 0;
    const uint8_t *kt;
    const uint8_t *curve;
    const uint8_t *q;
    uint32_t kt_len;
    uint32_t curve_len;
    uint32_t q_len;
    TEST_ASSERT_TRUE(rd_string(ks, ks_len, &ko, &kt, &kt_len));
    TEST_ASSERT_EQUAL_UINT32(19, kt_len);
    TEST_ASSERT_EQUAL_MEMORY("ecdsa-sha2-nistp256", kt, 19);
    TEST_ASSERT_TRUE(rd_string(ks, ks_len, &ko, &curve, &curve_len));
    TEST_ASSERT_EQUAL_MEMORY("nistp256", curve, 8);
    TEST_ASSERT_TRUE(rd_string(ks, ks_len, &ko, &q, &q_len));
    TEST_ASSERT_EQUAL_UINT32(PC_ECDSA_P256_PUB_LEN, q_len);
    TEST_ASSERT_EQUAL_MEMORY(ec_pub, q, PC_ECDSA_P256_PUB_LEN);

    // sigblob = string("ecdsa-sha2-nistp256") || string( mpint(r) || mpint(s) ).
    size_t so = 0;
    const uint8_t *st;
    const uint8_t *ecsig;
    uint32_t st_len;
    uint32_t ecsig_len;
    TEST_ASSERT_TRUE(rd_string(sigblob, sig_len, &so, &st, &st_len));
    TEST_ASSERT_EQUAL_MEMORY("ecdsa-sha2-nistp256", st, 19);
    TEST_ASSERT_TRUE(rd_string(sigblob, sig_len, &so, &ecsig, &ecsig_len));

    // Decode mpint(r) || mpint(s) into a raw 64-byte r || s.
    uint8_t raw[64];
    memset(raw, 0, sizeof(raw));
    size_t eo = 0;
    const uint8_t *rp;
    const uint8_t *sp;
    uint32_t rl;
    uint32_t sl;
    TEST_ASSERT_TRUE(rd_string(ecsig, ecsig_len, &eo, &rp, &rl));
    TEST_ASSERT_TRUE(rd_string(ecsig, ecsig_len, &eo, &sp, &sl));
    while (rl > 0 && rp[0] == 0) // strip mpint sign byte / leading zeros
    {
        rp++;
        rl--;
    }
    while (sl > 0 && sp[0] == 0)
    {
        sp++;
        sl--;
    }
    TEST_ASSERT_TRUE(rl <= 32 && sl <= 32);
    memcpy(raw + (32 - rl), rp, rl);
    memcpy(raw + 32 + (32 - sl), sp, sl);

    // Reconstruct H the way a conforming client would and verify the P-256 signature.
    uint8_t K[32];
    pc_x25519(K, client_sk, qs);
    static uint8_t pre[1024];
    size_t o = 0;
    o += put_string(pre + o, (const uint8_t *)vc, strlen(vc));
    o += put_string(pre + o, (const uint8_t *)SSH_SERVER_VERSION, strlen(SSH_SERVER_VERSION));
    o += put_string(pre + o, s->i_c, s->i_c_len);
    o += put_string(pre + o, s->i_s, s->i_s_len);
    o += put_string(pre + o, ks, ks_len);
    o += put_string(pre + o, qc, 32);
    o += put_string(pre + o, qs, 32);
    o += put_mpint(pre + o, K, 32);
    uint8_t H[PC_SHA256_DIGEST_LEN];
    pc_sha256(pre, o, H);
    TEST_ASSERT_EQUAL_MEMORY(H, s->session_id, PC_SHA256_DIGEST_LEN);
    TEST_ASSERT_TRUE(pc_ecdsa_p256_verify(ec_pub, H, PC_SHA256_DIGEST_LEN, raw));
}

// ---- rekey (RFC 4253 §9) --------------------------------------------------

void test_derive_keys_session_id_affects_output()
{
    uint8_t K[256];
    memset(K, 0, sizeof(K));
    K[255] = 7;
    uint8_t H[32], sid[32];
    for (int j = 0; j < 32; j++)
    {
        H[j] = (uint8_t)j;
        sid[j] = (uint8_t)(0xF0 - j); // distinct from H
    }

    ssh_dh_derive_keys_sid(0, K, H, H, SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256);
    uint8_t a[32];
    memcpy(a, ssh_keys[0].mac_key_c2s, 32);

    ssh_dh_derive_keys_sid(0, K, H, sid, SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, ssh_keys[0].mac_key_c2s, 32));

    // Deterministic: same inputs reproduce the same key.
    ssh_dh_derive_keys_sid(0, K, H, H, SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256);
    TEST_ASSERT_EQUAL_MEMORY(a, ssh_keys[0].mac_key_c2s, 32);
}

void test_rekey_needed_threshold()
{
    ssh_transport_init(0);
    ssh_pkt[0].seq_no_send = 0;
    ssh_pkt[0].seq_no_recv = 0;
    TEST_ASSERT_FALSE(ssh_rekey_needed(0));
    ssh_pkt[0].seq_no_send = SSH_REKEY_PACKET_THRESHOLD;
    TEST_ASSERT_TRUE(ssh_rekey_needed(0));
}

void test_rekey_due_volume_and_time()
{
    const uint32_t PKT = 1000000, TIME = 3600000;
    // Neither budget spent.
    TEST_ASSERT_FALSE(ssh_rekey_due(500, 500, 60000, PKT, TIME));
    // Outbound packet budget spent.
    TEST_ASSERT_TRUE(ssh_rekey_due(PKT, 0, 0, PKT, TIME));
    // Inbound packet budget spent.
    TEST_ASSERT_TRUE(ssh_rekey_due(0, PKT, 0, PKT, TIME));
    // Time budget spent (at the boundary).
    TEST_ASSERT_TRUE(ssh_rekey_due(1, 1, TIME, PKT, TIME));
    TEST_ASSERT_FALSE(ssh_rekey_due(1, 1, TIME - 1, PKT, TIME));
    // Time trigger disabled (0): only the packet budget matters.
    TEST_ASSERT_FALSE(ssh_rekey_due(1, 1, 0xFFFFFFFFu, PKT, 0));
}

void test_begin_rekey_preserves_session_and_auth()
{
    ssh_transport_init(0);
    setup_rsa_fixture();
    ssh_sess[0].have_session_id = true;
    for (int j = 0; j < 32; j++)
    {
        ssh_sess[0].session_id[j] = (uint8_t)j;
    }
    ssh_sess[0].authed = true;
    TEST_ASSERT_EQUAL_INT(0, ssh_dh_generate(0));

    uint8_t out[1024];
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_transport_begin_rekey(0, out, &n, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_KEXINIT, out[0]);
    TEST_ASSERT_EQUAL(SSH_PHASE_KEXINIT, ssh_sess[0].phase);
    TEST_ASSERT_TRUE(ssh_sess[0].have_session_id);
    TEST_ASSERT_TRUE(ssh_sess[0].authed);

    // After the re-key completes, an authenticated connection resumes OPEN.
    ssh_newkeys_complete(0);
    TEST_ASSERT_EQUAL(SSH_PHASE_OPEN, ssh_sess[0].phase);
}

// Build a KEXINIT with eight independently-chosen algorithm name-lists.
static size_t build_kexinit8(uint8_t *out, const char *const n[8])
{
    size_t o = 0;
    out[o++] = SSH_MSG_KEXINIT;
    for (int j = 0; j < 16; j++)
    {
        out[o++] = (uint8_t)j; // cookie
    }
    for (int j = 0; j < 8; j++)
    {
        o += put_namelist(out + o, n[j]);
    }
    o += put_namelist(out + o, ""); // lang c2s
    o += put_namelist(out + o, ""); // lang s2c
    out[o++] = 0;                   // first_kex_packet_follows
    for (int j = 0; j < 4; j++)
    {
        out[o++] = 0; // reserved
    }
    return o;
}

// Every transport entry point rejects an out-of-range slot.
void test_transport_index_guards()
{
    uint8_t out[512];
    size_t olen = 0, consumed = 0;
    ssh_transport_init(MAX_SSH_CONNS); // no-op, no crash
    TEST_ASSERT_EQUAL_INT(-1, ssh_transport_recv_banner(MAX_SSH_CONNS, (const uint8_t *)"x", 1, &consumed));
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_build(MAX_SSH_CONNS, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(MAX_SSH_CONNS, out, 20));
    uint8_t h[PC_SHA256_DIGEST_LEN];
    TEST_ASSERT_EQUAL_INT(-1, ssh_kex_exchange_hash(MAX_SSH_CONNS, NULL, NULL, NULL, NULL, 0, h));
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(MAX_SSH_CONNS, out, 10, out, &olen, sizeof(out)));
    ssh_newkeys_complete(MAX_SSH_CONNS); // no-op
    TEST_ASSERT_FALSE(ssh_rekey_needed(MAX_SSH_CONNS));
    TEST_ASSERT_EQUAL_INT(-1, ssh_transport_begin_rekey(MAX_SSH_CONNS, out, &olen, sizeof(out)));
}

// Banner and builder cap checks, and a banner line that exceeds the maximum.
void test_banner_and_build_caps()
{
    uint8_t small[4];
    size_t l = 0, consumed = 0;
    TEST_ASSERT_EQUAL_INT(-1, ssh_transport_server_banner(small, &l, 4)); // banner does not fit

    // An "SSH-" line of exactly SSH_VERSION_MAX chars, then LF: too long.
    static uint8_t long_ssh[SSH_VERSION_MAX + 2];
    memcpy(long_ssh, "SSH-", 4);
    memset(long_ssh + 4, 'x', SSH_VERSION_MAX - 4);
    long_ssh[SSH_VERSION_MAX] = '\n';
    ssh_transport_init(0);
    TEST_ASSERT_EQUAL_INT(-1, ssh_transport_recv_banner(0, long_ssh, SSH_VERSION_MAX + 1, &consumed));

    // A line with no terminator that exceeds the maximum length.
    static uint8_t long_line[SSH_VERSION_MAX + 4];
    memset(long_line, 'y', sizeof(long_line));
    ssh_transport_init(0);
    TEST_ASSERT_EQUAL_INT(-1, ssh_transport_recv_banner(0, long_line, sizeof(long_line), &consumed));

    ssh_transport_init(0);
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_build(0, small, &l, 4)); // writer overflow
    TEST_ASSERT_EQUAL_INT(-1, ssh_extinfo_build(small, &l, 4));    // writer overflow
    uint8_t ks[8] = {0}, f[256] = {0}, sig[8] = {0};
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_build_reply(ks, sizeof(ks), f, sig, sizeof(sig), small, &l, 4));
    TEST_ASSERT_EQUAL_INT(-1, ssh_transport_begin_rekey(0, small, &l, 4)); // inner kexinit_build overflow
}

// KEXINIT parsing rejects an unusable name-list at every negotiated field, an
// over-long payload, and truncated / over-claimed name-list fields.
void test_kexinit_parse_field_and_trunc()
{
    ssh_transport_init(0);
    uint8_t buf[3000];
    const char *K = "diffie-hellman-group14-sha256", *H = "rsa-sha2-256", *C = "aes256-ctr", *M = "hmac-sha2-256",
               *P = "none";

    const char *rows[6][8] = {
        {K, "", C, C, M, M, P, P}, // host-key
        {K, H, C, "", M, M, P, P}, // encryption s2c
        {K, H, C, C, "", M, P, P}, // mac c2s
        {K, H, C, C, M, "", P, P}, // mac s2c
        {K, H, C, C, M, M, "", P}, // compression c2s
        {K, H, C, C, M, M, P, ""}, // compression s2c
    };
    for (int r = 0; r < 6; r++)
    {
        size_t n = build_kexinit8(buf, rows[r]);
        TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, n));
    }

    // Over-long payload (> SSH_KEXINIT_MAX).
    memset(buf, 0, sizeof(buf));
    buf[0] = SSH_MSG_KEXINIT;
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, SSH_KEXINIT_MAX + 8));

    // Truncated mid-field: the host-key name-list length header runs past the buffer.
    const char *good[8] = {K, H, C, C, M, M, P, P};
    size_t full = build_kexinit8(buf, good);
    size_t f2 = 1 + 16 + (4 + strlen(K)); // start of the host-key field
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, f2 + 2));

    // Over-claimed field length: host-key name-list claims ~4 GiB.
    build_kexinit8(buf, good);
    buf[f2] = buf[f2 + 1] = buf[f2 + 2] = buf[f2 + 3] = 0xFF;
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, full));
}

// KEXDH parsing rejects a wrong type, an mpint that runs past the payload, and a
// payload that fails to parse in the full handler.
void test_kexdh_parse_and_handle_errors()
{
    uint8_t e_be[256];
    uint8_t bad[8] = {99, 0, 0, 0, 1, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_parse_init(bad, 8, e_be)); // wrong type
    uint8_t over[8] = {SSH_MSG_KEXDH_INIT, 0, 0, 0, 100, 1, 2, 3}; // e claims 100 bytes, 3 present
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_parse_init(over, 8, e_be));

    uint8_t out[512];
    size_t olen = 0;
    ssh_transport_init(0);
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, bad, 8, out, &olen, sizeof(out))); // parse failure

    // A full, valid handshake whose reply does not fit the output buffer fails closed
    // after the DH/exchange-hash/sign steps succeed.
    ssh_transport_init(0);
    setup_rsa_fixture();
    SshSession *s = &ssh_sess[0];
    strcpy(s->v_c, "SSH-2.0-TestClient");
    s->v_c_len = (uint16_t)strlen(s->v_c);
    for (int j = 0; j < 30; j++)
    {
        s->i_c[j] = (uint8_t)(j + 1);
        s->i_s[j] = (uint8_t)(j + 100);
    }
    s->i_c_len = 30;
    s->i_s_len = 30;
    TEST_ASSERT_EQUAL_INT(0, ssh_dh_generate(0));
    memset(e_be, 0, sizeof(e_be));
    e_be[255] = 0x02; // valid e
    uint8_t pkt[300];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    size_t n = 1 + put_mpint(pkt + 1, e_be, 256);
    uint8_t reply[8];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, pkt, n, reply, &rlen, sizeof(reply)));
}

void test_kdf_edge_paths_and_slot_guards()
{
    uint8_t H[PC_SHA256_DIGEST_LEN];
    memset(H, 0x11, sizeof(H));

    // A K of all zeros exercises the empty-mpint (K == 0) branch of the KDF's mpint(K) hashing, and an
    // out_len beyond SSH_KDF_MAX exercises the clamp. (K == 0 cannot happen for a real DH secret.)
    uint8_t K0[256] = {0};
    uint8_t big[SSH_KDF_MAX];
    ssh_kdf_derive(K0, H, H, 'A', big, sizeof(big) + 64); // clamps to SSH_KDF_MAX; K == 0 -> empty mpint

    // Slot-index guards on the DH generate + the SID key derivation are no-ops / -1.
    TEST_ASSERT_EQUAL_INT(-1, ssh_dh_generate(MAX_SSH_CONNS));
    ssh_dh_derive_keys_sid(MAX_SSH_CONNS, K0, H, H, SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256); // must not crash

    // The chacha20-poly1305 branch derives two 512-bit keys and installs no separate MAC key.
    uint8_t K[256] = {0};
    K[0] = 0x42; // nonzero shared secret
    ssh_dh_derive_keys_sid(0, K, H, H, SSH_CIPHER_CHACHA20POLY1305, SSH_MAC_HMAC_SHA256);
    TEST_ASSERT_TRUE(ssh_keys[0].active);
    TEST_ASSERT_EQUAL_UINT8(SSH_CIPHER_CHACHA20POLY1305, ssh_keys[0].cipher_mode);
}

// KEXINIT truncated after each name-list boundary is rejected at the corresponding read.
void test_kexinit_parse_truncation_points()
{
    uint8_t buf[256];
    // One cut per name-list read, in field order: kex / host-key / cipher-c2s / cipher-s2c /
    // mac-c2s / mac-s2c / comp-c2s / comp-s2c.
    int cuts[] = {0, 1, 2, 3, 4, 5, 6, 7};
    for (unsigned i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++)
    {
        size_t n = build_partial_kexinit(buf, cuts[i]);
        TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, n));
    }
}

// prefer-RSA accessor + the non-RSA build ordering, slot guards, and a malformed ECDH_INIT.
void test_ssh_transport_more_guards()
{
    ssh_kex_set_prefer_rsa(true);
    TEST_ASSERT_TRUE(ssh_kex_prefer_rsa());
    ssh_kex_set_prefer_rsa(false);
    TEST_ASSERT_FALSE(ssh_kex_prefer_rsa());
    uint8_t kbuf[1024]; // the full advertised KEXINIT (kex+hostkey+cipher+mac+comp lists) exceeds 512
    size_t kn = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_build(0, kbuf, &kn, sizeof(kbuf))); // build_kex_list non-RSA branch
    ssh_kex_set_prefer_rsa(true);                                            // restore the setUp default

    TEST_ASSERT_EQUAL_INT(-1, ssh_kex_generate(200)); // out-of-range slot
    ssh_newkeys_sent(200);                            // out-of-range slot: no-op, no crash

    // Negotiate curve25519 so ssh_kexdh_handle takes the ECDH branch, then feed a malformed ECDH_INIT.
    uint8_t buf[256];
    size_t n = build_client_kexinit(buf, "curve25519-sha256", "rsa-sha2-256", "aes256-ctr", "hmac-sha2-256", "none");
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));
    uint8_t reply[1024];
    size_t rlen = 0;
    uint8_t too_short[4] = {SSH_MSG_KEXDH_INIT, 0, 0, 0}; // no room for the Q_C length field
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, too_short, sizeof(too_short), reply, &rlen, sizeof(reply)));
    uint8_t wrong_len[21] = {SSH_MSG_KEXDH_INIT, 0, 0, 0, 16}; // Q_C declared 16 octets, must be 32
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, wrong_len, sizeof(wrong_len), reply, &rlen, sizeof(reply)));
}

// aes256-gcm@openssh.com key derivation (RFC 5647 + RFC 4253 §7.2): ssh_dh_derive_keys_sid installs a
// per-direction AES-256-GCM context (256-bit key from label 'C'/'D', 96-bit initial IV = first 12
// bytes of label 'A'/'B'). Verified byte-exact: independently deriving the key + IV and sealing the
// same block must produce the same ciphertext+tag as the installed context, proving the GCM branch
// derived the right material into both directions.
void test_dh_derive_keys_gcm_installs()
{
    ssh_keymat_wipe(0);
    uint8_t K[256];
    uint8_t H[PC_SHA256_DIGEST_LEN];
    uint8_t sid[PC_SHA256_DIGEST_LEN];
    memset(K, 0, sizeof(K));
    K[0] = 0x11; // nonzero shared secret, MSB set -> mpint pad path
    K[255] = 0x22;
    for (int j = 0; j < PC_SHA256_DIGEST_LEN; j++)
    {
        H[j] = (uint8_t)(0x40 + j);
        sid[j] = (uint8_t)(0x90 + j);
    }

    ssh_dh_derive_keys_sid(0, K, H, sid, SSH_CIPHER_AES256GCM, SSH_MAC_HMAC_SHA256);
    TEST_ASSERT_TRUE(ssh_keys[0].active);
    TEST_ASSERT_EQUAL_UINT8(SSH_CIPHER_AES256GCM, ssh_keys[0].cipher_mode);

    // Independently derive the C->S key ('C') + IV ('A') and the S->C key ('D') + IV ('B').
    uint8_t iv_c[PC_SHA256_DIGEST_LEN];
    uint8_t iv_s[PC_SHA256_DIGEST_LEN];
    uint8_t key_c[PC_SHA256_DIGEST_LEN];
    uint8_t key_s[PC_SHA256_DIGEST_LEN];
    ssh_kdf_derive(K, H, sid, 'A', iv_c, PC_SHA256_DIGEST_LEN);
    ssh_kdf_derive(K, H, sid, 'B', iv_s, PC_SHA256_DIGEST_LEN);
    ssh_kdf_derive(K, H, sid, 'C', key_c, PC_SHA256_DIGEST_LEN);
    ssh_kdf_derive(K, H, sid, 'D', key_s, PC_SHA256_DIGEST_LEN);

    uint8_t pt[16];
    for (int j = 0; j < 16; j++)
    {
        pt[j] = (uint8_t)(j + 1);
    }
    uint8_t aad[4] = {0, 0, 0, 16};
    uint8_t seal_ref[16 + PC_AESGCM_TAG_LEN];
    uint8_t seal_km[16 + PC_AESGCM_TAG_LEN];

    // Reference seal derives the key independently; the keymat seal uses the CONTEXT the KEX installed
    // (there is no raw GCM key in the keymat - install builds the context and wipes the key). Identical
    // output proves the installed context is correct, not merely that two copies of a key matched.
    pc_aesgcm_seal(gcm_key(key_c), iv_c, aad, sizeof(aad), pt, sizeof(pt), seal_ref, seal_ref + sizeof(pt));
    pc_aesgcm_seal(reinterpret_cast<pc_aesgcm_key *>(ssh_keys[0].gcm_ctx_c2s), ssh_keys[0].aes_iv_c2s, aad, sizeof(aad),
                   pt, sizeof(pt), seal_km, seal_km + sizeof(pt));
    TEST_ASSERT_EQUAL_MEMORY(seal_ref, seal_km, sizeof(seal_ref)); // C->S key + IV byte-correct

    pc_aesgcm_seal(gcm_key(key_s), iv_s, aad, sizeof(aad), pt, sizeof(pt), seal_ref, seal_ref + sizeof(pt));
    pc_aesgcm_seal(reinterpret_cast<pc_aesgcm_key *>(ssh_keys[0].gcm_ctx_s2c), ssh_keys[0].aes_iv_s2c, aad, sizeof(aad),
                   pt, sizeof(pt), seal_km, seal_km + sizeof(pt));
    TEST_ASSERT_EQUAL_MEMORY(seal_ref, seal_km, sizeof(seal_ref)); // S->C key + IV byte-correct

    ssh_keymat_wipe(0);
}

// Hybrid KEX (mlkem768x25519-sha256): K is hashed as a plain 32-byte SSH string (the last 32 octets of
// the K buffer), NOT as a canonical mpint. ssh_kdf_derive(..., k_is_string=true) must match an
// independent string-encoded computation, and must differ from the mpint encoding of the same buffer.
void test_kdf_string_k_hybrid()
{
    uint8_t K[256];
    uint8_t H[PC_SHA256_DIGEST_LEN];
    uint8_t sid[PC_SHA256_DIGEST_LEN];
    for (int i = 0; i < 256; i++)
    {
        K[i] = (uint8_t)(i * 11 + 3);
    }
    for (int i = 0; i < PC_SHA256_DIGEST_LEN; i++)
    {
        H[i] = (uint8_t)(0x20 + i);
        sid[i] = (uint8_t)(0x70 + i);
    }

    uint8_t got[PC_SHA256_DIGEST_LEN];
    ssh_kdf_derive(K, H, sid, 'C', got, PC_SHA256_DIGEST_LEN, true); // K encoded as a 32-byte string

    // Independent: K1 = SHA256( string(K[224:256]) || H || 'C' || sid ), string = 4-byte len(32) || bytes.
    uint8_t len_be[4] = {0, 0, 0, 32};
    pc_sha256_ctx c;
    pc_sha256_init(&c);
    pc_sha256_update(&c, len_be, 4);
    pc_sha256_update(&c, K + (256 - 32), 32);
    pc_sha256_update(&c, H, PC_SHA256_DIGEST_LEN);
    uint8_t lbl = 'C';
    pc_sha256_update(&c, &lbl, 1);
    pc_sha256_update(&c, sid, PC_SHA256_DIGEST_LEN);
    uint8_t expected[PC_SHA256_DIGEST_LEN];
    pc_sha256_final(&c, expected);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, PC_SHA256_DIGEST_LEN);

    // The mpint encoding of the same buffer yields a different key (string != mpint encoding).
    uint8_t as_mpint[PC_SHA256_DIGEST_LEN];
    ssh_kdf_derive(K, H, sid, 'C', as_mpint, PC_SHA256_DIGEST_LEN, false);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(got, as_mpint, PC_SHA256_DIGEST_LEN));
}

// Regression for the client-preference KEX bug (RFC 4253 §7.1). The exact CycloneSSH v2.6.4 KEXINIT +
// KEX_ECDH_INIT bytes captured on the wire (kex list: curve25519-sha256 first, then nistp256; host keys
// ssh-ed25519 first). Before the fix the server negotiated by ITS order (nistp256 + rsa-sha2-512 when an
// RSA key is held), so the client's 32-byte curve25519 init failed to parse as the server's 65-byte
// nistp256 init and ssh_kexdh_handle returned -1 (the P4 reset the connection here). With client
// preference the server picks curve25519 + ssh-ed25519 to match the client, and the full KEX completes.
static void test_cyclonessh_kex_repro(void)
{
    ssh_transport_init(0);
    pc_ssh_hostkey_ed25519_set(BASELINE_ED25519_SEEDS[0]);
    SshSession *s = &ssh_sess[0];
    const char *vc = "SSH-2.0-CycloneSSH_2.6.4";
    strcpy(s->v_c, vc);
    s->v_c_len = (uint16_t)strlen(vc);

    int rc_parse = ssh_kexinit_parse(0, CYCLONE_KEXINIT, sizeof(CYCLONE_KEXINIT));
    printf("[repro] parse rc=%d kex_alg=%d hostkey_alg=%d cipher=%d\n", rc_parse, (int)s->kex_alg, (int)s->hostkey_alg,
           (int)s->cipher_alg);
    TEST_ASSERT_EQUAL_INT(0, rc_parse);

    uint8_t isbuf[PC_SSH_KEXINIT_S_MAX];
    size_t isn = 0;
    int rc_build = ssh_kexinit_build(0, isbuf, &isn, sizeof(isbuf));
    printf("[repro] build rc=%d isn=%zu\n", rc_build, isn);
    TEST_ASSERT_EQUAL_INT(0, rc_build);

    int rc_gen = ssh_kex_generate(0);
    printf("[repro] gen rc=%d\n", rc_gen);
    TEST_ASSERT_EQUAL_INT(0, rc_gen);

    uint8_t reply[1024];
    size_t rlen = 0;
    int rc_handle = ssh_kexdh_handle(0, CYCLONE_ECDH_INIT, sizeof(CYCLONE_ECDH_INIT), reply, &rlen, sizeof(reply));
    printf("[repro] handle rc=%d rlen=%zu\n", rc_handle, rlen);
    TEST_ASSERT_EQUAL_INT(0, rc_handle);
}

// pc_ssh_hostkey_ecdsa_set derives the public point from the scalar and installs nothing when the
// scalar is not a valid P-256 private key (d == 0 or d >= n): availability must be left untouched, so
// a bad key never ends up advertised in the KEXINIT host-key name-list.
void test_hostkey_ecdsa_set_rejects_invalid_scalar()
{
    const bool before = pc_ssh_hostkey_ecdsa_available();

    uint8_t zero[PC_ECDSA_P256_PRIV_LEN];
    memset(zero, 0, sizeof(zero)); // d = 0 is not in [1, n)
    pc_ssh_hostkey_ecdsa_set(zero);
    TEST_ASSERT_EQUAL(before, pc_ssh_hostkey_ecdsa_available());

    uint8_t over[PC_ECDSA_P256_PRIV_LEN];
    memset(over, 0xFF, sizeof(over)); // d >= n
    pc_ssh_hostkey_ecdsa_set(over);
    TEST_ASSERT_EQUAL(before, pc_ssh_hostkey_ecdsa_available());
}

// RFC 4253 §7.1 negotiates each direction independently, but this server runs one cipher and one MAC
// for both: a client whose two directions land on different algorithms is rejected rather than keyed
// asymmetrically. Both lists here negotiate cleanly on their own - only the mismatch fails.
void test_kexinit_parse_rejects_direction_mismatch()
{
    ssh_transport_init(0);
    uint8_t buf[512];
    const char *K = "diffie-hellman-group14-sha256";
    const char *H = "rsa-sha2-256";
    const char *P = "none";

    const char *cipher_split[8] = {
        K, H, "aes256-ctr", "chacha20-poly1305@openssh.com", "hmac-sha2-256", "hmac-sha2-256", P, P};
    size_t n = build_kexinit8(buf, cipher_split);
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, n));

    // Same cipher both ways (aes256-ctr, so a separate MAC IS negotiated) but a different MAC per
    // direction.
    const char *mac_split[8] = {K, H, "aes256-ctr", "aes256-ctr", "hmac-sha2-256", "hmac-sha2-512", P, P};
    n = build_kexinit8(buf, mac_split);
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, n));
}

// Both AEAD ciphers carry their own integrity tag, so with chacha20-poly1305 negotiated the MAC
// name-lists are never negotiated: lists with nothing in common (even an empty one) must not fail
// the key exchange.
void test_kexinit_parse_aead_ignores_mac_lists()
{
    ssh_transport_init(0);
    uint8_t buf[512];
    const char *rows[8] = {"diffie-hellman-group14-sha256",
                           "rsa-sha2-256",
                           "chacha20-poly1305@openssh.com",
                           "chacha20-poly1305@openssh.com",
                           "umac-64@openssh.com",
                           "",
                           "none",
                           "none"};
    size_t n = build_kexinit8(buf, rows);
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_CIPHER_CHACHA20POLY1305, ssh_sess[0].cipher_alg);
}

// Name-list matching (RFC 4253 §7.1) compares whole elements: a name that is exactly as long as one
// of ours but differs in content must not match. "aes256-abc" is the length of "aes256-ctr" and
// "zlib" the length of "none", so a length-only comparison would mis-match both.
void test_kexinit_parse_same_length_names_do_not_match()
{
    ssh_transport_init(0);
    uint8_t buf[512];
    const char *K = "diffie-hellman-group14-sha256";
    const char *H = "rsa-sha2-256";
    const char *M = "hmac-sha2-256";

    // The same-length impostor is skipped and negotiation lands on the real name behind it.
    const char *rows[8] = {K, H, "aes256-abc,aes256-ctr", "aes256-abc,aes256-ctr", M, M, "zlib,none", "zlib,none"};
    size_t n = build_kexinit8(buf, rows);
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, buf, n));
    TEST_ASSERT_EQUAL(SSH_CIPHER_AES256CTR, ssh_sess[0].cipher_alg);

    // A c2s compression list holding only the impostor offers no "none": rejected.
    const char *nocomp[8] = {K, H, "aes256-ctr", "aes256-ctr", M, M, "zlib", "none"};
    n = build_kexinit8(buf, nocomp);
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, n));
}

// RFC 8308 server-sig-algs is ordered by the same preference that steers host-key negotiation: with
// prefer-RSA off the modern algorithms come first.
void test_extinfo_build_modern_first_order()
{
    uint8_t out[128];
    size_t n = 0;
    ssh_kex_set_prefer_rsa(false);
    const int rc = ssh_extinfo_build(out, &n, sizeof(out));
    ssh_kex_set_prefer_rsa(true); // restore the setUp default before any assertion can abort

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL(SSH_MSG_EXT_INFO, out[0]);
    // byte(EXT_INFO) || uint32(1) || string("server-sig-algs") || string(value)
    size_t off = 1 + 4;
    const uint8_t *name = NULL;
    const uint8_t *value = NULL;
    uint32_t name_len = 0, value_len = 0;
    TEST_ASSERT_TRUE(rd_string(out, n, &off, &name, &name_len));
    TEST_ASSERT_EQUAL_UINT32(15u, name_len);
    TEST_ASSERT_EQUAL_MEMORY("server-sig-algs", name, 15);
    TEST_ASSERT_TRUE(rd_string(out, n, &off, &value, &value_len));
    const char *want = "ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-512,rsa-sha2-256";
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(want), value_len);
    TEST_ASSERT_EQUAL_MEMORY(want, value, strlen(want));
}

// curve25519-sha256 (RFC 8731): the ECDH_INIT parser checks the message number and that the declared
// Q_C really is present, not just that it claims 32 octets.
void test_kexdh_handle_curve25519_rejects_malformed_init()
{
    ssh_transport_init(0);
    setup_rsa_fixture();
    ssh_sess[0].kex_alg = SSH_KEX_CURVE25519;
    ssh_sess[0].hostkey_alg = SSH_HOSTKEY_RSA_SHA256;
    TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));

    uint8_t reply[512];
    size_t rlen = 0;

    uint8_t wrong_type[40] = {SSH_MSG_KEXDH_REPLY, 0, 0, 0, 32}; // msg 31, not 30
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, wrong_type, sizeof(wrong_type), reply, &rlen, sizeof(reply)));

    uint8_t truncated[20] = {SSH_MSG_KEXDH_INIT, 0, 0, 0, 32}; // declares 32 octets, 15 present
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, truncated, sizeof(truncated), reply, &rlen, sizeof(reply)));
}

// ecdh-sha2-nistp256 (RFC 5656 §4): the same three guards on the 65-byte uncompressed-point form.
void test_kexdh_handle_ecdh_p256_rejects_malformed_init()
{
    ssh_transport_init(0);
    setup_rsa_fixture();
    ssh_sess[0].kex_alg = SSH_KEX_ECDH_NISTP256;
    ssh_sess[0].hostkey_alg = SSH_HOSTKEY_RSA_SHA256;
    TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));

    uint8_t reply[512];
    size_t rlen = 0;

    uint8_t too_short[4] = {SSH_MSG_KEXDH_INIT, 0, 0, 0}; // no room for the Q_C length field
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, too_short, sizeof(too_short), reply, &rlen, sizeof(reply)));

    uint8_t wrong_type[80] = {SSH_MSG_KEXDH_REPLY, 0, 0, 0, 65};
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, wrong_type, sizeof(wrong_type), reply, &rlen, sizeof(reply)));

    uint8_t wrong_len[80] = {SSH_MSG_KEXDH_INIT, 0, 0, 0, 32}; // Q_C declared 32, must be 65
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, wrong_len, sizeof(wrong_len), reply, &rlen, sizeof(reply)));

    uint8_t truncated[20] = {SSH_MSG_KEXDH_INIT, 0, 0, 0, 65}; // declares 65 octets, 15 present
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, truncated, sizeof(truncated), reply, &rlen, sizeof(reply)));
}

// Defensive: a server that selected ecdsa-sha2-nistp256 as its host-key algorithm while holding no
// P-256 key (mis-provisioned, or the algorithm forced by hand) cannot sign the exchange hash, so
// the key exchange fails closed instead of emitting a signature made with a zero scalar.
// MUST run before any test installs a P-256 host key - the transport ctx is not per-session.
void test_kexdh_handle_ecdsa_hostkey_absent_fails()
{
    TEST_ASSERT_FALSE(pc_ssh_hostkey_ecdsa_available());
    ssh_transport_init(0);
    ssh_sess[0].kex_alg = SSH_KEX_CURVE25519;
    ssh_sess[0].hostkey_alg = SSH_HOSTKEY_ECDSA_NISTP256;
    ssh_sess[0].v_c_len = ssh_sess[0].i_c_len = ssh_sess[0].i_s_len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));

    uint8_t client_sk[32];
    uint8_t qc[32];
    for (int j = 0; j < 32; j++)
    {
        client_sk[j] = (uint8_t)(0x20 + j);
    }
    pc_x25519_base(qc, client_sk);

    uint8_t pkt[64];
    pkt[0] = SSH_MSG_KEXDH_INIT;
    pkt[1] = pkt[2] = pkt[3] = 0;
    pkt[4] = 32;
    memcpy(pkt + 5, qc, 32);
    uint8_t reply[512];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, pkt, 1 + 4 + 32, reply, &rlen, sizeof(reply)));
    TEST_ASSERT_NOT_EQUAL(SSH_PHASE_NEWKEYS, ssh_sess[0].phase);
}

// RFC 4253 §4.2 preamble handling copes with the degenerate lines too: a bare LF (an empty line,
// so there is no trailing CR to strip) and a line shorter than the four bytes "SSH-" needs.
void test_recv_banner_empty_and_short_preamble_lines()
{
    ssh_transport_init(0);
    const char *data = "\nab\r\nSSH-2.0-Real\r\n";
    size_t consumed = 0;
    TEST_ASSERT_EQUAL_INT(1, ssh_transport_recv_banner(0, (const uint8_t *)data, strlen(data), &consumed));
    TEST_ASSERT_EQUAL_STRING("SSH-2.0-Real", ssh_sess[0].v_c);
    TEST_ASSERT_EQUAL_size_t(strlen(data), consumed);
}

// KEXINIT parsing rejects a payload too short to hold the message byte and the 16-byte cookie, and
// one of the right size that carries some other message number.
void test_kexinit_parse_rejects_short_and_mistyped()
{
    ssh_transport_init(0);
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    buf[0] = SSH_MSG_KEXINIT;
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, 16)); // one byte short of msg + cookie
    buf[0] = SSH_MSG_NEWKEYS;
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexinit_parse(0, buf, sizeof(buf)));
}

// An mpint of nothing but zero bytes is the value zero: the leading-zero strip walks the whole
// field and e comes out as an all-zero 2048-bit value (which bn_dh_validate then rejects).
void test_kexdh_parse_init_accepts_all_zero_mpint()
{
    uint8_t e_be[256];
    memset(e_be, 0xAA, sizeof(e_be));
    uint8_t pkt[16] = {SSH_MSG_KEXDH_INIT, 0, 0, 0, 4, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(0, ssh_kexdh_parse_init(pkt, 9, e_be));
    for (size_t k = 0; k < sizeof(e_be); k++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, e_be[k]);
    }
}

// ecdh-sha2-nistp256: an ephemeral scalar that is not a valid P-256 private key (zero) cannot
// produce a server public point, so the exchange fails before any shared secret is computed.
void test_kexdh_handle_ecdh_p256_rejects_bad_ephemeral()
{
    ssh_transport_init(0);
    setup_rsa_fixture();
    ssh_sess[0].kex_alg = SSH_KEX_ECDH_NISTP256;
    ssh_sess[0].hostkey_alg = SSH_HOSTKEY_RSA_SHA256;
    TEST_ASSERT_EQUAL_INT(0, ssh_kex_generate(0));
    memset(ssh_sess[0].ecdh_sk, 0, 32); // zero is not in [1, n)

    uint8_t qc[PC_ECDSA_P256_PUB_LEN];
    uint8_t d[32];
    memset(d, 0, sizeof(d));
    d[31] = 0x07;
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(qc, d)); // a well-formed client point

    uint8_t pkt[8 + PC_ECDSA_P256_PUB_LEN];
    size_t n = 0;
    pkt[n++] = SSH_MSG_KEXDH_INIT;
    pkt[n++] = 0;
    pkt[n++] = 0;
    pkt[n++] = 0;
    pkt[n++] = (uint8_t)PC_ECDSA_P256_PUB_LEN;
    memcpy(pkt + n, qc, PC_ECDSA_P256_PUB_LEN);
    n += PC_ECDSA_P256_PUB_LEN;

    uint8_t reply[512];
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(-1, ssh_kexdh_handle(0, pkt, n, reply, &rlen, sizeof(reply)));
}

// The re-key trigger watches BOTH sequence numbers: a receive-heavy session hits the threshold on
// seq_no_recv alone (RFC 4253 §9).
void test_rekey_needed_on_receive_sequence_alone()
{
    ssh_pkt_init(0);
    ssh_pkt[0].seq_no_send = 0;
    ssh_pkt[0].seq_no_recv = SSH_REKEY_PACKET_THRESHOLD;
    TEST_ASSERT_TRUE(ssh_rekey_needed(0));
    ssh_pkt_init(0);
}

int main()
{
    UNITY_BEGIN();
    // First: pc_ssh_hostkey_ecdsa_set must be probed before any test installs a P-256 host key, so
    // the "an invalid scalar changes nothing" assertion is made against a clean transport ctx.
    RUN_TEST(test_hostkey_ecdsa_set_rejects_invalid_scalar);
    // Also order-sensitive: must see the transport ctx with no P-256 key installed.
    RUN_TEST(test_kexdh_handle_ecdsa_hostkey_absent_fails);
    RUN_TEST(test_transport_index_guards);
    RUN_TEST(test_banner_and_build_caps);
    RUN_TEST(test_kexinit_parse_field_and_trunc);
    RUN_TEST(test_kexdh_parse_and_handle_errors);
    RUN_TEST(test_server_banner_format);
    RUN_TEST(test_recv_banner_complete);
    RUN_TEST(test_recv_banner_bare_lf);
    RUN_TEST(test_recv_banner_split_across_reads);
    RUN_TEST(test_recv_banner_skips_preamble_lines);
    RUN_TEST(test_kexinit_build_starts_with_msg_and_stores_is);
    RUN_TEST(test_kexinit_parse_accepts_supported);
    RUN_TEST(test_kexinit_parse_accepts_when_ours_listed_among_others);
    RUN_TEST(test_kexinit_parse_rejects_missing_kex);
    RUN_TEST(test_kexinit_parse_rejects_hostkey_we_lack);
    RUN_TEST(test_kexinit_parse_steers_to_curve_ed25519);
    RUN_TEST(test_kexinit_parse_rejects_missing_cipher);
    RUN_TEST(test_kexinit_parse_selects_chacha20poly1305);
    RUN_TEST(test_kexinit_parse_selects_aes256gcm);
    RUN_TEST(test_kexinit_parse_honors_client_cipher_preference);
    RUN_TEST(test_kexinit_parse_selects_rsa_sha512);
    RUN_TEST(test_kexinit_parse_selects_ecdsa);
    RUN_TEST(test_kexinit_parse_selects_ecdh_nistp256);
    RUN_TEST(test_kexinit_parse_selects_etm_mac);
    RUN_TEST(test_kexinit_parse_rejects_truncated);
    RUN_TEST(test_exchange_hash_matches_independent_assembly);
    RUN_TEST(test_exchange_hash_changes_with_input);
    RUN_TEST(test_kexdh_parse_init_extracts_e_with_padding);
    RUN_TEST(test_kexdh_parse_init_extracts_small_e);
    RUN_TEST(test_kexdh_parse_init_rejects_wrong_type);
    RUN_TEST(test_kexdh_parse_init_rejects_oversized_e);
    RUN_TEST(test_kexdh_build_reply_structure);
    RUN_TEST(test_kexdh_handle_produces_reply_and_installs_keys);
    RUN_TEST(test_kexdh_handle_rejects_invalid_e);
    RUN_TEST(test_kexdh_handle_curve25519_ed25519_end_to_end);
    RUN_TEST(test_kexdh_handle_curve25519_rejects_low_order);
    RUN_TEST(test_kexdh_handle_ecdh_nistp256_end_to_end);
    RUN_TEST(test_kexdh_handle_ecdh_nistp256_rejects_bad_point);
    RUN_TEST(test_kexdh_handle_rsa_sha512_signature);
    RUN_TEST(test_kexdh_handle_ecdsa_end_to_end);
    RUN_TEST(test_derive_keys_session_id_affects_output);
    RUN_TEST(test_rekey_needed_threshold);
    RUN_TEST(test_rekey_due_volume_and_time);
    RUN_TEST(test_begin_rekey_preserves_session_and_auth);
    RUN_TEST(test_kdf_edge_paths_and_slot_guards);
    RUN_TEST(test_kexinit_parse_truncation_points);
    RUN_TEST(test_ssh_transport_more_guards);
    RUN_TEST(test_dh_derive_keys_gcm_installs);
    RUN_TEST(test_kdf_string_k_hybrid);
    RUN_TEST(test_kexinit_parse_rejects_direction_mismatch);
    RUN_TEST(test_kexinit_parse_aead_ignores_mac_lists);
    RUN_TEST(test_kexinit_parse_same_length_names_do_not_match);
    RUN_TEST(test_extinfo_build_modern_first_order);
    RUN_TEST(test_kexdh_handle_curve25519_rejects_malformed_init);
    RUN_TEST(test_kexdh_handle_ecdh_p256_rejects_malformed_init);
    RUN_TEST(test_recv_banner_empty_and_short_preamble_lines);
    RUN_TEST(test_kexinit_parse_rejects_short_and_mistyped);
    RUN_TEST(test_kexdh_parse_init_accepts_all_zero_mpint);
    RUN_TEST(test_kexdh_handle_ecdh_p256_rejects_bad_ephemeral);
    RUN_TEST(test_rekey_needed_on_receive_sequence_alone);
    // Runs LAST on purpose: it provisions all three host-key types and the availability lives in the
    // (non-per-session) transport ctx that setUp does not clear, so leaving it set must not perturb the
    // order-sensitive earlier tests (e.g. rejects_hostkey_we_lack assumes only the RSA fixture is held).
    RUN_TEST(test_kexinit_hostkey_list_carries_all_four_when_all_keys_loaded);
    // Also LAST (leaves an ed25519 host key set): the CycloneSSH client-preference regression.
    RUN_TEST(test_cyclonessh_kex_repro);
    return UNITY_END();
}
