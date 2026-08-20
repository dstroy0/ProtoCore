// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the STOMP 1.2 frame codec (services/iot/stomp/stomp.h).
//
// The load-bearing case is test_published_error_frame: the STOMP 1.2 specification prints one
// complete ERROR frame with a content-length header of 170, and that body really is 170 octets. A
// parser that miscounts the body by one, or that stops at an embedded newline, cannot reproduce it.
// The SEND, CONNECT and repeated-header frames asserted here are likewise printed verbatim in the
// specification (stomp.github.io/stomp-specification-1.2.html), whose headings carry no numbers;
// the section numbers cited follow the module's own count of them.

#include "services/iot/stomp/stomp.h"
#include <string.h>

#include <unity.h>

static uint8_t stomp_work[16]; // the borrow an entry takes; Stomp never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static StompFrame g_frame;
static char g_out[512];

// Parse len octets and return whether the frame was complete.
static proto_bool parse(const char *in, size_t len)
{
    Stomp.frame = &g_frame;
    Stomp.buf.in = in;
    Stomp.buf.len = len;
    Stomp.parse(stomp_work);
    return Stomp.ok;
}

// The first entry named name, or NULL.
static const char *header(const char *name, size_t *out_len)
{
    Stomp.frame = &g_frame;
    Stomp.lookup.name = name;
    Stomp.header(stomp_work);
    *out_len = Stomp.value_len;
    return Stomp.ok ? Stomp.value : NULL;
}

// STOMP 1.2 "ERROR": the specification prints this frame complete, content-length and all. Its
// body - from "The message:" to the final newline - is exactly the 170 octets that header states,
// and it holds blank lines and a colon, neither of which ends it.
void test_published_error_frame(void)
{
    static const char FRAME[] = "ERROR\n"
                                "receipt-id:message-12345\n"
                                "content-type:text/plain\n"
                                "content-length:170\n"
                                "message:malformed frame received\n"
                                "\n"
                                "The message:\n"
                                "-----\n"
                                "MESSAGE\n"
                                "destined:/queue/a\n"
                                "receipt:message-12345\n"
                                "\n"
                                "Hello queue a!\n"
                                "-----\n"
                                "Did not contain a destination header, which is REQUIRED\n"
                                "for message propagation.\n";
    // sizeof carries the terminating NUL, which is the frame's own NULL octet (sec 9).
    TEST_ASSERT_TRUE(parse(FRAME, sizeof(FRAME)));
    TEST_ASSERT_EQUAL_UINT(5u, g_frame.command_len);
    TEST_ASSERT_EQUAL_MEMORY("ERROR", g_frame.command, 5);
    TEST_ASSERT_EQUAL_UINT(4u, g_frame.header_count);
    TEST_ASSERT_EQUAL_UINT(170u, g_frame.body_len);
    TEST_ASSERT_EQUAL_MEMORY("The message:", g_frame.body, 12);
    TEST_ASSERT_EQUAL_MEMORY("for message propagation.\n", g_frame.body + 170 - 25, 25);
    TEST_ASSERT_EQUAL_UINT(sizeof(FRAME), Stomp.consumed);

    size_t n = 0;
    const char *v = header("content-length", &n);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(3u, n);
    TEST_ASSERT_EQUAL_MEMORY("170", v, 3);
}

// STOMP 1.2 "Header receipt" prints this SEND frame. With no content-length the body runs to the
// NULL octet (sec 4.3.1), which here is the string's own terminator.
void test_published_send_frame(void)
{
    static const char FRAME[] = "SEND\n"
                                "destination:/queue/a\n"
                                "receipt:message-12345\n"
                                "\n"
                                "hello queue a";
    TEST_ASSERT_TRUE(parse(FRAME, sizeof(FRAME)));
    TEST_ASSERT_EQUAL_MEMORY("SEND", g_frame.command, 4);
    TEST_ASSERT_EQUAL_UINT(4u, g_frame.command_len);
    TEST_ASSERT_EQUAL_UINT(2u, g_frame.header_count);
    TEST_ASSERT_EQUAL_UINT(13u, g_frame.body_len);
    TEST_ASSERT_EQUAL_MEMORY("hello queue a", g_frame.body, 13);

    size_t n = 0;
    const char *v = header("destination", &n);
    TEST_ASSERT_EQUAL_UINT(8u, n);
    TEST_ASSERT_EQUAL_MEMORY("/queue/a", v, 8);

    v = header("receipt", &n);
    TEST_ASSERT_EQUAL_UINT(13u, n);
    TEST_ASSERT_EQUAL_MEMORY("message-12345", v, 13);
}

