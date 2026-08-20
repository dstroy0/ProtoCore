// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NTLMv2 response computation (network_drivers/application/smb/ntlm.h).
//
// MS-NLMP section 4.2 ("Cryptographic Values for Validation") publishes a complete worked NTLMv2
// exchange for User "User", domain "Domain", password "Password", server challenge
// 01 23 45 67 89 ab cd ef, client challenge aa x8 and a zero timestamp. Every expected byte below
// is copied from that section, not from any implementation.
//
// test_msnlmp_ntowfv2_worked_example is the load-bearing case: NTOWFv2 is the key everything else
// hangs off, so 4.2.4.1.1's published 0c 86 8a 40 ... value simultaneously pins the MD4 of the
// UTF-16LE password, the uppercasing rule for the user, the fact that the domain is NOT uppercased,
// the concatenation order and the HMAC-MD5 keying. A wrong NTOWFv2 makes every later value wrong,
// and a right one makes them checkable.

#include "network_drivers/application/smb/ntlm/ntlm.h"
#include <string.h>

#include <unity.h>

static uint8_t ntlm_work[16]; // the borrow an entry takes; Ntlm never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// MS-NLMP 4.2.1 Common Values, as UTF-8 (the module takes the ASCII form and widens it itself).
static const char *const USER = "User";
static const char *const DOMAIN_NAME = "Domain";
static const char *const PASSWORD = "Password";

// MS-NLMP 4.2.4.3 CHALLENGE_MESSAGE, ServerChallenge field at offset 0x18.
static const uint8_t SERVER_CHALLENGE[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
// MS-NLMP 4.2.1 ClientChallenge.
static const uint8_t CLIENT_CHALLENGE[8] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
static const uint8_t ZERO_TIME[8] = {0, 0, 0, 0, 0, 0, 0, 0};

// MS-NLMP 4.2.4.3, the TargetInfo carried inside the AUTHENTICATE_MESSAGE's NtChallengeResponse
// (offsets 0xB0..0xD3): MsvAvNbDomainName "Domain", MsvAvNbComputerName "Server", MsvAvEOL.
static const uint8_t TARGET_INFO[36] = {0x02, 0x00, 0x0c, 0x00, 0x44, 0x00, 0x6f, 0x00, 0x6d, 0x00, 0x61, 0x00,
                                        0x69, 0x00, 0x6e, 0x00, 0x01, 0x00, 0x0c, 0x00, 0x53, 0x00, 0x65, 0x00,
                                        0x72, 0x00, 0x76, 0x00, 0x65, 0x00, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00};

// MS-NLMP 4.2.4.1.1: "NTOWFv2(\"Password\", \"User\", \"Domain\") is
//   0c 86 8a 40 3b fd 7a 93 a3 00 1e f2 2e f0 2e 3f"
void test_msnlmp_ntowfv2_worked_example(void)
{
    uint8_t nt_hash[16];
    NtlmV.nt_hash_args.password = PASSWORD;
    NtlmV.nt_hash_args.nt_hash = nt_hash;
    Ntlm.nt_hash(ntlm_work);

    uint8_t owf[16];
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = USER;
    NtlmV.ntowfv2_args.domain = DOMAIN_NAME;
    NtlmV.ntowfv2_args.owf = owf;
    Ntlm.ntowfv2(ntlm_work);
    TEST_ASSERT_TRUE(NtlmV.ok);

    static const uint8_t WANT[16] = {0x0c, 0x86, 0x8a, 0x40, 0x3b, 0xfd, 0x7a, 0x93,
                                     0xa3, 0x00, 0x1e, 0xf2, 0x2e, 0xf0, 0x2e, 0x3f};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, owf, sizeof(WANT));
}

// MS-NLMP 3.3.1 defines NTOWFv1 as MD4(UNICODE(Passwd)), which is the NT hash this module computes.
// 4.2.2.1.2 publishes it for "Password":
//   a4 f4 9c 40 65 10 bd ca b6 82 4e e7 c3 0f d8 52
void test_nt_hash_is_the_published_ntowfv1(void)
{
    uint8_t nt_hash[16];
    NtlmV.nt_hash_args.password = PASSWORD;
    NtlmV.nt_hash_args.nt_hash = nt_hash;
    Ntlm.nt_hash(ntlm_work);
    static const uint8_t WANT[16] = {0xa4, 0xf4, 0x9c, 0x40, 0x65, 0x10, 0xbd, 0xca,
                                     0xb6, 0x82, 0x4e, 0xe7, 0xc3, 0x0f, 0xd8, 0x52};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, nt_hash, sizeof(WANT));
}

