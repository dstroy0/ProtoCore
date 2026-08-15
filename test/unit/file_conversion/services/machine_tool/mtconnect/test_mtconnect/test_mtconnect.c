// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the MTConnect agent response codec (services/machine_tool/mtconnect/mtconnect.h).
//
// Two documents govern every expectation here, and both were obtained:
//
//   The MTConnect 1.4 schemas, fetched from schemas.mtconnect.org/schemas/MTConnect<X>_1.4.xsd
//   (Streams 165977, Devices 74721, Assets 79581, Error 28432 octets). They publish the four
//   targetNamespace URNs verbatim, and they publish which attribute of which element is
//   use="required" - which is what the six failing cases below are asserted against.
//
//   XML 1.0 (Fifth Edition), fetched from www.w3.org/TR/xml/. Sec 2.4: "The ampersand character (&)
//   and the left angle bracket (<) MUST NOT appear in their literal form ... they MUST be escaped
//   using either numeric character references or the strings &amp; and &lt;". Production [10]
//   AttValue ::= '"' ([^<&"] | Reference)* '"'. Sec 3, WFC Element Type Match: "The Name in an
//   element's end-tag MUST match the element type in the start-tag."
//
// No expectation is a golden copy of the emitter's output: every document is run through a checker
// written from those XML 1.0 productions, and every attribute assertion names the xsd line that
// marks it required. test_the_xml_checker_rejects_malformed_documents drives the checker with texts
// XML 1.0 rejects, so it cannot pass by accepting everything.
//
// Six cases are expected to FAIL. Each is a document the codec emits that the 1.4 schema rejects:
//   headers               - MTConnectStreams_1.4.xsd HeaderType (lines 710-718) marks version,
//                           creationTime, nextSequence, lastSequence, firstSequence, instanceId,
//                           sender and bufferSize required; Devices (757-764), Assets (680-686) and
//                           Error (680-685) mark their own sets. mtconnect.c emits instanceId +
//                           version and, per document, some of the rest.
//   DeviceStream          - DeviceStreamType (800-801) requires name AND uuid; mtconnect.c:129 and
//                           :442 emit name only.
//   ComponentStream       - ComponentStreamType (832-835) requires componentId AND component;
//                           mtconnect.c:143 emits component only.
//   category grouping     - ComponentStreamType (809-831) is an xs:sequence of Samples, Events,
//                           Condition, each maxOccurs="1"; mtconnect.c:155-193 opens a fresh wrapper
//                           per observation, so two samples emit two <Samples> and an event before a
//                           sample emits them out of order.
//   CutterStatus          - MTConnectAssets_1.4.xsd CuttingToolLifeCycleType requires CutterStatus
//                           (minOccurs='1', line 1237); mtconnect.c never emits it.
//   ToolLife              - LifeType (1750-1754) requires type, countDirection, limit AND initial;
//                           mtconnect.c:364-377 has no initial at all and treats limit as optional.
//
// The sample-cursor cases are the window arithmetic mtconnect.h lines 162-199 states, with the
// subtraction shown in the comment that carries it.

#include "services/machine_tool/mtconnect/mtconnect.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// ---------------------------------------------------------------------------
// XML 1.0 well-formedness checker, for the subset an agent response uses
// ---------------------------------------------------------------------------

static int xml_name_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':';
}

static int xml_name_char(char c)
{
    return xml_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

static int xml_space(char c)
{
    return c == 0x20 || c == 0x09 || c == 0x0A || c == 0x0D;
}

// Reference ::= EntityRef | CharRef, both closed by a semicolon.
static const char *xml_ref(const char *p)
{
    p++;
    if (*p == '#')
    {
        p++;
        if (*p == 'x')
        {
            p++;
            while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))
            {
                p++;
            }
        }
        else
        {
            while (*p >= '0' && *p <= '9')
            {
                p++;
            }
        }
    }
    else
    {
        if (!xml_name_start(*p))
        {
            return NULL;
        }
        while (xml_name_char(*p))
        {
            p++;
        }
    }
    return (*p == ';') ? p + 1 : NULL;
}

