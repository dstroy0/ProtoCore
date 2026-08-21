// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_SIGFOX_H
#define PROTOCORE_SIGFOX_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

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
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_SIGFOX_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Classification of a Sigfox modem response line. */
typedef enum PROTO_ENUM_PACKED
{
    SIGFOX_PENDING = 0, ///< nothing conclusive yet (echo / partial); keep reading
    SIGFOX_OK = 1,      ///< the modem accepted / completed the command
    SIGFOX_ERROR = 2,   ///< the modem reported an error
} protocore_sigfox_result;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    uint16_t (*build_uplink)(uint8_t *, const uint8_t *, uint8_t, char *, uint16_t);
    protocore_sigfox_result (*parse_response)(uint8_t *, const char *, uint16_t);
} SigfoxNs;
PROTOCORE_NS_LAYOUT(SigfoxNs, build_uplink, parse_response);

/**
 * @brief Format an `AT$SF=<hex>\r\n` uplink command for payload into out (a .
 * @param work PROTOCORE_SIGFOX_BORROW bytes the caller took. Not held past the call.
 * @param payload Payload
 * @param len Len
 * @param out Out
 * @param cap Cap
 * @return The uint16_t.
 */
uint16_t protocore_sigfox_build_uplink(uint8_t *work, const uint8_t *payload, uint8_t len, char *out, uint16_t cap);
/**
 * @brief Classify a modem reply (scans buf for "OK" / "ERROR").
 * @param work PROTOCORE_SIGFOX_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param len Len
 * @return The protocore_sigfox_result.
 */
protocore_sigfox_result protocore_sigfox_parse_response(uint8_t *work, const char *buf, uint16_t len);

/** @brief Module namespace. */
PROTOCORE_NS SigfoxNs Sigfox PROTOCORE_UNUSED = {.build_uplink = protocore_sigfox_build_uplink,
                                                 .parse_response = protocore_sigfox_parse_response};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SIGFOX_H
