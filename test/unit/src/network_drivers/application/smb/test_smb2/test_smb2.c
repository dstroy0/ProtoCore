// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SMB2 client wire codec (network_drivers/application/smb/smb2.h).
//
// MS-SMB2 sec 2.2.1.2 prints the SYNC packet header field by field, and states two values outright:
// "ProtocolId (4 bytes): The protocol identifier. The value MUST be set to 0x424D53FE, also
// represented as (in network order) 0xFE, 'S', 'M', and 'B'" and "StructureSize (2 bytes): This
// MUST be set to 64". test_msnlmp_smb2_header_layout checks every offset in that diagram and is the
// load-bearing case: the header prefixes every message this module builds and gates every message
// it accepts, so a field at the wrong offset breaks the whole protocol at once.
//
// The remaining values come from MS-SMB2 sec 2.2.1.2 (command codes, Flags bits), sec 2.2.4
// (dialect revisions), sec 2.2.3 / 2.2.3.1 (NEGOTIATE and its contexts), sec 2.2.41
// (TRANSFORM_HEADER) and sec 3.1.4.3 (the AEAD nonce lengths).

#include "network_drivers/application/smb/smb2/smb2.h"
#include <string.h>

#include <unity.h>

static uint8_t smb2_work[16]; // the borrow an entry takes; Smb2 never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_work[PROTOCORE_CRYPTO_BORROW_MAX];

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
    {
        v = (v << 8) | p[i];
    }
    return v;
}

// MS-SMB2 sec 2.2.1.2, offset by offset:
//   ProtocolId 4 @0   StructureSize 2 @4   CreditCharge 2 @6   Status 4 @8
//   Command 2 @12     CreditRequest 2 @14  Flags 4 @16         NextCommand 4 @20
//   MessageId 8 @24   Reserved 4 @32       TreeId 4 @36        SessionId 8 @40
//   Signature 16 @48                                            -> 64 octets
void test_msnlmp_smb2_header_layout(void)
{
    uint8_t buf[PROTOCORE_SMB2_HEADER_SIZE];
    memset(buf, 0xEE, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(64, PROTOCORE_SMB2_HEADER_SIZE);
    Smb2.build_header_args.buf = buf;
    Smb2.build_header_args.cap = sizeof(buf);
    Smb2.build_header_args.command = SMB2_TREE_CONNECT;
    Smb2.build_header_args.credit_request = 0x0100;
    Smb2.build_header_args.message_id = 0x0123456789ABCDEFull;
    Smb2.build_header_args.tree_id = 0x11223344u;
    Smb2.build_header_args.session_id = 0xFEDCBA9876543210ull;
    Smb2.build_header(smb2_work);
    TEST_ASSERT_EQUAL_size_t(64, Smb2.n);

    static const uint8_t PROTOCOL_ID[4] = {0xFE, 'S', 'M', 'B'};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PROTOCOL_ID, buf, 4);
    TEST_ASSERT_EQUAL_HEX32(0x424D53FEu, le32(buf)); // the same four octets read as a little-endian u32
    TEST_ASSERT_EQUAL_UINT16(64, le16(buf + 4));
    TEST_ASSERT_EQUAL_UINT16(0, le16(buf + 6)); // CreditCharge, unused by this client
    TEST_ASSERT_EQUAL_UINT32(0, le32(buf + 8)); // Status, set to 0 in a request
    TEST_ASSERT_EQUAL_UINT16(SMB2_TREE_CONNECT, le16(buf + 12));
    TEST_ASSERT_EQUAL_UINT16(0x0100, le16(buf + 14));
    TEST_ASSERT_EQUAL_UINT32(0, le32(buf + 16)); // Flags: SERVER_TO_REDIR MUST NOT be set on a request
    TEST_ASSERT_EQUAL_UINT32(0, le32(buf + 20)); // NextCommand: not a compounded request
    TEST_ASSERT_EQUAL_HEX64(0x0123456789ABCDEFull, le64(buf + 24));
    TEST_ASSERT_EQUAL_UINT32(0, le32(buf + 32)); // Reserved
    TEST_ASSERT_EQUAL_HEX32(0x11223344u, le32(buf + 36));
    TEST_ASSERT_EQUAL_HEX64(0xFEDCBA9876543210ull, le64(buf + 40));
    for (size_t i = 48; i < 64; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, buf[i]); // "If the message is not signed, this field MUST be 0."
    }

    Smb2Header h;
    memset(&h, 0xEE, sizeof(h));
    Smb2.parse_header_args.buf = buf;
    Smb2.parse_header_args.len = sizeof(buf);
    Smb2.parse_header_args.out = &h;
    Smb2.parse_header(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);
    TEST_ASSERT_EQUAL_INT(SMB2_TREE_CONNECT, h.command);
    TEST_ASSERT_EQUAL_UINT32(0, h.status);
    TEST_ASSERT_EQUAL_UINT32(0, h.flags);
    TEST_ASSERT_EQUAL_HEX64(0x0123456789ABCDEFull, h.message_id);
    TEST_ASSERT_EQUAL_HEX32(0x11223344u, h.tree_id);
    TEST_ASSERT_EQUAL_HEX64(0xFEDCBA9876543210ull, h.session_id);
    TEST_ASSERT_EQUAL_UINT16(0x0100, h.credit_response);
}

