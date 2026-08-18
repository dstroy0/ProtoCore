// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IKEv2 message and payload codec (services/security/ikev2/ikev2.h).
//
// RFC 7296 sec 3.1 Figure 4 fixes the 28-octet IKE header field by field, and sec 3.2 fixes the
// generic payload header that every payload begins with. test_rfc7296_ike_header_layout is the
// load-bearing case: every expected octet in it is read straight off that figure and the field list
// beneath it, so a wrong offset or a little-endian integer cannot pass. The typed payloads below
// come from sec 3.3 through 3.15 the same way, and the Curve25519 key exchange (Diffie-Hellman Group
// Num 31, RFC 8031 sec 3) is checked against RFC 7748 sec 6.1's published X25519 test vector.

#include "services/security/ikev2/ikev2.h"
#include <string.h>

#include <unity.h>

static uint8_t ikev2_work[16]; // the borrow an entry takes; Ike never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_work[PROTOCORE_IKE_BORROW];
static uint8_t g_out[1024];

static const uint8_t SPI_I[8] = {0x92, 0x1F, 0x4E, 0x77, 0x9B, 0x28, 0xC1, 0x03};
static const uint8_t SPI_R[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// RFC 7296 sec 3.1 Figure 4, in order:
//   octets 0-7   IKE SA Initiator's SPI
//   octets 8-15  IKE SA Responder's SPI, zero in the first message of an initial exchange
//   octet 16     Next Payload
//   octet 17     MjVer | MnVer, 2 | 0 -> 0x20
//   octet 18     Exchange Type, IKE_SA_INIT = 34
//   octet 19     Flags, X|X|R|V|I|X|X|X: I is 0x08, V is 0x10, R is 0x20
//   octets 20-23 Message ID, big endian
//   octets 24-27 Length, big endian
void test_rfc7296_ike_header_layout(void)
{
    TEST_ASSERT_EQUAL_INT(28, PROTOCORE_IKE_HDR_LEN);
    TEST_ASSERT_EQUAL_INT(8, PROTOCORE_IKE_SPI_LEN);
    TEST_ASSERT_EQUAL_HEX8(0x20, PROTOCORE_IKE_VERSION);
    TEST_ASSERT_EQUAL_INT(34, IKE_SA_INIT);
    TEST_ASSERT_EQUAL_INT(35, IKE_AUTH);
    TEST_ASSERT_EQUAL_INT(36, IKE_CREATE_CHILD_SA);
    TEST_ASSERT_EQUAL_INT(37, IKE_INFORMATIONAL);
    TEST_ASSERT_EQUAL_HEX8(0x08, PROTOCORE_IKE_FLAG_INITIATOR);
    TEST_ASSERT_EQUAL_HEX8(0x10, PROTOCORE_IKE_FLAG_VERSION);
    TEST_ASSERT_EQUAL_HEX8(0x20, PROTOCORE_IKE_FLAG_RESPONSE);

    memcpy(Ike.hdr.init_spi, SPI_I, 8);
    memcpy(Ike.hdr.resp_spi, SPI_R, 8);
    Ike.hdr.next_payload = IKE_PL_SA;
    Ike.hdr.version = PROTOCORE_IKE_VERSION;
    Ike.hdr.exchange = IKE_SA_INIT;
    Ike.hdr.flags = PROTOCORE_IKE_FLAG_INITIATOR;
    Ike.hdr.message_id = 0;
    Ike.hdr.length = 0x0000012Cu; // 300
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.hdr_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(28, Ike.n);

    static const uint8_t WANT[28] = {0x92, 0x1F, 0x4E, 0x77, 0x9B, 0x28, 0xC1, 0x03, // Initiator's SPI
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Responder's SPI
                                     33,                                             // Next Payload: SA
                                     0x20,                                           // MjVer 2, MnVer 0
                                     34,                                             // IKE_SA_INIT
                                     0x08,                                           // Flags: I
                                     0x00, 0x00, 0x00, 0x00,                         // Message ID 0
                                     0x00, 0x00, 0x01, 0x2C};                        // Length 300
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_out, 28);

    // The same octets read back give the same header.
    memset(&Ike.hdr, 0, sizeof(Ike.hdr));
    Ike.wire.msg = WANT;
    Ike.wire.len = sizeof(WANT);
    Ike.hdr_parse(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SPI_I, Ike.hdr.init_spi, 8);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SPI_R, Ike.hdr.resp_spi, 8);
    TEST_ASSERT_EQUAL_INT(IKE_PL_SA, Ike.hdr.next_payload);
    TEST_ASSERT_EQUAL_HEX8(0x20, Ike.hdr.version);
    TEST_ASSERT_EQUAL_INT(IKE_SA_INIT, Ike.hdr.exchange);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_IKE_FLAG_INITIATOR, Ike.hdr.flags);
    TEST_ASSERT_EQUAL_UINT32(0u, Ike.hdr.message_id);
    TEST_ASSERT_EQUAL_UINT32(300u, Ike.hdr.length);

    // Anything shorter than the header is not a header.
    for (size_t len = 0; len < 28; len++)
    {
        Ike.wire.msg = WANT;
        Ike.wire.len = len;
        Ike.hdr_parse(ikev2_work);
        TEST_ASSERT_FALSE(Ike.ok);
    }
}

