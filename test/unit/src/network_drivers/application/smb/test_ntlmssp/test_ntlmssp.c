// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NTLMSSP message codec (network_drivers/application/smb/ntlmssp.h).
//
// MS-NLMP section 4.2 prints complete NTLMSSP messages octet for octet: 4.2.4.3 gives the
// CHALLENGE_MESSAGE and the AUTHENTICATE_MESSAGE of the NTLMv2 example, and 4.2.2.3 / 4.2.3.3 give
// the NTLMv1 ones. Those dumps are the oracle here, so the field offsets, the little-endian
// Len/MaxLen/BufferOffset triplets and the UTF-16LE identity strings are all checked against bytes
// Microsoft published rather than against what this encoder happens to emit.
//
// test_msnlmp_challenge_message is the load-bearing case: the CHALLENGE is the only message this
// module parses, it comes from the network, and everything downstream (the server challenge, the
// AV_PAIR blob the NTLMv2 response is computed over) is read out of it at offsets that dump fixes.

#include "network_drivers/application/smb/ntlmssp.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// MS-NLMP 4.2.4.3, the CHALLENGE_MESSAGE, transcribed from its hex dump.
static const uint8_t CHALLENGE[104] = {
    0x4e, 0x54, 0x4c, 0x4d, 0x53, 0x53, 0x50, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x0c, 0x00, // 0x00
    0x38, 0x00, 0x00, 0x00, 0x33, 0x82, 0x8a, 0xe2, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, // 0x10
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00, 0x24, 0x00, 0x44, 0x00, 0x00, 0x00, // 0x20
    0x06, 0x00, 0x70, 0x17, 0x00, 0x00, 0x00, 0x0f, 0x53, 0x00, 0x65, 0x00, 0x72, 0x00, 0x76, 0x00, // 0x30
    0x65, 0x00, 0x72, 0x00, 0x02, 0x00, 0x0c, 0x00, 0x44, 0x00, 0x6f, 0x00, 0x6d, 0x00, 0x61, 0x00, // 0x40
    0x69, 0x00, 0x6e, 0x00, 0x01, 0x00, 0x0c, 0x00, 0x53, 0x00, 0x65, 0x00, 0x72, 0x00, 0x76, 0x00, // 0x50
    0x65, 0x00, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00                                                  // 0x60
};

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// MS-NLMP 2.2.1.2 lays the CHALLENGE_MESSAGE out as Signature(8) @0, MessageType(4) @8,
// TargetNameFields(8) @12, NegotiateFlags(4) @20, ServerChallenge(8) @24, Reserved(8) @32,
// TargetInfoFields(8) @40, Version(8) @48, Payload @56. Reading 4.2.4.3's dump at those offsets:
//   @0  "NTLMSSP\0"                          @20 33 82 8a e2   NegotiateFlags
//   @8  02 00 00 00  MessageType = 2         @24 01 23 45 67 89 ab cd ef   ServerChallenge
//   @40 24 00 24 00 44 00 00 00              TargetInfo: Len 0x24 = 36, at offset 0x44 = 68
void test_msnlmp_challenge_message(void)
{
    NtlmChallenge c;
    memset(&c, 0, sizeof(c));
    TEST_ASSERT_TRUE(protocore_ntlmssp_parse_challenge(CHALLENGE, sizeof(CHALLENGE), &c));

    static const uint8_t WANT_CHALLENGE[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_CHALLENGE, c.server_challenge, sizeof(WANT_CHALLENGE));
    TEST_ASSERT_EQUAL_HEX32(0xe28a8233u, c.flags);
    TEST_ASSERT_EQUAL_UINT16(36, c.target_info_len);
    TEST_ASSERT_EQUAL_PTR(CHALLENGE + 68, c.target_info);

    // The blob at offset 68 is the AV_PAIR list of 4.2.4.3: MsvAvNbDomainName "Domain",
    // MsvAvNbComputerName "Server", MsvAvEOL.
    static const uint8_t WANT_TI[36] = {0x02, 0x00, 0x0c, 0x00, 0x44, 0x00, 0x6f, 0x00, 0x6d, 0x00, 0x61, 0x00,
                                        0x69, 0x00, 0x6e, 0x00, 0x01, 0x00, 0x0c, 0x00, 0x53, 0x00, 0x65, 0x00,
                                        0x72, 0x00, 0x76, 0x00, 0x65, 0x00, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_TI, c.target_info, sizeof(WANT_TI));
}

