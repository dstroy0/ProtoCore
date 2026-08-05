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
 * @p tx to clock zeros out (a read), or both to exchange.
 *
 * ::pc_spi_txn runs at the configured clock, bit order and mode; ::pc_spi_txn_at names its own,
 * which is what a bus carrying parts with different timing needs.
 *
 * ::pc_spi_txn_ext adds the framing a flash, a display controller or an ADC front end expects: a
 * command, an address, dummy clocks, and a data phase one, two or four bits wide. The controller
 * drives each phase, so the data buffer holds data alone.
 *
 * Chip select is the caller's: which pin selects a part is a board fact, and a driver often holds
 * it across several transfers. ::pc_spi_cs_idle / ::pc_spi_cs_select / ::pc_spi_cs_release drive
 * it through the GPIO seam.
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

/** @brief Controller the plain verbs drive. */
#ifndef PC_SPI_HOST
#define PC_SPI_HOST 0u
#endif

/** @brief Third and fourth data lines, for a quad-width bus; -1 leaves the bus single or dual. */
#ifndef PC_SPI_QUADWP_PIN
#define PC_SPI_QUADWP_PIN (-1)
#endif
#ifndef PC_SPI_QUADHD_PIN
#define PC_SPI_QUADHD_PIN (-1)
#endif

PROTO_BEGIN_DECLS

#if PROTOCORE_HOT

/** @brief Bring up @p host on the given pins; -1 on quadwp / quadhd leaves the bus single or dual. */
PC_INLINE proto_bool pc_spi_begin_on(uint8_t host, int mosi, int miso, int sclk, int quadwp, int quadhd)
{
    return pc_platform_spi_begin(host, mosi, miso, sclk, quadwp, quadhd) != 0;
}

/** @brief Bring up the shared SPI bus on the PC_SPI_*_PIN pins (-1 = default). */
PC_INLINE proto_bool pc_spi_begin(void)
{
    return pc_spi_begin_on((uint8_t)PC_SPI_HOST, (int)PC_SPI_MOSI_PIN, (int)PC_SPI_MISO_PIN, (int)PC_SPI_SCLK_PIN,
                           (int)PC_SPI_QUADWP_PIN, (int)PC_SPI_QUADHD_PIN);
}

/**
 * @brief Clock @p len bytes on @p host at @p hz, @p bit_order and @p mode, shifting @p tx out and
 *        @p rx in. A null @p rx discards the inbound bits; a null @p tx clocks zeros out.
 */
PC_INLINE proto_bool pc_spi_txn_on(uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode, const uint8_t *tx,
                                   uint8_t *rx, size_t len)
{
    return pc_platform_spi_txn(host, hz, bit_order, mode, tx, rx, (uint32_t)len) != 0;
}

/** @brief Clock @p len bytes on the shared bus at @p hz, @p bit_order and @p mode. */
PC_INLINE proto_bool pc_spi_txn_at(uint32_t hz, uint8_t bit_order, uint8_t mode, const uint8_t *tx, uint8_t *rx,
                                   size_t len)
{
    return pc_spi_txn_on((uint8_t)PC_SPI_HOST, hz, bit_order, mode, tx, rx, len);
}

/**
 * @brief Clock @p len bytes, shifting @p tx out and @p rx in.
 *
 * A null @p rx discards the inbound bits; a null @p tx clocks zeros out.
 */
