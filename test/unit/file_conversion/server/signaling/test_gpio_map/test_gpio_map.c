// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the GPIO pin-mapper core (server/signaling/gpio_map.h).
//
// The only document that governs the emitted report is RFC 8259. Sections 2 through 7 fix the
// grammar (structural characters, object, array, number, string) and sec 7 fixes what a string may
// carry unescaped. Nothing publishes this module's member names, member order, element order, or
// the four direction spellings, so no case here asserts them: the load-bearing case runs the
// document through a checker written from the sec 2-7 ABNF, and the content cases assert that the
// pins handed in come back out - one element per pin, in table order, each carrying its own number,
// its level and its label. A checker that accepted everything would prove nothing, so
// test_the_grammar_checker_rejects_malformed_texts drives it with texts the RFC's own rules reject.
//
// The control POST is a form data set, which RFC 1866 sec 8.2.1 item 2 does publish: "the name
// separated from the value by `=' and the pairs separated from each other by `&'". That copy is not
// in docs/learn/rfc/text; it was fetched from www.rfc-editor.org for this file.
//
// Two cases are expected to FAIL against the current source:
//   test_a_report_that_does_not_fit_is_reported_as_such - gpio_map.h line 102 documents
//     GpioMapNs::n as "bytes a report wrote, or < 0 when it did not fit"; gpio_map.c:99, :113 and
//     :116 report 0, which is the same value the caller sees for an empty table.
//   test_a_pin_the_field_cannot_hold_is_not_delivered_as_another_pin - gpio_map.c:139 accumulates
//     the decimal with no bound and gpio_map.c:166 narrows it to uint8_t, so "pin=258" is delivered
//     as pin 2, which TABLE declares an output, and a POST naming a pin that does not exist drives
//     one that does.
//
// The direction and level cases are properties, not spellings: four declared directions must be
// told apart from one another, and a level is a flag on both the parse and the write paths.

#include "server/signaling/gpio_map/gpio_map.h"
#include <string.h>

#include <unity.h>

static uint8_t gpio_map_work[16]; // the borrow an entry takes; GpioMap never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// ---------------------------------------------------------------------------
// RFC 8259 sec 2-7 grammar checker
// ---------------------------------------------------------------------------

static const char *jv_value(const char *p);

// sec 2: ws = *( %x20 / %x09 / %x0A / %x0D )
static const char *jv_ws(const char *p)
{
    while (*p == 0x20 || *p == 0x09 || *p == 0x0A || *p == 0x0D)
    {
        p++;
    }
    return p;
}

static int jv_hex(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F')
    {
        return 10 + (c - 'A');
    }
    return -1;
}

// sec 7: string = quotation-mark *char quotation-mark, where char is an unescaped octet, one of the
// eight two-character escapes, or \u followed by four hex digits. A quotation mark, a reverse
// solidus and every octet below 0x20 MUST be escaped, so meeting one raw ends the walk. The
// terminating NUL is one of those octets, so an unterminated string is refused too.
static const char *jv_string(const char *p)
{
    if (*p != '"')
    {
        return NULL;
    }
    p++;
    for (;;)
    {
        const unsigned char c = (unsigned char)*p;
        if (c == '"')
        {
            return p + 1;
        }
        if (c < 0x20u)
        {
            return NULL;
        }
        if (c == '\\')
        {
            p++;
            if (*p == 'u')
            {
                for (int i = 1; i <= 4; i++)
                {
                    if (jv_hex(p[i]) < 0)
                    {
                        return NULL;
                    }
                }
                p += 5;
                continue;
            }
            if (*p != '"' && *p != '\\' && *p != '/' && *p != 'b' && *p != 'f' && *p != 'n' && *p != 'r' && *p != 't')
            {
                return NULL;
            }
            p++;
            continue;
        }
        p++;
    }
}

