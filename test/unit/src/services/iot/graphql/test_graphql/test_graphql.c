// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the GraphQL executor (services/iot/graphql/graphql.h).
//
// The governing standard is the GraphQL specification, October 2021 release (spec.graphql.org), not
// an IETF RFC. Every query text below is copied from a numbered Example in that release, and the
// expected response is the response map sec 7.1 defines around the JSON the same Example publishes.
//
// test_spec_example_3_produces_example_4 is load-bearing: sec 1 Example No. 3 is the query
// `{ user(id: 4) { name } }` and Example No. 4 is its result, `{"user": {"name": "Mark Zuckerberg"}}`.
// Reproducing that pair, wrapped in the sec 7.1.1 `data` entry, is what makes the parser, the
// argument scope, the resolver path and the sec 7.2.1 serializer trustworthy together.

#include "services/iot/graphql/graphql.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_out[512];

// The dotted path the resolver was last called with, so the sec 6.4.2 path can be asserted.
static char g_last_path[128];

// Read the named Int argument out of the values in scope (spec sec 6.4.1).
static proto_bool arg_int(const struct protocore_gql_args *args, const char *name, long long *out)
{
    GraphQL.argument.values = args;
    GraphQL.argument.name = name;
    GraphQL.arg_int(GraphQL.internal);
    *out = GraphQL.i64;
    return GraphQL.ok;
}

// ResolveFieldValue (spec sec 6.4.2) for the Examples this suite executes.
static proto_bool resolver(const char *path, const struct protocore_gql_args *args, protocore_gql_value *out)
{
    size_t n = strlen(path);
    if (n < sizeof(g_last_path))
    {
        memcpy(g_last_path, path, n + 1);
    }
    if (strcmp(path, "user.name") == 0)
    {
        long long id = 0;
        if (!arg_int(args, "id", &id) || id != 4)
        {
            return PROTO_FALSE;
        }
        out->type = PROTOCORE_GQL_STR;
        out->s = "Mark Zuckerberg";
        return PROTO_TRUE;
    }
    if (strcmp(path, "user.id") == 0)
    {
        long long id = 0;
        (void)arg_int(args, "id", &id);
        out->type = PROTOCORE_GQL_INT;
        out->i = id;
        return PROTO_TRUE;
    }
    // Example No. 10: `profilePic(size: 100)` under `user(id: 4)`, so both are in scope here.
    if (strcmp(path, "user.profilePic") == 0)
    {
        long long size = 0, id = 0;
        (void)arg_int(args, "id", &id);
        if (!arg_int(args, "size", &size))
        {
            return PROTO_FALSE;
        }
        out->type = PROTOCORE_GQL_INT;
        out->i = id * 1000 + size;
        return PROTO_TRUE;
    }
    // Example No. 12 and No. 13: the same two arguments in either syntactic order.
    if (strcmp(path, "picture") == 0)
    {
        long long w = 0, h = 0;
        if (!arg_int(args, "width", &w) || !arg_int(args, "height", &h))
        {
            return PROTO_FALSE;
        }
        out->type = PROTOCORE_GQL_INT;
        out->i = w * 10000 + h;
        return PROTO_TRUE;
    }
    // Example No. 7: a flat selection set of scalar leaves.
    if (strcmp(path, "id") == 0)
    {
        out->type = PROTOCORE_GQL_INT;
        out->i = 4;
        return PROTO_TRUE;
    }
    if (strcmp(path, "firstName") == 0)
    {
        out->type = PROTOCORE_GQL_STR;
        out->s = "Mark";
        return PROTO_TRUE;
    }
    if (strcmp(path, "lastName") == 0)
    {
        out->type = PROTOCORE_GQL_STR;
        out->s = "Zuckerberg";
        return PROTO_TRUE;
    }
    // Example No. 8: nesting down to scalars.
    if (strcmp(path, "me.birthday.month") == 0)
    {
        out->type = PROTOCORE_GQL_INT;
        out->i = 5;
        return PROTO_TRUE;
    }
    if (strcmp(path, "me.birthday.day") == 0)
    {
        out->type = PROTOCORE_GQL_INT;
        out->i = 14;
        return PROTO_TRUE;
    }
    if (strcmp(path, "me.name") == 0)
    {
        out->type = PROTOCORE_GQL_STR;
        out->s = "Mark";
        return PROTO_TRUE;
    }
    // The sec 3.5 scalar kinds, one leaf each.
    if (strcmp(path, "anInt") == 0)
    {
        out->type = PROTOCORE_GQL_INT;
        out->i = -9007199254740993LL;
        return PROTO_TRUE;
    }
    if (strcmp(path, "aFloat") == 0)
    {
        out->type = PROTOCORE_GQL_FLOAT;
        out->f = 1.5;
        return PROTO_TRUE;
    }
    if (strcmp(path, "aTrue") == 0)
    {
        out->type = PROTOCORE_GQL_BOOL;
        out->b = PROTO_TRUE;
        return PROTO_TRUE;
    }
    if (strcmp(path, "aFalse") == 0)
    {
        out->type = PROTOCORE_GQL_BOOL;
        out->b = PROTO_FALSE;
        return PROTO_TRUE;
    }
    if (strcmp(path, "aString") == 0)
    {
        out->type = PROTOCORE_GQL_STR;
        out->s = "he said \"hi\"\tand\\left\n";
        return PROTO_TRUE;
    }
    if (strcmp(path, "aNull") == 0)
    {
        out->type = PROTOCORE_GQL_NULL;
        return PROTO_TRUE;
    }
    // Every other path fails to resolve, so the field completes as null.
    return PROTO_FALSE;
}

