// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the STOMP 1.2 frame codec (services/iot/stomp): the frame builder, the
// non-mutating parser, header lookup, content-length bodies, and escape/unescape.
// Pure host tests.

#include "services/iot/stomp/stomp.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// A SEND frame builds with the blank-line separator and a NUL-terminated body.
void test_build_send()
{
    const char *keys[] = {"destination", "content-type"};
    const char *vals[] = {"/queue/a", "text/plain"};
    char buf[128];
    size_t n = pc_stomp_build_frame(buf, sizeof(buf), "SEND", keys, vals, 2, "hi", 2);
    const char expect[] = "SEND\ndestination:/queue/a\ncontent-type:text/plain\n\nhi\0";
    TEST_ASSERT_EQUAL_size_t(sizeof(expect) - 1, n); // sizeof includes the source-string NUL == our frame NUL
    TEST_ASSERT_EQUAL_MEMORY(expect, buf, n);
}

// The builder has no headers / no body too (e.g. a DISCONNECT or a heart-beat-less probe).
void test_build_no_headers_no_body()
{
    char buf[32];
    size_t n = pc_stomp_build_frame(buf, sizeof(buf), "DISCONNECT", NULL, NULL, 0, NULL, 0);
    const char expect[] = "DISCONNECT\n\n\0";
    TEST_ASSERT_EQUAL_size_t(sizeof(expect) - 1, n);
    TEST_ASSERT_EQUAL_MEMORY(expect, buf, n);
}

// Header values escape the special octets (CR LF colon backslash) per STOMP 1.2.
void test_build_escapes_header()
{
    const char *keys[] = {"k"};
    const char *vals[] = {"a:b\\c\nd"};
    char buf[64];
    size_t n = pc_stomp_build_frame(buf, sizeof(buf), "SEND", keys, vals, 1, NULL, 0);
    const char expect[] = "SEND\nk:a\\cb\\\\c\\nd\n\n\0";
    TEST_ASSERT_EQUAL_size_t(sizeof(expect) - 1, n);
    TEST_ASSERT_EQUAL_MEMORY(expect, buf, n);
}

void test_build_overflow_fails_closed()
{
    const char *keys[] = {"destination"};
    const char *vals[] = {"/queue/a"};
    char buf[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_build_frame(buf, sizeof(buf), "SEND", keys, vals, 1, "hi", 2));
}

// Build then parse round-trips the command, headers, and body.
void test_round_trip()
{
    const char *keys[] = {"destination", "id"};
    const char *vals[] = {"/topic/x", "0"};
    char buf[128];
    size_t n = pc_stomp_build_frame(buf, sizeof(buf), "SUBSCRIBE", keys, vals, 2, NULL, 0);
    TEST_ASSERT_GREATER_THAN(0, (int)n);

    StompFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_stomp_parse_frame(buf, n, &f, &c));
    TEST_ASSERT_EQUAL_size_t(n, c);
    TEST_ASSERT_EQUAL_MEMORY("SUBSCRIBE", f.command, f.command_len);
    TEST_ASSERT_EQUAL_size_t(2, f.header_count);

    const char *v;
    size_t vl;
    TEST_ASSERT_TRUE(pc_stomp_header(&f, "destination", &v, &vl));
    TEST_ASSERT_EQUAL_MEMORY("/topic/x", v, vl);
    TEST_ASSERT_TRUE(pc_stomp_header(&f, "id", &v, &vl));
    TEST_ASSERT_EQUAL_MEMORY("0", v, vl);
    TEST_ASSERT_FALSE(pc_stomp_header(&f, "missing", &v, &vl));
    TEST_ASSERT_EQUAL_size_t(0, f.body_len);
}

