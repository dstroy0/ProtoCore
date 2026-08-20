// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host device model: NXP / Freescale MPR121 12-channel capacitive touch controller.
//
// The governing document is the MPR121 data sheet, cached at docs/learn/datasheets/mpr121.pdf.
// Every constant below carries the section it came from:
//
//   Table 2  the register map: touch status 00h..01h, electrode filtered data 04h..1Dh,
//            baseline 1Eh..2Ah, filter configuration 2Bh..35h, thresholds 41h..5Ah,
//            debounce 5Bh, CONFIG1 5Ch, CONFIG2 5Dh, ECR 5Eh, soft reset 80h
//   5.1      "the register write operation can only be done in Stop Mode. The ECR (0x5E) and
//            GPIO/LED control registers (0x73~0x7A) can be written at anytime." Stop Mode is
//            ELEPROX_EN and ELE_EN all zero.
//   5.2      touch status 00h/01h are read only: ELE0..ELE7, then ELE8..ELE11, ELEPROX at bit 4
//            and OVCF at bit 7. OVCF is read and write, and writing 1 clears it.
//   5.3      electrode filtered data is 10 bits, low byte at 04h + 2e and bits 9:8 in the byte
//            above it, read only, and a multibyte read walks them in order
//   5.11     ECR 5Eh, reset 00h: CL 7:6, ELEPROX_EN 5:4, ELE_EN 3:0. Non-zero enables are Run Mode.
//   5.13     soft reset 80h: writing 63h resets everything but the I2C module, the same as POR
//
// The discarded write is the one worth having a model for. A driver that configures the filter
// constants without stopping the part first still puts the right bytes on the wire, so a capture
// assertion passes and the part keeps its old configuration. Here the registers simply do not take.
//
// test/ style: plain host C, like the rest of the host HAL.

#ifndef PROTOCORE_HOST_DEVICE_MPR121_H
#define PROTOCORE_HOST_DEVICE_MPR121_H

#include "protocore_net_host.h" // protocore_bus_host_attach

#include <stdint.h>

// Table 2: the addresses this model acts on.
#define PROTOCORE_MPR121_DEV_TOUCH_LO 0x00u
#define PROTOCORE_MPR121_DEV_TOUCH_HI 0x01u
#define PROTOCORE_MPR121_DEV_FILTERED0 0x04u
#define PROTOCORE_MPR121_DEV_GPIO_FIRST 0x73u
#define PROTOCORE_MPR121_DEV_GPIO_LAST 0x7Au
#define PROTOCORE_MPR121_DEV_ECR 0x5Eu
#define PROTOCORE_MPR121_DEV_SOFT_RESET 0x80u
#define PROTOCORE_MPR121_DEV_SOFT_RESET_KEY 0x63u

// 5.11: the ECR fields. Run Mode is either enable field non-zero.
#define PROTOCORE_MPR121_DEV_RUN 0x3Fu

// 5.2: the flags in the high status byte.
#define PROTOCORE_MPR121_DEV_ELEPROX 0x1000u
#define PROTOCORE_MPR121_DEV_OVCF 0x80u

#define PROTOCORE_MPR121_DEV_ELECTRODES 13u // twelve, plus the proximity channel

typedef struct
{
    uint16_t touch;                                     /**< which channels are being touched, ELEPROX at bit 12 */
    uint16_t filtered[PROTOCORE_MPR121_DEV_ELECTRODES]; /**< the 10-bit reading on each channel */
    uint8_t reg[256];                                   /**< the register file, addressed as the part addresses it */
    uint8_t ptr;                                        /**< the register the last transfer named */
} protocore_mpr121_dev;

/** @brief Power-up state: 5.11 puts the ECR at 00h, which is Stop Mode, and everything else at 0. */
static inline void protocore_mpr121_dev_init(protocore_mpr121_dev *d)
{
    d->touch = 0;
    for (uint32_t i = 0; i < PROTOCORE_MPR121_DEV_ELECTRODES; i++)
    {
        d->filtered[i] = 0;
    }
    for (uint32_t i = 0; i < 256u; i++)
    {
        d->reg[i] = 0;
    }
    d->ptr = 0;
}

/** @brief 5.1: Run Mode is any ELEPROX_EN or ELE_EN bit set; everything else is Stop Mode. */
static inline int protocore_mpr121_dev_running(const protocore_mpr121_dev *d)
{
    return (d->reg[PROTOCORE_MPR121_DEV_ECR] & PROTOCORE_MPR121_DEV_RUN) != 0;
}

/** @brief 5.11 ELE_EN: how many electrodes the ECR enabled, 12 for any value past it. */
static inline uint8_t protocore_mpr121_dev_enabled(const protocore_mpr121_dev *d)
{
    const uint8_t n = (uint8_t)(d->reg[PROTOCORE_MPR121_DEV_ECR] & 0x0Fu);
    return n > 12u ? 12u : n;
}