// sec 6: number = [ minus ] int [ frac ] [ exp ], int = zero / ( digit1-9 *DIGIT ). A leading zero
// and a bare decimal point are outside the grammar.
static const char *jv_number(const char *p)
{
    if (*p == '-')
    {
        p++;
    }
    if (*p == '0')
    {
        p++;
    }
    else if (*p >= '1' && *p <= '9')
    {
        while (*p >= '0' && *p <= '9')
        {
            p++;
        }
    }
    else
    {
        return NULL;
    }
    if (*p == '.')
    {
        p++;
        if (*p < '0' || *p > '9')
        {
            return NULL;
        }
        while (*p >= '0' && *p <= '9')
        {
            p++;
        }
    }
    if (*p == 'e' || *p == 'E')
    {
        p++;
        if (*p == '+' || *p == '-')
        {
            p++;
        }
        if (*p < '0' || *p > '9')
        {
            return NULL;
        }
        while (*p >= '0' && *p <= '9')
        {
            p++;
        }
    }
    return p;
}

// sec 3: the three literal names, lowercase.
static const char *jv_lit(const char *p, const char *want)
{
    const size_t n = strlen(want);
    return strncmp(p, want, n) == 0 ? p + n : NULL;
}

// sec 5: array = begin-array [ value *( value-separator value ) ] end-array
static const char *jv_array(const char *p)
{
    p = jv_ws(p + 1);
    if (*p == ']')
    {
        return p + 1;
    }
    for (;;)
    {
        p = jv_value(p);
        if (!p)
        {
            return NULL;
        }
        p = jv_ws(p);
        if (*p == ',')
        {
            p++;
            continue;
        }
        if (*p == ']')
        {
            return p + 1;
        }
        return NULL;
    }
}

// sec 4: object = begin-object [ member *( value-separator member ) ] end-object,
//        member = string name-separator value
static const char *jv_object(const char *p)
{
    p = jv_ws(p + 1);
    if (*p == '}')
    {
        return p + 1;
    }
    for (;;)
    {
        p = jv_string(p);
        if (!p)
        {
            return NULL;
        }
        p = jv_ws(p);
        if (*p != ':')
        {
            return NULL;
        }
        p = jv_value(p + 1);
        if (!p)
        {
            return NULL;
        }
        p = jv_ws(p);
        if (*p == ',')
        {
            p = jv_ws(p + 1);
            continue;
        }
        if (*p == '}')
        {
            return p + 1;
        }
        return NULL;
    }
}

// sec 3: value = false / null / true / object / array / number / string
static const char *jv_value(const char *p)
{
    p = jv_ws(p);
    switch (*p)
    {
    case '{':
        return jv_object(p);
    case '[':
        return jv_array(p);
    case '"':
        return jv_string(p);
    case 't':
        return jv_lit(p, "true");
    case 'f':
        return jv_lit(p, "false");
    case 'n':
        return jv_lit(p, "null");
    default:
        return jv_number(p);
    }
}

// sec 2: JSON-text = ws value ws, and nothing after it.
static int jv_text(const char *s)
{
    const char *p = jv_value(s);
    if (!p)
    {
        return 0;
    }
    return *jv_ws(p) == '\0';
}

// The elements of the first array in @p doc, as [beg,fin) spans. Returns -1 when there is none.
static int jv_elements(const char *doc, const char **beg, const char **fin, int max)
{
    const char *p = doc;
    while (*p && *p != '[')
    {
        if (*p == '"')
        {
            p = jv_string(p);
            if (!p)
            {
                return -1;
            }
            continue;
        }
        p++;
    }
    if (*p != '[')
    {
        return -1;
    }
    p = jv_ws(p + 1);
    if (*p == ']')
    {
        return 0;
    }
    int n = 0;
    for (;;)
    {
        const char *start = jv_ws(p);
        const char *end = jv_value(p);
        if (!end)
        {
            return -1;
        }
        if (n < max)
        {
            beg[n] = start;
            fin[n] = end;
        }
        n++;
        p = jv_ws(end);
        if (*p == ',')
        {
            p = jv_ws(p + 1);
            continue;
        }
        if (*p == ']')
        {
            return n;
        }
        return -1;
    }
}

