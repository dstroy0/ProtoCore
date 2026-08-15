// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the VXI-11 codec over ONC RPC / XDR (services/instrumentation/vxi11/vxi11.h).
//
// The load-bearing case is test_destroy_link_call_is_byte_exact. Three specifications stack to fix
// every octet of it and none of them is this library's: RFC 5531 sec 9 gives call_body as rpcvers,
// prog, vers, proc, cred, verf in that order with rpcvers "MUST be equal to two (2)"; RFC 5531 sec
// 11 makes the record mark "a 31-bit unsigned binary value that is the length" with the high bit set
// on the last fragment; and the VXI-11 RPCL fixes program 0x0607AF version 1 and destroy_link at
// procedure 23. Every field is a 4-byte big-endian XDR word, so a single misordered or misaligned
// word shifts the whole call and the instrument answers GARBAGE_ARGS.
//
// The other calls are checked the same way against the RPCL's own parameter order, and the reply
// parsers against RFC 5531's accepted_reply (verf then accept_stat then the results).

#include "services/instrumentation/vxi11/vxi11.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Write one big-endian XDR word into a hand-built buffer.
static size_t put(uint8_t *p, size_t o, uint32_t v)
{
    p[o] = (uint8_t)(v >> 24);
    p[o + 1] = (uint8_t)(v >> 16);
    p[o + 2] = (uint8_t)(v >> 8);
    p[o + 3] = (uint8_t)v;
    return o + 4;
}

// RFC 5531 sec 9: an accepted reply is xid, REPLY(1), MSG_ACCEPTED(0), then the verifier
// (opaque_auth: flavor + counted body), then accept_stat, then the procedure results.
static size_t reply_head(uint8_t *p, uint32_t xid, uint32_t accept_stat)
{
    size_t o = 0;
    o = put(p, o, xid);
    o = put(p, o, 1); // msg_type REPLY
    o = put(p, o, 0); // reply_stat MSG_ACCEPTED
    o = put(p, o, 0); // verf.flavor AUTH_NONE
    o = put(p, o, 0); // verf.body length
    o = put(p, o, accept_stat);
    return o;
}

// A destroy_link call, every octet derived from the three specifications above:
//
//   0  80 00 00 2C   record mark: last fragment + 44 octets follow  (RFC 5531 sec 11)
//   4  DE AD BE EF   xid
//   8  00 00 00 00   msg_type CALL                                  (RFC 5531 sec 9)
//  12  00 00 00 02   rpcvers, "MUST be equal to two (2)"
//  16  00 06 07 AF   prog   DEVICE_CORE
//  20  00 00 00 01   vers   DEVICE_CORE_VERSION
//  24  00 00 00 17   proc   destroy_link = 23
//  28  00 00 00 00   cred.flavor  AUTH_NONE
//  32  00 00 00 00   cred.length
//  36  00 00 00 00   verf.flavor  AUTH_NONE
//  40  00 00 00 00   verf.length
//  44  00 00 00 07   Device_Link lid
//
// 48 octets total, 44 after the mark.
void test_destroy_link_call_is_byte_exact(void)
{
    static const uint8_t WANT[48] = {0x80, 0x00, 0x00, 0x2C, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x02, 0x00, 0x06, 0x07, 0xAF, 0x00, 0x00, 0x00, 0x01,
                                     0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07};
    uint8_t buf[64];
    size_t n = protocore_vxi11_build_destroy_link(buf, sizeof(buf), 0xDEADBEEFu, 7);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));
}

