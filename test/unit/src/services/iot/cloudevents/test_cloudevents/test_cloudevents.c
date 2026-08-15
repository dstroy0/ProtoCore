// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the CloudEvents envelope (services/iot/cloudevents/cloudevents.h).
//
// The load-bearing case is test_binary_mode_published_request. The HTTP Protocol Binding for
// CloudEvents v1.0.2 sec 3.1.3 states the rule - "all CloudEvents context attributes, including
// extensions, MUST be mapped to HTTP headers with the same name as the attribute name but prefixed
// with `ce-`" - and sec 3.1.4 prints a whole POST that obeys it, headers and all. Feeding that
// request through the parser and reading the attributes back off it is the only way to show the
// mapping is the specification's and not this module's, and the `Content-Type` half matters just as
// much: sec 3.1.1 puts `datacontenttype` there and nowhere else, so a reader that looks for a
// `ce-datacontenttype` header finds nothing on a conforming message.
//
// The structured-mode cases use the attribute values the JSON Event Format v1.0.2 sec 3 examples
// print (`com.example.someevent`, `/mycontext`, `A234-1234-1234`, `application/json`) and the
// specversion the core specification fixes at `1.0`.

#include "services/iot/cloudevents/cloudevents.h"
#include <string.h>

#include <unity.h>

static HttpReq g_req;
static char g_out[512];

void setUp(void)
{
    http_parser_reset(&g_req);
    memset(g_out, 0, sizeof(g_out));
    CloudEvents.attr.id = NULL;
    CloudEvents.attr.source = NULL;
    CloudEvents.attr.type = NULL;
    CloudEvents.attr.subject = NULL;
    CloudEvents.attr.datacontenttype = NULL;
    CloudEvents.data.json = NULL;
    CloudEvents.data.str = NULL;
    CloudEvents.envelope.out = g_out;
    CloudEvents.envelope.cap = sizeof(g_out);
    CloudEvents.msg.req = NULL;
}
void tearDown(void)
{
}

// Drive the whole request through the byte-at-a-time parser, the way a socket would.
static void feed(const char *raw)
{
    for (const char *p = raw; *p; p++)
    {
        http_parser_feed(&g_req, (uint8_t)*p);
    }
}

// HTTP Protocol Binding 1.0.2 sec 3.1.4, "Binary Mode Request Example", transcribed. The extension
// header lines and the Content-Length placeholder of the printed example are replaced by a real
// Content-Length and a real body, which is what a parser needs to reach a complete request; every
// `ce-` line and the `Content-Type` are the example's own.
void test_binary_mode_published_request(void)
{
    static const char REQ[] = "POST /someresource HTTP/1.1\r\n"
                              "Host: webhook.example.com\r\n"
                              "ce-specversion: 1.0\r\n"
                              "ce-type: com.example.someevent\r\n"
                              "ce-id: 1234-1234-1234\r\n"
                              "ce-source: /mycontext/subcontext\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: 2\r\n"
                              "\r\n"
                              "{}";
    feed(REQ);
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, g_req.parse_state);

    CloudEvents.msg.req = &g_req;
    CloudEvents.read_binary(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok); // all three REQUIRED attributes arrived

    TEST_ASSERT_EQUAL_STRING("1234-1234-1234", CloudEvents.attr.id);
    TEST_ASSERT_EQUAL_STRING("/mycontext/subcontext", CloudEvents.attr.source);
    TEST_ASSERT_EQUAL_STRING("com.example.someevent", CloudEvents.attr.type);
    // sec 3.1.1: datacontenttype rides in Content-Type, never in a ce- header
    TEST_ASSERT_EQUAL_STRING("application/json", CloudEvents.attr.datacontenttype);
    // sec 3.1.2: the payload is the body, so no attribute carries it
    TEST_ASSERT_NULL(CloudEvents.data.json);
    TEST_ASSERT_NULL(CloudEvents.data.str);
    TEST_ASSERT_EQUAL_UINT(0u, CloudEvents.n);
    // and the body is where the binding says it is
    TEST_ASSERT_EQUAL_STRING("{}", (const char *)g_req.body);
}

