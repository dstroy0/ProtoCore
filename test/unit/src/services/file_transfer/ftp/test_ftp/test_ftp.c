// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the FTP client wire codec (services/file_transfer/ftp/ftp.h).
//
// Two load-bearing cases. test_rfc959_multiline_reply_example uses the exact four-line reply RFC
// 959 sec 4.2 prints, whose whole point is the padded intermediary line "  234 A line beginning
// with numbers": a parser that stops at the first line starting with three digits and a space
// terminates the reply early, desynchronizes the control channel, and reads the next command's
// reply as this one's. test_rfc2428_published_eprt_examples reproduces the two EPRT command lines
// RFC 2428 sec 2 prints verbatim, which fix the delimiter placement and the address family numbers.
//
// The PASV tuple is derived rather than copied: RFC 959 sec 4.1.2 defines the argument as the host
// address and TCP port broken into 8-bit fields, so p1 is the port's high octet, and the derivation
// is written out at the case.

#include "services/file_transfer/ftp/ftp.h"
#include <string.h>

#include <unity.h>

static uint8_t ftp_work[16]; // the borrow an entry takes; Ftp never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 959 sec 4.2:
//     "For example:
//                     123-First line
//                     Second line
//                       234 A line beginning with numbers
//                     123 The last line"
// and: "The user-process then simply needs to search for the second occurrence of the same reply
// code, followed by <SP> (Space), at the beginning of a line, and ignore all intermediary lines."
void test_rfc959_multiline_reply_example(void)
{
    static const char REPLY[] = "123-First line\r\n"
                                "Second line\r\n"
                                "  234 A line beginning with numbers\r\n"
                                "123 The last line\r\n";
    int code = 0;
    size_t consumed = 0;
    Ftp.parse_reply_args.buf = REPLY;
    Ftp.parse_reply_args.len = sizeof(REPLY) - 1;
    Ftp.parse_reply_args.code = &code;
    Ftp.parse_reply_args.consumed = &consumed;
    Ftp.parse_reply(ftp_work);
    TEST_ASSERT_TRUE(Ftp.ok);
    TEST_ASSERT_EQUAL_INT(123, code);
    TEST_ASSERT_EQUAL_UINT(sizeof(REPLY) - 1, consumed); // the whole reply, terminator line included
}

// The same reply arriving one octet at a time: every prefix short of the terminator line must
// report "need more" rather than a code, or the client acts on a reply it has not fully read.
void test_partial_multiline_reply_needs_more(void)
{
    static const char REPLY[] = "123-First line\r\n"
                                "Second line\r\n"
                                "  234 A line beginning with numbers\r\n"
                                "123 The last line\r\n";
    const size_t full = sizeof(REPLY) - 1;
    for (size_t n = 0; n < full; n++)
    {
        int code = 0;
        size_t consumed = 0;
        Ftp.parse_reply_args.buf = REPLY;
        Ftp.parse_reply_args.len = n;
        Ftp.parse_reply_args.code = &code;
        Ftp.parse_reply_args.consumed = &consumed;
        Ftp.parse_reply(ftp_work);
        TEST_ASSERT_FALSE_MESSAGE(Ftp.ok, REPLY);
    }
}

// A single-line reply is NNN<SP>text<CRLF> (sec 4.2), and the consumed count stops at its LF so a
// pipelined reply behind it is left in the buffer.
void test_single_line_reply_and_pipelining(void)
{
    static const char TWO[] = "220 Service ready\r\n331 User name okay\r\n";
    int code = 0;
    size_t consumed = 0;
    Ftp.parse_reply_args.buf = TWO;
    Ftp.parse_reply_args.len = sizeof(TWO) - 1;
    Ftp.parse_reply_args.code = &code;
    Ftp.parse_reply_args.consumed = &consumed;
    Ftp.parse_reply(ftp_work);
    TEST_ASSERT_TRUE(Ftp.ok);
    TEST_ASSERT_EQUAL_INT(220, code);
    TEST_ASSERT_EQUAL_UINT(19u, consumed); // "220 Service ready\r\n"

    Ftp.parse_reply_args.buf = TWO + consumed;
    Ftp.parse_reply_args.len = sizeof(TWO) - 1 - consumed;
    Ftp.parse_reply_args.code = &code;
    Ftp.parse_reply_args.consumed = &consumed;
    Ftp.parse_reply(ftp_work);
    TEST_ASSERT_TRUE(Ftp.ok);
    TEST_ASSERT_EQUAL_INT(331, code);
}

