// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for multipart/form-data parser (multipart.cpp).
//
// Tests verify that:
//   - single text field is parsed correctly
//   - multiple fields are all parsed
//   - file upload parts carry name, filename, type, and data
//   - missing Content-Type returns false
//   - missing boundary in Content-Type returns false
//   - malformed body (no delimiter found) returns false
//   - pc_multipart_get_field() returns correct value or NULL
//   - part_count is accurate
//   - data_len is accurate
//   - boundary extraction: quoted, unquoted, with extra params
//   - max parts (MAX_MULTIPART_PARTS) are captured; extras ignored
//   - whitespace trimming in Content-Disposition header

#include "network_drivers/presentation/codec/multipart/multipart.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "network_drivers/transport/tcp.h"
#include <stdio.h>

#include <unity.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void reset_slot(uint8_t slot)
{
    conn_pool[slot] = (TcpConn){0};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = CONN_ACTIVE;
    conn_pool[slot].proto = PROTO_HTTP; // dispatch requires an explicit protocol
    conn_pool[slot].pcb = pc_net_host_pcb();
    http_reset(slot);
}

// Feed bytes into a slot's rx ring, the way the lwIP recv callback would.
static void push_rx(TcpConn *c, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        c->rx_buffer[c->rx_head] = (uint8_t)s[i];
        c->rx_head = (c->rx_head + 1) % RX_BUF_SIZE;
    }
}

// Build a complete HTTP POST with multipart body, parse it, return request ptr.
// body_buf is caller-supplied working space (the parser modifies body in-place).
static HttpReq *build_multipart_req(uint8_t slot, const char *boundary, const char *body, char *body_buf,
                                    size_t body_buf_size)
{
    reset_slot(slot);

    // Copy body into working buffer (parser modifies in-place)
    strncpy(body_buf, body, body_buf_size - 1);
    body_buf[body_buf_size - 1] = '\0';

    size_t blen = strlen(body_buf);

    // Build request headers manually
    char hdr[512];
    snprintf(hdr, sizeof(hdr),
             "POST /upload HTTP/1.1\r\n"
             "Content-Type: multipart/form-data; boundary=%s\r\n"
             "Content-Length: %u\r\n"
             "\r\n",
             boundary, (unsigned)blen);

    // Push headers then body into the ring buffer
    TcpConn *c = &conn_pool[slot];
    push_rx(c, hdr, strlen(hdr));
    push_rx(c, body_buf, blen);

    http_parse(slot);
    return &http_pool[slot];
}

// Binary-safe variant: pushes @p blen raw bytes (which may contain NULs) rather than a C-string.
static HttpReq *build_multipart_req_bin(uint8_t slot, const char *boundary, const char *body, size_t blen)
{
    reset_slot(slot);
    char hdr[256];
    snprintf(hdr, sizeof(hdr),
             "POST /upload HTTP/1.1\r\n"
             "Content-Type: multipart/form-data; boundary=%s\r\n"
             "Content-Length: %u\r\n"
             "\r\n",
             boundary, (unsigned)blen);
    TcpConn *c = &conn_pool[slot];
    push_rx(c, hdr, strlen(hdr));
    push_rx(c, body, blen);
    http_parse(slot);
    return &http_pool[slot];
}

void setUp()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        reset_slot((uint8_t)i);
    }
}

void tearDown()
{
}

// ====================================================================
// UNIT TESTS
// ====================================================================

void test_no_content_type_returns_false()
{
    reset_slot(0);
    // Craft a request with no Content-Type
    const char *raw = "POST /upload HTTP/1.1\r\n"
                      "Content-Length: 5\r\n"
                      "\r\n"
                      "hello";
    TcpConn *c = &conn_pool[0];
    for (const char *p = raw; *p; p++)
    {
        size_t next = (c->rx_head + 1) % RX_BUF_SIZE;
        c->rx_buffer[c->rx_head] = (uint8_t)*p;
        c->rx_head = next;
    }
    http_parse(0);

    Multipart mp;
    proto_bool ok = pc_multipart_parse(&http_pool[0], &mp);
    TEST_ASSERT_FALSE(ok);
}

