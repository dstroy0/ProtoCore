// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the FTP client wire codec (services/file_transfer/ftp): command builders, the
// single/multi-line reply parser, and the PASV / EPSV data-address decoders. The reply
// vectors are authentic strings captured from a live server (test.rebex.net). Pure host tests.

#include "services/file_transfer/ftp/ftp.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// --- command builders ------------------------------------------------------

void test_build_command()
{
    char b[64];
    size_t n = pc_ftp_build_command(b, sizeof(b), "USER", "demo");
    TEST_ASSERT_EQUAL_size_t(11, n);
    TEST_ASSERT_EQUAL_STRING("USER demo\r\n", b);
    TEST_ASSERT_EQUAL_size_t(n, strlen(b));

    n = pc_ftp_build_command(b, sizeof(b), "TYPE", "I");
    TEST_ASSERT_EQUAL_STRING("TYPE I\r\n", b);

    // bare verb (no arg) -> no trailing space
    n = pc_ftp_build_command(b, sizeof(b), "PASV", NULL);
    TEST_ASSERT_EQUAL_STRING("PASV\r\n", b);
    n = pc_ftp_build_command(b, sizeof(b), "QUIT", "");
    TEST_ASSERT_EQUAL_STRING("QUIT\r\n", b);

    // a path argument is copied verbatim
    n = pc_ftp_build_command(b, sizeof(b), "RETR", "/programs/O1234.nc");
    TEST_ASSERT_EQUAL_STRING("RETR /programs/O1234.nc\r\n", b);
    (void)n;
}

void test_build_command_fail_closed()
{
    char b[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_ftp_build_command(b, sizeof(b), "RETR", "/too/long/path")); // overflow
    TEST_ASSERT_EQUAL_size_t(0, pc_ftp_build_command(b, sizeof(b), NULL, "x"));                // bad verb
    TEST_ASSERT_EQUAL_size_t(0, pc_ftp_build_command(b, sizeof(b), "", "x"));
    // exact-fit boundary: "NO\r\n" + NUL needs 5 bytes
    char b5[5];
    TEST_ASSERT_EQUAL_size_t(4, pc_ftp_build_command(b5, sizeof(b5), "NO", NULL));
    char b4[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_ftp_build_command(b4, sizeof(b4), "NO", NULL)); // no room for NUL
}

void test_build_port_and_eprt()
{
    char b[64];
    const uint8_t ip[4] = {192, 168, 1, 50};
    size_t n = pc_ftp_build_port(b, sizeof(b), ip, 4096); // 4096 = 16*256 + 0
    TEST_ASSERT_EQUAL_STRING("PORT 192,168,1,50,16,0\r\n", b);
    TEST_ASSERT_EQUAL_size_t(n, strlen(b));

    n = pc_ftp_build_port(b, sizeof(b), ip, 40000); // 40000 = 156*256 + 64
    TEST_ASSERT_EQUAL_STRING("PORT 192,168,1,50,156,64\r\n", b);

    n = pc_ftp_build_eprt(b, sizeof(b), "132.235.1.2", PROTO_FALSE, 6275); // RFC 2428 example
    TEST_ASSERT_EQUAL_STRING("EPRT |1|132.235.1.2|6275|\r\n", b);

    n = pc_ftp_build_eprt(b, sizeof(b), "1080::8:800:200C:417A", PROTO_TRUE, 5282); // RFC 2428 IPv6 example
    TEST_ASSERT_EQUAL_STRING("EPRT |2|1080::8:800:200C:417A|5282|\r\n", b);
    (void)n;
}

// --- reply parser (authentic server strings) -------------------------------

void test_reply_single_line()
{
    int code = 0;
    size_t used = 0;
    const char *r = "230 User 'demo' logged in.\r\n";
    TEST_ASSERT_TRUE(pc_ftp_parse_reply(r, strlen(r), &code, &used));
    TEST_ASSERT_EQUAL_INT(230, code);
    TEST_ASSERT_EQUAL_size_t(strlen(r), used);
    TEST_ASSERT_TRUE(pc_ftp_reply_ok(code));
}