// A reply head that is not three digits followed by SP or '-' is malformed, not merely incomplete.
void test_malformed_reply_heads_are_refused(void)
{
    static const char *const BAD[] = {
        "20 Too short\r\n",       // two digits
        "2x0 Not a digit\r\n",    // non-digit in the code
        "220\tTab separator\r\n", // separator is neither SP nor '-'
        "220",                    // no separator at all
        "abc def\r\n",
        "",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        int code = 0;
        size_t consumed = 0;
        Ftp.parse_reply_args.buf = BAD[i];
        Ftp.parse_reply_args.len = strlen(BAD[i]);
        Ftp.parse_reply_args.code = &code;
        Ftp.parse_reply_args.consumed = &consumed;
        Ftp.parse_reply(ftp_work);
        TEST_ASSERT_FALSE_MESSAGE(Ftp.ok, BAD[i]);
    }
}

// A multiline reply whose terminator carries a DIFFERENT code does not end it: sec 4.2 requires the
// first and last line codes to be the same.
void test_a_different_code_does_not_terminate(void)
{
    static const char REPLY[] = "211-Features:\r\n MDTM\r\n215 UNIX Type: L8\r\n";
    int code = 0;
    size_t consumed = 0;
    Ftp.parse_reply_args.buf = REPLY;
    Ftp.parse_reply_args.len = sizeof(REPLY) - 1;
    Ftp.parse_reply_args.code = &code;
    Ftp.parse_reply_args.consumed = &consumed;
    Ftp.parse_reply(ftp_work);
    TEST_ASSERT_FALSE(Ftp.ok);
}

// RFC 2428 sec 2, "The following are sample EPRT commands", printed verbatim, plus the trailing
// telnet end-of-line RFC 959 requires on every command.
void test_rfc2428_published_eprt_examples(void)
{
    char buf[64];
    Ftp.build_eprt_args.buf = buf;
    Ftp.build_eprt_args.cap = sizeof(buf);
    Ftp.build_eprt_args.ip_str = "132.235.1.2";
    Ftp.build_eprt_args.ipv6 = PROTO_FALSE;
    Ftp.build_eprt_args.port = 6275;
    Ftp.build_eprt(ftp_work);
    TEST_ASSERT_EQUAL_UINT(27u, Ftp.n);
    TEST_ASSERT_EQUAL_STRING("EPRT |1|132.235.1.2|6275|\r\n", buf);

    Ftp.build_eprt_args.buf = buf;
    Ftp.build_eprt_args.cap = sizeof(buf);
    Ftp.build_eprt_args.ip_str = "1080::8:800:200C:417A";
    Ftp.build_eprt_args.ipv6 = PROTO_TRUE;
    Ftp.build_eprt_args.port = 5282;
    Ftp.build_eprt(ftp_work);
    TEST_ASSERT_EQUAL_UINT(37u, Ftp.n);
    TEST_ASSERT_EQUAL_STRING("EPRT |2|1080::8:800:200C:417A|5282|\r\n", buf);
}

// RFC 959 sec 4.1.2: "PORT h1,h2,h3,h4,p1,p2 ... where h1 is the high order 8 bits of the internet
// host address", the same 8-bit-field split applied to the 16-bit port.
//
// Reusing RFC 2428's published pair 132.235.1.2 : 6275, the port splits as
//   6275 = 0x1883, so p1 = 0x18 = 24 and p2 = 0x83 = 131
// giving PORT 132,235,1,2,24,131.
void test_port_command_splits_into_eight_bit_fields(void)
{
    static const uint8_t IP[4] = {132, 235, 1, 2};
    char buf[64];
    Ftp.build_port_args.buf = buf;
    Ftp.build_port_args.cap = sizeof(buf);
    Ftp.build_port_args.ip = IP;
    Ftp.build_port_args.port = 6275;
    Ftp.build_port(ftp_work);
    TEST_ASSERT_EQUAL_UINT(25u, Ftp.n);
    TEST_ASSERT_EQUAL_STRING("PORT 132,235,1,2,24,131\r\n", buf);

    // the extremes of both fields, from the same definition
    static const uint8_t ZERO[4] = {0, 0, 0, 0};
    Ftp.build_port_args.buf = buf;
    Ftp.build_port_args.cap = sizeof(buf);
    Ftp.build_port_args.ip = ZERO;
    Ftp.build_port_args.port = 0;
    Ftp.build_port(ftp_work);
    TEST_ASSERT_TRUE(Ftp.n > 0);
    TEST_ASSERT_EQUAL_STRING("PORT 0,0,0,0,0,0\r\n", buf);

    static const uint8_t FULL[4] = {255, 255, 255, 255};
    Ftp.build_port_args.buf = buf;
    Ftp.build_port_args.cap = sizeof(buf);
    Ftp.build_port_args.ip = FULL;
    Ftp.build_port_args.port = 65535;
    Ftp.build_port(ftp_work);
    TEST_ASSERT_TRUE(Ftp.n > 0);
    TEST_ASSERT_EQUAL_STRING("PORT 255,255,255,255,255,255\r\n", buf);
}