// RFC 1833 sec 3: "const PMAP_PORT = 111", program 100000 version 2, "PMAPPROC_GETPORT(mapping) =
// 3", and the mapping struct is prog, vers, prot, port. RFC 1833 sec 2 gives IPPROTO_TCP as 6.
//
// The call is the 44-octet header above with prog 100000 (0x000186A0), vers 2, proc 3, followed by
// the four mapping words, so 60 octets with 56 after the mark (0x38).
void test_rfc1833_getport_call_is_byte_exact(void)
{
    static const uint8_t WANT[60] = {0x80, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x86, 0xA0, 0x00, 0x00, 0x00, 0x02,
                                     0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x07, 0xAF,
                                     0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00};
    uint8_t buf[80];
    size_t n = protocore_vxi11_build_getport(buf, sizeof(buf), 1, PROTOCORE_VXI11_CORE_PROG, PROTOCORE_VXI11_CORE_VERS,
                                             PROTOCORE_RPC_PROTO_TCP);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));

    TEST_ASSERT_EQUAL_HEX32(0x0607AFu, PROTOCORE_VXI11_CORE_PROG);
    TEST_ASSERT_EQUAL_UINT(1u, PROTOCORE_VXI11_CORE_VERS);
    TEST_ASSERT_EQUAL_UINT(100000u, PROTOCORE_RPC_PMAP_PROG);
    TEST_ASSERT_EQUAL_UINT(2u, PROTOCORE_RPC_PMAP_VERS);
    TEST_ASSERT_EQUAL_UINT(111u, PROTOCORE_RPC_PMAP_PORT);
    TEST_ASSERT_EQUAL_UINT(3u, PROTOCORE_RPC_PMAP_GETPORT);
    TEST_ASSERT_EQUAL_UINT(6u, PROTOCORE_RPC_PROTO_TCP);
}

// RFC 5531 sec 11: "a 31-bit unsigned binary value that is the length" with the high bit set when
// the fragment is the last of the record. A length needing the 32nd bit has no record mark.
void test_rfc5531_record_marking(void)
{
    uint8_t buf[8];
    proto_bool last = PROTO_FALSE;
    uint32_t frag = 0;

    TEST_ASSERT_EQUAL_UINT(4u, protocore_rpc_record_mark(buf, sizeof(buf), 0x2C));
    static const uint8_t WANT[4] = {0x80, 0x00, 0x00, 0x2C};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 4);
    TEST_ASSERT_TRUE(protocore_rpc_parse_record_mark(buf, 4, &last, &frag));
    TEST_ASSERT_TRUE(last);
    TEST_ASSERT_EQUAL_UINT32(0x2Cu, frag);

    // the largest length the 31-bit field can name
    TEST_ASSERT_EQUAL_UINT(4u, protocore_rpc_record_mark(buf, sizeof(buf), 0x7FFFFFFFu));
    TEST_ASSERT_TRUE(protocore_rpc_parse_record_mark(buf, 4, &last, &frag));
    TEST_ASSERT_EQUAL_UINT32(0x7FFFFFFFu, frag);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_rpc_record_mark(buf, sizeof(buf), 0x80000000u));

    // a fragment with the flag clear is not the last one
    static const uint8_t NOT_LAST[4] = {0x00, 0x00, 0x01, 0x00};
    TEST_ASSERT_TRUE(protocore_rpc_parse_record_mark(NOT_LAST, 4, &last, &frag));
    TEST_ASSERT_FALSE(last);
    TEST_ASSERT_EQUAL_UINT32(256u, frag);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_rpc_record_mark(NULL, 4, 4));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_rpc_record_mark(buf, 3, 4));
    TEST_ASSERT_FALSE(protocore_rpc_parse_record_mark(buf, 3, &last, &frag));
    TEST_ASSERT_FALSE(protocore_rpc_parse_record_mark(NULL, 4, &last, &frag));
}