// MS-NLMP 4.2.4.2.2 gives the whole 84-octet NtChallengeResponse, and the AUTHENTICATE_MESSAGE in
// 4.2.4.3 carries it at offset 0x84 with NtChallengeResponseLen 0x54 = 84. Laid out, it is
//   NTProofStr(16) || 01 01 || Z(6) || Time(8) || ClientChallenge(8) || Z(4) || TargetInfo || Z(4)
// = 16 + 2 + 6 + 8 + 8 + 4 + 36 + 4 = 84.
// 4.2.4.1.2 publishes the SessionBaseKey the same computation produces.
void test_msnlmp_ntlmv2_response_and_session_base_key(void)
{
    uint8_t nt_hash[16];
    uint8_t owf[16];
    NtlmV.nt_hash_args.password = PASSWORD;
    NtlmV.nt_hash_args.nt_hash = nt_hash;
    Ntlm.nt_hash(ntlm_work);
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = USER;
    NtlmV.ntowfv2_args.domain = DOMAIN_NAME;
    NtlmV.ntowfv2_args.owf = owf;
    Ntlm.ntowfv2(ntlm_work);
    TEST_ASSERT_TRUE(NtlmV.ok);

    uint8_t out[128];
    uint8_t session_key[16];
    NtlmV.v2_response_args.owf = owf;
    NtlmV.v2_response_args.server_challenge = SERVER_CHALLENGE;
    NtlmV.v2_response_args.client_challenge = CLIENT_CHALLENGE;
    NtlmV.v2_response_args.timestamp = ZERO_TIME;
    NtlmV.v2_response_args.target_info = TARGET_INFO;
    NtlmV.v2_response_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.v2_response_args.out = out;
    NtlmV.v2_response_args.out_cap = sizeof(out);
    NtlmV.v2_response_args.session_key = session_key;
    Ntlm.v2_response(ntlm_work);
    size_t n = NtlmV.n;
    TEST_ASSERT_EQUAL_size_t(84, n);

    static const uint8_t WANT[84] = {
        // NTProofStr, MS-NLMP 4.2.4.2.2
        0x68, 0xcd, 0x0a, 0xb8, 0x51, 0xe5, 0x1c, 0x96, 0xaa, 0xbc, 0x92, 0x7b, 0xeb, 0xef, 0x6a, 0x1c,
        // temp: Responserversion, HiResponserversion, Z(6)
        0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Time
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // ClientChallenge
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        // Z(4)
        0x00, 0x00, 0x00, 0x00,
        // TargetInfo
        0x02, 0x00, 0x0c, 0x00, 0x44, 0x00, 0x6f, 0x00, 0x6d, 0x00, 0x61, 0x00, 0x69, 0x00, 0x6e, 0x00, 0x01, 0x00,
        0x0c, 0x00, 0x53, 0x00, 0x65, 0x00, 0x72, 0x00, 0x76, 0x00, 0x65, 0x00, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Z(4)
        0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    // MS-NLMP 4.2.4.1.2 Session Base Key.
    static const uint8_t WANT_KEY[16] = {0x8d, 0xe4, 0x0c, 0xca, 0xdb, 0xc1, 0x4a, 0x82,
                                         0xf1, 0x5c, 0xb0, 0xad, 0x0d, 0xe9, 0x5c, 0xa3};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_KEY, session_key, sizeof(WANT_KEY));
}

