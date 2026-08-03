// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for bounded regex routes (PC::on_regex()).

#include "protocore.h" // regex_match: matcher edges that cannot ride in a request-line path
#include <stdio.h>
#include <string.h>
#include <unity.h>

static proto_bool g_called;

static void push_str(uint8_t slot, const char *s)
{
    TcpConn *c = &conn_pool[slot];
    for (size_t i = 0; s[i]; i++)
    {
        size_t next = (c->rx_head + 1) % RX_BUF_SIZE;
        c->rx_buffer[c->rx_head] = (uint8_t)s[i];
        c->rx_head = next;
    }
}

static void h_ok(uint8_t slot, HttpReq *req)
{
    (void)req;
    g_called = PROTO_TRUE;
    send_text(slot, 200, "text/plain", "ok");
}

void setUp()
{
    pc_server_reset();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP; // dispatch requires an explicit protocol
        conn_pool[i].pcb = pc_net_host_pcb();
        http_reset(i);
    }
    ws_init();
    pc_sse_init();
    tcp_capture_reset();
    g_called = PROTO_FALSE;
}

void tearDown()
{
    tcp_capture_disable();
}

// Dispatch one request on a freshly armed slot 0; return whether the handler ran.
static proto_bool hit(const char *method, const char *path)
{
    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP; // dispatch requires an explicit protocol
    conn_pool[0].pcb = pc_net_host_pcb();
    http_reset(0);
    tcp_capture_reset();
    g_called = PROTO_FALSE;
    char req[160];
    snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\n\r\n", method, path);
    push_str(0, req);
    http_parse(0);
    handle();
    return g_called;
}

// ====================================================================
// TESTS
// ====================================================================

