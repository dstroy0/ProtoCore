// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the NTLMSSP message codec (network_drivers/application/smb/ntlmssp, MS-NLMP 2.2.1): the
// NEGOTIATE builder, the CHALLENGE parser, and the AUTHENTICATE builder. Plus an
// end-to-end that ties the codec to the NTLMv2 response (ntlm.h) using the MS-NLMP 4.2
// worked example.

#include "network_drivers/application/smb/ntlm.h"
#include "network_drivers/application/smb/ntlmssp.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static uint16_t r16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t r32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void w16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void w32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
    {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}
static size_t unhex(const char *h, uint8_t *out)
{
    size_t n = 0;
    for (; h[0] && h[1]; h += 2)
    {
        auto nib = [](char x) -> int { return x <= '9' ? x - '0' : (x | 0x20) - 'a' + 10; };
        out[n++] = (uint8_t)((nib(h[0]) << 4) | nib(h[1]));
    }
    return n;
}

static const uint8_t SIG[8] = {'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};

void test_build_negotiate()
{
    uint8_t buf[64];
    size_t n = pc_ntlmssp_build_negotiate(buf, sizeof(buf), NtlmsspFlags::NTLMSSP_CLIENT_DEFAULT_FLAGS);
    TEST_ASSERT_EQUAL_size_t(32, n);
    TEST_ASSERT_EQUAL_MEMORY(SIG, buf, 8);
    TEST_ASSERT_EQUAL_UINT32(1, r32(buf + 8)); // MessageType NEGOTIATE
    TEST_ASSERT_EQUAL_UINT32(NtlmsspFlags::NTLMSSP_CLIENT_DEFAULT_FLAGS, r32(buf + 12));
    TEST_ASSERT_EQUAL_size_t(0, pc_ntlmssp_build_negotiate(buf, 16, 0)); // overflow
}

// Build a CHALLENGE message with the given server challenge + target info at offset 48.
static size_t build_challenge(uint8_t *m, const uint8_t sc[8], const uint8_t *ti, uint16_t ti_len)
{
    memset(m, 0, 48);
    memcpy(m, SIG, 8);
    w32(m + 8, 2); // MessageType CHALLENGE
    w32(m + 20, NtlmsspFlags::NTLMSSP_NEGOTIATE_UNICODE | NtlmsspFlags::NTLMSSP_NEGOTIATE_NTLM |
                    NtlmsspFlags::NTLMSSP_NEGOTIATE_TARGET_INFO);
    memcpy(m + 24, sc, 8);
    w16(m + 40, ti_len); // TargetInfoLen
    w16(m + 42, ti_len);
    w32(m + 44, 48); // TargetInfoBufferOffset
    memcpy(m + 48, ti, ti_len);
    return 48 + ti_len;
}

void test_parse_challenge()
{
    uint8_t sc[8];
    unhex("0123456789abcdef", sc);
    uint8_t ti[64];
    size_t ti_len = unhex("02000c0044006f006d00610069006e0001000c0053006500720076006500720000000000", ti);
    uint8_t m[128];
    size_t n = build_challenge(m, sc, ti, (uint16_t)ti_len);

    NtlmChallenge ch;
    TEST_ASSERT_TRUE(pc_ntlmssp_parse_challenge(m, n, &ch));
    TEST_ASSERT_EQUAL_MEMORY(sc, ch.server_challenge, 8);
    TEST_ASSERT_EQUAL_UINT16(ti_len, ch.target_info_len);
    TEST_ASSERT_EQUAL_MEMORY(ti, ch.target_info, ti_len);
    TEST_ASSERT_TRUE((ch.flags & NtlmsspFlags::NTLMSSP_NEGOTIATE_TARGET_INFO) != 0);
}

void test_parse_challenge_rejects()
{
    uint8_t sc[8] = {0};
    uint8_t ti[4] = {0};
    uint8_t m[128], bad[128];
    size_t n = build_challenge(m, sc, ti, 4);
    NtlmChallenge ch;

    memcpy(bad, m, n);
    bad[0] = 'X'; // bad signature
    TEST_ASSERT_FALSE(pc_ntlmssp_parse_challenge(bad, n, &ch));
    memcpy(bad, m, n);
    w32(bad + 8, 3); // wrong MessageType
    TEST_ASSERT_FALSE(pc_ntlmssp_parse_challenge(bad, n, &ch));
    memcpy(bad, m, n);
    w16(bad + 40, 9000); // target info length past the message
    TEST_ASSERT_FALSE(pc_ntlmssp_parse_challenge(bad, n, &ch));
    TEST_ASSERT_FALSE(pc_ntlmssp_parse_challenge(m, 40, &ch)); // truncated
}

void test_build_authenticate()
{
    uint8_t nt[48];
    memset(nt, 0xEE, sizeof(nt));
    uint8_t buf[256];
    size_t n =
        pc_ntlmssp_build_authenticate(buf, sizeof(buf), nullptr, 0, nt, sizeof(nt), "Domain", "User", "WS", 0x12345678);
    TEST_ASSERT_GREATER_THAN_size_t(64, n);
    TEST_ASSERT_EQUAL_MEMORY(SIG, buf, 8);
    TEST_ASSERT_EQUAL_UINT32(3, r32(buf + 8)); // AUTHENTICATE
    TEST_ASSERT_EQUAL_UINT32(0x12345678, r32(buf + 60));

    // NtChallengeResponseFields (@20) must point at our nt response
    uint16_t nt_field_len = r16(buf + 20);
    uint32_t nt_field_off = r32(buf + 24);
    TEST_ASSERT_EQUAL_UINT16(48, nt_field_len);
    TEST_ASSERT_TRUE(nt_field_off + nt_field_len <= n);
    TEST_ASSERT_EQUAL_MEMORY(nt, buf + nt_field_off, 48);

    // UserNameFields (@36): "User" UTF-16LE = 8 bytes
    uint16_t u_len = r16(buf + 36);
    uint32_t u_off = r32(buf + 40);
    TEST_ASSERT_EQUAL_UINT16(8, u_len);
    const uint8_t user16[8] = {'U', 0, 's', 0, 'e', 0, 'r', 0};
    TEST_ASSERT_EQUAL_MEMORY(user16, buf + u_off, 8);

    TEST_ASSERT_EQUAL_size_t(0, pc_ntlmssp_build_authenticate(buf, 80, nullptr, 0, nt, sizeof(nt), "Domain", "User",
                                                              nullptr, 0)); // overflow
}

// with_mic=true: the AUTHENTICATE reserves an 8-byte Version + 16-byte MIC before the payload, sets
// NTLMSSP_NEGOTIATE_VERSION, zeroes the MIC at PC_NTLMSSP_MIC_OFFSET, and shifts all field offsets to 88.
void test_build_authenticate_with_mic()
{
    uint8_t nt[48];
    for (int i = 0; i < 48; i++)
    {
        nt[i] = (uint8_t)(0x40 + i);
    }
    uint8_t buf[256];
    size_t n = pc_ntlmssp_build_authenticate(buf, sizeof(buf), nullptr, 0, nt, sizeof(nt), "Domain", "User", "WS",
                                             0x00000001, /*with_mic=*/true);
    TEST_ASSERT_GREATER_THAN_size_t(88, n);
    // NTLMSSP_NEGOTIATE_VERSION (0x02000000) OR'd into the flags word by the builder.
    TEST_ASSERT_EQUAL_UINT32(0x00000001u | NtlmsspFlags::NTLMSSP_NEGOTIATE_VERSION, r32(buf + 60));
    // Version at offset 64 (a plausible build), MIC zeroed at offset 72.
    TEST_ASSERT_EQUAL_HEX8(6, buf[64]);
    TEST_ASSERT_EQUAL_size_t(72, PC_NTLMSSP_MIC_OFFSET);
    const uint8_t zero_mic[16] = {0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero_mic, buf + PC_NTLMSSP_MIC_OFFSET, 16);
    // The NtChallengeResponse now lives at or after offset 88 (past the fixed header + Version + MIC).
    uint32_t nt_off = r32(buf + 24);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(88, nt_off);
    TEST_ASSERT_EQUAL_MEMORY(nt, buf + nt_off, 48);
}

// End to end (MS-NLMP 4.2): parse a CHALLENGE, compute the NTLMv2 response, embed it in an
// AUTHENTICATE, and confirm the NT response field carries the expected NtChallengeResponse.
void test_end_to_end()
{
    uint8_t sc[8], cli[8], ti[64], time[8] = {0};
    unhex("0123456789abcdef", sc);
    unhex("aaaaaaaaaaaaaaaa", cli);
    size_t ti_len = unhex("02000c0044006f006d00610069006e0001000c0053006500720076006500720000000000", ti);

    uint8_t chal[128];
    size_t cn = build_challenge(chal, sc, ti, (uint16_t)ti_len);
    NtlmChallenge ch;
    TEST_ASSERT_TRUE(pc_ntlmssp_parse_challenge(chal, cn, &ch));

    uint8_t nt_hash[16], owf[16];
    pc_ntlm_nt_hash("Password", nt_hash);
    pc_ntlm_ntowfv2(nt_hash, "User", "Domain", owf);
    uint8_t nt_resp[256], skey[16];
    size_t nt_len = pc_ntlm_v2_response(owf, ch.server_challenge, cli, time, ch.target_info, ch.target_info_len,
                                        nt_resp, sizeof(nt_resp), skey);
    TEST_ASSERT_GREATER_THAN_size_t(0, nt_len);

    uint8_t auth[512];
    size_t an = pc_ntlmssp_build_authenticate(auth, sizeof(auth), nullptr, 0, nt_resp, nt_len, "Domain", "User",
                                              nullptr, ch.flags);
    TEST_ASSERT_GREATER_THAN_size_t(0, an);

    uint32_t nt_off = r32(auth + 24);
    // the embedded NtChallengeResponse starts with the MS-NLMP 4.2 NTProofStr
    uint8_t ntproof[16];
    unhex("68cd0ab851e51c96aabc927bebef6a1c", ntproof);
    TEST_ASSERT_EQUAL_MEMORY(ntproof, auth + nt_off, 16);
}

// The NEGOTIATE builder fails closed on a null buffer (the !buf side of its guard; the existing
// coverage only drives the cap side).
void test_build_negotiate_null_buf()
{
    TEST_ASSERT_EQUAL_size_t(0, pc_ntlmssp_build_negotiate(nullptr, 64, NtlmsspFlags::NTLMSSP_CLIENT_DEFAULT_FLAGS));
}

// The CHALLENGE parser rejects a null message and a null output struct (the !msg / !out sides of
// its guard; the existing coverage only drives the len side).
void test_parse_challenge_null_args()
{
    uint8_t sc[8] = {0};
    uint8_t ti[4] = {0};
    uint8_t m[128];
    size_t n = build_challenge(m, sc, ti, 4);
    NtlmChallenge ch;
    TEST_ASSERT_FALSE(pc_ntlmssp_parse_challenge(nullptr, n, &ch));
    TEST_ASSERT_FALSE(pc_ntlmssp_parse_challenge(m, n, nullptr));
    TEST_ASSERT_TRUE(pc_ntlmssp_parse_challenge(m, n, &ch)); // the same message is otherwise fine
}

// A CHALLENGE that advertises no target info parses, and reports an empty (null) blob rather than
// pointing into the message.
void test_parse_challenge_no_target_info()
{
    uint8_t sc[8];
    unhex("0011223344556677", sc);
    uint8_t ti[4] = {0};
    uint8_t m[128];
    size_t n = build_challenge(m, sc, ti, 0); // TargetInfoLen 0
    TEST_ASSERT_EQUAL_size_t(48, n);

    NtlmChallenge ch;
    memset(&ch, 0xAA, sizeof(ch));
    TEST_ASSERT_TRUE(pc_ntlmssp_parse_challenge(m, n, &ch));
    TEST_ASSERT_NULL(ch.target_info);
    TEST_ASSERT_EQUAL_UINT16(0, ch.target_info_len);
    TEST_ASSERT_EQUAL_MEMORY(sc, ch.server_challenge, 8);
}

// The AUTHENTICATE builder fails closed on a null buffer (the !buf side of its guard; the existing
// coverage only drives the total > cap side).
void test_build_authenticate_null_buf()
{
    uint8_t nt[16];
    memset(nt, 0x11, sizeof(nt));
    TEST_ASSERT_EQUAL_size_t(
        0, pc_ntlmssp_build_authenticate(nullptr, 256, nullptr, 0, nt, sizeof(nt), "Dom", "Usr", "Wks", 0));
}

// An LM response is laid out ahead of the NT response and its field triplet points at it. The SMB
// client always sends NTLMv2 only, so this is the one path that takes the lm_resp guard's true side.
void test_build_authenticate_with_lm()
{
    uint8_t lm[24], nt[24];
    memset(lm, 0x5A, sizeof(lm));
    memset(nt, 0xA5, sizeof(nt));
    uint8_t buf[256];
    size_t n = pc_ntlmssp_build_authenticate(buf, sizeof(buf), lm, sizeof(lm), nt, sizeof(nt), "Dom", "Usr", "Wks",
                                             0x11223344);
    TEST_ASSERT_EQUAL_size_t(64 + 24 + 24 + 6 + 6 + 6, n);

    TEST_ASSERT_EQUAL_UINT16(24, r16(buf + 12)); // LmChallengeResponseLen
    TEST_ASSERT_EQUAL_UINT32(64, r32(buf + 16)); // ...Offset: first thing after the fixed header
    TEST_ASSERT_EQUAL_MEMORY(lm, buf + 64, 24);

    TEST_ASSERT_EQUAL_UINT16(24, r16(buf + 20)); // NtChallengeResponseLen
    TEST_ASSERT_EQUAL_UINT32(88, r32(buf + 24)); // ...Offset: straight after the LM response
    TEST_ASSERT_EQUAL_MEMORY(nt, buf + 88, 24);
    TEST_ASSERT_EQUAL_UINT32(0x11223344, r32(buf + 60));
}

// A non-null response pointer paired with a zero length copies nothing, and a null pointer is
// skipped outright: the remaining sides of the LM and NT payload guards.
void test_build_authenticate_empty_responses()
{
    uint8_t resp[8];
    memset(resp, 0x77, sizeof(resp));
    uint8_t buf[256];

    // lm_resp non-null but lm_len 0, and nt_resp null: neither payload is written
    memset(buf, 0, sizeof(buf));
    size_t n = pc_ntlmssp_build_authenticate(buf, sizeof(buf), resp, 0, nullptr, 0, "D", "U", "W", 0x0BADF00D);
    TEST_ASSERT_EQUAL_size_t(64 + 2 + 2 + 2, n); // header + "D"/"U"/"W" UTF-16LE, no responses
    TEST_ASSERT_EQUAL_UINT16(0, r16(buf + 12));  // LmChallengeResponseLen
    TEST_ASSERT_EQUAL_UINT16(0, r16(buf + 20));  // NtChallengeResponseLen
    TEST_ASSERT_EQUAL_UINT32(64, r32(buf + 16)); // both point at the empty payload start
    TEST_ASSERT_EQUAL_UINT32(64, r32(buf + 24));
    TEST_ASSERT_EQUAL_UINT32(0x0BADF00D, r32(buf + 60));

    // nt_resp non-null but nt_len 0: the NT payload is still empty, so the names start at 64
    memset(buf, 0, sizeof(buf));
    n = pc_ntlmssp_build_authenticate(buf, sizeof(buf), nullptr, 0, resp, 0, "D", "U", "W", 0);
    TEST_ASSERT_EQUAL_size_t(64 + 2 + 2 + 2, n);
    TEST_ASSERT_EQUAL_UINT16(0, r16(buf + 20));  // NtChallengeResponseLen
    TEST_ASSERT_EQUAL_UINT16(2, r16(buf + 28));  // DomainNameLen
    TEST_ASSERT_EQUAL_UINT32(64, r32(buf + 32)); // DomainNameOffset
    const uint8_t dom16[2] = {'D', 0};
    TEST_ASSERT_EQUAL_MEMORY(dom16, buf + 64, 2); // 0x77 was never copied over the name area
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_negotiate);
    RUN_TEST(test_parse_challenge);
    RUN_TEST(test_parse_challenge_rejects);
    RUN_TEST(test_build_authenticate);
    RUN_TEST(test_end_to_end);
    RUN_TEST(test_build_negotiate_null_buf);
    RUN_TEST(test_parse_challenge_null_args);
    RUN_TEST(test_parse_challenge_no_target_info);
    RUN_TEST(test_build_authenticate_null_buf);
    RUN_TEST(test_build_authenticate_with_lm);
    RUN_TEST(test_build_authenticate_empty_responses);
    RUN_TEST(test_build_authenticate_with_mic);
    return UNITY_END();
}