// MS-NLMP 3.3.2: NTOWFv2 hashes ConcatenationOf(Uppercase(User), UserDom). Only the user is
// uppercased, so a lowercase user reaches the same key as the published one while a lowercase
// domain does not.
void test_only_the_user_is_uppercased(void)
{
    uint8_t nt_hash[16];
    NtlmV.nt_hash_args.password = PASSWORD;
    NtlmV.nt_hash_args.nt_hash = nt_hash;
    Ntlm.nt_hash(ntlm_work);
    static const uint8_t WANT[16] = {0x0c, 0x86, 0x8a, 0x40, 0x3b, 0xfd, 0x7a, 0x93,
                                     0xa3, 0x00, 0x1e, 0xf2, 0x2e, 0xf0, 0x2e, 0x3f};

    uint8_t owf[16];
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = "user";
    NtlmV.ntowfv2_args.domain = DOMAIN_NAME;
    NtlmV.ntowfv2_args.owf = owf;
    Ntlm.ntowfv2(ntlm_work);
    TEST_ASSERT_TRUE(NtlmV.ok);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, owf, sizeof(WANT));
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = "USER";
    NtlmV.ntowfv2_args.domain = DOMAIN_NAME;
    NtlmV.ntowfv2_args.owf = owf;
    Ntlm.ntowfv2(ntlm_work);
    TEST_ASSERT_TRUE(NtlmV.ok);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, owf, sizeof(WANT));
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = "uSeR";
    NtlmV.ntowfv2_args.domain = DOMAIN_NAME;
    NtlmV.ntowfv2_args.owf = owf;
    Ntlm.ntowfv2(ntlm_work);
    TEST_ASSERT_TRUE(NtlmV.ok);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, owf, sizeof(WANT));

    // The domain is taken as given, so a different spelling is a different key.
    uint8_t lower_domain[16];
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = USER;
    NtlmV.ntowfv2_args.domain = "domain";
    NtlmV.ntowfv2_args.owf = lower_domain;
    Ntlm.ntowfv2(ntlm_work);
    TEST_ASSERT_TRUE(NtlmV.ok);
    TEST_ASSERT_TRUE(memcmp(WANT, lower_domain, 16) != 0);

    // An empty domain is legal and gives its own key.
    uint8_t no_domain[16];
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = USER;
    NtlmV.ntowfv2_args.domain = "";
    NtlmV.ntowfv2_args.owf = no_domain;
    Ntlm.ntowfv2(ntlm_work);
    TEST_ASSERT_TRUE(NtlmV.ok);
    TEST_ASSERT_TRUE(memcmp(WANT, no_domain, 16) != 0);
}

// The password is widened to UTF-16LE before hashing, so the NT hash is case-sensitive and no two
// spellings collide. MS-NLMP 4.2.1 shows Passwd as 50 00 61 00 73 00 ... - each ASCII octet
// followed by a zero.
void test_nt_hash_is_case_sensitive(void)
{
    uint8_t a[16];
    uint8_t b[16];
    uint8_t empty[16];
    NtlmV.nt_hash_args.password = "Password";
    NtlmV.nt_hash_args.nt_hash = a;
    Ntlm.nt_hash(ntlm_work);
    NtlmV.nt_hash_args.password = "password";
    NtlmV.nt_hash_args.nt_hash = b;
    Ntlm.nt_hash(ntlm_work);
    TEST_ASSERT_TRUE(memcmp(a, b, 16) != 0);

    // MD4 of the empty string is a fixed value, so an empty password still yields a defined hash
    // that differs from every non-empty one.
    NtlmV.nt_hash_args.password = "";
    NtlmV.nt_hash_args.nt_hash = empty;
    Ntlm.nt_hash(ntlm_work);
    TEST_ASSERT_TRUE(memcmp(a, empty, 16) != 0);
}