// sec 3.1.3.1: `subject` maps to `ce-subject` like every other context attribute, and its absence
// is not an error because the core specification lists it as OPTIONAL.
void test_binary_mode_optional_subject(void)
{
    static const char WITH[] = "POST /x HTTP/1.1\r\n"
                               "Host: h\r\n"
                               "ce-id: A234-1234-1234\r\n"
                               "ce-source: /mycontext\r\n"
                               "ce-type: com.example.someevent\r\n"
                               "ce-subject: mynewfile.jpg\r\n"
                               "\r\n";
    feed(WITH);
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, g_req.parse_state);
    CloudEvents.msg.req = &g_req;
    CloudEvents.read_binary(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    TEST_ASSERT_EQUAL_STRING("mynewfile.jpg", CloudEvents.attr.subject);

    http_parser_reset(&g_req);
    static const char WITHOUT[] = "POST /x HTTP/1.1\r\n"
                                  "Host: h\r\n"
                                  "ce-id: A234-1234-1234\r\n"
                                  "ce-source: /mycontext\r\n"
                                  "ce-type: com.example.someevent\r\n"
                                  "\r\n";
    feed(WITHOUT);
    CloudEvents.msg.req = &g_req;
    CloudEvents.read_binary(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    TEST_ASSERT_NULL(CloudEvents.attr.subject);
    TEST_ASSERT_NULL(CloudEvents.attr.datacontenttype); // no Content-Type on this one either
}

// CloudEvents 1.0.2, "REQUIRED Attributes": `id`, `source` and `type` are required and each "MUST
// be a non-empty string". A message missing any one of them is not an event, so the read reports
// failure even though the message parsed.
void test_binary_mode_requires_id_source_and_type(void)
{
    static const char *const MISSING[] = {
        "POST /x HTTP/1.1\r\nHost: h\r\nce-source: /mycontext\r\nce-type: com.example.someevent\r\n\r\n",
        "POST /x HTTP/1.1\r\nHost: h\r\nce-id: A234\r\nce-type: com.example.someevent\r\n\r\n",
        "POST /x HTTP/1.1\r\nHost: h\r\nce-id: A234\r\nce-source: /mycontext\r\n\r\n",
        "POST /x HTTP/1.1\r\nHost: h\r\n\r\n",
    };
    for (size_t i = 0; i < sizeof(MISSING) / sizeof(MISSING[0]); i++)
    {
        http_parser_reset(&g_req);
        feed(MISSING[i]);
        TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, g_req.parse_state);
        CloudEvents.msg.req = &g_req;
        CloudEvents.read_binary(CloudEvents.internal);
        TEST_ASSERT_FALSE_MESSAGE(CloudEvents.ok, MISSING[i]);
    }

    // no message at all clears every attribute rather than leaving the last read's values behind
    CloudEvents.attr.id = "stale";
    CloudEvents.msg.req = NULL;
    CloudEvents.read_binary(CloudEvents.internal);
    TEST_ASSERT_FALSE(CloudEvents.ok);
    TEST_ASSERT_NULL(CloudEvents.attr.id);
    TEST_ASSERT_NULL(CloudEvents.attr.source);
    TEST_ASSERT_NULL(CloudEvents.attr.type);
}

// HTTP Protocol Binding 1.0.2 sec 3.2 puts the whole event in one JSON object, and the core
// specification's "specversion" section makes a producer "MUST use a value of `1.0`". The
// attribute values are the JSON Event Format sec 3 example's.
void test_structured_mode_required_attributes(void)
{
    CloudEvents.attr.id = "A234-1234-1234";
    CloudEvents.attr.source = "/mycontext";
    CloudEvents.attr.type = "com.example.someevent";
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    TEST_ASSERT_EQUAL_STRING("{\"specversion\":\"1.0\","
                             "\"id\":\"A234-1234-1234\","
                             "\"source\":\"/mycontext\","
                             "\"type\":\"com.example.someevent\"}",
                             g_out);
    TEST_ASSERT_EQUAL_UINT(strlen(g_out), CloudEvents.n);
    TEST_ASSERT_EQUAL_STRING("1.0", PROTOCORE_CLOUDEVENTS_SPECVERSION);
}