// Execute @p doc and return the serialized response map.
static const char *run(const char *doc)
{
    g_last_path[0] = '\0';
    g_out[0] = '\0';
    GraphQL.request.document = doc;
    GraphQL.request.len = strlen(doc);
    GraphQL.request.resolver = resolver;
    GraphQL.response.out = g_out;
    GraphQL.response.cap = sizeof(g_out);
    GraphQL.execute(GraphQL.internal);
    return g_out;
}

// GraphQL spec sec 1, Example No. 3 and Example No. 4. Example No. 4 is the "resulting data (in
// JSON)"; sec 7.1.1 says the response map's `data` entry is that result, and sec 7.2.2 keeps map
// entries in the order the selection set listed them.
void test_spec_example_3_produces_example_4(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"user\":{\"name\":\"Mark Zuckerberg\"}}}", run("{\n"
                                                                                        "  user(id: 4) {\n"
                                                                                        "    name\n"
                                                                                        "  }\n"
                                                                                        "}\n"));
    TEST_ASSERT_TRUE(GraphQL.ok);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_GQL_OK, GraphQL.result);
    TEST_ASSERT_EQUAL_UINT(strlen(g_out), GraphQL.n);
    // sec 6.4.2 hands the resolver the field, reached here as the dotted path from the root.
    TEST_ASSERT_EQUAL_STRING("user.name", g_last_path);
}

// sec 2.3 "Query shorthand": a document with one query operation defining no variables and no
// directives may omit the `query` keyword and the operation name. All three spellings are the same
// operation, so all three must produce the same response map.
void test_query_shorthand_matches_the_long_form(void)
{
    static const char *const SPELLING[] = {
        "{ user(id: 4) { name } }",
        "query { user(id: 4) { name } }",
        "query GetUser { user(id: 4) { name } }",
    };
    for (size_t i = 0; i < sizeof(SPELLING) / sizeof(SPELLING[0]); i++)
    {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("{\"data\":{\"user\":{\"name\":\"Mark Zuckerberg\"}}}", run(SPELLING[i]),
                                         SPELLING[i]);
    }
}

// sec 2.4 Example No. 7: `{ id firstName lastName }` is one selection set of three fields, and
// sec 7.2.2 requires the response map to list them in that order.
void test_selection_set_keeps_document_order(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"id\":4,\"firstName\":\"Mark\",\"lastName\":\"Zuckerberg\"}}",
                             run("{ id firstName lastName }"));
    // The same three fields written in another order come back in that other order.
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"lastName\":\"Zuckerberg\",\"id\":4,\"firstName\":\"Mark\"}}",
                             run("{ lastName id firstName }"));
}

// sec 2.5 Example No. 8: a field carrying a SelectionSet is completed by executing that set, so the
// response is shaped like the request all the way down to the scalar leaves.
void test_nested_selection_sets_shape_the_response(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"me\":{\"name\":\"Mark\",\"birthday\":{\"month\":5,\"day\":14}}}}",
                             run("{ me { name birthday { month day } } }"));
    TEST_ASSERT_EQUAL_STRING("me.birthday.day", g_last_path);
}

