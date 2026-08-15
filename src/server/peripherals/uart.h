// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file uart.h
 * @brief The one owner of the UART units for the peripheral drivers.
 *
 * The third sibling of i2c.h and spi.h, for the parts that speak a serial stream rather than a
 * bus: the mmWave presence radars (LD2410, HMMD) and the interface bridge's UART targets. A unit
 * is a controller index, 0 upward; which pins it runs on is a board fact, so the caller names
 * them and -1 leaves a pin at the unit's default.
 *
 * A driver reads what has arrived rather than blocking on a byte count: ::protocore_uart_available
 * reports the depth of the driver's receive ring, and ::protocore_uart_read takes what is there up to
 * its timeout. Nothing here waits without a bound (SRC_LAW rule 5).
 *
 * The bodies compile wherever the platform states a bus.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UART_H
#define PROTOCORE_UART_H

#include "core_setup/board_profiles/protocore_platform.h"
#include "protocore_config.h"

/** @brief Read timeout in milliseconds, for a driver that takes whatever has arrived. */
#ifndef PROTOCORE_UART_TIMEOUT_MS
#define PROTOCORE_UART_TIMEOUT_MS 20u
#endif

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_HAS_BUS

/** @brief Bring up @p unit at @p baud, 8N1, on @p rx_pin / @p tx_pin (-1 = the unit's default). */
PROTOCORE_INLINE proto_bool protocore_uart_begin(uint8_t unit, uint32_t baud, int rx_pin, int tx_pin)
{
    return protocore_platform_uart_begin(unit, baud, rx_pin, tx_pin) != 0;
}

/** @brief Write @p len bytes to @p unit. @return true if the driver took all of them. */
PROTOCORE_INLINE proto_bool protocore_uart_write(uint8_t unit, const uint8_t *buf, size_t len)
{
    return protocore_platform_uart_write(unit, buf, (int)len) == (int)len;
}

/**
 * @brief Take up to @p len bytes from @p unit's receive ring, waiting at most @p ms for them.
 * @return how many bytes were written into @p buf.
 */
PROTOCORE_INLINE size_t protocore_uart_read(uint8_t unit, uint8_t *buf, size_t len, uint32_t ms)
{
    int n = protocore_platform_uart_read(unit, buf, (uint32_t)len, ms);
    return n > 0 ? (size_t)n : 0u;
}

/** @brief Bytes sitting in @p unit's receive ring. */
PROTOCORE_INLINE size_t protocore_uart_available(uint8_t unit)
{
    return (size_t)protocore_platform_uart_available(unit);
}

/** @brief Take one byte from @p unit. @return true if one was there. */
PROTOCORE_INLINE proto_bool protocore_uart_read_byte(uint8_t unit, uint8_t *out)
{
    return protocore_uart_read(unit, out, 1, PROTOCORE_UART_TIMEOUT_MS) == 1u;
}

#else // no bus seam on this build

PROTOCORE_INLINE proto_bool protocore_uart_begin(uint8_t unit, uint32_t baud, int rx_pin, int tx_pin)
{
    (void)unit;
    (void)baud;
    (void)rx_pin;
    (void)tx_pin;
    return PROTO_TRUE;
}

PROTOCORE_INLINE proto_bool protocore_uart_write(uint8_t unit, const uint8_t *buf, size_t len)
{
    (void)unit;
    (void)buf;
    (void)len;
    return PROTO_FALSE;
}

PROTOCORE_INLINE size_t protocore_uart_read(uint8_t unit, uint8_t *buf, size_t len, uint32_t ms)
{
    (void)unit;
    (void)buf;
    (void)len;
    (void)ms;
    return 0u;
}

PROTOCORE_INLINE size_t protocore_uart_available(uint8_t unit)
{
    (void)unit;
    return 0u;
}

PROTOCORE_INLINE proto_bool protocore_uart_read_byte(uint8_t unit, uint8_t *out)
{
    return protocore_uart_read(unit, out, 1, PROTOCORE_UART_TIMEOUT_MS) == 1u;
}

#endif // PROTOCORE_HAS_BUS

PROTOCORE_END_DECLS

#endif // PROTOCORE_UART_H
