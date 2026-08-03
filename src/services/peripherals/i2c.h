// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file i2c.h
 * @brief The one owner of the shared I2C bus for the peripheral drivers.
 *
 * The sensor / peripheral drivers (RTC, SHT3x, MPR121, ADS1115, INA219, PCA9685, VL53L0X,
 * LDC1614, FDC2214) all share a single I2C bus, so they bring it up and address it through
 * these rather than reaching a framework object themselves. That gives one place to choose the
 * pins: PC_I2C_SDA_PIN / PC_I2C_SCL_PIN (default -1 = the platform default GPIO 21 / 22). Move
 * them off 21/22 when those pins are needed elsewhere - most importantly a wired-Ethernet PHY:
 * the LAN8720 RMII on the classic ESP32 (WROOM/WROVER) and the ESP32-P4 uses GPIO 21 and 22 (the
 * S3/C3 have no RMII EMAC and use an SPI Ethernet such as the W5500 instead). Re-begin is
 * idempotent, so per-driver calls are harmless.
 *
 * The transfer verbs are the three shapes the drivers use: a write, a read, and a register read,
 * which is a write and a read joined by a repeated start. They forward to the platform bus seam
 * (board_drivers), so this header names no vendor driver and no framework.
 *
 * Host builds compile the bodies to a refusal; the I2C transfer is silicon-only.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_I2C_H
#define PROTOCORE_I2C_H

#include "board_drivers/board_profiles/pc_platform.h"
#include "protocore_config.h"

/** @brief Bus clock for the shared peripheral bus; 100 kHz standard mode suits every driver here. */
#ifndef PC_I2C_HZ
#define PC_I2C_HZ 100000u
#endif

/** @brief Per-transfer timeout in milliseconds, so a wedged device stops one transfer, not the loop. */
#ifndef PC_I2C_TIMEOUT_MS
#define PC_I2C_TIMEOUT_MS 50u
#endif

PROTO_BEGIN_DECLS

#if PROTOCORE_HOT

/** @brief Bring up the shared I2C bus on PC_I2C_SDA_PIN / PC_I2C_SCL_PIN (-1 = default). */
PC_INLINE proto_bool pc_i2c_begin(void)
{
    return pc_platform_i2c_begin((int)PC_I2C_SDA_PIN, (int)PC_I2C_SCL_PIN, PC_I2C_HZ) != 0;
}

/** @brief Write @p len bytes to @p addr, closing with a stop. */
PC_INLINE proto_bool pc_i2c_write(uint8_t addr, const uint8_t *buf, size_t len)
{
    return pc_platform_i2c_write(addr, buf, (uint32_t)len, PC_I2C_TIMEOUT_MS) != 0;
}

/** @brief Read @p len bytes from @p addr. */
PC_INLINE proto_bool pc_i2c_read(uint8_t addr, uint8_t *buf, size_t len)
{
    return pc_platform_i2c_read(addr, buf, (uint32_t)len, PC_I2C_TIMEOUT_MS) != 0;
}

/** @brief Write @p wlen bytes, then read @p rlen back in the same transaction (repeated start). */
PC_INLINE proto_bool pc_i2c_write_read(uint8_t addr, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen)
{
    return pc_platform_i2c_write_read(addr, w, (uint32_t)wlen, r, (uint32_t)rlen, PC_I2C_TIMEOUT_MS) != 0;
}

#else // host build: no bus. The codecs above these drivers are host-tested; the transfer is not.

PC_INLINE proto_bool pc_i2c_begin(void)
{
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_write(uint8_t addr, const uint8_t *buf, size_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_read(uint8_t addr, uint8_t *buf, size_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_write_read(uint8_t addr, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen)
{
    (void)addr;
    (void)w;
    (void)wlen;
    (void)r;
    (void)rlen;
    return PROTO_FALSE;
}

#endif // PROTOCORE_HOT

PROTO_END_DECLS

#endif // PROTOCORE_I2C_H
