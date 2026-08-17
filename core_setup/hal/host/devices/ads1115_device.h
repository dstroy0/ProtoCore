// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host device model: TI ADS1113/ADS1114/ADS1115 16-bit delta-sigma ADC.
//
// The governing document is SBAS444E (May 2009, revised December 2024), cached at
// docs/learn/datasheets/ads1115.pdf. Every constant below carries the section it came from:
//
//   8.1.1  Address Pointer register, P[1:0]: 00b conversion, 01b config, 10b lo_thresh, 11b hi_thresh
//   8.1.2  Conversion register, reset 0000h, read only, 16-bit two's complement
//   8.1.3  Config register, reset 8583h: OS 15, MUX 14:12, PGA 11:9, MODE 8, DR 7:5,
//          COMP_MODE 4, COMP_POL 3, COMP_LAT 2, COMP_QUE 1:0
//   8.1.4  Lo_thresh reset 8000h, Hi_thresh reset 7FFFh
//   7.3.3  Table 7-1, the six full-scale ranges and their LSB sizes
//   7.5.4  Table 7-3, input signal versus ideal output code, clipping at 7FFFh and 8000h
//
// The model holds a register file and four analog inputs. A suite sets the inputs in microvolts,
// the driver composes its own transfers, and the code that comes back is this part's arithmetic on
// them - so read_raw and read_uv are exercised end to end rather than fed a hand-primed queue.
//
// test/ style: plain host C, like the rest of the host HAL.

#ifndef PROTOCORE_HOST_DEVICE_ADS1115_H
#define PROTOCORE_HOST_DEVICE_ADS1115_H

#include "protocore_net_host.h" // protocore_bus_host_attach

#include <stdint.h>

// 8.1.1: the address pointer values, which are also the register file's indices.
#define PROTOCORE_ADS1115_DEV_CONVERSION 0u
#define PROTOCORE_ADS1115_DEV_CONFIG 1u
#define PROTOCORE_ADS1115_DEV_LO_THRESH 2u
#define PROTOCORE_ADS1115_DEV_HI_THRESH 3u

// 8.1.3: the config fields this model acts on.
#define PROTOCORE_ADS1115_DEV_OS 0x8000u
#define PROTOCORE_ADS1115_DEV_MODE 0x0100u

// 8.1.3 reset values.
#define PROTOCORE_ADS1115_DEV_CONFIG_RESET 0x8583u
#define PROTOCORE_ADS1115_DEV_LO_RESET 0x8000u
#define PROTOCORE_ADS1115_DEV_HI_RESET 0x7FFFu

typedef struct
{
    int32_t ain_uv[4]; /**< what is on AIN0..AIN3 in microvolts, referred to GND */
    uint16_t reg[4];   /**< the register file, indexed by the address pointer */
    uint8_t ptr;       /**< the address pointer, P[1:0] */
} protocore_ads1115_dev;

// 7.3.3 Table 7-1: the full-scale range in microvolts for each PGA[2:0]. 110b and 111b repeat the
// 101b range of +/-0.256V.
static const int32_t protocore_ads1115_dev_fs_uv[8] = {6144000, 4096000, 2048000, 1024000,
                                                       512000,  256000,  256000,  256000};

// 8.1.3 MUX[2:0]: the positive and negative input for each setting. 4 is GND.
static const uint8_t protocore_ads1115_dev_mux[8][2] = {{0, 1}, {0, 3}, {1, 3}, {2, 3},
                                                        {0, 4}, {1, 4}, {2, 4}, {3, 4}};

/** @brief Power-up state: 8.1.2 and 8.1.4 reset values, config at 8583h, every input at 0 V. */
static inline void protocore_ads1115_dev_init(protocore_ads1115_dev *d)
{
    for (uint32_t i = 0; i < 4; i++)
    {
        d->ain_uv[i] = 0;
    }
    d->reg[PROTOCORE_ADS1115_DEV_CONVERSION] = 0x0000u;
    d->reg[PROTOCORE_ADS1115_DEV_CONFIG] = PROTOCORE_ADS1115_DEV_CONFIG_RESET;
    d->reg[PROTOCORE_ADS1115_DEV_LO_THRESH] = PROTOCORE_ADS1115_DEV_LO_RESET;
    d->reg[PROTOCORE_ADS1115_DEV_HI_THRESH] = PROTOCORE_ADS1115_DEV_HI_RESET;
    d->ptr = PROTOCORE_ADS1115_DEV_CONVERSION;
}