void test_reply_multiline_greeting()
{
    // real test.rebex.net greeting: continuation lines do NOT repeat the code
    const char *g = "220-Welcome to test.rebex.net!\r\n"
                    "    See https://test.rebex.net/ for more information and terms of use.\r\n"
                    "220 If you don't have an account, log in as 'anonymous' or 'ftp'.\r\n";
    int code = 0;
    size_t used = 0;
    TEST_ASSERT_TRUE(pc_ftp_parse_reply(g, strlen(g), &code, &used));
    TEST_ASSERT_EQUAL_INT(220, code);
    TEST_ASSERT_EQUAL_size_t(strlen(g), used); // consumed the whole multiline reply
}

void test_reply_multiline_feat()
{
    // real FEAT reply: many indented continuation lines, terminated by "211 End."
    const char *feat = "211-Supported extensions:\r\n"
                       " AUTH TLS;SSL;\r\n"
                       " EPSV\r\n"
                       " EPRT\r\n"
                       " PASV\r\n"
                       " PORT\r\n"
                       " REST STREAM\r\n"
                       " SIZE\r\n"
                       "211 End.\r\n";
    int code = 0;
    size_t used = 0;
    TEST_ASSERT_TRUE(pc_ftp_parse_reply(feat, strlen(feat), &code, &used));
    TEST_ASSERT_EQUAL_INT(211, code);
    TEST_ASSERT_EQUAL_size_t(strlen(feat), used);
}

void test_reply_incomplete_and_malformed()
{
    int code = 0;
    size_t used = 0;
    // single line without its CRLF yet -> incomplete
    TEST_ASSERT_FALSE(pc_ftp_parse_reply("230 User logged in", 18, &code, &used));
    // multiline whose terminator has not arrived -> incomplete
    const char *partial = "211-Supported extensions:\r\n EPSV\r\n";
    TEST_ASSERT_FALSE(pc_ftp_parse_reply(partial, strlen(partial), &code, &used));
    // malformed: no 3-digit head
    TEST_ASSERT_FALSE(pc_ftp_parse_reply("2x0 bad\r\n", 9, &code, &used));
    // malformed: separator is neither SP nor '-'
    TEST_ASSERT_FALSE(pc_ftp_parse_reply("200X\r\n", 6, &code, &used));
    // too short
    TEST_ASSERT_FALSE(pc_ftp_parse_reply("22", 2, &code, &used));
}

void test_reply_pipelined_advance()
{
    // two replies back-to-back; parse the first, advance by `used`, parse the second.
    const char *stream = "220 Ready.\r\n331 Password required.\r\n";
    int code = 0;
    size_t used = 0;
    TEST_ASSERT_TRUE(pc_ftp_parse_reply(stream, strlen(stream), &code, &used));
    TEST_ASSERT_EQUAL_INT(220, code);
    size_t off = used;
    TEST_ASSERT_TRUE(pc_ftp_parse_reply(stream + off, strlen(stream) - off, &code, &used));
    TEST_ASSERT_EQUAL_INT(331, code);
    TEST_ASSERT_EQUAL_size_t(off + used, strlen(stream));
}

// A continuation line that itself begins with a different NNN<space> must NOT terminate.
void test_reply_multiline_not_terminated_by_other_code()
{
    const char *r = "150-Opening data\r\n"
                    "226 unrelated-looking line\r\n" // different code + space: still a continuation
                    "150 Transfer starting\r\n";     // the real terminator (same code 150)
    int code = 0;
    size_t used = 0;
    TEST_ASSERT_TRUE(pc_ftp_parse_reply(r, strlen(r), &code, &used));
    TEST_ASSERT_EQUAL_INT(150, code);
    TEST_ASSERT_EQUAL_size_t(strlen(r), used);
}

// --- PASV / EPSV address decode (authentic server strings) -----------------

