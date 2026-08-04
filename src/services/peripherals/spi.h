// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file spi.h
 * @brief The one owner of the shared SPI bus for the peripheral drivers.
 *
 * The sibling of i2c.h, for the drivers whose part is on SPI rather than I2C (the interface
 * bridge, the W5500 Ethernet, the radio modules). They share one bus and bring it up through
 * these verbs. The pins come from PC_SPI_MOSI_PIN / PC_SPI_MISO_PIN / PC_SPI_SCLK_PIN (default
 * -1 = the platform's default host pins). Re-begin is idempotent, so per-driver calls are
 * harmless.
 *
 * SPI is symmetric: every clock shifts a bit out and a bit in, so one transfer verb covers the
 * three shapes a driver needs. Pass a null @p rx to discard what comes back (a write), a null
 * @p tx to clock zeros out (a read), or both to exchange. Chip select is the caller's, because
 * which pin selects the part is a board fact and a driver often holds it across several
 * transfers.
 *
 * Host builds compile the bodies to a refusal.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SPI_H
#define PROTOCORE_SPI_H

#include "board_drivers/board_profiles/pc_platform.h"
#include "protocore_config.h"

/** @brief Bus clock for the shared peripheral bus; 1 MHz is safe on every part on it. */
#ifndef PC_SPI_HZ
#define PC_SPI_HZ 1000000u
#endif

/** @brief Clock polarity and phase, as SPI mode 0..3. Mode 0 is what these parts use. */
#ifndef PC_SPI_MODE
#define PC_SPI_MODE 0u
#endif

PROTO_BEGIN_DECLS

#if PROTOCORE_HOT

/** @brief Bring up the shared SPI bus on the PC_SPI_*_PIN pins (-1 = default). */
PC_INLINE proto_bool pc_spi_begin(void)
{
    return pc_platform_spi_begin((int)PC_SPI_MOSI_PIN, (int)PC_SPI_MISO_PIN, (int)PC_SPI_SCLK_PIN) != 0;
}

/**
 * @brief Clock @p len bytes, shifting @p tx out and @p rx in.
 *
 * A null @p rx discards the inbound bits; a null @p tx clocks zeros out.
 */
PC_INLINE proto_bool pc_spi_txn(const uint8_t *tx, uint8_t *rx, size_t len)
{
    return pc_platform_spi_txn(PC_SPI_HZ, PC_SPI_MSBFIRST, (uint8_t)PC_SPI_MODE, tx, rx, (uint32_t)len) != 0;
}

/** @brief Clock @p len bytes out, discarding what shifts back. */
PC_INLINE proto_bool pc_spi_write(const uint8_t *tx, size_t len)
{
    return pc_spi_txn(tx, NULL, len);
}

/** @brief Clock @p len zero bytes out, keeping what shifts back. */
PC_INLINE proto_bool pc_spi_read(uint8_t *rx, size_t len)
{
    return pc_spi_txn(NULL, rx, len);
}

#else // host build: no bus

PC_INLINE proto_bool pc_spi_begin(void)
{
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_spi_txn(const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)tx;
    (void)rx;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_spi_write(const uint8_t *tx, size_t len)
{
    (void)tx;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_spi_read(uint8_t *rx, size_t len)
{
    (void)rx;
    (void)len;
    return PROTO_FALSE;
}

#endif // PROTOCORE_HOT

PROTO_END_DECLS

#endif // PROTOCORE_SPI_H