void test_no_boundary_in_content_type_returns_false()
{
    reset_slot(0);
    const char *raw = "POST /upload HTTP/1.1\r\n"
                      "Content-Type: multipart/form-data\r\n"
                      "Content-Length: 5\r\n"
                      "\r\n"
                      "hello";
    TcpConn *c = &conn_pool[0];
    for (const char *p = raw; *p; p++)
    {
        size_t next = (c->rx_head + 1) % RX_BUF_SIZE;
        c->rx_buffer[c->rx_head] = (uint8_t)*p;
        c->rx_head = next;
    }
    http_parse(0);

    Multipart mp;
    TEST_ASSERT_FALSE(pc_multipart_parse(&http_pool[0], &mp));
}

void test_body_missing_delimiter_returns_false()
{
    char buf[256];
    const char *body = "this has no multipart delimiters at all";
    HttpReq *req = build_multipart_req(0, "BOUND", body, buf, sizeof(buf));

    Multipart mp;
    TEST_ASSERT_FALSE(pc_multipart_parse(req, &mp));
}

void test_single_text_field_parsed()
{
    char buf[512];
    const char *body = "--BOUND\r\n"
                       "Content-Disposition: form-data; name=\"field1\"\r\n"
                       "\r\n"
                       "value1\r\n"
                       "--BOUND--\r\n";

    HttpReq *req = build_multipart_req(0, "BOUND", body, buf, sizeof(buf));
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_INT(1, mp.part_count);
    TEST_ASSERT_NOT_NULL(mp.parts[0].name);
    TEST_ASSERT_EQUAL_STRING("field1", mp.parts[0].name);
    TEST_ASSERT_EQUAL_STRING("value1", mp.parts[0].data);
    TEST_ASSERT_EQUAL_UINT(6, mp.parts[0].data_len);
}

void test_two_text_fields_parsed()
{
    char buf[512];
    const char *body = "--BOUND\r\n"
                       "Content-Disposition: form-data; name=\"username\"\r\n"
                       "\r\n"
                       "alice\r\n"
                       "--BOUND\r\n"
                       "Content-Disposition: form-data; name=\"email\"\r\n"
                       "\r\n"
                       "alice@example.com\r\n"
                       "--BOUND--\r\n";

    HttpReq *req = build_multipart_req(0, "BOUND", body, buf, sizeof(buf));
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_INT(2, mp.part_count);

    TEST_ASSERT_EQUAL_STRING("username", mp.parts[0].name);
    TEST_ASSERT_EQUAL_STRING("alice", mp.parts[0].data);

    TEST_ASSERT_EQUAL_STRING("email", mp.parts[1].name);
    TEST_ASSERT_EQUAL_STRING("alice@example.com", mp.parts[1].data);
}

void test_three_text_fields_parsed()
{
    char buf[768];
    const char *body = "--B\r\n"
                       "Content-Disposition: form-data; name=\"a\"\r\n"
                       "\r\n"
                       "AAA\r\n"
                       "--B\r\n"
                       "Content-Disposition: form-data; name=\"b\"\r\n"
                       "\r\n"
                       "BBB\r\n"
                       "--B\r\n"
                       "Content-Disposition: form-data; name=\"c\"\r\n"
                       "\r\n"
                       "CCC\r\n"
                       "--B--\r\n";

    HttpReq *req = build_multipart_req(0, "B", body, buf, sizeof(buf));
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_INT(3, mp.part_count);
    TEST_ASSERT_EQUAL_STRING("AAA", mp.parts[0].data);
    TEST_ASSERT_EQUAL_STRING("BBB", mp.parts[1].data);
    TEST_ASSERT_EQUAL_STRING("CCC", mp.parts[2].data);
}