// RFC 4506 sec 4.10: a variable-length opaque is a 4-byte count, the bytes, then "residual zero
// bytes, r, to make the total byte count a multiple of four". "inst0" is five octets, so its
// encoding is 4 + 5 + 3 pad = 12.
//
// A create_link call is the 44-octet header plus clientId, lockDevice, lock_timeout and that
// string: 44 + 12 + 12 = 68, so 64 (0x40) after the mark.
void test_create_link_call_pads_the_device_string(void)
{
    static const uint8_t WANT[68] = {
        0x80, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x06, 0x07, 0xAF, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // header, proc 10
        0x00, 0x00, 0x04, 0xD2,                                                             // clientId 1234
        0x00, 0x00, 0x00, 0x00,                                                             // lockDevice false
        0x00, 0x00, 0x27, 0x10,                                                             // lock_timeout 10000
        0x00, 0x00, 0x00, 0x05, 'i',  'n',  's',  't',  '0',  0x00, 0x00, 0x00};            // "inst0" + 3 pad
    uint8_t buf[96];
    size_t n = protocore_vxi11_build_create_link(buf, sizeof(buf), 2, 1234, PROTO_FALSE, 10000, "inst0");
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));
}

// The RPCL gives Device_WriteParms as lid, io_timeout, lock_timeout, flags, data. "*IDN?\n" is six
// octets, so its opaque encoding is 4 + 6 + 2 pad = 12 and the call is 44 + 16 + 12 = 72.
void test_device_write_parameter_order(void)
{
    static const uint8_t SCPI[] = {'*', 'I', 'D', 'N', '?', '\n'};
    uint8_t buf[96];
    size_t n = protocore_vxi11_build_device_write(buf, sizeof(buf), 3, 7, 2000, 10000, PROTOCORE_VXI11_FLAG_END, SCPI,
                                                  sizeof(SCPI));
    TEST_ASSERT_EQUAL_UINT(72u, n);

    size_t o = 44;
    static const uint8_t LID[4] = {0, 0, 0, 7};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(LID, buf + o, 4);
    static const uint8_t IO_TMO[4] = {0, 0, 0x07, 0xD0}; // 2000
    TEST_ASSERT_EQUAL_HEX8_ARRAY(IO_TMO, buf + o + 4, 4);
    static const uint8_t LOCK_TMO[4] = {0, 0, 0x27, 0x10}; // 10000
    TEST_ASSERT_EQUAL_HEX8_ARRAY(LOCK_TMO, buf + o + 8, 4);
    static const uint8_t FLAGS[4] = {0, 0, 0, 0x08}; // END
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FLAGS, buf + o + 12, 4);
    static const uint8_t DATA[12] = {0, 0, 0, 6, '*', 'I', 'D', 'N', '?', '\n', 0, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, buf + o + 16, 12);

    // the proc word says device_write = 11
    static const uint8_t PROC[4] = {0, 0, 0, 11};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PROC, buf + 24, 4);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_vxi11_build_device_write(buf, sizeof(buf), 3, 7, 0, 0, 0, NULL, 4));
}

// The RPCL gives Device_ReadParms as lid, requestSize, io_timeout, lock_timeout, flags, termChar,
// and XDR has no sub-word type, so the char occupies a whole 4-byte word.
void test_device_read_parameter_order(void)
{
    uint8_t buf[96];
    size_t n = protocore_vxi11_build_device_read(buf, sizeof(buf), 4, 7, 4096, 2000, 10000,
                                                 PROTOCORE_VXI11_FLAG_TERMCHRSET, '\n');
    TEST_ASSERT_EQUAL_UINT(44u + 24u, n);

    static const uint8_t PARAMS[24] = {0, 0, 0,    7,     // lid
                                       0, 0, 0x10, 0x00,  // requestSize 4096
                                       0, 0, 0x07, 0xD0,  // io_timeout 2000
                                       0, 0, 0x27, 0x10,  // lock_timeout 10000
                                       0, 0, 0,    0x80,  // flags TERMCHRSET
                                       0, 0, 0,    0x0A}; // termChar, one full XDR word
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PARAMS, buf + 44, 24);
    static const uint8_t PROC[4] = {0, 0, 0, 12};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PROC, buf + 24, 4);
}