// 5.2 / 5.3: the registers that are measurements rather than settings. They are recomputed per read
// because the part scans continuously in Run Mode, and read as zero in Stop Mode, where nothing is
// measured at all.
static inline void protocore_mpr121_dev_measure(protocore_mpr121_dev *d)
{
    const uint16_t live = protocore_mpr121_dev_running(d) ? d->touch : 0u;
    d->reg[PROTOCORE_MPR121_DEV_TOUCH_LO] = (uint8_t)(live & 0xFFu);
    d->reg[PROTOCORE_MPR121_DEV_TOUCH_HI] =
        (uint8_t)(((live >> 8) & 0x0Fu) | (live & PROTOCORE_MPR121_DEV_ELEPROX ? 0x10u : 0u) |
                  (d->reg[PROTOCORE_MPR121_DEV_TOUCH_HI] & PROTOCORE_MPR121_DEV_OVCF));
    for (uint32_t e = 0; e < PROTOCORE_MPR121_DEV_ELECTRODES; e++)
    {
        const uint16_t v = protocore_mpr121_dev_running(d) ? (uint16_t)(d->filtered[e] & 0x03FFu) : 0u;
        d->reg[PROTOCORE_MPR121_DEV_FILTERED0 + 2u * e] = (uint8_t)(v & 0xFFu);
        d->reg[PROTOCORE_MPR121_DEV_FILTERED0 + 2u * e + 1u] = (uint8_t)(v >> 8);
    }
}

// One byte into the register the pointer names, subject to 5.1's Stop Mode rule.
static inline void protocore_mpr121_dev_store(protocore_mpr121_dev *d, uint8_t v)
{
    if (d->ptr == PROTOCORE_MPR121_DEV_SOFT_RESET)
    {
        // 5.13: 63h resets everything but the I2C module. What is on the pads is not a register.
        if (v == PROTOCORE_MPR121_DEV_SOFT_RESET_KEY)
        {
            const uint16_t touch = d->touch;
            protocore_mpr121_dev_init(d);
            d->touch = touch;
        }
        return;
    }
    if (d->ptr == PROTOCORE_MPR121_DEV_TOUCH_HI)
    {
        // 5.2: the status bytes are read only except that writing 1 to OVCF clears it.
        if (v & PROTOCORE_MPR121_DEV_OVCF)
        {
            d->reg[PROTOCORE_MPR121_DEV_TOUCH_HI] &= (uint8_t)~PROTOCORE_MPR121_DEV_OVCF;
        }
        return;
    }
    if (d->ptr == PROTOCORE_MPR121_DEV_TOUCH_LO || (d->ptr >= PROTOCORE_MPR121_DEV_FILTERED0 && d->ptr <= 0x2Au))
    {
        return; // 5.2 / 5.3: status, filtered data and baseline outputs are read only
    }
    // 5.1: the ECR and the GPIO / LED control registers take a write at any time; everything else
    // only in Stop Mode.
    if (d->ptr != PROTOCORE_MPR121_DEV_ECR &&
        !(d->ptr >= PROTOCORE_MPR121_DEV_GPIO_FIRST && d->ptr <= PROTOCORE_MPR121_DEV_GPIO_LAST) &&
        protocore_mpr121_dev_running(d))
    {
        return;
    }
    d->reg[d->ptr] = v;
}

// One transfer. The first byte written is the register; the bytes after it land there and the
// pointer advances past each one. A read returns from the register the pointer names, walking on
// so a multibyte read takes a low byte and the high byte above it (5.3).
static inline uint32_t protocore_mpr121_dev_txn(void *ctx, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen)
{
    protocore_mpr121_dev *d = (protocore_mpr121_dev *)ctx;
    if (w && wlen)
    {
        d->ptr = w[0];
        for (uint32_t i = 1; i < wlen; i++)
        {
            protocore_mpr121_dev_store(d, w[i]);
            d->ptr++;
        }
    }
    if (rlen == 0u)
    {
        return 0u;
    }
    protocore_mpr121_dev_measure(d);
    uint32_t n = 0;
    while (n < rlen)
    {
        r[n++] = d->reg[d->ptr++];
    }
    return n;
}

/** @brief Reset the model and put it on the I2C bus at one address. 0 when the table is full. */
static inline int protocore_mpr121_dev_place(protocore_mpr121_dev *d, uint16_t addr)
{
    protocore_mpr121_dev_init(d);
    return protocore_bus_host_attach(PROTOCORE_BUS_HOST_I2C, addr, d, protocore_mpr121_dev_txn);
}

#endif // PROTOCORE_HOST_DEVICE_MPR121_H