void test_file_upload_part()
{
    char buf[512];
    const char *body = "--BOUND\r\n"
                       "Content-Disposition: form-data; name=\"file\"; filename=\"test.txt\"\r\n"
                       "Content-Type: text/plain\r\n"
                       "\r\n"
                       "file contents here\r\n"
                       "--BOUND--\r\n";

    HttpReq *req = build_multipart_req(0, "BOUND", body, buf, sizeof(buf));
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_INT(1, mp.part_count);

    TEST_ASSERT_NOT_NULL(mp.parts[0].name);
    TEST_ASSERT_NOT_NULL(mp.parts[0].filename);
    TEST_ASSERT_NOT_NULL(mp.parts[0].type);

    TEST_ASSERT_EQUAL_STRING("file", mp.parts[0].name);
    TEST_ASSERT_EQUAL_STRING("test.txt", mp.parts[0].filename);
    TEST_ASSERT_EQUAL_STRING("text/plain", mp.parts[0].type);
    TEST_ASSERT_EQUAL_STRING("file contents here", mp.parts[0].data);
}

void test_file_upload_with_text_field()
{
    char buf[768];
    const char *body = "--B\r\n"
                       "Content-Disposition: form-data; name=\"desc\"\r\n"
                       "\r\n"
                       "my description\r\n"
                       "--B\r\n"
                       "Content-Disposition: form-data; name=\"upload\"; filename=\"pic.jpg\"\r\n"
                       "Content-Type: image/jpeg\r\n"
                       "\r\n"
                       "JPEG_DATA\r\n"
                       "--B--\r\n";

    HttpReq *req = build_multipart_req(0, "B", body, buf, sizeof(buf));
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_INT(2, mp.part_count);

    TEST_ASSERT_EQUAL_STRING("desc", mp.parts[0].name);
    TEST_ASSERT_EQUAL_STRING("my description", mp.parts[0].data);
    TEST_ASSERT_NULL(mp.parts[0].filename);

    TEST_ASSERT_EQUAL_STRING("upload", mp.parts[1].name);
    TEST_ASSERT_EQUAL_STRING("pic.jpg", mp.parts[1].filename);
    TEST_ASSERT_EQUAL_STRING("image/jpeg", mp.parts[1].type);
    TEST_ASSERT_EQUAL_STRING("JPEG_DATA", mp.parts[1].data);
}

void test_get_field_found()
{
    char buf[512];
    const char *body = "--B\r\n"
                       "Content-Disposition: form-data; name=\"token\"\r\n"
                       "\r\n"
                       "abc123\r\n"
                       "--B--\r\n";

    HttpReq *req = build_multipart_req(0, "B", body, buf, sizeof(buf));
    Multipart mp;
    pc_multipart_parse(req, &mp);

    const char *val = pc_multipart_get_field(&mp, "token");
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_STRING("abc123", val);
}

void test_get_field_not_found_returns_null()
{
    char buf[512];
    const char *body = "--B\r\n"
                       "Content-Disposition: form-data; name=\"x\"\r\n"
                       "\r\n"
                       "val\r\n"
                       "--B--\r\n";

    HttpReq *req = build_multipart_req(0, "B", body, buf, sizeof(buf));
    Multipart mp;
    pc_multipart_parse(req, &mp);

    TEST_ASSERT_NULL(pc_multipart_get_field(&mp, "notexist"));
}

void test_get_field_multiple_fields()
{
    char buf[512];
    const char *body = "--B\r\n"
                       "Content-Disposition: form-data; name=\"first\"\r\n"
                       "\r\n"
                       "one\r\n"
                       "--B\r\n"
                       "Content-Disposition: form-data; name=\"second\"\r\n"
                       "\r\n"
                       "two\r\n"
                       "--B--\r\n";

    HttpReq *req = build_multipart_req(0, "B", body, buf, sizeof(buf));
    Multipart mp;
    pc_multipart_parse(req, &mp);

    TEST_ASSERT_EQUAL_STRING("one", pc_multipart_get_field(&mp, "first"));
    TEST_ASSERT_EQUAL_STRING("two", pc_multipart_get_field(&mp, "second"));
    TEST_ASSERT_NULL(pc_multipart_get_field(&mp, "third"));
}

void test_data_len_is_correct()
{
    char buf[512];
    const char *data_str = "exact length";
    char body[256];
    snprintf(body, sizeof(body),
             "--B\r\n"
             "Content-Disposition: form-data; name=\"d\"\r\n"
             "\r\n"
             "%s\r\n"
             "--B--\r\n",
             data_str);

    HttpReq *req = build_multipart_req(0, "B", body, buf, sizeof(buf));
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_UINT(strlen(data_str), mp.parts[0].data_len);
}