// sec 2.6 Example No. 10: `user(id: 4) { profilePic(size: 100) }`. sec 6.4.1 coerces a field's
// arguments before it is resolved, and an argument met along the path stays in scope beneath it.
void test_arguments_reach_the_leaf_that_needs_them(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"user\":{\"id\":4,\"name\":\"Mark Zuckerberg\",\"profilePic\":4100}}}",
                             run("{ user(id: 4) { id name profilePic(size: 100) } }"));
}

// sec 2.6 "Arguments are unordered": Example No. 12 and Example No. 13 are semantically identical.
void test_arguments_are_unordered(void)
{
    const char *a = "{ picture(width: 200, height: 100) }";
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"picture\":2000100}}", run(a));
    const char *b = "{ picture(height: 100, width: 200) }";
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"picture\":2000100}}", run(b));
}

// What the probing resolver read out of the argument scope.
static struct
{
    proto_bool int_ok;
    long long i;
    proto_bool str_ok;
    const char *s;
    proto_bool bool_ok;
    proto_bool b;
    proto_bool wrong_case_ok;
    proto_bool wrong_type_ok;
    proto_bool missing_ok;
} g_probe;

static proto_bool probe_resolver(const char *path, const struct protocore_gql_args *args, protocore_gql_value *out)
{
    (void)path;
    GraphQL.argument.values = args;

    GraphQL.argument.name = "count";
    GraphQL.arg_int(GraphQL.internal);
    g_probe.int_ok = GraphQL.ok;
    g_probe.i = GraphQL.i64;

    GraphQL.argument.name = "label";
    GraphQL.arg_str(GraphQL.internal);
    g_probe.str_ok = GraphQL.ok;
    g_probe.s = GraphQL.text;

    GraphQL.argument.name = "flag";
    GraphQL.arg_bool(GraphQL.internal);
    g_probe.bool_ok = GraphQL.ok;
    g_probe.b = GraphQL.b;

    // sec 2.1.9: Names are case-sensitive, so "Count" is not "count".
    GraphQL.argument.name = "Count";
    GraphQL.arg_int(GraphQL.internal);
    g_probe.wrong_case_ok = GraphQL.ok;

    // "label" is a String, so reading it as an Int reports absence.
    GraphQL.argument.name = "label";
    GraphQL.arg_int(GraphQL.internal);
    g_probe.wrong_type_ok = GraphQL.ok;

    GraphQL.argument.name = "absent";
    GraphQL.arg_str(GraphQL.internal);
    g_probe.missing_ok = GraphQL.ok;

    out->type = PROTOCORE_GQL_NULL;
    return PROTO_TRUE;
}

// sec 2.6 Arguments reach the resolver through the sec 6.4.1 coerced values, read by name and by the
// sec 3.5 scalar kind the accessor asks for.
void test_argument_accessors_are_named_and_typed(void)
{
    memset(&g_probe, 0, sizeof(g_probe));
    GraphQL.request.document = "{ probe(count: 7, label: \"kw\", flag: true) }";
    GraphQL.request.len = strlen(GraphQL.request.document);
    GraphQL.request.resolver = probe_resolver;
    GraphQL.response.out = g_out;
    GraphQL.response.cap = sizeof(g_out);
    GraphQL.execute(GraphQL.internal);
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"probe\":null}}", g_out);

    TEST_ASSERT_TRUE(g_probe.int_ok);
    TEST_ASSERT_EQUAL_INT64(7, g_probe.i);
    TEST_ASSERT_TRUE(g_probe.str_ok);
    TEST_ASSERT_EQUAL_STRING("kw", g_probe.s);
    TEST_ASSERT_TRUE(g_probe.bool_ok);
    TEST_ASSERT_TRUE(g_probe.b);
    TEST_ASSERT_FALSE(g_probe.wrong_case_ok);
    TEST_ASSERT_FALSE(g_probe.wrong_type_ok);
    TEST_ASSERT_FALSE(g_probe.missing_ok);
}

// With no resolver every leaf completes as null (sec 6.4.3 on a failed value resolution).
void test_no_resolver_completes_every_leaf_as_null(void)
{
    GraphQL.request.document = "{ user(id: 4) { name } id }";
    GraphQL.request.len = strlen(GraphQL.request.document);
    GraphQL.request.resolver = NULL;
    GraphQL.response.out = g_out;
    GraphQL.response.cap = sizeof(g_out);
    GraphQL.execute(GraphQL.internal);
    TEST_ASSERT_TRUE(GraphQL.ok);
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"user\":{\"name\":null},\"id\":null}}", g_out);
}

