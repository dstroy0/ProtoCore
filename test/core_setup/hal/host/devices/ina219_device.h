// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host device model: TI INA219 high-side current / power monitor.
//
// The governing document is SBOS448G (August 2008, revised December 2015), cached at
// docs/learn/datasheets/ina219.pdf. Every constant below carries the section it came from:
//
//   8.6.2.1  Config register 00h, reset 399Fh: RST 15, BRNG 13, PG 12:11, BADC 10:7, SADC 6:3,
//            MODE 2:0. PG 00b/01b/10b/11b is +/-40 / 80 / 160 / 320 mV.
//   8.6.3.1  Shunt voltage register 01h, two's complement, LSB 10 uV, clipped to the PG range
//   8.6.3.2  Bus voltage register 02h: BD12..BD0 in bits 15:3, LSB 4 mV, CNVR bit 1, OVF bit 0
//   8.6.3.3  Power register 03h, reset 0
//   8.6.3.4  Current register 04h, reset 0, two's complement
//   8.6.4.1  Calibration register 05h, reset 0. Bit FS0 takes no part in the calculation.
//   8.5.2    Eq 4: Current Register = Shunt Voltage Register * Calibration Register / 4096
//            Eq 5: Power Register   = Current Register * Bus Voltage Register / 5000
//
// Equation 5 names "Bus Voltage Register", and the published LSBs say which value that is. With
// Power_LSB = 20 * Current_LSB (Eq 3), bus LSB 4 mV and P = V * I:
//
//   PowerReg * 20 * Current_LSB = (CurrentReg * Current_LSB) * (BusCount * 0.004)
//   PowerReg = CurrentReg * BusCount / 5000
//
// so it is the 13-bit count in bits 15:3, not the register word. A worked case closes on it: at
// 100 mohm and 100 uA/bit the calibration is 4096, 1 A gives shunt register 10000 and current
// register 10000, 12 V gives bus count 3000, and the power register is 6000, which reads back as
// 6000 * 20 * 100 uA = 12 W - exactly 12 V times 1 A.
//
// A suite applies a shunt drop and a bus voltage; the part computes every register from them.
//
// test/ style: plain host C, like the rest of the host HAL.

#ifndef PROTOCORE_HOST_DEVICE_INA219_H
#define PROTOCORE_HOST_DEVICE_INA219_H

#include "protocore_net_host.h" // protocore_bus_host_attach

#include <stdint.h>

// 8.6.1: the register addresses, which are also the register file's indices.
#define PROTOCORE_INA219_DEV_CONFIG 0u
#define PROTOCORE_INA219_DEV_SHUNT 1u
#define PROTOCORE_INA219_DEV_BUS 2u
#define PROTOCORE_INA219_DEV_POWER 3u
#define PROTOCORE_INA219_DEV_CURRENT 4u
#define PROTOCORE_INA219_DEV_CALIBRATION 5u

// 8.6.2.1 reset value, and the RST bit that returns the part to it.
#define PROTOCORE_INA219_DEV_CONFIG_RESET 0x399Fu
#define PROTOCORE_INA219_DEV_RST 0x8000u

// 8.6.3.2: the bus register's flags, below the 13-bit reading.
#define PROTOCORE_INA219_DEV_CNVR 0x0002u
#define PROTOCORE_INA219_DEV_OVF 0x0001u

typedef struct
{
    int32_t shunt_uv; /**< the drop across the shunt resistor, in microvolts */
    int32_t bus_uv;   /**< the bus voltage, in microvolts */
    uint16_t reg[6];  /**< the register file, indexed by the address pointer */
    uint8_t ptr;      /**< the address pointer */
} protocore_ina219_dev;

// 8.6.2.1 Table 4: the shunt full scale in microvolts for each PG setting, which is where the
// shunt register clips.
static const int32_t protocore_ina219_dev_pg_uv[4] = {40000, 80000, 160000, 320000};

/** @brief Power-up state: 8.6.2.1 config reset, every other register 0, no voltage applied. */
static inline void protocore_ina219_dev_init(protocore_ina219_dev *d)
{
    d->shunt_uv = 0;
    d->bus_uv = 0;
    for (uint32_t i = 0; i < 6; i++)
    {
        d->reg[i] = 0;
    }
    d->reg[PROTOCORE_INA219_DEV_CONFIG] = PROTOCORE_INA219_DEV_CONFIG_RESET;
    d->ptr = PROTOCORE_INA219_DEV_CONFIG;
}

