// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SPNEGO DER wrapping of the NTLMSSP tokens
// (network_drivers/application/smb/spnego.h).
//
// Three specs meet here. RFC 2743 sec 3.1 fixes the InitialContextToken framing, [APPLICATION 0]
// over a mechanism OID followed by the inner token. RFC 4178 sec 4.2 gives the NegotiationToken
// CHOICE, negTokenInit [0] and negTokenResp [1], and sec 4.2 names the SPNEGO OID 1.3.6.1.5.5.2.
// X.690 gives the DER encoding those ASN.1 types reduce to.
//
// test_first_token_is_an_rfc2743_initial_context_token is the load-bearing case: it checks the
// whole first SESSION_SETUP security buffer octet for octet, including both OIDs encoded by X.690
// clause 8.19's rules, which are derived by hand in the comment rather than copied from anywhere.

#include "network_drivers/application/smb/spnego.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// X.690 clause 8.19: an OID's first two arcs X.Y collapse to the single octet 40*X + Y, and every
// later arc is base 128, high bit set on all but the last octet.
//   1.3.6.1.5.5.2       -> 40*1+3 = 43 = 0x2b, then 06 01 05 05 02             = 6 content octets
//   1.3.6.1.4.1.311.2.2.10 -> 0x2b, 06 01 04 01, then 311 = 2*128 + 55 -> 0x82 0x37,
//                          then 02 02 0a                                       = 10 content octets
// Each is wrapped as tag 0x06 (OBJECT IDENTIFIER) plus its length.
static const uint8_t SPNEGO_OID_TLV[8] = {0x06, 0x06, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x02};
static const uint8_t NTLM_OID_TLV[12] = {0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0a};