void test_max_parts_captured()
{
    // Build exactly MAX_MULTIPART_PARTS + 1 parts; only MAX_MULTIPART_PARTS
    // should be captured (the extra is silently ignored).
    char body[2048] = {0};
    char *p = body;
    for (int i = 0; i <= MAX_MULTIPART_PARTS; i++)
    {
        p += sprintf(p,
                     "--BND\r\n"
                     "Content-Disposition: form-data; name=\"f%d\"\r\n"
                     "\r\n"
                     "v%d\r\n",
                     i, i);
    }
    p += sprintf(p, "--BND--\r\n");

    char buf[2048];
    HttpReq *req = build_multipart_req(0, "BND", body, buf, sizeof(buf));
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_INT(MAX_MULTIPART_PARTS, mp.part_count);
}

void test_empty_field_value()
{
    char buf[512];
    const char *body = "--B\r\n"
                       "Content-Disposition: form-data; name=\"empty\"\r\n"
                       "\r\n"
                       "\r\n" // empty data -- immediately the next delimiter
                       "--B--\r\n";

    HttpReq *req = build_multipart_req(0, "B", body, buf, sizeof(buf));
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_INT(1, mp.part_count);
    TEST_ASSERT_EQUAL_UINT(0, mp.parts[0].data_len);
}

void test_part_without_filename_has_null_filename()
{
    char buf[512];
    const char *body = "--B\r\n"
                       "Content-Disposition: form-data; name=\"nofile\"\r\n"
                       "\r\n"
                       "data\r\n"
                       "--B--\r\n";

    HttpReq *req = build_multipart_req(0, "B", body, buf, sizeof(buf));
    Multipart mp;
    pc_multipart_parse(req, &mp);
    TEST_ASSERT_NULL(mp.parts[0].filename);
}

void test_part_without_content_type_has_null_type()
{
    char buf[512];
    const char *body = "--B\r\n"
                       "Content-Disposition: form-data; name=\"f\"\r\n"
                       "\r\n"
                       "data\r\n"
                       "--B--\r\n";

    HttpReq *req = build_multipart_req(0, "B", body, buf, sizeof(buf));
    Multipart mp;
    pc_multipart_parse(req, &mp);
    TEST_ASSERT_NULL(mp.parts[0].type);
}

void test_long_boundary_string()
{
    // MAX_VAL_LEN=48 limits the stored Content-Type value.
    // "multipart/form-data; boundary=" is 30 chars, leaving 17 chars for the boundary.
    // Use a 16-char boundary to stay within the stored header value limit.
    const char *boundary = "boundary16chars!";
    char body[512];
    char delim[24];
    snprintf(delim, sizeof(delim), "--%s", boundary);
    snprintf(body, sizeof(body),
             "%s\r\n"
             "Content-Disposition: form-data; name=\"f\"\r\n"
             "\r\n"
             "long_boundary_test\r\n"
             "%s--\r\n",
             delim, delim);

    char buf[512];
    HttpReq *req = build_multipart_req(0, boundary, body, buf, sizeof(buf));
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_STRING("long_boundary_test", mp.parts[0].data);
}

// ====================================================================
// STRESS TESTS
// ====================================================================

void stress_parse_100_requests()
{
    for (int i = 0; i < 100; i++)
    {
        uint8_t slot = (uint8_t)(i % MAX_CONNS);
        char val[16];
        snprintf(val, sizeof(val), "val%d", i);

        char body[256];
        snprintf(body, sizeof(body),
                 "--B\r\n"
                 "Content-Disposition: form-data; name=\"k\"\r\n"
                 "\r\n"
                 "%s\r\n"
                 "--B--\r\n",
                 val);

        char buf[256];
        HttpReq *req = build_multipart_req(slot, "B", body, buf, sizeof(buf));
        Multipart mp;
        TEST_ASSERT_TRUE_MESSAGE(pc_multipart_parse(req, &mp), "parse failed");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(val, mp.parts[0].data, "value mismatch");
    }
}