// JSON Event Format 1.0.2 sec 3: "Such a representation MUST use the media type
// `application/cloudevents+json`."
void test_structured_mode_media_type(void)
{
    TEST_ASSERT_EQUAL_STRING("application/cloudevents+json", PROTOCORE_CLOUDEVENTS_MEDIA_TYPE);
}

// The core specification's "Event Data" section makes `data` OPTIONAL and says the payload "will be
// encapsulated within `data`". A JSON value goes in as a value, not as a string.
void test_structured_mode_json_data(void)
{
    CloudEvents.attr.id = "C234-1234-1234";
    CloudEvents.attr.source = "/mycontext";
    CloudEvents.attr.type = "com.example.someevent";
    CloudEvents.data.json = "{\"appinfoA\":\"abc\",\"appinfoB\":123,\"appinfoC\":true}";
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    TEST_ASSERT_EQUAL_STRING("{\"specversion\":\"1.0\","
                             "\"id\":\"C234-1234-1234\","
                             "\"source\":\"/mycontext\","
                             "\"type\":\"com.example.someevent\","
                             "\"datacontenttype\":\"application/json\","
                             "\"data\":{\"appinfoA\":\"abc\",\"appinfoB\":123,\"appinfoC\":true}}",
                             g_out);
}

// A plain string payload is emitted as a JSON string, which is the sec 3.1.1 `data` of the
// specification's fourth example, "I'm just a string".
void test_structured_mode_string_data(void)
{
    CloudEvents.attr.id = "D234-1234-1234";
    CloudEvents.attr.source = "/mycontext";
    CloudEvents.attr.type = "com.example.someevent";
    CloudEvents.data.str = "I'm just a string";
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    TEST_ASSERT_EQUAL_STRING("{\"specversion\":\"1.0\","
                             "\"id\":\"D234-1234-1234\","
                             "\"source\":\"/mycontext\","
                             "\"type\":\"com.example.someevent\","
                             "\"data\":\"I'm just a string\"}",
                             g_out);
}

// A stated datacontenttype overrides the implied one, and the second example's
// `"datacontenttype":"application/xml"` with an XML string payload is the shape that shows it.
void test_structured_mode_stated_datacontenttype(void)
{
    CloudEvents.attr.id = "B234-1234-1234";
    CloudEvents.attr.source = "/mycontext";
    CloudEvents.attr.type = "com.example.someevent";
    CloudEvents.attr.datacontenttype = "application/xml";
    CloudEvents.data.str = "<much wow=\"xml\"/>";
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    TEST_ASSERT_EQUAL_STRING("{\"specversion\":\"1.0\","
                             "\"id\":\"B234-1234-1234\","
                             "\"source\":\"/mycontext\","
                             "\"type\":\"com.example.someevent\","
                             "\"datacontenttype\":\"application/xml\","
                             "\"data\":\"<much wow=\\\"xml\\\"/>\"}",
                             g_out);
}

// The core specification: `subject` "MUST be a non-empty string" when present, so an empty string is
// the same as absent and the member is omitted rather than emitted empty.
void test_structured_mode_optional_attributes(void)
{
    CloudEvents.attr.id = "A234-1234-1234";
    CloudEvents.attr.source = "/mycontext";
    CloudEvents.attr.type = "com.example.someevent";
    CloudEvents.attr.subject = "mynewfile.jpg";
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    TEST_ASSERT_EQUAL_STRING("{\"specversion\":\"1.0\","
                             "\"id\":\"A234-1234-1234\","
                             "\"source\":\"/mycontext\","
                             "\"type\":\"com.example.someevent\","
                             "\"subject\":\"mynewfile.jpg\"}",
                             g_out);

    CloudEvents.attr.subject = "";
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    TEST_ASSERT_NULL(strstr(g_out, "subject"));

    // a datacontenttype with no data at all still describes the (absent) payload
    CloudEvents.attr.subject = NULL;
    CloudEvents.attr.datacontenttype = "text/plain";
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "\"datacontenttype\":\"text/plain\""));
    TEST_ASSERT_NULL(strstr(g_out, "\"data\":"));
}