void test_parse_pasv()
{
    const char *r = "227 Entering Passive Mode (194,108,117,16,4,6)\r\n";
    uint8_t ip[4] = {0, 0, 0, 0};
    uint16_t port = 0;
    TEST_ASSERT_TRUE(pc_ftp_parse_pasv(r, strlen(r), ip, &port));
    TEST_ASSERT_EQUAL_UINT8(194, ip[0]);
    TEST_ASSERT_EQUAL_UINT8(108, ip[1]);
    TEST_ASSERT_EQUAL_UINT8(117, ip[2]);
    TEST_ASSERT_EQUAL_UINT8(16, ip[3]);
    TEST_ASSERT_EQUAL_UINT16(4 * 256 + 6, port); // 1030
}

void test_parse_pasv_malformed()
{
    uint8_t ip[4];
    uint16_t port;
    TEST_ASSERT_FALSE(pc_ftp_parse_pasv("227 no tuple here\r\n", 19, ip, &port)); // no '('
    const char *oob = "227 (194,108,117,999,4,6)\r\n";                            // 999 > 255
    TEST_ASSERT_FALSE(pc_ftp_parse_pasv(oob, strlen(oob), ip, &port));
    const char *few = "227 (1,2,3,4,5)\r\n"; // only 5 numbers
    TEST_ASSERT_FALSE(pc_ftp_parse_pasv(few, strlen(few), ip, &port));
}

void test_parse_epsv()
{
    const char *r = "229 Entering Extended Passive Mode (|||1050|)\r\n";
    uint16_t port = 0;
    TEST_ASSERT_TRUE(pc_ftp_parse_epsv(r, strlen(r), &port));
    TEST_ASSERT_EQUAL_UINT16(1050, port);

    // a non-'|' delimiter is also legal (RFC 2428: any ASCII 33-126)
    const char *r2 = "229 EPSV OK (!!!49152!)\r\n";
    TEST_ASSERT_TRUE(pc_ftp_parse_epsv(r2, strlen(r2), &port));
    TEST_ASSERT_EQUAL_UINT16(49152, port);
}

void test_parse_epsv_malformed()
{
    uint16_t port;
    TEST_ASSERT_FALSE(pc_ftp_parse_epsv("229 no parens\r\n", 15, &port)); // no '('
    TEST_ASSERT_FALSE(pc_ftp_parse_epsv("229 (|||)\r\n", 11, &port));     // no port digits
}

// null / partial / overflow edge cases (fail closed, never overrun).
void test_reply_null_and_partial_multiline()
{
    int code = 0;
    size_t used = 0;
    TEST_ASSERT_FALSE(pc_ftp_parse_reply(NULL, 4, &code, &used)); // null buffer
    const char *unterm_first = "211-Supported";                   // first multiline line lacks its CRLF
    TEST_ASSERT_FALSE(pc_ftp_parse_reply(unterm_first, strlen(unterm_first), &code, &used));
    const char *unterm_term = "211-a\r\n211 End"; // terminator line lacks its CRLF
    TEST_ASSERT_FALSE(pc_ftp_parse_reply(unterm_term, strlen(unterm_term), &code, &used));
    const char *unterm_cont = "211-a\r\nbcd"; // continuation line lacks its CRLF
    TEST_ASSERT_FALSE(pc_ftp_parse_reply(unterm_cont, strlen(unterm_cont), &code, &used));
}

void test_build_overflow_and_null()
{
    char tiny[10];
    const uint8_t ip[4] = {192, 168, 1, 50};
    TEST_ASSERT_EQUAL_size_t(0, pc_ftp_build_port(tiny, sizeof(tiny), ip, 4096));            // overflows mid-number
    TEST_ASSERT_EQUAL_size_t(0, pc_ftp_build_port(NULL, 32, ip, 80));                        // null buffer
    TEST_ASSERT_EQUAL_size_t(0, pc_ftp_build_eprt(tiny, 4, "1.2.3.4", PROTO_FALSE, 80));     // eprt overflow
    TEST_ASSERT_EQUAL_size_t(0, pc_ftp_build_eprt(tiny, sizeof(tiny), "", PROTO_FALSE, 80)); // empty ip
}

