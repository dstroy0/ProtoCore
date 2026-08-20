// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/presentation/codec/multipart/multipart.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "network_drivers/presentation/presentation.h" // HttpConn: the per-slot request the parser fills
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/tcp.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>

static uint8_t multipart_work[16]; // the borrow an entry takes; Multipart never reads it

static void reset_slot(uint8_t slot)
{
    conn_pool[slot] = (TcpConn){0};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = CONN_ACTIVE;
    conn_pool[slot].proto = PROTO_HTTP;
    conn_pool[slot].pcb = protocore_net_host_pcb();
    HttpConnV.slot = slot;
    HttpConn.reset(protocore_http_conn_span());
}

static void push_rx(TcpConn *c, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        c->rx_buffer[c->rx_head] = (uint8_t)s[i];
        c->rx_head = (c->rx_head + 1) % RX_BUF_SIZE;
    }
}

static HttpReq *build_multipart_req(uint8_t slot, const char *boundary, const char *body, char *body_buf,
                                    size_t body_buf_size)
{
    reset_slot(slot);

    strncpy(body_buf, body, body_buf_size - 1);
    body_buf[body_buf_size - 1] = '\0';

    size_t blen = strlen(body_buf);

    char hdr[512];
    snprintf(hdr, sizeof(hdr),
             "POST /upload HTTP/1.1\r\n"
             "Content-Type: multipart/form-data; boundary=%s\r\n"
             "Content-Length: %u\r\n"
             "\r\n",
             boundary, (unsigned)blen);

    TcpConn *c = &conn_pool[slot];
    push_rx(c, hdr, strlen(hdr));
    push_rx(c, body_buf, blen);

    HttpConnV.slot = slot;
    HttpConn.parse(protocore_http_conn_span());
    return &http_pool[slot];
}

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
    HttpConnV.slot = slot;
    HttpConn.parse(protocore_http_conn_span());
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

void test_no_content_type_returns_false()
{
    reset_slot(0);

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
    HttpConnV.slot = 0;
    HttpConn.parse(protocore_http_conn_span());

    MultipartBody mp;
    MultipartV.parse_args.req = &http_pool[0];
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    proto_bool ok = MultipartV.ok;
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
    HttpConnV.slot = 0;
    HttpConn.parse(protocore_http_conn_span());

    MultipartBody mp;
    MultipartV.parse_args.req = &http_pool[0];
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_FALSE(MultipartV.ok);
}

void test_body_missing_delimiter_returns_false()
{
    char buf[256];
    const char *body = "this has no multipart delimiters at all";
    HttpReq *req = build_multipart_req(0, "BOUND", body, buf, sizeof(buf));

    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_FALSE(MultipartV.ok);
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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);

    MultipartV.get_field_args.mp = &mp;
    MultipartV.get_field_args.field = "token";
    Multipart.get_field(multipart_work);
    const char *val = MultipartV.text;
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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);

    MultipartV.get_field_args.mp = &mp;
    MultipartV.get_field_args.field = "notexist";
    Multipart.get_field(multipart_work);
    TEST_ASSERT_NULL(MultipartV.text);
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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);

    MultipartV.get_field_args.mp = &mp;
    MultipartV.get_field_args.field = "first";
    Multipart.get_field(multipart_work);
    TEST_ASSERT_EQUAL_STRING("one", MultipartV.text);
    MultipartV.get_field_args.mp = &mp;
    MultipartV.get_field_args.field = "second";
    Multipart.get_field(multipart_work);
    TEST_ASSERT_EQUAL_STRING("two", MultipartV.text);
    MultipartV.get_field_args.mp = &mp;
    MultipartV.get_field_args.field = "third";
    Multipart.get_field(multipart_work);
    TEST_ASSERT_NULL(MultipartV.text);
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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
    TEST_ASSERT_EQUAL_UINT(strlen(data_str), mp.parts[0].data_len);
}

void test_max_parts_captured()
{

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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
    TEST_ASSERT_EQUAL_INT(MAX_MULTIPART_PARTS, mp.part_count);
}

void test_empty_field_value()
{
    char buf[512];
    const char *body = "--B\r\n"
                       "Content-Disposition: form-data; name=\"empty\"\r\n"
                       "\r\n"
                       "\r\n"
                       "--B--\r\n";

    HttpReq *req = build_multipart_req(0, "B", body, buf, sizeof(buf));
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_NULL(mp.parts[0].type);
}

void test_long_boundary_string()
{

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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
    TEST_ASSERT_EQUAL_STRING("long_boundary_test", mp.parts[0].data);
}

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
        MultipartBody mp;
        MultipartV.parse_args.req = req;
        MultipartV.parse_args.mp = &mp;
        Multipart.parse(multipart_work);
        TEST_ASSERT_TRUE_MESSAGE(MultipartV.ok, "parse failed");
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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);

    for (int i = 0; i < 100; i++)
    {
        MultipartV.get_field_args.mp = &mp;
        MultipartV.get_field_args.field = "key";
        Multipart.get_field(multipart_work);
        const char *v = MultipartV.text;
        TEST_ASSERT_NOT_NULL_MESSAGE(v, "field not found");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("found_it", v, "wrong value");
        MultipartV.get_field_args.mp = &mp;
        MultipartV.get_field_args.field = "missing";
        Multipart.get_field(multipart_work);
        TEST_ASSERT_NULL_MESSAGE(MultipartV.text, "expected null");
    }
}

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
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
    TEST_ASSERT_EQUAL_INT(1, mp.part_count);
    TEST_ASSERT_EQUAL_size_t(plen, mp.parts[0].data_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, mp.parts[0].data, plen);
    TEST_ASSERT_NOT_NULL(mp.parts[0].filename);
    TEST_ASSERT_EQUAL_STRING("a.png", mp.parts[0].filename);
}

