// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coaps.h
 * @brief CoAP over DTLS (RFC 7252 sec 9): the bridge between one DTLS connection and the CoAP server.
 *
 * RFC 7252 sec 9.1 binds CoAP to DTLS, and sec 6.2 gives that binding the "coaps" URI scheme; sec
 * 12.7 registers its port, 5684. This is the transport-neutral half: it drives one @ref DtlsConn
 * through its handshake and, once the connection is established, opens each protected application
 * record, answers the CoAP message inside it through @ref Coap, and seals the response back into one
 * record. The socket and the per-peer routing sit above it in coaps_server.h, so nothing here binds a
 * port and the whole path is host-testable against an in-test DTLS client.
 *
 * The record layer is RFC 9147 (DTLS 1.3). Its sec 4 Figure 3 gives the DTLSCiphertext unified
 * header: the three high bits of the first byte are 001, the C bit (0x10) marks a Connection ID, and
 * the two low bits (0x03) carry the low-order bits of the epoch. Epoch 3 is application data;
 * anything else is a handshake record and goes back to the state machine, which is what re-answers a
 * retransmitted client Finished whose acknowledgement was lost (RFC 9147 sec 5.8.3).
 *
 * The module exports one symbol, @ref Coaps. Everything in coaps.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_COAPS_H
#define PROTOCORE_COAPS_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_DTLS && PROTOCORE_ENABLE_COAP

#include "network_drivers/presentation/security/dtls/dtls_conn.h" // DtlsConn: the connection a call drives

PROTOCORE_BEGIN_DECLS

/** @brief One inbound datagram and the buffer whatever it owes is written into. */
typedef struct
{
    const uint8_t *data; ///< the received datagram's octets
    size_t len;          ///< how many
    uint8_t *out;        ///< where the outbound datagram is written
    size_t out_cap;      ///< how much room that has
} CoapsBridgeArgs;

/** @brief The bridge's calls, described only in coaps.c. */
struct CoapsInternal;

/**
 * @brief CoAP carried over one DTLS connection.
 *
 * A caller sets the members a call takes, invokes it through ::Coaps, and reads the outcome off the
 * same handle.
 *
 * No storage member: the connection is the caller's @ref DtlsConn and the plaintext scratch lives for
 * one call, so the bridge holds nothing between calls.
 *
 * @var CoapsNs::conn      the DTLS connection every call acts on
 * @var CoapsNs::dgram     the datagram a call reads and the buffer it writes
 * @var CoapsNs::i32       octets written to @c dgram.out, 0 when there is nothing to send, or -1 when
 *                         the handshake failed and @c conn is FAILED
 * @var CoapsNs::process   turn one received datagram: drive the handshake, or answer the CoAP message
 *                         inside an epoch-3 application record and seal the response
 * @var CoapsNs::internal  the calls that reach the connection
 */
typedef struct
{
    DtlsConn *conn; ///< the connection every call names

    CoapsBridgeArgs dgram; ///< what turning one datagram takes

    int32_t i32;

    void (*process)(struct CoapsInternal *ctx);

    struct CoapsInternal *internal;
} CoapsNs;

/** @brief The one symbol this module exports. */
extern CoapsNs Coaps;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DTLS && PROTOCORE_ENABLE_COAP

#endif // PROTOCORE_COAPS_H
