// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coaps.c
 * @brief CoAP over DTLS (CoAPs, RFC 7252 §9). See coaps.h.
 */

#include "services/iot/coap/coaps.h"

#if PC_ENABLE_DTLS && PC_ENABLE_COAP

#include "services/iot/coap/coap.h"

// Largest CoAP request/response carried in one DTLS application record. CoAP messages are expected to
// fit a single datagram (RFC 7252 §4.6); anything larger is dropped rather than fragmented here.
#define PC_COAPS_MSG_CAP 1152

int pc_coaps_process(DtlsConn *c, const uint8_t *dgram, size_t len, uint8_t *out, size_t out_cap)
{
    if (!pc_dtls_conn_established(c))
    {
        return pc_dtls_conn_process(c, dgram, len, out, out_cap); // still handshaking (or -1 on fatal error)
    }

    // Established. A DTLSCiphertext unified header is 0b001CSLEE; the low two bits are the epoch mod 4,
    // so epoch 3 (application data) is 0b001xxx11. HttpRoute application data through CoAP; route anything
    // else (a retransmitted epoch-2 client Finished) back to the state machine to be re-acknowledged.
    if (len >= 1 && (dgram[0] & 0xE0) == 0x20 && (dgram[0] & 0x03) == 3)
    {
        uint8_t req[PC_COAPS_MSG_CAP];
        size_t req_len = 0;
        if (!pc_dtls_conn_open_app(c, dgram, len, req, sizeof(req), &req_len))
        {
            return 0; // replay, truncated, or not application data
        }
        uint8_t resp[PC_COAPS_MSG_CAP];
        size_t pc_resp_len = pc_coap_server_process(req, req_len, resp, sizeof(resp));
        if (!pc_resp_len)
        {
            return 0; // no response (e.g. a Non-confirmable message with no resource match)
        }
        return (int)pc_dtls_conn_seal_app(c, resp, pc_resp_len, out, out_cap);
    }
    return pc_dtls_conn_process(c, dgram, len, out, out_cap);
}

#endif // PC_ENABLE_DTLS && PC_ENABLE_COAP