void stress_get_field_100_lookups()
{
    char buf[512];
    const char *body = "--B\r\n"
                       "Content-Disposition: form-data; name=\"key\"\r\n"
                       "\r\n"
                       "found_it\r\n"
                       "--B--\r\n";

    HttpReq *req = build_multipart_req(0, "B", body, buf, sizeof(buf));
    Multipart mp;
    pc_multipart_parse(req, &mp);

    for (int i = 0; i < 100; i++)
    {
        const char *v = pc_multipart_get_field(&mp, "key");
        TEST_ASSERT_NOT_NULL_MESSAGE(v, "field not found");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("found_it", v, "wrong value");
        TEST_ASSERT_NULL_MESSAGE(pc_multipart_get_field(&mp, "missing"), "expected null");
    }
}

// A binary part whose data contains a NUL byte AND the raw boundary token "--BND"
// (not framed by CRLF) must survive intact - the old strstr parser truncated it.
void test_binary_part_not_truncated()
{
    const unsigned char payload[] = {0x89, 0x50, 0x4E, 0x47, 0x00, 0x1A, '-', '-', 'B', 'N', 'D', 0x00, 0xFF, 0x42};
    const size_t plen = sizeof(payload);

    char body[256];
    size_t n = 0;
    const char *pre = "--BND\r\n"
                      "Content-Disposition: form-data; name=\"f\"; filename=\"a.png\"\r\n"
                      "Content-Type: application/octet-stream\r\n\r\n";
    memcpy(body + n, pre, strlen(pre));
    n += strlen(pre);
    memcpy(body + n, payload, plen);
    n += plen;
    const char *post = "\r\n--BND--\r\n";
    memcpy(body + n, post, strlen(post));
    n += strlen(post);

    HttpReq *req = build_multipart_req_bin(0, "BND", body, n);
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_INT(1, mp.part_count);                   // not split at the embedded token
    TEST_ASSERT_EQUAL_size_t(plen, mp.parts[0].data_len);      // full length, not truncated at NUL / --BND
    TEST_ASSERT_EQUAL_MEMORY(payload, mp.parts[0].data, plen); // bytes intact
    TEST_ASSERT_NOT_NULL(mp.parts[0].filename);
    TEST_ASSERT_EQUAL_STRING("a.png", mp.parts[0].filename);
}

// A quoted boundary value (boundary="BND") parses the same as an unquoted one.
void test_quoted_boundary()
{
    char bb[256];
    const char *body = "--BND\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\nval\r\n--BND--\r\n";
    HttpReq *req = build_multipart_req(0, "\"BND\"", body, bb, sizeof(bb)); // Content-Type: boundary="BND"
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_INT(1, mp.part_count);
    TEST_ASSERT_EQUAL_STRING("val", mp.parts[0].data);
}

// An empty quoted boundary (boundary="") is rejected.
void test_empty_boundary_returns_false()
{
    char bb[128];
    HttpReq *req = build_multipart_req(0, "\"\"", "--\r\n\r\n", bb, sizeof(bb));
    Multipart mp;
    TEST_ASSERT_FALSE(pc_multipart_parse(req, &mp));
}

// An unquoted or unterminated Content-Disposition value yields a null field (not a crash).
void test_malformed_disposition_values()
{
    char bb[256];
    Multipart mp;
    // unquoted name= value
    const char *b1 = "--BND\r\nContent-Disposition: form-data; name=nq\r\n\r\nx\r\n--BND--\r\n";
    HttpReq *r1 = build_multipart_req(0, "BND", b1, bb, sizeof(bb));
    TEST_ASSERT_TRUE(pc_multipart_parse(r1, &mp));
    TEST_ASSERT_EQUAL_INT(1, mp.part_count);
    TEST_ASSERT_NULL(mp.parts[0].name);
    // opening quote with no closing quote
    const char *b2 = "--BND\r\nContent-Disposition: form-data; name=\"unclosed\r\n\r\nx\r\n--BND--\r\n";
    HttpReq *r2 = build_multipart_req(0, "BND", b2, bb, sizeof(bb));
    TEST_ASSERT_TRUE(pc_multipart_parse(r2, &mp));
    TEST_ASSERT_NULL(mp.parts[0].name);
}

