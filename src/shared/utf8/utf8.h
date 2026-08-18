// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file utf8.h
 * @brief Strict UTF-8 validation (RFC 3629), one shared copy.
 *
 * Several protocols must reject non-UTF-8 input: WebSocket TEXT frames
 * (RFC 6455 8.1, fail with close 1007) and MQTT strings (MQTT 1.5.3). Both use
 * this single validator rather than each rolling its own. Header-only inline,
 * like the other shared primitives - zero link cost when unused.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UTF8_H
#define PROTOCORE_UTF8_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

/** @brief The octet run a check walks. */
typedef struct
{
    const uint8_t *s; ///< the octets
    size_t n;         ///< how many
} Utf8Args;

/**
 * @brief UTF-8 well-formedness.
 *
 * @var Utf8Ns::args      the octet run a check walks
 * @var Utf8Ns::ok        the run is well-formed UTF-8
 * @var Utf8Ns::valid     walk the run and judge it
 *
 * Rejects overlong encodings, surrogate code points (U+D800..U+DFFF), values above U+10FFFF, bad
 * continuation bytes, and truncated multi-byte sequences.
 *
 * No storage member: the run is the caller's and nothing is held between calls.
 */
typedef struct
{
    Utf8Args args;

    proto_bool ok;

    void (*const valid)(uint8_t *restrict work);
} Utf8Ns;

/** @brief The one symbol this module exports. */
extern Utf8Ns Utf8;

#endif // PROTOCORE_UTF8_H
