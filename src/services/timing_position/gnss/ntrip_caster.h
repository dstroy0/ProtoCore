// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_ntrip_caster.h
 * @brief NTRIP caster protocol codec (PC_ENABLE_NTRIP_CASTER) - the pure, host-tested core.
 *
 * NTRIP (Networked Transport of RTCM via Internet Protocol) is how a GNSS base's RTCM corrections reach
 * rovers over TCP. It is HTTP-shaped: a rover opens a connection and sends a request line
 * `GET /<mountpoint> HTTP/1.x` with headers; the caster answers and then streams raw RTCM bytes.
 *
 * Two protocol revisions are in the field and this codec speaks both:
 *  - **NTRIP 1.0** - the request omits a version header; the caster replies with the bare status line
 *    `ICY 200 OK\r\n\r\n` and then streams, or `SOURCETABLE 200 OK` + the source table for a `GET /`.
 *  - **NTRIP 2.0** - the request carries `Ntrip-Version: Ntrip/2.0`; the caster replies with a real
 *    `HTTP/1.1 200 OK` and `Content-Type: gnss/data` (or `gnss/sourcetable`) before streaming.
 *
 * This file parses a rover request (mountpoint, version, optional HTTP Basic credentials) and builds the
 * caster's responses - the stream-accept line, an error line, and the RTCM source table (one `STR;...`
 * record per mountpoint per the NTRIP source-table format, terminated by `ENDSOURCETABLE`). It touches no
 * sockets; the listener glue (pc_ntrip_caster_listener.h) drives it and pumps bytes. Zero heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NTRIP_CASTER_H
#define PROTOCORE_NTRIP_CASTER_H

#include "protocore_config.h"

#if PC_ENABLE_NTRIP_CASTER

/** @brief NTRIP protocol revision detected in / used for a request or response. */
typedef enum PROTO_ENUM_PACKED
{
    NTRIP_V1 = 1, ///< legacy: ICY 200 OK / SOURCETABLE 200 OK
    NTRIP_V2 = 2, ///< RFC-style: HTTP/1.1 200 OK, Content-Type: gnss/data
} NtripVersion;

/** @brief A parsed NTRIP rover request. String spans point into the caller's request buffer. */
typedef struct
{
    proto_bool complete;                 ///< the full request header block (up to a blank line) was present
    proto_bool is_get;                   ///< the request line was a GET
    NtripVersion version;                ///< NTRIP_V2 if an Ntrip-Version: Ntrip/2.0 header was present, else NTRIP_V1
    char mountpoint[PC_NTRIP_MOUNT_MAX]; ///< requested mountpoint (empty = source-table request, "GET /")
    proto_bool want_sourcetable;         ///< the request targets "/" (list the source table)
    const char *auth_b64;                ///< base64 of user:pass from an "Authorization: Basic" header, or null
    uint16_t auth_b64_len;               ///< length of @c auth_b64 (0 if none)
} NtripRequest;

/**
 * @brief Parse an NTRIP request from the bytes buffered so far.
 *
 * @return true once the request header block is complete (a `\r\n\r\n` or `\n\n` was seen) and @p out is
 *         filled; false if more bytes are still needed. A completed request with @c is_get false is a
 *         malformed / unsupported request the caller should reject.
 */
proto_bool pc_ntrip_request_parse(const char *buf, size_t len, NtripRequest *out);

/**
 * @brief Build the stream-accept response the caster sends before streaming RTCM to a rover.
 * @return bytes written (excluding any NUL), or 0 on overflow. V1 = "ICY 200 OK\r\n\r\n";
 *         V2 = an HTTP/1.1 200 response with Content-Type: gnss/data.
 */
size_t pc_ntrip_build_stream_response(char *out, size_t cap, NtripVersion version);

/**
 * @brief Build an error response for an unknown mountpoint / bad request.
 * @return bytes written, or 0 on overflow. V1 = a bare "SOURCETABLE 200 OK" fallback is NOT used here;
 *         this emits a 404-style line ("HTTP/1.1 404 Not Found" for V2, "ERROR - Bad Request" for V1).
 */
size_t pc_ntrip_build_error_response(char *out, size_t cap, NtripVersion version);

/**
 * @brief Build an unauthorized response for a mountpoint that requires (and did not get valid) HTTP
 *        Basic credentials. V2 = "HTTP/1.1 401 Unauthorized" with a WWW-Authenticate: Basic challenge;
 *        V1 = "ERROR - Bad Password". @return bytes written, or 0 on overflow.
 */
size_t pc_ntrip_build_unauthorized_response(char *out, size_t cap, NtripVersion version);

/** @brief One mountpoint's source-table (`STR;...`) description. Unset string fields default sensibly. */
typedef struct
{
    const char *mountpoint;     ///< e.g. "BASE1" (required)
    const char *identifier;     ///< source / place identifier, e.g. "Lab roof"
    const char *format_details; ///< RTCM message list, e.g. "1005(1),1006(10)"
    const char *nav_system;     ///< e.g. "GPS" or "GPS+GLO"
    const char *country;        ///< 3-char code, e.g. "USA"
    const char *generator;      ///< producing hardware/software (null -> "PC")
    double lat_deg;             ///< approximate base latitude (source-table advertises 2 decimals)
    double lon_deg;             ///< approximate base longitude
    proto_bool nmea_required;   ///< rover must send a GGA (1) or not (0); false for a single-base caster
} NtripMount;

/**
 * @brief Build one NTRIP source-table `STR;...` record (no trailing CRLF) for @p m into @p out.
 * @return bytes written (excluding NUL), or 0 on overflow.
 */
size_t pc_ntrip_build_str_record(char *out, size_t cap, const NtripMount *m);

/**
 * @brief Build a full source-table response: the status/header block, one `STR;...\r\n` per mountpoint,
 *        then `ENDSOURCETABLE\r\n`. The Content-Length (V2) / body length (V1) is computed for you.
 * @return total bytes written (excluding NUL), or 0 on overflow.
 */
size_t pc_ntrip_build_sourcetable(char *out, size_t cap, NtripVersion version, const NtripMount *mounts,
                                  size_t mount_count);

#endif // PC_ENABLE_NTRIP_CASTER

#endif // PROTOCORE_NTRIP_CASTER_H
