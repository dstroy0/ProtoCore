// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sigfox.h
 * @brief Sigfox modem AT-command codec (PROTOCORE_ENABLE_SIGFOX) - Wisol / Murata over UART.
 *
 * The tiny-uplink half of a Sigfox-to-web bridge. A Wisol (SFM10R) / Murata Sigfox modem
 * is driven by AT commands over a UART: protocore_sigfox_build_uplink() formats an `AT$SF=<hex>`
 * command for a payload (the Sigfox network caps a message at 12 bytes and ~140 messages
 * per day, so uplinks are rare and small), and protocore_sigfox_parse_response() classifies the
 * modem's reply as OK, ERROR, or still pending (nothing conclusive yet). Pure text codec -
 * you carry the bytes over your UART - so it is fully host-testable. This is uplink-only
 * (the common Sigfox use); a device sends readings up, it is not addressed downlink.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SIGFOX_H
#define PROTOCORE_SIGFOX_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SIGFOX

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Classification of a Sigfox modem response line. */
typedef enum PROTO_ENUM_PACKED
{
    SIGFOX_PENDING = 0, ///< nothing conclusive yet (echo / partial); keep reading
    SIGFOX_OK = 1,      ///< the modem accepted / completed the command
    SIGFOX_ERROR = 2,   ///< the modem reported an error
} protocore_sigfox_result;

/** @brief What build_uplink takes: payload, len, out, cap. */
typedef struct
{
    const uint8_t *payload;
    uint8_t len;
    char *out;
    uint16_t cap;
} SigfoxBuildUplinkArgs;

/** @brief What parse_response takes: buf, len. */
typedef struct
{
    const char *buf;
    uint16_t len;
} SigfoxParseResponseArgs;

/**
 * @brief Sigfox modem AT-command codec (PROTOCORE_ENABLE_SIGFOX) - Wisol / Murata over UART.
 *
 * A caller sets the members a call takes, invokes it through ::Sigfox with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Sigfox.build_uplink_args.payload = ...;
 *   Sigfox.build_uplink_args.len = ...;
 *   Sigfox.build_uplink_args.out = ...;
 *   Sigfox.build_uplink_args.cap = ...;
 *   Sigfox.build_uplink(work);
 *   // Sigfox.value is what the call reports
 *
 * @var SigfoxNs::build_uplink_args  what build_uplink takes: payload, len, out, cap
 * @var SigfoxNs::parse_response_args  what parse_response takes: buf, len
 * @var SigfoxNs::ok  a call's true/false outcome
 * @var SigfoxNs::value  the command length (excluding the NUL), or 0 if len exceeds ...
 * @var SigfoxNs::status  SIGFOX_OK, SIGFOX_ERROR, or SIGFOX_PENDING if neither is present yet
 * @var SigfoxNs::build_uplink  format an `AT$SF=<hex>\r\n` uplink command for payload into out (a ...
 * @var SigfoxNs::parse_response  classify a modem reply (scans buf for "OK" / "ERROR")
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    SigfoxBuildUplinkArgs build_uplink_args;
    SigfoxParseResponseArgs parse_response_args;

    proto_bool ok;
    uint16_t value;
    protocore_sigfox_result status;

    void (*const build_uplink)(uint8_t *restrict work);
    void (*const parse_response)(uint8_t *restrict work);
} SigfoxNs;

/** @brief The one symbol this module exports. */
extern SigfoxNs Sigfox;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SIGFOX

#endif // PROTOCORE_SIGFOX_H