// The 227 tuple decodes back to the address and port PORT would have encoded: the same split read
// the other way. 132,235,1,2,24,131 is the pair derived above.
void test_pasv_tuple_decodes_to_the_same_pair(void)
{
    static const char REPLY[] = "227 Entering Passive Mode (132,235,1,2,24,131)\r\n";
    uint8_t ip[4] = {0, 0, 0, 0};
    uint16_t port = 0;
    Ftp.parse_pasv_args.buf = REPLY;
    Ftp.parse_pasv_args.len = sizeof(REPLY) - 1;
    Ftp.parse_pasv_args.ip = ip;
    Ftp.parse_pasv_args.port = &port;
    Ftp.parse_pasv(ftp_work);
    TEST_ASSERT_TRUE(Ftp.ok);
    TEST_ASSERT_EQUAL_UINT8(132, ip[0]);
    TEST_ASSERT_EQUAL_UINT8(235, ip[1]);
    TEST_ASSERT_EQUAL_UINT8(1, ip[2]);
    TEST_ASSERT_EQUAL_UINT8(2, ip[3]);
    TEST_ASSERT_EQUAL_UINT16(6275, port);
}

// Every field of the tuple is one 8-bit field, so 256 in any of them is not a tuple. A missing
// comma, a missing parenthesis and a short tuple are refused too.
void test_pasv_refuses_out_of_range_and_malformed_tuples(void)
{
    static const char *const BAD[] = {
        "227 Entering Passive Mode (256,0,0,1,4,1)\r\n",
        "227 Entering Passive Mode (10,0,0,1,256,1)\r\n",
        "227 Entering Passive Mode (10,0,0,1,4)\r\n",
        "227 Entering Passive Mode (10,0,0,1,4,)\r\n",
        "227 Entering Passive Mode (10.0.0.1,4,1)\r\n",
        "227 Entering Passive Mode 10,0,0,1,4,1\r\n",
        "227 Entering Passive Mode ()\r\n",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        uint8_t ip[4];
        uint16_t port = 0;
        Ftp.parse_pasv_args.buf = BAD[i];
        Ftp.parse_pasv_args.len = strlen(BAD[i]);
        Ftp.parse_pasv_args.ip = ip;
        Ftp.parse_pasv_args.port = &port;
        Ftp.parse_pasv(ftp_work);
        TEST_ASSERT_FALSE_MESSAGE(Ftp.ok, BAD[i]);
    }
}

// RFC 2428 sec 3: "An example response string follows:
//     Entering Extended Passive Mode (|||6446|)"
// with the reply code the section fixes at 229.
void test_rfc2428_published_epsv_example(void)
{
    static const char REPLY[] = "229 Entering Extended Passive Mode (|||6446|)\r\n";
    uint16_t port = 0;
    int code = 0;
    size_t consumed = 0;
    Ftp.parse_epsv_args.buf = REPLY;
    Ftp.parse_epsv_args.len = sizeof(REPLY) - 1;
    Ftp.parse_epsv_args.port = &port;
    Ftp.parse_epsv(ftp_work);
    TEST_ASSERT_TRUE(Ftp.ok);
    TEST_ASSERT_EQUAL_UINT16(6446, port);
    Ftp.parse_reply_args.buf = REPLY;
    Ftp.parse_reply_args.len = sizeof(REPLY) - 1;
    Ftp.parse_reply_args.code = &code;
    Ftp.parse_reply_args.consumed = &consumed;
    Ftp.parse_reply(ftp_work);
    TEST_ASSERT_TRUE(Ftp.ok);
    TEST_ASSERT_EQUAL_INT(229, code);
}