// STOMP 1.2 "Repeated Header Entries" prints this MESSAGE frame and states "The value of the foo
// header is just World" - the first entry, not the last.
void test_repeated_header_first_entry_wins(void)
{
    static const char FRAME[] = "MESSAGE\n"
                                "foo:World\n"
                                "foo:Hello\n"
                                "\n";
    TEST_ASSERT_TRUE(parse(FRAME, sizeof(FRAME)));
    TEST_ASSERT_EQUAL_UINT(2u, g_frame.header_count);
    TEST_ASSERT_EQUAL_UINT(0u, g_frame.body_len);

    size_t n = 0;
    const char *v = header("foo", &n);
    TEST_ASSERT_EQUAL_UINT(5u, n);
    TEST_ASSERT_EQUAL_MEMORY("World", v, 5);
}

// STOMP 1.2 "CONNECT or STOMP Frame" prints this frame; a CONNECT carries headers and no body.
void test_published_connect_frame(void)
{
    static const char FRAME[] = "CONNECT\n"
                                "accept-version:1.2\n"
                                "host:stomp.github.org\n"
                                "\n";
    TEST_ASSERT_TRUE(parse(FRAME, sizeof(FRAME)));
    TEST_ASSERT_EQUAL_MEMORY("CONNECT", g_frame.command, 7);
    TEST_ASSERT_EQUAL_UINT(2u, g_frame.header_count);
    TEST_ASSERT_EQUAL_UINT(0u, g_frame.body_len);

    size_t n = 0;
    TEST_ASSERT_EQUAL_MEMORY("1.2", header("accept-version", &n), 3);
    TEST_ASSERT_EQUAL_UINT(3u, n);
}

// A build writes `command EOL *( header EOL ) EOL body NULL` (sec 9), the EOL a bare LF. The SEND
// frame above, rebuilt octet for octet.
void test_build_emits_the_published_send_frame(void)
{
    static const char WANT[] = "SEND\n"
                               "destination:/queue/a\n"
                               "receipt:message-12345\n"
                               "\n"
                               "hello queue a";
    static const char *const NAMES[] = {"destination", "receipt"};
    static const char *const VALUES[] = {"/queue/a", "message-12345"};

    Stomp.buf.out = g_out;
    Stomp.buf.cap = sizeof(g_out);
    Stomp.build_args.command = "SEND";
    Stomp.build_args.header_names = NAMES;
    Stomp.build_args.header_values = VALUES;
    Stomp.build_args.header_count = 2;
    Stomp.build_args.body = "hello queue a";
    Stomp.build_args.body_len = 13;
    Stomp.build(stomp_work);

    TEST_ASSERT_TRUE(Stomp.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Stomp.n); // the NULL octet is counted
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, sizeof(WANT));
}

// A built frame with no headers and no body is `command LF LF NULL`.
void test_build_minimal_frame(void)
{
    static const char WANT[] = "DISCONNECT\n\n";
    Stomp.buf.out = g_out;
    Stomp.buf.cap = sizeof(g_out);
    Stomp.build_args.command = "DISCONNECT";
    Stomp.build_args.header_names = NULL;
    Stomp.build_args.header_values = NULL;
    Stomp.build_args.header_count = 0;
    Stomp.build_args.body = NULL;
    Stomp.build_args.body_len = 0;
    Stomp.build(stomp_work);

    TEST_ASSERT_TRUE(Stomp.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Stomp.n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, sizeof(WANT));
}

