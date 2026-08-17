// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host device model: NXP PCA9685 16-channel 12-bit PWM controller.
//
// The governing document is the PCA9685 product data sheet Rev. 4 (16 April 2015), cached at
// docs/learn/datasheets/pca9685.pdf. Every constant below carries the section it came from:
//
//   7.3    Table 4, the register map: MODE1 00h, MODE2 01h, SUBADR1..3 02h..04h, ALLCALLADR 05h,
//          LED0_ON_L 06h with four registers per channel, ALL_LED FAh..FDh, PRE_SCALE FEh
//   7.3.1  Table 5, MODE1 reset 11h: RESTART 7, EXTCLK 6, AI 5, SLEEP 4 (1* at power-up),
//          SUB1 3, SUB2 2, SUB3 1, ALLCALL 0 (1* at power-up). A write of 1 to RESTART clears it;
//          a write of 0 has no effect.
//   7.3.2  Table 6, MODE2 reset 04h: INVRT 4, OCH 3, OUTDRV 2 (1* at power-up), OUTNE 1:0
//   7.3.3  each channel is a 12-bit ON count and a 12-bit OFF count, 0 to 4095
//   7.3.4  Table 8, PRE_SCALE reset 1Eh, which is 200 Hz
//   7.3.5  Eq 1: prescale = round(osc / (4096 * update_rate)) - 1, and the hardware forces a
//          minimum of 3
//   Table 4 note [1]: writes to PRE_SCALE are blocked while the SLEEP bit is logic 0
//   7.3.1 note [1]: with AI set, the control register increments after every read or write
//
// The blocked PRE_SCALE write is the one worth having a model for. A driver that forgets to sleep
// first still puts the right bytes on the wire, so a capture assertion passes and the part keeps
// its old frequency. Here the register simply does not take, and the reading says so.
//
// test/ style: plain host C, like the rest of the host HAL.

#ifndef PROTOCORE_HOST_DEVICE_PCA9685_H
#define PROTOCORE_HOST_DEVICE_PCA9685_H

#include "protocore_net_host.h" // protocore_bus_host_attach

#include <stdint.h>

// 7.3 Table 4: the addresses this model acts on.
#define PROTOCORE_PCA9685_DEV_MODE1 0x00u
#define PROTOCORE_PCA9685_DEV_MODE2 0x01u
#define PROTOCORE_PCA9685_DEV_ALLCALLADR 0x05u
#define PROTOCORE_PCA9685_DEV_LED0_ON_L 0x06u
#define PROTOCORE_PCA9685_DEV_PRE_SCALE 0xFEu

// 7.3.1 Table 5 / 7.3.2 Table 6 / 7.3.4 Table 8 reset values.
#define PROTOCORE_PCA9685_DEV_MODE1_RESET 0x11u
#define PROTOCORE_PCA9685_DEV_MODE2_RESET 0x04u
#define PROTOCORE_PCA9685_DEV_ALLCALLADR_RESET 0xE0u
#define PROTOCORE_PCA9685_DEV_PRE_SCALE_RESET 0x1Eu

// 7.3.1 Table 5: the MODE1 bits this model acts on.
#define PROTOCORE_PCA9685_DEV_RESTART 0x80u
#define PROTOCORE_PCA9685_DEV_AI 0x20u
#define PROTOCORE_PCA9685_DEV_SLEEP 0x10u

#define PROTOCORE_PCA9685_DEV_CHANNELS 16u

typedef struct
{
    uint8_t reg[256]; /**< the whole register file, addressed as the part addresses it */
    uint8_t ptr;      /**< the control register the last transfer named */
} protocore_pca9685_dev;

/** @brief Power-up state: 7.3.1 / 7.3.2 / 7.3.4 reset values, every PWM count 0. */
static inline void protocore_pca9685_dev_init(protocore_pca9685_dev *d)
{
    for (uint32_t i = 0; i < 256u; i++)
    {
        d->reg[i] = 0;
    }
    d->reg[PROTOCORE_PCA9685_DEV_MODE1] = PROTOCORE_PCA9685_DEV_MODE1_RESET;
    d->reg[PROTOCORE_PCA9685_DEV_MODE2] = PROTOCORE_PCA9685_DEV_MODE2_RESET;
    d->reg[PROTOCORE_PCA9685_DEV_ALLCALLADR] = PROTOCORE_PCA9685_DEV_ALLCALLADR_RESET;
    d->reg[PROTOCORE_PCA9685_DEV_PRE_SCALE] = PROTOCORE_PCA9685_DEV_PRE_SCALE_RESET;
    d->ptr = PROTOCORE_PCA9685_DEV_MODE1;
}