// A MESSAGE frame parses; \r\n line endings are tolerated; the body runs to the NUL.
void test_parse_message_crlf()
{
    const char raw[] = "MESSAGE\r\nsubscription:0\r\nmessage-id:7\r\ndestination:/topic/x\r\n\r\npayload\0extra";
    size_t len = sizeof(raw) - 1; // drop only the implicit terminator; keep the embedded NUL + "extra"
    StompFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_stomp_parse_frame(raw, len, &f, &c));
    TEST_ASSERT_EQUAL_MEMORY("MESSAGE", f.command, f.command_len);
    TEST_ASSERT_EQUAL_size_t(3, f.header_count);
    TEST_ASSERT_EQUAL_MEMORY("payload", f.body, f.body_len);
    TEST_ASSERT_EQUAL_size_t(7, f.body_len);
    // consumed lands just past the body's NUL, before the trailing "extra".
    TEST_ASSERT_EQUAL_MEMORY("extra", raw + c, 5);
}

// content-length makes the body length explicit, so a body may contain NULs.
void test_parse_content_length_body_with_nul()
{
    const char raw[] = "MESSAGE\ncontent-length:5\n\nab\0cd\0";
    size_t len = sizeof(raw) - 1; // include the body NUL and the terminating NUL
    StompFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_stomp_parse_frame(raw, len, &f, &c));
    TEST_ASSERT_EQUAL_size_t(5, f.body_len);
    TEST_ASSERT_EQUAL_MEMORY("ab\0cd", f.body, 5);
    TEST_ASSERT_EQUAL_size_t(len, c);
}

// Leading EOL octets (broker heart-beats) before a frame are skipped and counted.
void test_parse_skips_leading_heartbeats()
{
    const char raw[] = "\n\nRECEIPT\nreceipt-id:1\n\n\0";
    size_t len = sizeof(raw) - 1;
    StompFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_stomp_parse_frame(raw, len, &f, &c));
    TEST_ASSERT_EQUAL_MEMORY("RECEIPT", f.command, f.command_len);
    TEST_ASSERT_EQUAL_size_t(len, c);

    // A leading \r (as in a \r\n heart-beat) is skipped too, not just bare \n.
    const char raw_cr[] = "\r\nRECEIPT\nreceipt-id:1\n\n\0";
    size_t len_cr = sizeof(raw_cr) - 1;
    TEST_ASSERT_TRUE(pc_stomp_parse_frame(raw_cr, len_cr, &f, &c));
    TEST_ASSERT_EQUAL_MEMORY("RECEIPT", f.command, f.command_len);
    TEST_ASSERT_EQUAL_size_t(len_cr, c);
}

void test_parse_incomplete_and_malformed()
{
    StompFrame f;
    size_t c;
    TEST_ASSERT_FALSE(pc_stomp_parse_frame("SEND\n", 5, &f, &c));                 // no header/body terminator yet
    TEST_ASSERT_FALSE(pc_stomp_parse_frame("SEND\n\nbody", 10, &f, &c));          // body NUL not buffered
    TEST_ASSERT_FALSE(pc_stomp_parse_frame("SEND\nbadheader\n\n\0", 17, &f, &c)); // header without a colon
    TEST_ASSERT_FALSE(pc_stomp_parse_frame("\n\n\n", 3, &f, &c));                 // only heart-beats
    // content-length that overruns the buffer.
    TEST_ASSERT_FALSE(pc_stomp_parse_frame("MESSAGE\ncontent-length:99\n\nhi\0", 30, &f, &c));
    // An absurd content-length must fail closed, not overflow the length parse (32-bit hardening).
    TEST_ASSERT_FALSE(pc_stomp_parse_frame("MESSAGE\ncontent-length:99999999999999999999\n\nhi\0", 49, &f, &c));
}

void test_unescape()
{
    char dst[32];
    size_t n = pc_stomp_unescape(dst, sizeof(dst), "a\\cb\\\\c\\nd\\r", 12);
    TEST_ASSERT_EQUAL_size_t(8, n);                  // a : b \ c LF d CR
    TEST_ASSERT_EQUAL_MEMORY("a:b\\c\nd\r", dst, 8); // ':' from \c, '\' from \\, LF from \n, CR from \r
}