// Whether some number token in [beg,fin) has the value @p want.
static int jv_has_number(const char *beg, const char *fin, unsigned long want)
{
    const char *p = beg;
    while (p < fin)
    {
        if (*p == '"')
        {
            const char *e = jv_string(p);
            if (!e)
            {
                return 0;
            }
            p = e;
            continue;
        }
        if (*p >= '0' && *p <= '9')
        {
            const char *e = jv_number(p);
            if (!e)
            {
                return 0;
            }
            unsigned long v = 0;
            for (const char *q = p; q < e; q++)
            {
                v = v * 10u + (unsigned long)(*q - '0');
            }
            if (v == want)
            {
                return 1;
            }
            p = e;
            continue;
        }
        p++;
    }
    return 0;
}

// The octets a sec 7 string token stands for, with every escape form decoded.
static void jv_unescape(const char *p, const char *e, char *out, size_t cap)
{
    size_t n = 0;
    const char *q = p + 1;
    while (q + 1 < e && n + 1 < cap)
    {
        if (*q != '\\')
        {
            out[n++] = *q++;
            continue;
        }
        q++;
        switch (*q)
        {
        case 'b':
            out[n++] = '\b';
            q++;
            break;
        case 'f':
            out[n++] = '\f';
            q++;
            break;
        case 'n':
            out[n++] = '\n';
            q++;
            break;
        case 'r':
            out[n++] = '\r';
            q++;
            break;
        case 't':
            out[n++] = '\t';
            q++;
            break;
        case 'u': {
            unsigned v = 0;
            for (int i = 1; i <= 4; i++)
            {
                v = (v << 4) | (unsigned)jv_hex(q[i]);
            }
            q += 5;
            out[n++] = (char)(v & 0xFFu);
            break;
        }
        default:
            out[n++] = *q++;
            break;
        }
    }
    out[n] = '\0';
}

