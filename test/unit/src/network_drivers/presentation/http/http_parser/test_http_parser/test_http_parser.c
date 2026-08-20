// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the standalone HTTP/1.1 request parser
// (network_drivers/presentation/http/http_parser/http_parser.h).
//
// RFC 9112 sec 6.3 is the load-bearing section: message-body length is what separates one request
// from the next on a persistent connection, and every request-smuggling bug is a disagreement about
// it. test_rfc9112_6_3_body_framing walks its numbered rules - rule 7 (no Content-Length means no
// body), rule 6 (a valid Content-Length is the octet count), rule 5 (an invalid or self-contradicting
// Content-Length is unrecoverable), rule 3 (Transfer-Encoding beside Content-Length is an error) -
// and the rest of the file pins the sec 2.1 message grammar, the sec 3 request-line, and the sec 5
// field-line rules the framing decisions are read out of.

#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "shared/ip/ip.h" // PROTOCORE_IP_STR_MAX for the recovered-client buffer
#include <string.h>

#include <unity.h>

void setUp(void)
{
    for (int i = 0; i < CONN_POOL_SLOTS; i++)
    {
        http_pool[i].slot_id = (uint8_t)i;
        HttpParserV.reset_args.req = &http_pool[i];
        HttpParserV.reset(protocore_http_parser_span());
    }
}
void tearDown(void)
{
}

// Feed every octet of @p raw into slot 0 and hand the request back.
static HttpReq *feed(const char *raw)
{
    HttpReq *r = &http_pool[0];
    HttpParserV.reset_args.req = r;
    HttpParserV.reset(protocore_http_parser_span());
    for (const char *p = raw; *p; p++)
    {
        HttpParserV.feed_args.req = r;
        HttpParserV.feed_args.byte = (uint8_t)*p;
        HttpParserV.feed(protocore_http_parser_span());
    }
    return r;
}

// RFC 9112 sec 2.1: "HTTP-message = start-line CRLF *( field-line CRLF ) CRLF [ message-body ]".
// The shortest conforming request is a request-line, no field lines, and the empty line.
void test_rfc9112_2_1_message_grammar(void)
{
    HttpReq *r = feed("GET / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    TEST_ASSERT_EQUAL_STRING("GET", r->method);
    TEST_ASSERT_EQUAL_STRING("/", r->path);
    TEST_ASSERT_EQUAL_INT(HTTP_11, r->version);
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)r->header_count);
    TEST_ASSERT_EQUAL_UINT(0u, r->body_len);

    // The empty line is required. Stopping one CRLF short leaves the parser mid-message.
    r = feed("GET / HTTP/1.1\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_HEADER_KEY, r->parse_state);
}