// Sec 4.1 Value Encoding: "\r" is CR, "\n" is LF, "\c" is colon and "\\" is backslash. A build
// applies the reverse transformation to every header-name and header-value it writes, so a colon
// in a value cannot be read back as the name/value delimiter.
void test_build_escapes_a_header(void)
{
    static const char WANT[] = "SEND\n"
                               "a\\cb:x\\ny\n"
                               "\n";
    static const char *const NAMES[] = {"a:b"};
    static const char *const VALUES[] = {"x\ny"};

    Stomp.buf.out = g_out;
    Stomp.buf.cap = sizeof(g_out);
    Stomp.build_args.command = "SEND";
    Stomp.build_args.header_names = NAMES;
    Stomp.build_args.header_values = VALUES;
    Stomp.build_args.header_count = 1;
    Stomp.build_args.body = NULL;
    Stomp.build_args.body_len = 0;
    Stomp.build(stomp_work);

    TEST_ASSERT_TRUE(Stomp.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Stomp.n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, sizeof(WANT));
}

// Sec 4.1's four transformations, applied in the decode direction, in one pass.
void test_unescape_the_four_transformations(void)
{
    static const char IN[] = "\\r\\n\\c\\\\";
    static const char WANT[] = {'\r', '\n', ':', '\\'};

    Stomp.buf.in = IN;
    Stomp.buf.len = sizeof(IN) - 1;
    Stomp.buf.out = g_out;
    Stomp.buf.cap = sizeof(g_out);
    Stomp.unescape(stomp_work);

    TEST_ASSERT_TRUE(Stomp.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Stomp.n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, sizeof(WANT));
}

// Sec 4.1: "Undefined escape sequences such as \t ... MUST be treated as a fatal protocol error."
// A trailing lone backslash has no second octet and is the same refusal.
void test_unescape_rejects_an_undefined_escape(void)
{
    static const char BAD[] = "a\\tb";
    Stomp.buf.in = BAD;
    Stomp.buf.len = sizeof(BAD) - 1;
    Stomp.buf.out = g_out;
    Stomp.buf.cap = sizeof(g_out);
    Stomp.unescape(stomp_work);
    TEST_ASSERT_FALSE(Stomp.ok);
    TEST_ASSERT_EQUAL_UINT(0u, Stomp.n);

    static const char TRAILING[] = "ab\\";
    Stomp.buf.in = TRAILING;
    Stomp.buf.len = sizeof(TRAILING) - 1;
    Stomp.buf.out = g_out;
    Stomp.buf.cap = sizeof(g_out);
    Stomp.unescape(stomp_work);
    TEST_ASSERT_FALSE(Stomp.ok);
}

// Sec 9: EOL = [CR] LF. The same frame written with CRLF line endings parses to the same slices,
// the CR trimmed off each line.
void test_eol_accepts_an_optional_cr(void)
{
    static const char FRAME[] = "SEND\r\n"
                                "destination:/queue/a\r\n"
                                "\r\n"
                                "hi";
    TEST_ASSERT_TRUE(parse(FRAME, sizeof(FRAME)));
    TEST_ASSERT_EQUAL_UINT(4u, g_frame.command_len);
    TEST_ASSERT_EQUAL_MEMORY("SEND", g_frame.command, 4);
    TEST_ASSERT_EQUAL_UINT(1u, g_frame.header_count);
    TEST_ASSERT_EQUAL_UINT(8u, g_frame.headers[0].value_len);
    TEST_ASSERT_EQUAL_MEMORY("/queue/a", g_frame.headers[0].value, 8);
    TEST_ASSERT_EQUAL_UINT(2u, g_frame.body_len);
}