// sec 2.2.1.2 validates two things before anything else can be trusted: the ProtocolId and a
// StructureSize of 64. Either wrong, or a buffer shorter than the header, and there is no header.
void test_header_parse_fails_closed(void)
{
    uint8_t buf[PROTOCORE_SMB2_HEADER_SIZE];
    Smb2Header h;
    Smb2.build_header_args.buf = buf;
    Smb2.build_header_args.cap = sizeof(buf);
    Smb2.build_header_args.command = SMB2_NEGOTIATE;
    Smb2.build_header_args.credit_request = 1;
    Smb2.build_header_args.message_id = 0;
    Smb2.build_header_args.tree_id = 0;
    Smb2.build_header_args.session_id = 0;
    Smb2.build_header(smb2_work);
    TEST_ASSERT_EQUAL_size_t(64, Smb2.n);
    Smb2.parse_header_args.buf = buf;
    Smb2.parse_header_args.len = sizeof(buf);
    Smb2.parse_header_args.out = &h;
    Smb2.parse_header(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);

    Smb2.parse_header_args.buf = buf;
    Smb2.parse_header_args.len = 63;
    Smb2.parse_header_args.out = &h;
    Smb2.parse_header(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);

    uint8_t bad[PROTOCORE_SMB2_HEADER_SIZE];
    memcpy(bad, buf, sizeof(bad));
    bad[0] = 0xFF; // not 0xFE
    Smb2.parse_header_args.buf = bad;
    Smb2.parse_header_args.len = sizeof(bad);
    Smb2.parse_header_args.out = &h;
    Smb2.parse_header(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);

    memcpy(bad, buf, sizeof(bad));
    bad[1] = 'X'; // not 'S'
    Smb2.parse_header_args.buf = bad;
    Smb2.parse_header_args.len = sizeof(bad);
    Smb2.parse_header_args.out = &h;
    Smb2.parse_header(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);

    memcpy(bad, buf, sizeof(bad));
    bad[4] = 65; // StructureSize
    Smb2.parse_header_args.buf = bad;
    Smb2.parse_header_args.len = sizeof(bad);
    Smb2.parse_header_args.out = &h;
    Smb2.parse_header(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);

    Smb2.parse_header_args.buf = NULL;
    Smb2.parse_header_args.len = 64;
    Smb2.parse_header_args.out = &h;
    Smb2.parse_header(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    Smb2.parse_header_args.buf = buf;
    Smb2.parse_header_args.len = 64;
    Smb2.parse_header_args.out = NULL;
    Smb2.parse_header(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    Smb2.build_header_args.buf = buf;
    Smb2.build_header_args.cap = 63;
    Smb2.build_header_args.command = SMB2_NEGOTIATE;
    Smb2.build_header_args.credit_request = 1;
    Smb2.build_header_args.message_id = 0;
    Smb2.build_header_args.tree_id = 0;
    Smb2.build_header_args.session_id = 0;
    Smb2.build_header(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.build_header_args.buf = NULL;
    Smb2.build_header_args.cap = 64;
    Smb2.build_header_args.command = SMB2_NEGOTIATE;
    Smb2.build_header_args.credit_request = 1;
    Smb2.build_header_args.message_id = 0;
    Smb2.build_header_args.tree_id = 0;
    Smb2.build_header_args.session_id = 0;
    Smb2.build_header(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
}

// The command codes of sec 2.2.1.2's Command table, the dialect revisions of sec 2.2.4, the Flags
// bits, the SecurityMode flags of sec 2.2.3, the SessionFlags of sec 2.2.6, the ShareType of
// sec 2.2.10, the negotiate-context types of sec 2.2.3.1 and the cipher ids of sec 2.2.3.1.2.
void test_protocol_constants(void)
{
    TEST_ASSERT_EQUAL_UINT16(0x0000, SMB2_NEGOTIATE);
    TEST_ASSERT_EQUAL_UINT16(0x0001, SMB2_SESSION_SETUP);
    TEST_ASSERT_EQUAL_UINT16(0x0002, SMB2_LOGOFF);
    TEST_ASSERT_EQUAL_UINT16(0x0003, SMB2_TREE_CONNECT);
    TEST_ASSERT_EQUAL_UINT16(0x0004, SMB2_TREE_DISCONNECT);
    TEST_ASSERT_EQUAL_UINT16(0x0005, SMB2_CREATE);
    TEST_ASSERT_EQUAL_UINT16(0x0006, SMB2_CLOSE);
    TEST_ASSERT_EQUAL_UINT16(0x0008, SMB2_READ);
    TEST_ASSERT_EQUAL_UINT16(0x0009, SMB2_WRITE);

    TEST_ASSERT_EQUAL_UINT16(0x0202, SMB2_DIALECT_0202);
    TEST_ASSERT_EQUAL_UINT16(0x0210, SMB2_DIALECT_0210);
    TEST_ASSERT_EQUAL_UINT16(0x0300, SMB2_DIALECT_0300);
    TEST_ASSERT_EQUAL_UINT16(0x0302, SMB2_DIALECT_0302);
    TEST_ASSERT_EQUAL_UINT16(0x0311, SMB2_DIALECT_0311);

    TEST_ASSERT_EQUAL_HEX32(0x00000001u, SMB2_FLAGS_SERVER_TO_REDIR);
    TEST_ASSERT_EQUAL_HEX32(0x00000008u, SMB2_FLAGS_SIGNED);

    TEST_ASSERT_EQUAL_UINT16(0x0001, SMB2_NEGOTIATE_SIGNING_ENABLED);
    TEST_ASSERT_EQUAL_UINT16(0x0002, SMB2_NEGOTIATE_SIGNING_REQUIRED);

    TEST_ASSERT_EQUAL_UINT16(0x0001, SMB2_SESSION_FLAG_IS_GUEST);
    TEST_ASSERT_EQUAL_UINT16(0x0002, SMB2_SESSION_FLAG_IS_NULL);
    TEST_ASSERT_EQUAL_UINT16(0x0004, SMB2_SESSION_FLAG_ENCRYPT_DATA);

    TEST_ASSERT_EQUAL_UINT8(0x01, SMB2_SHARE_TYPE_DISK);
    TEST_ASSERT_EQUAL_UINT8(0x02, SMB2_SHARE_TYPE_PIPE);
    TEST_ASSERT_EQUAL_UINT8(0x03, SMB2_SHARE_TYPE_PRINT);

    TEST_ASSERT_EQUAL_UINT16(0x0001, SMB2_PREAUTH_INTEGRITY_CAPABILITIES);
    TEST_ASSERT_EQUAL_UINT16(0x0002, SMB2_ENCRYPTION_CAPABILITIES);
    TEST_ASSERT_EQUAL_UINT16(0x0008, SMB2_SIGNING_CAPABILITIES);
    TEST_ASSERT_EQUAL_UINT16(0x0001, SMB2_PREAUTH_INTEGRITY_SHA512);
    TEST_ASSERT_EQUAL_UINT16(0x0000, SMB2_SIGNING_HMAC_SHA256);
    TEST_ASSERT_EQUAL_UINT16(0x0001, SMB2_SIGNING_AES_CMAC);
    TEST_ASSERT_EQUAL_UINT16(0x0002, SMB2_SIGNING_AES_GMAC);

    TEST_ASSERT_EQUAL_UINT16(0x0001, SMB2_ENCRYPTION_AES128_CCM);
    TEST_ASSERT_EQUAL_UINT16(0x0002, SMB2_ENCRYPTION_AES128_GCM);
    TEST_ASSERT_EQUAL_UINT16(0x0003, SMB2_ENCRYPTION_AES256_CCM);
    TEST_ASSERT_EQUAL_UINT16(0x0004, SMB2_ENCRYPTION_AES256_GCM);
    TEST_ASSERT_EQUAL_HEX32(0x00000040u, SMB2_GLOBAL_CAP_ENCRYPTION);

    // MS-ERREF sec 2.3.1 status codes the SESSION_SETUP exchange turns on.
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, SMB2_STATUS_SUCCESS);
    TEST_ASSERT_EQUAL_HEX32(0xC0000016u, SMB2_STATUS_MORE_PROCESSING_REQUIRED);
    TEST_ASSERT_EQUAL_HEX32(0xC0000011u, SMB2_STATUS_END_OF_FILE);
}

// MS-SMB2 sec 2.1: over Direct TCP each message is preceded by a 4-octet header, "Zero (1 byte)"
// followed by "StreamProtocolLength (3 bytes)" in network byte order. So a 0x123456-octet message
// frames as 00 12 34 56, and 24 bits is the largest length the field can name.
void test_direct_tcp_transport_framing(void)
{
    static const uint8_t MSG[5] = {1, 2, 3, 4, 5};
    uint8_t out[16];
    Smb2.transport_frame_args.out = out;
    Smb2.transport_frame_args.cap = sizeof(out);
    Smb2.transport_frame_args.msg = MSG;
    Smb2.transport_frame_args.msg_len = sizeof(MSG);
    Smb2.transport_frame(smb2_work);
    size_t n = Smb2.n;
    TEST_ASSERT_EQUAL_size_t(9, n);
    static const uint8_t WANT[9] = {0x00, 0x00, 0x00, 0x05, 1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    Smb2.transport_len_args.buf = out;
    Smb2.transport_len_args.len = n;
    Smb2.transport_len(smb2_work);
    TEST_ASSERT_EQUAL_UINT32(5, Smb2.u32);

    // The three length octets are big-endian, so a value using all three is unambiguous.
    static const uint8_t BIG[4] = {0x00, 0x12, 0x34, 0x56};
    Smb2.transport_len_args.buf = BIG;
    Smb2.transport_len_args.len = sizeof(BIG);
    Smb2.transport_len(smb2_work);
    TEST_ASSERT_EQUAL_UINT32(0x123456u, Smb2.u32);

    // A non-zero first octet is not a Direct TCP frame.
    static const uint8_t BAD[4] = {0x01, 0x00, 0x00, 0x05};
    Smb2.transport_len_args.buf = BAD;
    Smb2.transport_len_args.len = sizeof(BAD);
    Smb2.transport_len(smb2_work);
    TEST_ASSERT_EQUAL_UINT32(0, Smb2.u32);
    Smb2.transport_len_args.buf = out;
    Smb2.transport_len_args.len = 3;
    Smb2.transport_len(smb2_work);
    TEST_ASSERT_EQUAL_UINT32(0, Smb2.u32);
    Smb2.transport_len_args.buf = NULL;
    Smb2.transport_len_args.len = 4;
    Smb2.transport_len(smb2_work);
    TEST_ASSERT_EQUAL_UINT32(0, Smb2.u32);

    // A length past 24 bits has no encoding, and the destination must hold prefix plus message.
    Smb2.transport_frame_args.out = out;
    Smb2.transport_frame_args.cap = sizeof(out);
    Smb2.transport_frame_args.msg = MSG;
    Smb2.transport_frame_args.msg_len = 0x01000000u;
    Smb2.transport_frame(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.transport_frame_args.out = out;
    Smb2.transport_frame_args.cap = 8;
    Smb2.transport_frame_args.msg = MSG;
    Smb2.transport_frame_args.msg_len = sizeof(MSG);
    Smb2.transport_frame(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.transport_frame_args.out = out;
    Smb2.transport_frame_args.cap = 9;
    Smb2.transport_frame_args.msg = MSG;
    Smb2.transport_frame_args.msg_len = sizeof(MSG);
    Smb2.transport_frame(smb2_work);
    TEST_ASSERT_EQUAL_size_t(9, Smb2.n);
    Smb2.transport_frame_args.out = NULL;
    Smb2.transport_frame_args.cap = 16;
    Smb2.transport_frame_args.msg = MSG;
    Smb2.transport_frame_args.msg_len = sizeof(MSG);
    Smb2.transport_frame(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.transport_frame_args.out = out;
    Smb2.transport_frame_args.cap = 16;
    Smb2.transport_frame_args.msg = NULL;
    Smb2.transport_frame_args.msg_len = sizeof(MSG);
    Smb2.transport_frame(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
}

// MS-SMB2 sec 2.2.3: the NEGOTIATE request body is StructureSize(2) = 36, DialectCount(2),
// SecurityMode(2), Reserved(2), Capabilities(4), ClientGuid(16), ClientStartTime(8), then
// DialectCount 2-octet dialect revisions. The header is 64 octets, so a four-dialect request is
// 64 + 36 + 8 = 108.
void test_negotiate_request_body(void)
{
    static const uint8_t GUID[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                     0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t buf[256];
    Smb2.build_negotiate_args.buf = buf;
    Smb2.build_negotiate_args.cap = sizeof(buf);
    Smb2.build_negotiate_args.client_guid = GUID;
    Smb2.build_negotiate_args.security_mode = SMB2_NEGOTIATE_SIGNING_ENABLED;
    Smb2.build_negotiate(smb2_work);
    size_t n = Smb2.n;
    TEST_ASSERT_EQUAL_size_t(108, n);

    const uint8_t *b = buf + PROTOCORE_SMB2_HEADER_SIZE;
    TEST_ASSERT_EQUAL_UINT16(36, le16(b + 0));
    TEST_ASSERT_EQUAL_UINT16(4, le16(b + 2));
    TEST_ASSERT_EQUAL_UINT16(SMB2_NEGOTIATE_SIGNING_ENABLED, le16(b + 4));
    TEST_ASSERT_EQUAL_UINT16(0, le16(b + 6)); // Reserved
    TEST_ASSERT_EQUAL_HEX8_ARRAY(GUID, b + 12, 16);
    TEST_ASSERT_EQUAL_UINT16(SMB2_DIALECT_0202, le16(b + 36));
    TEST_ASSERT_EQUAL_UINT16(SMB2_DIALECT_0210, le16(b + 38));
    TEST_ASSERT_EQUAL_UINT16(SMB2_DIALECT_0300, le16(b + 40));
    TEST_ASSERT_EQUAL_UINT16(SMB2_DIALECT_0302, le16(b + 42));

    // The header in front of it is a NEGOTIATE with no session and no tree, per sec 2.2.1.2.
    Smb2Header h;
    Smb2.parse_header_args.buf = buf;
    Smb2.parse_header_args.len = n;
    Smb2.parse_header_args.out = &h;
    Smb2.parse_header(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);
    TEST_ASSERT_EQUAL_INT(SMB2_NEGOTIATE, h.command);
    TEST_ASSERT_EQUAL_HEX64(0, h.session_id);
    TEST_ASSERT_EQUAL_HEX32(0, h.tree_id);

    Smb2.build_negotiate_args.buf = buf;
    Smb2.build_negotiate_args.cap = 107;
    Smb2.build_negotiate_args.client_guid = GUID;
    Smb2.build_negotiate_args.security_mode = 0;
    Smb2.build_negotiate(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.build_negotiate_args.buf = buf;
    Smb2.build_negotiate_args.cap = 108;
    Smb2.build_negotiate_args.client_guid = GUID;
    Smb2.build_negotiate_args.security_mode = 0;
    Smb2.build_negotiate(smb2_work);
    TEST_ASSERT_EQUAL_size_t(108, Smb2.n);
    Smb2.build_negotiate_args.buf = buf;
    Smb2.build_negotiate_args.cap = sizeof(buf);
    Smb2.build_negotiate_args.client_guid = NULL;
    Smb2.build_negotiate_args.security_mode = 0;
    Smb2.build_negotiate(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.build_negotiate_args.buf = NULL;
    Smb2.build_negotiate_args.cap = sizeof(buf);
    Smb2.build_negotiate_args.client_guid = GUID;
    Smb2.build_negotiate_args.security_mode = 0;
    Smb2.build_negotiate(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
}

// MS-SMB2 sec 2.2.4: the NEGOTIATE response body is StructureSize(2) = 65, SecurityMode(2),
// DialectRevision(2), NegotiateContextCount(2), ServerGuid(16), Capabilities(4), MaxTransactSize(4),
// MaxReadSize(4), MaxWriteSize(4), SystemTime(8), ServerStartTime(8), SecurityBufferOffset(2),
// SecurityBufferLength(2), NegotiateContextOffset(4), then the security buffer at its offset.
void test_negotiate_response_parse(void)
{
    static const uint8_t SERVER_GUID[16] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
                                            0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};
    static const uint8_t SEC_BUF[6] = {'N', 'T', 'L', 'M', 0x01, 0x02};

    uint8_t msg[256];
    memset(msg, 0, sizeof(msg));
    Smb2.build_header_args.buf = msg;
    Smb2.build_header_args.cap = sizeof(msg);
    Smb2.build_header_args.command = SMB2_NEGOTIATE;
    Smb2.build_header_args.credit_request = 1;
    Smb2.build_header_args.message_id = 0;
    Smb2.build_header_args.tree_id = 0;
    Smb2.build_header_args.session_id = 0;
    Smb2.build_header(smb2_work);
    TEST_ASSERT_EQUAL_size_t(64, Smb2.n);
    msg[16] = SMB2_FLAGS_SERVER_TO_REDIR; // a response

    uint8_t *b = msg + 64;
    b[0] = 65; // StructureSize
    b[2] = SMB2_NEGOTIATE_SIGNING_ENABLED;
    b[4] = 0x11; // DialectRevision 0x0311
    b[5] = 0x03;
    memcpy(b + 8, SERVER_GUID, 16);
    b[24] = 0x40; // Capabilities = SMB2_GLOBAL_CAP_ENCRYPTION
    b[28] = 0x00; // MaxTransactSize 0x00100000
    b[29] = 0x00;
    b[30] = 0x10;
    b[31] = 0x00;
    b[32] = 0x00; // MaxReadSize 0x00100000
    b[33] = 0x00;
    b[34] = 0x10;
    b[35] = 0x00;
    b[36] = 0x00; // MaxWriteSize 0x00100000
    b[37] = 0x00;
    b[38] = 0x10;
    b[39] = 0x00;
    const uint16_t sec_off = 64 + 64; // SecurityBufferOffset counts from the start of the message
    b[56] = (uint8_t)(sec_off & 0xFF);
    b[57] = (uint8_t)(sec_off >> 8);
    b[58] = (uint8_t)sizeof(SEC_BUF);
    memcpy(msg + sec_off, SEC_BUF, sizeof(SEC_BUF));

    Smb2NegotiateResp r;
    memset(&r, 0xEE, sizeof(r));
    Smb2.parse_negotiate_response_args.msg = msg;
    Smb2.parse_negotiate_response_args.len = sec_off + sizeof(SEC_BUF);
    Smb2.parse_negotiate_response_args.out = &r;
    Smb2.parse_negotiate_response(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);
    TEST_ASSERT_EQUAL_UINT16(SMB2_NEGOTIATE_SIGNING_ENABLED, r.security_mode);
    TEST_ASSERT_EQUAL_UINT16(SMB2_DIALECT_0311, r.dialect);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SERVER_GUID, r.server_guid, 16);
    TEST_ASSERT_EQUAL_HEX32(SMB2_GLOBAL_CAP_ENCRYPTION, r.capabilities);
    TEST_ASSERT_EQUAL_HEX32(0x00100000u, r.max_transact);
    TEST_ASSERT_EQUAL_HEX32(0x00100000u, r.max_read);
    TEST_ASSERT_EQUAL_HEX32(0x00100000u, r.max_write);
    TEST_ASSERT_EQUAL_UINT16(sizeof(SEC_BUF), r.sec_buf_len);
    TEST_ASSERT_EQUAL_PTR(msg + sec_off, r.sec_buf);

    // A StructureSize other than 65 is not a NEGOTIATE response body.
    b[0] = 64;
    Smb2.parse_negotiate_response_args.msg = msg;
    Smb2.parse_negotiate_response_args.len = sec_off + sizeof(SEC_BUF);
    Smb2.parse_negotiate_response_args.out = &r;
    Smb2.parse_negotiate_response(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    b[0] = 65;

    // A security buffer that runs past the message is refused rather than pointed at.
    b[58] = 0xFF;
    b[59] = 0xFF;
    Smb2.parse_negotiate_response_args.msg = msg;
    Smb2.parse_negotiate_response_args.len = sec_off + sizeof(SEC_BUF);
    Smb2.parse_negotiate_response_args.out = &r;
    Smb2.parse_negotiate_response(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    b[58] = (uint8_t)sizeof(SEC_BUF);
    b[59] = 0;

    // A wrong command in the header, and a message too short for the body.
    msg[12] = SMB2_SESSION_SETUP;
    Smb2.parse_negotiate_response_args.msg = msg;
    Smb2.parse_negotiate_response_args.len = sec_off + sizeof(SEC_BUF);
    Smb2.parse_negotiate_response_args.out = &r;
    Smb2.parse_negotiate_response(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    msg[12] = SMB2_NEGOTIATE;
    Smb2.parse_negotiate_response_args.msg = msg;
    Smb2.parse_negotiate_response_args.len = 64;
    Smb2.parse_negotiate_response_args.out = &r;
    Smb2.parse_negotiate_response(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    Smb2.parse_negotiate_response_args.msg = NULL;
    Smb2.parse_negotiate_response_args.len = 128;
    Smb2.parse_negotiate_response_args.out = &r;
    Smb2.parse_negotiate_response(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    Smb2.parse_negotiate_response_args.msg = msg;
    Smb2.parse_negotiate_response_args.len = 128;
    Smb2.parse_negotiate_response_args.out = NULL;
    Smb2.parse_negotiate_response(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
}

// MS-SMB2 sec 3.1.4.1: signing "MUST set the SMB2_FLAGS_SIGNED bit", zero the Signature, MAC the
// whole message and write the MAC into the Signature. sec 3.1.5.1 makes verification recompute it
// over the message with the Signature zeroed - so signing then verifying is an identity, and any
// mutation of the signed bytes breaks it.
void test_signing_round_trip_and_tamper_detection(void)
{
    static const uint8_t KEY[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    uint8_t msg[128];
    memset(msg, 0, sizeof(msg));
    Smb2.build_header_args.buf = msg;
    Smb2.build_header_args.cap = sizeof(msg);
    Smb2.build_header_args.command = SMB2_TREE_CONNECT;
    Smb2.build_header_args.credit_request = 1;
    Smb2.build_header_args.message_id = 42;
    Smb2.build_header_args.tree_id = 7;
    Smb2.build_header_args.session_id = 0xAABBCCDDEEFF0011ull;
    Smb2.build_header(smb2_work);
    for (size_t i = 64; i < sizeof(msg); i++)
    {
        msg[i] = (uint8_t)i;
    }

    uint8_t plain[128];
    memcpy(plain, msg, sizeof(plain));

    Smb2.sign_args.crypto_work = g_work;
    Smb2.sign_args.key = KEY;
    Smb2.sign_args.msg = msg;
    Smb2.sign_args.msg_len = sizeof(msg);
    Smb2.sign(smb2_work);
    TEST_ASSERT_TRUE((le32(msg + 16) & SMB2_FLAGS_SIGNED) != 0);
    TEST_ASSERT_TRUE(memcmp(msg + 48, plain + 48, 16) != 0); // a signature was written
    Smb2.verify_args.crypto_work = g_work;
    Smb2.verify_args.key = KEY;
    Smb2.verify_args.msg = msg;
    Smb2.verify_args.msg_len = sizeof(msg);
    Smb2.verify(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);

    // Verification leaves the message as it found it, so a caller can hand it on.
    uint8_t after[128];
    memcpy(after, msg, sizeof(after));
    Smb2.verify_args.crypto_work = g_work;
    Smb2.verify_args.key = KEY;
    Smb2.verify_args.msg = msg;
    Smb2.verify_args.msg_len = sizeof(msg);
    Smb2.verify(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(after, msg, sizeof(after));

    // A single flipped body bit, a changed header field, and a wrong key all fail.
    msg[70] ^= 0x01;
    Smb2.verify_args.crypto_work = g_work;
    Smb2.verify_args.key = KEY;
    Smb2.verify_args.msg = msg;
    Smb2.verify_args.msg_len = sizeof(msg);
    Smb2.verify(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    msg[70] ^= 0x01;
    Smb2.verify_args.crypto_work = g_work;
    Smb2.verify_args.key = KEY;
    Smb2.verify_args.msg = msg;
    Smb2.verify_args.msg_len = sizeof(msg);
    Smb2.verify(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);

    msg[36] ^= 0x01; // TreeId
    Smb2.verify_args.crypto_work = g_work;
    Smb2.verify_args.key = KEY;
    Smb2.verify_args.msg = msg;
    Smb2.verify_args.msg_len = sizeof(msg);
    Smb2.verify(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    msg[36] ^= 0x01;

    msg[48] ^= 0x80; // the signature itself
    Smb2.verify_args.crypto_work = g_work;
    Smb2.verify_args.key = KEY;
    Smb2.verify_args.msg = msg;
    Smb2.verify_args.msg_len = sizeof(msg);
    Smb2.verify(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    msg[48] ^= 0x80;

    uint8_t other_key[16];
    memcpy(other_key, KEY, sizeof(other_key));
    other_key[0] ^= 0x01;
    Smb2.verify_args.crypto_work = g_work;
    Smb2.verify_args.key = other_key;
    Smb2.verify_args.msg = msg;
    Smb2.verify_args.msg_len = sizeof(msg);
    Smb2.verify(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);

    // A message shorter than the header carries no signature field, so it is left untouched.
    uint8_t stub[32];
    memset(stub, 0x5A, sizeof(stub));
    Smb2.sign_args.crypto_work = g_work;
    Smb2.sign_args.key = KEY;
    Smb2.sign_args.msg = stub;
    Smb2.sign_args.msg_len = sizeof(stub);
    Smb2.sign(smb2_work);
    for (size_t i = 0; i < sizeof(stub); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x5A, stub[i]);
    }
    Smb2.verify_args.crypto_work = g_work;
    Smb2.verify_args.key = KEY;
    Smb2.verify_args.msg = stub;
    Smb2.verify_args.msg_len = sizeof(stub);
    Smb2.verify(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
}

// sec 3.1.4.1 selects AES-CMAC for the SMB 3.x dialects. The framing is the same, so the round trip
// and the tamper detection hold, and a CMAC signature must not verify as an HMAC one.
void test_cmac_signing_is_a_distinct_algorithm(void)
{
    static const uint8_t KEY[16] = {0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08,
                                    0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00};
    uint8_t msg[96];
    memset(msg, 0, sizeof(msg));
    Smb2.build_header_args.buf = msg;
    Smb2.build_header_args.cap = sizeof(msg);
    Smb2.build_header_args.command = SMB2_WRITE;
    Smb2.build_header_args.credit_request = 1;
    Smb2.build_header_args.message_id = 9;
    Smb2.build_header_args.tree_id = 3;
    Smb2.build_header_args.session_id = 0x1122334455667788ull;
    Smb2.build_header(smb2_work);
    for (size_t i = 64; i < sizeof(msg); i++)
    {
        msg[i] = (uint8_t)(i * 3);
    }

    Smb2.sign_cmac_args.crypto_work = g_work;
    Smb2.sign_cmac_args.key = KEY;
    Smb2.sign_cmac_args.msg = msg;
    Smb2.sign_cmac_args.msg_len = sizeof(msg);
    Smb2.sign_cmac(smb2_work);
    TEST_ASSERT_TRUE((le32(msg + 16) & SMB2_FLAGS_SIGNED) != 0);
    Smb2.verify_cmac_args.crypto_work = g_work;
    Smb2.verify_cmac_args.key = KEY;
    Smb2.verify_cmac_args.msg = msg;
    Smb2.verify_cmac_args.msg_len = sizeof(msg);
    Smb2.verify_cmac(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);
    Smb2.verify_args.crypto_work = g_work;
    Smb2.verify_args.key = KEY;
    Smb2.verify_args.msg = msg;
    Smb2.verify_args.msg_len = sizeof(msg);
    Smb2.verify(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);

    msg[80] ^= 0x40;
    Smb2.verify_cmac_args.crypto_work = g_work;
    Smb2.verify_cmac_args.key = KEY;
    Smb2.verify_cmac_args.msg = msg;
    Smb2.verify_cmac_args.msg_len = sizeof(msg);
    Smb2.verify_cmac(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    msg[80] ^= 0x40;
    Smb2.verify_cmac_args.crypto_work = g_work;
    Smb2.verify_cmac_args.key = KEY;
    Smb2.verify_cmac_args.msg = msg;
    Smb2.verify_cmac_args.msg_len = sizeof(msg);
    Smb2.verify_cmac(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);
}

// MS-SMB2 sec 2.2.41: the TRANSFORM_HEADER is ProtocolId(4) = 0xFD 'S' 'M' 'B', Signature(16),
// Nonce(16), OriginalMessageSize(4), Reserved(2), Flags(2), SessionId(8) = 52 octets. sec 3.1.4.3
// gives the AEAD nonce lengths: 12 for the GCM ciphers, 11 for the CCM ciphers, written into the
// leading octets of the 16-octet Nonce field.
void test_transform_header_constants(void)
{
    TEST_ASSERT_EQUAL_INT(52, PROTOCORE_SMB2_TRANSFORM_HDR_LEN);
    TEST_ASSERT_EQUAL_HEX32(0x424D53FDu, PROTOCORE_SMB2_TRANSFORM_PROTOCOL_ID);
    TEST_ASSERT_EQUAL_INT(16, PROTOCORE_SMB2_NONCE_FIELD_LEN);
    TEST_ASSERT_EQUAL_INT(12, PROTOCORE_SMB2_GCM_NONCE_LEN);
    TEST_ASSERT_EQUAL_INT(11, PROTOCORE_SMB2_CCM_NONCE_LEN);
    TEST_ASSERT_EQUAL_INT(32, PROTOCORE_SMB2_MAX_CIPHER_KEY_LEN);

    // 4 + 16 + 16 + 4 + 2 + 2 + 8 = 52, the sum sec 2.2.41 lists.
    TEST_ASSERT_EQUAL_INT(52, 4 + 16 + 16 + 4 + 2 + 2 + 8);

    // sec 2.2.3.1.2's cipher ids fix the AES key width and, with sec 3.1.4.3, the nonce width.
    TEST_ASSERT_EQUAL_size_t(16, protocore_smb2_cipher_key_len(SMB2_ENCRYPTION_AES128_CCM));
    TEST_ASSERT_EQUAL_size_t(16, protocore_smb2_cipher_key_len(SMB2_ENCRYPTION_AES128_GCM));
    TEST_ASSERT_EQUAL_size_t(32, protocore_smb2_cipher_key_len(SMB2_ENCRYPTION_AES256_CCM));
    TEST_ASSERT_EQUAL_size_t(32, protocore_smb2_cipher_key_len(SMB2_ENCRYPTION_AES256_GCM));
    TEST_ASSERT_EQUAL_size_t(0, protocore_smb2_cipher_key_len(0x0000));
    TEST_ASSERT_EQUAL_size_t(0, protocore_smb2_cipher_key_len(0x0005));

    TEST_ASSERT_EQUAL_size_t(11, protocore_smb2_cipher_nonce_len(SMB2_ENCRYPTION_AES128_CCM));
    TEST_ASSERT_EQUAL_size_t(12, protocore_smb2_cipher_nonce_len(SMB2_ENCRYPTION_AES128_GCM));
    TEST_ASSERT_EQUAL_size_t(11, protocore_smb2_cipher_nonce_len(SMB2_ENCRYPTION_AES256_CCM));
    TEST_ASSERT_EQUAL_size_t(12, protocore_smb2_cipher_nonce_len(SMB2_ENCRYPTION_AES256_GCM));
    TEST_ASSERT_EQUAL_size_t(0, protocore_smb2_cipher_nonce_len(0x0000));
}

// sec 3.1.4.3 / 3.1.4.4: encryption wraps the message in a TRANSFORM_HEADER carrying the AEAD tag
// in Signature and the plaintext length in OriginalMessageSize; decryption verifies the tag before
// exposing anything. Every negotiated cipher must round-trip, and a tampered blob must not decrypt.
void test_transform_round_trip_for_every_cipher(void)
{
    static const uint16_t CIPHERS[4] = {SMB2_ENCRYPTION_AES128_CCM, SMB2_ENCRYPTION_AES128_GCM,
                                        SMB2_ENCRYPTION_AES256_CCM, SMB2_ENCRYPTION_AES256_GCM};
    uint8_t key[PROTOCORE_SMB2_MAX_CIPHER_KEY_LEN];
    uint8_t nonce[PROTOCORE_SMB2_NONCE_FIELD_LEN];
    uint8_t msg[80];
    uint8_t blob[256];
    uint8_t back[256];

    memset(key, 0x2B, sizeof(key));
    memset(nonce, 0, sizeof(nonce));
    nonce[0] = 0x01;
    memset(msg, 0, sizeof(msg));
    Smb2.build_header_args.buf = msg;
    Smb2.build_header_args.cap = sizeof(msg);
    Smb2.build_header_args.command = SMB2_READ;
    Smb2.build_header_args.credit_request = 1;
    Smb2.build_header_args.message_id = 5;
    Smb2.build_header_args.tree_id = 2;
    Smb2.build_header_args.session_id = 0x0102030405060708ull;
    Smb2.build_header(smb2_work);
    for (size_t i = 64; i < sizeof(msg); i++)
    {
        msg[i] = (uint8_t)(i ^ 0x5A);
    }

    for (size_t c = 0; c < 4; c++)
    {
        const uint16_t cipher = CIPHERS[c];
        Smb2.encrypt_args.cipher = cipher;
        Smb2.encrypt_args.key = key;
        Smb2.encrypt_args.nonce = nonce;
        Smb2.encrypt_args.session_id = 0x0102030405060708ull;
        Smb2.encrypt_args.msg = msg;
        Smb2.encrypt_args.msg_len = sizeof(msg);
        Smb2.encrypt_args.out = blob;
        Smb2.encrypt_args.out_cap = sizeof(blob);
        Smb2.encrypt(smb2_work);
        size_t n = Smb2.n;
        TEST_ASSERT_EQUAL_size_t(PROTOCORE_SMB2_TRANSFORM_HDR_LEN + sizeof(msg), n);

        // The header sec 2.2.41 describes, at its own offsets.
        TEST_ASSERT_EQUAL_HEX32(PROTOCORE_SMB2_TRANSFORM_PROTOCOL_ID, le32(blob));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(nonce, blob + 20, PROTOCORE_SMB2_NONCE_FIELD_LEN);
        TEST_ASSERT_EQUAL_UINT32(sizeof(msg), le32(blob + 36));
        TEST_ASSERT_EQUAL_UINT16(0x0001, le16(blob + 42)); // Flags: Encrypted
        TEST_ASSERT_EQUAL_HEX64(0x0102030405060708ull, le64(blob + 44));
        // The plaintext is not in the blob.
        TEST_ASSERT_TRUE(memcmp(blob + PROTOCORE_SMB2_TRANSFORM_HDR_LEN, msg, sizeof(msg)) != 0);

        Smb2.decrypt_args.cipher = cipher;
        Smb2.decrypt_args.key = key;
        Smb2.decrypt_args.in = blob;
        Smb2.decrypt_args.in_len = n;
        Smb2.decrypt_args.out = back;
        Smb2.decrypt_args.out_cap = sizeof(back);
        Smb2.decrypt(smb2_work);
        size_t m = Smb2.n;
        TEST_ASSERT_EQUAL_size_t(sizeof(msg), m);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, back, sizeof(msg));

        // A flipped ciphertext bit, a flipped tag bit and a flipped AAD bit each fail the tag check.
        blob[PROTOCORE_SMB2_TRANSFORM_HDR_LEN] ^= 0x01;
        Smb2.decrypt_args.cipher = cipher;
        Smb2.decrypt_args.key = key;
        Smb2.decrypt_args.in = blob;
        Smb2.decrypt_args.in_len = n;
        Smb2.decrypt_args.out = back;
        Smb2.decrypt_args.out_cap = sizeof(back);
        Smb2.decrypt(smb2_work);
        TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
        blob[PROTOCORE_SMB2_TRANSFORM_HDR_LEN] ^= 0x01;

        blob[4] ^= 0x01; // Signature (the AEAD tag)
        Smb2.decrypt_args.cipher = cipher;
        Smb2.decrypt_args.key = key;
        Smb2.decrypt_args.in = blob;
        Smb2.decrypt_args.in_len = n;
        Smb2.decrypt_args.out = back;
        Smb2.decrypt_args.out_cap = sizeof(back);
        Smb2.decrypt(smb2_work);
        TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
        blob[4] ^= 0x01;

        blob[44] ^= 0x01; // SessionId, which is inside the AAD
        Smb2.decrypt_args.cipher = cipher;
        Smb2.decrypt_args.key = key;
        Smb2.decrypt_args.in = blob;
        Smb2.decrypt_args.in_len = n;
        Smb2.decrypt_args.out = back;
        Smb2.decrypt_args.out_cap = sizeof(back);
        Smb2.decrypt(smb2_work);
        TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
        blob[44] ^= 0x01;

        blob[0] ^= 0x01; // ProtocolId
        Smb2.decrypt_args.cipher = cipher;
        Smb2.decrypt_args.key = key;
        Smb2.decrypt_args.in = blob;
        Smb2.decrypt_args.in_len = n;
        Smb2.decrypt_args.out = back;
        Smb2.decrypt_args.out_cap = sizeof(back);
        Smb2.decrypt(smb2_work);
        TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
        blob[0] ^= 0x01;

        // The right blob under the wrong cipher is not decryptable either.
        Smb2.decrypt_args.cipher = CIPHERS[(c + 1) % 4];
        Smb2.decrypt_args.key = key;
        Smb2.decrypt_args.in = blob;
        Smb2.decrypt_args.in_len = n;
        Smb2.decrypt_args.out = back;
        Smb2.decrypt_args.out_cap = sizeof(back);
        Smb2.decrypt(smb2_work);
        TEST_ASSERT_EQUAL_size_t(0, Smb2.n);

        Smb2.decrypt_args.cipher = cipher;
        Smb2.decrypt_args.key = key;
        Smb2.decrypt_args.in = blob;
        Smb2.decrypt_args.in_len = n;
        Smb2.decrypt_args.out = back;
        Smb2.decrypt_args.out_cap = sizeof(back);
        Smb2.decrypt(smb2_work);
        TEST_ASSERT_EQUAL_size_t(m, Smb2.n);
    }
}

// An unrecognized cipher id, a null pointer and a destination too small are all refusals: nothing
// is written and no plaintext is exposed.
void test_transform_fails_closed(void)
{
    uint8_t key[PROTOCORE_SMB2_MAX_CIPHER_KEY_LEN];
    uint8_t nonce[PROTOCORE_SMB2_NONCE_FIELD_LEN];
    uint8_t msg[16];
    uint8_t blob[128];
    uint8_t back[128];
    memset(key, 0x11, sizeof(key));
    memset(nonce, 0x22, sizeof(nonce));
    memset(msg, 0x33, sizeof(msg));

    Smb2.encrypt_args.cipher = 0x0099;
    Smb2.encrypt_args.key = key;
    Smb2.encrypt_args.nonce = nonce;
    Smb2.encrypt_args.session_id = 0;
    Smb2.encrypt_args.msg = msg;
    Smb2.encrypt_args.msg_len = sizeof(msg);
    Smb2.encrypt_args.out = blob;
    Smb2.encrypt_args.out_cap = sizeof(blob);
    Smb2.encrypt(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.encrypt_args.cipher = SMB2_ENCRYPTION_AES128_GCM;
    Smb2.encrypt_args.key = NULL;
    Smb2.encrypt_args.nonce = nonce;
    Smb2.encrypt_args.session_id = 0;
    Smb2.encrypt_args.msg = msg;
    Smb2.encrypt_args.msg_len = sizeof(msg);
    Smb2.encrypt_args.out = blob;
    Smb2.encrypt_args.out_cap = sizeof(blob);
    Smb2.encrypt(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.encrypt_args.cipher = SMB2_ENCRYPTION_AES128_GCM;
    Smb2.encrypt_args.key = key;
    Smb2.encrypt_args.nonce = nonce;
    Smb2.encrypt_args.session_id = 0;
    Smb2.encrypt_args.msg = msg;
    Smb2.encrypt_args.msg_len = sizeof(msg);
    Smb2.encrypt_args.out = blob;
    Smb2.encrypt_args.out_cap = PROTOCORE_SMB2_TRANSFORM_HDR_LEN + sizeof(msg) - 1;
    Smb2.encrypt(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.encrypt_args.cipher = SMB2_ENCRYPTION_AES128_GCM;
    Smb2.encrypt_args.key = key;
    Smb2.encrypt_args.nonce = nonce;
    Smb2.encrypt_args.session_id = 0;
    Smb2.encrypt_args.msg = msg;
    Smb2.encrypt_args.msg_len = sizeof(msg);
    Smb2.encrypt_args.out = blob;
    Smb2.encrypt_args.out_cap = PROTOCORE_SMB2_TRANSFORM_HDR_LEN + sizeof(msg);
    Smb2.encrypt(smb2_work);
    size_t n = Smb2.n;
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_SMB2_TRANSFORM_HDR_LEN + sizeof(msg), n);

    Smb2.decrypt_args.cipher = 0x0099;
    Smb2.decrypt_args.key = key;
    Smb2.decrypt_args.in = blob;
    Smb2.decrypt_args.in_len = n;
    Smb2.decrypt_args.out = back;
    Smb2.decrypt_args.out_cap = sizeof(back);
    Smb2.decrypt(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.decrypt_args.cipher = SMB2_ENCRYPTION_AES128_GCM;
    Smb2.decrypt_args.key = key;
    Smb2.decrypt_args.in = blob;
    Smb2.decrypt_args.in_len = PROTOCORE_SMB2_TRANSFORM_HDR_LEN - 1;
    Smb2.decrypt_args.out = back;
    Smb2.decrypt_args.out_cap = sizeof(back);
    Smb2.decrypt(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.decrypt_args.cipher = SMB2_ENCRYPTION_AES128_GCM;
    Smb2.decrypt_args.key = key;
    Smb2.decrypt_args.in = NULL;
    Smb2.decrypt_args.in_len = n;
    Smb2.decrypt_args.out = back;
    Smb2.decrypt_args.out_cap = sizeof(back);
    Smb2.decrypt(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.decrypt_args.cipher = SMB2_ENCRYPTION_AES128_GCM;
    Smb2.decrypt_args.key = key;
    Smb2.decrypt_args.in = blob;
    Smb2.decrypt_args.in_len = n;
    Smb2.decrypt_args.out = back;
    Smb2.decrypt_args.out_cap = sizeof(msg) - 1;
    Smb2.decrypt(smb2_work);
    TEST_ASSERT_EQUAL_size_t(0, Smb2.n);
    Smb2.decrypt_args.cipher = SMB2_ENCRYPTION_AES128_GCM;
    Smb2.decrypt_args.key = key;
    Smb2.decrypt_args.in = blob;
    Smb2.decrypt_args.in_len = n;
    Smb2.decrypt_args.out = back;
    Smb2.decrypt_args.out_cap = sizeof(msg);
    Smb2.decrypt(smb2_work);
    TEST_ASSERT_EQUAL_size_t(sizeof(msg), Smb2.n);
}

// sec 3.1.4.2 derives the signing and cipher keys from the session key. Different labels must give
// different keys, and the 3.1.1 derivations must be bound to the preauth hash, so a changed preauth
// value changes every key it feeds.
void test_key_derivation_separates_its_outputs(void)
{
    static const uint8_t SESSION_KEY[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                            0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    uint8_t preauth[PROTOCORE_SMB2_PREAUTH_HASH_LEN];
    memset(preauth, 0x77, sizeof(preauth));

    uint8_t sign_311[16];
    uint8_t sign_300[16];
    Smb2.derive_signing_key_args.session_key = SESSION_KEY;
    Smb2.derive_signing_key_args.dialect = SMB2_DIALECT_0311;
    Smb2.derive_signing_key_args.preauth = preauth;
    Smb2.derive_signing_key_args.out_key = sign_311;
    Smb2.derive_signing_key(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);
    Smb2.derive_signing_key_args.session_key = SESSION_KEY;
    Smb2.derive_signing_key_args.dialect = SMB2_DIALECT_0300;
    Smb2.derive_signing_key_args.preauth = NULL;
    Smb2.derive_signing_key_args.out_key = sign_300;
    Smb2.derive_signing_key(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);
    TEST_ASSERT_TRUE(memcmp(sign_311, sign_300, 16) != 0); // different labels, different keys

    // 3.1.1 requires the preauth hash: without it there is nothing to bind the key to.
    uint8_t tmp[16];
    Smb2.derive_signing_key_args.session_key = SESSION_KEY;
    Smb2.derive_signing_key_args.dialect = SMB2_DIALECT_0311;
    Smb2.derive_signing_key_args.preauth = NULL;
    Smb2.derive_signing_key_args.out_key = tmp;
    Smb2.derive_signing_key(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    Smb2.derive_signing_key_args.session_key = NULL;
    Smb2.derive_signing_key_args.dialect = SMB2_DIALECT_0300;
    Smb2.derive_signing_key_args.preauth = NULL;
    Smb2.derive_signing_key_args.out_key = tmp;
    Smb2.derive_signing_key(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);

    // A different preauth hash gives a different signing key.
    uint8_t other_preauth[PROTOCORE_SMB2_PREAUTH_HASH_LEN];
    memcpy(other_preauth, preauth, sizeof(other_preauth));
    other_preauth[0] ^= 0x01;
    Smb2.derive_signing_key_args.session_key = SESSION_KEY;
    Smb2.derive_signing_key_args.dialect = SMB2_DIALECT_0311;
    Smb2.derive_signing_key_args.preauth = other_preauth;
    Smb2.derive_signing_key_args.out_key = tmp;
    Smb2.derive_signing_key(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);
    TEST_ASSERT_TRUE(memcmp(sign_311, tmp, 16) != 0);

    // The two directions of the cipher keys differ from each other and from the signing key.
    uint8_t c2s[PROTOCORE_SMB2_MAX_CIPHER_KEY_LEN];
    uint8_t s2c[PROTOCORE_SMB2_MAX_CIPHER_KEY_LEN];
    Smb2.derive_encryption_keys_args.session_key = SESSION_KEY;
    Smb2.derive_encryption_keys_args.dialect = SMB2_DIALECT_0311;
    Smb2.derive_encryption_keys_args.preauth = preauth;
    Smb2.derive_encryption_keys_args.key_len = 16;
    Smb2.derive_encryption_keys_args.out_c2s = c2s;
    Smb2.derive_encryption_keys_args.out_s2c = s2c;
    Smb2.derive_encryption_keys(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);
    TEST_ASSERT_TRUE(memcmp(c2s, s2c, 16) != 0);
    TEST_ASSERT_TRUE(memcmp(c2s, sign_311, 16) != 0);
    TEST_ASSERT_TRUE(memcmp(s2c, sign_311, 16) != 0);

    // A 256-bit request yields a different key than the 128-bit one, since [L] is in the KDF input.
    uint8_t c2s256[PROTOCORE_SMB2_MAX_CIPHER_KEY_LEN];
    uint8_t s2c256[PROTOCORE_SMB2_MAX_CIPHER_KEY_LEN];
    Smb2.derive_encryption_keys_args.session_key = SESSION_KEY;
    Smb2.derive_encryption_keys_args.dialect = SMB2_DIALECT_0311;
    Smb2.derive_encryption_keys_args.preauth = preauth;
    Smb2.derive_encryption_keys_args.key_len = 32;
    Smb2.derive_encryption_keys_args.out_c2s = c2s256;
    Smb2.derive_encryption_keys_args.out_s2c = s2c256;
    Smb2.derive_encryption_keys(smb2_work);
    TEST_ASSERT_TRUE(Smb2.ok);
    TEST_ASSERT_TRUE(memcmp(c2s, c2s256, 16) != 0);

    Smb2.derive_encryption_keys_args.session_key = SESSION_KEY;
    Smb2.derive_encryption_keys_args.dialect = SMB2_DIALECT_0311;
    Smb2.derive_encryption_keys_args.preauth = NULL;
    Smb2.derive_encryption_keys_args.key_len = 16;
    Smb2.derive_encryption_keys_args.out_c2s = c2s;
    Smb2.derive_encryption_keys_args.out_s2c = s2c;
    Smb2.derive_encryption_keys(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    Smb2.derive_encryption_keys_args.session_key = SESSION_KEY;
    Smb2.derive_encryption_keys_args.dialect = SMB2_DIALECT_0311;
    Smb2.derive_encryption_keys_args.preauth = preauth;
    Smb2.derive_encryption_keys_args.key_len = 24;
    Smb2.derive_encryption_keys_args.out_c2s = c2s;
    Smb2.derive_encryption_keys_args.out_s2c = s2c;
    Smb2.derive_encryption_keys(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
    Smb2.derive_encryption_keys_args.session_key = NULL;
    Smb2.derive_encryption_keys_args.dialect = SMB2_DIALECT_0300;
    Smb2.derive_encryption_keys_args.preauth = NULL;
    Smb2.derive_encryption_keys_args.key_len = 16;
    Smb2.derive_encryption_keys_args.out_c2s = c2s;
    Smb2.derive_encryption_keys_args.out_s2c = s2c;
    Smb2.derive_encryption_keys(smb2_work);
    TEST_ASSERT_FALSE(Smb2.ok);
}