// The RPCL gives Device_GenericParms as lid, flags, lock_timeout, io_timeout - a different order
// from Device_WriteParms - and readstb, trigger and clear all take it. Their proc numbers are 13,
// 14 and 15.
void test_generic_parms_calls_share_a_layout(void)
{
    static const uint8_t PARAMS[16] = {0, 0, 0,    7,     // lid
                                       0, 0, 0,    0x01,  // flags WAITLOCK
                                       0, 0, 0x27, 0x10,  // lock_timeout 10000
                                       0, 0, 0x07, 0xD0}; // io_timeout 2000
    static const struct
    {
        size_t (*build)(uint8_t *, size_t, uint32_t, int32_t, uint32_t, uint32_t, uint32_t);
        uint8_t proc;
    } CASES[] = {
        {protocore_vxi11_build_device_readstb, 13},
        {protocore_vxi11_build_device_trigger, 14},
        {protocore_vxi11_build_device_clear, 15},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t buf[96];
        size_t n = CASES[i].build(buf, sizeof(buf), 5, 7, PROTOCORE_VXI11_FLAG_WAITLOCK, 10000, 2000);
        TEST_ASSERT_EQUAL_UINT(60u, n);
        TEST_ASSERT_EQUAL_HEX8(CASES[i].proc, buf[27]);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(PARAMS, buf + 44, 16);
    }
}

// RFC 5531 sec 9: accepted_reply is the verifier then the accept_stat then the results, and the
// results offset is where a procedure's own decoding starts.
void test_rfc5531_accepted_reply_header(void)
{
    uint8_t rpc[64];
    size_t o = reply_head(rpc, 0xDEADBEEFu, PROTOCORE_RPC_ACCEPT_SUCCESS);
    TEST_ASSERT_EQUAL_UINT(24u, o);

    uint32_t xid = 0;
    uint32_t astat = 0xFF;
    size_t off = 0;
    TEST_ASSERT_TRUE(protocore_rpc_parse_reply(rpc, o, &xid, &astat, &off));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, xid);
    TEST_ASSERT_EQUAL_UINT32(0u, astat);
    TEST_ASSERT_EQUAL_UINT(24u, off);

    // MSG_DENIED (reply_stat 1) is not an accepted reply
    put(rpc, 8, 1);
    TEST_ASSERT_FALSE(protocore_rpc_parse_reply(rpc, o, &xid, &astat, &off));
    put(rpc, 8, 0);

    // a CALL is not a REPLY
    put(rpc, 4, 0);
    TEST_ASSERT_FALSE(protocore_rpc_parse_reply(rpc, o, &xid, &astat, &off));
    put(rpc, 4, 1);
    TEST_ASSERT_TRUE(protocore_rpc_parse_reply(rpc, o, &xid, &astat, &off));

    // every prefix short of the whole header is refused rather than read past
    for (size_t shorter = 0; shorter < o; shorter++)
    {
        TEST_ASSERT_FALSE(protocore_rpc_parse_reply(rpc, shorter, &xid, &astat, &off));
    }
    TEST_ASSERT_FALSE(protocore_rpc_parse_reply(NULL, o, &xid, &astat, &off));
}

// An accept_stat other than SUCCESS carries no results, so every result parser refuses it rather
// than decoding whatever follows.
void test_a_non_success_accept_stat_yields_no_results(void)
{
    uint8_t rpc[64];
    size_t o = reply_head(rpc, 1, 1); // PROG_UNAVAIL
    o = put(rpc, o, 0);
    Vxi11CreateLinkResp cl;
    Vxi11WriteResp wr;
    Vxi11ReadResp rd;
    Vxi11ReadStbResp stb;
    int32_t err = 0;
    uint32_t port = 0;
    TEST_ASSERT_FALSE(protocore_vxi11_parse_create_link_resp(rpc, o, &cl));
    TEST_ASSERT_FALSE(protocore_vxi11_parse_write_resp(rpc, o, &wr));
    TEST_ASSERT_FALSE(protocore_vxi11_parse_read_resp(rpc, o, &rd));
    TEST_ASSERT_FALSE(protocore_vxi11_parse_readstb_resp(rpc, o, &stb));
    TEST_ASSERT_FALSE(protocore_vxi11_parse_error_resp(rpc, o, &err));
    TEST_ASSERT_FALSE(protocore_vxi11_parse_getport_resp(rpc, o, &port));
}

