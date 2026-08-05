// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the WAMP codec (services/iot/wamp): the message builders (JSON arrays over
// JsonWriter) and the positional array parser. Pure host tests.

#include "services/iot/wamp/wamp.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_build_hello()
{
    char buf[128];
    size_t n = pc_wamp_build_hello(buf, sizeof(buf), "realm1", "{\"roles\":{}}");
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_EQUAL_STRING("[1,\"realm1\",{\"roles\":{}}]", buf);
}

void test_build_subscribe_default_options()
{
    char buf[128];
    size_t n = pc_wamp_build_subscribe(buf, sizeof(buf), 713845233, "com.myapp.topic", NULL);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_EQUAL_STRING("[32,713845233,{},\"com.myapp.topic\"]", buf);
}

void test_build_publish_with_args()
{
    char buf[160];
    size_t n = pc_wamp_build_publish(buf, sizeof(buf), 239714735, "com.myapp.temp", NULL, "[24.5]", "{\"unit\":\"C\"}");
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_EQUAL_STRING("[16,239714735,{},\"com.myapp.temp\",[24.5],{\"unit\":\"C\"}]", buf);
}

// kwargs without args still emits an empty positional Arguments list.
void test_build_publish_kwargs_only()
{
    char buf[160];
    size_t n = pc_wamp_build_publish(buf, sizeof(buf), 1, "t", NULL, NULL, "{\"k\":1}");
    TEST_ASSERT_EQUAL_STRING("[16,1,{},\"t\",[],{\"k\":1}]", buf);
}

void test_build_call_and_register_and_yield()
{
    char buf[160];
    TEST_ASSERT_GREATER_THAN(0,
                             (int)pc_wamp_build_call(buf, sizeof(buf), 7814135, "com.myapp.add", NULL, "[2,3]", NULL));
    TEST_ASSERT_EQUAL_STRING("[48,7814135,{},\"com.myapp.add\",[2,3]]", buf);

    TEST_ASSERT_GREATER_THAN(0, (int)pc_wamp_build_register(buf, sizeof(buf), 25349185, "com.myapp.add", NULL));
    TEST_ASSERT_EQUAL_STRING("[64,25349185,{},\"com.myapp.add\"]", buf);

    TEST_ASSERT_GREATER_THAN(0, (int)pc_wamp_build_yield(buf, sizeof(buf), 6131533, NULL, "[5]", NULL));
    TEST_ASSERT_EQUAL_STRING("[70,6131533,{},[5]]", buf);
}

void test_build_unsubscribe_and_goodbye()
{
    char buf[128];
    TEST_ASSERT_GREATER_THAN(0, (int)pc_wamp_build_unsubscribe(buf, sizeof(buf), 85346237, 5512315355ULL));
    TEST_ASSERT_EQUAL_STRING("[34,85346237,5512315355]", buf);
    TEST_ASSERT_GREATER_THAN(
        0, (int)pc_wamp_build_goodbye(buf, sizeof(buf), "wamp.close.goodbye_and_out", "{\"message\":\"bye\"}"));
    TEST_ASSERT_EQUAL_STRING("[6,{\"message\":\"bye\"},\"wamp.close.goodbye_and_out\"]", buf);
}

void test_build_unregister()
{
    char buf[128];
    // The canonical WAMP UNREGISTER example: [66, 788923562, 2103333224].
    TEST_ASSERT_GREATER_THAN(0, (int)pc_wamp_build_unregister(buf, sizeof(buf), 788923562, 2103333224ULL));
    TEST_ASSERT_EQUAL_STRING("[66,788923562,2103333224]", buf);
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_unregister(NULL, sizeof(buf), 1, 2)); // null buffer fails closed
}

void test_build_overflow_fails_closed()
{
    char buf[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_subscribe(buf, sizeof(buf), 1, "com.myapp.topic", NULL));
}

// Parse a WELCOME: [2, SessionId, Details].
void test_parse_type_and_id()
{
    const char *welcome = "[2, 9129137332, {\"roles\":{\"broker\":{}}}]";
    int type;
    TEST_ASSERT_TRUE(pc_wamp_get_type(welcome, &type));
    TEST_ASSERT_EQUAL_INT(WAMP_WELCOME, type);
    uint64_t session;
    TEST_ASSERT_TRUE(pc_wamp_get_uint(welcome, 1, &session));
    TEST_ASSERT_EQUAL_UINT64(9129137332ULL, session);
}