void test_pasv_epsv_null_and_edges()
{
    uint8_t ip[4];
    uint16_t port;
    TEST_ASSERT_FALSE(pc_ftp_parse_pasv(NULL, 10, ip, &port));                // null buffer
    TEST_ASSERT_FALSE(pc_ftp_parse_pasv("227 (x,1,2,3,4,5)", 17, ip, &port)); // leading non-digit
    TEST_ASSERT_FALSE(pc_ftp_parse_epsv(NULL, 10, &port));                    // null buffer
    TEST_ASSERT_FALSE(pc_ftp_parse_epsv("229 (", 5, &port));                  // ends right at '('
    TEST_ASSERT_FALSE(pc_ftp_parse_epsv("229 (|5|)", 9, &port));              // fewer than 3 delimiters
    TEST_ASSERT_FALSE(pc_ftp_parse_epsv("229 (|||99999|)", 15, &port));       // port > 65535
}

// Null-argument guards on every entry point (the other side of each `!buf`/`!ip`/`!port` guard).
void test_build_null_args()
{
    char b[32];
    const uint8_t ip[4] = {1, 2, 3, 4};
    uint8_t oip[4];
    uint16_t port;
    TEST_ASSERT_EQUAL_size_t(0, pc_ftp_build_command(NULL, sizeof(b), "USER", "x"));             // !buf
    TEST_ASSERT_EQUAL_size_t(0, pc_ftp_build_port(b, sizeof(b), NULL, 80));                      // !ip
    TEST_ASSERT_EQUAL_size_t(0, pc_ftp_build_eprt(NULL, sizeof(b), "1.2.3.4", PROTO_FALSE, 80)); // !buf
    TEST_ASSERT_EQUAL_size_t(0, pc_ftp_build_eprt(b, sizeof(b), NULL, PROTO_FALSE, 80));         // !ip_str
    const char *pasv = "227 (1,2,3,4,5,6)\r\n";
    TEST_ASSERT_FALSE(pc_ftp_parse_pasv(pasv, strlen(pasv), NULL, &port)); // !ip
    TEST_ASSERT_FALSE(pc_ftp_parse_pasv(pasv, strlen(pasv), oip, NULL));   // !port
    const char *epsv = "229 (|||1050|)\r\n";
    TEST_ASSERT_FALSE(pc_ftp_parse_epsv(epsv, strlen(epsv), NULL)); // !port
}

// A malformed reply head exercises each side of the 3-digit test (char below '0' / above '9' at
// each of the three positions).
void test_reply_head_nondigit_edges()
{
    int code = 0;
    size_t used = 0;
    TEST_ASSERT_FALSE(pc_ftp_parse_reply("/23 x\r\n", 7, &code, &used)); // p[0] '/' < '0'
    TEST_ASSERT_FALSE(pc_ftp_parse_reply(":23 x\r\n", 7, &code, &used)); // p[0] ':' > '9'
    TEST_ASSERT_FALSE(pc_ftp_parse_reply("2/3 x\r\n", 7, &code, &used)); // p[1] '/' < '0'
    TEST_ASSERT_FALSE(pc_ftp_parse_reply("22/ x\r\n", 7, &code, &used)); // p[2] '/' < '0'
    TEST_ASSERT_FALSE(pc_ftp_parse_reply("22: x\r\n", 7, &code, &used)); // p[2] ':' > '9'
}

// A continuation line that repeats the SAME code with '-' (not space) must not terminate.
void test_reply_multiline_samecode_dash()
{
    const char *r = "150-first\r\n"
                    "150-still going\r\n" // same code 150 but '-' separator: not the terminator
                    "150 done\r\n";       // real terminator: same code + space
    int code = 0;
    size_t used = 0;
    TEST_ASSERT_TRUE(pc_ftp_parse_reply(r, strlen(r), &code, &used));
    TEST_ASSERT_EQUAL_INT(150, code);
    TEST_ASSERT_EQUAL_size_t(strlen(r), used);
}

