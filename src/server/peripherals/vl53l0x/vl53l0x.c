// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file vl53l0x.c
 * @brief VL53L0X time-of-flight ranging codec + ESP32 binding (see vl53l0x.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_VL53L0X

#if !PROTOCORE_HAS_BUS
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_VL53L0X needs a bus master (an I2C master). Provide one in test/core_setup/hal/<vendor>, or\
 turn the driver off - there is no software stand-in for a part on the other end of a bus."
#endif

#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/peripherals/i2c.h"
#include "server/peripherals/vl53l0x/vl53l0x.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_I2C_DEVICE_BORROW persistent bytes
} Vl53l0xOwnCtx;
static Vl53l0xOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_vl53l0x_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_I2C_DEVICE_BORROW).buf;
    }
    return s_own.span;
}

void protocore_vl53l0x_range_mm(uint8_t *restrict work);
void protocore_vl53l0x_range_status(uint8_t *restrict work);
void protocore_vl53l0x_range_valid(uint8_t *restrict work);

void protocore_vl53l0x_range_mm(uint8_t *restrict work)
{
    (void)work;
    uint8_t hi = Vl53l0xV.range_mm_args.hi;
    uint8_t lo = Vl53l0xV.range_mm_args.lo;

    Vl53l0xV.mm = (uint16_t)((hi << 8) | lo);
}

void protocore_vl53l0x_data_ready(uint8_t *restrict work)
{
    (void)work;
    uint8_t interrupt_status = Vl53l0xV.data_ready_args.interrupt_status;

    Vl53l0xV.ok = (interrupt_status & 0x07) != 0;
}

void protocore_vl53l0x_range_status(uint8_t *restrict work)
{
    (void)work;
    uint8_t range_status_reg = Vl53l0xV.range_status_args.range_status_reg;

    Vl53l0xV.status = (uint8_t)((range_status_reg >> 3) & 0x0F);
}

void protocore_vl53l0x_range_valid(uint8_t *restrict work)
{
    uint8_t range_status_reg = Vl53l0xV.range_valid_args.range_status_reg;

    Vl53l0xV.range_status_args.range_status_reg = range_status_reg;
    protocore_vl53l0x_range_status(work);
    Vl53l0xV.ok = Vl53l0xV.status == VL53L0X_RANGE_VALID;
}

// All VL53L0X I2C-binding state, owned by one instance (internal linkage): the device address, the
// register-pair frame, and the result block, so it is one named owner, unreachable from any other
// translation unit. Both buffers are members rather than locals because a transfer is composed in
// place: a write is one register byte and its value, and a range read is the twelve result
// registers from RESULT_RANGE_STATUS onward.
typedef struct
{
    uint8_t addr;
    uint8_t frame[2];
    uint8_t result[12];
} Vl53l0xCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define VL53L0X_OFF_CTX 0u
static_assert(VL53L0X_OFF_CTX + sizeof(Vl53l0xCtx) <= PROTOCORE_I2C_DEVICE_BORROW,
              "PROTOCORE_I2C_DEVICE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(VL53L0X_OFF_CTX % _Alignof(Vl53l0xCtx) == 0,
              "VL53L0X_OFF_CTX is not a multiple of alignof(Vl53l0xCtx) - VL53L0X_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define VL53L0X_CTX(w) ((Vl53l0xCtx *)(void *)((w) + VL53L0X_OFF_CTX))

// Zero is "no address set yet", which is the address the part answers to out of reset - stated
// here rather than on the declaration so the context carries no initializer and can live in a
// borrow that arrives zeroed. begin() applies the same default to the address it is handed.
static uint8_t dev_addr(uint8_t *restrict work)
{
    return VL53L0X_CTX(work)->addr ? VL53L0X_CTX(work)->addr : (uint8_t)PROTOCORE_VL53L0X_I2C_ADDR;
}

static proto_bool w8(uint8_t *restrict work, uint8_t reg, uint8_t val)
{
    VL53L0X_CTX(work)->frame[0] = reg;
    VL53L0X_CTX(work)->frame[1] = val;
    return protocore_i2c_write(dev_addr(work), VL53L0X_CTX(work)->frame, sizeof(VL53L0X_CTX(work)->frame));
}

static proto_bool r8(uint8_t *restrict work, uint8_t reg, uint8_t *val)
{
    return protocore_i2c_write_read(dev_addr(work), &reg, 1, val, 1);
}

static proto_bool rn(uint8_t *restrict work, uint8_t reg, uint8_t *buf, uint8_t n)
{
    return protocore_i2c_write_read(dev_addr(work), &reg, 1, buf, n);
}

void protocore_vl53l0x_begin(uint8_t *restrict work)
{
    uint8_t addr = Vl53l0xV.begin_args.addr;

    protocore_i2c_begin();
    VL53L0X_CTX(work)->addr = addr ? addr : (uint8_t)PROTOCORE_VL53L0X_I2C_ADDR;
    uint8_t id = 0;
    if (!r8(work, VL53L0X_REG_IDENTIFICATION_MODEL_ID, &id) || id != VL53L0X_MODEL_ID)
    {
        Vl53l0xV.ok = PROTO_FALSE;
        return;
    }
    Vl53l0xV.ok = w8(work, VL53L0X_REG_SYSRANGE_START, 0x02); // continuous back-to-back ranging
}

void protocore_vl53l0x_read_mm(uint8_t *restrict work)
{
    uint16_t *mm = Vl53l0xV.read_mm_args.mm;

    if (!mm)
    {
        Vl53l0xV.ok = PROTO_FALSE;
        return;
    }
    // The read fills irq and the test reads it, so the two are separate statements: staged above
    // the transfer, data_ready would run on the byte from before it.
    uint8_t irq = 0;
    if (!r8(work, VL53L0X_REG_RESULT_INTERRUPT_STATUS, &irq))
    {
        Vl53l0xV.ok = PROTO_FALSE;
        return;
    }
    Vl53l0xV.data_ready_args.interrupt_status = irq;
    protocore_vl53l0x_data_ready(work);
    if (!Vl53l0xV.ok)
    {
        Vl53l0xV.ok = PROTO_FALSE;
        return;
    }
    if (!rn(work, VL53L0X_REG_RESULT_RANGE_STATUS, VL53L0X_CTX(work)->result,
            (uint8_t)sizeof(VL53L0X_CTX(work)->result)))
    {
        Vl53l0xV.ok = PROTO_FALSE;
        return;
    }
    Vl53l0xV.range_valid_args.range_status_reg = VL53L0X_CTX(work)->result[0];
    protocore_vl53l0x_range_valid(work);
    proto_bool valid = Vl53l0xV.ok;
    Vl53l0xV.range_mm_args.hi = VL53L0X_CTX(work)->result[10];
    Vl53l0xV.range_mm_args.lo = VL53L0X_CTX(work)->result[11];
    protocore_vl53l0x_range_mm(work);
    *mm = Vl53l0xV.mm; // distance at RESULT_RANGE_STATUS + 10/11
    (void)w8(work, VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
    Vl53l0xV.ok = valid;
}

/** @brief The operands and the outcome. */
Vl53l0xVars Vl53l0xV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_VL53L0X