PC_INLINE proto_bool pc_spi_txn(const uint8_t *tx, uint8_t *rx, size_t len)
{
    return pc_spi_txn_at(PC_SPI_HZ, PC_SPI_MSBFIRST, (uint8_t)PC_SPI_MODE, tx, rx, len);
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

/**
 * @brief A framed transfer on @p host: a @p cmd_bits command, an @p addr_bits address,
 *        @p dummy_bits idle clocks, then @p len data bytes at @p lanes bits per clock.
 *
 * A zero bit count omits that phase. @p lanes is PC_SPI_LANES_1, _2 or _4.
 */
PC_INLINE proto_bool pc_spi_txn_ext_on(uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode, uint16_t cmd,
                                       uint8_t cmd_bits, uint32_t addr, uint8_t addr_bits, uint8_t dummy_bits,
                                       uint8_t lanes, const uint8_t *tx, uint8_t *rx, size_t len)
{
    return pc_platform_spi_txn_ext(host, hz, bit_order, mode, cmd, cmd_bits, addr, addr_bits, dummy_bits, lanes, tx, rx,
                                   (uint32_t)len) != 0;
}

/** @brief A framed transfer on the shared bus at the configured clock, bit order and mode. */
PC_INLINE proto_bool pc_spi_txn_ext(uint16_t cmd, uint8_t cmd_bits, uint32_t addr, uint8_t addr_bits,
                                    uint8_t dummy_bits, uint8_t lanes, const uint8_t *tx, uint8_t *rx, size_t len)
{
    return pc_spi_txn_ext_on((uint8_t)PC_SPI_HOST, PC_SPI_HZ, PC_SPI_MSBFIRST, (uint8_t)PC_SPI_MODE, cmd, cmd_bits,
                             addr, addr_bits, dummy_bits, lanes, tx, rx, len);
}

/** @brief Drive @p pin as an output at the deselected level, which is how a part is left idle. */
PC_INLINE void pc_spi_cs_idle(uint8_t pin)
{
    pc_platform_gpio_mode(pin, PC_GPIO_OUT);
    pc_platform_gpio_write(pin, PC_GPIO_HIGH);
}

/** @brief Pull @p pin low, selecting the part for the transfers that follow. */
PC_INLINE void pc_spi_cs_select(uint8_t pin)
{
    pc_platform_gpio_write(pin, PC_GPIO_LOW);
}

/** @brief Let @p pin back high, deselecting the part. */
PC_INLINE void pc_spi_cs_release(uint8_t pin)
{
    pc_platform_gpio_write(pin, PC_GPIO_HIGH);
}

#else // host build: no bus

PC_INLINE proto_bool pc_spi_begin_on(uint8_t host, int mosi, int miso, int sclk, int quadwp, int quadhd)
{
    (void)host;
    (void)mosi;
    (void)miso;
    (void)sclk;
    (void)quadwp;
    (void)quadhd;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_spi_begin(void)
{
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_spi_txn_on(uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode, const uint8_t *tx,
                                   uint8_t *rx, size_t len)
{
    (void)host;
    (void)hz;
    (void)bit_order;
    (void)mode;
    (void)tx;
    (void)rx;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_spi_txn_at(uint32_t hz, uint8_t bit_order, uint8_t mode, const uint8_t *tx, uint8_t *rx,
                                   size_t len)
{
    (void)hz;
    (void)bit_order;
    (void)mode;
    (void)tx;
    (void)rx;
    (void)len;
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

PC_INLINE proto_bool pc_spi_txn_ext_on(uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode, uint16_t cmd,
                                       uint8_t cmd_bits, uint32_t addr, uint8_t addr_bits, uint8_t dummy_bits,
                                       uint8_t lanes, const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)host;
    (void)hz;
    (void)bit_order;
    (void)mode;
    (void)cmd;
    (void)cmd_bits;
    (void)addr;
    (void)addr_bits;
    (void)dummy_bits;
    (void)lanes;
    (void)tx;
    (void)rx;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_spi_txn_ext(uint16_t cmd, uint8_t cmd_bits, uint32_t addr, uint8_t addr_bits,
                                    uint8_t dummy_bits, uint8_t lanes, const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)cmd;
    (void)cmd_bits;
    (void)addr;
    (void)addr_bits;
    (void)dummy_bits;
    (void)lanes;
    (void)tx;
    (void)rx;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE void pc_spi_cs_idle(uint8_t pin)
{
    (void)pin;
}

PC_INLINE void pc_spi_cs_select(uint8_t pin)
{
    (void)pin;
}

PC_INLINE void pc_spi_cs_release(uint8_t pin)
{
    (void)pin;
}

#endif // PROTOCORE_HOT

PROTO_END_DECLS

#endif // PROTOCORE_SPI_H
