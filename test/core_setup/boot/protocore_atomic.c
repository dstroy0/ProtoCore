// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_atomic.c
 * @brief The read-modify-write atomics on cores whose instruction set has none.
 *
 * mmgr/ring.h claims and releases slots with atomic_fetch_or / atomic_fetch_and, and the TCP
 * protocol counters use atomic_fetch_add. Acquire and release loads and stores compile to plain
 * instructions everywhere, but a read-modify-write needs an exclusive-access pair, and two of the
 * targets do not have one: armv6-m has no LDREX/STREX, and riscv32 without the A extension has no
 * LR/SC. On those the compiler emits a call to __atomic_fetch_*_4 instead, which is what this
 * defines. Measured, not assumed: cortex-M0 and rv32imc emit the calls; cortex-M3, M4, M7 and
 * rv32imac inline the same operations and never reach this file.
 *
 * The sequence is made indivisible by masking interrupts across it, which is what "atomic" can mean
 * on a core with no exclusive monitor. Both cores that reach this file are single-core parts; a
 * multi-core target carries the instruction and never gets here.
 *
 * The memory order argument is dropped. A masked region cannot be observed part-done by anything
 * this can exclude, and the compiler barrier on both asm statements keeps the accesses inside it.
 */

#include <stdint.h>

#if defined(__ARM_ARCH_6M__) || (defined(__riscv) && !defined(__riscv_atomic))

#if defined(__ARM_ARCH_6M__)

/** @brief Mask interrupts, returning PRIMASK as it was. */
static inline uint32_t protocore_irq_save(void)
{
    uint32_t primask;
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(primask)::"memory");
    return primask;
}

/** @brief Put PRIMASK back, which unmasks only if it was unmasked on the way in. */
static inline void protocore_irq_restore(uint32_t primask)
{
    __asm volatile("msr primask, %0" ::"r"(primask) : "memory");
}

#else

#define PROTOCORE_MSTATUS_MIE 0x8u

/** @brief Clear mstatus.MIE, returning the bit as it was. */
static inline uint32_t protocore_irq_save(void)
{
    uint32_t prev;
    __asm volatile("csrrc %0, mstatus, %1" : "=r"(prev) : "r"(PROTOCORE_MSTATUS_MIE) : "memory");
    return prev & PROTOCORE_MSTATUS_MIE;
}

/** @brief Set mstatus.MIE back, which is a no-op when it was already clear. */
static inline void protocore_irq_restore(uint32_t prev)
{
    __asm volatile("csrs mstatus, %0" ::"r"(prev) : "memory");
}

#endif

uint32_t __atomic_fetch_add_4(volatile void *ptr, uint32_t val, int memorder)
{
    (void)memorder;
    volatile uint32_t *const p = (volatile uint32_t *)ptr;
    const uint32_t mask = protocore_irq_save();
    const uint32_t old = *p;
    *p = old + val;
    protocore_irq_restore(mask);
    return old;
}

uint32_t __atomic_fetch_sub_4(volatile void *ptr, uint32_t val, int memorder)
{
    (void)memorder;
    volatile uint32_t *const p = (volatile uint32_t *)ptr;
    const uint32_t mask = protocore_irq_save();
    const uint32_t old = *p;
    *p = old - val;
    protocore_irq_restore(mask);
    return old;
}

uint32_t __atomic_fetch_or_4(volatile void *ptr, uint32_t val, int memorder)
{
    (void)memorder;
    volatile uint32_t *const p = (volatile uint32_t *)ptr;
    const uint32_t mask = protocore_irq_save();
    const uint32_t old = *p;
    *p = old | val;
    protocore_irq_restore(mask);
    return old;
}

uint32_t __atomic_fetch_and_4(volatile void *ptr, uint32_t val, int memorder)
{
    (void)memorder;
    volatile uint32_t *const p = (volatile uint32_t *)ptr;
    const uint32_t mask = protocore_irq_save();
    const uint32_t old = *p;
    *p = old & val;
    protocore_irq_restore(mask);
    return old;
}

uint32_t __atomic_exchange_4(volatile void *ptr, uint32_t val, int memorder)
{
    (void)memorder;
    volatile uint32_t *const p = (volatile uint32_t *)ptr;
    const uint32_t mask = protocore_irq_save();
    const uint32_t old = *p;
    *p = val;
    protocore_irq_restore(mask);
    return old;
}

/** @brief Writes @p desired only if the word still reads @p *expected; reports whether it did. */
int __atomic_compare_exchange_4(volatile void *ptr, void *expected, uint32_t desired, int weak, int success,
                                int failure)
{
    (void)weak;
    (void)success;
    (void)failure;
    volatile uint32_t *const p = (volatile uint32_t *)ptr;
    uint32_t *const want = (uint32_t *)expected;
    const uint32_t mask = protocore_irq_save();
    const uint32_t old = *p;
    const int matched = (old == *want);
    if (matched)
    {
        *p = desired;
    }
    else
    {
        *want = old;
    }
    protocore_irq_restore(mask);
    return matched;
}

#endif // no hardware read-modify-write
