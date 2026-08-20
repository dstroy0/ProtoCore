// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PROTOCORE_TCP_CLIENT_H
#define PROTOCORE_TCP_CLIENT_H

/**
 * @file client.h
 * @brief Layer 4 (Transport) - the active OPEN: the outbound client transport.
 *
 * RFC 9293 sec 3.9.1.1 gives OPEN an active/passive flag; this is the active side, the dialing
 * peer of the passive OPEN in server.h.
 *
 * A small fixed pool of outbound connections so the application's clients
 * (services/net/http_client, services/iot/mqtt, services/net/ws_client) no longer each own a
 * private raw stack at L7. As with the server transport, every raw stack call is marshaled into
 * the stack's own context (see lower.h), so the main-loop/worker task never races it. All storage
 * is static (no heap).
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

/** @brief RFC 9293 sec 3.9.1.1 active OPEN: where a connection is dialled, and how long it may take. */
typedef struct
{
    const char *host;    ///< the name it dials; read on every step until it resolves, so it must outlive the open
    uint16_t port;       ///< the port it dials
    uint32_t timeout_ms; ///< what the whole open, resolve included, is given
} TcpDialArgs;

/** @brief The bytes a send or a read moves. Nothing a dial reads. */
typedef struct
{
    const void *data; ///< bytes for a send
    size_t len;       ///< how many
    uint8_t *buf;     ///< where a read writes
    size_t cap;       ///< how much room it has
} TcpClientIoArgs;

/**
 * @brief The outbound side of TCP.
 *
 * A caller sets the members a call takes, invokes it through ::TcpClient, and reads the outcome off
 * the same handle. The slot pool itself is behind @ref internal.
 *
 * @var TcpClientNs::cid         the slot a call acts on
 * @var TcpClientNs::dial        what an active OPEN dials
 * @var TcpClientNs::io          the bytes a send or a read moves
 * @var TcpClientNs::ok          a call's true/false outcome
 * @var TcpClientNs::i32         the cid an open took, or < 0 when none is free
 * @var TcpClientNs::n           a byte count a call reports
 * @var TcpClientNs::open       take a slot and start resolving; cid >= 0, or < 0 when none is free
 * @var TcpClientNs::connected  step the open along, and report whether the handshake completed
 * @var TcpClientNs::is_closed  step the open along, and report a close, an error, or the timeout
 * @var TcpClientNs::send       queue wire bytes for transmission
 * @var TcpClientNs::available  wire bytes buffered and ready to read
 * @var TcpClientNs::read       drain buffered wire bytes
 * @var TcpClientNs::close      tear the connection down and free the slot
 *
 * open() returns before the connection exists. @ref host is read on every step until the name
 * resolves, so it has to outlive the open. The caller polls connected() until it is true, or
 * is_closed() is, and closes the slot on either failure.
 */
typedef struct
{
    int cid;            ///< the slot every call names
    TcpDialArgs dial;   ///< what an active OPEN dials (RFC 9293 sec 3.9.1.1)
    TcpClientIoArgs io; ///< the bytes a send or a read moves
    proto_bool ok;
    int32_t i32;
    size_t n;
} TcpClientVars;

/** @brief The operands and the outcome. */
extern TcpClientVars TcpClientV;

/** @brief The entries. */
typedef struct
{
    void (*const open)(uint8_t *restrict work);
    void (*const connected)(uint8_t *restrict work);
    void (*const is_closed)(uint8_t *restrict work);
    void (*const send)(uint8_t *restrict work);
    void (*const available)(uint8_t *restrict work);
    void (*const read)(uint8_t *restrict work);
    void (*const close)(uint8_t *restrict work);
} TcpClientNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in TcpClientV or a region of the borrow at a fixed offset.
void protocore_tcp_client_open(uint8_t *restrict work);
void protocore_tcp_client_connected(uint8_t *restrict work);
void protocore_tcp_client_is_closed(uint8_t *restrict work);
void protocore_tcp_client_send(uint8_t *restrict work);
void protocore_tcp_client_available(uint8_t *restrict work);
void protocore_tcp_client_read(uint8_t *restrict work);
void protocore_tcp_client_close(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `TcpClient.open(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const TcpClientNs TcpClient __attribute__((unused)) = {
    .open = protocore_tcp_client_open,
    .connected = protocore_tcp_client_connected,
    .is_closed = protocore_tcp_client_is_closed,
    .send = protocore_tcp_client_send,
    .available = protocore_tcp_client_available,
    .read = protocore_tcp_client_read,
    .close = protocore_tcp_client_close,
};

/**
 * @brief The PROTOCORE_TCP_CLIENT_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_tcp_client_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_TCP_CLIENT_H
