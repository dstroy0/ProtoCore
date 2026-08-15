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

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SIGFOX

PROTOCORE_BEGIN_DECLS

/** @brief Classification of a Sigfox modem response line. */
typedef enum PROTO_ENUM_PACKED
{
    SIGFOX_PENDING = 0, ///< nothing conclusive yet (echo / partial); keep reading
    SIGFOX_OK = 1,      ///< the modem accepted / completed the command
    SIGFOX_ERROR = 2,   ///< the modem reported an error
} protocore_sigfox_result;

/**
 * @brief Format an `AT$SF=<hex>\r\n` uplink command for @p payload into @p out (a NUL-
 *        terminated C string).
 * @return the command length (excluding the NUL), or 0 if @p len exceeds
 *         PROTOCORE_SIGFOX_MAX_PAYLOAD or the command would not fit @p cap.
 */
uint16_t protocore_sigfox_build_uplink(const uint8_t *payload, uint8_t len, char *out, uint16_t cap);

/**
 * @brief Classify a modem reply (scans @p buf for "OK" / "ERROR").
 * @return SIGFOX_OK, SIGFOX_ERROR, or SIGFOX_PENDING if
 * neither is present yet.
 */
protocore_sigfox_result protocore_sigfox_parse_response(const char *buf, uint16_t len);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SIGFOX

#endif // PROTOCORE_SIGFOX_H