// Parse an EVENT: [36, SubscriptionId, PublicationId, Details, Arguments].
void test_parse_event_positions()
{
    const char *event = "[36,5512315355,4429313566,{},[\"hello\"]]";
    int type;
    uint64_t sub, pub;
    TEST_ASSERT_TRUE(pc_wamp_get_type(event, &type));
    TEST_ASSERT_EQUAL_INT(WAMP_EVENT, type);
    TEST_ASSERT_TRUE(pc_wamp_get_uint(event, 1, &sub));
    TEST_ASSERT_TRUE(pc_wamp_get_uint(event, 2, &pub));
    TEST_ASSERT_EQUAL_UINT64(5512315355ULL, sub);
    TEST_ASSERT_EQUAL_UINT64(4429313566ULL, pub);
    // element 4 is the Arguments list, returned raw.
    const char *s;
    size_t n;
    TEST_ASSERT_TRUE(pc_wamp_element(event, 4, &s, &n));
    TEST_ASSERT_EQUAL_size_t(9, n);
    TEST_ASSERT_EQUAL_MEMORY("[\"hello\"]", s, 9);
}

// The positional scanner must skip nested strings/objects with commas and brackets.
void test_parse_get_uri_and_nesting()
{
    const char *subscribed = "[33, 713845233, 5512315355]";
    uint64_t request, subscription;
    TEST_ASSERT_TRUE(pc_wamp_get_uint(subscribed, 1, &request));
    TEST_ASSERT_TRUE(pc_wamp_get_uint(subscribed, 2, &subscription));
    TEST_ASSERT_EQUAL_UINT64(713845233ULL, request);

    // A CALL with a tricky Options dict (commas, brackets, an escaped quote in a string).
    const char *call = "[48,1,{\"a\":[1,2],\"b\":\"x,]\\\"y\"},\"com.myapp.proc\",[7]]";
    int type;
    TEST_ASSERT_TRUE(pc_wamp_get_type(call, &type));
    TEST_ASSERT_EQUAL_INT(WAMP_CALL, type);
    char uri[32];
    TEST_ASSERT_TRUE(pc_wamp_get_uri(call, 3, uri, sizeof(uri)));
    TEST_ASSERT_EQUAL_STRING("com.myapp.proc", uri);
    uint64_t arg;
    const char *s;
    size_t n;
    TEST_ASSERT_TRUE(pc_wamp_element(call, 4, &s, &n));
    TEST_ASSERT_EQUAL_MEMORY("[7]", s, 3);
    (void)arg;
}

void test_parse_malformed()
{
    int type;
    uint64_t v;
    TEST_ASSERT_FALSE(pc_wamp_get_type("not an array", &type));
    TEST_ASSERT_FALSE(pc_wamp_get_uint("[2,9129137332]", 5, &v)); // index past the end
    TEST_ASSERT_FALSE(pc_wamp_get_uint("[2,\"notanumber\"]", 1, &v));
    char uri[8];
    TEST_ASSERT_FALSE(pc_wamp_get_uri("[2,123]", 1, uri, sizeof(uri))); // not a quoted string
}

// A URI that does not fit the destination fails closed without touching the buffer;
// one that exactly fills it (body + NUL) succeeds.
void test_get_uri_dest_bounds()
{
    const char *call = "[48,1,{},\"com.myapp.long.procedure.name\",[]]";
    char small[8];
    memset(small, '#', sizeof(small));
    TEST_ASSERT_FALSE(pc_wamp_get_uri(call, 3, small, sizeof(small))); // 26-char URI > 7
    TEST_ASSERT_EQUAL_CHAR('#', small[0]);                             // buffer untouched on failure

    // Exact fit: a 3-char URI needs 4 bytes (3 + NUL).
    char exact[4];
    TEST_ASSERT_TRUE(pc_wamp_get_uri("[2,\"abc\"]", 1, exact, sizeof(exact)));
    TEST_ASSERT_EQUAL_STRING("abc", exact);

    // One byte short of exact fails closed.
    char tight[3];
    TEST_ASSERT_FALSE(pc_wamp_get_uri("[2,\"abc\"]", 1, tight, sizeof(tight)));
}

