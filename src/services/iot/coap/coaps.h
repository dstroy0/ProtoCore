// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coaps.h
 * @brief CoAP over DTLS (CoAPs, RFC 7252 §9) - the bridge between the DTLS 1.3 server and the CoAP
 *        request handler.
 *
 * CoAP secured with DTLS is the standard way to run CoAP over the open Internet (coaps://, UDP port
 * 5684). This is the transport-neutral glue: it drives one @ref DtlsConn through its handshake and,
 * once established, unwraps each encrypted application record, hands the CoAP request to
 * pc_coap_server_process(), and re-wraps the response. The socket / per-peer routing lives in a thin
 * front-end (pc_dtls_server) on top; this file has no sockets, so it is host-testable with an in-test
 * DTLS client, exactly like pc_dtls_conn itself.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_COAPS_H
#define PROTOCORE_COAPS_H

#include "protocore_config.h"

#if PC_ENABLE_DTLS && PC_ENABLE_COAP

#include "network_drivers/presentation/security/dtls/dtls_conn.h"

/**
 * @brief Process one inbound DTLS datagram for a CoAP-over-DTLS connection @p c.
 *
 * While the handshake is in progress the datagram is driven through @ref pc_dtls_conn_process. Once the
 * connection is established, an epoch-3 application record is decrypted, its CoAP request is answered
 * by pc_coap_server_process(), and the response is sealed as an epoch-3 record. A handshake record that
 * arrives after establishment (a retransmitted client Finished whose ACK was lost) is routed back to
 * the state machine so it is re-acknowledged (RFC 9147 §5.8.3).
 *
 * @return bytes written to @p out (0 if there is nothing to send), or -1 on a fatal handshake error
 *         (then @p c is FAILED and @ref pc_dtls_conn_alert gives the reason).
 */
int pc_coaps_process(DtlsConn *c, const uint8_t *dgram, size_t len, uint8_t *out, size_t out_cap);

#endif // PC_ENABLE_DTLS && PC_ENABLE_COAP
#endif // PROTOCORE_COAPS_H