// RFC 2743 sec 3.1's InitialContextToken over RFC 4178 sec 4.2.1's NegTokenInit, with a four-octet
// mechToken standing in for the NTLMSSP NEGOTIATE. Sizes, innermost outward:
//   OCTET STRING(4)                 04 04 <4>                          = 6
//   [2] mechToken                   a2 06 <6>                          = 8
//   SEQUENCE OF { NTLM OID }        30 0c <12>                         = 14
//   [0] mechTypes                   a0 0e <14>                         = 16
//   SEQUENCE (NegTokenInit)         30 18 <16+8=24>                    = 26
//   [0] negTokenInit                a0 1a <26>                         = 28
//   [APPLICATION 0]                 60 24 <8 (SPNEGO OID) + 28 = 36>   = 38
void test_first_token_is_an_rfc2743_initial_context_token(void)
{
    static const uint8_t NTLM[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t out[64];
    size_t n = protocore_spnego_wrap_negotiate(NTLM, sizeof(NTLM), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(38, n);

    static const uint8_t WANT[38] = {0x60, 0x24,                                     // [APPLICATION 0], 36 octets
                                     0x06, 0x06, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x02, // thisMech = SPNEGO OID
                                     0xa0, 0x1a,                                     // [0] negTokenInit, 26 octets
                                     0x30, 0x18,                                     // SEQUENCE, 24 octets
                                     0xa0, 0x0e,                                     // [0] mechTypes, 14 octets
                                     0x30, 0x0c,                                     // SEQUENCE OF, 12 octets
                                     0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0a, // NTLM OID
                                     0xa2, 0x06, // [2] mechToken, 6 octets
                                     0x04, 0x04, // OCTET STRING, 4 octets
                                     0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));
}

// RFC 4178 sec 4.2: "Subsequent tokens MUST NOT be encapsulated in this GSS-API generic token
// framing", so the second client token is a bare negTokenResp [1] wrapping a SEQUENCE with only the
// responseToken [2] present. Sizes for a four-octet token:
//   OCTET STRING(4)   04 04 <4>   = 6    SEQUENCE        30 08 <8>   = 10
//   [2] responseToken a2 06 <6>   = 8    [1] NegTokenResp a1 0a <10> = 12
void test_second_token_is_a_bare_neg_token_resp(void)
{
    static const uint8_t NTLM[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t out[64];
    size_t n = protocore_spnego_wrap_authenticate(NTLM, sizeof(NTLM), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(12, n);

    static const uint8_t WANT[12] = {0xa1, 0x0a, 0x30, 0x08, 0xa2, 0x06, 0x04, 0x04, 0x01, 0x02, 0x03, 0x04};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    // No InitialContextToken framing: the first octet is the [1] tag, not [APPLICATION 0].
    TEST_ASSERT_EQUAL_HEX8(0xa1, out[0]);
}

// The server's reply is a NegTokenResp too, and RFC 4178 sec 4.2.2 makes negState [0] and
// supportedMech [1] present before responseToken [2] in the first reply from the target. The walk
// has to step over those to find the token rather than assuming it comes first.
void test_response_token_is_found_after_negstate_and_supportedmech(void)
{
    static const uint8_t NTLM[6] = {'N', 'T', 'L', 'M', 0x02, 0x00};
    // SEQUENCE content: [0] negState 5 + [1] supportedMech 14 + [2] responseToken 10 = 29 octets,
    // so the SEQUENCE is 31 octets and the [1] NegTokenResp wrapping it is 33.
    static const uint8_t BLOB[33] = {0xa1, 0x1f,                   // [1] NegTokenResp, 31
                                     0x30, 0x1d,                   // SEQUENCE, 29
                                     0xa0, 0x03, 0x0a, 0x01, 0x01, // [0] ENUMERATED 1
                                     0xa1, 0x0c, 0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82,
                                     0x37, 0x02, 0x02, 0x0a,                                      // [1] NTLM OID
                                     0xa2, 0x08, 0x04, 0x06, 'N',  'T',  'L',  'M',  0x02, 0x00}; // [2] responseToken

    const uint8_t *tok = NULL;
    size_t tok_len = 0;
    TEST_ASSERT_TRUE(protocore_spnego_parse_response(BLOB, sizeof(BLOB), &tok, &tok_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(NTLM), tok_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NTLM, tok, sizeof(NTLM));
    TEST_ASSERT_EQUAL_PTR(BLOB + 27, tok); // a pointer INTO the blob, not a copy
}

// Wrapping then unwrapping must return the same octets: the AUTHENTICATE wrapper and the response
// parser are the two halves of the same NegTokenResp shape.
void test_wrap_then_parse_round_trip(void)
{
    for (size_t len = 1; len <= 300; len += 37)
    {
        uint8_t ntlm[300];
        for (size_t i = 0; i < len; i++)
        {
            ntlm[i] = (uint8_t)(i * 7 + 1);
        }
        uint8_t wrapped[512];
        size_t n = protocore_spnego_wrap_authenticate(ntlm, len, wrapped, sizeof(wrapped));
        TEST_ASSERT_TRUE(n > len);

        const uint8_t *tok = NULL;
        size_t tok_len = 0;
        TEST_ASSERT_TRUE(protocore_spnego_parse_response(wrapped, n, &tok, &tok_len));
        TEST_ASSERT_EQUAL_size_t(len, tok_len);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(ntlm, tok, len);
    }
}

// X.690 clause 8.1.3: a length under 128 is the single short-form octet, and 128 or more takes the
// long form, an octet counting the length octets that follow. So a 127-octet content is 1 length
// octet, 128 takes 0x81 plus one, and 256 takes 0x82 plus two. Each step changes the whole token's
// size by exactly the octets the nested headers grew by.
void test_der_length_forms(void)
{
    uint8_t ntlm[400];
    memset(ntlm, 0x5A, sizeof(ntlm));
    uint8_t out[512];

    // 127-octet token: OCTET STRING 04 7f <127> = 129; [2] a2 81 81 <129> = 132;
    // SEQUENCE 30 81 84 <132> = 135; [1] a1 81 87 <135> = 138.
    size_t n = protocore_spnego_wrap_authenticate(ntlm, 127, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(138, n);
    TEST_ASSERT_EQUAL_HEX8(0xa1, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x81, out[1]);
    TEST_ASSERT_EQUAL_HEX8(135, out[2]);

    // 300-octet token: OCTET STRING 04 82 01 2c <300> = 304; [2] a2 82 01 30 <304> = 308;
    // SEQUENCE 30 82 01 34 <308> = 312; [1] a1 82 01 38 <312> = 316.
    n = protocore_spnego_wrap_authenticate(ntlm, 300, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(316, n);
    TEST_ASSERT_EQUAL_HEX8(0xa1, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x82, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x38, out[3]);

    // A short content still uses the short form at every level.
    n = protocore_spnego_wrap_authenticate(ntlm, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(9, n); // 04 01 <1> = 3, a2 03 = 5, 30 05 = 7, a1 07 = 9
    TEST_ASSERT_EQUAL_HEX8(0x07, out[1]);
}

// The server's blob is untrusted DER. Every shape that is not a NegTokenResp carrying a responseToken
// OCTET STRING has to be a refusal, not a pointer into whatever the length octets happened to name.
void test_parse_response_fails_closed(void)
{
    const uint8_t *tok = (const uint8_t *)"sentinel";
    size_t tok_len = 12345;

    // Wrong outer tag: an InitialContextToken where a NegTokenResp belongs.
    static const uint8_t WRONG_OUTER[12] = {0x60, 0x0a, 0x30, 0x08, 0xa2, 0x06, 0x04, 0x04, 1, 2, 3, 4};
    TEST_ASSERT_FALSE(protocore_spnego_parse_response(WRONG_OUTER, sizeof(WRONG_OUTER), &tok, &tok_len));

    // NegTokenResp whose content is not a SEQUENCE.
    static const uint8_t NOT_SEQ[6] = {0xa1, 0x04, 0x04, 0x02, 0xAA, 0xBB};
    TEST_ASSERT_FALSE(protocore_spnego_parse_response(NOT_SEQ, sizeof(NOT_SEQ), &tok, &tok_len));

    // SEQUENCE with no [2] field at all: only a negState.
    static const uint8_t NO_TOKEN[9] = {0xa1, 0x07, 0x30, 0x05, 0xa0, 0x03, 0x0a, 0x01, 0x00};
    TEST_ASSERT_FALSE(protocore_spnego_parse_response(NO_TOKEN, sizeof(NO_TOKEN), &tok, &tok_len));

    // [2] present but wrapping something that is not an OCTET STRING.
    static const uint8_t NOT_OCTET[10] = {0xa1, 0x08, 0x30, 0x06, 0xa2, 0x04, 0x02, 0x02, 0xAA, 0xBB};
    TEST_ASSERT_FALSE(protocore_spnego_parse_response(NOT_OCTET, sizeof(NOT_OCTET), &tok, &tok_len));

    // A length that runs past the buffer.
    static const uint8_t OVER[8] = {0xa1, 0x7f, 0x30, 0x08, 0xa2, 0x06, 0x04, 0x04};
    TEST_ASSERT_FALSE(protocore_spnego_parse_response(OVER, sizeof(OVER), &tok, &tok_len));

    // An inner OCTET STRING longer than the [2] that contains it.
    static const uint8_t INNER_OVER[12] = {0xa1, 0x0a, 0x30, 0x08, 0xa2, 0x06, 0x04, 0x40, 1, 2, 3, 4};
    TEST_ASSERT_FALSE(protocore_spnego_parse_response(INNER_OVER, sizeof(INNER_OVER), &tok, &tok_len));

    // X.690 clause 8.1.3.6 indefinite length (0x80) is not DER: this parser takes definite only.
    static const uint8_t INDEFINITE[8] = {0xa1, 0x80, 0x30, 0x08, 0xa2, 0x06, 0x04, 0x04};
    TEST_ASSERT_FALSE(protocore_spnego_parse_response(INDEFINITE, sizeof(INDEFINITE), &tok, &tok_len));

    // A long form claiming more length octets than a size_t token could need.
    static const uint8_t HUGE_LEN[10] = {0xa1, 0x85, 1, 2, 3, 4, 5, 0x30, 0x00, 0x00};
    TEST_ASSERT_FALSE(protocore_spnego_parse_response(HUGE_LEN, sizeof(HUGE_LEN), &tok, &tok_len));

    // Truncated to nothing.
    static const uint8_t ONE[1] = {0xa1};
    TEST_ASSERT_FALSE(protocore_spnego_parse_response(ONE, sizeof(ONE), &tok, &tok_len));
    TEST_ASSERT_FALSE(protocore_spnego_parse_response(ONE, 0, &tok, &tok_len));

    TEST_ASSERT_FALSE(protocore_spnego_parse_response(NULL, 12, &tok, &tok_len));
    TEST_ASSERT_FALSE(protocore_spnego_parse_response(ONE, 1, NULL, &tok_len));
    TEST_ASSERT_FALSE(protocore_spnego_parse_response(ONE, 1, &tok, NULL));

    // Nothing above touched the caller's outputs.
    TEST_ASSERT_EQUAL_size_t(12345, tok_len);
}

// A destination that cannot hold the whole token gets nothing: a truncated DER token is a token the
// server would parse as a different structure.
void test_wrappers_fail_closed(void)
{
    static const uint8_t NTLM[4] = {1, 2, 3, 4};
    uint8_t out[64];

    TEST_ASSERT_EQUAL_size_t(0, protocore_spnego_wrap_negotiate(NTLM, sizeof(NTLM), out, 37));
    TEST_ASSERT_EQUAL_size_t(38, protocore_spnego_wrap_negotiate(NTLM, sizeof(NTLM), out, 38));
    TEST_ASSERT_EQUAL_size_t(0, protocore_spnego_wrap_negotiate(NULL, sizeof(NTLM), out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, protocore_spnego_wrap_negotiate(NTLM, sizeof(NTLM), NULL, sizeof(out)));

    TEST_ASSERT_EQUAL_size_t(0, protocore_spnego_wrap_authenticate(NTLM, sizeof(NTLM), out, 11));
    TEST_ASSERT_EQUAL_size_t(12, protocore_spnego_wrap_authenticate(NTLM, sizeof(NTLM), out, 12));
    TEST_ASSERT_EQUAL_size_t(0, protocore_spnego_wrap_authenticate(NULL, sizeof(NTLM), out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, protocore_spnego_wrap_authenticate(NTLM, sizeof(NTLM), NULL, sizeof(out)));
}

// The two OIDs are what tell the server which mechanism is being negotiated, so both must appear in
// the first token exactly as X.690 encodes them.
void test_both_oids_appear_in_the_first_token(void)
{
    static const uint8_t NTLM[2] = {0xAA, 0xBB};
    uint8_t out[64];
    size_t n = protocore_spnego_wrap_negotiate(NTLM, sizeof(NTLM), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(36, n);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(SPNEGO_OID_TLV, out + 2, sizeof(SPNEGO_OID_TLV));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NTLM_OID_TLV, out + 18, sizeof(NTLM_OID_TLV));
}