// MS-NLMP 2.2.1.1: the NEGOTIATE_MESSAGE is Signature(8), MessageType(4) = 1, NegotiateFlags(4),
// DomainNameFields(8), WorkstationFields(8) - 32 octets when both name fields are empty. A field
// triplet is Len(2), MaxLen(2), BufferOffset(4), all little-endian, and an absent name is Len 0
// with the offset pointing at where the payload would start.
void test_negotiate_message_layout(void)
{
    uint8_t buf[64];
    memset(buf, 0xEE, sizeof(buf));
    size_t n = protocore_ntlmssp_build_negotiate(buf, sizeof(buf), NTLMSSP_CLIENT_DEFAULT_FLAGS);
    TEST_ASSERT_EQUAL_size_t(32, n);

    TEST_ASSERT_EQUAL_HEX8_ARRAY("NTLMSSP\0", buf, 8);
    TEST_ASSERT_EQUAL_UINT32(1, le32(buf + 8));
    TEST_ASSERT_EQUAL_HEX32(NTLMSSP_CLIENT_DEFAULT_FLAGS, le32(buf + 12));

    TEST_ASSERT_EQUAL_UINT16(0, le16(buf + 16));  // DomainName Len
    TEST_ASSERT_EQUAL_UINT16(0, le16(buf + 18));  // DomainName MaxLen
    TEST_ASSERT_EQUAL_UINT32(32, le32(buf + 20)); // DomainName BufferOffset
    TEST_ASSERT_EQUAL_UINT16(0, le16(buf + 24));
    TEST_ASSERT_EQUAL_UINT16(0, le16(buf + 26));
    TEST_ASSERT_EQUAL_UINT32(32, le32(buf + 28));

    TEST_ASSERT_EQUAL_HEX8(0xEE, buf[32]); // nothing past the fixed part
    TEST_ASSERT_EQUAL_size_t(0, protocore_ntlmssp_build_negotiate(buf, 31, 0));
    TEST_ASSERT_EQUAL_size_t(32, protocore_ntlmssp_build_negotiate(buf, 32, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ntlmssp_build_negotiate(NULL, 64, 0));
}

// MS-NLMP 2.2.2.5 assigns the NegotiateFlags bits. The default set a v2 client offers is
// Unicode | RequestTarget | NTLM | AlwaysSign | ExtendedSessionSecurity, which is 0x00088205 by
// adding those bits: 0x00000001 + 0x00000004 + 0x00000200 + 0x00008000 + 0x00080000.
void test_negotiate_flag_bits(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, NTLMSSP_NEGOTIATE_UNICODE);
    TEST_ASSERT_EQUAL_HEX32(0x00000004u, NTLMSSP_REQUEST_TARGET);
    TEST_ASSERT_EQUAL_HEX32(0x00000200u, NTLMSSP_NEGOTIATE_NTLM);
    TEST_ASSERT_EQUAL_HEX32(0x00008000u, NTLMSSP_NEGOTIATE_ALWAYS_SIGN);
    TEST_ASSERT_EQUAL_HEX32(0x00080000u, NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY);
    TEST_ASSERT_EQUAL_HEX32(0x00800000u, NTLMSSP_NEGOTIATE_TARGET_INFO);
    TEST_ASSERT_EQUAL_HEX32(0x02000000u, NTLMSSP_NEGOTIATE_VERSION);
    TEST_ASSERT_EQUAL_HEX32(0x20000000u, NTLMSSP_NEGOTIATE_128);
    TEST_ASSERT_EQUAL_HEX32(0x80000000u, NTLMSSP_NEGOTIATE_56);

    TEST_ASSERT_EQUAL_HEX32(0x00088205u, NTLMSSP_CLIENT_DEFAULT_FLAGS);

    // The example server's flags (0xe28a8233) carry Unicode, NTLM, ExtendedSessionSecurity and
    // TargetInfo, which is what makes an NTLMv2 exchange possible at all.
    static const uint32_t SERVER_FLAGS = 0xe28a8233u;
    TEST_ASSERT_TRUE((SERVER_FLAGS & NTLMSSP_NEGOTIATE_UNICODE) != 0);
    TEST_ASSERT_TRUE((SERVER_FLAGS & NTLMSSP_NEGOTIATE_NTLM) != 0);
    TEST_ASSERT_TRUE((SERVER_FLAGS & NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY) != 0);
    TEST_ASSERT_TRUE((SERVER_FLAGS & NTLMSSP_NEGOTIATE_TARGET_INFO) != 0);
}

