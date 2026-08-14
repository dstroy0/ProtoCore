// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the TLS version + cipher-suite policy (server/security/tls_policy/tls_policy.h).
//
// Every number below is a code point the standards publish. RFC 5246 sec A.1 fixes TLS 1.2 as
// ProtocolVersion { 3, 3 } and RFC 8446 sec 4.2.1 fixes TLS 1.3 as 0x0304; the suite identifiers
// come from RFC 8446 App. B.4 (0x1301..0x1303), RFC 5289 sec 3.2 (0xC02B / 0xC02C / 0xC02F /
// 0xC030), RFC 7905 sec 2 (0xCCA8 / 0xCCA9) and RFC 5246 App. A.5 (the legacy CBC / 3DES / RC4
// suites). test_the_published_version_words is the load-bearing case: a policy that pins the wrong
// version word negotiates a version neither peer meant, and every other case here would still pass.
//
// The AEAD classifier is the module's documented allowlist of the GCM and ChaCha20-Poly1305 suites,
// not a general "is this suite AEAD" predicate, so the RFC 8446 CCM suites 0x1304 / 0x1305 are left
// out of the assertions rather than pinned to an answer the header does not promise.

#include "server/security/tls_policy/tls_policy.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 5246 sec A.1: "The version of the protocol being employed ... { 3, 3 }" for TLS 1.2.
// RFC 8446 sec 4.2.1: TLS 1.3 is identified in supported_versions by 0x0304.
void test_the_published_version_words(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0303, TLS_VERSION_1_2);
    TEST_ASSERT_EQUAL_HEX16(0x0304, TLS_VERSION_1_3);
    TEST_ASSERT_TRUE(TLS_VERSION_1_3 > TLS_VERSION_1_2); // the words order as the versions do
}

// A server picks the highest version it supports that is not above what the client offered.
void test_negotiation_picks_the_highest_common_version(void)
{
    // both peers can do 1.3
    TEST_ASSERT_EQUAL_HEX16(TLS_VERSION_1_3,
                            protocore_tls_negotiate_version(TLS_VERSION_1_3, TLS_VERSION_1_2, TLS_VERSION_1_3));
    // the client tops out at 1.2, so 1.2 it is
    TEST_ASSERT_EQUAL_HEX16(TLS_VERSION_1_2,
                            protocore_tls_negotiate_version(TLS_VERSION_1_2, TLS_VERSION_1_2, TLS_VERSION_1_3));
    // the server is pinned to 1.3 only and the client offers 1.3
    TEST_ASSERT_EQUAL_HEX16(TLS_VERSION_1_3,
                            protocore_tls_negotiate_version(TLS_VERSION_1_3, TLS_VERSION_1_3, TLS_VERSION_1_3));
    // the server is pinned to 1.2 only, so a 1.3 client is answered with 1.2
    TEST_ASSERT_EQUAL_HEX16(TLS_VERSION_1_2,
                            protocore_tls_negotiate_version(TLS_VERSION_1_3, TLS_VERSION_1_2, TLS_VERSION_1_2));
}

// A client offering a version above everything the server knows is answered with the server's
// ceiling, never with the client's word: a server must not claim a version it cannot speak.
void test_a_future_client_gets_the_server_ceiling(void)
{
    TEST_ASSERT_EQUAL_HEX16(TLS_VERSION_1_3,
                            protocore_tls_negotiate_version(0x0305, TLS_VERSION_1_2, TLS_VERSION_1_3));
    TEST_ASSERT_EQUAL_HEX16(TLS_VERSION_1_2,
                            protocore_tls_negotiate_version(0x03FF, TLS_VERSION_1_2, TLS_VERSION_1_2));
}

// A client below the server's floor gets no version at all. The floor is the whole point of the
// policy: TLS 1.1 (0x0302), TLS 1.0 (0x0301) and SSL 3.0 (0x0300) must not negotiate.
void test_a_client_below_the_floor_is_refused(void)
{
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_negotiate_version(0x0302, TLS_VERSION_1_2, TLS_VERSION_1_3));
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_negotiate_version(0x0301, TLS_VERSION_1_2, TLS_VERSION_1_3));
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_negotiate_version(0x0300, TLS_VERSION_1_2, TLS_VERSION_1_3));
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_negotiate_version(0x0000, TLS_VERSION_1_2, TLS_VERSION_1_3));
    // a 1.2 client against a 1.3-only server has no overlap either
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_negotiate_version(TLS_VERSION_1_2, TLS_VERSION_1_3, TLS_VERSION_1_3));
}

// An inverted server range names no versions at all, so nothing negotiates out of it.
void test_an_inverted_server_range_negotiates_nothing(void)
{
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_negotiate_version(TLS_VERSION_1_3, TLS_VERSION_1_3, TLS_VERSION_1_2));
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_negotiate_version(TLS_VERSION_1_2, TLS_VERSION_1_3, TLS_VERSION_1_2));
}