// Every builder returns 0 on a null destination or a null required string argument.
void test_builder_null_guards()
{
    char buf[64];
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_hello(NULL, sizeof(buf), "r", NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_hello(buf, sizeof(buf), NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_goodbye(NULL, sizeof(buf), "u", NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_goodbye(buf, sizeof(buf), NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_subscribe(NULL, sizeof(buf), 1, "t", NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_subscribe(buf, sizeof(buf), 1, NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_unsubscribe(NULL, sizeof(buf), 1, 2));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_publish(NULL, sizeof(buf), 1, "t", NULL, NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_publish(buf, sizeof(buf), 1, NULL, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_call(NULL, sizeof(buf), 1, "p", NULL, NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_call(buf, sizeof(buf), 1, NULL, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_register(NULL, sizeof(buf), 1, "p", NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_register(buf, sizeof(buf), 1, NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_wamp_build_yield(NULL, sizeof(buf), 1, NULL, NULL, NULL));
}

// The emit_uint zero fast-path and an args-less PUBLISH (emit_args early return).
void test_emit_uint_zero_and_no_args()
{
    char buf[64];
    size_t n = pc_wamp_build_subscribe(buf, sizeof(buf), 0, "t", NULL); // request 0
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("[32,0,{},\"t\"]", buf);
    n = pc_wamp_build_publish(buf, sizeof(buf), 1, "t", NULL, NULL, NULL); // no args, no kwargs
    TEST_ASSERT_EQUAL_STRING("[16,1,{},\"t\"]", buf);
}

// The positional scanner's error paths: null message, empty array, and each way a value
// scan can fail (unterminated string, trailing backslash, bad nested string, unclosed
// bracket), plus pc_wamp_get_uri's argument guards.
void test_parser_error_paths()
{
    const char *s;
    size_t len;
    TEST_ASSERT_FALSE(pc_wamp_element(NULL, 0, &s, &len));     // null message
    TEST_ASSERT_FALSE(pc_wamp_element("[]", 0, &s, &len));     // empty array
    TEST_ASSERT_FALSE(pc_wamp_element("[\"abc", 0, &s, &len)); // unterminated string
    TEST_ASSERT_FALSE(pc_wamp_element("[\"x\\", 0, &s, &len)); // escape at end of string
    TEST_ASSERT_FALSE(pc_wamp_element("[{\"a", 0, &s, &len));  // bad string inside an object
    TEST_ASSERT_FALSE(pc_wamp_element("[[1,2", 0, &s, &len));  // unclosed nested array

    char uri[16];
    TEST_ASSERT_FALSE(pc_wamp_get_uri("[2,\"abc\"]", 1, NULL, sizeof(uri))); // null out
    TEST_ASSERT_FALSE(pc_wamp_get_uri("[2,\"abc\"]", 1, uri, 0));            // zero out_cap
    TEST_ASSERT_FALSE(pc_wamp_get_uri("[2]", 5, uri, sizeof(uri)));          // element not present
}

// Every builder takes an optional details/options JSON object. The suite above always passes
// NULL (the "{}" default); this pins the other side of that choice - a caller-supplied object
// must be emitted verbatim, in the slot the WAMP message structure puts it in.
void test_builder_explicit_options()
{
    char b[160];
    TEST_ASSERT_TRUE(pc_wamp_build_hello(b, sizeof(b), "realm1", "{\"roles\":{}}") > 0);
    TEST_ASSERT_EQUAL_STRING("[1,\"realm1\",{\"roles\":{}}]", b);
    TEST_ASSERT_TRUE(pc_wamp_build_goodbye(b, sizeof(b), "wamp.close", "{\"message\":\"bye\"}") > 0);
    TEST_ASSERT_EQUAL_STRING("[6,{\"message\":\"bye\"},\"wamp.close\"]", b);
    TEST_ASSERT_TRUE(pc_wamp_build_subscribe(b, sizeof(b), 7, "t.a", "{\"match\":\"prefix\"}") > 0);
    TEST_ASSERT_EQUAL_STRING("[32,7,{\"match\":\"prefix\"},\"t.a\"]", b);
    TEST_ASSERT_TRUE(pc_wamp_build_publish(b, sizeof(b), 8, "t.b", "{\"acknowledge\":true}", NULL, NULL) > 0);
    TEST_ASSERT_EQUAL_STRING("[16,8,{\"acknowledge\":true},\"t.b\"]", b);
    TEST_ASSERT_TRUE(pc_wamp_build_call(b, sizeof(b), 9, "p.c", "{\"timeout\":10}", NULL, NULL) > 0);
    TEST_ASSERT_EQUAL_STRING("[48,9,{\"timeout\":10},\"p.c\"]", b);
    TEST_ASSERT_TRUE(pc_wamp_build_register(b, sizeof(b), 10, "p.d", "{\"invoke\":\"roundrobin\"}") > 0);
    TEST_ASSERT_EQUAL_STRING("[64,10,{\"invoke\":\"roundrobin\"},\"p.d\"]", b);
    TEST_ASSERT_TRUE(pc_wamp_build_yield(b, sizeof(b), 11, "{\"progress\":true}", NULL, NULL) > 0);
    TEST_ASSERT_EQUAL_STRING("[70,11,{\"progress\":true}]", b);

    // ...and the other side for HELLO / GOODBYE, whose details argument the suite otherwise
    // always supplies: omitting it must emit the empty object, not nothing at all.
    TEST_ASSERT_TRUE(pc_wamp_build_hello(b, sizeof(b), "realm1", NULL) > 0);
    TEST_ASSERT_EQUAL_STRING("[1,\"realm1\",{}]", b);
    TEST_ASSERT_TRUE(pc_wamp_build_goodbye(b, sizeof(b), "wamp.close", NULL) > 0);
    TEST_ASSERT_EQUAL_STRING("[6,{},\"wamp.close\"]", b);
}

// skip_ws accepts all four JSON whitespace characters; the scanner must tolerate them between
// the bracket, the elements and the commas.
void test_parser_all_whitespace_forms()
{
    const char *s = NULL;
    size_t n = 0;
    TEST_ASSERT_TRUE(pc_wamp_element(" \t\n\r[ \t\n\r1 \t\n\r, \t\n\r2 ]", 1, &s, &n));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_CHAR('2', s[0]);
}

// A container closing at depth > 1 must NOT end the scan - only the outermost close does. Nested
// brackets of both kinds, and a bracket character inside a string, which the scanner skips whole.
void test_parser_nested_containers()
{
    const char *s = NULL;
    size_t n = 0;
    TEST_ASSERT_TRUE(pc_wamp_element("[[[1],[2]],9]", 0, &s, &n));
    TEST_ASSERT_EQUAL_size_t(9, n); // the whole "[[1],[2]]", not just up to the first ']'
    TEST_ASSERT_EQUAL_STRING_LEN("[[1],[2]]", s, 9);
    TEST_ASSERT_TRUE(pc_wamp_element("[{\"a\":{\"b\":1}},9]", 0, &s, &n));
    TEST_ASSERT_EQUAL_size_t(13, n);
    TEST_ASSERT_TRUE(pc_wamp_element("[{\"a\":\"]}\"},9]", 0, &s, &n)); // brackets inside a string
    TEST_ASSERT_EQUAL_size_t(10, n);
    TEST_ASSERT_TRUE(pc_wamp_element("[[[1],[2]],9]", 1, &s, &n)); // element after a nested one
    TEST_ASSERT_EQUAL_CHAR('9', s[0]);
}

// A bare token (number / true / false / null) ends at any of the seven terminators, and a value
// position that starts ON a terminator is malformed rather than a zero-length token.
void test_parser_bare_token_terminators()
{
    const char *s = NULL;
    size_t n = 0;
    TEST_ASSERT_TRUE(pc_wamp_element("[1,2]", 0, &s, &n)); // ','
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_TRUE(pc_wamp_element("[true]", 0, &s, &n)); // ']'
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_TRUE(pc_wamp_element("[null ]", 0, &s, &n)); // ' '
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_TRUE(pc_wamp_element("[12\t]", 0, &s, &n)); // '\t'
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_TRUE(pc_wamp_element("[34\n]", 0, &s, &n)); // '\n'
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_TRUE(pc_wamp_element("[56\r]", 0, &s, &n)); // '\r'
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_TRUE(pc_wamp_element("[{\"k\":7}]", 0, &s, &n)); // '}' ends the token inside
    TEST_ASSERT_EQUAL_size_t(7, n);
    TEST_ASSERT_TRUE(pc_wamp_element("[1}", 0, &s, &n)); // stray '}' terminates a bare token
    TEST_ASSERT_EQUAL_size_t(1, n);
    // NUL also ends a bare token. The scan is deliberately lenient here: element 0 is complete
    // before the (missing) ']', so it is returned rather than rejected for the unclosed array.
    TEST_ASSERT_TRUE(pc_wamp_element("[78", 0, &s, &n));
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_FALSE(pc_wamp_element("[78", 1, &s, &n));  // ...but element 1 really is absent
    TEST_ASSERT_FALSE(pc_wamp_element("[,1]", 0, &s, &n)); // value position starts on ','
    TEST_ASSERT_FALSE(pc_wamp_element("[", 0, &s, &n));    // NUL where an element was expected
}

// start / len are both optional out-parameters, as are the out-parameters of get_uint and
// get_type: a caller that only wants the yes/no answer may pass null and must still get it.
void test_parser_optional_out_params()
{
    const char *s = NULL;
    size_t n = 0;
    TEST_ASSERT_TRUE(pc_wamp_element("[1,2]", 0, NULL, &n)); // null start
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_TRUE(pc_wamp_element("[1,2]", 0, &s, NULL)); // null len
    TEST_ASSERT_EQUAL_CHAR('1', s[0]);
    TEST_ASSERT_TRUE(pc_wamp_element("[1,2]", 0, NULL, NULL)); // both null
    TEST_ASSERT_TRUE(pc_wamp_get_uint("[42,2]", 0, NULL));     // null out
    TEST_ASSERT_TRUE(pc_wamp_get_type("[42,2]", NULL));        // null out
}

// get_uint accepts digits only: a character below '0' and one above '9' must each be rejected
// (the two arms of the digit test), as must a non-numeric element.
void test_get_uint_rejects_non_digits()
{
    uint64_t v = 0;
    TEST_ASSERT_FALSE(pc_wamp_get_uint("[1-2,0]", 0, &v));   // '-' is below '0'
    TEST_ASSERT_FALSE(pc_wamp_get_uint("[1a,0]", 0, &v));    // 'a' is above '9'
    TEST_ASSERT_FALSE(pc_wamp_get_uint("[\"7\",0]", 0, &v)); // quoted, not a bare number
    TEST_ASSERT_FALSE(pc_wamp_get_type("[\"x\"]", NULL));    // type element not numeric
    TEST_ASSERT_TRUE(pc_wamp_get_uint("[1234567890,0]", 0, &v));
    TEST_ASSERT_EQUAL_UINT64(1234567890ull, v);
}

// get_uri requires a properly quoted string: too short, no opening quote, and no closing quote
// are each rejected (the three arms of the shape test).
void test_get_uri_shape_rejects()
{
    char uri[16];
    TEST_ASSERT_FALSE(pc_wamp_get_uri("[2,1]", 1, uri, sizeof(uri)));         // n < 2 (bare "1")
    TEST_ASSERT_FALSE(pc_wamp_get_uri("[2,12]", 1, uri, sizeof(uri)));        // n == 2 but not quoted
    TEST_ASSERT_FALSE(pc_wamp_get_uri("[2,{\"a\":1}]", 1, uri, sizeof(uri))); // opens with '{'
    TEST_ASSERT_TRUE(pc_wamp_get_uri("[2,\"ok\"]", 1, uri, sizeof(uri)));
    TEST_ASSERT_EQUAL_STRING("ok", uri);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_hello);
    RUN_TEST(test_build_subscribe_default_options);
    RUN_TEST(test_build_publish_with_args);
    RUN_TEST(test_build_publish_kwargs_only);
    RUN_TEST(test_build_call_and_register_and_yield);
    RUN_TEST(test_build_unsubscribe_and_goodbye);
    RUN_TEST(test_build_unregister);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_parse_type_and_id);
    RUN_TEST(test_parse_event_positions);
    RUN_TEST(test_parse_get_uri_and_nesting);
    RUN_TEST(test_parse_malformed);
    RUN_TEST(test_get_uri_dest_bounds);
    RUN_TEST(test_builder_null_guards);
    RUN_TEST(test_emit_uint_zero_and_no_args);
    RUN_TEST(test_parser_error_paths);
    RUN_TEST(test_builder_explicit_options);
    RUN_TEST(test_parser_all_whitespace_forms);
    RUN_TEST(test_parser_nested_containers);
    RUN_TEST(test_parser_bare_token_terminators);
    RUN_TEST(test_parser_optional_out_params);
    RUN_TEST(test_get_uint_rejects_non_digits);
    RUN_TEST(test_get_uri_shape_rejects);
    return UNITY_END();
}
