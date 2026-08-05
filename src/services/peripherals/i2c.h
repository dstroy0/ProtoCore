// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file i2c.h
 * @brief The one owner of the shared I2C bus for the peripheral drivers.
 *
 * The sensor / peripheral drivers (RTC, SHT3x, MPR121, ADS1115, INA219, PCA9685, VL53L0X,
 * LDC1614, FDC2214) share one bus and bring it up through these verbs. The pins come from
 * PC_I2C_SDA_PIN / PC_I2C_SCL_PIN (default -1 = the platform default GPIO 21 / 22). Re-begin
 * is idempotent, so per-driver calls are harmless.
 *
 * This covers the master half of the protocol. The three transfer shapes a driver reaches for are
 * a write, a read, and a register read, which is a write and a read joined by a repeated start.
 * The rest of what the bus can express is here too: a bus scan, an address probe, the general
 * call, 10-bit addressing, a per-transfer clock, and recovery of a bus a device is holding low.
 *
 * Every verb has a plain form on the default bus (PC_I2C_BUS) and an `_on` form naming a
 * controller, so a board with two I2C controllers drives both through one owner.
 *
 * An address is 7-bit unless it is wrapped in ::PC_I2C_ADDR10, which selects the 10-bit form.
 *
 * Host builds compile the bodies to a refusal.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_I2C_H
#define PROTOCORE_I2C_H

#include "board_drivers/board_profiles/pc_platform.h"
#include "protocore_config.h"

/** @brief Bus clock for the shared peripheral bus; 100 kHz standard mode. */
#ifndef PC_I2C_HZ
#define PC_I2C_HZ 100000u
#endif

/** @brief Per-transfer timeout in milliseconds. */
#ifndef PC_I2C_TIMEOUT_MS
#define PC_I2C_TIMEOUT_MS 50u
#endif

/** @brief Controller the plain verbs drive. */
#ifndef PC_I2C_BUS
#define PC_I2C_BUS 0u
#endif

/** @brief Standard-mode, fast-mode and fast-mode-plus bus clocks. */
#define PC_I2C_HZ_STANDARD 100000u
#define PC_I2C_HZ_FAST 400000u
#define PC_I2C_HZ_FAST_PLUS 1000000u

/** @brief Wrap a 10-bit address so a transfer verb puts the two-byte form on the wire. */
#define PC_I2C_ADDR10(a) ((uint16_t)(PC_I2C_ADDR_10BIT | ((a) & PC_I2C_ADDR_MASK)))

/** @brief Lowest and highest 7-bit addresses a scan reports; 0x00 - 0x07 and 0x78 - 0x7F are reserved. */
#define PC_I2C_SCAN_FIRST 0x08u
#define PC_I2C_SCAN_LAST 0x77u

PROTO_BEGIN_DECLS

#if PROTOCORE_HOT

/** @brief Bring up @p bus on @p sda / @p scl at @p hz (-1 on a pin = the platform default). */
PC_INLINE proto_bool pc_i2c_begin_on(uint8_t bus, int sda, int scl, uint32_t hz)
{
    return pc_platform_i2c_begin(bus, sda, scl, hz) != 0;
}

/** @brief Bring up the shared I2C bus on PC_I2C_SDA_PIN / PC_I2C_SCL_PIN (-1 = default). */
PC_INLINE proto_bool pc_i2c_begin(void)
{
    return pc_i2c_begin_on((uint8_t)PC_I2C_BUS, (int)PC_I2C_SDA_PIN, (int)PC_I2C_SCL_PIN, PC_I2C_HZ);
}

/** @brief Write @p len bytes to @p addr on @p bus, closing with a stop. */
PC_INLINE proto_bool pc_i2c_write_on(uint8_t bus, uint16_t addr, const uint8_t *buf, size_t len)
{
    return pc_platform_i2c_write(bus, addr, buf, (uint32_t)len, PC_I2C_TIMEOUT_MS) != 0;
}

/** @brief Write @p len bytes to @p addr, closing with a stop. */
PC_INLINE proto_bool pc_i2c_write(uint16_t addr, const uint8_t *buf, size_t len)
{
    return pc_i2c_write_on((uint8_t)PC_I2C_BUS, addr, buf, len);
}

/** @brief Read @p len bytes from @p addr on @p bus. */
PC_INLINE proto_bool pc_i2c_read_on(uint8_t bus, uint16_t addr, uint8_t *buf, size_t len)
{
    return pc_platform_i2c_read(bus, addr, buf, (uint32_t)len, PC_I2C_TIMEOUT_MS) != 0;
}

/** @brief Read @p len bytes from @p addr. */
PC_INLINE proto_bool pc_i2c_read(uint16_t addr, uint8_t *buf, size_t len)
{
    return pc_i2c_read_on((uint8_t)PC_I2C_BUS, addr, buf, len);
}

/** @brief Write @p wlen bytes to @p addr on @p bus, then read @p rlen back through a repeated start. */
PC_INLINE proto_bool pc_i2c_write_read_on(uint8_t bus, uint16_t addr, const uint8_t *w, size_t wlen, uint8_t *r,
                                          size_t rlen)
{
    return pc_platform_i2c_write_read(bus, addr, w, (uint32_t)wlen, r, (uint32_t)rlen, PC_I2C_TIMEOUT_MS) != 0;
}

/** @brief Write @p wlen bytes, then read @p rlen back in the same transaction (repeated start). */
PC_INLINE proto_bool pc_i2c_write_read(uint16_t addr, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen)
{
    return pc_i2c_write_read_on((uint8_t)PC_I2C_BUS, addr, w, wlen, r, rlen);
}

