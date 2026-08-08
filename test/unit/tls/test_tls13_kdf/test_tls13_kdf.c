// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the TLS 1.3 key schedule (network_drivers/tls/tls13_kdf; RFC 8446
// sec 7.1 / 4.4.4). Pinned to the RFC 8448 sec 3 "Simple 1-RTT Handshake" worked trace, which lists
// every intermediate secret and the X25519 shared secret directly:
//   - Early Secret         (Extract, salt 0, IKM 0^32).
//   - Handshake Secret + client/server handshake traffic secrets (Extract of the X25519 output).
//   - Master Secret + client/server application traffic secrets.
//   - Server handshake write key/iv (HKDF-Expand-Label "key"/"iv") - checks the TLS label path.
//   - Server Finished verify_data over the full ClientHello..CertificateVerify transcript.
// Pure host crypto (software SHA-256/HMAC on native).

#include "crypto/hash/sha256.h"
#include "crypto/kdf/hkdf.h"
#include "network_drivers/tls/tls13_kdf.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// Decode a hex string (spaces/newlines ignored) into buf; returns byte count.
static size_t hx(const char *s, uint8_t *buf, size_t cap)
{
    size_t n = 0;
    int hi = -1;
    for (; *s; s++)
    {
        char c = *s;
        int v;
        if (c >= '0' && c <= '9')
        {
            v = c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            v = c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            v = c - 'A' + 10;
        }
        else
        {
            continue;
        }
        if (hi < 0)
        {
            hi = v;
        }
        else
        {
            TEST_ASSERT_TRUE(n < cap);
            buf[n++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }
    }
    TEST_ASSERT_EQUAL_INT(-1, hi);
    return n;
}

// --- RFC 8448 sec 3 constants ---------------------------------------------------------------
static const char *ECDHE = "8bd4054fb55b9d63fdfbacf9f04b9f0d35e6d63f537563efd46272900f89492d";
static const char *CH_SH_HASH = "860c06edc07858ee8e78f0e7428c58edd6b43f2ca3e6e95f02ed063cf0e1cad8";
static const char *CH_SFIN_HASH = "9608102a0f1ccc6db6250b7b7e417b1a000eaada3daae4777a7686c9ff83df13";

static const char *EARLY = "33ad0a1c607ec03b09e6cd9893680ce210adf300aa1f2660e1b22e10f170f92a";
static const char *HANDSHAKE = "1dc826e93606aa6fdc0aadc12f741b01046aa6b99f691ed221a9f0ca043fbeac";
static const char *MASTER = "18df06843d13a08bf2a449844c5f8a478001bc4d4c627984d5a41da8d0402919";
static const char *C_HS = "b3eddb126e067f35a780b3abf45e2d8f3b1a950738f52e9600746a0e27a55a21";
static const char *S_HS = "b67b7d690cc16c4e75e54213cb2d37b4e9c912bcded9105d42befd59d391ad38";
static const char *C_AP = "9e40646ce79a7f9dc05af8889bce6552875afa0b06df0087f792ebb7c17504a5";
static const char *S_AP = "a11af9f05531f856ad47116b45a9503282 04b4f4 4bfb6b3a4b4f1f3fcb631643";

// --- Key schedule steps -----------------------------------------------------------------------
void test_early_secret()
{
    uint8_t exp[32];
    hx(EARLY, exp, 32);
    Tls13KeySchedule ks;
    pc_tls13_ks_early(&TLS13_KDF, &ks);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, ks.early_secret, 32);
}

void test_handshake_secrets()
{
    uint8_t ecdhe[32], ch_sh[32];
    hx(ECDHE, ecdhe, 32);
    hx(CH_SH_HASH, ch_sh, 32);
    Tls13KeySchedule ks;
    pc_tls13_ks_early(&TLS13_KDF, &ks);
    pc_tls13_ks_handshake(&ks, ecdhe, ch_sh, 32);

    uint8_t exp[32];
    hx(HANDSHAKE, exp, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, ks.handshake_secret, 32);
    hx(C_HS, exp, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, ks.client_hs_traffic, 32);
    hx(S_HS, exp, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, ks.server_hs_traffic, 32);
}

void test_master_secrets()
{
    uint8_t ecdhe[32], ch_sh[32], ch_sfin[32];
    hx(ECDHE, ecdhe, 32);
    hx(CH_SH_HASH, ch_sh, 32);
    hx(CH_SFIN_HASH, ch_sfin, 32);
    Tls13KeySchedule ks;
    pc_tls13_ks_early(&TLS13_KDF, &ks);
    pc_tls13_ks_handshake(&ks, ecdhe, ch_sh, 32);
    pc_tls13_ks_master(&ks, ch_sfin);

    uint8_t exp[32];
    hx(MASTER, exp, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, ks.master_secret, 32);
    hx(C_AP, exp, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, ks.client_ap_traffic, 32);
    hx(S_AP, exp, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, ks.server_ap_traffic, 32);
}