// A body shorter than the delimiter finds no boundary (length-bounded search, not an over-read).
void test_body_shorter_than_delimiter()
{
    char bb[64];
    HttpReq *req = build_multipart_req(0, "BND", "--B", bb, sizeof(bb));
    Multipart mp;
    TEST_ASSERT_FALSE(pc_multipart_parse(req, &mp));
}

// A part header with no CRLF, and part data with no closing delimiter, both fail closed.
void test_truncated_part_fails_closed()
{
    char bb[256];
    Multipart mp;
    HttpReq *r1 = build_multipart_req(0, "BND", "--BND\r\nContent-Disposition: form-data; name=\"f\"", bb, sizeof(bb));
    TEST_ASSERT_FALSE(pc_multipart_parse(r1, &mp)); // header without CRLF
    HttpReq *r2 = build_multipart_req(
        0, "BND", "--BND\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\ndata-no-end", bb, sizeof(bb));
    TEST_ASSERT_FALSE(pc_multipart_parse(r2, &mp)); // data without closing "\r\n--boundary"
}

// A boundary value that stops at a ';' (more Content-Type params follow) or a ' ' (unquoted,
// trailing space) - not just at the quote/end-of-header cases the other tests already cover.
void test_boundary_stops_at_semicolon_or_space()
{
    char bb[512];
    Multipart mp;
    const char *b1 = "--BND\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\nv1\r\n--BND--\r\n";
    HttpReq *r1 = build_multipart_req(0, "BND;charset=utf-8", b1, bb, sizeof(bb));
    TEST_ASSERT_TRUE(pc_multipart_parse(r1, &mp));
    TEST_ASSERT_EQUAL_STRING("v1", mp.parts[0].data);

    const char *b2 = "--BND\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\nv2\r\n--BND--\r\n";
    HttpReq *r2 = build_multipart_req(0, "BND extra", b2, bb, sizeof(bb));
    TEST_ASSERT_TRUE(pc_multipart_parse(r2, &mp));
    TEST_ASSERT_EQUAL_STRING("v2", mp.parts[0].data);
}

// A body that is nothing but the terminating boundary (no parts at all) parses with
// part_count == 0, so the overall result is false - the delimiter is found but nothing
// followed by a CRLF ever runs (pos[0] is '-', not '\r', right after the opening delimiter).
void test_empty_multipart_body_has_no_parts()
{
    char bb[64];
    HttpReq *req = build_multipart_req(0, "BND", "--BND--\r\n", bb, sizeof(bb));
    Multipart mp;
    TEST_ASSERT_FALSE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_INT(0, mp.part_count);
}

// A lone '\r' right after the opening delimiter, not followed by '\n', fails closed instead of
// being mistaken for the header/data blank-line separator.
void test_lone_cr_after_delimiter_fails_closed()
{
    char bb[64];
    HttpReq *req = build_multipart_req(0, "BND", "--BND\rzzz", bb, sizeof(bb));
    Multipart mp;
    TEST_ASSERT_FALSE(pc_multipart_parse(req, &mp));
}

// A per-part header line that matches neither Content-Disposition nor Content-Type is simply
// ignored (not a parse error), leaving name/filename/type null; pc_multipart_get_field must
// skip such a nameless part without crashing.
void test_unrecognized_header_line_yields_null_name()
{
    char bb[128];
    const char *body = "--BND\r\n-X\r\n\r\ndata\r\n--BND--\r\n";
    HttpReq *req = build_multipart_req(0, "BND", body, bb, sizeof(bb));
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_INT(1, mp.part_count);
    TEST_ASSERT_NULL(mp.parts[0].name);
    TEST_ASSERT_EQUAL_STRING("data", mp.parts[0].data);
    TEST_ASSERT_NULL(pc_multipart_get_field(&mp, "anything")); // skips the nameless part, no crash
}

