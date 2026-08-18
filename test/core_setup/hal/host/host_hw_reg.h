// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_hw_reg.h
 * @brief The host arm of the register bus: an address keyed store with a modelled width and order.
 *
 * On silicon a register access is a load or a store at a peripheral address, and the bus decides how
 * a 32-bit register is carried: in one beat or in two halves or inside a wider one, low unit first or
 * high unit first. A host build has no such bus, so the same access has to land somewhere real for a
 * capability arm to be compiled and run off target. This supplies that, and models the carriage as
 * well as the storage: PROTOCORE_HW_BUS_BITS selects 16, 32 or 64, PROTOCORE_HW_BUS_BE selects the
 * order, and a register written under one setting and read under another comes back rearranged
 * exactly as it would on the part.
 *
 * A READ AND A WRITE, NOT AN LVALUE
 * A narrow or byte-swapped access cannot be a dereference, so the accessor is a pair. On silicon
 * both forms are the direct volatile access they always were, so nothing is paid for the model
 * anywhere but the host.
 *
 * What this gives and what it does not. It gives the accesses themselves - the bring-up order, the
 * read-modify-writes, the bounded polls, the state save and restore around each block, and now the
 * width and order a driver assumed. It does not give peripheral BEHAVIOR: a store answers a read with
 * what was last written to it, so a status bit does not clear itself and a compression does not
 * happen. An arm whose correctness depends on the peripheral acting is checked against the function
 * level host arms beside this file, or on the part.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HOST_HW_REG_H
#define PROTOCORE_HOST_HW_REG_H

#include "protocore_config.h" // the entry point: PROTOCORE_HOST and the widths

#if PROTOCORE_HOST

PROTOCORE_BEGIN_DECLS

/** @brief Slots the window holds. A power of two: the index is a mask, never a modulo. */
#ifndef PROTOCORE_HW_REG_HOST_SLOTS
#define PROTOCORE_HW_REG_HOST_SLOTS 256u
#endif

/** @brief Bus width in bits the model carries a register in: 16, 32 or 64. */
#ifndef PROTOCORE_HW_BUS_BITS
#define PROTOCORE_HW_BUS_BITS 32
#endif

/** @brief Nonzero to carry each bus unit big-endian. */
#ifndef PROTOCORE_HW_BUS_BE
#define PROTOCORE_HW_BUS_BE 0
#endif

/**
 * @brief The word standing in for the register at @p addr, as the modelled bus delivers it.
 * @param addr  the peripheral address the driver would load from, word aligned.
 * @return what was last written there, rearranged if the width or order says so; zero if unseen.
 */
uint32_t protocore_hw_reg_host_read(uintptr_t addr);

/**
 * @brief Store @p val at @p addr, as the modelled bus carries it.
 * @param addr  the peripheral address the driver would store to, word aligned.
 * @param val   the register value.
 */
void protocore_hw_reg_host_write(uintptr_t addr, uint32_t val);

/** @brief Drop every slot, so one test's register writes are not another's starting state. */
void protocore_hw_reg_host_reset(void);

/**
 * @brief Select the bus the model carries accesses on, for a test that drives more than one.
 * @param bits  16, 32 or 64; anything else is ignored and the current setting stands.
 * @param be    nonzero for big-endian units.
 */
void protocore_hw_reg_host_bus(unsigned bits, proto_bool be);

// The host forms of the accessors every register driver reaches a peripheral with. Stated here first
// so the vendor headers, which guard their own definitions, take these on a host build.
#ifndef PROTOCORE_HW_RD
#define PROTOCORE_HW_RD(a) protocore_hw_reg_host_read((uintptr_t)(a))
#endif
#ifndef PROTOCORE_HW_WR
#define PROTOCORE_HW_WR(a, v) protocore_hw_reg_host_write((uintptr_t)(a), (uint32_t)(v))
#endif

PROTOCORE_END_DECLS

#endif // PROTOCORE_HOST

#endif // PROTOCORE_HOST_HW_REG_H