// sec 7.2.1 JSON Serialization of the sec 3.5 scalars a leaf may carry.
void test_scalar_serialization_forms(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"aTrue\":true,\"aFalse\":false}}", run("{ aTrue aFalse }"));
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"aNull\":null}}", run("{ aNull }"));
    // -9007199254740993 is 2^53 + 1 negated: it needs the full 64-bit Int this module carries, and
    // a JSON number written through a double would come back as -9007199254740992.
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"anInt\":-9007199254740993}}", run("{ anInt }"));
    // 1.5 is exact in binary64, so its shortest decimal form is "1.5" whatever the formatter.
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"aFloat\":1.5}}", run("{ aFloat }"));
}

// sec 7.2.1 serializes a String as a JSON string, so the quotation mark, the reverse solidus and the
// control characters must leave escaped or the response is not parseable JSON.
void test_string_values_are_json_escaped(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"aString\":\"he said \\\"hi\\\"\\tand\\\\left\\n\"}}", run("{ aString }"));
}

// sec 7.1.2 "Request errors": raised before execution begins, so the `data` entry must not be
// present and the `errors` entry must hold at least one error map carrying a `message`.
void test_request_error_carries_errors_and_no_data(void)
{
    static const char *const BAD[] = {
        "",                      // no operation at all
        "{",                     // an unterminated selection set
        "{ user(id: 4 ) { name", // an unterminated inner set
        "{ user(id) { name } }", // an Argument with no `: Value`
        "{ a } { b }",           // a second definition
        "{ a } trailing",        // trailing junk past the operation
        "notquery { a }",        // sec 2.3: only the query OperationType is executed
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        const char *got = run(BAD[i]);
        TEST_ASSERT_FALSE_MESSAGE(GraphQL.ok, BAD[i]);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(PROTOCORE_GQL_OK, GraphQL.result, BAD[i]);
        TEST_ASSERT_NULL_MESSAGE(strstr(got, "\"data\""), BAD[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(got, "\"errors\""), BAD[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(got, "\"message\""), BAD[i]);
    }
}

// The grammar this module declares out of scope: each construct is legal GraphQL but parses as a
// sec 7.1.2 request error rather than being silently ignored.
void test_out_of_scope_grammar_is_a_request_error(void)
{
    static const char *const OUT[] = {
        "mutation { likeStory(storyID: 12345) { story { likeCount } } }", // sec 2.3, Example No. 5
        "subscription { newMessage { body } }",                           // sec 2.3
        "{ user(id: 4) { ...userFields } }",                              // sec 2.8 fragments
        "query Q($id: Int) { user(id: $id) { name } }",                   // sec 2.10 variables
        "{ user(id: 4) @skip(if: true) { name } }",                       // sec 2.12 directives
        "{ zuck: user(id: 4) { name } }",                                 // sec 2.7 aliases
        "{ f(list: [1, 2]) }",                                            // sec 2.9.7 list values
        "{ f(obj: {a: 1}) }",                                             // sec 2.9.8 input objects
    };
    for (size_t i = 0; i < sizeof(OUT) / sizeof(OUT[0]); i++)
    {
        const char *got = run(OUT[i]);
        TEST_ASSERT_FALSE_MESSAGE(GraphQL.ok, OUT[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(PROTOCORE_GQL_ERR_PARSE, GraphQL.result, OUT[i]);
        TEST_ASSERT_NULL_MESSAGE(strstr(got, "\"data\""), OUT[i]);
    }
}

// sec 2.1.4 comments run to the line terminator and sec 2.1.5 commas are insignificant, so both are
// Ignored tokens (sec 2.1.7) and neither changes the operation. Example No. 9 carries a comment.
void test_comments_and_commas_are_ignored(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"id\":4,\"firstName\":\"Mark\",\"lastName\":\"Zuckerberg\"}}",
                             run("# `me` could represent the currently logged in viewer.\n"
                                 "{ id, firstName, lastName }\n"));
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"picture\":2000100}}", run("{ picture(width: 200 height: 100) } # tail"));
}

// A leaf whose resolver declines completes as null (sec 6.4.3 with a failed value resolution), so an
// unknown field never breaks the shape of the response.
void test_unresolved_leaf_completes_as_null(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"id\":4,\"unknownField\":null}}", run("{ id unknownField }"));
    TEST_ASSERT_TRUE(GraphQL.ok);
}

// The compile-time bounds are request errors (sec 7.1.2), not truncated results.
void test_bounds_are_request_errors(void)
{
    // PROTOCORE_GQL_MAX_DEPTH nesting levels parse; one more does not. The root set is level 1.
    char deep[256];
    size_t n = 0;
    for (int d = 0; d < PROTOCORE_GQL_MAX_DEPTH; d++)
    {
        deep[n++] = '{';
        deep[n++] = 'a';
    }
    for (int d = 0; d < PROTOCORE_GQL_MAX_DEPTH; d++)
    {
        deep[n++] = '}';
    }
    deep[n] = '\0';
    (void)run(deep);
    TEST_ASSERT_TRUE_MESSAGE(GraphQL.ok, deep);

    n = 0;
    for (int d = 0; d <= PROTOCORE_GQL_MAX_DEPTH; d++)
    {
        deep[n++] = '{';
        deep[n++] = 'a';
    }
    for (int d = 0; d <= PROTOCORE_GQL_MAX_DEPTH; d++)
    {
        deep[n++] = '}';
    }
    deep[n] = '\0';
    (void)run(deep);
    TEST_ASSERT_FALSE_MESSAGE(GraphQL.ok, deep);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_GQL_ERR_LIMIT, GraphQL.result);

    // More fields than the node pool holds.
    char wide[4 * PROTOCORE_GQL_MAX_NODES + 8];
    n = 0;
    wide[n++] = '{';
    for (int f = 0; f <= PROTOCORE_GQL_MAX_NODES; f++)
    {
        wide[n++] = ' ';
        wide[n++] = 'a';
        wide[n++] = (char)('0' + (f % 10));
        wide[n++] = (char)('a' + (f / 10));
    }
    wide[n++] = '}';
    wide[n] = '\0';
    (void)run(wide);
    TEST_ASSERT_FALSE(GraphQL.ok);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_GQL_ERR_LIMIT, GraphQL.result);
}

