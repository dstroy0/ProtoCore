// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host device model: TI FDC2212 / FDC2214 / FDC2112 / FDC2114 capacitance-to-digital converter.
//
// The governing document is SNOSCZ5B (June 2015, revised October 2024), cached at
// docs/learn/datasheets/fdc2214.pdf. Every constant below carries the section it came from:
//
//   7.6.1   the register list: DATA_CH0 00h, DATA_LSB_CH0 01h, RCOUNT_CH0..3 08h..0Bh,
//           OFFSET 0Ch..0Fh, SETTLECOUNT 10h..13h, CLOCK_DIVIDERS 14h..17h, STATUS 18h,
//           STATUS_CONFIG 19h, CONFIG 1Ah, MUX_CONFIG 1Bh, RESET_DEV 1Ch,
//           DRIVE_CURRENT_CH0..3 1Eh..21h, MANUFACTURER_ID 7Eh, DEVICE_ID 7Fh
//   7.6.2   DATA_CH0: CH0_ERR_WD bit 13, CH0_ERR_AW bit 12, DATA0[27:16] in bits 11:0
//   7.6.3   DATA_LSB_CH0 holds DATA0[15:0] and "must be read after Register address 0x00"
//   7.6.10  RCOUNT_CH0 reset 0080h
//   7.6.28  Table 7-38, CONFIG reset 2801h: ACTIVE_CHAN 00, SLEEP_MODE_EN 1, reserved 12 set to
//           b1, SENSOR_ACTIVATE_SEL 1, reserved 10 set to b1, REF_CLK_SRC 0, reserved 8 set to b0,
//           INTB_DIS 0, HIGH_CURRENT_DRV 0, reserved 5:0 = b00'0001
//   7.6.35  MANUFACTURER_ID = 5449h
//   7.6.36  DEVICE_ID = 3055h on the FDC2212 / FDC2214 (3054h on the 12-bit FDC2112 / FDC2114)
//   7.4.1   "When the FDC powers up, the FDC enters into Sleep Mode and waits for configuration.
//           When the device is configured, exit Sleep Mode by setting CONFIG.SLEEP_MODE_EN to b0."
//
// The read-after rule is the one worth having a model for. 7.6.3 makes DATA_LSB_CH0 coherent only
// when it follows a read of DATA_CH0, so the low half is latched when the high half is read. A
// driver that reads them the other way round still puts two valid register addresses on the wire,
// and only a model that latches shows it reading a stale low half.
//
// Unlike the PCA9685 and the MPR121, nothing here is discarded outside a mode: 7.4.1 recommends
// configuring in Sleep Mode but does not say a write is dropped otherwise, so this model takes
// every write. What Sleep Mode gates is the conversion, not the register.
//
// test/ style: plain host C, like the rest of the host HAL.

#ifndef PROTOCORE_HOST_DEVICE_FDC2214_H
#define PROTOCORE_HOST_DEVICE_FDC2214_H

#include "protocore_net_host.h" // protocore_bus_host_attach

#include <stdint.h>

// 7.6.1: the addresses this model acts on.
#define PROTOCORE_FDC2214_DEV_DATA_CH0 0x00u
#define PROTOCORE_FDC2214_DEV_DATA_LSB_CH0 0x01u
#define PROTOCORE_FDC2214_DEV_RCOUNT_CH0 0x08u
#define PROTOCORE_FDC2214_DEV_CONFIG 0x1Au
#define PROTOCORE_FDC2214_DEV_MANUFACTURER_ID 0x7Eu
#define PROTOCORE_FDC2214_DEV_DEVICE_ID 0x7Fu

// 7.6.28 / 7.6.10 / 7.6.35 / 7.6.36 reset values.
#define PROTOCORE_FDC2214_DEV_CONFIG_RESET 0x2801u
#define PROTOCORE_FDC2214_DEV_RCOUNT_RESET 0x0080u
#define PROTOCORE_FDC2214_DEV_MANUFACTURER 0x5449u
#define PROTOCORE_FDC2214_DEV_ID_2214 0x3055u
#define PROTOCORE_FDC2214_DEV_ID_2114 0x3054u

// 7.6.28: the CONFIG bit that holds the part out of conversion.
#define PROTOCORE_FDC2214_DEV_SLEEP 0x2000u

