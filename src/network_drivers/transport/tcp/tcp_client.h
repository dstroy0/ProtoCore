// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PROTOCORE_TCP_CLIENT_H
#define PROTOCORE_TCP_CLIENT_H

/**
 * @file client.h
 * @brief Layer 4 outbound TCP client transport - the client-side peer of the
 *        (server) transport in tcp.cpp.
 *
 * A small fixed pool of outbound connections so the application's clients
 * (services/net/http_client, services/iot/mqtt, services/net/ws_client) no longer each own a
 * private raw-lwIP TCP stack at L7. As with the server transport, every raw
 * `tcp_*()` call is marshaled into `tcpip_thread` via `tcpip_api_call()`, so the
 * main-loop/worker task never races the stack. All storage is static (no heap).
 *
 * The receive ring carries **wire bytes**: for a plaintext connection those are
 * the application bytes; for a TLS connection they are ciphertext and the caller
 * layers the shared client-TLS session (`protocore_tls_client_session_*`) on top, pointing its
 * BIO at protocore_client_send() / protocore_client_read().
 *
 * Nothing here blocks. open() takes a slot, starts the resolve and returns a cid straight away; the
 * slot carries its own timer and steps from resolving to connected each time the caller asks
 * connected() or is_closed(). The caller drives that from its own loop, and the whole open - the
 * name lookup included - is bounded by the timeout_ms it passed.
 */

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief The outbound side of TCP.
 *
 * @var TcpClientNs::open       take a slot and start resolving; cid >= 0, or < 0 when none is free
 * @var TcpClientNs::connected  step the open along, and report whether the handshake completed
 * @var TcpClientNs::is_closed  step the open along, and report a close, an error, or the timeout
 * @var TcpClientNs::send       queue wire bytes for transmission
 * @var TcpClientNs::available  wire bytes buffered and ready to read
 * @var TcpClientNs::read       drain buffered wire bytes
 * @var TcpClientNs::close      tear the connection down and free the slot
 *
 * open() returns before the connection exists. @p host is read on every step until the name
 * resolves, so it has to outlive the open. The caller polls connected() until it is true, or
 * is_closed() is, and closes the slot on either failure.
 */
typedef struct
{
    int (*open)(const char *host, uint16_t port, uint32_t timeout_ms);
    proto_bool (*connected)(int cid);
    proto_bool (*is_closed)(int cid);
    proto_bool (*send)(int cid, const void *data, size_t len);
    size_t (*available)(int cid);
    size_t (*read)(int cid, uint8_t *buf, size_t cap);
    void (*close)(int cid);
} TcpClientNs;

/** @brief The one symbol this module exports. */
extern const TcpClientNs TcpClient;

PROTOCORE_END_DECLS

#endif // PROTOCORE_TCP_CLIENT_H