// Read the field triplet at @p at and check that it locates @p want inside the message: Len and
// MaxLen both equal the value's length and BufferOffset points at the value. MS-NLMP 2.2.1.3 makes
// each field self-locating this way, so the payload order is the sender's choice and only the
// triplet has to agree with it.
static void assert_field(const uint8_t *msg, size_t msg_len, size_t at, const uint8_t *want, size_t want_len)
{
    TEST_ASSERT_EQUAL_UINT16(want_len, le16(msg + at));
    TEST_ASSERT_EQUAL_UINT16(want_len, le16(msg + at + 2));
    uint32_t off = le32(msg + at + 4);
    TEST_ASSERT_TRUE((size_t)off + want_len <= msg_len);
    if (want_len)
    {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want, msg + off, want_len);
    }
}

// MS-NLMP 2.2.1.3 lays the AUTHENTICATE_MESSAGE out as Signature(8) @0, MessageType(4) = 3 @8, then
// the LmChallengeResponse @12, NtChallengeResponse @20, DomainName @28, UserName @36, Workstation
// @44 and EncryptedRandomSessionKey @52 field triplets, NegotiateFlags @60, and the payload after.
// The values below are 4.2.4.3's own: its 24-octet LmChallengeResponse, its 84-octet
// NtChallengeResponse, "Domain", "User" and "COMPUTER" in UTF-16LE, and its NegotiateFlags.
void test_msnlmp_authenticate_message(void)
{
    static const uint8_t LM_RESP[24] = {0x86, 0xc3, 0x50, 0x97, 0xac, 0x9c, 0xec, 0x10, 0x25, 0x54, 0x76, 0x4a,
                                        0x57, 0xcc, 0xcc, 0x19, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
    static const uint8_t NT_RESP[84] = {
        0x68, 0xcd, 0x0a, 0xb8, 0x51, 0xe5, 0x1c, 0x96, 0xaa, 0xbc, 0x92, 0x7b, 0xeb, 0xef, 0x6a, 0x1c, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0c, 0x00, 0x44, 0x00, 0x6f, 0x00, 0x6d, 0x00,
        0x61, 0x00, 0x69, 0x00, 0x6e, 0x00, 0x01, 0x00, 0x0c, 0x00, 0x53, 0x00, 0x65, 0x00, 0x72, 0x00, 0x76, 0x00,
        0x65, 0x00, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // MS-NLMP 4.2.1 Common Values, UTF-16LE: "Domain", "User" and the workstation "COMPUTER".
    static const uint8_t DOMAIN_U16[12] = {0x44, 0x00, 0x6f, 0x00, 0x6d, 0x00, 0x61, 0x00, 0x69, 0x00, 0x6e, 0x00};
    static const uint8_t USER_U16[8] = {0x55, 0x00, 0x73, 0x00, 0x65, 0x00, 0x72, 0x00};
    static const uint8_t COMPUTER_U16[16] = {0x43, 0x00, 0x4f, 0x00, 0x4d, 0x00, 0x50, 0x00,
                                             0x55, 0x00, 0x54, 0x00, 0x45, 0x00, 0x52, 0x00};

    uint8_t buf[512];
    size_t n = protocore_ntlmssp_build_authenticate(buf, sizeof(buf), LM_RESP, sizeof(LM_RESP), NT_RESP,
                                                    sizeof(NT_RESP), "Domain", "User", "COMPUTER", 0xe2888235u,
                                                    PROTO_FALSE);
    // 64 fixed + 24 LM + 84 NT + 12 "Domain" + 8 "User" + 16 "COMPUTER" = 208, no key exchange.
    TEST_ASSERT_EQUAL_size_t(208, n);

    TEST_ASSERT_EQUAL_HEX8_ARRAY("NTLMSSP\0", buf, 8);
    TEST_ASSERT_EQUAL_UINT32(3, le32(buf + 8));
    TEST_ASSERT_EQUAL_HEX32(0xe2888235u, le32(buf + 60));

    assert_field(buf, n, 12, LM_RESP, sizeof(LM_RESP));
    assert_field(buf, n, 20, NT_RESP, sizeof(NT_RESP));
    assert_field(buf, n, 28, DOMAIN_U16, sizeof(DOMAIN_U16));
    assert_field(buf, n, 36, USER_U16, sizeof(USER_U16));
    assert_field(buf, n, 44, COMPUTER_U16, sizeof(COMPUTER_U16));
    assert_field(buf, n, 52, NULL, 0); // EncryptedRandomSessionKey: none negotiated

    // Every payload lands past the fixed part and inside the message.
    for (size_t at = 12; at <= 52; at += 8)
    {
        uint32_t off = le32(buf + at + 4);
        TEST_ASSERT_TRUE(off >= 64);
        TEST_ASSERT_TRUE((size_t)off + le16(buf + at) <= n);
    }
}

// MS-NLMP 2.2.1.3: with a MIC the fixed part grows by the 8-octet Version and the 16-octet MIC, so
// the payload starts at 88 instead of 64. 3.1.5.1.2 requires the MIC field to be zero while the
// digest over the three messages is taken, and 2.2.2.10's MsvAvFlags bit tells the server it is
// there - which the caller can only honor if NTLMSSP_NEGOTIATE_VERSION is set as well.
void test_authenticate_with_mic_reserves_version_and_mic(void)
{
    static const uint8_t NT_RESP[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t buf[256];
    memset(buf, 0xEE, sizeof(buf));
    size_t n = protocore_ntlmssp_build_authenticate(buf, sizeof(buf), NULL, 0, NT_RESP, sizeof(NT_RESP), "D", "U", NULL,
                                                    NTLMSSP_CLIENT_DEFAULT_FLAGS, PROTO_TRUE);
    // 88 fixed + 0 LM + 8 NT + 2 "D" + 2 "U" + 0 workstation = 100
    TEST_ASSERT_EQUAL_size_t(100, n);

    // Every payload now starts past the 88-octet fixed part.
    assert_field(buf, n, 20, NT_RESP, sizeof(NT_RESP));
    for (size_t at = 12; at <= 52; at += 8)
    {
        TEST_ASSERT_TRUE(le32(buf + at + 4) >= 88);
    }

    TEST_ASSERT_EQUAL_UINT(72u, PROTOCORE_NTLMSSP_MIC_OFFSET);
    TEST_ASSERT_EQUAL_UINT(16u, PROTOCORE_NTLMSSP_MIC_LEN);
    for (size_t i = 0; i < PROTOCORE_NTLMSSP_MIC_LEN; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, buf[PROTOCORE_NTLMSSP_MIC_OFFSET + i]);
    }

    // The Version field is present, so the flag that says so must be set whatever the caller passed.
    TEST_ASSERT_TRUE((le32(buf + 60) & NTLMSSP_NEGOTIATE_VERSION) != 0);

    // Without a MIC the fixed part is 64 and neither field is reserved.
    n = protocore_ntlmssp_build_authenticate(buf, sizeof(buf), NULL, 0, NT_RESP, sizeof(NT_RESP), "D", "U", NULL,
                                             NTLMSSP_CLIENT_DEFAULT_FLAGS, PROTO_FALSE);
    TEST_ASSERT_EQUAL_size_t(76, n); // 64 + 8 + 2 + 2
    assert_field(buf, n, 20, NT_RESP, sizeof(NT_RESP));
    for (size_t at = 12; at <= 52; at += 8)
    {
        TEST_ASSERT_TRUE(le32(buf + at + 4) >= 64);
    }
    TEST_ASSERT_TRUE((le32(buf + 60) & NTLMSSP_NEGOTIATE_VERSION) == 0);
}

// The CHALLENGE arrives from the network, so every way it can be wrong has to be a refusal: a bad
// signature, the wrong MessageType, a message too short to hold the fixed fields, and a
// TargetInfoFields triplet that points outside the message.
void test_challenge_parse_fails_closed(void)
{
    NtlmChallenge c;
    uint8_t bad[sizeof(CHALLENGE)];

    memcpy(bad, CHALLENGE, sizeof(bad));
    bad[0] = 'X'; // signature
    TEST_ASSERT_FALSE(protocore_ntlmssp_parse_challenge(bad, sizeof(bad), &c));

    memcpy(bad, CHALLENGE, sizeof(bad));
    bad[8] = 3; // MessageType 3 is an AUTHENTICATE, not a CHALLENGE
    TEST_ASSERT_FALSE(protocore_ntlmssp_parse_challenge(bad, sizeof(bad), &c));

    // Shorter than the fixed fields through TargetInfoFields (48 octets), and short of what the
    // TargetInfoFields triplet says the message carries (68 + 36 = 104).
    TEST_ASSERT_FALSE(protocore_ntlmssp_parse_challenge(CHALLENGE, 47, &c));
    TEST_ASSERT_FALSE(protocore_ntlmssp_parse_challenge(CHALLENGE, 48, &c));
    TEST_ASSERT_FALSE(protocore_ntlmssp_parse_challenge(CHALLENGE, sizeof(CHALLENGE) - 1, &c));
    TEST_ASSERT_TRUE(protocore_ntlmssp_parse_challenge(CHALLENGE, sizeof(CHALLENGE), &c));

    // A target-info offset past the end of the message.
    memcpy(bad, CHALLENGE, sizeof(bad));
    bad[44] = 0xFF;
    bad[45] = 0xFF;
    TEST_ASSERT_FALSE(protocore_ntlmssp_parse_challenge(bad, sizeof(bad), &c));

    // A target-info length that runs past the end.
    memcpy(bad, CHALLENGE, sizeof(bad));
    bad[40] = 0xFF;
    bad[41] = 0x00;
    TEST_ASSERT_FALSE(protocore_ntlmssp_parse_challenge(bad, sizeof(bad), &c));

    TEST_ASSERT_FALSE(protocore_ntlmssp_parse_challenge(NULL, sizeof(CHALLENGE), &c));
    TEST_ASSERT_FALSE(protocore_ntlmssp_parse_challenge(CHALLENGE, sizeof(CHALLENGE), NULL));
}

// A CHALLENGE with TargetInfoLen 0 is legal - the server simply offered no AV_PAIRs - and must
// report an absent blob rather than a pointer into the message.
void test_challenge_without_target_info(void)
{
    uint8_t msg[56];
    memset(msg, 0, sizeof(msg));
    memcpy(msg, "NTLMSSP\0", 8);
    msg[8] = 2;
    msg[24] = 0xDE;
    msg[31] = 0xAD;

    NtlmChallenge c;
    c.target_info = CHALLENGE;
    c.target_info_len = 99;
    TEST_ASSERT_TRUE(protocore_ntlmssp_parse_challenge(msg, sizeof(msg), &c));
    TEST_ASSERT_NULL(c.target_info);
    TEST_ASSERT_EQUAL_UINT16(0, c.target_info_len);
    TEST_ASSERT_EQUAL_HEX8(0xDE, c.server_challenge[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAD, c.server_challenge[7]);
}

// Nothing is written when the destination cannot hold the whole AUTHENTICATE: a message whose
// declared field offsets point past its own end is worse than no message.
void test_authenticate_fails_closed(void)
{
    static const uint8_t NT_RESP[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t buf[256];
    // 64 + 8 + 2 + 2 = 76 with no MIC.
    TEST_ASSERT_EQUAL_size_t(0, protocore_ntlmssp_build_authenticate(buf, 75, NULL, 0, NT_RESP, sizeof(NT_RESP), "D",
                                                                     "U", NULL, 0, PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(76, protocore_ntlmssp_build_authenticate(buf, 76, NULL, 0, NT_RESP, sizeof(NT_RESP), "D",
                                                                      "U", NULL, 0, PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ntlmssp_build_authenticate(NULL, 256, NULL, 0, NT_RESP, sizeof(NT_RESP), "D",
                                                                     "U", NULL, 0, PROTO_FALSE));

    // With a MIC the fixed part alone is 88, so anything under that cannot hold the message.
    TEST_ASSERT_EQUAL_size_t(0, protocore_ntlmssp_build_authenticate(buf, 87, NULL, 0, NULL, 0, NULL, NULL, NULL, 0,
                                                                     PROTO_TRUE));
    TEST_ASSERT_EQUAL_size_t(88, protocore_ntlmssp_build_authenticate(buf, 88, NULL, 0, NULL, 0, NULL, NULL, NULL, 0,
                                                                      PROTO_TRUE));
}

// A null identity string is an absent field, not an empty one written somewhere unexpected: its
// triplet reports length 0 at the offset the payload had reached.
void test_absent_identity_fields(void)
{
    uint8_t buf[256];
    size_t n = protocore_ntlmssp_build_authenticate(buf, sizeof(buf), NULL, 0, NULL, 0, NULL, NULL, NULL, 0,
                                                    PROTO_FALSE);
    TEST_ASSERT_EQUAL_size_t(64, n);
    for (size_t at = 12; at <= 52; at += 8)
    {
        TEST_ASSERT_EQUAL_UINT16(0, le16(buf + at));
        TEST_ASSERT_EQUAL_UINT16(0, le16(buf + at + 2));
        TEST_ASSERT_EQUAL_UINT32(64, le32(buf + at + 4));
    }
}