// sec 2: "The delimiter character MUST be one of the ASCII characters in range 33-126 inclusive.
// The character "|" (ASCII 124) is recommended". The port reads the same whichever is used.
void test_epsv_accepts_any_legal_delimiter(void)
{
    static const char *const FORMS[] = {
        "229 Entering Extended Passive Mode (|||6446|)\r\n",
        "229 Entering Extended Passive Mode (!!!6446!)\r\n",
        "229 Entering Extended Passive Mode (~~~6446~)\r\n",
        "229 Entering Extended Passive Mode (,,,6446,)\r\n",
    };
    for (size_t i = 0; i < sizeof(FORMS) / sizeof(FORMS[0]); i++)
    {
        uint16_t port = 0;
        Ftp.parse_epsv_args.buf = FORMS[i];
        Ftp.parse_epsv_args.len = strlen(FORMS[i]);
        Ftp.parse_epsv_args.port = &port;
        Ftp.parse_epsv(ftp_work);
        TEST_ASSERT_TRUE_MESSAGE(Ftp.ok, FORMS[i]);
        TEST_ASSERT_EQUAL_UINT16(6446, port);
    }
}

// A port past the 16-bit field, a missing field and a missing parenthesis are refused.
void test_epsv_refuses_malformed_replies(void)
{
    static const char *const BAD[] = {
        "229 Entering Extended Passive Mode (|||65536|)\r\n", "229 Entering Extended Passive Mode (||6446|)\r\n",
        "229 Entering Extended Passive Mode (|||xyz|)\r\n",   "229 Entering Extended Passive Mode |||6446|\r\n",
        "229 Entering Extended Passive Mode (\r\n",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        uint16_t port = 0;
        Ftp.parse_epsv_args.buf = BAD[i];
        Ftp.parse_epsv_args.len = strlen(BAD[i]);
        Ftp.parse_epsv_args.port = &port;
        Ftp.parse_epsv(ftp_work);
        TEST_ASSERT_FALSE_MESSAGE(Ftp.ok, BAD[i]);
    }
}

// sec 4: a command is the verb, optionally SP and one argument, then the telnet end-of-line. A bare
// verb carries no trailing space.
void test_command_line_form(void)
{
    char buf[64];
    Ftp.build_command_args.buf = buf;
    Ftp.build_command_args.cap = sizeof(buf);
    Ftp.build_command_args.verb = "PASV";
    Ftp.build_command_args.arg = NULL;
    Ftp.build_command(ftp_work);
    TEST_ASSERT_EQUAL_UINT(6u, Ftp.n);
    TEST_ASSERT_EQUAL_STRING("PASV\r\n", buf);
    Ftp.build_command_args.buf = buf;
    Ftp.build_command_args.cap = sizeof(buf);
    Ftp.build_command_args.verb = "PASV";
    Ftp.build_command_args.arg = "";
    Ftp.build_command(ftp_work);
    TEST_ASSERT_EQUAL_UINT(6u, Ftp.n);
    TEST_ASSERT_EQUAL_STRING("PASV\r\n", buf);

    Ftp.build_command_args.buf = buf;
    Ftp.build_command_args.cap = sizeof(buf);
    Ftp.build_command_args.verb = "USER";
    Ftp.build_command_args.arg = "anonymous";
    Ftp.build_command(ftp_work);
    TEST_ASSERT_EQUAL_UINT(16u, Ftp.n);
    TEST_ASSERT_EQUAL_STRING("USER anonymous\r\n", buf);

    // an argument may itself contain spaces; sec 5.3 makes the whole tail the argument
    Ftp.build_command_args.buf = buf;
    Ftp.build_command_args.cap = sizeof(buf);
    Ftp.build_command_args.verb = "STOR";
    Ftp.build_command_args.arg = "my program.nc";
    Ftp.build_command(ftp_work);
    TEST_ASSERT_TRUE(Ftp.n > 0);
    TEST_ASSERT_EQUAL_STRING("STOR my program.nc\r\n", buf);
}