// The response length is 48 + the target-info length, from the temp layout in MS-NLMP 3.3.2:
// 16 NTProofStr + 2 version octets + 6 zeros + 8 time + 8 client challenge + 4 zeros + 4 trailing
// zeros = 48 fixed octets around the blob.
void test_response_length_is_forty_eight_plus_target_info(void)
{
    uint8_t owf[16];
    memset(owf, 0x11, sizeof(owf));
    uint8_t out[256];

    static const size_t TI_LENS[] = {0, 4, 36, 100};
    for (size_t i = 0; i < sizeof(TI_LENS) / sizeof(TI_LENS[0]); i++)
    {
        uint8_t ti[100];
        memset(ti, 0x5A, sizeof(ti));
        NtlmV.v2_response_args.owf = owf;
        NtlmV.v2_response_args.server_challenge = SERVER_CHALLENGE;
        NtlmV.v2_response_args.client_challenge = CLIENT_CHALLENGE;
        NtlmV.v2_response_args.timestamp = ZERO_TIME;
        NtlmV.v2_response_args.target_info = ti;
        NtlmV.v2_response_args.ti_len = TI_LENS[i];
        NtlmV.v2_response_args.out = out;
        NtlmV.v2_response_args.out_cap = sizeof(out);
        NtlmV.v2_response_args.session_key = NULL;
        Ntlm.v2_response(ntlm_work);
        size_t n = NtlmV.n;
        TEST_ASSERT_EQUAL_size_t(48 + TI_LENS[i], n);
    }

    // One octet short of the needed room writes nothing.
    NtlmV.v2_response_args.owf = owf;
    NtlmV.v2_response_args.server_challenge = SERVER_CHALLENGE;
    NtlmV.v2_response_args.client_challenge = CLIENT_CHALLENGE;
    NtlmV.v2_response_args.timestamp = ZERO_TIME;
    NtlmV.v2_response_args.target_info = TARGET_INFO;
    NtlmV.v2_response_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.v2_response_args.out = out;
    NtlmV.v2_response_args.out_cap = 83;
    NtlmV.v2_response_args.session_key = NULL;
    Ntlm.v2_response(ntlm_work);
    TEST_ASSERT_EQUAL_size_t(0, NtlmV.n);
    NtlmV.v2_response_args.owf = owf;
    NtlmV.v2_response_args.server_challenge = SERVER_CHALLENGE;
    NtlmV.v2_response_args.client_challenge = CLIENT_CHALLENGE;
    NtlmV.v2_response_args.timestamp = ZERO_TIME;
    NtlmV.v2_response_args.target_info = TARGET_INFO;
    NtlmV.v2_response_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.v2_response_args.out = out;
    NtlmV.v2_response_args.out_cap = 84;
    NtlmV.v2_response_args.session_key = NULL;
    Ntlm.v2_response(ntlm_work);
    TEST_ASSERT_EQUAL_size_t(84, NtlmV.n);
    NtlmV.v2_response_args.owf = owf;
    NtlmV.v2_response_args.server_challenge = SERVER_CHALLENGE;
    NtlmV.v2_response_args.client_challenge = CLIENT_CHALLENGE;
    NtlmV.v2_response_args.timestamp = ZERO_TIME;
    NtlmV.v2_response_args.target_info = TARGET_INFO;
    NtlmV.v2_response_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.v2_response_args.out = NULL;
    NtlmV.v2_response_args.out_cap = 256;
    NtlmV.v2_response_args.session_key = NULL;
    Ntlm.v2_response(ntlm_work);
    TEST_ASSERT_EQUAL_size_t(0, NtlmV.n);
}

// The timestamp is carried through verbatim as an 8-octet little-endian FILETIME, and it changes
// the NTProofStr: two responses that differ only in their time must not share a proof string.
void test_timestamp_is_carried_and_bound_in(void)
{
    uint8_t owf[16];
    memset(owf, 0x22, sizeof(owf));
    static const uint8_t TIME[8] = {0x00, 0x80, 0x3e, 0xd5, 0xde, 0xb1, 0x9d, 0x01};
    uint8_t out[128];
    uint8_t other[128];

    NtlmV.v2_response_args.owf = owf;
    NtlmV.v2_response_args.server_challenge = SERVER_CHALLENGE;
    NtlmV.v2_response_args.client_challenge = CLIENT_CHALLENGE;
    NtlmV.v2_response_args.timestamp = TIME;
    NtlmV.v2_response_args.target_info = TARGET_INFO;
    NtlmV.v2_response_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.v2_response_args.out = out;
    NtlmV.v2_response_args.out_cap = sizeof(out);
    NtlmV.v2_response_args.session_key = NULL;
    Ntlm.v2_response(ntlm_work);
    TEST_ASSERT_EQUAL_size_t(84, NtlmV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TIME, out + 24, 8); // 16 NTProofStr + 2 + 6 zeros

    NtlmV.v2_response_args.owf = owf;
    NtlmV.v2_response_args.server_challenge = SERVER_CHALLENGE;
    NtlmV.v2_response_args.client_challenge = CLIENT_CHALLENGE;
    NtlmV.v2_response_args.timestamp = ZERO_TIME;
    NtlmV.v2_response_args.target_info = TARGET_INFO;
    NtlmV.v2_response_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.v2_response_args.out = other;
    NtlmV.v2_response_args.out_cap = sizeof(other);
    NtlmV.v2_response_args.session_key = NULL;
    Ntlm.v2_response(ntlm_work);
    TEST_ASSERT_EQUAL_size_t(84, NtlmV.n);
    TEST_ASSERT_TRUE(memcmp(out, other, 16) != 0);
}

