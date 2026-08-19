// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mpr121.c
 * @brief NXP MPR121 capacitive-touch codec - implementation. See mpr121.h.
 *
 * The bring-up register values are the NXP AN3944 / reference defaults (rising/falling/touched
 * filter constants, CONFIG1 0x10, CONFIG2 0x20), with per-electrode touch/release thresholds
 * and an electrode-config (ECR) that enables the electrodes with baseline tracking.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MPR121

#if !PROTOCORE_HAS_BUS
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_MPR121 needs a bus master (an I2C master). Provide one in test/core_setup/hal/<vendor>, or\
 turn the driver off - there is no software stand-in for a part on the other end of a bus."
#endif

#include "mmgr/protomem/protomem.h"
#include "mmgr/secure/secure.h"        // the persistent end this module's state is taken from
#include "server/clock/clock.h" // pcdelay
#include "server/peripherals/i2c.h"
#include "server/peripherals/mpr121/mpr121.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_MPR121_BORROW persistent bytes, or null while the pool was short
} Mpr121OwnCtx;
static Mpr121OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_mpr121_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_secure_persist_span(PROTOCORE_MPR121_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void mpr121_build_init(uint8_t *restrict work);
static void mpr121_touched(uint8_t *restrict work);
static void mpr121_word10(uint8_t *restrict work);

static void mpr121_touched(uint8_t *restrict work)
{
    (void)work;
    uint8_t status_lo = Mpr121.touched_args.status_lo;
    uint8_t status_hi = Mpr121.touched_args.status_hi;

    Mpr121.value = (uint16_t)(((uint16_t)status_lo | ((uint16_t)status_hi << 8)) & 0x0FFF);
}

static void mpr121_is_touched(uint8_t *restrict work)
{
    (void)work;
    uint16_t mask = Mpr121.is_touched_args.mask;
    uint8_t e = Mpr121.is_touched_args.e;

    Mpr121.ok = e < MPR121_ELECTRODES && (mask & (uint16_t)(1u << e)) != 0;
}

static void mpr121_proximity(uint8_t *restrict work)
{
    (void)work;
    uint8_t status_hi = Mpr121.proximity_args.status_hi;

    Mpr121.ok = (status_hi & 0x10) != 0; // status bit 12
}

static void mpr121_overcurrent(uint8_t *restrict work)
{
    (void)work;
    uint8_t status_hi = Mpr121.overcurrent_args.status_hi;

    Mpr121.ok = (status_hi & 0x80) != 0; // status bit 15
}

static void mpr121_word10(uint8_t *restrict work)
{
    (void)work;
    uint8_t lsb = Mpr121.word10_args.lsb;
    uint8_t msb = Mpr121.word10_args.msb;

    Mpr121.value = (uint16_t)(((uint16_t)lsb | ((uint16_t)msb << 8)) & 0x03FF);
}

static void mpr121_build_init(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Mpr121.build_init_args.buf;
    size_t cap = Mpr121.build_init_args.cap;
    uint8_t n = Mpr121.build_init_args.n_electrodes;
    uint8_t touch_thr = Mpr121.build_init_args.touch_thr;
    uint8_t release_thr = Mpr121.build_init_args.release_thr;

    if (!buf || n == 0 || n > MPR121_ELECTRODES)
    {
        Mpr121.n = 0;
        return;
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
        Mpr121.n = 0;
        return;
    }
    size_t i = sizeof(fixed);
    mem.cpy(buf, fixed, sizeof(fixed));
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
    Mpr121.n = i;
}

// ---------------------------------------------------------------------------
// I2C binding
// ---------------------------------------------------------------------------

// All MPR121 I2C-binding state, owned by one instance (internal linkage): the device address, the
// register-pair frame, and the bring-up sequence buffer, so it is one named owner, unreachable
// from any other translation unit. Both buffers are members rather than locals because a transfer
// is composed in place: a write is one register byte and its value, and the bring-up sequence is
// the register/value pairs protocore_mpr121_build_init lays down.
typedef struct
{
    uint8_t addr;
    uint8_t frame[2];
    uint8_t init[MPR121_INIT_MAX];
} Mpr121Ctx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define MPR121_OFF_CTX 0u
static_assert(MPR121_OFF_CTX + sizeof(Mpr121Ctx) <= PROTOCORE_MPR121_BORROW,
              "PROTOCORE_MPR121_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define MPR121_CTX(w) ((Mpr121Ctx *)(void *)((w) + MPR121_OFF_CTX))

// Zero is "no address set yet", which is the default address - stated here rather than on the
// declaration so the context carries no initializer and can live in a borrow that arrives zeroed.
// begin() applies the same default to the address it is handed.
static uint8_t dev_addr(uint8_t *restrict work)
{
    return MPR121_CTX(work)->addr ? MPR121_CTX(work)->addr : (uint8_t)PROTOCORE_MPR121_I2C_ADDR;
}

static proto_bool wr(uint8_t *restrict work, uint8_t reg, uint8_t val)
{
    MPR121_CTX(work)->frame[0] = reg;
    MPR121_CTX(work)->frame[1] = val;
    return protocore_i2c_write(dev_addr(work), MPR121_CTX(work)->frame, sizeof(MPR121_CTX(work)->frame));
}

static proto_bool rd(uint8_t *restrict work, uint8_t reg, uint8_t *out, uint8_t n)
{
    return protocore_i2c_write_read(dev_addr(work), &reg, 1, out, n);
}

static void mpr121_begin(uint8_t *restrict work)
{
    uint8_t addr = Mpr121.begin_args.addr;

    MPR121_CTX(work)->addr = addr ? addr : (uint8_t)PROTOCORE_MPR121_I2C_ADDR;
    protocore_i2c_begin();
    Mpr121.build_init_args.buf = MPR121_CTX(work)->init;
    Mpr121.build_init_args.cap = sizeof(MPR121_CTX(work)->init);
    Mpr121.build_init_args.n_electrodes = MPR121_ELECTRODES;
    Mpr121.build_init_args.touch_thr = PROTOCORE_MPR121_TOUCH_THRESHOLD;
    Mpr121.build_init_args.release_thr = PROTOCORE_MPR121_RELEASE_THRESHOLD;
    mpr121_build_init(work);
    size_t n = Mpr121.n;
    if (n == 0)
    {
        Mpr121.ok = PROTO_FALSE;
        return;
    }
    if (!wr(work, MPR121_CTX(work)->init[0], MPR121_CTX(work)->init[1])) // soft reset first; then let the chip settle
    {
        Mpr121.ok = PROTO_FALSE;
        return;
    }
    pcdelay(1);
    for (size_t i = 2; i + 1 < n; i += 2)
    {
        if (!wr(work, MPR121_CTX(work)->init[i], MPR121_CTX(work)->init[i + 1]))
        {
            Mpr121.ok = PROTO_FALSE;
            return;
        }
    }
    Mpr121.ok = PROTO_TRUE;
}

static void mpr121_read_touched(uint8_t *restrict work)
{

    if (!rd(work, 0x00, MPR121_CTX(work)->frame, 2))
    {
        Mpr121.value = 0;
        return;
    }
    Mpr121.touched_args.status_lo = MPR121_CTX(work)->frame[0];
    Mpr121.touched_args.status_hi = MPR121_CTX(work)->frame[1];
    mpr121_touched(work);
}

static void mpr121_read_filtered(uint8_t *restrict work)
{
    uint8_t e = Mpr121.read_filtered_args.e;

    if (e >= MPR121_ELECTRODES)
    {
        Mpr121.value = 0;
        return;
    }
    if (!rd(work, (uint8_t)(0x04 + 2 * e), MPR121_CTX(work)->frame, 2))
    {
        Mpr121.value = 0;
        return;
    }
    Mpr121.word10_args.lsb = MPR121_CTX(work)->frame[0];
    Mpr121.word10_args.msb = MPR121_CTX(work)->frame[1];
    mpr121_word10(work);
}

Mpr121Ns Mpr121 = {.touched = mpr121_touched,
                   .is_touched = mpr121_is_touched,
                   .proximity = mpr121_proximity,
                   .overcurrent = mpr121_overcurrent,
                   .word10 = mpr121_word10,
                   .build_init = mpr121_build_init,
                   .begin = mpr121_begin,
                   .read_touched = mpr121_read_touched,
                   .read_filtered = mpr121_read_filtered};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MPR121