// One root element, every end-tag matching its start-tag (sec 3 WFC Element Type Match), attribute
// values holding no raw < & or " (production [10]), and character data holding no raw < or & (sec
// 2.4). A leading XML declaration is skipped as the prolog.
static int xml_wf(const char *s)
{
    const char *p = s;
    const char *nbeg[32];
    size_t nlen[32];
    int depth = 0;
    int rooted = 0;

    if (strncmp(p, "<?xml", 5) == 0)
    {
        p = strstr(p, "?>");
        if (!p)
        {
            return 0;
        }
        p += 2;
    }
    while (*p)
    {
        if (*p == '<' && p[1] == '/')
        {
            if (depth == 0)
            {
                return 0;
            }
            const char *b = p + 2;
            const char *q = b;
            while (xml_name_char(*q))
            {
                q++;
            }
            depth--;
            if ((size_t)(q - b) != nlen[depth] || strncmp(b, nbeg[depth], nlen[depth]) != 0)
            {
                return 0;
            }
            if (*q != '>')
            {
                return 0;
            }
            p = q + 1;
            continue;
        }
        if (*p == '<')
        {
            if (depth == 0 && rooted)
            {
                return 0;
            }
            const char *b = p + 1;
            const char *q = b;
            if (!xml_name_start(*q))
            {
                return 0;
            }
            while (xml_name_char(*q))
            {
                q++;
            }
            const char *ne = q;
            for (;;)
            {
                while (xml_space(*q))
                {
                    q++;
                }
                if (*q == '>')
                {
                    if (depth >= 32)
                    {
                        return 0;
                    }
                    nbeg[depth] = b;
                    nlen[depth] = (size_t)(ne - b);
                    depth++;
                    rooted = 1;
                    p = q + 1;
                    break;
                }
                if (*q == '/' && q[1] == '>')
                {
                    rooted = 1;
                    p = q + 2;
                    break;
                }
                if (!xml_name_start(*q))
                {
                    return 0;
                }
                while (xml_name_char(*q))
                {
                    q++;
                }
                if (*q != '=')
                {
                    return 0;
                }
                q++;
                if (*q != '"')
                {
                    return 0;
                }
                q++;
                while (*q && *q != '"')
                {
                    if (*q == '<')
                    {
                        return 0;
                    }
                    if (*q == '&')
                    {
                        const char *r = xml_ref(q);
                        if (!r)
                        {
                            return 0;
                        }
                        q = r;
                        continue;
                    }
                    q++;
                }
                if (*q != '"')
                {
                    return 0;
                }
                q++;
            }
            continue;
        }
        if (*p == '&')
        {
            const char *r = xml_ref(p);
            if (!r)
            {
                return 0;
            }
            p = r;
            continue;
        }
        if (depth == 0 && !xml_space(*p))
        {
            return 0;
        }
        p++;
    }
    return rooted && depth == 0;
}

// The first start-tag named @p name, at its '<'.
static const char *find_tag_from(const char *p, const char *name)
{
    const size_t n = strlen(name);
    for (; *p; p++)
    {
        if (*p != '<' || strncmp(p + 1, name, n) != 0)
        {
            continue;
        }
        const char c = p[1 + n];
        if (xml_space(c) || c == '>' || c == '/')
        {
            return p;
        }
    }
    return NULL;
}

static int count_tag(const char *doc, const char *name)
{
    int n = 0;
    for (const char *p = find_tag_from(doc, name); p; p = find_tag_from(p + 1, name))
    {
        n++;
    }
    return n;
}

// Whether the start-tag at @p tag carries @p attr, stepping over quoted values.
static int attr_present(const char *tag, const char *attr)
{
    const size_t n = strlen(attr);
    for (const char *p = tag + 1; *p && *p != '>'; p++)
    {
        if (*p == '"')
        {
            p++;
            while (*p && *p != '"')
            {
                p++;
            }
            if (!*p)
            {
                return 0;
            }
            continue;
        }
        if (xml_space(*p) && strncmp(p + 1, attr, n) == 0 && p[1 + n] == '=')
        {
            return 1;
        }
    }
    return 0;
}

// The value of @p attr on the start-tag at @p tag, with its references decoded.
static void attr_value(const char *tag, const char *attr, char *out, size_t cap)
{
    out[0] = '\0';
    const size_t n = strlen(attr);
    for (const char *p = tag + 1; *p && *p != '>'; p++)
    {
        if (*p == '"')
        {
            p++;
            while (*p && *p != '"')
            {
                p++;
            }
            if (!*p)
            {
                return;
            }
            continue;
        }
        if (!xml_space(*p) || strncmp(p + 1, attr, n) != 0 || p[1 + n] != '=' || p[2 + n] != '"')
        {
            continue;
        }
        const char *v = p + 3 + n;
        size_t k = 0;
        while (*v && *v != '"' && k + 1 < cap)
        {
            if (*v == '&')
            {
                if (strncmp(v, "&amp;", 5) == 0)
                {
                    out[k++] = '&';
                    v += 5;
                    continue;
                }
                if (strncmp(v, "&lt;", 4) == 0)
                {
                    out[k++] = '<';
                    v += 4;
                    continue;
                }
                if (strncmp(v, "&gt;", 4) == 0)
                {
                    out[k++] = '>';
                    v += 4;
                    continue;
                }
                if (strncmp(v, "&quot;", 6) == 0)
                {
                    out[k++] = '"';
                    v += 6;
                    continue;
                }
                if (strncmp(v, "&apos;", 6) == 0)
                {
                    out[k++] = '\'';
                    v += 6;
                    continue;
                }
            }
            out[k++] = *v++;
        }
        out[k] = '\0';
        return;
    }
}

// The character data between the start-tag named @p name and its end-tag, with references decoded.
static void text_of(const char *doc, const char *name, char *out, size_t cap)
{
    out[0] = '\0';
    const char *tag = find_tag_from(doc, name);
    if (!tag)
    {
        return;
    }
    const char *v = strchr(tag, '>');
    if (!v)
    {
        return;
    }
    v++;
    size_t k = 0;
    while (*v && *v != '<' && k + 1 < cap)
    {
        if (*v == '&')
        {
            if (strncmp(v, "&amp;", 5) == 0)
            {
                out[k++] = '&';
                v += 5;
                continue;
            }
            if (strncmp(v, "&lt;", 4) == 0)
            {
                out[k++] = '<';
                v += 4;
                continue;
            }
            if (strncmp(v, "&gt;", 4) == 0)
            {
                out[k++] = '>';
                v += 4;
                continue;
            }
            if (strncmp(v, "&quot;", 6) == 0)
            {
                out[k++] = '"';
                v += 6;
                continue;
            }
            if (strncmp(v, "&apos;", 6) == 0)
            {
                out[k++] = '\'';
                v += 6;
                continue;
            }
        }
        out[k++] = *v++;
    }
    out[k] = '\0';
}