// The server challenge is the replay defense, so it must be bound into the proof string: the same
// credentials against a different challenge must produce a different response.
void test_server_challenge_is_bound_into_the_proof(void)
{
    uint8_t nt_hash[16];
    uint8_t owf[16];
    NtlmV.nt_hash_args.password = PASSWORD;
    NtlmV.nt_hash_args.nt_hash = nt_hash;
    Ntlm.nt_hash(ntlm_work);
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = USER;
    NtlmV.ntowfv2_args.domain = DOMAIN_NAME;
    NtlmV.ntowfv2_args.owf = owf;
    Ntlm.ntowfv2(ntlm_work);
    TEST_ASSERT_TRUE(NtlmV.ok);

    static const uint8_t OTHER_CHALLENGE[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xee};
    uint8_t a[128];
    uint8_t b[128];
    NtlmV.v2_response_args.owf = owf;
    NtlmV.v2_response_args.server_challenge = SERVER_CHALLENGE;
    NtlmV.v2_response_args.client_challenge = CLIENT_CHALLENGE;
    NtlmV.v2_response_args.timestamp = ZERO_TIME;
    NtlmV.v2_response_args.target_info = TARGET_INFO;
    NtlmV.v2_response_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.v2_response_args.out = a;
    NtlmV.v2_response_args.out_cap = sizeof(a);
    NtlmV.v2_response_args.session_key = NULL;
    Ntlm.v2_response(ntlm_work);
    NtlmV.v2_response_args.owf = owf;
    NtlmV.v2_response_args.server_challenge = OTHER_CHALLENGE;
    NtlmV.v2_response_args.client_challenge = CLIENT_CHALLENGE;
    NtlmV.v2_response_args.timestamp = ZERO_TIME;
    NtlmV.v2_response_args.target_info = TARGET_INFO;
    NtlmV.v2_response_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.v2_response_args.out = b;
    NtlmV.v2_response_args.out_cap = sizeof(b);
    NtlmV.v2_response_args.session_key = NULL;
    Ntlm.v2_response(ntlm_work);
    TEST_ASSERT_TRUE(memcmp(a, b, 16) != 0);          // the proof strings differ
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a + 16, b + 16, 68); // temp is identical: only the proof moved
}

// MS-NLMP 2.2.2.10: MsvAvFlags is AvId 6 with a 32-bit value, of which "0x00000002: indicates that
// the client is providing message integrity in the MIC field". 2.2.2.1 requires MsvAvEOL (AvId 0)
// to be last, so a new pair is spliced in just before it rather than appended after.
void test_mic_flag_is_inserted_before_the_eol(void)
{
    uint8_t out[64];
    NtlmV.set_mic_flag_args.target_info = TARGET_INFO;
    NtlmV.set_mic_flag_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.set_mic_flag_args.out = out;
    NtlmV.set_mic_flag_args.out_cap = sizeof(out);
    Ntlm.set_mic_flag(ntlm_work);
    size_t n = NtlmV.n;
    TEST_ASSERT_EQUAL_size_t(sizeof(TARGET_INFO) + 8, n);

    // The two original pairs are untouched.
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TARGET_INFO, out, 32);
    // The new pair: AvId 6, AvLen 4, value 0x00000002, all little-endian.
    static const uint8_t PAIR[8] = {0x06, 0x00, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAIR, out + 32, sizeof(PAIR));
    // MsvAvEOL is still last.
    static const uint8_t EOL[4] = {0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(EOL, out + 40, sizeof(EOL));
}