// RFC 9112 sec 6.3, the numbered rules for message body length, in the order they take precedence.
void test_rfc9112_6_3_body_framing(void)
{
    // Rule 7: "If this is a request message and none of the above are true, then the message body
    // length is zero (no message body is present)."
    HttpReq *r = feed("GET /x HTTP/1.1\r\nHost: a\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    TEST_ASSERT_EQUAL_UINT(0u, r->content_length);
    TEST_ASSERT_EQUAL_UINT(0u, r->body_len);

    // Rule 6: "If a valid Content-Length header field is present without Transfer-Encoding, its
    // decimal value defines the expected message body length in octets."
    r = feed("POST /x HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    TEST_ASSERT_EQUAL_UINT(5u, r->content_length);
    TEST_ASSERT_EQUAL_UINT(5u, r->body_len);
    TEST_ASSERT_EQUAL_STRING("hello", (const char *)r->body);

    // One octet short of the declared length is not a complete message.
    r = feed("POST /x HTTP/1.1\r\nContent-Length: 5\r\n\r\nhell");
    TEST_ASSERT_EQUAL_INT(PARSE_BODY, r->parse_state);

    // Rule 5: "If a message is received without Transfer-Encoding and with an invalid Content-Length
    // header field, then the message framing is invalid and the recipient MUST treat it as an
    // unrecoverable error". RFC 9110 sec 8.6: "Content-Length = 1*DIGIT".
    r = feed("POST /x HTTP/1.1\r\nContent-Length: abc\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, r->parse_state);
    r = feed("POST /x HTTP/1.1\r\nContent-Length: \r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, r->parse_state);
    r = feed("POST /x HTTP/1.1\r\nContent-Length: 5x\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, r->parse_state);

    // Rule 5's escape hatch: repeated values are accepted only when "all values in the list are the
    // same"; two that disagree are the unrecoverable error.
    r = feed("POST /x HTTP/1.1\r\nContent-Length: 3\r\nContent-Length: 3\r\n\r\nabc");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    TEST_ASSERT_EQUAL_UINT(3u, r->content_length);
    r = feed("POST /x HTTP/1.1\r\nContent-Length: 3\r\nContent-Length: 4\r\n\r\nabc");
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, r->parse_state);

    // Rule 3: "If a message is received with both a Transfer-Encoding and a Content-Length header
    // field ... ought to be handled as an error." This parser decodes no transfer coding at all, so
    // it fails closed on any Transfer-Encoding.
    r = feed("POST /x HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, r->parse_state);
    r = feed("POST /x HTTP/1.1\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\nabc");
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, r->parse_state);
    // Field names are case-insensitive (RFC 9110 sec 5.1), so the lower-case spelling fails too.
    r = feed("POST /x HTTP/1.1\r\ntransfer-encoding: gzip\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, r->parse_state);
}

// RFC 9112 sec 3: "request-line = method SP request-target SP HTTP-version", and sec 3.1: "The
// request method is case-sensitive." sec 3.2.1: "origin-form = absolute-path [ '?' query ]".
void test_rfc9112_3_request_line(void)
{
    HttpReq *r = feed("DELETE /a/b/c HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    TEST_ASSERT_EQUAL_STRING("DELETE", r->method);
    TEST_ASSERT_EQUAL_STRING("/a/b/c", r->path);

    // The method is stored as sent: "get" is a different token from "GET".
    r = feed("get / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    TEST_ASSERT_EQUAL_STRING("get", r->method);

    // sec 3.1: "method = token"; RFC 9110 sec 5.6.2 excludes the delimiters "(),/:;<=>?@[\]{}" and
    // DQUOTE from tchar, so a method carrying one is malformed.
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, feed("GE(T / HTTP/1.1\r\n\r\n")->parse_state);
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, feed("G/T / HTTP/1.1\r\n\r\n")->parse_state);

    // sec 3.2.1: everything after '?' is the query, and it is not part of the path.
    r = feed("GET /search?q=hello&n=2 HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    TEST_ASSERT_EQUAL_STRING("/search", r->path);
    TEST_ASSERT_EQUAL_STRING("q=hello&n=2", r->query);
    TEST_ASSERT_EQUAL_UINT(2u, (unsigned)r->query_count);
    HttpParserV.get_query_args.req = r;
    HttpParserV.get_query_args.key = "q";
    HttpParserV.get_query(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_STRING("hello", HttpParserV.text);
    HttpParserV.get_query_args.req = r;
    HttpParserV.get_query_args.key = "n";
    HttpParserV.get_query(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_STRING("2", HttpParserV.text);
    HttpParserV.get_query_args.req = r;
    HttpParserV.get_query_args.key = "missing";
    HttpParserV.get_query(protocore_http_parser_span());
    TEST_ASSERT_NULL(HttpParserV.text);

    // A key with no "=" carries an empty value; the key is still present.
    r = feed("GET /?flag&k=v HTTP/1.1\r\n\r\n");
    HttpParserV.get_query_args.req = r;
    HttpParserV.get_query_args.key = "flag";
    HttpParserV.get_query(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_STRING("", HttpParserV.text);
    HttpParserV.get_query_args.req = r;
    HttpParserV.get_query_args.key = "k";
    HttpParserV.get_query(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_STRING("v", HttpParserV.text);
}

// RFC 9112 sec 2.3: "HTTP-version = HTTP-name '/' DIGIT '.' DIGIT", "HTTP-name = %s'HTTP'". The %s
// prefix is ABNF's case-sensitive form, and the section states "HTTP-version is case-sensitive".
void test_rfc9112_2_3_version_is_case_sensitive(void)
{
    TEST_ASSERT_EQUAL_INT(HTTP_11, feed("GET / HTTP/1.1\r\n\r\n")->version);
    TEST_ASSERT_EQUAL_INT(HTTP_10, feed("GET / HTTP/1.0\r\n\r\n")->version);

    TEST_ASSERT_EQUAL_INT(HTTP_UNKNOWN, feed("GET / http/1.1\r\n\r\n")->version);
    TEST_ASSERT_EQUAL_INT(HTTP_UNKNOWN, feed("GET / HTTP/2.0\r\n\r\n")->version);
    TEST_ASSERT_EQUAL_INT(HTTP_UNKNOWN, feed("GET / HTTP/1.2\r\n\r\n")->version);
    TEST_ASSERT_EQUAL_INT(HTTP_UNKNOWN, feed("GET / GARBAGE\r\n\r\n")->version);
}

// RFC 9112 sec 5: "field-line = field-name ':' OWS field-value OWS", and RFC 9110 sec 5.1: "Field
// names are case-insensitive."
void test_rfc9112_5_field_lines(void)
{
    HttpReq *r = feed("GET / HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "X-Trace: abc123\r\n"
                      "Content-Type: text/plain\r\n"
                      "\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    TEST_ASSERT_EQUAL_UINT(3u, (unsigned)r->header_count);
    HttpParserV.get_header_args.req = r;
    HttpParserV.get_header_args.key = "Host";
    HttpParserV.get_header(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_STRING("example.com", HttpParserV.text);
    HttpParserV.get_header_args.req = r;
    HttpParserV.get_header_args.key = "HOST";
    HttpParserV.get_header(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_STRING("example.com", HttpParserV.text);
    HttpParserV.get_header_args.req = r;
    HttpParserV.get_header_args.key = "host";
    HttpParserV.get_header(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_STRING("example.com", HttpParserV.text);
    HttpParserV.get_header_args.req = r;
    HttpParserV.get_header_args.key = "x-trace";
    HttpParserV.get_header(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_STRING("abc123", HttpParserV.text);
    HttpParserV.get_header_args.req = r;
    HttpParserV.get_header_args.key = "X-Absent";
    HttpParserV.get_header(protocore_http_parser_span());
    TEST_ASSERT_NULL(HttpParserV.text);

    // sec 5.1: "OWS occurring before the first non-whitespace octet of the field line value, or
    // after the last non-whitespace octet ... is excluded by parsers when extracting the field line
    // value". Both spellings below therefore carry the same field value.
    r = feed("GET / HTTP/1.1\r\nX-A:\tone\r\nX-B:   two   \r\nX-C:three\r\n\r\n");
    HttpParserV.get_header_args.req = r;
    HttpParserV.get_header_args.key = "X-A";
    HttpParserV.get_header(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_STRING("one", HttpParserV.text);
    HttpParserV.get_header_args.req = r;
    HttpParserV.get_header_args.key = "X-B";
    HttpParserV.get_header(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_STRING("two", HttpParserV.text);
    HttpParserV.get_header_args.req = r;
    HttpParserV.get_header_args.key = "X-C";
    HttpParserV.get_header(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_STRING("three", HttpParserV.text);

    // An empty field value is legal.
    r = feed("GET / HTTP/1.1\r\nX-Empty:\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    HttpParserV.get_header_args.req = r;
    HttpParserV.get_header_args.key = "X-Empty";
    HttpParserV.get_header(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_STRING("", HttpParserV.text);
}

// RFC 9112 sec 5.1: "No whitespace is allowed between the field name and colon ... A server MUST
// reject, with a response status code of 400 (Bad Request), any received request message that
// contains whitespace between a header field name and colon."
void test_rfc9112_5_1_space_before_colon_is_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, feed("GET / HTTP/1.1\r\nHost : a\r\n\r\n")->parse_state);
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, feed("GET / HTTP/1.1\r\nHost\t: a\r\n\r\n")->parse_state);

    // RFC 9110 sec 5.6.2: a field name is a token, so a delimiter inside it is malformed too.
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, feed("GET / HTTP/1.1\r\nHo(st: a\r\n\r\n")->parse_state);
}

// RFC 9112 sec 3.2: "A server MUST respond with a 400 (Bad Request) status code ... to any request
// message that contains more than one Host header field line".
void test_rfc9112_3_2_duplicate_host_is_rejected(void)
{
    HttpReq *r = feed("GET / HTTP/1.1\r\nHost: a.example\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)r->host_count);

    r = feed("GET / HTTP/1.1\r\nHost: a.example\r\nHost: b.example\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, r->parse_state);

    // The count is name-case-insensitive, so a mixed-case second line is still a second Host.
    r = feed("GET / HTTP/1.1\r\nHost: a.example\r\nhOsT: b.example\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, r->parse_state);
}

// RFC 9112 sec 2.2: the line terminator for the start-line and fields "is the sequence CRLF"; the
// same section makes recognizing a bare LF a MAY, which this parser declines. A CR not followed by
// its LF is malformed, and a message terminated only by LF never completes.
void test_rfc9112_2_2_requires_crlf(void)
{
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, feed("GET / HTTP/1.1\rX")->parse_state);
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, feed("GET / HTTP/1.1\r\nHost: a\r\n\rX")->parse_state);
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, feed("GET / HTTP/1.1\r\nHost: a\r\rX")->parse_state);

    // LF alone terminates nothing, so the message stays unfinished rather than being accepted.
    TEST_ASSERT_NOT_EQUAL(PARSE_COMPLETE, feed("GET / HTTP/1.1\nHost: a\n\n")->parse_state);
}

// RFC 9110 sec 15.5.15 (414 URI Too Long) and sec 15.5.14 (413 Content Too Large): a request-target
// or a declared content length past this build's storage is reported as its own terminal state, not
// as a generic parse error, so the caller can answer with the right status code.
void test_capacity_limits_get_their_own_terminal_states(void)
{
    // MAX_PATH_LEN-1 path octets fit; one more is 414.
    char req[MAX_PATH_LEN + 64];
    size_t i = 0;
    const char *head = "GET ";
    for (const char *p = head; *p; p++)
    {
        req[i++] = *p;
    }
    size_t path_start = i;
    for (size_t k = 0; k < MAX_PATH_LEN - 1; k++)
    {
        req[i++] = (k == 0) ? '/' : 'a';
    }
    const char *tail = " HTTP/1.1\r\n\r\n";
    for (const char *p = tail; *p; p++)
    {
        req[i++] = *p;
    }
    req[i] = '\0';
    HttpReq *r = feed(req);
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    TEST_ASSERT_EQUAL_UINT(MAX_PATH_LEN - 1, r->path_idx);

    // Push one more octet into the path.
    for (size_t k = i; k > path_start; k--)
    {
        req[k] = req[k - 1];
    }
    req[path_start] = '/';
    req[i + 1] = '\0';
    r = feed(req);
    TEST_ASSERT_EQUAL_INT(PARSE_URI_TOO_LONG, r->parse_state);

    // A Content-Length past BODY_BUF_SIZE is 413, decided at the blank line before any body octet
    // is read. BODY_BUF_SIZE is 256 here, so 257 is the first refused length.
    r = feed("POST / HTTP/1.1\r\nContent-Length: 257\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_ENTITY_TOO_LARGE, r->parse_state);
    r = feed("POST / HTTP/1.1\r\nContent-Length: 256\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_BODY, r->parse_state);
}

// Once a message has reached a terminal state, later octets belong to the next message and must not
// move this one: a pipelined second request would otherwise be folded into the first.
void test_terminal_states_ignore_further_octets(void)
{
    HttpReq *r = feed("GET / HTTP/1.1\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    for (const char *p = "GET /second HTTP/1.1\r\n\r\n"; *p; p++)
    {
        HttpParserV.feed_args.req = r;
        HttpParserV.feed_args.byte = (uint8_t)*p;
        HttpParserV.feed(protocore_http_parser_span());
    }
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    TEST_ASSERT_EQUAL_STRING("/", r->path);

    r = feed("GET / HTTP/1.1\r\nHost : a\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, r->parse_state);
    for (const char *p = "GET / HTTP/1.1\r\n\r\n"; *p; p++)
    {
        HttpParserV.feed_args.req = r;
        HttpParserV.feed_args.byte = (uint8_t)*p;
        HttpParserV.feed(protocore_http_parser_span());
    }
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, r->parse_state);
}

// http_parser_reset returns a context to the start state and clears every field it accumulated,
// keeping only the slot identity: a recycled connection must not carry the previous request's
// method, path, headers, or body into the next one.
void test_reset_clears_everything_but_the_slot(void)
{
    HttpReq *r = &http_pool[3];
    r->slot_id = 3;
    HttpParserV.reset_args.req = r;
    HttpParserV.reset(protocore_http_parser_span());
    for (const char *p = "POST /a?x=1 HTTP/1.1\r\nHost: h\r\nContent-Length: 3\r\n\r\nabc"; *p; p++)
    {
        HttpParserV.feed_args.req = r;
        HttpParserV.feed_args.byte = (uint8_t)*p;
        HttpParserV.feed(protocore_http_parser_span());
    }
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);

    HttpParserV.reset_args.req = r;
    HttpParserV.reset(protocore_http_parser_span());
    TEST_ASSERT_EQUAL_INT(PARSE_METHOD, r->parse_state);
    TEST_ASSERT_EQUAL_UINT8(3u, r->slot_id);
    TEST_ASSERT_EQUAL_CHAR('\0', r->method[0]);
    TEST_ASSERT_EQUAL_CHAR('\0', r->path[0]);
    TEST_ASSERT_EQUAL_CHAR('\0', r->query[0]);
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)r->header_count);
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)r->query_count);
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)r->host_count);
    TEST_ASSERT_EQUAL_UINT(0u, r->content_length);
    TEST_ASSERT_EQUAL_UINT(0u, r->body_len);
    TEST_ASSERT_EQUAL_INT(HTTP_UNKNOWN, r->version);
}

// Feeding the same message one octet at a time and in one run are the same message: the parser
// keeps no state outside the context, so a segment boundary anywhere is invisible.
void test_segmentation_does_not_change_the_parse(void)
{
    static const char REQ[] = "POST /api/v1?a=1 HTTP/1.1\r\n"
                              "Host: h.example\r\n"
                              "Content-Type: text/plain\r\n"
                              "Content-Length: 11\r\n"
                              "\r\n"
                              "hello world";
    HttpReq *whole = feed(REQ);
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, whole->parse_state);

    // Same octets, but the second context is inspected after every single feed call.
    HttpReq *bit = &http_pool[1];
    HttpParserV.reset_args.req = bit;
    HttpParserV.reset(protocore_http_parser_span());
    for (size_t i = 0; REQ[i]; i++)
    {
        HttpParserV.feed_args.req = bit;
        HttpParserV.feed_args.byte = (uint8_t)REQ[i];
        HttpParserV.feed(protocore_http_parser_span());
    }
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, bit->parse_state);
    TEST_ASSERT_EQUAL_STRING(whole->method, bit->method);
    TEST_ASSERT_EQUAL_STRING(whole->path, bit->path);
    TEST_ASSERT_EQUAL_STRING(whole->query, bit->query);
    TEST_ASSERT_EQUAL_UINT(whole->content_length, bit->content_length);
    TEST_ASSERT_EQUAL_UINT(whole->body_len, bit->body_len);
    TEST_ASSERT_EQUAL_STRING((const char *)whole->body, (const char *)bit->body);
    TEST_ASSERT_EQUAL_UINT((unsigned)whole->header_count, (unsigned)bit->header_count);
}

// Storage caps are capacity limits, not protocol errors: headers past MAX_HEADERS are not stored,
// but Host and Content-Length are still counted from every field line, because sec 3.2 and sec 6.3
// enforcement cannot depend on how many slots happen to be free.
void test_headers_past_the_cap_still_frame_the_message(void)
{
    HttpReq *r = feed("POST / HTTP/1.1\r\n"
                      "X-1: 1\r\nX-2: 2\r\nX-3: 3\r\nX-4: 4\r\n"
                      "X-5: 5\r\nX-6: 6\r\nX-7: 7\r\nX-8: 8\r\n"
                      "X-9: 9\r\n"
                      "Content-Length: 2\r\n"
                      "\r\nhi");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    TEST_ASSERT_EQUAL_UINT((unsigned)MAX_HEADERS, (unsigned)r->header_count);
    TEST_ASSERT_EQUAL_UINT(2u, r->content_length);
    TEST_ASSERT_EQUAL_STRING("hi", (const char *)r->body);
    HttpParserV.get_header_args.req = r;
    HttpParserV.get_header_args.key = "Content-Length";
    HttpParserV.get_header(protocore_http_parser_span());
    TEST_ASSERT_NULL(HttpParserV.text); // past the cap: counted, not stored

    // The same for Host: the duplicate is caught even when neither line was stored.
    r = feed("GET / HTTP/1.1\r\n"
             "X-1: 1\r\nX-2: 2\r\nX-3: 3\r\nX-4: 4\r\n"
             "X-5: 5\r\nX-6: 6\r\nX-7: 7\r\nX-8: 8\r\n"
             "Host: a\r\nHost: b\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR, r->parse_state);
}

// RFC 6265 sec 4.2.1: "cookie-string = cookie-pair *( ';' SP cookie-pair )". Cookie names are
// case-sensitive (sec 4.1.1 nests the name in a token, matched literally).
void test_rfc6265_cookie_extraction(void)
{
    char out[64];
    HttpReq *r = feed("GET / HTTP/1.1\r\nCookie: sid=abc123; theme=dark\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);

    HttpParserV.get_cookie_args.req = r;
    HttpParserV.get_cookie_args.name = "sid";
    HttpParserV.get_cookie_args.out = out;
    HttpParserV.get_cookie_args.out_size = sizeof(out);
    HttpParserV.get_cookie(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("abc123", out);
    HttpParserV.get_cookie_args.req = r;
    HttpParserV.get_cookie_args.name = "theme";
    HttpParserV.get_cookie_args.out = out;
    HttpParserV.get_cookie_args.out_size = sizeof(out);
    HttpParserV.get_cookie(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("dark", out);

    // Absent, and case-mismatched, both report false and leave an empty string.
    HttpParserV.get_cookie_args.req = r;
    HttpParserV.get_cookie_args.name = "nope";
    HttpParserV.get_cookie_args.out = out;
    HttpParserV.get_cookie_args.out_size = sizeof(out);
    HttpParserV.get_cookie(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("", out);
    HttpParserV.get_cookie_args.req = r;
    HttpParserV.get_cookie_args.name = "SID";
    HttpParserV.get_cookie_args.out = out;
    HttpParserV.get_cookie_args.out_size = sizeof(out);
    HttpParserV.get_cookie(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);

    // sec 4.1.1: cookie-value may be a DQUOTE-wrapped span; the quotes are not part of the value.
    r = feed("GET / HTTP/1.1\r\nCookie: q=\"quoted value\"\r\n\r\n");
    HttpParserV.get_cookie_args.req = r;
    HttpParserV.get_cookie_args.name = "q";
    HttpParserV.get_cookie_args.out = out;
    HttpParserV.get_cookie_args.out_size = sizeof(out);
    HttpParserV.get_cookie(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("quoted value", out);

    // A name that is a prefix of a stored one must not match it.
    r = feed("GET / HTTP/1.1\r\nCookie: session=one\r\n\r\n");
    HttpParserV.get_cookie_args.req = r;
    HttpParserV.get_cookie_args.name = "sess";
    HttpParserV.get_cookie_args.out = out;
    HttpParserV.get_cookie_args.out_size = sizeof(out);
    HttpParserV.get_cookie(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
    HttpParserV.get_cookie_args.req = r;
    HttpParserV.get_cookie_args.name = "session";
    HttpParserV.get_cookie_args.out = out;
    HttpParserV.get_cookie_args.out_size = sizeof(out);
    HttpParserV.get_cookie(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);

    // No Cookie header at all.
    r = feed("GET / HTTP/1.1\r\n\r\n");
    HttpParserV.get_cookie_args.req = r;
    HttpParserV.get_cookie_args.name = "sid";
    HttpParserV.get_cookie_args.out = out;
    HttpParserV.get_cookie_args.out_size = sizeof(out);
    HttpParserV.get_cookie(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
}

// RFC 7239 sec 4 publishes the Forwarded examples verbatim; sec 6 defines the node identifier as an
// IPv4address, a bracketed IPv6address, "unknown", or an obfnode beginning with "_". Only the first
// two name a client, and sec 6.1 says the IPv6 form "SHOULD comply with ... [RFC5952]", which is the
// canonical text this returns.
void test_rfc7239_forwarded_client(void)
{
    char ip[PROTOCORE_IP_STR_MAX];
    proto_bool https = PROTO_FALSE;

    // sec 4 example: "Forwarded: for=192.0.2.60;proto=http;by=203.0.113.43".
    HttpReq *r = feed("GET / HTTP/1.1\r\nForwarded: for=192.0.2.60;proto=http\r\n\r\n");
    HttpParserV.forwarded_client_args.req = r;
    HttpParserV.forwarded_client_args.ip_out = ip;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(ip);
    HttpParserV.forwarded_client_args.is_https = &https;
    HttpParserV.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("192.0.2.60", ip);
    TEST_ASSERT_FALSE(https);

    // sec 4 example: "Forwarded: for=192.0.2.43, for=198.51.100.17" - the first element is the
    // original client.
    r = feed("GET / HTTP/1.1\r\nForwarded: for=192.0.2.43, for=198.51.100.17\r\n\r\n");
    HttpParserV.forwarded_client_args.req = r;
    HttpParserV.forwarded_client_args.ip_out = ip;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(ip);
    HttpParserV.forwarded_client_args.is_https = &https;
    HttpParserV.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("192.0.2.43", ip);

    // sec 6 example: "[2001:db8:cafe::17]:47011", quoted because ":" is not a token character.
    r = feed("GET / HTTP/1.1\r\nForwarded: For=\"[2001:db8:cafe::17]:4711\"\r\n\r\n");
    HttpParserV.forwarded_client_args.req = r;
    HttpParserV.forwarded_client_args.ip_out = ip;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(ip);
    HttpParserV.forwarded_client_args.is_https = &https;
    HttpParserV.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("2001:db8:cafe::17", ip);

    // proto=https is what sets the flag.
    r = feed("GET / HTTP/1.1\r\nForwarded: for=192.0.2.60;proto=https\r\n\r\n");
    https = PROTO_FALSE;
    HttpParserV.forwarded_client_args.req = r;
    HttpParserV.forwarded_client_args.ip_out = ip;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(ip);
    HttpParserV.forwarded_client_args.is_https = &https;
    HttpParserV.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_TRUE(https);

    // sec 6.2 "unknown" and sec 6.3 obfuscated "_gazonk" name no address, so neither is returned.
    r = feed("GET / HTTP/1.1\r\nForwarded: for=unknown\r\n\r\n");
    HttpParserV.forwarded_client_args.req = r;
    HttpParserV.forwarded_client_args.ip_out = ip;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(ip);
    HttpParserV.forwarded_client_args.is_https = &https;
    HttpParserV.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
    r = feed("GET / HTTP/1.1\r\nForwarded: for=\"_gazonk\"\r\n\r\n");
    HttpParserV.forwarded_client_args.req = r;
    HttpParserV.forwarded_client_args.ip_out = ip;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(ip);
    HttpParserV.forwarded_client_args.is_https = &https;
    HttpParserV.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);

    // The de-facto X-Forwarded-For / -Proto pair, leftmost first.
    r = feed("GET / HTTP/1.1\r\nX-Forwarded-For: 203.0.113.7, 198.51.100.2\r\n"
             "X-Forwarded-Proto: https\r\n\r\n");
    https = PROTO_FALSE;
    HttpParserV.forwarded_client_args.req = r;
    HttpParserV.forwarded_client_args.ip_out = ip;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(ip);
    HttpParserV.forwarded_client_args.is_https = &https;
    HttpParserV.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("203.0.113.7", ip);
    TEST_ASSERT_TRUE(https);

    // An IPv4 with the sec 6 optional ":" node-port keeps only the address.
    r = feed("GET / HTTP/1.1\r\nX-Forwarded-For: 203.0.113.7:47011\r\n\r\n");
    HttpParserV.forwarded_client_args.req = r;
    HttpParserV.forwarded_client_args.ip_out = ip;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(ip);
    HttpParserV.forwarded_client_args.is_https = &https;
    HttpParserV.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("203.0.113.7", ip);

    // Nothing forwarded, and a malformed literal, both report false with an empty result.
    r = feed("GET / HTTP/1.1\r\n\r\n");
    HttpParserV.forwarded_client_args.req = r;
    HttpParserV.forwarded_client_args.ip_out = ip;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(ip);
    HttpParserV.forwarded_client_args.is_https = &https;
    HttpParserV.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("", ip);
    r = feed("GET / HTTP/1.1\r\nX-Forwarded-For: 999.1.1.1\r\n\r\n");
    HttpParserV.forwarded_client_args.req = r;
    HttpParserV.forwarded_client_args.ip_out = ip;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(ip);
    HttpParserV.forwarded_client_args.is_https = &https;
    HttpParserV.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
}

// An application/x-www-form-urlencoded body is the same "&"-separated key=value list the query
// string uses, so a field is read out of the body the same way one is read out of the query.
void test_urlencoded_form_fields(void)
{
    char out[64];
    HttpReq *r = feed("POST /f HTTP/1.1\r\n"
                      "Content-Type: application/x-www-form-urlencoded\r\n"
                      "Content-Length: 19\r\n"
                      "\r\n"
                      "user=bob&pass=s3cr3");
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, r->parse_state);
    HttpParserV.get_form_args.req = r;
    HttpParserV.get_form_args.key = "user";
    HttpParserV.get_form_args.out = out;
    HttpParserV.get_form_args.out_size = sizeof(out);
    HttpParserV.get_form(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("bob", out);
    HttpParserV.get_form_args.req = r;
    HttpParserV.get_form_args.key = "pass";
    HttpParserV.get_form_args.out = out;
    HttpParserV.get_form_args.out_size = sizeof(out);
    HttpParserV.get_form(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("s3cr3", out);
    HttpParserV.get_form_args.req = r;
    HttpParserV.get_form_args.key = "missing";
    HttpParserV.get_form_args.out = out;
    HttpParserV.get_form_args.out_size = sizeof(out);
    HttpParserV.get_form(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("", out);

    // A field with no value is present with an empty one; a prefix of a field name is not a match.
    r = feed("POST /f HTTP/1.1\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: 12\r\n"
             "\r\n"
             "flag&user=bo");
    HttpParserV.get_form_args.req = r;
    HttpParserV.get_form_args.key = "flag";
    HttpParserV.get_form_args.out = out;
    HttpParserV.get_form_args.out_size = sizeof(out);
    HttpParserV.get_form(protocore_http_parser_span());
    TEST_ASSERT_TRUE(HttpParserV.ok);
    TEST_ASSERT_EQUAL_STRING("", out);
    HttpParserV.get_form_args.req = r;
    HttpParserV.get_form_args.key = "use";
    HttpParserV.get_form_args.out = out;
    HttpParserV.get_form_args.out_size = sizeof(out);
    HttpParserV.get_form(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);

    // Any other media type is not a form, whatever the body looks like.
    r = feed("POST /f HTTP/1.1\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: 8\r\n"
             "\r\n"
             "user=bob");
    HttpParserV.get_form_args.req = r;
    HttpParserV.get_form_args.key = "user";
    HttpParserV.get_form_args.out = out;
    HttpParserV.get_form_args.out_size = sizeof(out);
    HttpParserV.get_form(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
}

// The lookup helpers refuse a null destination or a zero capacity rather than writing through it.
void test_lookup_helpers_refuse_a_null_destination(void)
{
    char out[8];
    HttpReq *r = feed("GET / HTTP/1.1\r\nCookie: sid=a\r\n\r\n");

    HttpParserV.get_cookie_args.req = r;
    HttpParserV.get_cookie_args.name = "sid";
    HttpParserV.get_cookie_args.out = NULL;
    HttpParserV.get_cookie_args.out_size = sizeof(out);
    HttpParserV.get_cookie(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
    HttpParserV.get_cookie_args.req = r;
    HttpParserV.get_cookie_args.name = "sid";
    HttpParserV.get_cookie_args.out = out;
    HttpParserV.get_cookie_args.out_size = 0;
    HttpParserV.get_cookie(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
    HttpParserV.get_cookie_args.req = r;
    HttpParserV.get_cookie_args.name = NULL;
    HttpParserV.get_cookie_args.out = out;
    HttpParserV.get_cookie_args.out_size = sizeof(out);
    HttpParserV.get_cookie(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
    HttpParserV.get_form_args.req = r;
    HttpParserV.get_form_args.key = "x";
    HttpParserV.get_form_args.out = NULL;
    HttpParserV.get_form_args.out_size = sizeof(out);
    HttpParserV.get_form(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
    HttpParserV.get_form_args.req = r;
    HttpParserV.get_form_args.key = "x";
    HttpParserV.get_form_args.out = out;
    HttpParserV.get_form_args.out_size = 0;
    HttpParserV.get_form(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
    HttpParserV.forwarded_client_args.req = r;
    HttpParserV.forwarded_client_args.ip_out = NULL;
    HttpParserV.forwarded_client_args.ip_cap = sizeof(out);
    HttpParserV.forwarded_client_args.is_https = NULL;
    HttpParserV.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
    HttpParserV.forwarded_client_args.req = r;
    HttpParserV.forwarded_client_args.ip_out = out;
    HttpParserV.forwarded_client_args.ip_cap = 0;
    HttpParserV.forwarded_client_args.is_https = NULL;
    HttpParserV.forwarded_client(protocore_http_parser_span());
    TEST_ASSERT_FALSE(HttpParserV.ok);
    HttpParserV.get_param_args.req = r;
    HttpParserV.get_param_args.key = NULL;
    HttpParserV.get_param(protocore_http_parser_span());
    TEST_ASSERT_NULL(HttpParserV.text);
    HttpParserV.get_param_args.req = r;
    HttpParserV.get_param_args.key = "id";
    HttpParserV.get_param(protocore_http_parser_span());
    TEST_ASSERT_NULL(HttpParserV.text); // nothing captured a path parameter
}