// A part's data runs right up to the last byte of the buffer (the closing "\r\n--boundary" is
// found, but nothing - not even "--" or a CRLF - follows it). Every length check guarding the
// next iteration's lookahead must fail closed rather than reading past the buffer.
void test_part_data_ends_exactly_at_buffer_end()
{
    char bb[128];
    HttpReq *req = build_multipart_req(
        0, "BND", "--BND\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\ndata\r\n--BND", bb, sizeof(bb));
    Multipart mp;
    TEST_ASSERT_FALSE(pc_multipart_parse(req, &mp));
}

// The body is exactly the opening delimiter with nothing after it at all (not even a CRLF) -
// the lookahead that skips the delimiter's trailing CRLF must fail closed on a too-short
// remainder instead of reading past the buffer.
void test_delimiter_with_nothing_after_it()
{
    char bb[32];
    HttpReq *req = build_multipart_req(0, "BND", "--BND", bb, sizeof(bb));
    Multipart mp;
    TEST_ASSERT_FALSE(pc_multipart_parse(req, &mp));
}

// After a part's data delimiter, a lone '\r' not followed by '\n' fails closed instead of being
// mistaken for the trailing-CRLF skip ahead of the next part.
void test_lone_cr_after_data_delimiter_fails_closed()
{
    char bb[128];
    const char *body = "--BND\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\ndata\r\n--BND\rZ";
    HttpReq *req = build_multipart_req(0, "BND", body, bb, sizeof(bb));
    Multipart mp;
    TEST_ASSERT_FALSE(pc_multipart_parse(req, &mp));
}

// A Content-Disposition header with no space after the colon still parses (the leading-space
// skip loop simply does zero iterations instead of one).
void test_content_disposition_no_space_after_colon()
{
    char bb[256];
    const char *body = "--BND\r\nContent-Disposition:form-data; name=\"f\"\r\n\r\nval\r\n--BND--\r\n";
    HttpReq *req = build_multipart_req(0, "BND", body, bb, sizeof(bb));
    Multipart mp;
    TEST_ASSERT_TRUE(pc_multipart_parse(req, &mp));
    TEST_ASSERT_EQUAL_STRING("f", mp.parts[0].name);
    TEST_ASSERT_EQUAL_STRING("val", mp.parts[0].data);
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_no_content_type_returns_false);
    RUN_TEST(test_no_boundary_in_content_type_returns_false);
    RUN_TEST(test_body_missing_delimiter_returns_false);
    RUN_TEST(test_single_text_field_parsed);
    RUN_TEST(test_two_text_fields_parsed);
    RUN_TEST(test_three_text_fields_parsed);
    RUN_TEST(test_file_upload_part);
    RUN_TEST(test_file_upload_with_text_field);
    RUN_TEST(test_get_field_found);
    RUN_TEST(test_get_field_not_found_returns_null);
    RUN_TEST(test_get_field_multiple_fields);
    RUN_TEST(test_data_len_is_correct);
    RUN_TEST(test_max_parts_captured);
    RUN_TEST(test_empty_field_value);
    RUN_TEST(test_part_without_filename_has_null_filename);
    RUN_TEST(test_part_without_content_type_has_null_type);
    RUN_TEST(test_long_boundary_string);
    RUN_TEST(stress_parse_100_requests);
    RUN_TEST(stress_get_field_100_lookups);
    RUN_TEST(test_binary_part_not_truncated);
    RUN_TEST(test_quoted_boundary);
    RUN_TEST(test_empty_boundary_returns_false);
    RUN_TEST(test_malformed_disposition_values);
    RUN_TEST(test_body_shorter_than_delimiter);
    RUN_TEST(test_truncated_part_fails_closed);
    RUN_TEST(test_boundary_stops_at_semicolon_or_space);
    RUN_TEST(test_empty_multipart_body_has_no_parts);
    RUN_TEST(test_lone_cr_after_delimiter_fails_closed);
    RUN_TEST(test_unrecognized_header_line_yields_null_name);
    RUN_TEST(test_part_data_ends_exactly_at_buffer_end);
    RUN_TEST(test_content_disposition_no_space_after_colon);
    RUN_TEST(test_delimiter_with_nothing_after_it);
    RUN_TEST(test_lone_cr_after_data_delimiter_fails_closed);

    return UNITY_END();
}