// A list that already carries MsvAvFlags keeps its other bits: 0x00000001 ("account authentication
// is constrained") OR 0x00000002 = 0x00000003, and the list does not grow.
void test_mic_flag_is_ored_into_an_existing_pair(void)
{
    static const uint8_t WITH_FLAGS[20] = {
        0x02, 0x00, 0x04, 0x00, 0x44, 0x00, 0x6f, 0x00, // MsvAvNbDomainName "Do"
        0x06, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, // MsvAvFlags = 0x00000001
        0x00, 0x00, 0x00, 0x00                          // MsvAvEOL
    };
    uint8_t out[64];
    NtlmV.set_mic_flag_args.target_info = WITH_FLAGS;
    NtlmV.set_mic_flag_args.ti_len = sizeof(WITH_FLAGS);
    NtlmV.set_mic_flag_args.out = out;
    NtlmV.set_mic_flag_args.out_cap = sizeof(out);
    Ntlm.set_mic_flag(ntlm_work);
    size_t n = NtlmV.n;
    TEST_ASSERT_EQUAL_size_t(sizeof(WITH_FLAGS), n);
    TEST_ASSERT_EQUAL_HEX8(0x03, out[12]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[13]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[14]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[15]);

    // Setting it twice is the same list: OR-ing a bit already present changes nothing.
    uint8_t again[64];
    NtlmV.set_mic_flag_args.target_info = out;
    NtlmV.set_mic_flag_args.ti_len = n;
    NtlmV.set_mic_flag_args.out = again;
    NtlmV.set_mic_flag_args.out_cap = sizeof(again);
    Ntlm.set_mic_flag(ntlm_work);
    size_t m = NtlmV.n;
    TEST_ASSERT_EQUAL_size_t(n, m);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(out, again, n);
}

// The blob the flag was set on is what the response must be computed over, so setting the flag has
// to change the proof string. It also has to fail closed on a destination too small to hold either
// the copy or the inserted pair.
void test_mic_flag_changes_the_response_and_fails_closed(void)
{
    uint8_t nt_hash[16];
    uint8_t owf[16];
    NtlmV.nt_hash_args.password = PASSWORD;
    NtlmV.nt_hash_args.nt_hash = nt_hash;
    Ntlm.nt_hash(ntlm_work);
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = USER;
    NtlmV.ntowfv2_args.domain = DOMAIN_NAME;
    NtlmV.ntowfv2_args.owf = owf;
    Ntlm.ntowfv2(ntlm_work);
    TEST_ASSERT_TRUE(NtlmV.ok);

    uint8_t flagged[64];
    NtlmV.set_mic_flag_args.target_info = TARGET_INFO;
    NtlmV.set_mic_flag_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.set_mic_flag_args.out = flagged;
    NtlmV.set_mic_flag_args.out_cap = sizeof(flagged);
    Ntlm.set_mic_flag(ntlm_work);
    size_t fl = NtlmV.n;
    TEST_ASSERT_EQUAL_size_t(44, fl);

    uint8_t plain_resp[128];
    uint8_t flagged_resp[128];
    NtlmV.v2_response_args.owf = owf;
    NtlmV.v2_response_args.server_challenge = SERVER_CHALLENGE;
    NtlmV.v2_response_args.client_challenge = CLIENT_CHALLENGE;
    NtlmV.v2_response_args.timestamp = ZERO_TIME;
    NtlmV.v2_response_args.target_info = TARGET_INFO;
    NtlmV.v2_response_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.v2_response_args.out = plain_resp;
    NtlmV.v2_response_args.out_cap = sizeof(plain_resp);
    NtlmV.v2_response_args.session_key = NULL;
    Ntlm.v2_response(ntlm_work);
    NtlmV.v2_response_args.owf = owf;
    NtlmV.v2_response_args.server_challenge = SERVER_CHALLENGE;
    NtlmV.v2_response_args.client_challenge = CLIENT_CHALLENGE;
    NtlmV.v2_response_args.timestamp = ZERO_TIME;
    NtlmV.v2_response_args.target_info = flagged;
    NtlmV.v2_response_args.ti_len = fl;
    NtlmV.v2_response_args.out = flagged_resp;
    NtlmV.v2_response_args.out_cap = sizeof(flagged_resp);
    NtlmV.v2_response_args.session_key = NULL;
    Ntlm.v2_response(ntlm_work);
    TEST_ASSERT_TRUE(memcmp(plain_resp, flagged_resp, 16) != 0);

    uint8_t small[40];
    NtlmV.set_mic_flag_args.target_info = TARGET_INFO;
    NtlmV.set_mic_flag_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.set_mic_flag_args.out = small;
    NtlmV.set_mic_flag_args.out_cap = 35;
    Ntlm.set_mic_flag(ntlm_work);
    TEST_ASSERT_EQUAL_size_t(0, NtlmV.n); // no copy
    NtlmV.set_mic_flag_args.target_info = TARGET_INFO;
    NtlmV.set_mic_flag_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.set_mic_flag_args.out = small;
    NtlmV.set_mic_flag_args.out_cap = 40;
    Ntlm.set_mic_flag(ntlm_work);
    TEST_ASSERT_EQUAL_size_t(0, NtlmV.n); // no pair
    NtlmV.set_mic_flag_args.target_info = NULL;
    NtlmV.set_mic_flag_args.ti_len = 4;
    NtlmV.set_mic_flag_args.out = small;
    NtlmV.set_mic_flag_args.out_cap = sizeof(small);
    Ntlm.set_mic_flag(ntlm_work);
    TEST_ASSERT_EQUAL_size_t(0, NtlmV.n);
    NtlmV.set_mic_flag_args.target_info = TARGET_INFO;
    NtlmV.set_mic_flag_args.ti_len = sizeof(TARGET_INFO);
    NtlmV.set_mic_flag_args.out = NULL;
    NtlmV.set_mic_flag_args.out_cap = 64;
    Ntlm.set_mic_flag(ntlm_work);
    TEST_ASSERT_EQUAL_size_t(0, NtlmV.n);
}