// RFC 959 sec 4.2: "There are five values for the first digit of the reply code: 1yz Positive
// Preliminary ... 2yz Positive Completion ... 3yz Positive Intermediate ... 4yz Transient Negative
// Completion ... 5yz Permanent Negative Completion". Only 2yz is a completed request.
void test_reply_class_follows_the_first_digit(void)
{
    TEST_ASSERT_EQUAL_INT(1, protocore_ftp_reply_class(150)); // File status okay; about to open
    TEST_ASSERT_EQUAL_INT(2, protocore_ftp_reply_class(200));
    TEST_ASSERT_EQUAL_INT(2, protocore_ftp_reply_class(226)); // Closing data connection
    TEST_ASSERT_EQUAL_INT(3, protocore_ftp_reply_class(331)); // User name okay, need password
    TEST_ASSERT_EQUAL_INT(4, protocore_ftp_reply_class(421)); // Service not available
    TEST_ASSERT_EQUAL_INT(5, protocore_ftp_reply_class(550)); // Requested action not taken

    TEST_ASSERT_TRUE(protocore_ftp_reply_ok(200));
    TEST_ASSERT_TRUE(protocore_ftp_reply_ok(227));
    TEST_ASSERT_TRUE(protocore_ftp_reply_ok(229));
    TEST_ASSERT_FALSE(protocore_ftp_reply_ok(150));
    TEST_ASSERT_FALSE(protocore_ftp_reply_ok(331));
    TEST_ASSERT_FALSE(protocore_ftp_reply_ok(550));

    // a code outside the three-digit range has no class
    TEST_ASSERT_EQUAL_INT(0, protocore_ftp_reply_class(99));
    TEST_ASSERT_EQUAL_INT(0, protocore_ftp_reply_class(600));
    TEST_ASSERT_EQUAL_INT(0, protocore_ftp_reply_class(0));
    TEST_ASSERT_EQUAL_INT(0, protocore_ftp_reply_class(-1));
}

// The builders refuse rather than truncate: one octet short of the exact need, including the NUL,
// reports 0. A truncated command line would be sent as a different command.
void test_builders_refuse_a_short_buffer(void)
{
    static const uint8_t IP[4] = {132, 235, 1, 2};
    char buf[64];

    Ftp.build_command_args.buf = buf;
    Ftp.build_command_args.cap = 7;
    Ftp.build_command_args.verb = "PASV";
    Ftp.build_command_args.arg = NULL;
    Ftp.build_command(ftp_work);
    TEST_ASSERT_EQUAL_UINT(6u, Ftp.n); // 6 + NUL
    Ftp.build_command_args.buf = buf;
    Ftp.build_command_args.cap = 6;
    Ftp.build_command_args.verb = "PASV";
    Ftp.build_command_args.arg = NULL;
    Ftp.build_command(ftp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Ftp.n);

    Ftp.build_port_args.buf = buf;
    Ftp.build_port_args.cap = 26;
    Ftp.build_port_args.ip = IP;
    Ftp.build_port_args.port = 6275;
    Ftp.build_port(ftp_work);
    TEST_ASSERT_EQUAL_UINT(25u, Ftp.n);
    Ftp.build_port_args.buf = buf;
    Ftp.build_port_args.cap = 25;
    Ftp.build_port_args.ip = IP;
    Ftp.build_port_args.port = 6275;
    Ftp.build_port(ftp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Ftp.n);

    Ftp.build_eprt_args.buf = buf;
    Ftp.build_eprt_args.cap = 28;
    Ftp.build_eprt_args.ip_str = "132.235.1.2";
    Ftp.build_eprt_args.ipv6 = PROTO_FALSE;
    Ftp.build_eprt_args.port = 6275;
    Ftp.build_eprt(ftp_work);
    TEST_ASSERT_EQUAL_UINT(27u, Ftp.n);
    Ftp.build_eprt_args.buf = buf;
    Ftp.build_eprt_args.cap = 27;
    Ftp.build_eprt_args.ip_str = "132.235.1.2";
    Ftp.build_eprt_args.ipv6 = PROTO_FALSE;
    Ftp.build_eprt_args.port = 6275;
    Ftp.build_eprt(ftp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Ftp.n);
}