// RFC 1833 sec 3: GETPORT returns "the port number ... a port value of zeros means the program has
// not been registered".
void test_getport_reply(void)
{
    uint8_t rpc[64];
    uint32_t port = 0xFFFF;
    size_t o = reply_head(rpc, 1, PROTOCORE_RPC_ACCEPT_SUCCESS);
    o = put(rpc, o, 9009);
    TEST_ASSERT_TRUE(protocore_vxi11_parse_getport_resp(rpc, o, &port));
    TEST_ASSERT_EQUAL_UINT32(9009u, port);

    o = reply_head(rpc, 1, PROTOCORE_RPC_ACCEPT_SUCCESS);
    o = put(rpc, o, 0);
    TEST_ASSERT_TRUE(protocore_vxi11_parse_getport_resp(rpc, o, &port));
    TEST_ASSERT_EQUAL_UINT32(0u, port); // not registered

    TEST_ASSERT_FALSE(protocore_vxi11_parse_getport_resp(rpc, o - 1, &port)); // a truncated word
}

// The RPCL gives Create_LinkResp as error, lid, abortPort, maxRecvSize - and XDR has no sub-word
// type, so the `unsigned short` abortPort still occupies a whole word.
void test_create_link_reply(void)
{
    uint8_t rpc[64];
    Vxi11CreateLinkResp r;
    size_t o = reply_head(rpc, 1, PROTOCORE_RPC_ACCEPT_SUCCESS);
    o = put(rpc, o, 0);       // error: no error
    o = put(rpc, o, 42);      // lid
    o = put(rpc, o, 703);     // abortPort
    o = put(rpc, o, 1048576); // maxRecvSize
    TEST_ASSERT_TRUE(protocore_vxi11_parse_create_link_resp(rpc, o, &r));
    TEST_ASSERT_EQUAL_INT32(0, r.error);
    TEST_ASSERT_EQUAL_INT32(42, r.lid);
    TEST_ASSERT_EQUAL_UINT32(703u, r.abort_port);
    TEST_ASSERT_EQUAL_UINT32(1048576u, r.max_recv_size);

    for (size_t shorter = 24; shorter < o; shorter++)
    {
        TEST_ASSERT_FALSE(protocore_vxi11_parse_create_link_resp(rpc, shorter, &r));
    }
    TEST_ASSERT_FALSE(protocore_vxi11_parse_create_link_resp(rpc, o, NULL));
}

// The RPCL gives Device_ReadResp as error, reason, data. The reason bits combine, so a read that
// ended on both the term char and END reports both.
void test_device_read_reply(void)
{
    uint8_t rpc[64];
    Vxi11ReadResp r;
    size_t o = reply_head(rpc, 1, PROTOCORE_RPC_ACCEPT_SUCCESS);
    o = put(rpc, o, 0);
    o = put(rpc, o, PROTOCORE_VXI11_REASON_CHR | PROTOCORE_VXI11_REASON_END);
    o = put(rpc, o, 5); // opaque length
    memcpy(rpc + o, "1.234", 5);
    rpc[o + 5] = 0;
    rpc[o + 6] = 0;
    rpc[o + 7] = 0; // XDR pad to a word boundary
    o += 8;

    TEST_ASSERT_TRUE(protocore_vxi11_parse_read_resp(rpc, o, &r));
    TEST_ASSERT_EQUAL_INT32(0, r.error);
    TEST_ASSERT_EQUAL_INT32(6, r.reason);
    TEST_ASSERT_EQUAL_UINT(5u, r.data_len);
    TEST_ASSERT_EQUAL_MEMORY("1.234", r.data, 5);

    TEST_ASSERT_EQUAL_HEX32(0x01u, PROTOCORE_VXI11_REASON_REQCNT);
    TEST_ASSERT_EQUAL_HEX32(0x02u, PROTOCORE_VXI11_REASON_CHR);
    TEST_ASSERT_EQUAL_HEX32(0x04u, PROTOCORE_VXI11_REASON_END);

    // an opaque whose count runs past the buffer is refused rather than pointed at
    put(rpc, 32, 64);
    TEST_ASSERT_FALSE(protocore_vxi11_parse_read_resp(rpc, o, &r));
}