// One channel's four registers: ON_L, ON_H, OFF_L, OFF_H at 06h + 4 * channel (7.3 Table 4).
static inline uint8_t protocore_pca9685_dev_channel_reg(uint8_t channel)
{
    return (uint8_t)(PROTOCORE_PCA9685_DEV_LED0_ON_L + 4u * channel);
}

/** @brief A channel's 12-bit ON count. Bit 4 of the _H register is the full-ON flag, not a count. */
static inline uint16_t protocore_pca9685_dev_on(const protocore_pca9685_dev *d, uint8_t channel)
{
    const uint8_t base = protocore_pca9685_dev_channel_reg(channel);
    return (uint16_t)(((uint16_t)(d->reg[base + 1u] & 0x0Fu) << 8) | d->reg[base]);
}

/** @brief A channel's 12-bit OFF count. */
static inline uint16_t protocore_pca9685_dev_off(const protocore_pca9685_dev *d, uint8_t channel)
{
    const uint8_t base = protocore_pca9685_dev_channel_reg(channel);
    return (uint16_t)(((uint16_t)(d->reg[base + 3u] & 0x0Fu) << 8) | d->reg[base + 2u]);
}

/**
 * @brief The output frequency the programmed prescale gives, per 7.3.5 Eq 1 solved for the rate.
 *
 * Truncating, and a rounded prescale does not invert exactly: prescale 30 is what Eq 1 returns for
 * a 200 Hz request and gives 196.9 Hz.
 */
static inline uint32_t protocore_pca9685_dev_freq_hz(const protocore_pca9685_dev *d)
{
    return 25000000u / (4096u * ((uint32_t)d->reg[PROTOCORE_PCA9685_DEV_PRE_SCALE] + 1u));
}

// One byte into the register the control pointer names. The two registers that do not simply store
// what arrives are handled first.
static inline void protocore_pca9685_dev_store(protocore_pca9685_dev *d, uint8_t v)
{
    if (d->ptr == PROTOCORE_PCA9685_DEV_PRE_SCALE && !(d->reg[PROTOCORE_PCA9685_DEV_MODE1] & PROTOCORE_PCA9685_DEV_SLEEP))
    {
        return; // Table 4 note [1]: blocked while SLEEP is logic 0
    }
    if (d->ptr == PROTOCORE_PCA9685_DEV_MODE1)
    {
        // 7.3.1 Table 5: RESTART is read-only except that writing 1 clears it, so it never takes
        // the written bit. Every other bit stores.
        const uint8_t restart = (uint8_t)(v & PROTOCORE_PCA9685_DEV_RESTART
                                              ? 0u
                                              : (d->reg[PROTOCORE_PCA9685_DEV_MODE1] & PROTOCORE_PCA9685_DEV_RESTART));
        d->reg[PROTOCORE_PCA9685_DEV_MODE1] = (uint8_t)((v & (uint8_t)~PROTOCORE_PCA9685_DEV_RESTART) | restart);
        return;
    }
    d->reg[d->ptr] = v;
}

// One transfer. The first byte written is the control register; the bytes after it land there, and
// the pointer advances past each one when AI is set (7.3.1 note [1]). A read returns the register
// the pointer names, advancing the same way.
static inline uint32_t protocore_pca9685_dev_txn(void *ctx, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen)
{
    protocore_pca9685_dev *d = (protocore_pca9685_dev *)ctx;
    if (w && wlen)
    {
        d->ptr = w[0];
        for (uint32_t i = 1; i < wlen; i++)
        {
            protocore_pca9685_dev_store(d, w[i]);
            if (d->reg[PROTOCORE_PCA9685_DEV_MODE1] & PROTOCORE_PCA9685_DEV_AI)
            {
                d->ptr++;
            }
        }
    }
    uint32_t n = 0;
    while (n < rlen)
    {
        r[n++] = d->reg[d->ptr];
        if (d->reg[PROTOCORE_PCA9685_DEV_MODE1] & PROTOCORE_PCA9685_DEV_AI)
        {
            d->ptr++;
        }
    }
    return n;
}

/** @brief Reset the model and put it on the I2C bus at one address. 0 when the table is full. */
static inline int protocore_pca9685_dev_place(protocore_pca9685_dev *d, uint16_t addr)
{
    protocore_pca9685_dev_init(d);
    return protocore_bus_host_attach(PROTOCORE_BUS_HOST_I2C, addr, d, protocore_pca9685_dev_txn);
}

#endif // PROTOCORE_HOST_DEVICE_PCA9685_H