// Whatever is negotiated is inside the server's range and not above the client's offer.
void test_the_negotiated_version_is_always_inside_both_ranges(void)
{
    for (uint16_t client = 0x0300; client <= 0x0308; client++)
    {
        for (uint16_t lo = TLS_VERSION_1_2; lo <= TLS_VERSION_1_3; lo++)
        {
            for (uint16_t hi = TLS_VERSION_1_2; hi <= TLS_VERSION_1_3; hi++)
            {
                uint16_t got = protocore_tls_negotiate_version(client, lo, hi);
                if (got == 0)
                {
                    continue;
                }
                TEST_ASSERT_TRUE(got >= lo);
                TEST_ASSERT_TRUE(got <= hi);
                TEST_ASSERT_TRUE(got <= client);
            }
        }
    }
}

// The names a status endpoint prints, and the fallback for anything else.
void test_version_names(void)
{
    TEST_ASSERT_EQUAL_STRING("TLS 1.2", protocore_tls_version_name(TLS_VERSION_1_2));
    TEST_ASSERT_EQUAL_STRING("TLS 1.3", protocore_tls_version_name(TLS_VERSION_1_3));
    TEST_ASSERT_EQUAL_STRING("unknown", protocore_tls_version_name(0x0302)); // TLS 1.1
    TEST_ASSERT_EQUAL_STRING("unknown", protocore_tls_version_name(0x0301)); // TLS 1.0
    TEST_ASSERT_EQUAL_STRING("unknown", protocore_tls_version_name(0x0000));
    TEST_ASSERT_EQUAL_STRING("unknown", protocore_tls_version_name(0xFFFF));
}

// RFC 8446 sec 4.1.1 and RFC 5246 sec 7.4.1.2: the SERVER chooses the suite from what the client
// offered. The pinned list is in preference order, so the first pinned suite the client also
// offered wins whatever order the client listed them in.
void test_selection_follows_server_preference_not_client_order(void)
{
    static const uint16_t PINNED[3] = {0x1301, 0xC02F, 0xCCA8}; // AES-128-GCM, then RSA-GCM, then ChaCha
    static const uint16_t CLIENT_A[3] = {0xCCA8, 0xC02F, 0x1301};
    static const uint16_t CLIENT_B[3] = {0x1301, 0xC02F, 0xCCA8};

    // the client's own order is ignored: the server's first pinned suite wins both times
    TEST_ASSERT_EQUAL_HEX16(0x1301, protocore_tls_select_cipher(CLIENT_A, 3, PINNED, 3));
    TEST_ASSERT_EQUAL_HEX16(0x1301, protocore_tls_select_cipher(CLIENT_B, 3, PINNED, 3));

    // drop the server's favourite from the client's list and the second preference is taken
    static const uint16_t CLIENT_C[2] = {0xCCA8, 0xC02F};
    TEST_ASSERT_EQUAL_HEX16(0xC02F, protocore_tls_select_cipher(CLIENT_C, 2, PINNED, 3));

    // and only the third is left
    static const uint16_t CLIENT_D[1] = {0xCCA8};
    TEST_ASSERT_EQUAL_HEX16(0xCCA8, protocore_tls_select_cipher(CLIENT_D, 1, PINNED, 3));
}

// A client offering nothing on the pinned list gets no suite: the handshake fails rather than
// falling back to something the operator did not audit.
void test_no_overlap_selects_nothing(void)
{
    static const uint16_t PINNED[2] = {0x1301, 0x1302};
    static const uint16_t LEGACY[3] = {0x002F, 0x000A, 0x0005}; // AES-CBC-SHA, 3DES-CBC-SHA, RC4-SHA
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_select_cipher(LEGACY, 3, PINNED, 2));

    // an empty list on either side is no overlap
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_select_cipher(LEGACY, 0, PINNED, 2));
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_select_cipher(LEGACY, 3, PINNED, 0));
}

// A null list is refused rather than dereferenced.
void test_selection_refuses_a_null_list(void)
{
    static const uint16_t PINNED[1] = {0x1301};
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_select_cipher(NULL, 1, PINNED, 1));
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_select_cipher(PINNED, 1, NULL, 1));
    TEST_ASSERT_EQUAL_HEX16(0, protocore_tls_select_cipher(NULL, 0, NULL, 0));
}