// Sec 5.4 Heart-beating sends a bare EOL, and sec 9 trails a frame with *( EOL ). Those octets sit
// ahead of the next command; the parse steps over them and counts them in consumed.
void test_leading_eols_are_consumed(void)
{
    static const char FRAME[] = "\n\r\n"
                                "SEND\n"
                                "\n";
    TEST_ASSERT_TRUE(parse(FRAME, sizeof(FRAME)));
    TEST_ASSERT_EQUAL_UINT(4u, g_frame.command_len);
    TEST_ASSERT_EQUAL_MEMORY("SEND", g_frame.command, 4);
    TEST_ASSERT_EQUAL_UINT(0u, g_frame.body_len);
    TEST_ASSERT_EQUAL_UINT(sizeof(FRAME), Stomp.consumed);
}

// Sec 4.3.1: with a content-length "this number of octets MUST be read, regardless of whether or
// not there are NULL octets in the body". Three octets, the middle one a NULL.
void test_content_length_reads_null_octets(void)
{
    static const char FRAME[] = "SEND\n"
                                "content-length:3\n"
                                "\n"
                                "a\0b";
    TEST_ASSERT_TRUE(parse(FRAME, sizeof(FRAME)));
    TEST_ASSERT_EQUAL_UINT(3u, g_frame.body_len);
    TEST_ASSERT_EQUAL_MEMORY("a\0b", g_frame.body, 3);
    TEST_ASSERT_EQUAL_UINT(sizeof(FRAME), Stomp.consumed);
}

// A content-length that does not land on the frame's NULL octet describes a different frame, so
// the parse refuses rather than handing back a body of the stated length.
void test_content_length_must_land_on_the_null(void)
{
    static const char FRAME[] = "SEND\n"
                                "content-length:2\n"
                                "\n"
                                "abc";
    TEST_ASSERT_FALSE(parse(FRAME, sizeof(FRAME)));

    static const char BAD_DIGITS[] = "SEND\n"
                                     "content-length:x\n"
                                     "\n";
    TEST_ASSERT_FALSE(parse(BAD_DIGITS, sizeof(BAD_DIGITS)));
}

// The NULL octet has not arrived yet, so no frame is complete and nothing is consumed.
void test_incomplete_frame_is_refused(void)
{
    static const char PARTIAL[] = "SEND\ndestination:/queue/a\n\nhello";
    TEST_ASSERT_FALSE(parse(PARTIAL, sizeof(PARTIAL) - 1)); // the terminator withheld
    TEST_ASSERT_EQUAL_UINT(0u, Stomp.consumed);

    static const char NO_HEADER_EOL[] = "SEND\ndestination";
    TEST_ASSERT_FALSE(parse(NO_HEADER_EOL, sizeof(NO_HEADER_EOL) - 1));

    TEST_ASSERT_FALSE(parse("", 0));
}

// Sec 9: header = header-name ":" header-value. A line inside the header block with no colon is
// not a header entry, so the frame is refused rather than half-parsed.
void test_header_without_a_colon_is_refused(void)
{
    static const char FRAME[] = "SEND\n"
                                "destination\n"
                                "\n";
    TEST_ASSERT_FALSE(parse(FRAME, sizeof(FRAME)));
}

// Sec 5.4: a sender with nothing to send "MUST send an end-of-line (EOL)". Octets that are only
// EOLs are a heart-beat, not a frame, so the parse reports no frame rather than an empty one.
void test_only_eols_is_not_a_frame(void)
{
    static const char BEAT[] = "\n\r\n\n";
    TEST_ASSERT_FALSE(parse(BEAT, sizeof(BEAT) - 1));
    TEST_ASSERT_EQUAL_UINT(0u, Stomp.consumed);
}

// A lookup that names no entry reports nothing rather than the previous find.
void test_header_lookup_misses(void)
{
    static const char FRAME[] = "SEND\n"
                                "destination:/queue/a\n"
                                "\n";
    TEST_ASSERT_TRUE(parse(FRAME, sizeof(FRAME)));
    size_t n = 1;
    TEST_ASSERT_NULL(header("receipt", &n));
    TEST_ASSERT_EQUAL_UINT(0u, n);
}