// Server handshake write key/iv are HKDF-Expand-Label(s_hs_traffic, "key"/"iv") - the plain TLS
// labels (QUIC uses "quic key"/"quic iv" instead). Confirms the label path against RFC 8448.
void test_server_hs_write_keys()
{
    uint8_t s_hs[32];
    hx(S_HS, s_hs, 32);
    uint8_t key[16], iv[12], exp_key[16], exp_iv[12];
    pc_hkdf_expand_label(s_hs, "key", key, sizeof(key), PC_HKDF_LABEL_PREFIX);
    pc_hkdf_expand_label(s_hs, "iv", iv, sizeof(iv), PC_HKDF_LABEL_PREFIX);
    hx("3fce516009c21727d0f2e4e86ee403bc", exp_key, 16);
    hx("5d313eb2671276ee13000b30", exp_iv, 12);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_key, key, 16);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_iv, iv, 12);
}

// Full server Finished: verify_data = HMAC(finished_key(s_hs_traffic), Hash(CH..CertificateVerify)).
// The transcript is hashed from the raw handshake messages (4-byte headers included), exercising the
// exact construction the real handshake will use. Expected verify_data from RFC 8448 sec 3.
void test_server_finished()
{
    // ClientHello (196 octets).
    static const char *CH =
        "01 00 00 c0 03 03 cb 34 ec b1 e7 81 63 ba 1c 38 c6 da cb 19 6a 6d ff a2 1a 8d 99 12 ec 18 a2 ef 62 83"
        "02 4d ec e7 00 00 06 13 01 13 03 13 02 01 00 00 91 00 00 00 0b 00 09 00 00 06 73 65 72 76 65 72 ff 01"
        "00 01 00 00 0a 00 14 00 12 00 1d 00 17 00 18 00 19 01 00 01 01 01 02 01 03 01 04 00 23 00 00 00 33 00"
        "26 00 24 00 1d 00 20 99 38 1d e5 60 e4 bd 43 d2 3d 8e 43 5a 7d ba fe b3 c0 6e 51 c1 3c ae 4d 54 13 69"
        "1e 52 9a af 2c 00 2b 00 03 02 03 04 00 0d 00 20 00 1e 04 03 05 03 06 03 02 03 08 04 08 05 08 06 04 01"
        "05 01 06 01 02 01 04 02 05 02 06 02 02 02 00 2d 00 02 01 01 00 1c 00 02 40 01";
    // ServerHello (90 octets).
    static const char *SH =
        "02 00 00 56 03 03 a6 af 06 a4 12 18 60 dc 5e 6e 60 24 9c d3 4c 95 93 0c 8a c5 cb 14 34 da c1 55 77 2e"
        "d3 e2 69 28 00 13 01 00 00 2e 00 33 00 24 00 1d 00 20 c9 82 88 76 11 20 95 fe 66 76 2b db f7 c6 72 e1"
        "56 d6 cc 25 3b 83 3d f1 dd 69 b1 b0 4e 75 1f 0f 00 2b 00 02 03 04";
    // EncryptedExtensions (40 octets).
    static const char *EE = "08 00 00 24 00 22 00 0a 00 14 00 12 00 1d 00 17 00 18 00 19 01 00 01 01 01 02 01 03 01"
                            "04 00 1c 00 02 40 01 00 00 00 00";
    // Certificate (445 octets).
    static const char *CERT =
        "0b 00 01 b9 00 00 01 b5 00 01 b0 30 82 01 ac 30 82 01 15 a0 03 02 01 02 02 01 02 30 0d 06 09 2a 86 48"
        "86 f7 0d 01 01 0b 05 00 30 0e 31 0c 30 0a 06 03 55 04 03 13 03 72 73 61 30 1e 17 0d 31 36 30 37 33 30"
        "30 31 32 33 35 39 5a 17 0d 32 36 30 37 33 30 30 31 32 33 35 39 5a 30 0e 31 0c 30 0a 06 03 55 04 03 13"
        "03 72 73 61 30 81 9f 30 0d 06 09 2a 86 48 86 f7 0d 01 01 01 05 00 03 81 8d 00 30 81 89 02 81 81 00 b4"
        "bb 49 8f 82 79 30 3d 98 08 36 39 9b 36 c6 98 8c 0c 68 de 55 e1 bd b8 26 d3 90 1a 24 61 ea fd 2d e4 9a"
        "91 d0 15 ab bc 9a 95 13 7a ce 6c 1a f1 9e aa 6a f9 8c 7c ed 43 12 09 98 e1 87 a8 0e e0 cc b0 52 4b 1b"
        "01 8c 3e 0b 63 26 4d 44 9a 6d 38 e2 2a 5f da 43 08 46 74 80 30 53 0e f0 46 1c 8c a9 d9 ef bf ae 8e a6"
        "d1 d0 3e 2b d1 93 ef f0 ab 9a 80 02 c4 74 28 a6 d3 5a 8d 88 d7 9f 7f 1e 3f 02 03 01 00 01 a3 1a 30 18"
        "30 09 06 03 55 1d 13 04 02 30 00 30 0b 06 03 55 1d 0f 04 04 03 02 05 a0 30 0d 06 09 2a 86 48 86 f7 0d"
        "01 01 0b 05 00 03 81 81 00 85 aa d2 a0 e5 b9 27 6b 90 8c 65 f7 3a 72 67 17 06 18 a5 4c 5f 8a 7b 33 7d"
        "2d f7 a5 94 36 54 17 f2 ea e8 f8 a5 8c 8f 81 72 f9 31 9c f3 6b 7f d6 c5 5b 80 f2 1a 03 01 51 56 72 60"
        "96 fd 33 5e 5e 67 f2 db f1 02 70 2e 60 8c ca e6 be c1 fc 63 a4 2a 99 be 5c 3e b7 10 7c 3c 54 e9 b9 eb"
        "2b d5 20 3b 1c 3b 84 e0 a8 b2 f7 59 40 9b a3 ea c9 d9 1d 40 2d cc 0c c8 f8 96 12 29 ac 91 87 b4 2b 4d"
        "e1 00 00";
    // CertificateVerify (136 octets).
    static const char *CV =
        "0f 00 00 84 08 04 00 80 5a 74 7c 5d 88 fa 9b d2 e5 5a b0 85 a6 10 15 b7 21 1f 82 4c d4 84 14 5a b3 ff"
        "52 f1 fd a8 47 7b 0b 7a bc 90 db 78 e2 d3 3a 5c 14 1a 07 86 53 fa 6b ef 78 0c 5e a2 48 ee aa a7 85 c4"
        "f3 94 ca b6 d3 0b be 8d 48 59 ee 51 1f 60 29 57 b1 54 11 ac 02 76 71 45 9e 46 44 5c 9e a5 8c 18 1e 81"
        "8e 95 b8 c3 fb 0b f3 27 84 09 d3 be 15 2a 3d a5 04 3e 06 3d da 65 cd f5 ae a2 0d 53 df ac d4 2f 74 f3";

    uint8_t buf[512];
    pc_sha256_ctx sha;
    pc_sha256_init(&sha);
    size_t n;
    n = hx(CH, buf, sizeof(buf));
    pc_sha256_update(&sha, buf, n);
    n = hx(SH, buf, sizeof(buf));
    pc_sha256_update(&sha, buf, n);
    n = hx(EE, buf, sizeof(buf));
    pc_sha256_update(&sha, buf, n);
    n = hx(CERT, buf, sizeof(buf));
    pc_sha256_update(&sha, buf, n);
    n = hx(CV, buf, sizeof(buf));
    pc_sha256_update(&sha, buf, n);
    uint8_t thash[32];
    pc_sha256_final(&sha, thash);

    uint8_t s_hs[32];
    hx(S_HS, s_hs, 32);
    uint8_t verify[32], exp[32];
    pc_tls13_finished_mac(&TLS13_KDF, s_hs, thash, verify);
    hx("9b9b141d906337fbd2cbdce71df4deda4ab42c309572cb7fffee5454b78f0718", exp, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, verify, 32);
}

