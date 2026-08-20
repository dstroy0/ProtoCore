// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file file_serving.h
 * @brief Layer 7 static file responses: open a path on a mount and page it out over a slot.
 *
 * serve_file_internal() opens the file and writes the response head; file_send_pump() writes one
 * send-buffer window per call until the file is drained, so a response larger than the buffer is
 * spread across dispatches instead of held in one. protocore_file_holds_slot() reports whether a slot is
 * mid-paging.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FILE_SERVING_H
#define PROTOCORE_FILE_SERVING_H

#include "network_drivers/presentation/http/http_parser/http_parser.h" // the complete type a public struct below holds by value
#include "network_drivers/presentation/http/route/http_route.h" // the complete type a public struct below holds by value
#include "protocore_config.h"                                   // the entry point: protocore_types.h for the widths
#include "server/storage/mnt/mnt.h" // the complete type a public struct below holds by value

#if PROTOCORE_ENABLE_FILE_SERVING

PROTOCORE_BEGIN_DECLS

// PROTOCORE_FILE_SERVING_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#include "network_drivers/presentation/http/http_parser/http_parser.h" // HttpReq: the type a parameter points at

#include "network_drivers/presentation/http/route/http_route.h" // HttpRoute: the type a parameter points at

#include "server/storage/mnt/mnt.h" // protocore_mnt_backend: the type a parameter points at

/** @brief What http_rfc1123 takes: epoch, out, cap. */
typedef struct
{
    int64_t epoch;
    char *out;
    size_t cap;
} FileServingHttpRfc1123Args;
/** @brief What serve_static_request takes: slot_id, req, r. */
typedef struct
{
    uint8_t slot_id;
    HttpReq *req;
    const HttpRoute *r;
} FileServingServeStaticRequestArgs;
/** @brief What serve_file_internal takes: slot_id, head, file_sys, ... */
typedef struct
{
    uint8_t slot_id;
    proto_bool head;
    const protocore_mnt_backend *file_sys;
    const char *fs_path;
    const char *content_type;
    const char *content_encoding;
} FileServingServeFileInternalArgs;
/** @brief What file_send_pump takes: slot_id. */
typedef struct
{
    uint8_t slot_id;
} FileServingFileSendPumpArgs;
/** @brief What holds_slot takes: slot. */
typedef struct
{
    uint8_t slot;
} FileServingHoldsSlotArgs;
/** @brief What serve_file takes: slot_id, file_sys, fs_path, ... */
typedef struct
{
    uint8_t slot_id;                       ///< Connection slot index
    const protocore_mnt_backend *file_sys; ///< Backend to read from; NULL uses whatever is mounted (the board's)
    const char *fs_path;                   ///< Request path to the file, resolved against the mount root
    const char *content_type;              ///< MIME type string, e.g. "text/html"
} FileServingServeFileArgs;
/** @brief What serve_static takes: url_prefix, file_sys, fs_root. */
typedef struct
{
    const char *url_prefix;                ///< URL prefix to mount (with or without a trailing `*`)
    const protocore_mnt_backend *file_sys; ///< Backend to serve from; NULL uses whatever is mounted (the board's)
    const char *fs_root;                   ///< Subtree on that backend (persistent string)
} FileServingServeStaticArgs;
/**
 * @brief Layer 7 static file responses: open a path on a mount and page it out over a slot. serve_file_internal() opens
 * the file and writes the response head; file_send_pump() writes one send-buffer window per call until the file is
 * drained, so a response larger than the buffer is spread across dispatches instead of held in one.
 * protocore_file_holds_slot() reports whether a slot is mid-paging.
 *
 * A caller sets the members a call takes, invokes it through ::FileServing with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   FileServing.http_rfc1123_args.epoch = ...;
 *   FileServing.http_rfc1123_args.out = ...;
 *   FileServing.http_rfc1123_args.cap = ...;
 *   FileServing.http_rfc1123(work);
 *
 * @var FileServingNs::http_rfc1123_args  what http_rfc1123 takes: epoch, out, cap
 * @var FileServingNs::serve_static_request_args  what serve_static_request takes: slot_id, req, r
 * @var FileServingNs::serve_file_internal_args  what serve_file_internal takes: slot_id, head, file_sys,
 * @var FileServingNs::file_send_pump_args  what file_send_pump takes: slot_id
 * @var FileServingNs::holds_slot_args  what holds_slot takes: slot
 * @var FileServingNs::serve_file_args  what serve_file takes: slot_id, file_sys, fs_path,
 * @var FileServingNs::serve_static_args  what serve_static takes: url_prefix, file_sys, fs_root
 * @var FileServingNs::ok  a call's true/false outcome
 * @var FileServingNs::http_rfc1123  format epoch as an RFC 1123 GMT date into out (cap bytes); out is ...
 * @var FileServingNs::serve_static_request  dispatch a ROUTE_STATIC match: resolve the FS path and serve it ...
 * @var FileServingNs::serve_file_internal  open fs_path on file_sys and stream it as 200 with the given type ...
 * @var FileServingNs::file_send_pump  resume a pending file response: page out one send-buffer window, ...
 * @var FileServingNs::holds_slot  true while a file response is paging out on slot
 * @var FileServingNs::serve_file  serve a file from the mounted volume. Opens fs_path through the ...
 * @var FileServingNs::serve_static  mount a filesystem subtree at a URL prefix (one-call static ...
 *
 * @c work is PROTOCORE_FILE_SERVING_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    FileServingHttpRfc1123Args http_rfc1123_args;
    FileServingServeStaticRequestArgs serve_static_request_args;
    FileServingServeFileInternalArgs serve_file_internal_args;
    FileServingFileSendPumpArgs file_send_pump_args;
    FileServingHoldsSlotArgs holds_slot_args;
    FileServingServeFileArgs serve_file_args;
    FileServingServeStaticArgs serve_static_args;
    proto_bool ok;
} FileServingVars;

/** @brief The operands and the outcome. */
extern FileServingVars FileServingV;