// MS-NLMP 3.1.5.1.2 defines the MIC as HMAC_MD5 over the three messages concatenated. The module
// streams them instead of concatenating, so the check is that streaming a split message reaches the
// digest a single-buffer HMAC-MD5 would. RFC 2202 sec 2 publishes those digests:
//   case 1: key = 0x0b x16, data = "Hi There"      -> 0x9294727a3638bb1c13f48ef8158bfc9d
//   case 3: key = 0xaa x16, data = 0xdd x50        -> 0x56be34521d144c88dbb8c733f0e8b3f6
// Both keys are 16 octets, which is exactly the ExportedSessionKey width this function takes.
void test_mic_matches_the_rfc2202_hmac_md5_vectors(void)
{
    uint8_t key[16];
    uint8_t out[16];

    memset(key, 0x0b, sizeof(key));
    NtlmV.mic_args.session_key = key;
    NtlmV.mic_args.neg = (const uint8_t *)"Hi ";
    NtlmV.mic_args.neg_len = 3;
    NtlmV.mic_args.chal = (const uint8_t *)"Th";
    NtlmV.mic_args.chal_len = 2;
    NtlmV.mic_args.auth = (const uint8_t *)"ere";
    NtlmV.mic_args.auth_len = 3;
    NtlmV.mic_args.out = out;
    Ntlm.mic(ntlm_work);
    static const uint8_t WANT1[16] = {0x92, 0x94, 0x72, 0x7a, 0x36, 0x38, 0xbb, 0x1c,
                                      0x13, 0xf4, 0x8e, 0xf8, 0x15, 0x8b, 0xfc, 0x9d};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT1, out, sizeof(WANT1));

    // The same message split differently must give the same digest: the split is not part of it.
    NtlmV.mic_args.session_key = key;
    NtlmV.mic_args.neg = (const uint8_t *)"H";
    NtlmV.mic_args.neg_len = 1;
    NtlmV.mic_args.chal = (const uint8_t *)"i Ther";
    NtlmV.mic_args.chal_len = 6;
    NtlmV.mic_args.auth = (const uint8_t *)"e";
    NtlmV.mic_args.auth_len = 1;
    NtlmV.mic_args.out = out;
    Ntlm.mic(ntlm_work);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT1, out, sizeof(WANT1));

    uint8_t data[50];
    memset(key, 0xaa, sizeof(key));
    memset(data, 0xdd, sizeof(data));
    NtlmV.mic_args.session_key = key;
    NtlmV.mic_args.neg = data;
    NtlmV.mic_args.neg_len = 20;
    NtlmV.mic_args.chal = data;
    NtlmV.mic_args.chal_len = 20;
    NtlmV.mic_args.auth = data;
    NtlmV.mic_args.auth_len = 10;
    NtlmV.mic_args.out = out;
    Ntlm.mic(ntlm_work);
    static const uint8_t WANT3[16] = {0x56, 0xbe, 0x34, 0x52, 0x1d, 0x14, 0x4c, 0x88,
                                      0xdb, 0xb8, 0xc7, 0x33, 0xf0, 0xe8, 0xb3, 0xf6};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT3, out, sizeof(WANT3));
}