// A build with a REQUIRED attribute absent or empty reports 0 and writes no object: half an
// envelope is not a valid CloudEvent and a consumer would reject it anyway.
void test_structured_mode_refuses_missing_required(void)
{
    static const char *const ID[] = {NULL, "", "ok"};
    static const char *const SOURCE[] = {NULL, "", "/ok"};
    static const char *const TYPE[] = {NULL, "", "com.ok"};
    for (int a = 0; a < 3; a++)
    {
        for (int b = 0; b < 3; b++)
        {
            for (int c = 0; c < 3; c++)
            {
                CloudEvents.attr.id = ID[a];
                CloudEvents.attr.source = SOURCE[b];
                CloudEvents.attr.type = TYPE[c];
                CloudEvents.build_structured(CloudEvents.internal);
                if (a == 2 && b == 2 && c == 2)
                {
                    TEST_ASSERT_TRUE(CloudEvents.ok);
                }
                else
                {
                    TEST_ASSERT_FALSE(CloudEvents.ok);
                    TEST_ASSERT_EQUAL_UINT(0u, CloudEvents.n);
                }
            }
        }
    }
}

// An envelope that does not fit reports 0 rather than a truncated object, and one byte more than
// the object needs is enough.
void test_structured_mode_refuses_a_short_buffer(void)
{
    CloudEvents.attr.id = "A234-1234-1234";
    CloudEvents.attr.source = "/mycontext";
    CloudEvents.attr.type = "com.example.someevent";
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    size_t need = CloudEvents.n;

    CloudEvents.envelope.cap = need + 1;
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    TEST_ASSERT_EQUAL_UINT(need, CloudEvents.n);

    CloudEvents.envelope.cap = need;
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_FALSE(CloudEvents.ok);
    TEST_ASSERT_EQUAL_UINT(0u, CloudEvents.n);

    CloudEvents.envelope.cap = 0;
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_FALSE(CloudEvents.ok);

    CloudEvents.envelope.out = NULL;
    CloudEvents.envelope.cap = sizeof(g_out);
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_FALSE(CloudEvents.ok);
}

// RFC 8259 sec 7: the quotation mark, the reverse solidus and the control characters U+0000 through
// U+001F must be escaped, so an attribute carrying one cannot break out of its JSON string. An
// envelope that could would let a producer inject members into its own event.
void test_attribute_values_are_json_escaped(void)
{
    CloudEvents.attr.id = "a\"b\\c";
    CloudEvents.attr.source = "/my\tcontext";
    CloudEvents.attr.type = "com.example\nsomeevent";
    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    TEST_ASSERT_EQUAL_STRING("{\"specversion\":\"1.0\","
                             "\"id\":\"a\\\"b\\\\c\","
                             "\"source\":\"/my\\tcontext\","
                             "\"type\":\"com.example\\nsomeevent\"}",
                             g_out);
}

// A binary-mode read followed by a structured build must use the read's attributes, which is the
// gateway shape: take an event off the wire in one mode and re-emit it in the other.
void test_binary_read_feeds_a_structured_build(void)
{
    static const char REQ[] = "POST /someresource HTTP/1.1\r\n"
                              "Host: webhook.example.com\r\n"
                              "ce-specversion: 1.0\r\n"
                              "ce-type: com.example.someevent\r\n"
                              "ce-id: 1234-1234-1234\r\n"
                              "ce-source: /mycontext\r\n"
                              "\r\n";
    feed(REQ);
    TEST_ASSERT_EQUAL_INT(PARSE_COMPLETE, g_req.parse_state);

    CloudEvents.msg.req = &g_req;
    CloudEvents.read_binary(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);

    CloudEvents.build_structured(CloudEvents.internal);
    TEST_ASSERT_TRUE(CloudEvents.ok);
    TEST_ASSERT_EQUAL_STRING("{\"specversion\":\"1.0\","
                             "\"id\":\"1234-1234-1234\","
                             "\"source\":\"/mycontext\","
                             "\"type\":\"com.example.someevent\"}",
                             g_out);
}
