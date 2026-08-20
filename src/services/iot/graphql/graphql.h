// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file graphql.h
 * @brief Zero-heap GraphQL executor over a query subset (PROTOCORE_ENABLE_GRAPHQL).
 *
 * **The governing standard is not IETF.** GraphQL is specified by the GraphQL Foundation and
 * published at spec.graphql.org, released by date. Every section cited in this module is the
 * **October 2021** release. There is no RFC for GraphQL.
 *
 * The document source text is parsed into fixed pools (no heap) by the sec 2 grammar, the operation
 * is executed by the sec 6 algorithms, and the result is serialized as the sec 7.1 response map in
 * the sec 7.2.1 JSON form. The client picks the shape: sec 2.4 says an operation "selects the set of
 * information it needs, and will receive exactly that information and nothing more".
 *
 * **Schema-free model.** There is no type system (sec 3). A Field carrying a SelectionSet
 * (`obj { a b }`, sec 2.5) is completed by executing that selection set; a Field with none is a
 * scalar leaf, completed by calling the resolver (ResolveFieldValue, sec 6.4.2). Arguments met along
 * the path (sec 2.6) stay in scope, so a resolver for `sensor.value` reads `id` from
 * `sensor(id: 2) { value }`. The application implements one function: the value of the scalar at
 * this dotted path, given the arguments in scope.
 *
 * Supported: one operation, either the sec 2.3 query shorthand (`{...}`) or `query [Name] {...}`;
 * nested selection sets (sec 2.4); field arguments (sec 2.6) taking Int, Float, String, Boolean and
 * Null values (sec 2.9.1 to sec 2.9.5); comments (sec 2.1.4) and insignificant commas (sec 2.1.5).
 * Out of scope: mutations and subscriptions (sec 2.3), fragments (sec 2.8), variables (sec 2.10),
 * directives (sec 2.12), aliases (sec 2.7), list values (sec 2.9.7), input objects (sec 2.9.8),
 * block strings and `\uXXXX` escapes (sec 2.9.4), and lists of objects (sec 3.11). Each parses as a
 * request error.
 *
 * Two deviations from the released spec, both stated rather than hidden: Int is carried as a 64-bit
 * value where sec 3.5.1 defines a signed 32-bit scalar, and a leaf that fails to resolve completes
 * as null with no `errors` entry, where sec 7.1.2 says a field error should be listed.
 *
 * A malformed document raises a request error (sec 7.1.2): execution does not begin and the response
 * map carries `errors` and no `data`.
 *
 * Bounds are compile-time (PROTOCORE_GQL_*); parsing and execution allocate nothing.
 *
 * The module exports one symbol, @ref GraphQL. Everything in graphql.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GRAPHQL_H
#define PROTOCORE_GRAPHQL_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_GRAPHQL

PROTOCORE_BEGIN_DECLS

/** @brief The scalar kinds a resolved leaf or an argument carries (GraphQL spec sec 3.5). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_GQL_NULL = 0, ///< the Null value (sec 2.9.5).
    PROTOCORE_GQL_INT,      ///< Int (sec 3.5.1), read from @c i.
    PROTOCORE_GQL_FLOAT,    ///< Float (sec 3.5.2), read from @c f.
    PROTOCORE_GQL_BOOL,     ///< Boolean (sec 3.5.4), read from @c b.
    PROTOCORE_GQL_STR,      ///< String (sec 3.5.3), read from @c s, NUL-terminated and stable for the call.
} protocore_gql_type;

/** @brief One scalar: a resolved field value, or an argument's Value (spec sec 2.9). */
typedef struct
{
    protocore_gql_type type; ///< which member below holds the value.
    long long i;             ///< the Int.
    double f;                ///< the Float.
    proto_bool b;            ///< the Boolean.
    const char *s;           ///< the String.
} protocore_gql_value;

/** @brief The argument values in scope at a resolved field (spec sec 6.4.1 coercedValues). */
struct protocore_gql_args;

/**
 * @brief ResolveFieldValue (spec sec 6.4.2): the scalar at dotted @p path, e.g. "device.uptime".
 *
 * Fills @p out and returns true, or returns false to complete the field as null (sec 6.4.3).
 * @p args names the argument values in scope, read back through ::GraphQLNs::arg_int,
 * ::GraphQLNs::arg_str and ::GraphQLNs::arg_bool.
 */
typedef proto_bool (*protocore_gql_resolver_fn)(const char *path, const struct protocore_gql_args *args,
                                                protocore_gql_value *out);