// Device_WriteResp is error then size; Device_ReadStbResp is error then the status byte, again as a
// whole XDR word whose value is the low octet.
void test_write_and_readstb_replies(void)
{
    uint8_t rpc[64];
    Vxi11WriteResp w;
    Vxi11ReadStbResp s;

    size_t o = reply_head(rpc, 1, PROTOCORE_RPC_ACCEPT_SUCCESS);
    o = put(rpc, o, 0);
    o = put(rpc, o, 6);
    TEST_ASSERT_TRUE(protocore_vxi11_parse_write_resp(rpc, o, &w));
    TEST_ASSERT_EQUAL_INT32(0, w.error);
    TEST_ASSERT_EQUAL_UINT32(6u, w.size);

    o = reply_head(rpc, 1, PROTOCORE_RPC_ACCEPT_SUCCESS);
    o = put(rpc, o, 0);
    o = put(rpc, o, 0x40); // MSS/RQS set in the status byte
    TEST_ASSERT_TRUE(protocore_vxi11_parse_readstb_resp(rpc, o, &s));
    TEST_ASSERT_EQUAL_HEX8(0x40, s.stb);
}

// destroy_link, device_trigger and device_clear all return a bare Device_Error, and a non-zero code
// is carried through rather than turned into a parse failure: the reply was well-formed.
void test_bare_device_error_reply(void)
{
    uint8_t rpc[64];
    int32_t err = 0;
    size_t o = reply_head(rpc, 1, PROTOCORE_RPC_ACCEPT_SUCCESS);
    o = put(rpc, o, (uint32_t)PROTOCORE_VXI11_ERR_INVALID_LINK);
    TEST_ASSERT_TRUE(protocore_vxi11_parse_error_resp(rpc, o, &err));
    TEST_ASSERT_EQUAL_INT32(4, err);
    TEST_ASSERT_FALSE(protocore_vxi11_parse_error_resp(rpc, o - 1, &err));
}

