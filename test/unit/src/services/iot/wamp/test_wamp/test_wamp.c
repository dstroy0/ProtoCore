// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the WAMP message codec (services/iot/wamp/wamp.h).
//
// The load-bearing case is test_published_subscribe: WAMP Basic Profile sec 3.4.2.3 prints
// "[32, 713845233, {}, "com.myapp.mytopic1"]" as its worked SUBSCRIBE, and every builder case below
// rebuilds a message the specification prints verbatim, element for element. JSON whitespace is
// insignificant, so the expected strings are the same messages in the compact spelling this writer
// emits. The reader cases scan the specification's own received messages.

#include "services/iot/wamp/wamp.h"
#include <string.h>

#include <unity.h>

static uint8_t wamp_work[16]; // the borrow an entry takes; Wamp never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_buf[512];
static char g_uri[128];

// Clear every element member, so a build only carries what its own case set.
static void reset(void)
{
    WampV.out.buf = g_buf;
    WampV.out.cap = sizeof(g_buf);
    WampV.id.request = 0;
    WampV.id.subscription = 0;
    WampV.id.registration = 0;
    WampV.uri.realm = NULL;
    WampV.uri.reason = NULL;
    WampV.uri.topic = NULL;
    WampV.uri.procedure = NULL;
    WampV.payload.details = NULL;
    WampV.payload.options = NULL;
    WampV.payload.arguments = NULL;
    WampV.payload.arguments_kw = NULL;
}

// WAMP sec 3.4.2.3: "[SUBSCRIBE, Request|id, Options|dict, Topic|uri]", printed as
// [32, 713845233, {}, "com.myapp.mytopic1"]. sec 3.5 gives SUBSCRIBE the message type code 32, and
// an unset Options|dict emits the empty dict the example carries.
void test_published_subscribe(void)
{
    reset();
    WampV.id.request = 713845233u;
    WampV.uri.topic = "com.myapp.mytopic1";
    Wamp.build_subscribe(wamp_work);

    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("[32,713845233,{},\"com.myapp.mytopic1\"]", g_buf);
    TEST_ASSERT_EQUAL_UINT(strlen(g_buf), WampV.n);
}

