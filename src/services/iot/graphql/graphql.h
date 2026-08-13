// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file graphql.h
 * @brief Zero-heap GraphQL query subset - parser + executor (PROTOCORE_ENABLE_GRAPHQL).
 *
 * A small, deterministic GraphQL *query* engine for a constrained device: it
 * parses a query document into a fixed AST node pool (no heap), then walks the
 * selection set emitting a `{"data":{...}}` response that mirrors exactly the
 * fields requested - the core GraphQL property (the client picks the shape).
 *
 * **Schema-free model.** There is no separate schema: a field that carries a
 * sub-selection (`obj { a b }`) is an object - the engine recurses and emits the
 * nested object - and a field with no sub-selection is a leaf scalar, for which
 * the engine calls your single resolver. Arguments encountered along the path
 * (`sensor(id: 2) { value }`) are collected and handed to the leaf resolver, so a
 * resolver for `sensor.value` can read `id`. The app implements one function: "the
 * value of the scalar at this dotted path, given these args."
 *
 * Supported: a single query operation (bare `{...}` or `query [Name] {...}`),
 * nested selection sets, field arguments (int / float / string / bool / null),
 * and insignificant commas. Out of scope (keeps it bounded + deterministic):
 * mutations, subscriptions, fragments, variables, directives, aliases, lists of
 * objects. Malformed input fails closed with `{"errors":[...]}`.
 *
 * Pure and host-tested. Bounds are compile-time (PROTOCORE_GQL_*); parsing and
 * execution allocate nothing.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GRAPHQL_H
#define PROTOCORE_GRAPHQL_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_GRAPHQL

/** @brief Scalar value kinds a resolver can return. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_GQL_NULL = 0,
    PROTOCORE_GQL_INT,
    PROTOCORE_GQL_FLOAT,
    PROTOCORE_GQL_BOOL,
    PROTOCORE_GQL_STR, ///< s points to a NUL-terminated string stable for the call.
} protocore_gql_type;

/** @brief A scalar value (resolver output, or an argument). */
typedef struct
{
    protocore_gql_type type; ///< the value's type.
    long long i;
    double f;
    proto_bool b;
    const char *s;
} protocore_gql_value;

/** @brief Opaque view of the arguments in scope at a resolved field. */
struct protocore_gql_args;

/** @brief Read an int argument @p name; false if absent / not an int. */
proto_bool protocore_gql_arg_int(const struct protocore_gql_args *args, const char *name, long long *out);
/** @brief Read a string argument @p name; false if absent / not a string. */
proto_bool protocore_gql_arg_str(const struct protocore_gql_args *args, const char *name, const char **out);
/** @brief Read a bool argument @p name; false if absent / not a bool. */
proto_bool protocore_gql_arg_bool(const struct protocore_gql_args *args, const char *name, proto_bool *out);

/**
 * @brief Resolve the scalar leaf at dotted @p path (e.g. "device.uptime").
 *
 * Fill @p out with the value and return true; return false to emit JSON null.
 * @p args exposes every argument in scope along the path.
 */
typedef proto_bool (*protocore_gql_resolver_fn)(const char *path, const struct protocore_gql_args *args,
                                                protocore_gql_value *out);

/** @brief protocore_graphql_execute() result codes. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_GQL_OK = 0,           ///< Executed; @p out holds `{"data":{...}}`.
    PROTOCORE_GQL_ERR_PARSE = -1,   ///< Malformed query (syntax / unsupported construct).
    PROTOCORE_GQL_ERR_LIMIT = -2,   ///< Exceeded a PROTOCORE_GQL_* bound (nodes/args/depth/name).
    PROTOCORE_GQL_ERR_OVERFLOW = -3 ///< Response did not fit @p cap.
} protocore_gql_result;

/**
 * @brief Parse and execute a GraphQL query, writing the JSON response.
 *
 * On success writes `{"data":{...}}`; on a parse/limit error writes
 * `{"errors":[{"message":"..."}]}` (and still returns the negative code) when it
 * fits, else nothing.
 *
 * @param query,len  the query document.
 * @param resolver   leaf resolver (may be nullptr -> every leaf is null).
 * @param out,cap    response buffer and capacity.
 * @return ::PROTOCORE_GQL_OK or a negative ::protocore_gql_result.
 */
protocore_gql_result protocore_graphql_execute(const char *query, size_t len, protocore_gql_resolver_fn resolver,
                                               char *out, size_t cap);

#endif // PROTOCORE_ENABLE_GRAPHQL

PROTOCORE_END_DECLS

#endif // PROTOCORE_GRAPHQL_H