// The Length is stamped last, once the payloads are laid down, so it is patched at octets 24..27.
void test_length_is_patched_in_place(void)
{
    memset(g_out, 0, 32);
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.msg.length = 0xDEADBEEFu;
    Ike.set_length(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_HEX8(0xDE, g_out[24]);
    TEST_ASSERT_EQUAL_HEX8(0xAD, g_out[25]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, g_out[26]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, g_out[27]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[23]); // nothing before the field is touched
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[28]); // nor after it

    Ike.out.cap = 27;
    Ike.set_length(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    Ike.out.buf = NULL;
    Ike.out.cap = sizeof(g_out);
    Ike.set_length(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
}

// RFC 7296 sec 3.2: Next Payload, the Critical bit at 0x80 of the second octet with RESERVED clear,
// then a Payload Length that counts the generic header itself.
void test_rfc7296_generic_payload_header(void)
{
    TEST_ASSERT_EQUAL_INT(4, PROTOCORE_IKE_PAYLOAD_HDR_LEN);
    TEST_ASSERT_EQUAL_HEX8(0x80, PROTOCORE_IKE_CRITICAL);

    static const uint8_t BODY[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.pl.next_payload = IKE_PL_NONCE;
    Ike.pl.critical = PROTO_FALSE;
    Ike.pl.data = BODY;
    Ike.pl.data_len = sizeof(BODY);
    Ike.payload_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(10, Ike.n);  // 4 + 6
    TEST_ASSERT_EQUAL_HEX8(40, g_out[0]); // Nonce
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, g_out[3]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BODY, g_out + 4, 6);

    Ike.pl.critical = PROTO_TRUE;
    Ike.payload_build(ikev2_work);
    TEST_ASSERT_EQUAL_HEX8(0x80, g_out[1]);

    // A payload with an empty body is still four octets long.
    Ike.pl.critical = PROTO_FALSE;
    Ike.pl.data = NULL;
    Ike.pl.data_len = 0;
    Ike.payload_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(4, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(0x04, g_out[3]);

    // Nowhere to write, or a body that does not fit, produces nothing.
    Ike.pl.data = BODY;
    Ike.pl.data_len = sizeof(BODY);
    Ike.out.cap = 9;
    Ike.payload_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(0, Ike.n);
    Ike.out.cap = sizeof(g_out);
    Ike.out.buf = NULL;
    Ike.payload_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(0, Ike.n);
    Ike.out.buf = g_out;
    Ike.pl.data = NULL;
    Ike.pl.data_len = 4;
    Ike.payload_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(0, Ike.n);
}

// RFC 7296 sec 3.1: payloads are identified by the header's Next Payload and then by each payload's
// own, until a Next Payload of zero says none follow.
void test_payload_chain_is_walked_forward(void)
{
    // Three payloads: Nonce(8) -> Notify(12) -> Delete(8), chained by their Next Payload fields.
    static const uint8_t AREA[28] = {
        41, 0x00, 0x00, 0x08, 0x01, 0x02, 0x03, 0x04,             // Nonce, next = Notify
        42, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x40, 0x0E, 0, 0, 0, 0, // Notify, next = Delete
        0,  0x00, 0x00, 0x08, 0x03, 0x04, 0x00, 0x01,             // Delete, next = none
    };
    IkePayloadIter it;
    Ike.walk.chain = &it;
    Ike.walk.first_type = IKE_PL_NONCE;
    Ike.wire.msg = AREA;
    Ike.wire.len = sizeof(AREA);
    Ike.payload_iter_init(ikev2_work);

    Ike.payload_next(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_PL_NONCE, Ike.payload.type);
    TEST_ASSERT_EQUAL_INT(IKE_PL_NOTIFY, Ike.payload.next_payload);
    TEST_ASSERT_FALSE(Ike.payload.critical);
    TEST_ASSERT_EQUAL_size_t(4, Ike.payload.body_len); // Payload Length counts the header
    TEST_ASSERT_EQUAL_HEX8(0x01, Ike.payload.body[0]);

    Ike.payload_next(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_PL_NOTIFY, Ike.payload.type);
    TEST_ASSERT_EQUAL_INT(IKE_PL_DELETE, Ike.payload.next_payload);
    TEST_ASSERT_EQUAL_size_t(8, Ike.payload.body_len);

    Ike.payload_next(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_PL_DELETE, Ike.payload.type);
    TEST_ASSERT_EQUAL_INT(IKE_PL_NONE, Ike.payload.next_payload);

    Ike.payload_next(ikev2_work); // the chain ended
    TEST_ASSERT_FALSE(Ike.ok);
}

// A Payload Length below the generic header, or one that runs past the payload area, is malformed:
// the walk stops rather than reading outside the message.
void test_payload_chain_rejects_bad_lengths(void)
{
    IkePayloadIter it;

    static const uint8_t TOO_SHORT[4] = {0, 0x00, 0x00, 0x03}; // Payload Length 3, under the header
    Ike.walk.chain = &it;
    Ike.walk.first_type = IKE_PL_NONCE;
    Ike.wire.msg = TOO_SHORT;
    Ike.wire.len = sizeof(TOO_SHORT);
    Ike.payload_iter_init(ikev2_work);
    Ike.payload_next(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);

    static const uint8_t PAST_END[4] = {0, 0x00, 0x00, 0x40}; // Payload Length 64, area is 4
    Ike.wire.msg = PAST_END;
    Ike.wire.len = sizeof(PAST_END);
    Ike.payload_iter_init(ikev2_work);
    Ike.payload_next(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);

    static const uint8_t TRUNCATED[3] = {0, 0x00, 0x00}; // not even a generic header
    Ike.wire.msg = TRUNCATED;
    Ike.wire.len = sizeof(TRUNCATED);
    Ike.payload_iter_init(ikev2_work);
    Ike.payload_next(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);

    // A first type of none says the chain is empty before it starts.
    Ike.walk.first_type = IKE_PL_NONE;
    Ike.wire.msg = TOO_SHORT;
    Ike.wire.len = sizeof(TOO_SHORT);
    Ike.payload_iter_init(ikev2_work);
    Ike.payload_next(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
}

// RFC 7296 sec 3.3 the SA payload, sec 3.3.1 the Proposal Substructure (Last Substruc, RESERVED,
// Proposal Length, Proposal Num, Protocol ID, SPI Size, Num Transforms), sec 3.3.2 the Transform
// Substructure (Last Substruc, RESERVED, Transform Length, Transform Type, RESERVED, Transform ID),
// and sec 3.3.5 the Key Length attribute in TV form: the Attribute Format bit set with type 14.
void test_rfc7296_sa_proposal_transform_tree(void)
{
    static const IkeTransform TRANSFORMS[3] = {
        {IKE_TRANSFORM_ENCR, IKE_ENCR_AES_GCM_16, 256}, // with a Key Length attribute
        {IKE_TRANSFORM_PRF, IKE_PRF_HMAC_SHA2_256, -1}, // fixed-length key, no attribute
        {IKE_TRANSFORM_DH, IKE_DH_CURVE25519, -1},
    };
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.pl.next_payload = IKE_PL_KE;
    Ike.prop.proposal_num = 1;
    Ike.prop.protocol_id = IKE_PROTO_IKE;
    Ike.prop.spi = NULL;
    Ike.prop.spi_size = 0;
    Ike.prop.transforms = TRANSFORMS;
    Ike.prop.num_transforms = 3;
    Ike.sa_build(ikev2_work);

    //  4 generic header + 8 proposal header + 12 (ENCR with attribute) + 8 (PRF) + 8 (DH) = 40
    static const uint8_t WANT[40] = {
        34,   0x00, 0x00, 0x28,                 // SA payload: next = KE, Payload Length 40
        0x00, 0x00, 0x00, 0x24,                 // Last Substruc 0, RESERVED, Proposal Length 36
        1,    1,    0,    3,                    // Proposal Num 1, Protocol IKE, SPI Size 0, 3 transforms
        3,    0x00, 0x00, 0x0C, 1, 0x00, 0, 20, // more follow, len 12, ENCR, id 20 (AES-GCM 16)
        0x80, 0x0E, 0x01, 0x00,                 // AF|Key Length(14), 256 bits
        3,    0x00, 0x00, 0x08, 2, 0x00, 0, 5,  // more follow, len 8, PRF, id 5
        0,    0x00, 0x00, 0x08, 4, 0x00, 0, 31, // last, len 8, DH, Group Num 31
    };
    TEST_ASSERT_EQUAL_size_t(40, Ike.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_out, 40);

    // The same octets read back as the same tree.
    Ike.wire.msg = g_out + PROTOCORE_IKE_PAYLOAD_HDR_LEN;
    Ike.wire.len = Ike.n - PROTOCORE_IKE_PAYLOAD_HDR_LEN;
    Ike.sa_first_proposal(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_TRUE(Ike.proposal.last);
    TEST_ASSERT_EQUAL_UINT8(1, Ike.proposal.proposal_num);
    TEST_ASSERT_EQUAL_INT(IKE_PROTO_IKE, Ike.proposal.protocol_id);
    TEST_ASSERT_EQUAL_UINT8(0, Ike.proposal.spi_size);
    TEST_ASSERT_EQUAL_UINT8(3, Ike.proposal.num_transforms);

    IkeProposalRef prop = Ike.proposal;
    IkeTransformIter tit;
    Ike.walk.transforms = &tit;
    Ike.walk.proposal = &prop;
    Ike.transform_iter_init(ikev2_work);

    Ike.transform_next(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_TRANSFORM_ENCR, Ike.transform.type);
    TEST_ASSERT_EQUAL_UINT16(IKE_ENCR_AES_GCM_16, Ike.transform.id);
    TEST_ASSERT_EQUAL_INT32(256, Ike.transform.key_length);
    TEST_ASSERT_FALSE(Ike.transform.last);

    Ike.transform_next(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_TRANSFORM_PRF, Ike.transform.type);
    TEST_ASSERT_EQUAL_UINT16(IKE_PRF_HMAC_SHA2_256, Ike.transform.id);
    TEST_ASSERT_EQUAL_INT32(-1, Ike.transform.key_length); // absent, not zero

    Ike.transform_next(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_TRANSFORM_DH, Ike.transform.type);
    TEST_ASSERT_EQUAL_UINT16(IKE_DH_CURVE25519, Ike.transform.id);
    TEST_ASSERT_TRUE(Ike.transform.last);

    Ike.transform_next(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
}

// RFC 7296 sec 3.3.1: a Child SA proposal names its SPI, and SPI Size says how long it is.
void test_sa_proposal_with_an_spi(void)
{
    static const uint8_t ESP_SPI[4] = {0xC0, 0xFF, 0xEE, 0x01};
    static const IkeTransform ONE[1] = {{IKE_TRANSFORM_ENCR, IKE_ENCR_AES_GCM_16, 128}};
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.pl.next_payload = IKE_PL_NONE;
    Ike.prop.proposal_num = 1;
    Ike.prop.protocol_id = IKE_PROTO_ESP;
    Ike.prop.spi = ESP_SPI;
    Ike.prop.spi_size = 4;
    Ike.prop.transforms = ONE;
    Ike.prop.num_transforms = 1;
    Ike.sa_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(4 + 8 + 4 + 12, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(3, g_out[9]);  // Protocol ID: ESP
    TEST_ASSERT_EQUAL_HEX8(4, g_out[10]); // SPI Size
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ESP_SPI, g_out + 12, 4);

    Ike.wire.msg = g_out + 4;
    Ike.wire.len = Ike.n - 4;
    Ike.sa_first_proposal(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_UINT8(4, Ike.proposal.spi_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ESP_SPI, Ike.proposal.spi, 4);
    TEST_ASSERT_EQUAL_INT(IKE_PROTO_ESP, Ike.proposal.protocol_id);

    // A proposal with no transforms is not a proposal, and an SPI Size with no SPI is not one either.
    Ike.prop.num_transforms = 0;
    Ike.sa_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(0, Ike.n);
    Ike.prop.num_transforms = 1;
    Ike.prop.spi = NULL;
    Ike.sa_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(0, Ike.n);
}

// RFC 7296 sec 3.4: Diffie-Hellman Group Num then two RESERVED octets, then Key Exchange Data.
void test_rfc7296_ke_payload(void)
{
    static const uint8_t KE_DATA[32] = {0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54, 0x74, 0x8b, 0x7d,
                                        0xdc, 0xb4, 0x3e, 0xf7, 0x5a, 0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38,
                                        0x1a, 0xf4, 0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a};
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.pl.next_payload = IKE_PL_NONCE;
    Ike.pl.data = KE_DATA;
    Ike.pl.data_len = sizeof(KE_DATA);
    Ike.ke.dh_group = IKE_DH_CURVE25519;
    Ike.ke_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(4 + 4 + 32, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(40, g_out[0]);   // next = Nonce
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[2]); // Payload Length 40
    TEST_ASSERT_EQUAL_HEX8(0x28, g_out[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[4]); // Group Num 31, big endian
    TEST_ASSERT_EQUAL_HEX8(0x1F, g_out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[6]); // RESERVED
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[7]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(KE_DATA, g_out + 8, 32);

    Ike.wire.msg = g_out + 4;
    Ike.wire.len = Ike.n - 4;
    Ike.ke_parse(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_UINT16(IKE_DH_CURVE25519, Ike.ke_ref.dh_group);
    TEST_ASSERT_EQUAL_size_t(32, Ike.ke_ref.ke_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(KE_DATA, Ike.ke_ref.ke_data, 32);

    // A body with no room for the Group Num and RESERVED is not a KE payload.
    static const uint8_t SHORT[3] = {0, 31, 0};
    Ike.wire.msg = SHORT;
    Ike.wire.len = sizeof(SHORT);
    Ike.ke_parse(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
}

// RFC 7296 sec 3.9 the Nonce payload, sec 3.5 the Identification payload (ID Type then three
// RESERVED octets), sec 3.8 the Authentication payload (Auth Method then three RESERVED octets).
void test_rfc7296_nonce_id_and_auth_payloads(void)
{
    static const uint8_t NONCE[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                      0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.pl.next_payload = IKE_PL_NONE;
    Ike.pl.data = NONCE;
    Ike.pl.data_len = sizeof(NONCE);
    Ike.nonce_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(20, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(0x14, g_out[3]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NONCE, g_out + 4, 16);

    // ID Type 3 is ID_RFC822_ADDR (sec 3.5), so the Identification Data is the address itself.
    static const char EMAIL[] = "ike@example.com";
    Ike.pl.next_payload = IKE_PL_AUTH;
    Ike.pl.data = (const uint8_t *)EMAIL;
    Ike.pl.data_len = sizeof(EMAIL) - 1;
    Ike.id.id_type = IKE_ID_RFC822_ADDR;
    Ike.id_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(4 + 4 + 15, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(39, g_out[0]); // next = AUTH
    TEST_ASSERT_EQUAL_HEX8(0x17, g_out[3]);
    TEST_ASSERT_EQUAL_HEX8(3, g_out[4]);    // ID Type
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[5]); // RESERVED
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[6]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[7]);
    TEST_ASSERT_EQUAL_MEMORY(EMAIL, g_out + 8, 15);

    Ike.wire.msg = g_out + 4;
    Ike.wire.len = Ike.n - 4;
    Ike.id_parse(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_ID_RFC822_ADDR, Ike.id_ref.id_type);
    TEST_ASSERT_EQUAL_size_t(15, Ike.id_ref.id_len);

    // Auth Method 2 is the Shared Key Message Integrity Code (sec 3.8), whose data is the 32-octet
    // PRF_HMAC_SHA2_256 output (sec 2.15).
    static const uint8_t AUTH_DATA[PROTOCORE_IKE_AUTH_LEN] = {0xFF};
    Ike.pl.next_payload = IKE_PL_NONE;
    Ike.pl.data = AUTH_DATA;
    Ike.pl.data_len = sizeof(AUTH_DATA);
    Ike.auth.auth_method = IKE_AUTH_PSK;
    Ike.auth_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(4 + 4 + 32, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(2, g_out[4]); // Auth Method: PSK
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[5]);

    Ike.wire.msg = g_out + 4;
    Ike.wire.len = Ike.n - 4;
    Ike.auth_parse(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_AUTH_PSK, Ike.auth_ref.auth_method);
    TEST_ASSERT_EQUAL_size_t(32, Ike.auth_ref.auth_len);

    // A body with no room for the type octet and its RESERVED is neither an ID nor an AUTH payload.
    static const uint8_t SHORT[3] = {1, 0, 0};
    Ike.wire.msg = SHORT;
    Ike.wire.len = sizeof(SHORT);
    Ike.id_parse(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_ID_RESERVED, Ike.id_ref.id_type);
    Ike.auth_parse(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_AUTH_RESERVED, Ike.auth_ref.auth_method);
}

// RFC 7296 sec 3.10: Protocol ID, SPI Size, Notify Message Type, then the SPI and the Notification
// Data. Sec 3.10.1 assigns COOKIE 16390 and RFC 7383 sec 6 assigns
// IKEV2_FRAGMENTATION_SUPPORTED 16430.
void test_rfc7296_notify_payload(void)
{
    TEST_ASSERT_EQUAL_INT(16390, PROTOCORE_IKE_N_COOKIE);
    TEST_ASSERT_EQUAL_INT(16430, PROTOCORE_IKE_N_FRAGMENTATION_SUPPORTED);

    static const uint8_t ESP_SPI[4] = {0xC0, 0xFF, 0xEE, 0x02};
    static const uint8_t DATA[3] = {0xAA, 0xBB, 0xCC};
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.pl.next_payload = IKE_PL_NONE;
    Ike.pl.data = DATA;
    Ike.pl.data_len = sizeof(DATA);
    Ike.prop.protocol_id = IKE_PROTO_ESP;
    Ike.prop.spi = ESP_SPI;
    Ike.prop.spi_size = 4;
    Ike.notify.notify_type = PROTOCORE_IKE_N_FRAGMENTATION_SUPPORTED;
    Ike.notify_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(4 + 4 + 4 + 3, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(0x0F, g_out[3]); // Payload Length 15
    TEST_ASSERT_EQUAL_HEX8(3, g_out[4]);    // Protocol ID: ESP
    TEST_ASSERT_EQUAL_HEX8(4, g_out[5]);    // SPI Size
    TEST_ASSERT_EQUAL_HEX8(0x40, g_out[6]); // 16430 = 0x402E
    TEST_ASSERT_EQUAL_HEX8(0x2E, g_out[7]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ESP_SPI, g_out + 8, 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, g_out + 12, 3);

    Ike.wire.msg = g_out + 4;
    Ike.wire.len = Ike.n - 4;
    Ike.notify_parse(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_PROTO_ESP, Ike.notify_ref.protocol_id);
    TEST_ASSERT_EQUAL_UINT8(4, Ike.notify_ref.spi_size);
    TEST_ASSERT_EQUAL_UINT16(16430, Ike.notify_ref.notify_type);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ESP_SPI, Ike.notify_ref.spi, 4);
    TEST_ASSERT_EQUAL_size_t(3, Ike.notify_ref.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, Ike.notify_ref.data, 3);

    // A Notify concerning no existing SA carries Protocol ID and SPI Size zero and no SPI.
    Ike.prop.protocol_id = IKE_PROTO_NONE;
    Ike.prop.spi = NULL;
    Ike.prop.spi_size = 0;
    Ike.pl.data = NULL;
    Ike.pl.data_len = 0;
    Ike.notify_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(8, Ike.n);
    Ike.wire.msg = g_out + 4;
    Ike.wire.len = 4;
    Ike.notify_parse(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_NULL(Ike.notify_ref.spi);
    TEST_ASSERT_EQUAL_size_t(0, Ike.notify_ref.data_len);

    // An SPI Size the body cannot hold is malformed.
    static const uint8_t LIES[4] = {0x01, 0x08, 0x40, 0x06}; // SPI Size 8, body is 4
    Ike.wire.msg = LIES;
    Ike.wire.len = sizeof(LIES);
    Ike.notify_parse(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
}

// RFC 7296 sec 3.11: Protocol ID, SPI Size, Num of SPIs, then that many SPIs of that size.
void test_rfc7296_delete_payload(void)
{
    static const uint8_t SPIS[8] = {0xC0, 0xFF, 0xEE, 0x01, 0xC0, 0xFF, 0xEE, 0x02};
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.pl.next_payload = IKE_PL_NONE;
    Ike.pl.data = SPIS;
    Ike.prop.protocol_id = IKE_PROTO_ESP;
    Ike.prop.spi_size = 4;
    Ike.prop.num_spis = 2;
    Ike.delete_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(4 + 4 + 8, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(0x10, g_out[3]);
    TEST_ASSERT_EQUAL_HEX8(3, g_out[4]);
    TEST_ASSERT_EQUAL_HEX8(4, g_out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[6]); // Num of SPIs is 16 bits, big endian
    TEST_ASSERT_EQUAL_HEX8(0x02, g_out[7]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SPIS, g_out + 8, 8);

    Ike.wire.msg = g_out + 4;
    Ike.wire.len = Ike.n - 4;
    Ike.delete_parse(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_PROTO_ESP, Ike.delete_ref.protocol_id);
    TEST_ASSERT_EQUAL_UINT8(4, Ike.delete_ref.spi_size);
    TEST_ASSERT_EQUAL_UINT16(2, Ike.delete_ref.num_spis);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SPIS, Ike.delete_ref.spis, 8);

    // Deleting the IKE SA itself names no SPI (sec 3.11).
    Ike.prop.protocol_id = IKE_PROTO_IKE;
    Ike.prop.spi_size = 0;
    Ike.prop.num_spis = 0;
    Ike.delete_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(8, Ike.n);
    Ike.wire.msg = g_out + 4;
    Ike.wire.len = 4;
    Ike.delete_parse(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_NULL(Ike.delete_ref.spis);

    // A count the body cannot hold is malformed.
    static const uint8_t LIES[4] = {0x03, 0x04, 0x00, 0x09}; // 9 SPIs of 4 octets, body is 4
    Ike.wire.msg = LIES;
    Ike.wire.len = sizeof(LIES);
    Ike.delete_parse(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
}

// RFC 7296 sec 3.13: Number of TSs then three RESERVED octets. Sec 3.13.1: each selector is TS Type,
// IP Protocol ID, Selector Length, Start Port, End Port, Starting Address, Ending Address. TS Type 7
// is TS_IPV4_ADDR_RANGE, so its addresses are 4 octets and the selector is 16 octets long.
void test_rfc7296_traffic_selectors(void)
{
    static const uint8_t LO4[4] = {10, 0, 0, 0};
    static const uint8_t HI4[4] = {10, 0, 0, 255};
    static const uint8_t LO6[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    static const uint8_t HI6[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const IkeTrafficSelector SELS[2] = {
        {IKE_TS_IPV4_ADDR_RANGE, 6, 0, 65535, LO4, HI4, 4},  // TCP, every port
        {IKE_TS_IPV6_ADDR_RANGE, 0, 443, 443, LO6, HI6, 16}, // any protocol, one port
    };
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.pl.next_payload = IKE_PL_NONE;
    Ike.ts.sels = SELS;
    Ike.ts.num = 2;
    Ike.ts_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(4 + 4 + 16 + 40, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(2, g_out[4]);    // Number of TSs
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[5]); // RESERVED
    TEST_ASSERT_EQUAL_HEX8(7, g_out[8]);    // TS Type: IPv4 address range
    TEST_ASSERT_EQUAL_HEX8(6, g_out[9]);    // IP Protocol ID: TCP
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[10]);
    TEST_ASSERT_EQUAL_HEX8(0x10, g_out[11]); // Selector Length 16
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[12]); // Start Port 0
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[13]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_out[14]); // End Port 65535
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_out[15]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(LO4, g_out + 16, 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HI4, g_out + 20, 4);
    TEST_ASSERT_EQUAL_HEX8(8, g_out[24]);    // TS Type: IPv6 address range
    TEST_ASSERT_EQUAL_HEX8(0x28, g_out[27]); // Selector Length 40

    Ike.wire.msg = g_out + 4;
    Ike.wire.len = Ike.n - 4;
    Ike.ts_count(ikev2_work);
    TEST_ASSERT_EQUAL_UINT8(2, Ike.u8);

    Ike.ts.index = 0;
    Ike.ts_get(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_TS_IPV4_ADDR_RANGE, Ike.sel.ts_type);
    TEST_ASSERT_EQUAL_UINT8(6, Ike.sel.ip_protocol);
    TEST_ASSERT_EQUAL_UINT16(0, Ike.sel.start_port);
    TEST_ASSERT_EQUAL_UINT16(65535, Ike.sel.end_port);
    TEST_ASSERT_EQUAL_size_t(4, Ike.sel.addr_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(LO4, Ike.sel.start_addr, 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HI4, Ike.sel.end_addr, 4);

    Ike.ts.index = 1;
    Ike.ts_get(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_TS_IPV6_ADDR_RANGE, Ike.sel.ts_type);
    TEST_ASSERT_EQUAL_UINT16(443, Ike.sel.start_port);
    TEST_ASSERT_EQUAL_size_t(16, Ike.sel.addr_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HI6, Ike.sel.end_addr, 16);

    Ike.ts.index = 2; // past Number of TSs
    Ike.ts_get(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);

    // A selector whose address halves are not the same length is malformed: the remainder after the
    // 8-octet head has to be even.
    static const uint8_t ODD[13] = {1, 0, 0, 0, 7, 0, 0, 0x09, 0, 0, 0, 0, 0};
    Ike.wire.msg = ODD;
    Ike.wire.len = sizeof(ODD);
    Ike.ts.index = 0;
    Ike.ts_get(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
}

// RFC 7296 sec 3.15: CFG Type then three RESERVED octets. Sec 3.15.1: each attribute is a reserved
// bit plus a 15-bit Attribute Type, a 2-octet Length, and that many Value octets. Type 1 is
// INTERNAL_IP4_ADDRESS and type 3 is INTERNAL_IP4_DNS.
void test_rfc7296_configuration_payload(void)
{
    static const uint8_t IP4[4] = {10, 1, 2, 3};
    static const uint8_t DNS[4] = {8, 8, 8, 8};
    static const IkeCfgAttr ATTRS[3] = {
        {PROTOCORE_IKE_CFG_INTERNAL_IP4_ADDRESS, IP4, 4},
        {PROTOCORE_IKE_CFG_INTERNAL_IP4_DNS, DNS, 4},
        {PROTOCORE_IKE_CFG_INTERNAL_IP4_NETMASK, NULL, 0}, // a request asks with an empty value
    };
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.pl.next_payload = IKE_PL_NONE;
    Ike.cp.cfg_type = IKE_CFG_REPLY;
    Ike.cp.attrs = ATTRS;
    Ike.cp.num_attrs = 3;
    Ike.cp_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(4 + 4 + 8 + 8 + 4, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(2, g_out[4]);     // CFG Type: CFG_REPLY
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[8]);  // the reserved bit stays clear
    TEST_ASSERT_EQUAL_HEX8(0x01, g_out[9]);  // Attribute Type 1
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[10]); // Length 4
    TEST_ASSERT_EQUAL_HEX8(0x04, g_out[11]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(IP4, g_out + 12, 4);

    Ike.wire.msg = g_out + 4;
    Ike.wire.len = Ike.n - 4;
    Ike.cp_parse(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_INT(IKE_CFG_REPLY, Ike.cp_ref.cfg_type);

    IkeCfgAttrIter it;
    Ike.walk.attrs = &it;
    Ike.wire.msg = Ike.cp_ref.attrs;
    Ike.wire.len = Ike.cp_ref.attrs_len;
    Ike.cp_attr_iter_init(ikev2_work);

    Ike.cp_attr_next(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_IKE_CFG_INTERNAL_IP4_ADDRESS, Ike.attr.type);
    TEST_ASSERT_EQUAL_UINT16(4, Ike.attr.value_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(IP4, Ike.attr.value, 4);

    Ike.cp_attr_next(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_IKE_CFG_INTERNAL_IP4_DNS, Ike.attr.type);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DNS, Ike.attr.value, 4);

    Ike.cp_attr_next(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_IKE_CFG_INTERNAL_IP4_NETMASK, Ike.attr.type);
    TEST_ASSERT_EQUAL_UINT16(0, Ike.attr.value_len);
    TEST_ASSERT_NULL(Ike.attr.value);

    Ike.cp_attr_next(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);

    // A Length that runs past the attribute area is malformed.
    static const uint8_t LIES[6] = {0x00, 0x01, 0x00, 0x40, 0x00, 0x00};
    Ike.wire.msg = LIES;
    Ike.wire.len = sizeof(LIES);
    Ike.cp_attr_iter_init(ikev2_work);
    Ike.cp_attr_next(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
}

// RFC 7296 sec 3.14 as RFC 5282 sec 3 rewrites it: the Encrypted payload carries the Initialization
// Vector, the Ciphertext and the Integrity Checksum Data end to end behind the generic header. RFC
// 5282 sec 3.1 gives AES-GCM an 8-octet IV and sec 3.2 a 16-octet ICV.
void test_rfc5282_sk_payload_envelope(void)
{
    TEST_ASSERT_EQUAL_INT(8, PROTOCORE_IKE_GCM_IV_LEN);
    TEST_ASSERT_EQUAL_INT(16, PROTOCORE_IKE_AEAD_ICV_LEN);
    TEST_ASSERT_EQUAL_INT(4, PROTOCORE_IKE_GCM_SALT_LEN);
    TEST_ASSERT_EQUAL_INT(20, IKE_ENCR_AES_GCM_16); // RFC 5282 sec 7.2

    static const uint8_t IV[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t CT[10] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9};
    static const uint8_t ICV[16] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
                                    0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.pl.next_payload = IKE_PL_IDI; // the Encrypted payload's Next Payload names the inner chain
    Ike.sk.iv = IV;
    Ike.sk.iv_len = sizeof(IV);
    Ike.sk.ct = CT;
    Ike.sk.ct_len = sizeof(CT);
    Ike.sk.icv = ICV;
    Ike.sk.icv_len = sizeof(ICV);
    Ike.sk_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(4 + 8 + 10 + 16, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(35, g_out[0]);   // next = IDi
    TEST_ASSERT_EQUAL_HEX8(0x26, g_out[3]); // Payload Length 38
    TEST_ASSERT_EQUAL_HEX8_ARRAY(IV, g_out + 4, 8);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CT, g_out + 12, 10);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ICV, g_out + 22, 16);

    // The body carves back apart by the lengths the negotiated transform defines.
    Ike.wire.msg = g_out + 4;
    Ike.wire.len = Ike.n - 4;
    Ike.sk_parse(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(IV, Ike.sk_ref.iv, 8);
    TEST_ASSERT_EQUAL_size_t(10, Ike.sk_ref.ct_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CT, Ike.sk_ref.ct, 10);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ICV, Ike.sk_ref.icv, 16);

    // A body too short to hold the IV and the ICV is malformed.
    Ike.wire.len = 23;
    Ike.sk_parse(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    Ike.wire.len = 24; // exactly IV + ICV, with no ciphertext, is still well formed
    Ike.sk_parse(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_size_t(0, Ike.sk_ref.ct_len);
}

// RFC 8031 sec 3 gives Curve25519 Diffie-Hellman Group Num 31 and makes the Key Exchange Data the
// 32-octet X25519 public value. RFC 7748 sec 6.1 publishes the vector below.
void test_rfc7748_curve25519_key_exchange(void)
{
    TEST_ASSERT_EQUAL_INT(31, IKE_DH_CURVE25519);
    TEST_ASSERT_EQUAL_INT(32, PROTOCORE_IKE_X25519_LEN);

    // Alice's private key, a
    static const uint8_t A_PRIV[32] = {0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d, 0x3c, 0x16, 0xc1,
                                       0x72, 0x51, 0xb2, 0x66, 0x45, 0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0,
                                       0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a};
    // Alice's public key, X25519(a, 9)
    static const uint8_t A_PUB[32] = {0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54, 0x74, 0x8b, 0x7d,
                                      0xdc, 0xb4, 0x3e, 0xf7, 0x5a, 0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38,
                                      0x1a, 0xf4, 0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a};
    // Bob's private key, b
    static const uint8_t B_PRIV[32] = {0x5d, 0xab, 0x08, 0x7e, 0x62, 0x4a, 0x8a, 0x4b, 0x79, 0xe1, 0x7f,
                                       0x8b, 0x83, 0x80, 0x0e, 0xe6, 0x6f, 0x3b, 0xb1, 0x29, 0x26, 0x18,
                                       0xb6, 0xfd, 0x1c, 0x2f, 0x8b, 0x27, 0xff, 0x88, 0xe0, 0xeb};
    // Bob's public key, X25519(b, 9)
    static const uint8_t B_PUB[32] = {0xde, 0x9e, 0xdb, 0x7d, 0x7b, 0x7d, 0xc1, 0xb4, 0xd3, 0x5b, 0x61,
                                      0xc2, 0xec, 0xe4, 0x35, 0x37, 0x3f, 0x83, 0x43, 0xc8, 0x5b, 0x78,
                                      0x67, 0x4d, 0xad, 0xfc, 0x7e, 0x14, 0x6f, 0x88, 0x2b, 0x4f};
    // Their shared secret, K
    static const uint8_t K[32] = {0x4a, 0x5d, 0x9d, 0x5b, 0xa4, 0xce, 0x2d, 0xe1, 0x72, 0x8e, 0x3b,
                                  0xf4, 0x80, 0x35, 0x0f, 0x25, 0xe0, 0x7e, 0x21, 0xc9, 0x47, 0xd1,
                                  0x9e, 0x33, 0x76, 0xf0, 0x9b, 0x3c, 0x1e, 0x16, 0x17, 0x42};

    uint8_t pub[32];
    Ike.ke.dh_group = IKE_DH_CURVE25519;
    Ike.ke.our_priv = A_PRIV;
    Ike.ke.our_priv_len = 32;
    Ike.out.buf = pub;
    Ike.out.cap = sizeof(pub);
    Ike.dh_public(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(32, Ike.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(A_PUB, pub, 32);

    Ike.ke.our_priv = B_PRIV;
    Ike.dh_public(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(32, Ike.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(B_PUB, pub, 32);

    // Both sides reach the same g^ir from their own private and the peer's Key Exchange Data.
    uint8_t secret[32];
    Ike.out.buf = secret;
    Ike.out.cap = sizeof(secret);
    Ike.ke.our_priv = A_PRIV;
    Ike.ke.our_priv_len = 32;
    Ike.ke.peer_pub = B_PUB;
    Ike.ke.peer_pub_len = 32;
    Ike.dh_compute(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(32, Ike.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(K, secret, 32);

    memset(secret, 0, sizeof(secret));
    Ike.ke.our_priv = B_PRIV;
    Ike.ke.peer_pub = A_PUB;
    Ike.dh_compute(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(32, Ike.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(K, secret, 32);
}

// Only Group Num 31 is implemented, and its values are 32 octets. Anything else produces nothing
// rather than a shorter or a wrong-group key.
void test_dh_refuses_other_groups_and_lengths(void)
{
    static const uint8_t PRIV[32] = {1};
    static const uint8_t PUB[32] = {9};
    uint8_t out[32];
    Ike.out.buf = out;
    Ike.out.cap = sizeof(out);
    Ike.ke.our_priv = PRIV;
    Ike.ke.our_priv_len = 32;
    Ike.ke.peer_pub = PUB;
    Ike.ke.peer_pub_len = 32;

    static const uint16_t OTHER_GROUPS[3] = {IKE_DH_MODP2048, IKE_DH_ECP256, 0};
    for (size_t i = 0; i < 3; i++)
    {
        Ike.ke.dh_group = OTHER_GROUPS[i];
        Ike.dh_public(ikev2_work);
        TEST_ASSERT_EQUAL_size_t(0, Ike.n);
        Ike.dh_compute(ikev2_work);
        TEST_ASSERT_EQUAL_size_t(0, Ike.n);
    }

    Ike.ke.dh_group = IKE_DH_CURVE25519;
    Ike.ke.our_priv_len = 31;
    Ike.dh_public(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(0, Ike.n);
    Ike.dh_compute(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(0, Ike.n);

    Ike.ke.our_priv_len = 32;
    Ike.ke.peer_pub_len = 31;
    Ike.dh_compute(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(0, Ike.n);

    Ike.ke.peer_pub_len = 32;
    Ike.out.cap = 31;
    Ike.dh_public(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(0, Ike.n);
    Ike.dh_compute(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(0, Ike.n);

    Ike.out.cap = sizeof(out);
    Ike.ke.our_priv = NULL;
    Ike.dh_public(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(0, Ike.n);
    Ike.dh_compute(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(0, Ike.n);
}

// RFC 7296 sec 2.13: the PRF's preferred key length fixes SK_d, SK_pi and SK_pr, which for
// PRF_HMAC_SHA2_256 is 32 octets (RFC 4868 sec 2.1.2). RFC 4868 sec 2.1.1 keys
// AUTH_HMAC_SHA2_256_128 with the full 32-octet hash output. RFC 5282 sec 7.1: an AEAD cipher
// carries its own integrity so SK_ai and SK_ar are empty, and its key has a 4-octet salt appended.
void test_rfc7296_suite_key_lengths(void)
{
    IkeSuite suite;
    IkeKeyLengths lens;

    // AES-GCM-256 with no separate integrity transform.
    suite.encr = IKE_ENCR_AES_GCM_16;
    suite.encr_keylen = 256;
    suite.prf = IKE_PRF_HMAC_SHA2_256;
    suite.integ = 0;
    suite.dh = IKE_DH_CURVE25519;
    Ike.keymat.suite = &suite;
    Ike.keymat.lens = &lens;
    Ike.suite_keylengths(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_size_t(32, lens.sk_d);
    TEST_ASSERT_EQUAL_size_t(32, lens.sk_p);
    TEST_ASSERT_EQUAL_size_t(0, lens.sk_a);  // the AEAD carries its own integrity
    TEST_ASSERT_EQUAL_size_t(36, lens.sk_e); // 32 key octets plus the 4-octet salt

    // AES-CBC-256 with AUTH_HMAC_SHA2_256_128: a separate integrity key and no salt.
    suite.encr = IKE_ENCR_AES_CBC;
    suite.integ = IKE_INTEG_HMAC_SHA2_256_128;
    Ike.suite_keylengths(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_size_t(32, lens.sk_a);
    TEST_ASSERT_EQUAL_size_t(32, lens.sk_e);

    // AES-GCM-128: 16 key octets plus the salt.
    suite.encr = IKE_ENCR_AES_GCM_16;
    suite.encr_keylen = 128;
    suite.integ = 0;
    Ike.suite_keylengths(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_size_t(20, lens.sk_e);

    // A PRF this schedule does not implement, an unknown integrity transform, and a key length that
    // is not a whole number of octets, are each refused rather than guessed at.
    suite.prf = 2;
    Ike.suite_keylengths(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    suite.prf = IKE_PRF_HMAC_SHA2_256;
    suite.integ = 99;
    Ike.suite_keylengths(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    suite.integ = 0;
    suite.encr_keylen = 129;
    Ike.suite_keylengths(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    suite.encr_keylen = -1;
    Ike.suite_keylengths(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    Ike.keymat.lens = NULL;
    suite.encr_keylen = 256;
    Ike.suite_keylengths(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
}

// RFC 7296 sec 2.13: prf+(K,S) = T1 | T2 | T3 | ... where T1 = prf(K, S | 0x01) and
// Ti = prf(K, Ti-1 | S | i). Every output is therefore a prefix of every longer output of the same
// K and S, and the single-octet counter caps the stream at 255 blocks.
void test_rfc7296_prf_plus_is_one_stream(void)
{
    static const uint8_t KEY[32] = {0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                                    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                                    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
    static const uint8_t SEED[8] = {'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e'};

    uint8_t short_out[32];
    uint8_t long_out[100];
    Ike.work = g_work;
    Ike.keymat.prf_key = KEY;
    Ike.keymat.prf_key_len = sizeof(KEY);
    Ike.keymat.seed = SEED;
    Ike.keymat.seed_len = sizeof(SEED);

    Ike.out.buf = short_out;
    Ike.out.cap = sizeof(short_out);
    Ike.prf_plus(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);

    Ike.out.buf = long_out;
    Ike.out.cap = sizeof(long_out);
    Ike.prf_plus(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(short_out, long_out, 32); // T1 is the same block either way

    // The blocks after T1 are not repeats of it: Ti feeds Ti+1.
    TEST_ASSERT_TRUE(memcmp(long_out, long_out + 32, 32) != 0);
    TEST_ASSERT_TRUE(memcmp(long_out + 32, long_out + 64, 32) != 0);

    // A different key or a different seed is a different stream.
    uint8_t other[32];
    Ike.out.buf = other;
    Ike.out.cap = sizeof(other);
    static const uint8_t SEED2[8] = {'H', 'i', ' ', 'T', 'h', 'e', 'r', 'f'};
    Ike.keymat.seed = SEED2;
    Ike.prf_plus(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_TRUE(memcmp(short_out, other, 32) != 0);

    Ike.keymat.seed = SEED;
    static const uint8_t KEY2[32] = {0x0c};
    Ike.keymat.prf_key = KEY2;
    Ike.prf_plus(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_TRUE(memcmp(short_out, other, 32) != 0);

    // Nothing to expand into, no key and no seed are each refused.
    Ike.keymat.prf_key = KEY;
    Ike.out.cap = 0;
    Ike.prf_plus(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    Ike.out.buf = NULL;
    Ike.out.cap = sizeof(other);
    Ike.prf_plus(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    Ike.out.buf = other;
    Ike.keymat.prf_key = NULL;
    Ike.prf_plus(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    Ike.keymat.prf_key = KEY;
    Ike.keymat.seed = NULL;
    Ike.prf_plus(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
}

// RFC 7296 sec 2.6: a stateless COOKIE is a version identifier for the secret it was made with,
// followed by a hash over Ni, IPi, SPIi and that secret. The responder recomputes it and compares.
void test_rfc7296_stateless_cookie(void)
{
    TEST_ASSERT_EQUAL_INT(33, PROTOCORE_IKE_COOKIE_LEN); // one version octet plus a SHA-256 hash

    static const uint8_t SECRET[16] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
                                       0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};
    static const uint8_t NI[16] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    static const uint8_t IPI[4] = {192, 0, 2, 1};

    uint8_t cookie[PROTOCORE_IKE_COOKIE_LEN];
    Ike.work = g_work;
    Ike.notify.version = 7;
    Ike.notify.secret = SECRET;
    Ike.notify.secret_len = sizeof(SECRET);
    Ike.notify.ni = NI;
    Ike.notify.ni_len = sizeof(NI);
    Ike.notify.ipi = IPI;
    Ike.notify.ipi_len = sizeof(IPI);
    Ike.notify.spii = SPI_I;
    Ike.out.buf = cookie;
    Ike.out.cap = sizeof(cookie);
    Ike.cookie_compute(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(33, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(7, cookie[0]); // VersionIDofSecret leads, so the secret can be rotated

    Ike.notify.cookie = cookie;
    Ike.notify.cookie_len = sizeof(cookie);
    Ike.cookie_verify(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);

    // A cookie made for a different initiator, address or nonce does not verify here.
    static const uint8_t OTHER_IP[4] = {198, 51, 100, 7};
    Ike.notify.ipi = OTHER_IP;
    Ike.cookie_verify(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    Ike.notify.ipi = IPI;

    static const uint8_t OTHER_SECRET[16] = {0xB0};
    Ike.notify.secret = OTHER_SECRET;
    Ike.cookie_verify(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    Ike.notify.secret = SECRET;

    // sec 2.6 writes the cookie as <VersionIDofSecret> | Hash(Ni | IPi | SPIi | <secret>), so the
    // version octet sits outside the hash: it names which secret to recompute with, and a verify
    // takes it from the cookie itself. Changing it therefore changes which secret the caller looks
    // up, not whether the hash agrees.
    uint8_t reversioned[PROTOCORE_IKE_COOKIE_LEN];
    memcpy(reversioned, cookie, sizeof(reversioned));
    reversioned[0] = (uint8_t)(reversioned[0] ^ 0x01);
    Ike.notify.cookie = reversioned;
    Ike.notify.cookie_len = sizeof(reversioned);
    Ike.cookie_verify(ikev2_work);
    TEST_ASSERT_TRUE(Ike.ok);
    TEST_ASSERT_EQUAL_HEX8(6, reversioned[0]); // the same secret, tagged as a different version

    // One flipped bit anywhere in the hash fails.
    for (size_t i = 1; i < PROTOCORE_IKE_COOKIE_LEN; i++)
    {
        uint8_t bad[PROTOCORE_IKE_COOKIE_LEN];
        memcpy(bad, cookie, sizeof(bad));
        bad[i] = (uint8_t)(bad[i] ^ 0x01);
        Ike.notify.cookie = bad;
        Ike.notify.cookie_len = sizeof(bad);
        Ike.cookie_verify(ikev2_work);
        TEST_ASSERT_FALSE(Ike.ok);
    }

    // A cookie of the wrong length, or none at all, is not a cookie.
    Ike.notify.cookie = cookie;
    Ike.notify.cookie_len = 32;
    Ike.cookie_verify(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);
    Ike.notify.cookie = NULL;
    Ike.notify.cookie_len = sizeof(cookie);
    Ike.cookie_verify(ikev2_work);
    TEST_ASSERT_FALSE(Ike.ok);

    // The COOKIE goes back as a Notify with Protocol ID and SPI Size zero (sec 2.6, sec 3.10).
    Ike.notify.cookie = cookie;
    Ike.notify.cookie_len = sizeof(cookie);
    Ike.out.buf = g_out;
    Ike.out.cap = sizeof(g_out);
    Ike.pl.next_payload = IKE_PL_NONE;
    Ike.cookie_notify_build(ikev2_work);
    TEST_ASSERT_EQUAL_size_t(4 + 4 + 33, Ike.n);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[4]); // Protocol ID
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[5]); // SPI Size
    TEST_ASSERT_EQUAL_HEX8(0x40, g_out[6]); // 16390 = 0x4006
    TEST_ASSERT_EQUAL_HEX8(0x06, g_out[7]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(cookie, g_out + 8, 33);
}