// sec 3.4.1.1: "[HELLO, Realm|uri, Details|dict]", printed as
// [1, "somerealm", {"roles": {"publisher": {}, "subscriber": {}}}]. Details is a pre-formatted
// literal, so it lands verbatim; unset it emits {}.
void test_published_hello(void)
{
    reset();
    WampV.uri.realm = "somerealm";
    WampV.payload.details = "{\"roles\":{\"publisher\":{},\"subscriber\":{}}}";
    Wamp.build_hello(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("[1,\"somerealm\",{\"roles\":{\"publisher\":{},\"subscriber\":{}}}]", g_buf);

    reset();
    WampV.uri.realm = "somerealm";
    Wamp.build_hello(wamp_work);
    TEST_ASSERT_EQUAL_STRING("[1,\"somerealm\",{}]", g_buf);
}

// sec 3.4.1.4: "[GOODBYE, Details|dict, Reason|uri]", printed as
// [6, {}, "wamp.close.goodbye_and_out"] and
// [6, {"message": "The host is shutting down now."}, "wamp.close.system_shutdown"].
// The Details dict precedes the Reason here, unlike HELLO.
void test_published_goodbye(void)
{
    reset();
    WampV.uri.reason = "wamp.close.goodbye_and_out";
    Wamp.build_goodbye(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("[6,{},\"wamp.close.goodbye_and_out\"]", g_buf);

    reset();
    WampV.uri.reason = "wamp.close.system_shutdown";
    WampV.payload.details = "{\"message\":\"The host is shutting down now.\"}";
    Wamp.build_goodbye(wamp_work);
    TEST_ASSERT_EQUAL_STRING("[6,{\"message\":\"The host is shutting down now.\"},"
                             "\"wamp.close.system_shutdown\"]",
                             g_buf);
}

// sec 3.4.2.5: "[UNSUBSCRIBE, Request|id, SUBSCRIBED.Subscription|id]", printed as
// [34, 85346237, 5512315355]. The second id is the one SUBSCRIBED handed back, not the request.
void test_published_unsubscribe(void)
{
    reset();
    WampV.id.request = 85346237u;
    WampV.id.subscription = 5512315355ull;
    Wamp.build_unsubscribe(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("[34,85346237,5512315355]", g_buf);
}

// sec 3.4.2.1 prints all three PUBLISH forms:
//   [16, 239714735, {}, "com.myapp.mytopic1"]
//   [16, 239714735, {}, "com.myapp.mytopic1", ["Hello, world!"]]
//   [16, 239714735, {}, "com.myapp.mytopic1", [], {"color": "orange", "sizes": [23, 42, 7]}]
// The third shows the rule that fixes the payload positions: ArgumentsKw sits one element past
// Arguments, so a keyword-only payload still emits the empty Arguments list to hold the place.
void test_published_publish(void)
{
    reset();
    WampV.id.request = 239714735u;
    WampV.uri.topic = "com.myapp.mytopic1";
    Wamp.build_publish(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("[16,239714735,{},\"com.myapp.mytopic1\"]", g_buf);

    reset();
    WampV.id.request = 239714735u;
    WampV.uri.topic = "com.myapp.mytopic1";
    WampV.payload.arguments = "[\"Hello, world!\"]";
    Wamp.build_publish(wamp_work);
    TEST_ASSERT_EQUAL_STRING("[16,239714735,{},\"com.myapp.mytopic1\",[\"Hello, world!\"]]", g_buf);

    reset();
    WampV.id.request = 239714735u;
    WampV.uri.topic = "com.myapp.mytopic1";
    WampV.payload.arguments_kw = "{\"color\":\"orange\",\"sizes\":[23,42,7]}";
    Wamp.build_publish(wamp_work);
    TEST_ASSERT_EQUAL_STRING("[16,239714735,{},\"com.myapp.mytopic1\",[],"
                             "{\"color\":\"orange\",\"sizes\":[23,42,7]}]",
                             g_buf);
}

// sec 3.4.3.1: "[CALL, Request|id, Options|dict, Procedure|uri]", printed as
// [48, 7814135, {}, "com.myapp.ping"] and [48, 7814135, {}, "com.myapp.add2", [23, 7]].
void test_published_call(void)
{
    reset();
    WampV.id.request = 7814135u;
    WampV.uri.procedure = "com.myapp.ping";
    Wamp.build_call(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("[48,7814135,{},\"com.myapp.ping\"]", g_buf);

    reset();
    WampV.id.request = 7814135u;
    WampV.uri.procedure = "com.myapp.add2";
    WampV.payload.arguments = "[23,7]";
    Wamp.build_call(wamp_work);
    TEST_ASSERT_EQUAL_STRING("[48,7814135,{},\"com.myapp.add2\",[23,7]]", g_buf);
}

// sec 3.4.3.3: "[REGISTER, Request|id, Options|dict, Procedure|uri]", printed as
// [64, 25349185, {}, "com.myapp.myprocedure1"]. sec 3.4.3.5 prints the matching
// [66, 788923562, 2103333224] for UNREGISTER.
void test_published_register_and_unregister(void)
{
    reset();
    WampV.id.request = 25349185u;
    WampV.uri.procedure = "com.myapp.myprocedure1";
    Wamp.build_register(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("[64,25349185,{},\"com.myapp.myprocedure1\"]", g_buf);

    reset();
    WampV.id.request = 788923562u;
    WampV.id.registration = 2103333224u;
    Wamp.build_unregister(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("[66,788923562,2103333224]", g_buf);
}

// sec 3.4.3.8: "[YIELD, INVOCATION.Request|id, Options|dict]", printed as [70, 6131533, {}],
// [70, 6131533, {}, [30]] and [70, 6131533, {}, [], {"userid": 123, "karma": 10}].
void test_published_yield(void)
{
    reset();
    WampV.id.request = 6131533u;
    Wamp.build_yield(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("[70,6131533,{}]", g_buf);

    reset();
    WampV.id.request = 6131533u;
    WampV.payload.arguments = "[30]";
    Wamp.build_yield(wamp_work);
    TEST_ASSERT_EQUAL_STRING("[70,6131533,{},[30]]", g_buf);

    reset();
    WampV.id.request = 6131533u;
    WampV.payload.arguments_kw = "{\"userid\":123,\"karma\":10}";
    Wamp.build_yield(wamp_work);
    TEST_ASSERT_EQUAL_STRING("[70,6131533,{},[],{\"userid\":123,\"karma\":10}]", g_buf);
}

// An Options|dict the caller supplies replaces the default empty dict, in the position the message
// layout gives it.
void test_options_dict_is_carried(void)
{
    reset();
    WampV.id.request = 1u;
    WampV.uri.topic = "com.myapp.t";
    WampV.payload.options = "{\"acknowledge\":true}";
    Wamp.build_publish(wamp_work);
    TEST_ASSERT_EQUAL_STRING("[16,1,{\"acknowledge\":true},\"com.myapp.t\"]", g_buf);
}

// sec 2.1.2 puts an id in 1..2^53. Both ends of that range render as plain decimal digits, and
// zero renders as a single 0 rather than an empty element.
void test_id_range(void)
{
    reset();
    WampV.id.request = 1u;
    WampV.id.subscription = 9007199254740992ull; // 2^53
    Wamp.build_unsubscribe(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("[34,1,9007199254740992]", g_buf);

    reset();
    Wamp.build_unsubscribe(wamp_work);
    TEST_ASSERT_EQUAL_STRING("[34,0,0]", g_buf);
}

// sec 2.1.1 URIs carry dots but no quotes, so the writer's escaping leaves them untouched. A URI
// that did carry a quote must come out escaped rather than closing the JSON string early.
void test_uri_is_written_as_a_json_string(void)
{
    reset();
    WampV.uri.realm = "a\"b";
    Wamp.build_hello(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("[1,\"a\\\"b\",{}]", g_buf);
}

// A message whose required URI element is unset builds nothing: an element the layout names cannot
// be left out of the list.
void test_build_refuses_a_missing_uri(void)
{
    reset();
    Wamp.build_hello(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, WampV.n);

    reset();
    Wamp.build_subscribe(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);

    reset();
    Wamp.build_call(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);

    reset();
    Wamp.build_goodbye(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);
}

// A buffer too small for the whole list reports nothing written, so a truncated JSON array never
// reaches the peer.
void test_build_refuses_a_short_buffer(void)
{
    char small[8];
    WampV.out.buf = small;
    WampV.out.cap = sizeof(small);
    WampV.id.request = 713845233u;
    WampV.uri.topic = "com.myapp.mytopic1";
    WampV.payload.options = NULL;
    Wamp.build_subscribe(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, WampV.n);

    WampV.out.buf = NULL;
    WampV.out.cap = 0;
    Wamp.build_subscribe(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);
}

// sec 3.3: the first element of a message is its type code. The reader takes it off the wire form
// the specification prints, whitespace and all.
void test_read_message_type(void)
{
    WampV.parse.msg = "[32, 713845233, {}, \"com.myapp.mytopic1\"]";
    Wamp.get_type(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_INT32(WAMP_SUBSCRIBE, WampV.i32);

    WampV.parse.msg = "[2, 9129137332, {\"roles\": {\"broker\": {}}}]"; // sec 3.4.1.2 WELCOME
    Wamp.get_type(wamp_work);
    TEST_ASSERT_EQUAL_INT32(WAMP_WELCOME, WampV.i32);

    WampV.parse.msg = "[8, 32, 713845233, {}, \"wamp.error.not_authorized\"]"; // sec 3.4.2.4 ERROR
    Wamp.get_type(wamp_work);
    TEST_ASSERT_EQUAL_INT32(WAMP_ERROR, WampV.i32);
}

// sec 3.4.2.4 prints SUBSCRIBED as [33, 713845233, 5512315355]: element 1 is the request the
// SUBSCRIBE carried, element 2 the subscription the broker assigned.
void test_read_ids_by_position(void)
{
    WampV.parse.msg = "[33, 713845233, 5512315355]";
    WampV.parse.index = 1;
    Wamp.get_id(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_UINT64(713845233ull, WampV.u64);

    WampV.parse.index = 2;
    Wamp.get_id(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_UINT64(5512315355ull, WampV.u64);
}

// sec 3.4.2.4 prints EVENT as [36, 5512315355, 4429313566, {}], and its payload form as
// [36, 5512315355, 4429313566, {}, [], {"color": "orange", "sizes": [23, 42, 7]}]. The scanner
// steps over a nested dict and a nested list as single elements, brackets inside them included.
void test_read_across_nested_elements(void)
{
    WampV.parse.msg = "[36, 5512315355, 4429313566, {}, [], {\"color\": \"orange\", \"sizes\": [23, 42, 7]}]";

    Wamp.get_type(wamp_work);
    TEST_ASSERT_EQUAL_INT32(WAMP_EVENT, WampV.i32);

    WampV.parse.index = 2;
    Wamp.get_id(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_UINT64(4429313566ull, WampV.u64);

    WampV.parse.index = 5;
    Wamp.element(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_UINT(strlen("{\"color\": \"orange\", \"sizes\": [23, 42, 7]}"), WampV.n);
    TEST_ASSERT_EQUAL_MEMORY("{\"color\": \"orange\", \"sizes\": [23, 42, 7]}", WampV.text, WampV.n);
}

// sec 2.1.1: a URI carries no whitespace and no '#', so the copy strips the quotes and undoes no
// escape.
void test_read_uri(void)
{
    WampV.parse.msg = "[32, 713845233, {}, \"com.myapp.mytopic1\"]";
    WampV.parse.index = 3;
    WampV.parse.uri_out = g_uri;
    WampV.parse.uri_cap = sizeof(g_uri);
    Wamp.get_uri(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("com.myapp.mytopic1", g_uri);

    WampV.parse.msg = "[8, 32, 713845233, {}, \"wamp.error.not_authorized\"]";
    WampV.parse.index = 4;
    Wamp.get_uri(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("wamp.error.not_authorized", g_uri);
}

// A slice points into the received message rather than copying it.
void test_element_slices_the_message(void)
{
    static const char MSG[] = "[50, 7814135, {}]"; // sec 3.4.3.2 RESULT
    WampV.parse.msg = MSG;
    WampV.parse.index = 2;
    Wamp.element(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_UINT(2u, WampV.n);
    TEST_ASSERT_EQUAL_MEMORY("{}", WampV.text, 2);
    TEST_ASSERT_TRUE(WampV.text >= MSG && WampV.text < MSG + sizeof(MSG));
}

// An index past the last element reports nothing, rather than the last element or a stale slice.
void test_read_past_the_end(void)
{
    WampV.parse.msg = "[50, 7814135, {}]";
    WampV.parse.index = 3;
    Wamp.element(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);
    TEST_ASSERT_NULL(WampV.text);
    TEST_ASSERT_EQUAL_UINT(0u, WampV.n);

    WampV.parse.index = 9;
    Wamp.get_id(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);
}

// An element that is not a run of decimal digits is not an id, and one that is not a quoted string
// is not a URI. Neither is coerced.
void test_reads_refuse_the_wrong_element_kind(void)
{
    WampV.parse.msg = "[32, 713845233, {}, \"com.myapp.mytopic1\"]";

    WampV.parse.index = 2; // the Options dict
    Wamp.get_id(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);

    WampV.parse.index = 3; // the topic URI
    Wamp.get_id(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);

    WampV.parse.index = 1; // the request id
    WampV.parse.uri_out = g_uri;
    WampV.parse.uri_cap = sizeof(g_uri);
    Wamp.get_uri(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);
}

// A destination one octet short of the URI and its NUL copies nothing, rather than a truncated
// URI - which names a different topic.
void test_get_uri_refuses_a_short_destination(void)
{
    char small[sizeof("com.myapp.mytopic1") - 1];
    WampV.parse.msg = "[32, 713845233, {}, \"com.myapp.mytopic1\"]";
    WampV.parse.index = 3;
    WampV.parse.uri_out = small;
    WampV.parse.uri_cap = sizeof(small);
    Wamp.get_uri(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);

    WampV.parse.uri_out = NULL;
    WampV.parse.uri_cap = 0;
    Wamp.get_uri(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);
}

// Text that is not a message list is refused rather than scanned: sec 3.3 makes a message a list.
void test_read_refuses_a_non_list(void)
{
    WampV.parse.index = 0;
    WampV.parse.msg = "{\"a\":1}";
    Wamp.element(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);

    WampV.parse.msg = "";
    Wamp.element(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);

    WampV.parse.msg = NULL;
    Wamp.element(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);

    WampV.parse.msg = "[32, 713845233"; // the list never closes
    WampV.parse.index = 3;
    Wamp.element(wamp_work);
    TEST_ASSERT_FALSE(WampV.ok);
}

// A built message reads back through the reader: the type code, the ids and the URI it was given.
void test_build_then_read_round_trip(void)
{
    reset();
    WampV.id.request = 25349185u;
    WampV.uri.procedure = "com.myapp.myprocedure1";
    Wamp.build_register(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);

    WampV.parse.msg = g_buf;
    Wamp.get_type(wamp_work);
    TEST_ASSERT_EQUAL_INT32(WAMP_REGISTER, WampV.i32);

    WampV.parse.index = 1;
    Wamp.get_id(wamp_work);
    TEST_ASSERT_EQUAL_UINT64(25349185ull, WampV.u64);

    WampV.parse.index = 3;
    WampV.parse.uri_out = g_uri;
    WampV.parse.uri_cap = sizeof(g_uri);
    Wamp.get_uri(wamp_work);
    TEST_ASSERT_TRUE(WampV.ok);
    TEST_ASSERT_EQUAL_STRING("com.myapp.myprocedure1", g_uri);
}
