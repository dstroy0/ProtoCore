// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mpr121.cpp
 * @brief NXP MPR121 capacitive-touch codec - implementation. See mpr121.h.
 *
 * The bring-up register values are the NXP AN3944 / reference defaults (rising/falling/touched
 * filter constants, CONFIG1 0x10, CONFIG2 0x20), with per-electrode touch/release thresholds
 * and an electrode-config (ECR) that enables the electrodes with baseline tracking.
 */

#include "services/peripherals/mpr121/mpr121.h"
#include "protocore_config.h"
#include "services/system/clock.h" // pcdelay

#if PC_ENABLE_MPR121

#include <string.h>

#if defined(ARDUINO)
#include "services/peripherals/i2c.h"
#include <Arduino.h>
#include <Wire.h>
#endif
uint16_t pc_mpr121_touched(uint8_t status_lo, uint8_t status_hi)
{
    return (uint16_t)(((uint16_t)status_lo | ((uint16_t)status_hi << 8)) & 0x0FFF);
}

bool pc_mpr121_is_touched(uint16_t mask, uint8_t e)
{
    return e < MPR121_ELECTRODES && (mask & (uint16_t)(1u << e)) != 0;
}

bool pc_mpr121_proximity(uint8_t status_hi)
{
    return (status_hi & 0x10) != 0; // status bit 12
}

bool pc_mpr121_overcurrent(uint8_t status_hi)
{
    return (status_hi & 0x80) != 0; // status bit 15
}

uint16_t pc_mpr121_word10(uint8_t lsb, uint8_t msb)
{
    return (uint16_t)(((uint16_t)lsb | ((uint16_t)msb << 8)) & 0x03FF);
}

size_t pc_mpr121_build_init(uint8_t *buf, size_t cap, uint8_t n, uint8_t touch_thr, uint8_t release_thr)
{
    if (!buf || n == 0 || n > MPR121_ELECTRODES)
    {
        return 0;
    }
    // Reset, ECR-stop, then the rising / falling / touched baseline-filter defaults.
    static const uint8_t fixed[] = {
        0x80, 0x63,             // soft reset
        0x5E, 0x00,             // ECR = stop (config only allowed while stopped)
        0x2B, 0x01, 0x2C, 0x01, // MHDR, NHDR
        0x2D, 0x0E, 0x2E, 0x00, // NCLR, FDLR
        0x2F, 0x01, 0x30, 0x05, // MHDF, NHDF
        0x31, 0x01, 0x32, 0x00, // NCLF, FDLF
        0x33, 0x00, 0x34, 0x00, // NHDT, NCLT
        0x35, 0x00,             // FDLT
    };
    size_t need = sizeof(fixed) + (size_t)n * 4 + 8;
    if (cap < need)
    {
        return 0;
    }
    size_t i = sizeof(fixed);
    memcpy(buf, fixed, sizeof(fixed));
    for (uint8_t e = 0; e < n; e++)
    {
        buf[i++] = (uint8_t)(0x41 + 2 * e); // touch threshold reg
        buf[i++] = touch_thr;
        buf[i++] = (uint8_t)(0x42 + 2 * e); // release threshold reg
        buf[i++] = release_thr;
    }
    buf[i++] = 0x5B;
    buf[i++] = 0x00; // debounce
    buf[i++] = 0x5C;
    buf[i++] = 0x10; // CONFIG1
    buf[i++] = 0x5D;
    buf[i++] = 0x20; // CONFIG2
    buf[i++] = 0x5E;
    buf[i++] = (uint8_t)(0x80 | n); // ECR: CL=baseline tracking, ELE_EN=n (written last)
    return i;
}

// ---------------------------------------------------------------------------
// I2C binding
// ---------------------------------------------------------------------------

#if defined(ARDUINO)

namespace
{
// All MPR121 I2C-binding state, owned by one instance (internal linkage): the device address,
// so it is one named owner, unreachable from any other translation unit.
struct Mpr121Ctx
{
    uint8_t addr = PC_MPR121_I2C_ADDR;
};
Mpr121Ctx s_mpr;

bool wr(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(s_mpr.addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool rd(uint8_t reg, uint8_t *out, uint8_t n)
{
    Wire.beginTransmission(s_mpr.addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0)
    {
        return false;
    }
    if (Wire.requestFrom((int)s_mpr.addr, (int)n) != (int)n)
    {
        return false;
    }
    for (uint8_t i = 0; i < n; i++)
    {
        out[i] = (uint8_t)Wire.read();
    }
    return true;
}
} // namespace

bool pc_mpr121_begin(uint8_t addr)
{
    s_mpr.addr = addr ? addr : (uint8_t)PC_MPR121_I2C_ADDR;
    pc_i2c_begin();
    uint8_t seq[MPR121_INIT_MAX];
    size_t n = pc_mpr121_build_init(seq, sizeof(seq), MPR121_ELECTRODES, PC_MPR121_TOUCH_THRESHOLD,
                                    PC_MPR121_RELEASE_THRESHOLD);
    if (n == 0)
    {
        return false;
    }
    if (!wr(seq[0], seq[1])) // soft reset first; then let the chip settle
    {
        return false;
    }
    pcdelay(1);
    for (size_t i = 2; i + 1 < n; i += 2)
    {
        if (!wr(seq[i], seq[i + 1]))
        {
            return false;
        }
    }
    return true;
}

uint16_t pc_mpr121_read_touched()
{
    uint8_t s[2] = {0, 0};
    if (!rd(0x00, s, 2))
    {
        return 0;
    }
    return pc_mpr121_touched(s[0], s[1]);
}

uint16_t pc_mpr121_read_filtered(uint8_t e)
{
    if (e >= MPR121_ELECTRODES)
    {
        return 0;
    }
    uint8_t d[2] = {0, 0};
    if (!rd((uint8_t)(0x04 + 2 * e), d, 2))
    {
        return 0;
    }
    return pc_mpr121_word10(d[0], d[1]);
}

#else // host build: no I2C. The decode + init-sequence builder above are host-tested.

bool pc_mpr121_begin(uint8_t)
{
    return false;
}
uint16_t pc_mpr121_read_touched()
{
    return 0;
}
uint16_t pc_mpr121_read_filtered(uint8_t)
{
    return 0;
}

#endif // ARDUINO

#endif // PC_ENABLE_MPR121