// Null and empty inputs are refused rather than written through.
void test_builders_refuse_bad_arguments(void)
{
    static const uint8_t IP[4] = {10, 0, 0, 1};
    char buf[64];
    Ftp.build_command_args.buf = NULL;
    Ftp.build_command_args.cap = 64;
    Ftp.build_command_args.verb = "PASV";
    Ftp.build_command_args.arg = NULL;
    Ftp.build_command(ftp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Ftp.n);
    Ftp.build_command_args.buf = buf;
    Ftp.build_command_args.cap = sizeof(buf);
    Ftp.build_command_args.verb = NULL;
    Ftp.build_command_args.arg = NULL;
    Ftp.build_command(ftp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Ftp.n);
    Ftp.build_command_args.buf = buf;
    Ftp.build_command_args.cap = sizeof(buf);
    Ftp.build_command_args.verb = "";
    Ftp.build_command_args.arg = NULL;
    Ftp.build_command(ftp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Ftp.n);
    Ftp.build_port_args.buf = NULL;
    Ftp.build_port_args.cap = 64;
    Ftp.build_port_args.ip = IP;
    Ftp.build_port_args.port = 21;
    Ftp.build_port(ftp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Ftp.n);
    Ftp.build_port_args.buf = buf;
    Ftp.build_port_args.cap = sizeof(buf);
    Ftp.build_port_args.ip = NULL;
    Ftp.build_port_args.port = 21;
    Ftp.build_port(ftp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Ftp.n);
    Ftp.build_eprt_args.buf = NULL;
    Ftp.build_eprt_args.cap = 64;
    Ftp.build_eprt_args.ip_str = "10.0.0.1";
    Ftp.build_eprt_args.ipv6 = PROTO_FALSE;
    Ftp.build_eprt_args.port = 21;
    Ftp.build_eprt(ftp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Ftp.n);
    Ftp.build_eprt_args.buf = buf;
    Ftp.build_eprt_args.cap = sizeof(buf);
    Ftp.build_eprt_args.ip_str = NULL;
    Ftp.build_eprt_args.ipv6 = PROTO_FALSE;
    Ftp.build_eprt_args.port = 21;
    Ftp.build_eprt(ftp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Ftp.n);
    Ftp.build_eprt_args.buf = buf;
    Ftp.build_eprt_args.cap = sizeof(buf);
    Ftp.build_eprt_args.ip_str = "";
    Ftp.build_eprt_args.ipv6 = PROTO_FALSE;
    Ftp.build_eprt_args.port = 21;
    Ftp.build_eprt(ftp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Ftp.n);
}

// The parsers refuse null arguments rather than dereferencing them.
void test_parsers_refuse_null_arguments(void)
{
    static const char REPLY[] = "227 Entering Passive Mode (10,0,0,1,4,1)\r\n";
    uint8_t ip[4];
    uint16_t port = 0;
    int code = 0;
    size_t consumed = 0;
    Ftp.parse_reply_args.buf = NULL;
    Ftp.parse_reply_args.len = 10;
    Ftp.parse_reply_args.code = &code;
    Ftp.parse_reply_args.consumed = &consumed;
    Ftp.parse_reply(ftp_work);
    TEST_ASSERT_FALSE(Ftp.ok);
    Ftp.parse_pasv_args.buf = NULL;
    Ftp.parse_pasv_args.len = 10;
    Ftp.parse_pasv_args.ip = ip;
    Ftp.parse_pasv_args.port = &port;
    Ftp.parse_pasv(ftp_work);
    TEST_ASSERT_FALSE(Ftp.ok);
    Ftp.parse_pasv_args.buf = REPLY;
    Ftp.parse_pasv_args.len = sizeof(REPLY) - 1;
    Ftp.parse_pasv_args.ip = NULL;
    Ftp.parse_pasv_args.port = &port;
    Ftp.parse_pasv(ftp_work);
    TEST_ASSERT_FALSE(Ftp.ok);
    Ftp.parse_pasv_args.buf = REPLY;
    Ftp.parse_pasv_args.len = sizeof(REPLY) - 1;
    Ftp.parse_pasv_args.ip = ip;
    Ftp.parse_pasv_args.port = NULL;
    Ftp.parse_pasv(ftp_work);
    TEST_ASSERT_FALSE(Ftp.ok);
    Ftp.parse_epsv_args.buf = NULL;
    Ftp.parse_epsv_args.len = 10;
    Ftp.parse_epsv_args.port = &port;
    Ftp.parse_epsv(ftp_work);
    TEST_ASSERT_FALSE(Ftp.ok);
    Ftp.parse_epsv_args.buf = REPLY;
    Ftp.parse_epsv_args.len = sizeof(REPLY) - 1;
    Ftp.parse_epsv_args.port = NULL;
    Ftp.parse_epsv(ftp_work);
    TEST_ASSERT_FALSE(Ftp.ok);
}