void test_unescape_rejects_bad()
{
    char dst[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_unescape(dst, sizeof(dst), "a\\x", 3)); // invalid escape
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_unescape(dst, sizeof(dst), "a\\", 2));  // dangling escape
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_unescape(dst, 2, "abc", 3));            // overflow
}

// The CR escape, null-argument guards, and every builder overflow boundary.
void test_build_cr_escape_and_guards()
{
    const char *keys[] = {"k"};
    const char *vals[] = {"a\rb"}; // CR forces the \r escape path
    char full[128];
    size_t flen = pc_stomp_build_frame(full, sizeof(full), "SEND", keys, vals, 1, "body", 4);
    TEST_ASSERT_GREATER_THAN(0, (int)flen);
    TEST_ASSERT_NOT_NULL(strstr(full, "k:a\\rb\n")); // \r escaped to backslash-r

    // Null / zero-cap / null-command guards.
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_build_frame(NULL, 64, "SEND", NULL, NULL, 0, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_build_frame(full, 0, "SEND", NULL, NULL, 0, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_build_frame(full, sizeof(full), NULL, NULL, NULL, 0, NULL, 0));
    // A null header key inside the loop fails closed.
    const char *nk[] = {NULL};
    const char *nv[] = {"v"};
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_build_frame(full, sizeof(full), "SEND", nk, nv, 1, NULL, 0));
    // A null per-entry header value (as opposed to key) also fails closed.
    const char *nv2[] = {NULL};
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_build_frame(full, sizeof(full), "SEND", keys, nv2, 1, NULL, 0));
    // A whole-array-null header_keys / header_vals (distinct from a null entry) fails closed too.
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_build_frame(full, sizeof(full), "SEND", NULL, vals, 1, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_build_frame(full, sizeof(full), "SEND", keys, NULL, 1, NULL, 0));
    // body_len > 0 with a null body pointer fails closed (no headers, so the header-array guards above don't fire).
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_build_frame(full, sizeof(full), "SEND", NULL, NULL, 0, NULL, 5));

    // Every capacity below the full length fails closed (walks each overflow return).
    for (size_t cap = 1; cap < flen; cap++)
    {
        char small[128];
        TEST_ASSERT_EQUAL_size_t(0, pc_stomp_build_frame(small, cap, "SEND", keys, vals, 1, "body", 4));
    }
}

void test_parse_more_edges()
{
    StompFrame f;
    size_t c;
    TEST_ASSERT_FALSE(pc_stomp_parse_frame(NULL, 5, &f, &c));             // null args
    TEST_ASSERT_FALSE(pc_stomp_parse_frame("x", 1, NULL, &c));            // null out
    TEST_ASSERT_FALSE(pc_stomp_parse_frame("x", 1, &f, NULL));            // null consumed
    TEST_ASSERT_FALSE(pc_stomp_parse_frame("SEND", 4, &f, &c));           // command line incomplete
    TEST_ASSERT_FALSE(pc_stomp_parse_frame("SEND\nfoo:bar", 12, &f, &c)); // header line incomplete

    TEST_ASSERT_FALSE(pc_stomp_parse_frame("MSG\ncontent-length:\n\nx", 22, &f, &c));   // empty content-length
    TEST_ASSERT_FALSE(pc_stomp_parse_frame("MSG\ncontent-length:xy\n\nx", 24, &f, &c)); // non-digit content-length
    TEST_ASSERT_FALSE(pc_stomp_parse_frame("MSG\ncontent-length:-1\n\nx", 24, &f, &c)); // leading non-digit ('-')
    const char not_on_nul[] = "MSG\ncontent-length:2\n\nabcd";                          // 2 bytes then 'c', not the NUL
    TEST_ASSERT_FALSE(pc_stomp_parse_frame(not_on_nul, sizeof(not_on_nul) - 1, &f, &c));
}

