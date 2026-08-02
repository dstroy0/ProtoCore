// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_h3_conn.h
 * @brief HTTP/3 application engine over QUIC streams (RFC 9114).
 *
 * Sits on top of the QUIC transport engine (pc_quic_conn) and speaks HTTP/3: on the handshake
 * completing it opens the server's unidirectional control stream (sending SETTINGS) and the two
 * QPACK encoder / decoder streams, reads the client's control stream, and on each client-initiated
 * bidirectional request stream reassembles the HTTP/3 frames - QPACK-decoding the HEADERS field
 * section into a request (method / path / authority + a small header set) and collecting the DATA
 * body - then hands the finished request to the application through a callback. pc_h3_conn_respond()
 * serializes a response (HEADERS + DATA) back onto the request stream and finishes it.
 *
 * QPACK is static-table only (we advertise QPACK_MAX_TABLE_CAPACITY = 0), so no dynamic-table state
 * is needed and the encoder / decoder streams carry only their stream-type byte. Fixed storage, no
 * heap; host-testable by feeding it decoded stream bytes through the pc_quic_conn callback seam.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_H3_CONN_H
#define PROTOCORE_H3_CONN_H

#include "protocore_config.h"

#if PC_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/h3_frame.h"
#include "network_drivers/presentation/http/http3/quic_conn.h"

#ifndef PC_H3_STREAM_BUF
#define PC_H3_STREAM_BUF 2048 ///< per-request-stream reassembly buffer (HEADERS + DATA)
#endif
#ifndef PC_H3_METHOD_LEN
#define PC_H3_METHOD_LEN 16 ///< captured :method length cap
#endif
#ifndef PC_H3_PATH_LEN
#define PC_H3_PATH_LEN 256 ///< captured :path length cap
#endif
#ifndef PC_H3_AUTHORITY_LEN
#define PC_H3_AUTHORITY_LEN 128 ///< captured :authority length cap
#endif

struct H3Conn;

/**
 * @brief A completed request delivered to the application.
 * @param body / body_len  the request body (may be empty); valid only during the call.
 */
typedef void (*H3RequestFn)(void *app, H3Conn *h3, uint64_t stream_id, const char *method, const char *path,
                            const char *authority, const uint8_t *body, size_t body_len);

/** @brief HTTP/3 stream roles (a mutually-exclusive internal role, not a wire value). */
typedef enum PROTO_ENUM_PACKED
{
    H3_ROLE_FREE = 0,
    H3_ROLE_REQUEST,   ///< client-initiated bidirectional request stream
    H3_ROLE_CONTROL,   ///< client control stream (type 0x00)
    H3_ROLE_QPACK_ENC, ///< client QPACK encoder stream (type 0x02)
    H3_ROLE_QPACK_DEC, ///< client QPACK decoder stream (type 0x03)
    H3_ROLE_OTHER_UNI, ///< an unknown unidirectional stream (drained/ignored)
} H3StreamRole;

/** @brief Per-stream HTTP/3 state. */
typedef struct
{
    uint64_t id;          ///< stream id (UINT64_MAX = free)
    H3StreamRole role;    ///< stream role
    proto_bool type_read; ///< a unidirectional stream's type varint has been consumed
    proto_bool responded; ///< a response has been sent on this request stream
    uint8_t buf[PC_H3_STREAM_BUF];
    size_t buf_len;
    char method[PC_H3_METHOD_LEN];
    char path[PC_H3_PATH_LEN];
    char authority[PC_H3_AUTHORITY_LEN];
    proto_bool have_headers; ///< a HEADERS frame has been decoded
    size_t body_off;         ///< where the accumulated body begins within buf (after the last HEADERS)
} H3Stream;

/** @brief One HTTP/3 connection (wraps a QuicConn). */
typedef struct H3Conn
{
    QuicConn *qc;
    H3RequestFn on_request;
    void *app;
    H3Settings peer_settings;
    proto_bool control_opened; ///< our control + QPACK streams have been opened
    uint64_t next_uni_id;      ///< next server-initiated unidirectional stream id (3, 7, 11, ...)
    H3Stream streams[PC_H3_MAX_STREAMS];
} H3Conn;

/**
 * @brief Initialize an HTTP/3 connection over @p qc.
 *
 * Registers the QUIC stream / handshake callbacks on @p qc (so @p qc->cb is overwritten with this
 * engine's hooks; pass the app callback here instead). @p on_request is invoked for each completed
 * request.
 */
void pc_h3_conn_init(H3Conn *h3, QuicConn *qc, H3RequestFn on_request, void *app);

/**
 * @brief Serialize and send a response on @p stream_id: a HEADERS field section (QPACK) with
 * :status and optional content-type / content-length, then a DATA frame with @p body, and finish
 * the stream. @return false on a bad stream or serialization overflow.
 */
proto_bool pc_h3_conn_respond(H3Conn *h3, uint64_t stream_id, int status, const char *content_type, const uint8_t *body,
                              size_t body_len);

#endif // PC_ENABLE_HTTP3
#endif // PROTOCORE_H3_CONN_H