/** @brief Run @p bus at @p hz from here on. */
PC_INLINE proto_bool pc_i2c_set_clock_on(uint8_t bus, uint32_t hz)
{
    return pc_platform_i2c_set_clock(bus, hz) != 0;
}

/** @brief Run the shared bus at @p hz from here on; a part slower than the bus needs this. */
PC_INLINE proto_bool pc_i2c_set_clock(uint32_t hz)
{
    return pc_i2c_set_clock_on((uint8_t)PC_I2C_BUS, hz);
}

/** @brief Address @p addr on @p bus and stop. @return true if a device drove ACK. */
PC_INLINE proto_bool pc_i2c_probe_on(uint8_t bus, uint16_t addr)
{
    return pc_platform_i2c_probe(bus, addr, PC_I2C_TIMEOUT_MS) != 0;
}

/** @brief Address @p addr and stop. @return true if a device drove ACK. */
PC_INLINE proto_bool pc_i2c_probe(uint16_t addr)
{
    return pc_i2c_probe_on((uint8_t)PC_I2C_BUS, addr);
}

/**
 * @brief Probe every non-reserved 7-bit address on @p bus, writing those that answered into
 *        @p out (caller-owned, @p cap entries).
 * @return how many answered, which is how many entries of @p out were written.
 */
PC_INLINE size_t pc_i2c_scan_on(uint8_t bus, uint8_t *out, size_t cap)
{
    size_t n = 0;
    for (uint16_t a = PC_I2C_SCAN_FIRST; a <= PC_I2C_SCAN_LAST && n < cap; a++)
    {
        if (pc_i2c_probe_on(bus, a))
        {
            out[n] = (uint8_t)a;
            n++;
        }
    }
    return n;
}

/** @brief Probe every non-reserved 7-bit address on the shared bus into @p out. */
PC_INLINE size_t pc_i2c_scan(uint8_t *out, size_t cap)
{
    return pc_i2c_scan_on((uint8_t)PC_I2C_BUS, out, cap);
}

/** @brief Write @p len bytes to address 0x00 on @p bus, which every device answers. */
PC_INLINE proto_bool pc_i2c_general_call_on(uint8_t bus, const uint8_t *buf, size_t len)
{
    return pc_i2c_write_on(bus, PC_I2C_GENERAL_CALL, buf, len);
}

/** @brief Write @p len bytes to the general call address, which every device on the bus answers. */
PC_INLINE proto_bool pc_i2c_general_call(const uint8_t *buf, size_t len)
{
    return pc_i2c_general_call_on((uint8_t)PC_I2C_BUS, buf, len);
}

/** @brief Clock @p bus until the device holding SDA low releases it, then drive a stop. */
PC_INLINE proto_bool pc_i2c_recover_on(uint8_t bus, int sda, int scl)
{
    return pc_platform_i2c_recover(bus, sda, scl) != 0;
}

/** @brief Free the shared bus from a device holding SDA low. @return true if SDA came back high. */
PC_INLINE proto_bool pc_i2c_recover(void)
{
    return pc_i2c_recover_on((uint8_t)PC_I2C_BUS, (int)PC_I2C_SDA_PIN, (int)PC_I2C_SCL_PIN);
}

#else // host build: no bus

PC_INLINE proto_bool pc_i2c_begin_on(uint8_t bus, int sda, int scl, uint32_t hz)
{
    (void)bus;
    (void)sda;
    (void)scl;
    (void)hz;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_begin(void)
{
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_write_on(uint8_t bus, uint16_t addr, const uint8_t *buf, size_t len)
{
    (void)bus;
    (void)addr;
    (void)buf;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_write(uint16_t addr, const uint8_t *buf, size_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_read_on(uint8_t bus, uint16_t addr, uint8_t *buf, size_t len)
{
    (void)bus;
    (void)addr;
    (void)buf;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_read(uint16_t addr, uint8_t *buf, size_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_write_read_on(uint8_t bus, uint16_t addr, const uint8_t *w, size_t wlen, uint8_t *r,
                                          size_t rlen)
{
    (void)bus;
    (void)addr;
    (void)w;
    (void)wlen;
    (void)r;
    (void)rlen;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_write_read(uint16_t addr, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen)
{
    (void)addr;
    (void)w;
    (void)wlen;
    (void)r;
    (void)rlen;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_set_clock_on(uint8_t bus, uint32_t hz)
{
    (void)bus;
    (void)hz;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_set_clock(uint32_t hz)
{
    (void)hz;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_probe_on(uint8_t bus, uint16_t addr)
{
    (void)bus;
    (void)addr;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_probe(uint16_t addr)
{
    (void)addr;
    return PROTO_FALSE;
}

PC_INLINE size_t pc_i2c_scan_on(uint8_t bus, uint8_t *out, size_t cap)
{
    (void)bus;
    (void)out;
    (void)cap;
    return 0;
}

PC_INLINE size_t pc_i2c_scan(uint8_t *out, size_t cap)
{
    (void)out;
    (void)cap;
    return 0;
}

PC_INLINE proto_bool pc_i2c_general_call_on(uint8_t bus, const uint8_t *buf, size_t len)
{
    (void)bus;
    (void)buf;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_general_call(const uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_recover_on(uint8_t bus, int sda, int scl)
{
    (void)bus;
    (void)sda;
    (void)scl;
    return PROTO_FALSE;
}

PC_INLINE proto_bool pc_i2c_recover(void)
{
    return PROTO_FALSE;
}

#endif // PROTOCORE_HOT

PROTO_END_DECLS

#endif // PROTOCORE_I2C_H