// Whether some string token in [beg,fin) stands for @p want.
static int jv_has_string(const char *beg, const char *fin, const char *want)
{
    const char *p = beg;
    while (p < fin)
    {
        if (*p == '"')
        {
            const char *e = jv_string(p);
            if (!e)
            {
                return 0;
            }
            char buf[64];
            jv_unescape(p, e, buf, sizeof(buf));
            if (strcmp(buf, want) == 0)
            {
                return 1;
            }
            p = e;
            continue;
        }
        p++;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// The module under test
// ---------------------------------------------------------------------------

static char g_out[512];

static const char *render(const protocore_gpio_pin *pins, uint8_t count, uint32_t cap)
{
    memset(g_out, '#', sizeof(g_out));
    GpioMap.args.pins = pins;
    GpioMap.args.count = count;
    GpioMap.out_args.out = g_out;
    GpioMap.out_args.cap = cap;
    GpioMap.json(gpio_map_work);
    return g_out;
}

static const char *dir_name(protocore_gpio_dir dir)
{
    GpioMap.args.dir = dir;
    GpioMap.dir_name(gpio_map_work);
    return GpioMap.text;
}

static proto_bool is_output(const protocore_gpio_pin *pins, uint8_t count, uint8_t pin)
{
    GpioMap.args.pins = pins;
    GpioMap.args.count = count;
    GpioMap.args.pin = pin;
    GpioMap.is_output(gpio_map_work);
    return GpioMap.ok;
}

static proto_bool parse_set(const char *body, size_t len, uint8_t *pin, uint8_t *level)
{
    GpioMap.parse_args.body = body;
    GpioMap.parse_args.len = len;
    GpioMap.parse_args.pin_out = pin;
    GpioMap.parse_args.level_out = level;
    GpioMap.parse_set(gpio_map_work);
    return GpioMap.ok;
}

static const protocore_gpio_pin TABLE[3] = {
    {2, "led", PROTOCORE_GPIO_DIR_OUT, 1},
    {4, "button", PROTOCORE_GPIO_DIR_IN_PULLUP, 0},
    {5, "sense", PROTOCORE_GPIO_DIR_IN, 1},
};

// A checker that accepts everything certifies nothing, so drive it with texts the sec 2-7 rules
// name: sec 4 requires a quoted name and a colon, sec 5 separates elements with a single comma,
// sec 6 forbids a leading zero and a bare point, sec 3 makes the literal names lowercase, sec 7
// forbids a raw control octet and an escape that is not one of the eight.
void test_the_grammar_checker_rejects_malformed_texts(void)
{
    static const char *const GOOD[] = {
        "{}",
        "[]",
        " \t\r\n{ \"a\" : [ 1, -2, 3.5e+2, true, false, null, \"\" ] } \n",
        "{\"a\":{\"b\":[{}]}}",
        "\"\\u0041\\\\\\\"\\/\\b\\f\\n\\r\\t\"",
        "0",
        "-0.5E10",
    };
    static const char *const BAD[] = {
        "{\"a\":1,}",  // sec 4: a comma must be followed by a member
        "[1,]",        // sec 5: a comma must be followed by a value
        "{a:1}",       // sec 4: a name is a string
        "{\"a\" 1}",   // sec 4: name-separator missing
        "{\"a\":}",    // sec 4: member value missing
        "01",          // sec 6: int = zero / digit1-9 *DIGIT
        "+1",          // sec 6: only minus may lead
        ".5",          // sec 6: frac needs an int
        "1.",          // sec 6: frac = decimal-point 1*DIGIT
        "1e",          // sec 6: exp = e [sign] 1*DIGIT
        "tRue",        // sec 3: the literal names MUST be lowercase
        "\"a",         // sec 7: unterminated
        "\"a\tb\"",    // sec 7: U+0009 MUST be escaped
        "\"a\\qb\"",   // sec 7: not one of the eight escapes
        "\"\\u00g0\"", // sec 7: four hex digits
        "{} {}",       // sec 2: JSON-text is one value
        "",            //
    };
    for (size_t i = 0; i < sizeof(GOOD) / sizeof(GOOD[0]); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(jv_text(GOOD[i]), GOOD[i]);
    }
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(jv_text(BAD[i]), BAD[i]);
    }
}

// The report a browser panel fetches has to parse. Every table size the serializer can be handed
// walks the sec 2-7 grammar, including the empty one.
void test_the_report_is_a_json_text(void)
{
    for (uint8_t n = 0; n <= 3u; n++)
    {
        TEST_ASSERT_TRUE_MESSAGE(jv_text(render(TABLE, n, (uint32_t)sizeof(g_out))), "not a JSON text");
    }
}

// One element per pin, in the order the table holds them, each carrying its own pin number, its
// level and its label. A dropped pin, a duplicated one or a reordered table fails here.
void test_every_pin_is_one_element_in_table_order(void)
{
    const char *s = render(TABLE, 3, (uint32_t)sizeof(g_out));
    const char *beg[8];
    const char *fin[8];
    TEST_ASSERT_EQUAL_INT(3, jv_elements(s, beg, fin, 8));
    for (int i = 0; i < 3; i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(jv_has_number(beg[i], fin[i], TABLE[i].pin), TABLE[i].label);
        TEST_ASSERT_TRUE_MESSAGE(jv_has_number(beg[i], fin[i], TABLE[i].level ? 1u : 0u), TABLE[i].label);
        TEST_ASSERT_TRUE_MESSAGE(jv_has_string(beg[i], fin[i], TABLE[i].label), TABLE[i].label);
    }
}

// No pins is an empty array, not a missing one: the panel gets a document either way.
void test_an_empty_table_is_an_empty_array(void)
{
    const char *s = render(TABLE, 0, (uint32_t)sizeof(g_out));
    const char *beg[1];
    const char *fin[1];
    TEST_ASSERT_TRUE(jv_text(s));
    TEST_ASSERT_EQUAL_INT(0, jv_elements(s, beg, fin, 1));
    TEST_ASSERT_EQUAL_INT((int32_t)strlen(s), GpioMap.n);
}

// A prefix of the table is the same document with fewer elements, so the separator is placed
// between elements and never before the first or after the last.
void test_a_shorter_table_is_the_same_document_with_fewer_elements(void)
{
    for (uint8_t n = 0; n <= 3u; n++)
    {
        const char *s = render(TABLE, n, (uint32_t)sizeof(g_out));
        const char *beg[8];
        const char *fin[8];
        TEST_ASSERT_TRUE_MESSAGE(jv_text(s), "not a JSON text");
        TEST_ASSERT_EQUAL_INT((int)n, jv_elements(s, beg, fin, 8));
    }
}

// RFC 8259 sec 7: a quotation mark, a reverse solidus and U+0000 through U+001F MUST be escaped.
// A label is caller-owned text, so a label that closed its own string would let whoever names the
// pins write the rest of the document. The identity check is what proves the escape is an escape
// and not a deletion.
void test_a_label_cannot_escape_its_string(void)
{
    static const char *const LABEL[] = {
        "a\"b\\c",
        "\x01\x1F",
        "line\nfeed\ttab",
        "}],\"pins\":[",
    };
    for (size_t i = 0; i < sizeof(LABEL) / sizeof(LABEL[0]); i++)
    {
        protocore_gpio_pin pins[1] = {{9, LABEL[i], PROTOCORE_GPIO_DIR_IN, 0}};
        const char *s = render(pins, 1, (uint32_t)sizeof(g_out));
        TEST_ASSERT_TRUE_MESSAGE(jv_text(s), "not a JSON text");

        const char *beg[2];
        const char *fin[2];
        TEST_ASSERT_EQUAL_INT(1, jv_elements(s, beg, fin, 2));
        TEST_ASSERT_TRUE_MESSAGE(jv_has_string(beg[0], fin[0], LABEL[i]), "label did not round-trip");

        for (const char *p = s; *p; p++)
        {
            TEST_ASSERT_TRUE_MESSAGE((unsigned char)*p >= 0x20u, "raw control octet in the document");
        }
    }
}

// gpio_map.h line 53 declares the level a 0/1 flag, so every truthy sample is the same document.
void test_the_reported_level_is_a_flag(void)
{
    protocore_gpio_pin pins[1] = {{7, "x", PROTOCORE_GPIO_DIR_OUT, 0}};
    const char *beg[2];
    const char *fin[2];

    const char *s = render(pins, 1, (uint32_t)sizeof(g_out));
    TEST_ASSERT_EQUAL_INT(1, jv_elements(s, beg, fin, 2));
    TEST_ASSERT_TRUE(jv_has_number(beg[0], fin[0], 0u));

    pins[0].level = 1;
    s = render(pins, 1, (uint32_t)sizeof(g_out));
    TEST_ASSERT_EQUAL_INT(1, jv_elements(s, beg, fin, 2));
    TEST_ASSERT_TRUE(jv_has_number(beg[0], fin[0], 1u));

    pins[0].level = 200;
    s = render(pins, 1, (uint32_t)sizeof(g_out));
    TEST_ASSERT_EQUAL_INT(1, jv_elements(s, beg, fin, 2));
    TEST_ASSERT_TRUE(jv_has_number(beg[0], fin[0], 1u));
    TEST_ASSERT_FALSE(jv_has_number(beg[0], fin[0], 200u));
}

// Four declared directions, four names a panel can tell apart: a pull-up input that renders as a
// plain input renders a pin the panel will not offer to drive as one it will. The spellings
// themselves are the module's own and are not asserted.
void test_the_four_directions_are_told_apart(void)
{
    static const protocore_gpio_dir DIR[4] = {PROTOCORE_GPIO_DIR_IN, PROTOCORE_GPIO_DIR_IN_PULLUP,
                                              PROTOCORE_GPIO_DIR_IN_PULLDOWN, PROTOCORE_GPIO_DIR_OUT};
    const char *name[4];
    for (int i = 0; i < 4; i++)
    {
        name[i] = dir_name(DIR[i]);
        TEST_ASSERT_NOT_NULL(name[i]);
        TEST_ASSERT_TRUE(name[i][0] != '\0');
    }
    for (int i = 0; i < 4; i++)
    {
        for (int j = i + 1; j < 4; j++)
        {
            TEST_ASSERT_TRUE_MESSAGE(strcmp(name[i], name[j]) != 0, "two directions share one name");
        }
    }
}

// A value outside the enumeration still names something, and it must not name the one direction
// the panel is allowed to drive.
void test_an_undeclared_direction_is_not_named_as_an_output(void)
{
    const char *out_name = dir_name(PROTOCORE_GPIO_DIR_OUT);
    const char *unknown = dir_name((protocore_gpio_dir)99);
    TEST_ASSERT_NOT_NULL(unknown);
    TEST_ASSERT_TRUE(unknown[0] != '\0');
    TEST_ASSERT_TRUE(strcmp(unknown, out_name) != 0);
}

// The document plus its terminator is what a caller must budget: cap = strlen + 1 is the smallest
// buffer that yields it, and n counts the octets without the terminator.
void test_the_smallest_buffer_that_holds_the_report(void)
{
    const int32_t need = (int32_t)strlen(render(TABLE, 3, (uint32_t)sizeof(g_out)));
    TEST_ASSERT_TRUE(need > 0);

    const char *s = render(TABLE, 3, (uint32_t)need + 1u);
    TEST_ASSERT_EQUAL_INT(need, GpioMap.n);
    TEST_ASSERT_EQUAL_INT(need, (int32_t)strlen(s));
    TEST_ASSERT_TRUE(jv_text(s));
}

// gpio_map.h line 102: n is "bytes a report wrote, or < 0 when it did not fit". Zero is what an
// empty report is worth, so a caller cannot tell a truncated document from a written one unless
// the short buffer reports negative.
void test_a_report_that_does_not_fit_is_reported_as_such(void)
{
    const int32_t need = (int32_t)strlen(render(TABLE, 3, (uint32_t)sizeof(g_out)));
    TEST_ASSERT_TRUE(need > 0);

    for (uint32_t cap = 1; cap <= (uint32_t)need; cap++)
    {
        (void)render(TABLE, 3, cap);
        TEST_ASSERT_TRUE_MESSAGE(GpioMap.n < 0, "a report that did not fit must report < 0");
    }
}

// No destination, no room and no table each produce no document rather than a partial one.
void test_the_serializer_refuses_missing_arguments(void)
{
    GpioMap.args.pins = TABLE;
    GpioMap.args.count = 3;
    GpioMap.out_args.out = NULL;
    GpioMap.out_args.cap = (uint32_t)sizeof(g_out);
    GpioMap.json(gpio_map_work);
    TEST_ASSERT_TRUE(GpioMap.n <= 0);

    g_out[0] = 'x';
    GpioMap.out_args.out = g_out;
    GpioMap.out_args.cap = 0;
    GpioMap.json(gpio_map_work);
    TEST_ASSERT_TRUE(GpioMap.n <= 0);
    TEST_ASSERT_EQUAL_CHAR('x', g_out[0]);

    GpioMap.args.pins = NULL;
    GpioMap.out_args.cap = (uint32_t)sizeof(g_out);
    GpioMap.json(gpio_map_work);
    TEST_ASSERT_TRUE(GpioMap.n <= 0);
    TEST_ASSERT_EQUAL_STRING("", g_out);
}

// The guard the control POST is checked against: only a pin the table declares an output may be
// driven, and a pin outside the walked prefix is not in the table at all.
void test_only_a_declared_output_may_be_driven(void)
{
    TEST_ASSERT_TRUE(is_output(TABLE, 3, 2));
    TEST_ASSERT_FALSE(is_output(TABLE, 3, 4));
    TEST_ASSERT_FALSE(is_output(TABLE, 3, 5));
    TEST_ASSERT_FALSE(is_output(TABLE, 3, 99));
    TEST_ASSERT_FALSE(is_output(TABLE, 3, 0));

    TEST_ASSERT_FALSE(is_output(TABLE, 0, 2));
    TEST_ASSERT_TRUE(is_output(TABLE, 1, 2));

    TEST_ASSERT_FALSE(is_output(NULL, 3, 2));
}

// RFC 1866 sec 8.2.1 item 2 gives the form encoding a browser POSTs: "the name separated from the
// value by `=' and the pairs separated from each other by `&'". A pair carries its meaning in its
// name, so the order they arrive in cannot change what is parsed, and a name the reader does not
// know is skipped.
void test_a_set_request_parses_both_fields(void)
{
    uint8_t pin = 0xFF;
    uint8_t level = 0xFF;

    TEST_ASSERT_TRUE(parse_set("pin=2&level=1", 13, &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(2u, pin);
    TEST_ASSERT_EQUAL_UINT8(1u, level);

    TEST_ASSERT_TRUE(parse_set("level=0&pin=13", 14, &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(13u, pin);
    TEST_ASSERT_EQUAL_UINT8(0u, level);

    TEST_ASSERT_TRUE(parse_set("x=9&pin=7&y=1&level=1", 21, &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(7u, pin);
    TEST_ASSERT_EQUAL_UINT8(1u, level);
}

// RFC 1866 sec 8.2.1 item 2 separates the pairs with `&', so a name runs from the start of the body
// or from a `&'. A name that merely ends with "pin" is a different field, and a reader that matches
// inside one takes another field's value.
void test_a_field_name_must_start_at_a_pair_boundary(void)
{
    uint8_t pin = 0xAA;
    uint8_t level = 0xAA;

    TEST_ASSERT_FALSE(parse_set("spin=2&level=1", 14, &pin, &level));
    TEST_ASSERT_FALSE(parse_set("mypin=2&level=1", 15, &pin, &level));
    TEST_ASSERT_FALSE(parse_set("pin=2&mylevel=1", 15, &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(0xAAu, pin);
    TEST_ASSERT_EQUAL_UINT8(0xAAu, level);
}

// gpio_map.h line 66 holds the pin in a uint8_t and gpio_map.c drives whatever lands there. 258,
// 514 and 2^32+2 all reduce to 2 when the field is narrowed, and TABLE declares pin 2 an output,
// so a body naming a pin that cannot exist must not arrive as one that can.
void test_a_pin_the_field_cannot_hold_is_not_delivered_as_another_pin(void)
{
    static const char *const BODY[] = {
        "pin=258&level=1",
        "pin=514&level=1",
        "pin=4294967298&level=1",
    };
    for (size_t i = 0; i < sizeof(BODY) / sizeof(BODY[0]); i++)
    {
        uint8_t pin = 0xAA;
        uint8_t level = 0xAA;
        const proto_bool ok = parse_set(BODY[i], strlen(BODY[i]), &pin, &level);
        TEST_ASSERT_TRUE_MESSAGE(!ok || pin != 2u, BODY[i]);
    }
}

// gpio_map.h line 67 calls level "the level a write drives" and gpio_map.c:248 indexes a two-entry
// table with it, so every truthy spelling has to arrive as the same value.
void test_the_parsed_level_is_a_flag(void)
{
    uint8_t pin = 0;
    uint8_t level = 0xFF;

    TEST_ASSERT_TRUE(parse_set("pin=1&level=1", 13, &pin, &level));
    const uint8_t on = level;

    TEST_ASSERT_TRUE(parse_set("pin=1&level=200", 15, &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(on, level);

    TEST_ASSERT_TRUE(parse_set("pin=1&level=0", 13, &pin, &level));
    TEST_ASSERT_TRUE(level != on);
    TEST_ASSERT_EQUAL_UINT8(0u, level);
}

// Half a request is not a request: a missing field, an empty value and a non-numeric value each
// leave the caller's pin and level untouched, so a refused POST cannot drive anything.
void test_an_incomplete_set_request_is_refused(void)
{
    uint8_t pin = 0xAA;
    uint8_t level = 0xAA;

    static const char *const BAD[] = {
        "", "pin=2", "level=1", "pin=&level=1", "pin=2&level=", "pin=x&level=1", "pin=2&level=x", "pin2&level=1",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(parse_set(BAD[i], strlen(BAD[i]), &pin, &level), BAD[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(0xAAu, pin);
    TEST_ASSERT_EQUAL_UINT8(0xAAu, level);

    TEST_ASSERT_FALSE(parse_set(NULL, 13, &pin, &level));
    TEST_ASSERT_FALSE(parse_set("pin=2&level=1", 13, NULL, &level));
    TEST_ASSERT_FALSE(parse_set("pin=2&level=1", 13, &pin, NULL));
}

// The body is a length and a pointer, not a C string: the parse stops where the caller says the
// body stops, whatever the octets after it happen to be.
void test_the_body_length_bounds_the_parse(void)
{
    uint8_t pin = 0;
    uint8_t level = 0;
    TEST_ASSERT_FALSE(parse_set("pin=2&level=1", 5, &pin, &level));
    TEST_ASSERT_FALSE(parse_set("pin=2&level=1", 12, &pin, &level));
    TEST_ASSERT_TRUE(parse_set("pin=2&level=1", 13, &pin, &level));
}

// Arming the table drives each pin to the mode its own entry declares. The enum and the board
// profile's pin-mode numbers are different encodings (gpio_map.h lines 31-37), so this reads the
// seam the profile defines rather than the enum the table holds.
void test_each_pin_is_armed_in_its_declared_direction(void)
{
    static const protocore_gpio_pin PINS[4] = {
        {20, "o", PROTOCORE_GPIO_DIR_OUT, 0},
        {21, "u", PROTOCORE_GPIO_DIR_IN_PULLUP, 0},
        {22, "d", PROTOCORE_GPIO_DIR_IN_PULLDOWN, 0},
        {23, "i", PROTOCORE_GPIO_DIR_IN, 0},
    };
    GpioMap.args.pins = PINS;
    GpioMap.args.count = 4;
    GpioMap.begin_pins(gpio_map_work);

    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_GPIO_OUT, protocore_gpio_host_mode(20));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_GPIO_IN_PULLUP, protocore_gpio_host_mode(21));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_GPIO_IN_PULLDOWN, protocore_gpio_host_mode(22));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_GPIO_IN, protocore_gpio_host_mode(23));
}

// A sample reports the pin, not the table: whatever the seam holds is what lands in the entry, and
// a write puts it there. The level a write drives is a flag, the same as the parsed one.
void test_a_sample_reads_the_seam_back_into_the_table(void)
{
    protocore_gpio_pin pins[2] = {
        {24, "o", PROTOCORE_GPIO_DIR_OUT, 0},
        {25, "i", PROTOCORE_GPIO_DIR_IN, 0},
    };
    GpioMap.args.pins = pins;
    GpioMap.args.count = 2;
    GpioMap.begin_pins(gpio_map_work);

    GpioMap.args.pin = 24;
    GpioMap.args.level = 1;
    GpioMap.write(gpio_map_work);
    protocore_gpio_host_set(25, 1);
    GpioMap.args.pins_rw = pins;
    GpioMap.args.count = 2;
    GpioMap.sample(gpio_map_work);
    TEST_ASSERT_EQUAL_UINT8(1u, pins[0].level);
    TEST_ASSERT_EQUAL_UINT8(1u, pins[1].level);

    GpioMap.args.pin = 24;
    GpioMap.args.level = 0;
    GpioMap.write(gpio_map_work);
    protocore_gpio_host_set(25, 0);
    GpioMap.args.pins_rw = pins;
    GpioMap.sample(gpio_map_work);
    TEST_ASSERT_EQUAL_UINT8(0u, pins[0].level);
    TEST_ASSERT_EQUAL_UINT8(0u, pins[1].level);

    GpioMap.args.pin = 24;
    GpioMap.args.level = 200;
    GpioMap.write(gpio_map_work);
    TEST_ASSERT_EQUAL_UINT8(1u, protocore_gpio_host_level(24));
}

// A table that is not there is walked zero times rather than dereferenced.
void test_a_missing_table_is_walked_zero_times(void)
{
    GpioMap.args.pins = NULL;
    GpioMap.args.count = 3;
    GpioMap.begin_pins(gpio_map_work);

    GpioMap.args.pins_rw = NULL;
    GpioMap.args.count = 3;
    GpioMap.sample(gpio_map_work);
}