void test_numeric_class_plus()
{
    on_regex("/sensor/[0-9]+", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/sensor/42"));
    TEST_ASSERT_TRUE(hit("GET", "/sensor/7"));
    TEST_ASSERT_FALSE(hit("GET", "/sensor/abc")); // non-digit
    TEST_ASSERT_FALSE(hit("GET", "/sensor/"));    // needs >=1 digit
    TEST_ASSERT_FALSE(hit("GET", "/sensor/4a"));  // trailing junk -> no full match
}

void test_dot_star_matches_rest()
{
    on_regex("/files/.*", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/files/")); // .* matches empty
    TEST_ASSERT_TRUE(hit("GET", "/files/a"));
    TEST_ASSERT_TRUE(hit("GET", "/files/deep/a/b")); // '.' matches '/'
    TEST_ASSERT_FALSE(hit("GET", "/other"));
}

void test_escaped_dot_extension()
{
    on_regex("/img/.+\\.png", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/img/cat.png"));
    TEST_ASSERT_FALSE(hit("GET", "/img/cat.jpg"));
    TEST_ASSERT_FALSE(hit("GET", "/img/.png")); // .+ needs >=1 char before ".png"
}

void test_optional_quantifier()
{
    on_regex("/colou?r", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/color"));
    TEST_ASSERT_TRUE(hit("GET", "/colour"));
    TEST_ASSERT_FALSE(hit("GET", "/colouur"));
}

void test_range_class_only()
{
    on_regex("/[a-z]+", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/abc"));
    TEST_ASSERT_FALSE(hit("GET", "/ABC"));
    TEST_ASSERT_FALSE(hit("GET", "/a1"));
}

void test_negated_class()
{
    on_regex("/x[^/]+", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/xabc"));
    TEST_ASSERT_FALSE(hit("GET", "/x/")); // '/' excluded
}

void test_anchored_full_match()
{
    on_regex("/api/v[12]", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/api/v1"));
    TEST_ASSERT_TRUE(hit("GET", "/api/v2"));
    TEST_ASSERT_FALSE(hit("GET", "/api/v3"));
    TEST_ASSERT_FALSE(hit("GET", "/api/v12")); // extra char -> no full match
    TEST_ASSERT_FALSE(hit("GET", "/api/v1/x"));
}

void test_method_still_enforced()
{
    on_regex("/sensor/[0-9]+", HTTP_GET, h_ok);
    // Path matches but method differs -> 405, handler not called.
    TEST_ASSERT_FALSE(hit("POST", "/sensor/42"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "405"));
}

void test_pathological_pattern_terminates_no_match()
{
    // Catastrophic-looking pattern with no possible match: must return (not hang)
    // and report no match, thanks to the RE_MAX_STEPS budget.
    on_regex("/a*a*a*a*a*b", HTTP_GET, h_ok);
    TEST_ASSERT_FALSE(hit("GET", "/aaaaaaaaaaaaaaaaaaaaaaaac"));
}

// Perl-style escape classes \d and \D (digit / non-digit).
void test_escape_class_digit()
{
    on_regex("/d/\\d+", HTTP_GET, h_ok);
    on_regex("/D/\\D+", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/d/42"));   // \d matches digits
    TEST_ASSERT_FALSE(hit("GET", "/d/x"));   // non-digit -> no match
    TEST_ASSERT_TRUE(hit("GET", "/D/abc"));  // \D matches non-digits
    TEST_ASSERT_FALSE(hit("GET", "/D/123")); // digit -> no match
}

// Escape classes \w and \W (word / non-word).
void test_escape_class_word()
{
    on_regex("/w/\\w+", HTTP_GET, h_ok);
    on_regex("/W/\\W+", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/w/ab_9")); // letters/digits/underscore
    TEST_ASSERT_FALSE(hit("GET", "/w/--"));  // '-' is not a word char
    TEST_ASSERT_TRUE(hit("GET", "/W/---"));  // \W matches non-word chars
    TEST_ASSERT_FALSE(hit("GET", "/W/abc")); // word char -> no match
}

// Escape classes \s and \S. A raw space can't ride in a request-line path, so
// \s is exercised as a (non-matching) atom evaluation; \S matches non-space.
void test_escape_class_space()
{
    on_regex("/s/\\S+", HTTP_GET, h_ok);
    on_regex("/z/\\sx", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/s/abc")); // \S matches non-space
    TEST_ASSERT_FALSE(hit("GET", "/z/qx")); // \s vs 'q' -> false (case executes)
}

// Backslash escapes inside a character class: an escaped member [\.] and an
// escaped range bound [0-\9].
void test_class_escaped_members()
{
    on_regex("/c/[\\.]+", HTTP_GET, h_ok);
    on_regex("/r/[0-\\9]+", HTTP_GET, h_ok);
    TEST_ASSERT_TRUE(hit("GET", "/c/...")); // escaped '.' member
    TEST_ASSERT_FALSE(hit("GET", "/c/x"));  // outside the class
    TEST_ASSERT_TRUE(hit("GET", "/r/507")); // escaped range bound 0-9
    TEST_ASSERT_FALSE(hit("GET", "/r/9a")); // 'a' outside 0-9
}

// A lone trailing '\' is a 1-byte atom (there is no escapee behind it) and matches
// nothing, so a pattern ending in one can never complete a match.
void test_trailing_backslash_atom()
{
    TEST_ASSERT_TRUE(regex_match("a", "a")); // control: the same pattern without the '\'
    TEST_ASSERT_FALSE(regex_match("a\\", "a"));
    TEST_ASSERT_FALSE(regex_match("a\\", "ab"));
}

// A ']' immediately after '[' (or '[^') is a literal member, not the terminator.
void test_class_leading_bracket_is_literal()
{
    TEST_ASSERT_TRUE(regex_match("[]]", "]"));
    TEST_ASSERT_FALSE(regex_match("[]]", "x"));
    TEST_ASSERT_TRUE(regex_match("[^]]", "x")); // negated: everything but ']'
    TEST_ASSERT_FALSE(regex_match("[^]]", "]"));
}

// An unterminated class runs to the NUL; the atom then spans to end-of-pattern and
// its last byte is treated as the (missing) ']', so that byte is not a member.
void test_class_unterminated_fails_closed()
{
    TEST_ASSERT_TRUE(regex_match("[abc", "a"));
    TEST_ASSERT_TRUE(regex_match("[abc", "b"));
    TEST_ASSERT_FALSE(regex_match("[abc", "c")); // 'c' lands on the terminator slot
    TEST_ASSERT_FALSE(regex_match("[abc", "x"));
}

// A '\' as the final byte of an unterminated class has nothing to escape: it ends
// the scan rather than consuming two bytes.
void test_class_trailing_backslash_in_body()
{
    TEST_ASSERT_TRUE(regex_match("[a\\", "a"));
    TEST_ASSERT_FALSE(regex_match("[a\\", "\\"));
}

// A '\' directly before the closing ']' cannot consume the ']' (that would run past
// the class), so it is read as a literal backslash member.
void test_class_escaped_bound_at_end()
{
    TEST_ASSERT_TRUE(regex_match("[a\\]", "a"));
    TEST_ASSERT_TRUE(regex_match("[a\\]", "\\"));
    TEST_ASSERT_FALSE(regex_match("[a\\]", "b"));
}

// "[]" has no body at all (its ']' is the leading literal, leaving an empty span):
// it matches no character.
void test_empty_class_matches_nothing()
{
    TEST_ASSERT_FALSE(regex_match("[]", "x"));
    TEST_ASSERT_FALSE(regex_match("[]", "]"));
    TEST_ASSERT_FALSE(regex_match("[]", ""));
}

// A '-' just before the closing ']' has no upper bound to pair with, so it is a
// literal member rather than the start of a range.
void test_class_trailing_dash_is_literal()
{
    TEST_ASSERT_TRUE(regex_match("[a-]+", "a-a"));
    TEST_ASSERT_TRUE(regex_match("[a-]+", "-"));
    TEST_ASSERT_FALSE(regex_match("[a-]+", "b"));
}

// Two ranges in one class: a member of either range matches, and a later
// non-matching range must not clear a hit already recorded by an earlier one.
void test_class_two_ranges()
{
    TEST_ASSERT_TRUE(regex_match("[a-z0-9]+", "a1"));
    TEST_ASSERT_TRUE(regex_match("[a-z0-9]+", "9z"));
    TEST_ASSERT_FALSE(regex_match("[a-z0-9]+", "-"));
}

// \d / \D at the low edge: a byte below '0' is a non-digit just as one above '9' is.
void test_escape_class_digit_low_edge()
{
    TEST_ASSERT_FALSE(regex_match("\\d", "-"));
    TEST_ASSERT_FALSE(regex_match("\\d", "x"));
    TEST_ASSERT_TRUE(regex_match("\\D", "-"));
    TEST_ASSERT_TRUE(regex_match("\\d", "5"));
    TEST_ASSERT_FALSE(regex_match("\\D", "5"));
}

// \w / \W across every sub-range of the word-character test: upper case, digits,
// underscore, and a byte above 'z' that must not be mistaken for a letter.
void test_escape_class_word_edges()
{
    TEST_ASSERT_TRUE(regex_match("\\w", "A"));  // upper-case range
    TEST_ASSERT_TRUE(regex_match("\\w", "9"));  // digit range
    TEST_ASSERT_TRUE(regex_match("\\w", "_"));  // underscore
    TEST_ASSERT_FALSE(regex_match("\\w", "~")); // above 'z': not a word char
    TEST_ASSERT_FALSE(regex_match("\\w", "-"));
    TEST_ASSERT_FALSE(regex_match("\\W", "A"));
    TEST_ASSERT_FALSE(regex_match("\\W", "9"));
    TEST_ASSERT_FALSE(regex_match("\\W", "_"));
    TEST_ASSERT_TRUE(regex_match("\\W", "~"));
    TEST_ASSERT_TRUE(regex_match("\\W", "-"));
}

// \s / \S against the actual whitespace bytes. These cannot ride in a request-line
// path, so the matcher is called directly.
void test_escape_class_space_direct()
{
    TEST_ASSERT_TRUE(regex_match("\\s", " "));
    TEST_ASSERT_TRUE(regex_match("\\s", "\t"));
    TEST_ASSERT_FALSE(regex_match("\\s", "q"));
    TEST_ASSERT_FALSE(regex_match("\\S", " "));
    TEST_ASSERT_FALSE(regex_match("\\S", "\t"));
    TEST_ASSERT_TRUE(regex_match("\\S", "q"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_numeric_class_plus);
    RUN_TEST(test_dot_star_matches_rest);
    RUN_TEST(test_escaped_dot_extension);
    RUN_TEST(test_optional_quantifier);
    RUN_TEST(test_range_class_only);
    RUN_TEST(test_negated_class);
    RUN_TEST(test_anchored_full_match);
    RUN_TEST(test_method_still_enforced);
    RUN_TEST(test_pathological_pattern_terminates_no_match);
    RUN_TEST(test_escape_class_digit);
    RUN_TEST(test_escape_class_word);
    RUN_TEST(test_escape_class_space);
    RUN_TEST(test_class_escaped_members);
    RUN_TEST(test_trailing_backslash_atom);
    RUN_TEST(test_class_leading_bracket_is_literal);
    RUN_TEST(test_class_unterminated_fails_closed);
    RUN_TEST(test_class_trailing_backslash_in_body);
    RUN_TEST(test_class_escaped_bound_at_end);
    RUN_TEST(test_empty_class_matches_nothing);
    RUN_TEST(test_class_trailing_dash_is_literal);
    RUN_TEST(test_class_two_ranges);
    RUN_TEST(test_escape_class_digit_low_edge);
    RUN_TEST(test_escape_class_word_edges);
    RUN_TEST(test_escape_class_space_direct);
    return UNITY_END();
}
