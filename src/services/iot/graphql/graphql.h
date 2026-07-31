// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file graphql.h
 * @brief Zero-heap GraphQL query subset - parser + executor (PC_ENABLE_GRAPHQL).
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
 * Pure and host-tested. Bounds are compile-time (PC_GQL_*); parsing and
 * execution allocate nothing.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GRAPHQL_H
#define PROTOCORE_GRAPHQL_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_GRAPHQL

/** @brief Scalar value kinds a resolver can return. */
enum class pc_gql_type : uint8_t
{
    PC_GQL_NULL = 0,
    PC_GQL_INT,
    PC_GQL_FLOAT,
    PC_GQL_BOOL,
    PC_GQL_STR, ///< s points to a NUL-terminated string stable for the call.
};

/** @brief A scalar value (resolver output, or an argument). */
struct pc_gql_value
{
    pc_gql_type type; ///< the value's type.
    long long i;
    double f;
    bool b;
    const char *s;
};

/** @brief Opaque view of the arguments in scope at a resolved field. */
struct pc_gql_args;

/** @brief Read an int argument @p name; false if absent / not an int. */
bool pc_gql_arg_int(const pc_gql_args *args, const char *name, long long *out);
/** @brief Read a string argument @p name; false if absent / not a string. */
bool pc_gql_arg_str(const pc_gql_args *args, const char *name, const char **out);
/** @brief Read a bool argument @p name; false if absent / not a bool. */
bool pc_gql_arg_bool(const pc_gql_args *args, const char *name, bool *out);

/**
 * @brief Resolve the scalar leaf at dotted @p path (e.g. "device.uptime").
 *
 * Fill @p out with the value and return true; return false to emit JSON null.
 * @p args exposes every argument in scope along the path.
 */
typedef bool (*pc_gql_resolver_fn)(const char *path, const pc_gql_args *args, pc_gql_value *out);

/** @brief pc_graphql_execute() result codes. */
enum class pc_gql_result : int32_t
{
    PC_GQL_OK = 0,           ///< Executed; @p out holds `{"data":{...}}`.
    PC_GQL_ERR_PARSE = -1,   ///< Malformed query (syntax / unsupported construct).
    PC_GQL_ERR_LIMIT = -2,   ///< Exceeded a PC_GQL_* bound (nodes/args/depth/name).
    PC_GQL_ERR_OVERFLOW = -3 ///< Response did not fit @p cap.
};

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
 * @return ::PC_GQL_OK or a negative ::pc_gql_result.
 */
pc_gql_result pc_graphql_execute(const char *query, size_t len, pc_gql_resolver_fn resolver, char *out, size_t cap);

#endif // PC_ENABLE_GRAPHQL
#endif // PROTOCORE_GRAPHQL_H