// Collects "<element>:<attribute>" for every required attribute a document left out, so one
// assertion reports the whole gap instead of the first one met.
static char g_missing[512];

static void missing_reset(void)
{
    g_missing[0] = '\0';
}

static void missing_put(const char *s)
{
    size_t n = strlen(g_missing);
    while (*s && n + 1 < sizeof(g_missing))
    {
        g_missing[n++] = *s++;
    }
    g_missing[n] = '\0';
}

static void missing_add(const char *label, const char *attr)
{
    missing_put(label);
    missing_put(":");
    missing_put(attr);
    missing_put(" ");
}

static void require_attrs(const char *doc, const char *tag_name, const char *label, const char *const *attrs, size_t n)
{
    const char *tag = find_tag_from(doc, tag_name);
    if (!tag)
    {
        missing_add(label, "<element absent>");
        return;
    }
    for (size_t i = 0; i < n; i++)
    {
        if (!attr_present(tag, attrs[i]))
        {
            missing_add(label, attrs[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// Cases
// ---------------------------------------------------------------------------

// A checker that accepts everything certifies nothing, so drive it with what XML 1.0 rejects: an
// end-tag naming another element (sec 3 WFC Element Type Match), an unclosed element, a second root
// element, a raw & or < in character data or in an attribute value (sec 2.4, production [10]), and
// an unquoted attribute value.
void test_the_xml_checker_rejects_malformed_documents(void)
{
    static const char *const GOOD[] = {
        "<a/>",
        "<a></a>",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><a b=\"1\"><c d=\"x&amp;y\">t&lt;u</c></a>",
        "<a>plain text</a>",
        "<a b=\"&#38;&#x26;\"/>",
        "<a>]]&gt;</a>",
    };
    static const char *const BAD[] = {
        "<a></b>",        // WFC Element Type Match
        "<a>",            // unclosed
        "</a>",           // end-tag with no start-tag
        "<a/><b/>",       // one root element
        "<a>x&y</a>",     // sec 2.4: a raw ampersand
        "<a>x<y</a>",     // sec 2.4: a raw left angle bracket
        "<a b=\"x&y\"/>", // production [10]: AttValue takes no raw ampersand
        "<a b=\"x<y\"/>", // production [10]: AttValue takes no raw left angle bracket
        "<a b=1/>",       // production [10]: AttValue is quoted
        "<a b/>",         // an attribute is Name Eq AttValue
        "<a>x&amp</a>",   // a Reference is closed by a semicolon
        "text",           // no element
        "",               //
    };
    for (size_t i = 0; i < sizeof(GOOD) / sizeof(GOOD[0]); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(xml_wf(GOOD[i]), GOOD[i]);
    }
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(xml_wf(BAD[i]), BAD[i]);
    }
}

static char g_buf[8192];

static size_t build_streams(void)
{
    protocore_mtc_streams s;
    protocore_mtc_streams_begin(&s, g_buf, sizeof(g_buf), 7, 100, "VF2");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", 1, "2026-01-01T00:00:00Z", "1.5");
    return protocore_mtc_streams_end(&s);
}

static size_t build_devices(void)
{
    protocore_mtc_streams s;
    protocore_mtc_devices_begin(&s, g_buf, sizeof(g_buf), 7, "d1", "VF2", "uuid-1");
    protocore_mtc_devices_add_item(&s, PROTOCORE_MTC_SAMPLE, "Xabs", "POSITION", "Xpos", "MILLIMETER");
    return protocore_mtc_devices_end(&s);
}

static size_t build_assets(void)
{
    protocore_mtc_streams s;
    protocore_mtc_assets_begin(&s, g_buf, sizeof(g_buf), 7, 1, 64);
    protocore_mtc_assets_cutting_tool_begin(&s, "T5", "SN-9", "5", "uuid-1", "2026-01-01T00:00:00Z");
    protocore_mtc_assets_tool_life(&s, "MINUTES", "DOWN", "100", "37");
    protocore_mtc_assets_tool_life(&s, "PART_COUNT", "UP", NULL, "12");
    protocore_mtc_assets_cutting_tool_end(&s);
    return protocore_mtc_assets_end(&s);
}

static size_t build_error(void)
{
    return protocore_mtc_error(42, "OUT_OF_RANGE", "'from' must be <= 99", g_buf, sizeof(g_buf));
}

// Whatever else is wrong with them, all four documents have to parse.
void test_every_document_is_well_formed_xml(void)
{
    TEST_ASSERT_TRUE(build_streams() > 0);
    TEST_ASSERT_TRUE_MESSAGE(xml_wf(g_buf), "MTConnectStreams");
    TEST_ASSERT_TRUE(build_devices() > 0);
    TEST_ASSERT_TRUE_MESSAGE(xml_wf(g_buf), "MTConnectDevices");
    TEST_ASSERT_TRUE(build_assets() > 0);
    TEST_ASSERT_TRUE_MESSAGE(xml_wf(g_buf), "MTConnectAssets");
    TEST_ASSERT_TRUE(build_error() > 0);
    TEST_ASSERT_TRUE_MESSAGE(xml_wf(g_buf), "MTConnectError");

    static protocore_mtc_sample_buffer b;
    protocore_mtc_sample_buffer_init(&b, 1000);
    protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:00Z", "1.0");
    TEST_ASSERT_TRUE(protocore_mtc_sample_query(&b, g_buf, sizeof(g_buf), 7, "VF2", 1000, 10) > 0);
    TEST_ASSERT_TRUE_MESSAGE(xml_wf(g_buf), "sample response");
}

// The four URNs are the targetNamespace each 1.4 schema declares on its xs:schema element (Streams
// line 34, Devices 35, Assets 34, Error 34). A document in another namespace is a document the
// client's parser will not bind to the schema at all.
void test_root_namespaces_are_the_published_target_namespaces(void)
{
    char v[128];

    TEST_ASSERT_TRUE(build_streams() > 0);
    attr_value(find_tag_from(g_buf, "MTConnectStreams"), "xmlns", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("urn:mtconnect.org:MTConnectStreams:1.4", v);

    TEST_ASSERT_TRUE(build_devices() > 0);
    attr_value(find_tag_from(g_buf, "MTConnectDevices"), "xmlns", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("urn:mtconnect.org:MTConnectDevices:1.4", v);

    TEST_ASSERT_TRUE(build_assets() > 0);
    attr_value(find_tag_from(g_buf, "MTConnectAssets"), "xmlns", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("urn:mtconnect.org:MTConnectAssets:1.4", v);

    TEST_ASSERT_TRUE(build_error() > 0);
    attr_value(find_tag_from(g_buf, "MTConnectError"), "xmlns", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("urn:mtconnect.org:MTConnectError:1.4", v);
}

// Each root complexType is an xs:sequence: Header then Streams (MTConnectStreamsType, Streams xsd
// 728-742), Header then Devices, Header then Assets (AssetsType 697-710), Header then Errors
// (Error xsd 696-711). A sequence is ordered, so the Header cannot follow its payload.
void test_each_root_is_a_header_followed_by_its_payload(void)
{
    static const char *const PAYLOAD[4] = {"Streams", "Devices", "Assets", "Errors"};
    for (int i = 0; i < 4; i++)
    {
        switch (i)
        {
        case 0:
            TEST_ASSERT_TRUE(build_streams() > 0);
            break;
        case 1:
            TEST_ASSERT_TRUE(build_devices() > 0);
            break;
        case 2:
            TEST_ASSERT_TRUE(build_assets() > 0);
            break;
        default:
            TEST_ASSERT_TRUE(build_error() > 0);
            break;
        }
        const char *h = find_tag_from(g_buf, "Header");
        const char *p = find_tag_from(g_buf, PAYLOAD[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(h, PAYLOAD[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(p, PAYLOAD[i]);
        TEST_ASSERT_TRUE_MESSAGE(h < p, PAYLOAD[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_tag(g_buf, "Header"), PAYLOAD[i]);
    }
}

// MTConnectStreams_1.4.xsd HeaderType lines 710-718, MTConnectDevices_1.4.xsd 757-764,
// MTConnectAssets_1.4.xsd 680-686 and MTConnectError_1.4.xsd 680-685: every attribute below is
// use="required". A Header short of one of them is not a valid response, and an agent that answers
// with it is answering with a document its own schema rejects.
void test_every_header_carries_the_attributes_its_schema_marks_required(void)
{
    static const char *const STREAMS[] = {"version",       "creationTime", "nextSequence", "lastSequence",
                                          "firstSequence", "instanceId",   "sender",       "bufferSize"};
    static const char *const DEVICES[] = {"version",    "creationTime",    "instanceId", "sender",
                                          "bufferSize", "assetBufferSize", "assetCount"};
    static const char *const ASSETS[] = {"version", "creationTime",    "instanceId",
                                         "sender",  "assetBufferSize", "assetCount"};
    static const char *const ERRORS[] = {"version", "creationTime", "instanceId", "sender", "bufferSize"};

    missing_reset();

    TEST_ASSERT_TRUE(build_streams() > 0);
    require_attrs(g_buf, "Header", "streams", STREAMS, sizeof(STREAMS) / sizeof(STREAMS[0]));

    static protocore_mtc_sample_buffer b;
    protocore_mtc_sample_buffer_init(&b, 1000);
    protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:00Z", "1.0");
    TEST_ASSERT_TRUE(protocore_mtc_sample_query(&b, g_buf, sizeof(g_buf), 7, "VF2", 1000, 10) > 0);
    require_attrs(g_buf, "Header", "sample", STREAMS, sizeof(STREAMS) / sizeof(STREAMS[0]));

    TEST_ASSERT_TRUE(build_devices() > 0);
    require_attrs(g_buf, "Header", "devices", DEVICES, sizeof(DEVICES) / sizeof(DEVICES[0]));

    TEST_ASSERT_TRUE(build_assets() > 0);
    require_attrs(g_buf, "Header", "assets", ASSETS, sizeof(ASSETS) / sizeof(ASSETS[0]));

    TEST_ASSERT_TRUE(build_error() > 0);
    require_attrs(g_buf, "Header", "error", ERRORS, sizeof(ERRORS) / sizeof(ERRORS[0]));

    TEST_ASSERT_EQUAL_STRING("", g_missing);
}

// DeviceStreamType (Streams xsd lines 800-801) marks name and uuid required. The uuid is what a
// client keys a device by across restarts, and the name is only a label.
void test_the_device_stream_carries_name_and_uuid(void)
{
    static const char *const REQ[] = {"name", "uuid"};
    missing_reset();

    TEST_ASSERT_TRUE(build_streams() > 0);
    require_attrs(g_buf, "DeviceStream", "streams", REQ, 2);

    static protocore_mtc_sample_buffer b;
    protocore_mtc_sample_buffer_init(&b, 1000);
    protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:00Z", "1.0");
    TEST_ASSERT_TRUE(protocore_mtc_sample_query(&b, g_buf, sizeof(g_buf), 7, "VF2", 1000, 10) > 0);
    require_attrs(g_buf, "DeviceStream", "sample", REQ, 2);

    TEST_ASSERT_EQUAL_STRING("", g_missing);
}

// ComponentStreamType (Streams xsd lines 832-835) marks componentId and component required.
// componentId is the id from the probe response that ties the observation back to a component;
// without it a client cannot tell which of a device's components reported.
void test_the_component_stream_carries_component_and_component_id(void)
{
    static const char *const REQ[] = {"componentId", "component"};
    missing_reset();

    TEST_ASSERT_TRUE(build_streams() > 0);
    require_attrs(g_buf, "ComponentStream", "streams", REQ, 2);

    TEST_ASSERT_EQUAL_STRING("", g_missing);
}

// ComponentStreamType (Streams xsd lines 809-831) is an xs:sequence of Samples, Events and
// Condition, each with maxOccurs="1". So a component reports at most one of each container, and in
// that order, however many observations it carries.
void test_the_component_stream_groups_each_category_once_and_in_order(void)
{
    protocore_mtc_streams s;
    protocore_mtc_streams_begin(&s, g_buf, sizeof(g_buf), 7, 100, "VF2");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", 1, "2026-01-01T00:00:00Z", "1.5");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_EVENT, "Execution", "exec", 2, "2026-01-01T00:00:01Z", "ACTIVE");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", 3, "2026-01-01T00:00:02Z", "2.5");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_CONDITION, "SYSTEM", "sys", 4, "2026-01-01T00:00:03Z", "Fault");
    TEST_ASSERT_TRUE(protocore_mtc_streams_end(&s) > 0);
    TEST_ASSERT_TRUE(xml_wf(g_buf));

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_tag(g_buf, "Samples"), "maxOccurs=1 on Samples");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_tag(g_buf, "Events"), "maxOccurs=1 on Events");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_tag(g_buf, "Condition"), "maxOccurs=1 on Condition");

    const char *samples = find_tag_from(g_buf, "Samples");
    const char *events = find_tag_from(g_buf, "Events");
    const char *condition = find_tag_from(g_buf, "Condition");
    TEST_ASSERT_TRUE_MESSAGE(samples < events, "the sequence puts Samples before Events");
    TEST_ASSERT_TRUE_MESSAGE(events < condition, "the sequence puts Events before Condition");
}

// ResultType (Streams xsd lines 876-880) marks sequence, timestamp and dataItemId required on every
// Sample and Event, and the element itself is the DataItem type name the probe published
// (Position at line 1340, Execution at 3229). The value is the element's character data.
void test_a_sample_and_an_event_carry_the_required_result_attributes(void)
{
    static const char *const REQ[] = {"sequence", "timestamp", "dataItemId"};
    protocore_mtc_streams s;
    protocore_mtc_streams_begin(&s, g_buf, sizeof(g_buf), 7, 100, "VF2");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", 11, "2026-01-01T00:00:00Z", "1.5");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_EVENT, "Execution", "exec", 12, "2026-01-01T00:00:01Z", "ACTIVE");
    TEST_ASSERT_TRUE(protocore_mtc_streams_end(&s) > 0);

    missing_reset();
    require_attrs(g_buf, "Position", "Position", REQ, 3);
    require_attrs(g_buf, "Execution", "Execution", REQ, 3);
    TEST_ASSERT_EQUAL_STRING("", g_missing);

    char v[64];
    attr_value(find_tag_from(g_buf, "Position"), "sequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("11", v);
    attr_value(find_tag_from(g_buf, "Position"), "dataItemId", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("Xabs", v);
    text_of(g_buf, "Position", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1.5", v);
    text_of(g_buf, "Execution", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("ACTIVE", v);
}

// The Condition substitution group publishes exactly four elements: Unavailable (Streams xsd line
// 4318), Normal (4336), Warning (4355) and Fault (4376). ConditionType (4287-4292) marks sequence,
// timestamp, dataItemId and type required on each, and the DataItem type moves into the type
// attribute because the element name is the condition state instead.
void test_a_condition_is_one_of_the_four_published_states(void)
{
    static const char *const STATE[] = {"Unavailable", "Normal", "Warning", "Fault"};
    static const char *const REQ[] = {"sequence", "timestamp", "dataItemId", "type"};

    for (size_t i = 0; i < sizeof(STATE) / sizeof(STATE[0]); i++)
    {
        protocore_mtc_streams s;
        protocore_mtc_streams_begin(&s, g_buf, sizeof(g_buf), 7, 100, "VF2");
        protocore_mtc_streams_add(&s, PROTOCORE_MTC_CONDITION, "SYSTEM", "sys", 5, "2026-01-01T00:00:00Z", STATE[i]);
        TEST_ASSERT_TRUE(protocore_mtc_streams_end(&s) > 0);
        TEST_ASSERT_TRUE(xml_wf(g_buf));

        missing_reset();
        require_attrs(g_buf, STATE[i], STATE[i], REQ, 4);
        TEST_ASSERT_EQUAL_STRING("", g_missing);

        char v[64];
        attr_value(find_tag_from(g_buf, STATE[i]), "type", v, sizeof(v));
        TEST_ASSERT_EQUAL_STRING("SYSTEM", v);
    }

    // No state named: the element still has to be one of the four the schema publishes.
    protocore_mtc_streams s;
    protocore_mtc_streams_begin(&s, g_buf, sizeof(g_buf), 7, 100, "VF2");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_CONDITION, "SYSTEM", "sys", 5, "2026-01-01T00:00:00Z", NULL);
    TEST_ASSERT_TRUE(protocore_mtc_streams_end(&s) > 0);
    int found = 0;
    for (size_t i = 0; i < sizeof(STATE) / sizeof(STATE[0]); i++)
    {
        found += find_tag_from(g_buf, STATE[i]) ? 1 : 0;
    }
    TEST_ASSERT_EQUAL_INT(1, found);
}

// DataItemType (Devices xsd lines 1870-1877) marks id, type and category required and name / units
// optional, and CategoryType (1773-1775) publishes the only three category values. An optional
// attribute is left out rather than written empty: an empty NameType is not the absence of a name.
void test_a_data_item_carries_its_required_attributes_and_a_published_category(void)
{
    static const char *const REQ[] = {"id", "type", "category"};
    protocore_mtc_streams s;
    protocore_mtc_devices_begin(&s, g_buf, sizeof(g_buf), 7, "d1", "VF2", "uuid-1");
    protocore_mtc_devices_add_item(&s, PROTOCORE_MTC_SAMPLE, "Xabs", "POSITION", "Xpos", "MILLIMETER");
    protocore_mtc_devices_add_item(&s, PROTOCORE_MTC_EVENT, "exec", "EXECUTION", NULL, NULL);
    protocore_mtc_devices_add_item(&s, PROTOCORE_MTC_CONDITION, "sys", "SYSTEM", "", "");
    TEST_ASSERT_TRUE(protocore_mtc_devices_end(&s) > 0);
    TEST_ASSERT_TRUE(xml_wf(g_buf));
    TEST_ASSERT_EQUAL_INT(3, count_tag(g_buf, "DataItem"));

    missing_reset();
    const char *p = g_buf;
    static const char *const CATEGORY[3] = {"SAMPLE", "EVENT", "CONDITION"};
    for (int i = 0; i < 3; i++)
    {
        const char *tag = find_tag_from(p, "DataItem");
        TEST_ASSERT_NOT_NULL(tag);
        for (int a = 0; a < 3; a++)
        {
            if (!attr_present(tag, REQ[a]))
            {
                missing_add("DataItem", REQ[a]);
            }
        }
        char v[64];
        attr_value(tag, "category", v, sizeof(v));
        TEST_ASSERT_EQUAL_STRING(CATEGORY[i], v);
        p = tag + 1;
    }
    TEST_ASSERT_EQUAL_STRING("", g_missing);

    TEST_ASSERT_NULL(strstr(g_buf, "name=\"\""));
    TEST_ASSERT_NULL(strstr(g_buf, "units=\"\""));
}

// DeviceType (Devices xsd lines 1043-1044) marks uuid and name required and ComponentType (line
// 947), which it extends, marks id required.
void test_the_probed_device_carries_id_name_and_uuid(void)
{
    static const char *const REQ[] = {"id", "name", "uuid"};
    TEST_ASSERT_TRUE(build_devices() > 0);

    missing_reset();
    require_attrs(g_buf, "Device", "Device", REQ, 3);
    TEST_ASSERT_EQUAL_STRING("", g_missing);

    char v[64];
    const char *tag = find_tag_from(g_buf, "Device");
    attr_value(tag, "id", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("d1", v);
    attr_value(tag, "uuid", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("uuid-1", v);
}

// XML 1.0 sec 2.4 keeps & and < out of character data and production [10] keeps <, & and the
// delimiting quotation mark out of an attribute value. A machine name or an operator message is
// arbitrary text, so the check that matters is that it comes back out of the document unchanged.
void test_markup_characters_in_values_are_escaped(void)
{
    static const char *const NAME = "A&B<C>\"D\"";
    static const char *const VALUE = "a<b>c&d\"e";

    protocore_mtc_streams s;
    protocore_mtc_streams_begin(&s, g_buf, sizeof(g_buf), 1, 1, NAME);
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_EVENT, "Message", "msg", 1, "2026-01-01T00:00:00Z", VALUE);
    TEST_ASSERT_TRUE(protocore_mtc_streams_end(&s) > 0);
    TEST_ASSERT_TRUE_MESSAGE(xml_wf(g_buf), "escaping produced a document that does not parse");

    char v[128];
    attr_value(find_tag_from(g_buf, "DeviceStream"), "name", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING(NAME, v);
    text_of(g_buf, "Message", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING(VALUE, v);

    TEST_ASSERT_TRUE(build_error() > 0);
    TEST_ASSERT_TRUE(xml_wf(g_buf));
    text_of(g_buf, "Error", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("'from' must be <= 99", v);
}

// MTConnectAssets_1.4.xsd CuttingToolLifeCycleType (line 1237) puts CutterStatus at minOccurs='1',
// ahead of ToolLife in the same xs:sequence (line 1251). A life cycle without it is not an asset a
// client can accept, whatever the tool life says.
void test_the_cutting_tool_life_cycle_carries_a_cutter_status(void)
{
    TEST_ASSERT_TRUE(build_assets() > 0);
    TEST_ASSERT_TRUE(xml_wf(g_buf));

    const char *status = find_tag_from(g_buf, "CutterStatus");
    TEST_ASSERT_NOT_NULL_MESSAGE(status, "CuttingToolLifeCycleType requires CutterStatus");
    TEST_ASSERT_TRUE_MESSAGE(status < find_tag_from(g_buf, "ToolLife"), "CutterStatus precedes ToolLife");
}

// MTConnectAssets_1.4.xsd LifeType (lines 1750-1754) marks type, countDirection, limit and initial
// required. A count with no limit and no initial does not say how much life the tool started with
// or where it ends, which is the whole content of the element.
void test_tool_life_carries_the_attributes_the_schema_marks_required(void)
{
    static const char *const REQ[] = {"type", "countDirection", "limit", "initial"};
    TEST_ASSERT_TRUE(build_assets() > 0);
    TEST_ASSERT_EQUAL_INT(2, count_tag(g_buf, "ToolLife"));

    missing_reset();
    const char *p = g_buf;
    for (int i = 0; i < 2; i++)
    {
        const char *tag = find_tag_from(p, "ToolLife");
        TEST_ASSERT_NOT_NULL(tag);
        for (int a = 0; a < 4; a++)
        {
            if (!attr_present(tag, REQ[a]))
            {
                missing_add(i ? "ToolLife[1]" : "ToolLife[0]", REQ[a]);
            }
        }
        p = tag + 1;
    }
    TEST_ASSERT_EQUAL_STRING("", g_missing);
}

// The CuttingTool carries the caller's asset id and the ToolLife carries its value as character
// data, which is what a client reads the remaining life out of.
void test_the_cutting_tool_reports_its_asset_id_and_life_values(void)
{
    TEST_ASSERT_TRUE(build_assets() > 0);
    char v[64];

    const char *tool = find_tag_from(g_buf, "CuttingTool");
    TEST_ASSERT_NOT_NULL(tool);
    attr_value(tool, "assetId", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("T5", v);
    attr_value(tool, "timestamp", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("2026-01-01T00:00:00Z", v);

    const char *first = find_tag_from(g_buf, "ToolLife");
    text_of(first, "ToolLife", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("37", v);
    attr_value(first, "type", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("MINUTES", v);

    const char *second = find_tag_from(first + 1, "ToolLife");
    text_of(second, "ToolLife", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("12", v);
}

// mtconnect.h documents every closer as "@return length, or 0 on overflow" and ok as "cleared on any
// overflow". A truncated XML document is not a document, so a caller that only checks the length
// must not be handed one.
void test_overflow_reports_zero_length(void)
{
    char small[64];
    protocore_mtc_streams s;
    protocore_mtc_streams_begin(&s, small, sizeof(small), 1, 1, "VF2");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", 1, "2026-01-01T00:00:00Z", "1.5");
    TEST_ASSERT_FALSE(s.ok);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_mtc_streams_end(&s));

    protocore_mtc_streams_begin(&s, NULL, 1024, 1, 1, "VF2");
    TEST_ASSERT_FALSE(s.ok);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_mtc_streams_end(&s));

    TEST_ASSERT_EQUAL_size_t(0u, protocore_mtc_error(42, "OUT_OF_RANGE", "x", small, 8));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_mtc_error(42, "OUT_OF_RANGE", "x", NULL, sizeof(small)));

    TEST_ASSERT_TRUE(build_streams() > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(g_buf), build_streams());
}

// mtconnect.h line 181: "the first sequence number the agent will assign (0 is treated as 1)", and
// line 189: each add assigns the next one. Three adds from 1000 therefore assign 1000, 1001, 1002
// and leave next_seq one past the newest.
void test_sample_buffer_assigns_monotonic_sequences(void)
{
    static protocore_mtc_sample_buffer b;
    protocore_mtc_sample_buffer_init(&b, 1000);
    TEST_ASSERT_EQUAL_UINT64(1000u, b.next_seq);
    TEST_ASSERT_EQUAL_UINT64(1000u, b.first_seq);
    TEST_ASSERT_EQUAL_UINT32(0u, b.count);

    for (uint32_t i = 0; i < 3; i++)
    {
        const uint64_t seq = protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs",
                                                             "2026-01-01T00:00:00Z", "1.5");
        TEST_ASSERT_EQUAL_UINT64((uint64_t)(1000 + i), seq);
    }
    TEST_ASSERT_EQUAL_UINT32(3u, b.count);
    TEST_ASSERT_EQUAL_UINT64(1003u, b.next_seq);
    TEST_ASSERT_EQUAL_UINT64(1000u, b.first_seq);

    protocore_mtc_sample_buffer_init(&b, 0);
    TEST_ASSERT_EQUAL_UINT64(1u, b.next_seq);
    TEST_ASSERT_EQUAL_UINT64(1u, b.first_seq);
}

// mtconnect.h lines 164-167: the retained window is always [first_seq, next_seq), holding at most
// PROTOCORE_MTC_SAMPLE_BUFFER observations. One add past full evicts the oldest, so with the ring
// filled from 1000 the window is [1001, 1000 + BUFFER + 1) and its width is the count.
void test_sample_buffer_eviction_advances_the_window(void)
{
    static protocore_mtc_sample_buffer b;
    protocore_mtc_sample_buffer_init(&b, 1000);
    for (uint32_t i = 0; i < PROTOCORE_MTC_SAMPLE_BUFFER + 1; i++)
    {
        protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:00Z", "1.5");
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_MTC_SAMPLE_BUFFER, b.count);
    TEST_ASSERT_EQUAL_UINT64(1001u, b.first_seq);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(1000 + PROTOCORE_MTC_SAMPLE_BUFFER + 1), b.next_seq);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)b.count, b.next_seq - b.first_seq);
}

// mtconnect.h lines 193-199: the query emits up to count observations from `from` onward and reports
// a nextSequence one past the last one returned. Three observations from 1000 with count 10 return
// all three, so nextSequence = 1000 + 3 = 1003; asking from 1001 with count 1 returns one, so
// nextSequence = 1001 + 1 = 1002 and neither neighbour appears.
void test_sample_query_replays_the_requested_window(void)
{
    static protocore_mtc_sample_buffer b;
    char v[64];
    protocore_mtc_sample_buffer_init(&b, 1000);
    protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:00Z", "1.0");
    protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:01Z", "2.0");
    protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:02Z", "3.0");

    TEST_ASSERT_TRUE(protocore_mtc_sample_query(&b, g_buf, sizeof(g_buf), 7, "VF2", 1000, 10) > 0);
    const char *h = find_tag_from(g_buf, "Header");
    attr_value(h, "firstSequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1000", v);
    attr_value(h, "lastSequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1002", v);
    attr_value(h, "nextSequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1003", v);
    TEST_ASSERT_EQUAL_INT(3, count_tag(g_buf, "Position"));
    attr_value(find_tag_from(g_buf, "Position"), "sequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1000", v);
    text_of(g_buf, "Position", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1.0", v);

    TEST_ASSERT_TRUE(protocore_mtc_sample_query(&b, g_buf, sizeof(g_buf), 7, "VF2", 1001, 1) > 0);
    attr_value(find_tag_from(g_buf, "Header"), "nextSequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1002", v);
    TEST_ASSERT_EQUAL_INT(1, count_tag(g_buf, "Position"));
    attr_value(find_tag_from(g_buf, "Position"), "sequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1001", v);
    text_of(g_buf, "Position", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("2.0", v);
}

// mtconnect.h lines 195-197: "a from below the retained firstSequence is clamped up to it ... and
// the header firstSequence tells it data was dropped". After one eviction the oldest kept is 1001,
// so from 0 with count 2 replays 1001 and 1002 and resumes at 1001 + 2 = 1003.
void test_sample_query_clamps_a_stale_from(void)
{
    static protocore_mtc_sample_buffer b;
    char v[64];
    protocore_mtc_sample_buffer_init(&b, 1000);
    for (uint32_t i = 0; i < PROTOCORE_MTC_SAMPLE_BUFFER + 1; i++)
    {
        protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_EVENT, "Execution", "exec", "2026-01-01T00:00:00Z", "ACTIVE");
    }
    TEST_ASSERT_TRUE(protocore_mtc_sample_query(&b, g_buf, sizeof(g_buf), 7, "VF2", 0, 2) > 0);

    const char *h = find_tag_from(g_buf, "Header");
    attr_value(h, "firstSequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1001", v);
    attr_value(h, "nextSequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1003", v);
    TEST_ASSERT_EQUAL_INT(2, count_tag(g_buf, "Execution"));
    attr_value(find_tag_from(g_buf, "Execution"), "sequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1001", v);
}

// mtconnect.h lines 197-199: a from at or past the newest returns nothing and resumes at the
// buffer's own nextSequence. With one observation at 1000 the buffer's next is 1001, and an empty
// buffer started at 1000 reports lastSequence = next - 1 = 999, the width [1000, 1000) being zero.
void test_sample_query_past_the_newest_returns_no_observations(void)
{
    static protocore_mtc_sample_buffer b;
    char v[64];
    protocore_mtc_sample_buffer_init(&b, 1000);
    protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:00Z", "1.0");

    TEST_ASSERT_TRUE(protocore_mtc_sample_query(&b, g_buf, sizeof(g_buf), 7, "VF2", 1001, 10) > 0);
    attr_value(find_tag_from(g_buf, "Header"), "nextSequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1001", v);
    TEST_ASSERT_EQUAL_INT(0, count_tag(g_buf, "ComponentStream"));
    TEST_ASSERT_EQUAL_INT(1, count_tag(g_buf, "DeviceStream"));
    TEST_ASSERT_TRUE(xml_wf(g_buf));

    protocore_mtc_sample_buffer_init(&b, 1000);
    TEST_ASSERT_TRUE(protocore_mtc_sample_query(&b, g_buf, sizeof(g_buf), 7, "VF2", 1000, 10) > 0);
    const char *h = find_tag_from(g_buf, "Header");
    attr_value(h, "firstSequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1000", v);
    attr_value(h, "lastSequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("999", v);
    attr_value(h, "nextSequence", v, sizeof(v));
    TEST_ASSERT_EQUAL_STRING("1000", v);
}