// 7.6.2: the two error flags above the data, in the high register.
#define PROTOCORE_FDC2214_DEV_ERR_WD 0x2000u
#define PROTOCORE_FDC2214_DEV_ERR_AW 0x1000u

typedef struct
{
    uint32_t ch0;      /**< the 28-bit conversion result on channel 0 */
    uint8_t err;       /**< the error flags to raise with it, in bits 13 and 12 of the high register */
    uint16_t reg[128]; /**< the register file, addressed as the part addresses it */
    uint8_t ptr;       /**< the register the last transfer named */
} protocore_fdc2214_dev;

/** @brief Power-up state: 7.4.1 puts the part in Sleep Mode with the 7.6.x reset values. */
static inline void protocore_fdc2214_dev_init(protocore_fdc2214_dev *d)
{
    d->ch0 = 0;
    d->err = 0;
    for (uint32_t i = 0; i < 128u; i++)
    {
        d->reg[i] = 0;
    }
    for (uint32_t i = 0; i < 4u; i++)
    {
        d->reg[PROTOCORE_FDC2214_DEV_RCOUNT_CH0 + i] = PROTOCORE_FDC2214_DEV_RCOUNT_RESET;
    }
    d->reg[PROTOCORE_FDC2214_DEV_CONFIG] = PROTOCORE_FDC2214_DEV_CONFIG_RESET;
    d->reg[PROTOCORE_FDC2214_DEV_MANUFACTURER_ID] = PROTOCORE_FDC2214_DEV_MANUFACTURER;
    d->reg[PROTOCORE_FDC2214_DEV_DEVICE_ID] = PROTOCORE_FDC2214_DEV_ID_2214;
    d->ptr = 0;
}

/** @brief 7.4.1 / 7.6.28: the part converts only once SLEEP_MODE_EN has been cleared. */
static inline int protocore_fdc2214_dev_awake(const protocore_fdc2214_dev *d)
{
    return (d->reg[PROTOCORE_FDC2214_DEV_CONFIG] & PROTOCORE_FDC2214_DEV_SLEEP) == 0;
}

// One transfer. A write is the register byte then its 16-bit value, big-endian; a read returns the
// register the pointer names, MSB first. Reading DATA_CH0 latches the low half into DATA_LSB_CH0,
// which is what 7.6.3's "must be read after Register address 0x00" means: the pair is coherent only
// in that order, and a read of the low half alone returns whatever the last high read left there.
static inline uint32_t protocore_fdc2214_dev_txn(void *ctx, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen)
{
    protocore_fdc2214_dev *d = (protocore_fdc2214_dev *)ctx;
    if (w && wlen)
    {
        d->ptr = (uint8_t)(w[0] & 0x7Fu);
        if (wlen >= 3u)
        {
            const uint16_t v = (uint16_t)(((uint16_t)w[1] << 8) | w[2]);
            // 7.6.2 / 7.6.3 / 7.6.35 / 7.6.36: the data and the identity registers are read only.
            if (d->ptr > PROTOCORE_FDC2214_DEV_DATA_LSB_CH0 && d->ptr < PROTOCORE_FDC2214_DEV_MANUFACTURER_ID)
            {
                d->reg[d->ptr] = v;
            }
        }
    }
    if (rlen == 0u)
    {
        return 0u;
    }
    if (d->ptr == PROTOCORE_FDC2214_DEV_DATA_CH0)
    {
        const uint32_t live = protocore_fdc2214_dev_awake(d) ? d->ch0 : 0u;
        d->reg[PROTOCORE_FDC2214_DEV_DATA_CH0] =
            (uint16_t)(((live >> 16) & 0x0FFFu) | ((uint16_t)(d->err & 0x0Fu) << 12));
        d->reg[PROTOCORE_FDC2214_DEV_DATA_LSB_CH0] = (uint16_t)(live & 0xFFFFu); // latched by this read
    }
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
static inline int protocore_fdc2214_dev_place(protocore_fdc2214_dev *d, uint16_t addr)
{
    protocore_fdc2214_dev_init(d);
    return protocore_bus_host_attach(PROTOCORE_BUS_HOST_I2C, addr, d, protocore_fdc2214_dev_txn);
}

#endif // PROTOCORE_HOST_DEVICE_FDC2214_H