// A response that does not fit reports the overflow instead of emitting a truncated map.
void test_short_buffer_reports_overflow(void)
{
    char small[8];
    GraphQL.request.document = "{ user(id: 4) { name } }";
    GraphQL.request.len = strlen(GraphQL.request.document);
    GraphQL.request.resolver = resolver;
    GraphQL.response.out = small;
    GraphQL.response.cap = sizeof(small);
    GraphQL.execute(GraphQL.internal);
    TEST_ASSERT_FALSE(GraphQL.ok);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_GQL_ERR_OVERFLOW, GraphQL.result);
    TEST_ASSERT_EQUAL_UINT(0u, GraphQL.n);
}

// A missing document or a missing response buffer is refused rather than written through.
void test_null_inputs_are_refused(void)
{
    GraphQL.request.document = NULL;
    GraphQL.request.len = 0;
    GraphQL.request.resolver = resolver;
    GraphQL.response.out = g_out;
    GraphQL.response.cap = sizeof(g_out);
    GraphQL.execute(GraphQL.internal);
    TEST_ASSERT_FALSE(GraphQL.ok);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_GQL_ERR_PARSE, GraphQL.result);

    GraphQL.request.document = "{ id }";
    GraphQL.request.len = 6;
    GraphQL.response.out = NULL;
    GraphQL.response.cap = sizeof(g_out);
    GraphQL.execute(GraphQL.internal);
    TEST_ASSERT_FALSE(GraphQL.ok);

    GraphQL.response.out = g_out;
    GraphQL.response.cap = 0;
    GraphQL.execute(GraphQL.internal);
    TEST_ASSERT_FALSE(GraphQL.ok);
}

// An accessor asked about arguments it was never handed reports absence with a zeroed result.
void test_argument_accessor_without_values_reports_absence(void)
{
    GraphQL.argument.values = NULL;
    GraphQL.argument.name = "id";
    GraphQL.arg_int(GraphQL.internal);
    TEST_ASSERT_FALSE(GraphQL.ok);
    TEST_ASSERT_EQUAL_INT64(0, GraphQL.i64);

    GraphQL.arg_str(GraphQL.internal);
    TEST_ASSERT_FALSE(GraphQL.ok);
    TEST_ASSERT_NULL(GraphQL.text);

    GraphQL.arg_bool(GraphQL.internal);
    TEST_ASSERT_FALSE(GraphQL.ok);
    TEST_ASSERT_FALSE(GraphQL.b);
}