// Headers beyond PC_STOMP_MAX_HEADERS are parsed (the frame is still valid) but not stored.
void test_parse_header_capacity_cap()
{
    const char *keys[PC_STOMP_MAX_HEADERS + 1];
    const char *vals[PC_STOMP_MAX_HEADERS + 1];
    char kbuf[PC_STOMP_MAX_HEADERS + 1][4];
    for (size_t i = 0; i < PC_STOMP_MAX_HEADERS + 1; i++)
    {
        kbuf[i][0] = 'h';
        kbuf[i][1] = (char)('a' + i);
        kbuf[i][2] = '\0';
        keys[i] = kbuf[i];
        vals[i] = "v";
    }
    char buf[1024];
    size_t n = pc_stomp_build_frame(buf, sizeof(buf), "SEND", keys, vals, PC_STOMP_MAX_HEADERS + 1, NULL, 0);
    TEST_ASSERT_GREATER_THAN(0, (int)n);

    StompFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_stomp_parse_frame(buf, n, &f, &c));
    TEST_ASSERT_EQUAL_size_t(PC_STOMP_MAX_HEADERS, f.header_count); // the (MAX+1)th header is dropped, not stored
}

// A second content-length header is ignored (first occurrence wins), and a same-length header
// name that isn't actually "content-length" is correctly not mistaken for it.
void test_parse_duplicate_content_length_and_lookalike_header()
{
    // "contentxlength" is 14 chars, same as "content-length", but does not match it.
    const char raw[] = "MESSAGE\ncontentxlength:5\ncontent-length:5\ncontent-length:9\n\nHELLO\0";
    size_t len = sizeof(raw) - 1; // keep our explicit body-terminating NUL, drop only the implicit one
    StompFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_stomp_parse_frame(raw, len, &f, &c));
    TEST_ASSERT_EQUAL_size_t(3, f.header_count);
    TEST_ASSERT_EQUAL_size_t(5, f.body_len); // from the FIRST content-length (5), not the second (9)
    TEST_ASSERT_EQUAL_MEMORY("HELLO", f.body, 5);
}

// pc_stomp_header(): a same-length-but-different-name lookup, and val-only / val_len-only lookups.
void test_header_lookup_edge_branches()
{
    const char raw[] = "MESSAGE\ndestination:/topic/x\n\n\0";
    size_t len = sizeof(raw) - 1;
    StompFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_stomp_parse_frame(raw, len, &f, &c));

    // "destinatioX" is the same length (11) as "destination" but not equal to it.
    TEST_ASSERT_FALSE(pc_stomp_header(&f, "destinatioX", NULL, NULL));

    const char *v;
    size_t vl;
    TEST_ASSERT_TRUE(pc_stomp_header(&f, "destination", NULL, &vl)); // val not requested
    TEST_ASSERT_EQUAL_size_t(8, vl);
    TEST_ASSERT_TRUE(pc_stomp_header(&f, "destination", &v, NULL)); // val_len not requested
    TEST_ASSERT_EQUAL_MEMORY("/topic/x", v, 8);
}

void test_header_and_unescape_null()
{
    StompFrame f;
    const char *v;
    size_t vl;
    TEST_ASSERT_FALSE(pc_stomp_header(NULL, "x", &v, &vl));
    TEST_ASSERT_FALSE(pc_stomp_header(&f, NULL, &v, &vl));
    char dst[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_unescape(NULL, sizeof(dst), "a", 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_stomp_unescape(dst, sizeof(dst), NULL, 1));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_send);
    RUN_TEST(test_build_cr_escape_and_guards);
    RUN_TEST(test_parse_more_edges);
    RUN_TEST(test_header_and_unescape_null);
    RUN_TEST(test_build_no_headers_no_body);
    RUN_TEST(test_build_escapes_header);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_parse_message_crlf);
    RUN_TEST(test_parse_content_length_body_with_nul);
    RUN_TEST(test_parse_skips_leading_heartbeats);
    RUN_TEST(test_parse_incomplete_and_malformed);
    RUN_TEST(test_parse_header_capacity_cap);
    RUN_TEST(test_parse_duplicate_content_length_and_lookalike_header);
    RUN_TEST(test_header_lookup_edge_branches);
    RUN_TEST(test_unescape);
    RUN_TEST(test_unescape_rejects_bad);
    return UNITY_END();
}
