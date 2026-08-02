// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wamp.h
 * @brief WAMP (Web Application Messaging Protocol) codec (PC_ENABLE_WAMP) - zero-heap
 *        builders + a positional parser for the unified RPC + PubSub protocol, which rides
 *        the shipped WebSocket layer (subprotocol `wamp.2.json`).
 *
 * A WAMP message is a JSON array whose first element is the integer message type, e.g.
 * `SUBSCRIBE = [32, Request|id, Options|dict, Topic|uri]`. The builders drive the shared
 * @ref JsonWriter to emit these arrays into a caller buffer (Options/Details default to
 * `{}`; Arguments / ArgumentsKw are passed as pre-formatted JSON literals or omitted). The
 * parser is a small positional scanner over an inbound array: extract the message type, an
 * id at a given position, or a URI - enough to drive WELCOME / SUBSCRIBED / EVENT / RESULT
 * / INVOCATION / ERROR handling. Message codes verified against the WAMP spec.
 *
 * The WebSocket connection and the session / subscription / registration tables are the
 * application's; this is the message codec.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_WAMP_H
#define PROTOCORE_WAMP_H

#include "protocore_config.h"

#if PC_ENABLE_WAMP

// WAMP message type codes (basic + advanced profile).
#define WAMP_HELLO 1
#define WAMP_WELCOME 2
#define WAMP_ABORT 3
#define WAMP_GOODBYE 6
#define WAMP_ERROR 8
#define WAMP_PUBLISH 16
#define WAMP_PUBLISHED 17
#define WAMP_SUBSCRIBE 32
#define WAMP_SUBSCRIBED 33
#define WAMP_UNSUBSCRIBE 34
#define WAMP_UNSUBSCRIBED 35
#define WAMP_EVENT 36
#define WAMP_CALL 48
#define WAMP_RESULT 50
#define WAMP_REGISTER 64
#define WAMP_REGISTERED 65
#define WAMP_UNREGISTER 66
#define WAMP_UNREGISTERED 67
#define WAMP_INVOCATION 68
#define WAMP_YIELD 70

// ---- builders (return bytes written, or 0 on overflow / bad input) ----
// For the *_json params: pass a pre-formatted JSON literal, or nullptr for the default
// (`{}` for options/details). Passing kwargs without args emits an empty `[]` for args so
// the positional layout stays valid.

/** @brief HELLO: `[1, "realm", Details]`. */
size_t pc_wamp_build_hello(char *buf, size_t cap, const char *realm, const char *details_json);

/** @brief GOODBYE: `[6, Details, "reason_uri"]`. */
size_t pc_wamp_build_goodbye(char *buf, size_t cap, const char *reason_uri, const char *details_json);

/** @brief SUBSCRIBE: `[32, Request, Options, "topic"]`. */
size_t pc_wamp_build_subscribe(char *buf, size_t cap, uint64_t request, const char *topic, const char *options_json);

/** @brief UNSUBSCRIBE: `[34, Request, SubscriptionId]`. */
size_t pc_wamp_build_unsubscribe(char *buf, size_t cap, uint64_t request, uint64_t subscription_id);

/** @brief PUBLISH: `[16, Request, Options, "topic"(, Arguments(, ArgumentsKw))]`. */
size_t pc_wamp_build_publish(char *buf, size_t cap, uint64_t request, const char *topic, const char *options_json,
                             const char *args_json, const char *kwargs_json);

/** @brief CALL: `[48, Request, Options, "procedure"(, Arguments(, ArgumentsKw))]`. */
size_t pc_wamp_build_call(char *buf, size_t cap, uint64_t request, const char *procedure, const char *options_json,
                          const char *args_json, const char *kwargs_json);

/** @brief REGISTER: `[64, Request, Options, "procedure"]`. */
size_t pc_wamp_build_register(char *buf, size_t cap, uint64_t request, const char *procedure, const char *options_json);

/** @brief UNREGISTER: `[66, Request, RegistrationId]` (the RPC-side sibling of UNSUBSCRIBE). */
size_t pc_wamp_build_unregister(char *buf, size_t cap, uint64_t request, uint64_t registration_id);

/** @brief YIELD: `[70, Request, Options(, Arguments(, ArgumentsKw))]` (replies to an INVOCATION). */
size_t pc_wamp_build_yield(char *buf, size_t cap, uint64_t request, const char *options_json, const char *args_json,
                           const char *kwargs_json);

// ---- parser (positional access over an inbound JSON-array message) ----

/** @brief Slice the raw text of the top-level array element at @p index (into @p msg). */
proto_bool pc_wamp_element(const char *msg, size_t index, const char **start, size_t *len);

/** @brief Read the message type (element 0) as an integer. */
proto_bool pc_wamp_get_type(const char *msg, int *out);

/** @brief Read the unsigned integer (e.g. a Request / Subscription / Publication id) at @p index. */
proto_bool pc_wamp_get_uint(const char *msg, size_t index, uint64_t *out);

/**
 * @brief Copy the URI/string element at @p index (surrounding quotes stripped) into @p out.
 * @note No unescaping - WAMP URIs are restricted to unescaped characters. Bounded by @p out_cap.
 */
proto_bool pc_wamp_get_uri(const char *msg, size_t index, char *out, size_t out_cap);

#endif // PC_ENABLE_WAMP

#endif // PROTOCORE_WAMP_H