// pc_tls13_kdf_expand_label() is the KDF-variant HKDF-Expand-Label wrapper the record-key derivations
// call (the key-schedule steps above reach the HKDF core through pc_tls13_derive_secret instead, so this
// public entry point is otherwise unexercised). RFC 8448 sec 3: HKDF-Expand-Label("tls13 " prefix,
// server_handshake_traffic_secret, "key", 16) is the server write key - the same KAT as
// test_server_hs_write_keys, here through the wrapper. It must equal pc_hkdf_expand_label() with the
// TLS 1.3 prefix, which is exactly what the wrapper forwards to.
void test_kdf_expand_label_wrapper()
{
    uint8_t s_hs[32];
    hx(S_HS, s_hs, 32);

    uint8_t key[16], exp_key[16];
    pc_tls13_kdf_expand_label(&TLS13_KDF, s_hs, "key", key, sizeof(key));
    hx("3fce516009c21727d0f2e4e86ee403bc", exp_key, 16);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_key, key, 16);

    uint8_t via_quic[16];
    pc_hkdf_expand_label(s_hs, "key", via_quic, sizeof(via_quic), PC_HKDF_LABEL_PREFIX);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(via_quic, key, 16);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_early_secret);
    RUN_TEST(test_handshake_secrets);
    RUN_TEST(test_master_secrets);
    RUN_TEST(test_server_hs_write_keys);
    RUN_TEST(test_server_finished);
    RUN_TEST(test_kdf_expand_label_wrapper);
    return UNITY_END();
}