void test_quoted_boundary()
{
    char bb[256];
    const char *body = "--BND\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\nval\r\n--BND--\r\n";
    HttpReq *req = build_multipart_req(0, "\"BND\"", body, bb, sizeof(bb));
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
    TEST_ASSERT_EQUAL_INT(1, mp.part_count);
    TEST_ASSERT_EQUAL_STRING("val", mp.parts[0].data);
}

void test_empty_boundary_returns_false()
{
    char bb[128];
    HttpReq *req = build_multipart_req(0, "\"\"", "--\r\n\r\n", bb, sizeof(bb));
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_FALSE(MultipartV.ok);
}

void test_malformed_disposition_values()
{
    char bb[256];
    MultipartBody mp;

    const char *b1 = "--BND\r\nContent-Disposition: form-data; name=nq\r\n\r\nx\r\n--BND--\r\n";
    HttpReq *r1 = build_multipart_req(0, "BND", b1, bb, sizeof(bb));
    MultipartV.parse_args.req = r1;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
    TEST_ASSERT_EQUAL_INT(1, mp.part_count);
    TEST_ASSERT_NULL(mp.parts[0].name);

    const char *b2 = "--BND\r\nContent-Disposition: form-data; name=\"unclosed\r\n\r\nx\r\n--BND--\r\n";
    HttpReq *r2 = build_multipart_req(0, "BND", b2, bb, sizeof(bb));
    MultipartV.parse_args.req = r2;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
    TEST_ASSERT_NULL(mp.parts[0].name);
}

void test_body_shorter_than_delimiter()
{
    char bb[64];
    HttpReq *req = build_multipart_req(0, "BND", "--B", bb, sizeof(bb));
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_FALSE(MultipartV.ok);
}

void test_truncated_part_fails_closed()
{
    char bb[256];
    MultipartBody mp;
    HttpReq *r1 = build_multipart_req(0, "BND", "--BND\r\nContent-Disposition: form-data; name=\"f\"", bb, sizeof(bb));
    MultipartV.parse_args.req = r1;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_FALSE(MultipartV.ok);
    HttpReq *r2 = build_multipart_req(
        0, "BND", "--BND\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\ndata-no-end", bb, sizeof(bb));
    MultipartV.parse_args.req = r2;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_FALSE(MultipartV.ok);
}

void test_boundary_stops_at_semicolon_or_space()
{
    char bb[512];
    MultipartBody mp;
    const char *b1 = "--BND\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\nv1\r\n--BND--\r\n";
    HttpReq *r1 = build_multipart_req(0, "BND;charset=utf-8", b1, bb, sizeof(bb));
    MultipartV.parse_args.req = r1;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
    TEST_ASSERT_EQUAL_STRING("v1", mp.parts[0].data);

    const char *b2 = "--BND\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\nv2\r\n--BND--\r\n";
    HttpReq *r2 = build_multipart_req(0, "BND extra", b2, bb, sizeof(bb));
    MultipartV.parse_args.req = r2;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
    TEST_ASSERT_EQUAL_STRING("v2", mp.parts[0].data);
}

void test_empty_multipart_body_has_no_parts()
{
    char bb[64];
    HttpReq *req = build_multipart_req(0, "BND", "--BND--\r\n", bb, sizeof(bb));
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_FALSE(MultipartV.ok);
    TEST_ASSERT_EQUAL_INT(0, mp.part_count);
}

void test_lone_cr_after_delimiter_fails_closed()
{
    char bb[64];
    HttpReq *req = build_multipart_req(0, "BND", "--BND\rzzz", bb, sizeof(bb));
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_FALSE(MultipartV.ok);
}

void test_unrecognized_header_line_yields_null_name()
{
    char bb[128];
    const char *body = "--BND\r\n-X\r\n\r\ndata\r\n--BND--\r\n";
    HttpReq *req = build_multipart_req(0, "BND", body, bb, sizeof(bb));
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
    TEST_ASSERT_EQUAL_INT(1, mp.part_count);
    TEST_ASSERT_NULL(mp.parts[0].name);
    TEST_ASSERT_EQUAL_STRING("data", mp.parts[0].data);
    MultipartV.get_field_args.mp = &mp;
    MultipartV.get_field_args.field = "anything";
    Multipart.get_field(multipart_work);
    TEST_ASSERT_NULL(MultipartV.text);
}

void test_part_data_ends_exactly_at_buffer_end()
{
    char bb[128];
    HttpReq *req = build_multipart_req(
        0, "BND", "--BND\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\ndata\r\n--BND", bb, sizeof(bb));
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_FALSE(MultipartV.ok);
}

void test_delimiter_with_nothing_after_it()
{
    char bb[32];
    HttpReq *req = build_multipart_req(0, "BND", "--BND", bb, sizeof(bb));
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_FALSE(MultipartV.ok);
}

void test_lone_cr_after_data_delimiter_fails_closed()
{
    char bb[128];
    const char *body = "--BND\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\ndata\r\n--BND\rZ";
    HttpReq *req = build_multipart_req(0, "BND", body, bb, sizeof(bb));
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_FALSE(MultipartV.ok);
}

void test_content_disposition_no_space_after_colon()
{
    char bb[256];
    const char *body = "--BND\r\nContent-Disposition:form-data; name=\"f\"\r\n\r\nval\r\n--BND--\r\n";
    HttpReq *req = build_multipart_req(0, "BND", body, bb, sizeof(bb));
    MultipartBody mp;
    MultipartV.parse_args.req = req;
    MultipartV.parse_args.mp = &mp;
    Multipart.parse(multipart_work);
    TEST_ASSERT_TRUE(MultipartV.ok);
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