// The Device_ErrorCode values the header names, and a description for each. The strings are this
// module's; the numbers are the VXI-11 specification's Device_ErrorCode table.
void test_error_codes_have_distinct_descriptions(void)
{
    static const int32_t CODES[] = {
        PROTOCORE_VXI11_ERR_NONE,         PROTOCORE_VXI11_ERR_SYNTAX,    PROTOCORE_VXI11_ERR_NOT_ACCESSIBLE,
        PROTOCORE_VXI11_ERR_INVALID_LINK, PROTOCORE_VXI11_ERR_PARAMETER, PROTOCORE_VXI11_ERR_NO_LOCK,
        PROTOCORE_VXI11_ERR_IO_TIMEOUT,   PROTOCORE_VXI11_ERR_IO_ERROR,  PROTOCORE_VXI11_ERR_ABORT};
    TEST_ASSERT_EQUAL_INT32(0, PROTOCORE_VXI11_ERR_NONE);
    TEST_ASSERT_EQUAL_INT32(1, PROTOCORE_VXI11_ERR_SYNTAX);
    TEST_ASSERT_EQUAL_INT32(3, PROTOCORE_VXI11_ERR_NOT_ACCESSIBLE);
    TEST_ASSERT_EQUAL_INT32(4, PROTOCORE_VXI11_ERR_INVALID_LINK);
    TEST_ASSERT_EQUAL_INT32(5, PROTOCORE_VXI11_ERR_PARAMETER);
    TEST_ASSERT_EQUAL_INT32(12, PROTOCORE_VXI11_ERR_NO_LOCK);
    TEST_ASSERT_EQUAL_INT32(15, PROTOCORE_VXI11_ERR_IO_TIMEOUT);
    TEST_ASSERT_EQUAL_INT32(17, PROTOCORE_VXI11_ERR_IO_ERROR);
    TEST_ASSERT_EQUAL_INT32(23, PROTOCORE_VXI11_ERR_ABORT);

    for (size_t i = 0; i < sizeof(CODES) / sizeof(CODES[0]); i++)
    {
        const char *a = protocore_vxi11_error_str(CODES[i]);
        TEST_ASSERT_NOT_NULL(a);
        TEST_ASSERT_TRUE(a[0] != '\0');
        for (size_t j = i + 1; j < sizeof(CODES) / sizeof(CODES[0]); j++)
        {
            TEST_ASSERT_TRUE(strcmp(a, protocore_vxi11_error_str(CODES[j])) != 0);
        }
    }
    // a code with no entry still returns a usable string, never null
    TEST_ASSERT_EQUAL_STRING("unknown error", protocore_vxi11_error_str(9999));
}

// The Device_Flags bits the header names.
void test_device_flags_bits(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x01u, PROTOCORE_VXI11_FLAG_WAITLOCK);
    TEST_ASSERT_EQUAL_HEX32(0x08u, PROTOCORE_VXI11_FLAG_END);
    TEST_ASSERT_EQUAL_HEX32(0x80u, PROTOCORE_VXI11_FLAG_TERMCHRSET);
}

// Every builder refuses a buffer one octet short of the call it would write: a truncated RPC record
// is a protocol violation, not a shorter call.
void test_builders_refuse_a_short_buffer(void)
{
    static const uint8_t D[] = {1, 2, 3};
    uint8_t buf[96];

    TEST_ASSERT_EQUAL_UINT(48u, protocore_vxi11_build_destroy_link(buf, 48, 1, 1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_vxi11_build_destroy_link(buf, 47, 1, 1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_vxi11_build_destroy_link(NULL, 48, 1, 1));

    TEST_ASSERT_EQUAL_UINT(60u, protocore_vxi11_build_getport(buf, 60, 1, 1, 1, 6));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_vxi11_build_getport(buf, 59, 1, 1, 1, 6));

    TEST_ASSERT_EQUAL_UINT(68u, protocore_vxi11_build_create_link(buf, 68, 1, 1, PROTO_FALSE, 0, "inst0"));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_vxi11_build_create_link(buf, 67, 1, 1, PROTO_FALSE, 0, "inst0"));

    // 44 header + 16 parameter words + (4 count + 3 data + 1 XDR pad) = 68
    TEST_ASSERT_EQUAL_UINT(68u, protocore_vxi11_build_device_write(buf, 68, 1, 1, 0, 0, 0, D, sizeof(D)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_vxi11_build_device_write(buf, 67, 1, 1, 0, 0, 0, D, sizeof(D)));

    TEST_ASSERT_EQUAL_UINT(68u, protocore_vxi11_build_device_read(buf, 68, 1, 1, 0, 0, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_vxi11_build_device_read(buf, 67, 1, 1, 0, 0, 0, 0, 0));

    TEST_ASSERT_EQUAL_UINT(60u, protocore_vxi11_build_device_readstb(buf, 60, 1, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_vxi11_build_device_readstb(buf, 59, 1, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_vxi11_build_device_trigger(buf, 59, 1, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_vxi11_build_device_clear(buf, 59, 1, 1, 0, 0, 0));
}