// Whatever is selected was both pinned and offered, over every subset of a small suite set.
void test_a_selected_suite_was_always_both_pinned_and_offered(void)
{
    static const uint16_t ALL[4] = {0x1301, 0x1302, 0xC02F, 0x002F};

    for (unsigned cm = 0; cm < 16u; cm++)
    {
        uint16_t client[4];
        size_t nc = 0;
        for (unsigned b = 0; b < 4u; b++)
        {
            if (cm & (1u << b))
            {
                client[nc++] = ALL[b];
            }
        }
        for (unsigned sm = 0; sm < 16u; sm++)
        {
            uint16_t server[4];
            size_t ns = 0;
            for (unsigned b = 0; b < 4u; b++)
            {
                if (sm & (1u << b))
                {
                    server[ns++] = ALL[b];
                }
            }
            uint16_t got = protocore_tls_select_cipher(client, nc, server, ns);
            if (got == 0)
            {
                TEST_ASSERT_EQUAL_UINT(0u, (unsigned)(cm & sm)); // nothing was in both lists
                continue;
            }
            proto_bool in_client = PROTO_FALSE;
            proto_bool in_server = PROTO_FALSE;
            for (size_t i = 0; i < nc; i++)
            {
                in_client = in_client || (client[i] == got);
            }
            for (size_t i = 0; i < ns; i++)
            {
                in_server = in_server || (server[i] == got);
            }
            TEST_ASSERT_TRUE(in_client);
            TEST_ASSERT_TRUE(in_server);
        }
    }
}

// The three TLS 1.3 suites this policy pins (RFC 8446 App. B.4) and the six TLS 1.2 ECDHE AEAD
// suites (RFC 5289 sec 3.2, RFC 7905 sec 2) classify as AEAD.
void test_the_aead_suites(void)
{
    TEST_ASSERT_TRUE(protocore_tls_is_aead(0x1301)); // TLS_AES_128_GCM_SHA256
    TEST_ASSERT_TRUE(protocore_tls_is_aead(0x1302)); // TLS_AES_256_GCM_SHA384
    TEST_ASSERT_TRUE(protocore_tls_is_aead(0x1303)); // TLS_CHACHA20_POLY1305_SHA256
    TEST_ASSERT_TRUE(protocore_tls_is_aead(0xC02B)); // TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256
    TEST_ASSERT_TRUE(protocore_tls_is_aead(0xC02C)); // TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384
    TEST_ASSERT_TRUE(protocore_tls_is_aead(0xC02F)); // TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
    TEST_ASSERT_TRUE(protocore_tls_is_aead(0xC030)); // TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384
    TEST_ASSERT_TRUE(protocore_tls_is_aead(0xCCA8)); // TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256
    TEST_ASSERT_TRUE(protocore_tls_is_aead(0xCCA9)); // TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256
}

// The block-cipher and stream-cipher suites RFC 5246 App. A.5 lists are not AEAD, and neither is
// the null suite. A classifier that answered true for these is what a hardened profile is meant to
// exclude.
void test_the_non_aead_suites(void)
{
    TEST_ASSERT_FALSE(protocore_tls_is_aead(0x0000)); // TLS_NULL_WITH_NULL_NULL
    TEST_ASSERT_FALSE(protocore_tls_is_aead(0x0005)); // TLS_RSA_WITH_RC4_128_SHA
    TEST_ASSERT_FALSE(protocore_tls_is_aead(0x000A)); // TLS_RSA_WITH_3DES_EDE_CBC_SHA
    TEST_ASSERT_FALSE(protocore_tls_is_aead(0x002F)); // TLS_RSA_WITH_AES_128_CBC_SHA
    TEST_ASSERT_FALSE(protocore_tls_is_aead(0x0035)); // TLS_RSA_WITH_AES_256_CBC_SHA
    TEST_ASSERT_FALSE(protocore_tls_is_aead(0x003C)); // TLS_RSA_WITH_AES_128_CBC_SHA256
    TEST_ASSERT_FALSE(protocore_tls_is_aead(0xC013)); // TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA
    TEST_ASSERT_FALSE(protocore_tls_is_aead(0xC027)); // TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256
    // a code point next to a pinned one is not pinned by proximity
    TEST_ASSERT_FALSE(protocore_tls_is_aead(0xC02E));
    TEST_ASSERT_FALSE(protocore_tls_is_aead(0xC031));
    TEST_ASSERT_FALSE(protocore_tls_is_aead(0x1300));
    TEST_ASSERT_FALSE(protocore_tls_is_aead(0xFFFF));
}

// A suite selected from a pinned list of AEAD-only suites is itself AEAD, which is what makes the
// pinned list the hardened profile it claims to be.
void test_an_aead_only_pin_can_only_select_aead(void)
{
    static const uint16_t PINNED[4] = {0x1301, 0x1302, 0x1303, 0xC02F};
    static const uint16_t CLIENT[6] = {0x002F, 0x000A, 0xC013, 0xC02F, 0x1303, 0x0005};

    uint16_t got = protocore_tls_select_cipher(CLIENT, 6, PINNED, 4);
    TEST_ASSERT_EQUAL_HEX16(0x1303, got); // the first pinned suite the client offered
    TEST_ASSERT_TRUE(protocore_tls_is_aead(got));
}