/** @brief What one execute reports. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_GQL_OK = 0,           ///< Executed; the response map holds `data` (spec sec 7.1.1).
    PROTOCORE_GQL_ERR_PARSE = -1,   ///< Request error (sec 7.1.2): the document does not parse.
    PROTOCORE_GQL_ERR_LIMIT = -2,   ///< Request error (sec 7.1.2): a PROTOCORE_GQL_* bound was exceeded.
    PROTOCORE_GQL_ERR_OVERFLOW = -3 ///< The serialized response did not fit the buffer.
} protocore_gql_result;

/** @brief ExecuteRequest (spec sec 6.1): the document to run and the resolver its leaves call. */
typedef struct
{
    const char *document;               ///< the ExecutableDocument source text (sec 2.2)
    size_t len;                         ///< how many octets of it there are
    protocore_gql_resolver_fn resolver; ///< ResolveFieldValue (sec 6.4.2); NULL completes every leaf as null
} GraphQLRequestArgs;

/** @brief Where the response map is serialized (spec sec 7.1, in the sec 7.2.1 JSON form). */
typedef struct
{
    char *out;  ///< the buffer the response is written into
    size_t cap; ///< how much room it has, the NUL included
} GraphQLResponseArgs;

/** @brief One Argument read by name out of the values in scope (spec sec 2.6). */
typedef struct
{
    const struct protocore_gql_args *values; ///< the argument values a resolver was handed (sec 6.4.1)
    const char *name;                        ///< the argument's Name, matched case-sensitively (sec 2.1.9)
} GraphQLArgumentArgs;

/**
 * @brief The GraphQL executor (GraphQL spec, October 2021 release; not an IETF standard).
 *
 * A caller sets the members a call takes, invokes it through ::GraphQL, and reads the outcome off
 * the same handle. A resolver running inside an execute sets @c argument and reads @c ok with
 * @c i64, @c text or @c b.
 *
 * No slot member: one document executes at a time, so no call names a row.
 *
 * @var GraphQLNs::request   the document an execute runs and the resolver its leaves call (sec 6.1)
 * @var GraphQLNs::response  where an execute serializes the response map (sec 7.1)
 * @var GraphQLNs::argument  the argument an accessor reads, and the values it reads from (sec 2.6)
 * @var GraphQLNs::ok        a call's true/false outcome: an execute succeeded, or an argument was
 *                           present with the type the accessor asked for
 * @var GraphQLNs::n         octets an execute wrote to @c response.out, excluding the NUL, 0 if
 *                           nothing was emitted
 * @var GraphQLNs::result    an execute's outcome code
 * @var GraphQLNs::i64       the Int an arg_int read (sec 3.5.1), 0 when @c ok is false
 * @var GraphQLNs::text      the String an arg_str read (sec 3.5.3), NULL when @c ok is false
 * @var GraphQLNs::b         the Boolean an arg_bool read (sec 3.5.4), false when @c ok is false
 * @var GraphQLNs::execute   parse @c request, execute its operation, and serialize the response map
 *                           into @c response (sec 6.1)
 * @var GraphQLNs::arg_int   read @c argument as an Int
 * @var GraphQLNs::arg_str   read @c argument as a String
 * @var GraphQLNs::arg_bool  read @c argument as a Boolean
 */
typedef struct
{
    GraphQLRequestArgs request;   ///< what an execute runs
    GraphQLResponseArgs response; ///< where its response lands
    GraphQLArgumentArgs argument; ///< what an accessor reads
    proto_bool ok;
    size_t n;
    protocore_gql_result result;
    long long i64;
    const char *text;
    proto_bool b;
} GraphQLVars;

/** @brief The operands and the outcome. */
extern GraphQLVars GraphQLV;

/** @brief The entries. */
typedef struct
{
    void (*const execute)(uint8_t *restrict work);
    void (*const arg_int)(uint8_t *restrict work);
    void (*const arg_str)(uint8_t *restrict work);
    void (*const arg_bool)(uint8_t *restrict work);
} GraphQLNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in GraphQLV or a region of the borrow at a fixed offset.
void protocore_graph_ql_execute(uint8_t *restrict work);
void protocore_graph_ql_arg_int(uint8_t *restrict work);
void protocore_graph_ql_arg_str(uint8_t *restrict work);
void protocore_graph_ql_arg_bool(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `GraphQL.execute(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const GraphQLNs GraphQL __attribute__((unused)) = {
    .execute = protocore_graph_ql_execute,
    .arg_int = protocore_graph_ql_arg_int,
    .arg_str = protocore_graph_ql_arg_str,
    .arg_bool = protocore_graph_ql_arg_bool,
};

/**
 * @brief The PROTOCORE_GRAPHQL_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_graphql_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_GRAPHQL

#endif // PROTOCORE_GRAPHQL_H
