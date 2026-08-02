// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cotp.h
 * @brief TPKT (RFC 1006) + COTP / ISO 8073 X.224 class-0 frame codec (PC_ENABLE_COTP) -
 *        zero-heap "ISO transport on TCP" framing, the reusable foundation under S7comm and
 *        IEC 61850 MMS.
 *
 * Two stacked layers over TCP:
 *  - TPKT (RFC 1006): a 4-octet envelope - version(1)=3, reserved(1)=0, length(2,
 *    big-endian, the whole packet including this header) - then an X.224 TPDU.
 *  - COTP / X.224 class 0: a Data TPDU is `LI(1) 0xF0 (EOT|TPDU-NR)` then the user data,
 *    where LI is the count of header octets after itself. Connection Request / Confirm use
 *    codes 0xE0 / 0xD0 and carry a destination ref, a source ref, a class octet, and
 *    variable parameters (e.g. the TPDU-size parameter 0xC0).
 *
 * The builders frame a payload into a caller buffer (fail-closed); the parsers validate and
 * report the slices. TPKT/X.224 layout verified against RFC 1006 / ISO 8073.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_COTP_H
#define PROTOCORE_COTP_H

#include "protocore_config.h"

#if PC_ENABLE_COTP

#define TPKT_VERSION 0x03  ///< RFC 1006 TPKT version (always 3)
#define TPKT_HEADER_SIZE 4 ///< version + reserved + 2-octet length

// X.224 TPDU type codes (the high nibble of the code octet; the low nibble is the CDT /
// credit, which is 0 for class 0).
#define COTP_DT 0xF0 ///< Data
#define COTP_CR 0xE0 ///< Connection Request
#define COTP_CC 0xD0 ///< Connection Confirm
#define COTP_DR 0x80 ///< Disconnect Request
#define COTP_DC 0xC0 ///< Disconnect Confirm
#define COTP_ER 0x70 ///< TPDU Error

#define COTP_EOT 0x80             ///< end-of-TSDU bit in the DT TPDU-NR octet
#define COTP_PARAM_TPDU_SIZE 0xC0 ///< variable-parameter code: TPDU size (value = size exponent)
#define COTP_DT_HEADER_LEN 3      ///< DT TPDU header octets: LI + code + (EOT|NR)

// ---- TPKT ----

/** @brief Wrap @p payload in a TPKT envelope. Returns total octets, or 0 on overflow. */
size_t pc_tpkt_build(uint8_t *buf, size_t cap, const uint8_t *payload, size_t payload_len);

/**
 * @brief Parse a TPKT envelope; reports the X.224 payload slice and bytes consumed.
 * @return true on a complete, version-3 packet; false on bad version / truncation.
 */
proto_bool pc_tpkt_parse(const uint8_t *buf, size_t len, const uint8_t **payload, size_t *payload_len,
                         size_t *consumed);

// ---- COTP (X.224 class 0) ----

/** @brief Build a COTP Data TPDU around @p data: `LI=2, 0xF0, (EOT|0)` + data. */
size_t pc_cotp_build_dt(uint8_t *buf, size_t cap, const uint8_t *data, size_t data_len, proto_bool eot);

/**
 * @brief Build a COTP Connection Request: `LI 0xE0 dst-ref(0) src-ref class(0)` + a TPDU-size
 *        parameter + any @p extra_params (e.g. the S7 src/dst TSAP parameters).
 * @param tpdu_size_code the TPDU-size exponent (e.g. 0x0A = 1024).
 */
size_t pc_cotp_build_cr(uint8_t *buf, size_t cap, uint16_t src_ref, uint8_t tpdu_size_code, const uint8_t *extra_params,
                        size_t extra_len);

/**
 * @brief Build a COTP Connection Confirm (the server's response to a CR): `LI 0xD0 dst-ref src-ref
 *        class(0)` + a TPDU-size parameter + any @p extra_params.
 * @param dst_ref        the connecting peer's source reference, echoed back as the destination reference.
 * @param src_ref        this end's source reference.
 * @param tpdu_size_code the negotiated TPDU-size exponent (e.g. 0x0A = 1024).
 */
size_t pc_cotp_build_cc(uint8_t *buf, size_t cap, uint16_t dst_ref, uint16_t src_ref, uint8_t tpdu_size_code,
                        const uint8_t *extra_params, size_t extra_len);

/** @brief A parsed COTP header. For DT, @ref data is the user data; for CR/CC, the refs. */
typedef struct
{
    uint8_t code;        ///< TPDU type (high nibble): COTP_DT / COTP_CR / ...
    uint16_t dst_ref;    ///< CR / CC destination reference
    uint16_t src_ref;    ///< CR / CC source reference
    proto_bool eot;      ///< DT end-of-TSDU flag
    const uint8_t *data; ///< DT user data (points INTO the source buffer)
    size_t data_len;
} CotpHeader;

/** @brief Parse a COTP TPDU (typically the TPKT payload). */
proto_bool pc_cotp_parse(const uint8_t *buf, size_t len, CotpHeader *out);

#endif // PC_ENABLE_COTP

#endif // PROTOCORE_COTP_H