// One conversion: the input pair MUX selects, divided by the LSB PGA selects. Table 7-3 fixes the
// scale at FS/2^15 per code and clips at 7FFFh and 8000h. The division truncates toward zero, so
// an input that is a whole number of LSBs lands on the code the table publishes.
static inline uint16_t protocore_ads1115_dev_convert(const protocore_ads1115_dev *d)
{
    const uint16_t cfg = d->reg[PROTOCORE_ADS1115_DEV_CONFIG];
    const uint8_t *pair = protocore_ads1115_dev_mux[(cfg >> 12) & 0x7u];
    const int32_t p = pair[0] < 4u ? d->ain_uv[pair[0]] : 0;
    const int32_t n = pair[1] < 4u ? d->ain_uv[pair[1]] : 0;
    int64_t code = (int64_t)(p - n) * 32768 / protocore_ads1115_dev_fs_uv[(cfg >> 9) & 0x7u];
    if (code > 32767)
    {
        code = 32767;
    }
    if (code < -32768)
    {
        code = -32768;
    }
    return (uint16_t)(int16_t)code;
}

// One transfer. A write is the address pointer byte, and for a writable register the two bytes it
// takes; the pointer persists past the write, which is what lets a read follow with no pointer of
// its own (7.5.4). A read returns the selected register MSB first.
static inline uint32_t protocore_ads1115_dev_txn(void *ctx, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen)
{
    protocore_ads1115_dev *d = (protocore_ads1115_dev *)ctx;
    if (w && wlen)
    {
        d->ptr = (uint8_t)(w[0] & 0x03u);
        // 8.1.2 makes the conversion register read only, so a write that selects it only moves the
        // pointer.
        if (wlen >= 3u && d->ptr != PROTOCORE_ADS1115_DEV_CONVERSION)
        {
            const uint16_t v = (uint16_t)(((uint16_t)w[1] << 8) | w[2]);
            d->reg[d->ptr] = v;
            // 8.1.3 OS: writing 1b starts a single conversion, 0b has no effect.
            if (d->ptr == PROTOCORE_ADS1115_DEV_CONFIG && (v & PROTOCORE_ADS1115_DEV_OS))
            {
                d->reg[PROTOCORE_ADS1115_DEV_CONVERSION] = protocore_ads1115_dev_convert(d);
            }
        }
    }
    if (rlen == 0u)
    {
        return 0u;
    }
    // 8.1.3 MODE 0b is continuous conversion, so the conversion register tracks the input without
    // being asked. 1b is single shot: the value stands until the next OS write.
    if (d->ptr == PROTOCORE_ADS1115_DEV_CONVERSION && !(d->reg[PROTOCORE_ADS1115_DEV_CONFIG] & PROTOCORE_ADS1115_DEV_MODE))
    {
        d->reg[PROTOCORE_ADS1115_DEV_CONVERSION] = protocore_ads1115_dev_convert(d);
    }
    uint16_t v = d->reg[d->ptr];
    // 8.1.3 OS on read: 1b is "not currently performing a conversion", which this model always is,
    // since a conversion completes inside the write that started it.
    if (d->ptr == PROTOCORE_ADS1115_DEV_CONFIG)
    {
        v |= PROTOCORE_ADS1115_DEV_OS;
    }
    uint32_t n = 0;
    while (n < rlen)
    {
        r[n] = (n & 1u) ? (uint8_t)(v & 0xFFu) : (uint8_t)(v >> 8);
        n++;
    }
    return n;
}

/** @brief Reset the model and put it on the I2C bus at one address. 0 when the table is full. */
static inline int protocore_ads1115_dev_place(protocore_ads1115_dev *d, uint16_t addr)
{
    protocore_ads1115_dev_init(d);
    return protocore_bus_host_attach(PROTOCORE_BUS_HOST_I2C, addr, d, protocore_ads1115_dev_txn);
}

#endif // PROTOCORE_HOST_DEVICE_ADS1115_H
