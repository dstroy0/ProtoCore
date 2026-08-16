// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_hw_reg.c
 * @brief The register window behind the host accessors. See host_hw_reg.h.
 *
 * Open addressed, linear probed, fixed extent, no heap. Peripheral addresses are word aligned and
 * clustered in a few windows, so the index is the address shifted past its two dead low bits and
 * masked - a shift and an AND, which every target in the list has.
 *
 * A slot holds the register's four octets rather than a word, because that is what a bus carries. A
 * write serializes the value into them in the modelled width and order; a read assembles it back the
 * same way. At 32 bits little-endian the pair is the identity, which is the setting a native build
 * takes unless an env states otherwise.
 */

#include "core_setup/hal/host/host_hw_reg.h"

#if PROTOCORE_HOST

PROTOCORE_BEGIN_DECLS

// Every slot of the window in one owned context (internal linkage): the address a slot answers for,
// whether it has been claimed, the four octets themselves, the discard slot a full window falls back
// on, and the bus the model currently carries accesses on.
typedef struct
{
    uintptr_t addr[PROTOCORE_HW_REG_HOST_SLOTS];
    uint8_t octet[PROTOCORE_HW_REG_HOST_SLOTS][4];
    proto_bool used[PROTOCORE_HW_REG_HOST_SLOTS];
    uint8_t discard[4];
    unsigned bits;
    proto_bool be;
} HwRegHostCtx;
static HwRegHostCtx s_hw = {{0}, {{0}}, {0}, {0}, PROTOCORE_HW_BUS_BITS, PROTOCORE_HW_BUS_BE};

// The four octets a register lives in, claimed on first sight.
static uint8_t *slot_of(uintptr_t addr)
{
    const uintptr_t mask = (uintptr_t)PROTOCORE_HW_REG_HOST_SLOTS - 1u;
    uintptr_t i = (addr >> 2) & mask; // word index, folded into the window
    for (uint32_t probe = 0; probe < PROTOCORE_HW_REG_HOST_SLOTS; probe++)
    {
        if (!s_hw.used[i])
        {
            s_hw.used[i] = PROTO_TRUE;
            s_hw.addr[i] = addr;
            s_hw.octet[i][0] = 0u; // an address seen for the first time reads zero
            s_hw.octet[i][1] = 0u;
            s_hw.octet[i][2] = 0u;
            s_hw.octet[i][3] = 0u;
            return s_hw.octet[i];
        }
        if (s_hw.addr[i] == addr)
        {
            return s_hw.octet[i];
        }
        i = (i + 1u) & mask;
    }
    return s_hw.discard; // window full: absorb the access rather than alias another register
}

// Where octet @p k of the value sits once the bus has carried it. A unit is bits/8 octets wide; the
// value's octets keep their order inside a unit on a little-endian bus and reverse on a big-endian
// one. A bus wider than the register carries it in one unit, so 64 behaves as 32 does for the
// register's own four octets and differs only in the unit a driver must align to.
static unsigned carried_index(unsigned k)
{
    unsigned unit = (s_hw.bits >= 32u) ? 4u : (s_hw.bits / 8u); // octets per unit, capped at the register
    if (!s_hw.be)
    {
        return k;
    }
    unsigned base = (k / unit) * unit; // the unit this octet belongs to
    unsigned off = k % unit;
    return base + (unit - 1u - off); // reversed inside the unit
}

uint32_t protocore_hw_reg_host_read(uintptr_t addr)
{
    const uint8_t *o = slot_of(addr);
    uint32_t v = 0;
    for (unsigned k = 0; k < 4u; k++)
    {
        v |= (uint32_t)o[carried_index(k)] << (8u * k);
    }
    return v;
}

void protocore_hw_reg_host_write(uintptr_t addr, uint32_t val)
{
    uint8_t *o = slot_of(addr);
    for (unsigned k = 0; k < 4u; k++)
    {
        o[carried_index(k)] = (uint8_t)(val >> (8u * k));
    }
}

void protocore_hw_reg_host_reset(void)
{
    for (uint32_t i = 0; i < PROTOCORE_HW_REG_HOST_SLOTS; i++)
    {
        s_hw.used[i] = PROTO_FALSE;
        s_hw.addr[i] = 0u;
        s_hw.octet[i][0] = 0u;
        s_hw.octet[i][1] = 0u;
        s_hw.octet[i][2] = 0u;
        s_hw.octet[i][3] = 0u;
    }
    s_hw.discard[0] = 0u;
    s_hw.discard[1] = 0u;
    s_hw.discard[2] = 0u;
    s_hw.discard[3] = 0u;
}

void protocore_hw_reg_host_bus(unsigned bits, proto_bool be)
{
    if (bits == 16u || bits == 32u || bits == 64u)
    {
        s_hw.bits = bits;
    }
    s_hw.be = be ? PROTO_TRUE : PROTO_FALSE;
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_HOST
