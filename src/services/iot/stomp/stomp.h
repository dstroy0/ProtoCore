// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file stomp.h
 * @brief The STOMP 1.2 frame codec: the sec 9 grammar, built and parsed over caller buffers.
 *
 * STOMP 1.2 is a community specification published at
 * https://stomp.github.io/stomp-specification-1.2.html. It is not an IETF document and carries no
 * RFC number; every citation here names that specification and its section.
 *
 * Sec 9 Augmented BNF gives the frame:
 *
 *     frame       = command EOL *( header EOL ) EOL *OCTET NULL *( EOL )
 *     header      = header-name ":" header-value
 *     header-name = 1*<any OCTET except CR or LF or ":">
 *     EOL         = [CR] LF
 *
 * Sec 4 states the octets: a command terminated by an EOL, "an OPTIONAL carriage return (octet 13)
 * followed by a REQUIRED line feed (octet 10)", then zero or more header entries in
 * `<key>:<value>` format, then "a blank line (i.e. an extra EOL)" ending the headers and beginning
 * the body. Sec 6 names the client commands, sec 7 the server commands.
 *
 * Sec 4.3.1: a content-length header "is an octet count for the length of the message body. If a
 * content-length header is included, this number of octets MUST be read, regardless of whether or
 * not there are NULL octets in the body. The frame still needs to be terminated with a NULL
 * octet." With no such header the body runs to the first NULL.
 *
 * Sec 4.1 Value Encoding escapes four octets inside a header-name or header-value: `\r` is CR,
 * `\n` is LF, `\c` is colon, `\\` is backslash. An undefined escape sequence "MUST be treated as a
 * fatal protocol error", so an unescape reports zero octets on one.
 *
 * Sec 4.4 Repeated Header Entries: "only the first header entry SHOULD be used as the value of
 * header entry" - the entry a lookup reports.
 *
 * Sec 5.4 Heart-beating: a sender with no frame to send "MUST send an end-of-line (EOL)", and sec 9
 * trails a frame with `*( EOL )`. A parse steps over those octets ahead of a command and counts
 * them in @c consumed.
 *
 * The parser copies nothing: the command, every header-name and header-value, and the body are
 * slices into the caller's buffer, and a header-value arrives still escaped. The builder escapes
 * every header-name and header-value it writes; sec 4.1 exempts the CONNECT and CONNECTED frames
 * from escaping for STOMP 1.0 compatibility.
 *
 * The module exports one symbol, @ref Stomp. Everything in stomp.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_STOMP_H
#define PROTOCORE_STOMP_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_STOMP

PROTOCORE_BEGIN_DECLS

/** @brief One header entry (sec 9 `header = header-name ":" header-value`), sliced from the source. */
typedef struct
{
    const char *name;  ///< the header-name octets, not NUL-terminated
    size_t name_len;   ///< how many
    const char *value; ///< the header-value octets, still escaped (sec 4.1)
    size_t value_len;  ///< how many
} StompHeader;

/** @brief One parsed frame (sec 9). Every pointer slices the source buffer; nothing is copied. */
typedef struct
{
    const char *command;                              ///< the command octets (sec 6, sec 7)
    size_t command_len;                               ///< how many
    StompHeader headers[PROTOCORE_STOMP_MAX_HEADERS]; ///< the header entries, in wire order
    size_t header_count;                              ///< how many, capped at PROTOCORE_STOMP_MAX_HEADERS
    const char *body;                                 ///< the body octets (sec 4.2)
    size_t body_len;                                  ///< how many
} StompFrame;

/** @brief The caller buffer a codec runs over. */
typedef struct
{
    char *out;      ///< where a build or an unescape writes its octets
    size_t cap;     ///< how many it holds
    const char *in; ///< the octets a parse or an unescape reads
    size_t len;     ///< how many
} StompBufArgs;

/** @brief The frame a build emits: its command, its header entries, and its body (sec 9). */
typedef struct
{
    const char *command;              ///< the command it writes (sec 6 client-command, sec 7 server-command)
    const char *const *header_names;  ///< NUL-terminated header-name strings, @c header_count of them
    const char *const *header_values; ///< NUL-terminated header-value strings, parallel to @c header_names
    size_t header_count;              ///< how many header entries
    const char *body;                 ///< the body octets (sec 4.2); NULL for an empty body
    size_t body_len;                  ///< how many
} StompBuildArgs;

/** @brief The header entry a lookup names (sec 4.4 takes the first entry with that name). */
typedef struct
{
    const char *name; ///< the header-name to match, NUL-terminated
} StompLookupArgs;

/** @brief The codec's calls, described only in stomp.c. */
struct StompInternal;

/**
 * @brief The STOMP 1.2 frame codec (stomp.github.io, not an IETF document).
 *
 * A caller points @c frame at its own ::StompFrame, sets the members a call takes, invokes it
 * through ::Stomp, and reads the outcome off the same handle.
 *
 * No storage member: both buffers and the frame are the caller's, and the codec holds nothing
 * between calls.
 *
 * @var StompNs::frame       the frame a parse fills and a lookup searches
 * @var StompNs::buf         the caller buffer a build writes and a parse reads
 * @var StompNs::build_args  the command, header entries and body a build emits
 * @var StompNs::lookup      the header-name a lookup matches
 * @var StompNs::ok          a call's true/false outcome
 * @var StompNs::n           octets a build wrote including the NULL, or octets an unescape decoded; 0 on failure
 * @var StompNs::consumed    octets the parsed frame occupied, its leading EOLs included
 * @var StompNs::value       the raw, still escaped header-value a lookup found
 * @var StompNs::value_len   how many
 * @var StompNs::build       write `command EOL *( header EOL ) EOL body NULL` into @c buf.out, every
 *                           header-name and header-value escaped (sec 4.1, sec 9)
 * @var StompNs::parse       take one frame from the head of @c buf.in into @c *frame, the body sized by
 *                           content-length when present (sec 4.3.1)
 * @var StompNs::header      find @c lookup.name among @c *frame header entries, first match wins (sec 4.4)
 * @var StompNs::unescape    decode the sec 4.1 escapes in @c buf.in into @c buf.out
 * @var StompNs::internal    the calls that reach the buffers
 */
typedef struct
{
    StompFrame *frame; ///< the frame a parse fills and a lookup searches

    StompBufArgs buf;          ///< the caller buffer a codec runs over
    StompBuildArgs build_args; ///< what a build emits
    StompLookupArgs lookup;    ///< what a lookup matches

    proto_bool ok;
    size_t n;
    size_t consumed;
    const char *value;
    size_t value_len;

    void (*build)(struct StompInternal *ctx);
    void (*parse)(struct StompInternal *ctx);
    void (*header)(struct StompInternal *ctx);
    void (*unescape)(struct StompInternal *ctx);

    struct StompInternal *internal;
} StompNs;

/** @brief The one symbol this module exports. */
extern StompNs Stomp;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_STOMP

#endif // PROTOCORE_STOMP_H
