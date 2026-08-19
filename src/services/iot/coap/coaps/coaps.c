// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coaps.c
 * @brief CoAP over DTLS (RFC 7252 sec 9): the bridge and its one owner. See coaps.h.
 */

#include "services/iot/coap/coaps/coaps.h"

#if PROTOCORE_ENABLE_DTLS && PROTOCORE_ENABLE_COAP

#include "services/iot/coap/coap/coap.h" // Coap.process: the CoAP message inside an application record

// The largest CoAP message carried in one DTLS application record. RFC 7252 sec 4.6 puts a good
// upper bound at 1152 octets for the message where nothing is known about the headers, so a larger
// record is dropped rather than fragmented here.
#define PROTOCORE_COAPS_MSG_CAP 1152

// RFC 9147 sec 4 Figure 3, the DTLSCiphertext unified header's first byte.
#define COAPS_UHDR_FIXED_MASK 0xE0u ///< the three high bits
#define COAPS_UHDR_FIXED 0x20u      ///< which are set to 001
#define COAPS_UHDR_EPOCH_MASK 0x03u ///< the two low bits, the low-order bits of the epoch
#define COAPS_EPOCH_APP 3u          ///< the epoch application data travels in

// Turn one datagram for the connection in ns->conn.
static void coaps_process(uint8_t *restrict work)
{
    (void)work;
    DtlsConn *c = Coaps.conn;
    const uint8_t *dgram = Coaps.dgram.data;
    const size_t len = Coaps.dgram.len;
    uint8_t *out = Coaps.dgram.out;
    const size_t out_cap = Coaps.dgram.out_cap;

    Coaps.i32 = 0;
    if (!c || !dgram || !out)
    {
        return;
    }

    if (!DtlsServer.established(c))
    {
        Coaps.i32 = DtlsServer.process(c, dgram, len, out, out_cap); // still handshaking, or -1 fatal
        return;
    }

    // Established. Application data is an epoch-3 DTLSCiphertext record (RFC 9147 sec 4); anything
    // else is a handshake record and goes back to the state machine to be re-acknowledged.
    if (len >= 1 && (dgram[0] & COAPS_UHDR_FIXED_MASK) == COAPS_UHDR_FIXED &&
        (dgram[0] & COAPS_UHDR_EPOCH_MASK) == COAPS_EPOCH_APP)
    {
        uint8_t req[PROTOCORE_COAPS_MSG_CAP];
        size_t req_len = 0;
        if (!DtlsServer.open_app(c, dgram, len, req, sizeof(req), &req_len))
        {
            return; // replayed, truncated, or not application data
        }
        uint8_t resp[PROTOCORE_COAPS_MSG_CAP];
        Coap.msg.req = req;
        Coap.msg.req_len = req_len;
        Coap.msg.resp = resp;
        Coap.msg.resp_cap = sizeof(resp);
        Coap.process(protocore_coap_span());
        if (Coap.n == 0)
        {
            return; // nothing to send, as for a Non-confirmable message the server does not answer
        }
        Coaps.i32 = (int32_t)DtlsServer.seal_app(c, resp, Coap.n, out, out_cap);
        return;
    }
    Coaps.i32 = DtlsServer.process(c, dgram, len, out, out_cap);
}

// Designated, so a member's position in the struct does not decide what it binds to.
CoapsNs Coaps = {.process = coaps_process};

#endif // PROTOCORE_ENABLE_DTLS && PROTOCORE_ENABLE_COAP
