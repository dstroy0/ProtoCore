// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cotp.h
 * @brief TPKT (RFC 1006) + COTP / ISO 8073 X.224 class-0 frame codec (PROTOCORE_ENABLE_COTP) -
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_COTP

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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

/** @brief What tpkt_build takes: buf, cap, payload, payload_len. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const uint8_t *payload;
    size_t payload_len;
} CotpTpktBuildArgs;

/** @brief What tpkt_parse takes: buf, len, payload, payload_len, ... */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    const uint8_t **payload;
    size_t *payload_len;
    size_t *consumed;
} CotpTpktParseArgs;

/** @brief What build_dt takes: buf, cap, data, data_len, eot. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const uint8_t *data;
    size_t data_len;
    proto_bool eot;
} CotpBuildDtArgs;

/** @brief What build_cr takes: buf, cap, src_ref, tpdu_size_code, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t src_ref;
    uint8_t tpdu_size_code; ///< the TPDU-size exponent (e.g. 0x0A = 1024)
    const uint8_t *extra_params;
    size_t extra_len;
} CotpBuildCrArgs;

/** @brief What build_cc takes: buf, cap, dst_ref, src_ref, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t dst_ref;       ///< the connecting peer's source reference, echoed back as the destination reference
    uint16_t src_ref;       ///< this end's source reference
    uint8_t tpdu_size_code; ///< the negotiated TPDU-size exponent (e.g. 0x0A = 1024)
    const uint8_t *extra_params;
    size_t extra_len;
} CotpBuildCcArgs;

/** @brief What parse takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    CotpHeader *out;
} CotpParseArgs;

/**
 * @brief TPKT (RFC 1006) + COTP / ISO 8073 X.224 class-0 frame codec (PROTOCORE_ENABLE_COTP) - zero-heap "ISO transport
 * on TCP" framing, the reusable foundation under S7comm and IEC 61850 MMS.
 *
 * A caller sets the members a call takes, invokes it through ::Cotp with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Cotp.tpkt_build_args.buf = ...;
 *   Cotp.tpkt_build_args.cap = ...;
 *   Cotp.tpkt_build_args.payload = ...;
 *   Cotp.tpkt_build_args.payload_len = ...;
 *   Cotp.tpkt_build(work);
 *   // Cotp.n is what the call reports
 *
 * @var CotpNs::tpkt_build_args  what tpkt_build takes: buf, cap, payload, payload_len
 * @var CotpNs::tpkt_parse_args  what tpkt_parse takes: buf, len, payload, payload_len,
 * @var CotpNs::build_dt_args  what build_dt takes: buf, cap, data, data_len, eot
 * @var CotpNs::build_cr_args  what build_cr takes: buf, cap, src_ref, tpdu_size_code,
 * @var CotpNs::build_cc_args  what build_cc takes: buf, cap, dst_ref, src_ref,
 * @var CotpNs::parse_args  what parse takes: buf, len, out
 * @var CotpNs::ok  true on a complete, version-3 packet; false on bad version / ...
 * @var CotpNs::n  the count a call reports
 * @var CotpNs::tpkt_build  wrap payload in a TPKT envelope. Returns total octets, or 0 on ...
 * @var CotpNs::tpkt_parse  parse a TPKT envelope; reports the X.224 payload slice and bytes ...
 * @var CotpNs::build_dt  build a COTP Data TPDU around data: `LI=2, 0xF0, (EOT|0)` + data
 * @var CotpNs::build_cr  build a COTP Connection Request: `LI 0xE0 dst-ref(0) src-ref ...
 * @var CotpNs::build_cc  build a COTP Connection Confirm (the server's response to a CR): ...
 * @var CotpNs::parse  parse a COTP TPDU (typically the TPKT payload)
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    CotpTpktBuildArgs tpkt_build_args;
    CotpTpktParseArgs tpkt_parse_args;
    CotpBuildDtArgs build_dt_args;
    CotpBuildCrArgs build_cr_args;
    CotpBuildCcArgs build_cc_args;
    CotpParseArgs parse_args;

    proto_bool ok;
    size_t n;

    void (*const tpkt_build)(uint8_t *restrict work);
    void (*const tpkt_parse)(uint8_t *restrict work);
    void (*const build_dt)(uint8_t *restrict work);
    void (*const build_cr)(uint8_t *restrict work);
    void (*const build_cc)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
} CotpNs;

/** @brief The one symbol this module exports. */
extern CotpNs Cotp;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_COTP

#endif // PROTOCORE_COTP_H