/** @brief The entries. */
typedef struct
{
    void (*const http_rfc1123)(uint8_t *restrict work);
    void (*const serve_static_request)(uint8_t *restrict work);
    void (*const serve_file_internal)(uint8_t *restrict work);
    void (*const file_send_pump)(uint8_t *restrict work);
    void (*const holds_slot)(uint8_t *restrict work);
    void (*const serve_file)(uint8_t *restrict work);
    void (*const serve_static)(uint8_t *restrict work);
} FileServingNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in FileServingV or a region of the borrow at a fixed offset.
void protocore_file_serving_http_rfc1123(uint8_t *restrict work);
void protocore_file_serving_serve_static_request(uint8_t *restrict work);
void protocore_file_serving_serve_file_internal(uint8_t *restrict work);
void protocore_file_serving_file_send_pump(uint8_t *restrict work);
void protocore_file_serving_holds_slot(uint8_t *restrict work);
void protocore_file_serving_serve_file(uint8_t *restrict work);
void protocore_file_serving_serve_static(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `FileServing.http_rfc1123(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const FileServingNs FileServing __attribute__((unused)) = {
    .http_rfc1123 = protocore_file_serving_http_rfc1123,
    .serve_static_request = protocore_file_serving_serve_static_request,
    .serve_file_internal = protocore_file_serving_serve_file_internal,
    .file_send_pump = protocore_file_serving_file_send_pump,
    .holds_slot = protocore_file_serving_holds_slot,
    .serve_file = protocore_file_serving_serve_file,
    .serve_static = protocore_file_serving_serve_static,
};

/**
 * @brief The PROTOCORE_FILE_SERVING_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_file_serving_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FILE_SERVING

#endif // PROTOCORE_FILE_SERVING_H