// The MIC is keyed by the session key, so the same three messages under two keys must not share a
// digest, and a changed AUTHENTICATE_MESSAGE must not keep its old MIC.
void test_mic_binds_the_key_and_every_message(void)
{
    uint8_t k1[16];
    uint8_t k2[16];
    memset(k1, 0x01, sizeof(k1));
    memset(k2, 0x02, sizeof(k2));

    uint8_t a[16];
    uint8_t b[16];
    NtlmV.mic_args.session_key = k1;
    NtlmV.mic_args.neg = (const uint8_t *)"neg";
    NtlmV.mic_args.neg_len = 3;
    NtlmV.mic_args.chal = (const uint8_t *)"chal";
    NtlmV.mic_args.chal_len = 4;
    NtlmV.mic_args.auth = (const uint8_t *)"auth";
    NtlmV.mic_args.auth_len = 4;
    NtlmV.mic_args.out = a;
    Ntlm.mic(ntlm_work);
    NtlmV.mic_args.session_key = k2;
    NtlmV.mic_args.neg = (const uint8_t *)"neg";
    NtlmV.mic_args.neg_len = 3;
    NtlmV.mic_args.chal = (const uint8_t *)"chal";
    NtlmV.mic_args.chal_len = 4;
    NtlmV.mic_args.auth = (const uint8_t *)"auth";
    NtlmV.mic_args.auth_len = 4;
    NtlmV.mic_args.out = b;
    Ntlm.mic(ntlm_work);
    TEST_ASSERT_TRUE(memcmp(a, b, 16) != 0);

    NtlmV.mic_args.session_key = k1;
    NtlmV.mic_args.neg = (const uint8_t *)"neg";
    NtlmV.mic_args.neg_len = 3;
    NtlmV.mic_args.chal = (const uint8_t *)"chal";
    NtlmV.mic_args.chal_len = 4;
    NtlmV.mic_args.auth = (const uint8_t *)"autH";
    NtlmV.mic_args.auth_len = 4;
    NtlmV.mic_args.out = b;
    Ntlm.mic(ntlm_work);
    TEST_ASSERT_TRUE(memcmp(a, b, 16) != 0);

    NtlmV.mic_args.session_key = k1;
    NtlmV.mic_args.neg = (const uint8_t *)"Neg";
    NtlmV.mic_args.neg_len = 3;
    NtlmV.mic_args.chal = (const uint8_t *)"chal";
    NtlmV.mic_args.chal_len = 4;
    NtlmV.mic_args.auth = (const uint8_t *)"auth";
    NtlmV.mic_args.auth_len = 4;
    NtlmV.mic_args.out = b;
    Ntlm.mic(ntlm_work);
    TEST_ASSERT_TRUE(memcmp(a, b, 16) != 0);
}

// The scratch that widens Uppercase(User) + domain to UTF-16LE holds 256 characters, so a longer
// pair is refused rather than silently truncated into a key both sides would compute differently.
void test_ntowfv2_refuses_an_oversized_name_pair(void)
{
    uint8_t nt_hash[16];
    NtlmV.nt_hash_args.password = PASSWORD;
    NtlmV.nt_hash_args.nt_hash = nt_hash;
    Ntlm.nt_hash(ntlm_work);

    char long_user[300];
    memset(long_user, 'a', sizeof(long_user) - 1);
    long_user[sizeof(long_user) - 1] = '\0';

    uint8_t owf[16];
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = long_user;
    NtlmV.ntowfv2_args.domain = DOMAIN_NAME;
    NtlmV.ntowfv2_args.owf = owf;
    Ntlm.ntowfv2(ntlm_work);
    TEST_ASSERT_FALSE(NtlmV.ok);

    char at_limit[257];
    memset(at_limit, 'a', 256);
    at_limit[256] = '\0';
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = at_limit;
    NtlmV.ntowfv2_args.domain = "";
    NtlmV.ntowfv2_args.owf = owf;
    Ntlm.ntowfv2(ntlm_work);
    TEST_ASSERT_TRUE(NtlmV.ok); // exactly 256 chars fits
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = at_limit;
    NtlmV.ntowfv2_args.domain = "x";
    NtlmV.ntowfv2_args.owf = owf;
    Ntlm.ntowfv2(ntlm_work);
    TEST_ASSERT_FALSE(NtlmV.ok);
}