// A buffer that cannot hold the whole frame plus its NULL reports nothing written: half a STOMP
// frame is a different frame to whatever reads it next.
void test_build_refuses_a_short_buffer(void)
{
    static const char *const NAMES[] = {"destination"};
    static const char *const VALUES[] = {"/queue/a"};
    char small[16];

    Stomp.buf.out = small;
    Stomp.buf.cap = sizeof(small);
    Stomp.build_args.command = "SEND";
    Stomp.build_args.header_names = NAMES;
    Stomp.build_args.header_values = VALUES;
    Stomp.build_args.header_count = 1;
    Stomp.build_args.body = NULL;
    Stomp.build_args.body_len = 0;
    Stomp.build(stomp_work);
    TEST_ASSERT_FALSE(Stomp.ok);
    TEST_ASSERT_EQUAL_UINT(0u, Stomp.n);
}

// A null command, a null output buffer, or a header count with no arrays behind it, are each
// reported rather than written through.
void test_build_refuses_missing_arguments(void)
{
    Stomp.buf.out = g_out;
    Stomp.buf.cap = sizeof(g_out);
    Stomp.build_args.command = NULL;
    Stomp.build_args.header_count = 0;
    Stomp.build_args.body = NULL;
    Stomp.build_args.body_len = 0;
    Stomp.build(stomp_work);
    TEST_ASSERT_FALSE(Stomp.ok);

    Stomp.build_args.command = "SEND";
    Stomp.buf.out = NULL;
    Stomp.build(stomp_work);
    TEST_ASSERT_FALSE(Stomp.ok);

    Stomp.buf.out = g_out;
    Stomp.build_args.header_names = NULL;
    Stomp.build_args.header_values = NULL;
    Stomp.build_args.header_count = 1;
    Stomp.build(stomp_work);
    TEST_ASSERT_FALSE(Stomp.ok);
}

// A frame's header entries are sliced out of the caller's buffer, not copied.
void test_parse_slices_the_source(void)
{
    static const char FRAME[] = "SEND\n"
                                "destination:/queue/a\n"
                                "\n"
                                "hi";
    TEST_ASSERT_TRUE(parse(FRAME, sizeof(FRAME)));
    TEST_ASSERT_TRUE(g_frame.command >= FRAME && g_frame.command < FRAME + sizeof(FRAME));
    TEST_ASSERT_TRUE(g_frame.headers[0].name >= FRAME && g_frame.headers[0].name < FRAME + sizeof(FRAME));
    TEST_ASSERT_TRUE(g_frame.body >= FRAME && g_frame.body < FRAME + sizeof(FRAME));
}

// A build then a parse of the same frame reports the command, the entries and the body unchanged,
// for values that carry no octet the encoding escapes.
void test_build_parse_round_trip(void)
{
    static const char *const NAMES[] = {"id", "destination", "ack"};
    static const char *const VALUES[] = {"0", "/queue/foo", "client"};

    Stomp.buf.out = g_out;
    Stomp.buf.cap = sizeof(g_out);
    Stomp.build_args.command = "SUBSCRIBE";
    Stomp.build_args.header_names = NAMES;
    Stomp.build_args.header_values = VALUES;
    Stomp.build_args.header_count = 3;
    Stomp.build_args.body = NULL;
    Stomp.build_args.body_len = 0;
    Stomp.build(stomp_work);
    TEST_ASSERT_TRUE(Stomp.ok);
    const size_t n = Stomp.n;

    TEST_ASSERT_TRUE(parse(g_out, n));
    TEST_ASSERT_EQUAL_UINT(9u, g_frame.command_len);
    TEST_ASSERT_EQUAL_MEMORY("SUBSCRIBE", g_frame.command, 9);
    TEST_ASSERT_EQUAL_UINT(3u, g_frame.header_count);
    TEST_ASSERT_EQUAL_UINT(n, Stomp.consumed);
    for (size_t i = 0; i < 3; i++)
    {
        size_t len = 0;
        const char *v = header(NAMES[i], &len);
        TEST_ASSERT_NOT_NULL(v);
        TEST_ASSERT_EQUAL_UINT(strlen(VALUES[i]), len);
        TEST_ASSERT_EQUAL_MEMORY(VALUES[i], v, len);
    }
}