// PASV field scanner edges: field starting past the buffer, a char above '9', truncation mid-number.
void test_parse_pasv_edges()
{
    uint8_t ip[4];
    uint16_t port;
    TEST_ASSERT_FALSE(pc_ftp_parse_pasv("227 (", 5, ip, &port)); // first field: i >= len right after '('
    const char *hi = "227 (194,:,3,4,5,6)\r\n";                  // ':' (0x3A) > '9' in a field
    TEST_ASSERT_FALSE(pc_ftp_parse_pasv(hi, strlen(hi), ip, &port));
    TEST_ASSERT_FALSE(pc_ftp_parse_pasv("227 (194", 8, ip, &port)); // ends mid-number, then no comma
    const char *empty = "227 (194,,3,4,5,6)\r\n";                   // empty field: ',' (0x2C) < '0' at field start
    TEST_ASSERT_FALSE(pc_ftp_parse_pasv(empty, strlen(empty), ip, &port));
    const char *trail = "227 (19a,3,4,5,6,7)\r\n"; // digit then 'a' (> '9') ends the accumulation loop
    TEST_ASSERT_FALSE(pc_ftp_parse_pasv(trail, strlen(trail), ip, &port));
}

// EPSV port scanner edges: end right after the delimiters, a char above '9', digits to end of buffer.
void test_parse_epsv_edges()
{
    uint16_t port = 0;
    TEST_ASSERT_FALSE(pc_ftp_parse_epsv("229 (|||", 8, &port)); // i >= len at the port field
    const char *hi = "229 (|||:|)\r\n";                         // ':' > '9' where the port should start
    TEST_ASSERT_FALSE(pc_ftp_parse_epsv(hi, strlen(hi), &port));
    TEST_ASSERT_TRUE(pc_ftp_parse_epsv("229 (|||1050", 12, &port)); // digits run to end of buffer (valid)
    TEST_ASSERT_EQUAL_UINT16(1050, port);
}

// pc_ftp_reply_class is public (static inline in the header) and callable directly with values
// outside the valid 3-digit-reply range; exercise both edges of its `code >= 100 && code <= 599`
// guard (below 100, and above 599) which no parsed-reply test happens to reach.
void test_reply_class_out_of_range()
{
    TEST_ASSERT_EQUAL_INT(0, pc_ftp_reply_class(50));  // below 100: first half of && is false
    TEST_ASSERT_EQUAL_INT(0, pc_ftp_reply_class(600)); // above 599: first half true, second half false
    TEST_ASSERT_FALSE(pc_ftp_reply_ok(50));
    TEST_ASSERT_FALSE(pc_ftp_reply_ok(600));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_command);
    RUN_TEST(test_build_command_fail_closed);
    RUN_TEST(test_build_port_and_eprt);
    RUN_TEST(test_reply_single_line);
    RUN_TEST(test_reply_multiline_greeting);
    RUN_TEST(test_reply_multiline_feat);
    RUN_TEST(test_reply_incomplete_and_malformed);
    RUN_TEST(test_reply_pipelined_advance);
    RUN_TEST(test_reply_multiline_not_terminated_by_other_code);
    RUN_TEST(test_parse_pasv);
    RUN_TEST(test_parse_pasv_malformed);
    RUN_TEST(test_parse_epsv);
    RUN_TEST(test_parse_epsv_malformed);
    RUN_TEST(test_reply_null_and_partial_multiline);
    RUN_TEST(test_build_overflow_and_null);
    RUN_TEST(test_pasv_epsv_null_and_edges);
    RUN_TEST(test_build_null_args);
    RUN_TEST(test_reply_head_nondigit_edges);
    RUN_TEST(test_reply_multiline_samecode_dash);
    RUN_TEST(test_parse_pasv_edges);
    RUN_TEST(test_parse_epsv_edges);
    RUN_TEST(test_reply_class_out_of_range);
    return UNITY_END();
}