// The four measurement registers, computed from the applied voltages and the programmed
// calibration. They are recomputed per read because the part converts continuously in the default
// mode, so a read reports what is on the pins now.
static inline void protocore_ina219_dev_convert(protocore_ina219_dev *d)
{
    // 8.6.3.1: 10 uV per count, clipped to the range PG selects.
    const int32_t fs = protocore_ina219_dev_pg_uv[(d->reg[PROTOCORE_INA219_DEV_CONFIG] >> 11) & 0x3u];
    int32_t sv = d->shunt_uv;
    sv = sv > fs ? fs : (sv < -fs ? -fs : sv);
    const int16_t shunt = (int16_t)(sv / 10);

    // 8.6.3.2: 4 mV per count in bits 15:3, and the conversion is ready.
    int32_t count = d->bus_uv / 4000;
    count = count > 8191 ? 8191 : (count < 0 ? 0 : count);

    // 8.5.2 Eq 4, with FS0 taking no part (8.6.4.1).
    const int32_t cal = (int32_t)(d->reg[PROTOCORE_INA219_DEV_CALIBRATION] & 0xFFFEu);
    const int32_t current = (int32_t)(((int64_t)shunt * cal) / 4096);

    d->reg[PROTOCORE_INA219_DEV_SHUNT] = (uint16_t)shunt;
    d->reg[PROTOCORE_INA219_DEV_BUS] = (uint16_t)(((uint16_t)count << 3) | PROTOCORE_INA219_DEV_CNVR);
    d->reg[PROTOCORE_INA219_DEV_CURRENT] = (uint16_t)(int16_t)current;
    // 8.5.2 Eq 5, on the 13-bit bus count.
    d->reg[PROTOCORE_INA219_DEV_POWER] = (uint16_t)(int16_t)(((int64_t)current * count) / 5000);
}

// One transfer. A write is the address pointer byte, and for a writable register the two bytes it
// takes; the pointer persists past the write. A read returns the selected register MSB first.
static inline uint32_t protocore_ina219_dev_txn(void *ctx, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen)
{
    protocore_ina219_dev *d = (protocore_ina219_dev *)ctx;
    if (w && wlen)
    {
        d->ptr = w[0];
        // 8.6.3.1 through 8.6.3.4 make the measurement registers read only, so a write that
        // selects one only moves the pointer.
        if (wlen >= 3u && (d->ptr == PROTOCORE_INA219_DEV_CONFIG || d->ptr == PROTOCORE_INA219_DEV_CALIBRATION))
        {
            const uint16_t v = (uint16_t)(((uint16_t)w[1] << 8) | w[2]);
            // 8.6.2.1 RST: a system reset the same as power-on, and the bit self-clears.
            if (d->ptr == PROTOCORE_INA219_DEV_CONFIG && (v & PROTOCORE_INA219_DEV_RST))
            {
                const int32_t sv = d->shunt_uv;
                const int32_t bv = d->bus_uv;
                protocore_ina219_dev_init(d);
                d->shunt_uv = sv;
                d->bus_uv = bv;
            }
            else
            {
                d->reg[d->ptr] = v;
            }
        }
    }
    if (rlen == 0u)
    {
        return 0u;
    }
    if (d->ptr > PROTOCORE_INA219_DEV_CALIBRATION)
    {
        return 0u; // no such register: nothing drives the bus
    }
    protocore_ina219_dev_convert(d);
    const uint16_t v = d->reg[d->ptr];
    uint32_t n = 0;
    while (n < rlen)
    {
        r[n] = (n & 1u) ? (uint8_t)(v & 0xFFu) : (uint8_t)(v >> 8);
        n++;
    }
    return n;
}

/** @brief Reset the model and put it on the I2C bus at one address. 0 when the table is full. */
static inline int protocore_ina219_dev_place(protocore_ina219_dev *d, uint16_t addr)
{
    protocore_ina219_dev_init(d);
    return protocore_bus_host_attach(PROTOCORE_BUS_HOST_I2C, addr, d, protocore_ina219_dev_txn);
}

#endif // PROTOCORE_HOST_DEVICE_INA219_H
