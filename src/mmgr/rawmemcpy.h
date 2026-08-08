// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rawmemcpy.h
 * @brief The raw load: bytes at a pointer read as a wider type, in the machine's own order.
 *
 * One owner for the operation every word-at-a-time reader needs - the lane math (swar.h), the
 * span walks (protomem.c), the builder's float bit reads (membuild.h) - so the alignment rule is
 * stated once and every reader inherits it.
 *
 * **The two attributes are the whole implementation.** A bare `*(const uint32_t *)p` says the wrong
 * thing twice, and each attribute answers one of them: `aligned(1)` states that the address carries
 * no alignment guarantee, and `may_alias` states that this type may alias any other.
 *
 * **Machine order, not wire order.** These reconstruct the value the way this machine stores it,
 * which is what reading a representation means. A defined byte order on the wire is a different
 * question and belongs to mmgr/endian.h.
 *
 * These are the primitives the span walks are built from. A caller that wants to move a span asks
 * @ref mem for it.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RAWMEMCPY_H
#define PROTOCORE_RAWMEMCPY_H

#include "protocore_config.h" // PROTO_WORD_BITS: the register width the die declared

PROTO_BEGIN_DECLS

/**
 * @brief The attributes that make a raw access legal, named once.
 *
 * A struct read out of a byte stream needs exactly what a scalar does: it sits at whatever offset
 * the stream put it, and it is being reached through a pointer of another type. Carrying this on
 * the declaration is what stops the compiler assuming the struct's natural alignment and emitting
 * wide member loads that fault - the same failure as the scalar case, only harder to see, because
 * a struct's alignment is inherited from its widest member rather than written down.
 *
 *     typedef struct PROTO_RAW { uint32_t id; uint16_t len; } wire_hdr;
 */
#define PROTO_RAW __attribute__((aligned(1), may_alias))

/**
 * @brief Declare storage aligned to @p n bytes - the other half of the same platform question.
 *
 * ::PROTO_RAW says an address carries no guarantee; this states one. Both belong here because they
 * are the same decision seen from either end, and a codebase that spells alignment two ways ends up
 * with a pool that is aligned on one target and not on the next. @p n must be a power of two.
 *
 *     static uint8_t PROTO_ALIGN(PC_ARENA_MAX_ALIGN) arena_store[PC_ARENA_SIZE];
 *
 * This is the declaration, not the arithmetic: rounding a runtime offset up to a boundary is the
 * arena's job (mmgr/arena.h), which is where that already lives.
 */
#define PROTO_ALIGN(n) __attribute__((aligned(n)))

/**
 * @brief The widest step a multi-byte move takes here: the register width the die declared.
 *
 * There are only three distinct operations in this file - load 2, load 4, load 8 - and every width
 * above the top rung is that same load repeated, not a new one, because C has no wider scalar and
 * asking for one only makes the compiler build it out of these anyway, with the halves parked in
 * registers the caller cannot see. Anything past 64 bits is the caller's own walk over @ref
 * MemNs::u32, in whatever limb order that caller's arithmetic already stores.
 *
 * Below the top rung a move shifts down: each rung is half the one above, so the tail costs at most
 * one pass each.
 *
 * Taken from PROTO_WORD_BITS, which the die states in core_setup/board_profiles/, rather than
 * from UINTPTR_MAX. Reading the pointer width measures the BUILD MACHINE: the host toolchain is
 * 64-bit, so that inference gave the host an 8-byte ladder while every target in the list steps 4,
 * and a move is then a different shape in the test than in the thing being tested.
 */
#if PROTO_WORD_BITS >= 64
#define PROTO_RAW_WORD 8
#elif PROTO_WORD_BITS >= 32
#define PROTO_RAW_WORD 4
#else
#define PROTO_RAW_WORD 2
#endif

/// @brief Unaligned, aliasing-permitted views of each scalar width. Declared once so the two
///        attributes that make the load legal cannot be omitted at a call site.
typedef uint16_t proto_raw_u16_t PROTO_RAW;
typedef uint32_t proto_raw_u32_t PROTO_RAW;
typedef uint64_t proto_raw_u64_t PROTO_RAW;

/** @brief Read the 2 bytes at @p p as a uint16_t in machine order, whatever the alignment. */
static inline uint16_t proto_raw_u16(const void *p)
{
    return *(const proto_raw_u16_t *)p;
}

/** @brief Read the 4 bytes at @p p as a uint32_t in machine order, whatever the alignment. */
static inline uint32_t proto_raw_u32(const void *p)
{
    return *(const proto_raw_u32_t *)p;
}

/** @brief Read the 8 bytes at @p p as a uint64_t in machine order, whatever the alignment. */
static inline uint64_t proto_raw_u64(const void *p)
{
    return *(const proto_raw_u64_t *)p;
}

/**
 * @brief Read @p n bytes (1, 2, 4 or 8) at @p p as one integer in machine order.
 *
 * For a caller whose width is a constant it does not spell out - the lane carrier in swar.h is set
 * by PROTO_SWAR_BITS, so its load follows that knob rather than naming a width. The octet case needs
 * no attribute: every address is aligned for one byte and a character type may alias anything, so
 * it is the plain dereference the wider rungs are masked down to. @p n outside {1,2,4,8} reads
 * nothing and yields 0.
 *
 * **Costs a whole byte-assembly sequence on a machine with no unaligned load.** ::PROTO_RAW states
 * that the address carries no alignment guarantee, which is right for a wire offset and wrong for a
 * caller that already walked its pointer to a boundary - the compiler must then synthesize each
 * word from bytes and shifts. A caller that has established alignment says so with ::proto_al_load.
 */
static inline uint64_t proto_raw_load(const void *p, size_t n)
{
    switch (n)
    {
    case 1:
        return *(const unsigned char *)p;
    case 2:
        return proto_raw_u16(p);
    case 4:
        return proto_raw_u32(p);
    case 8:
        return proto_raw_u64(p);
    default:
        return 0;
    }
}

/**
 * @brief The aliasing half of ::PROTO_RAW without the alignment disclaimer.
 *
 * Reading one object through a pointer of another type still needs `may_alias` or the optimizer may
 * reorder or drop the access. What it does NOT need, once the caller has proven the address is
 * aligned, is `aligned(1)` - and dropping that is the difference between one load instruction and a
 * hand-built word.
 */
#define PROTO_ALIAS __attribute__((may_alias))

/// @brief Aliasing-permitted views at NATURAL alignment. The caller guarantees the address.
typedef uint16_t proto_al_u16_t PROTO_ALIAS;
typedef uint32_t proto_al_u32_t PROTO_ALIAS;
typedef uint64_t proto_al_u64_t PROTO_ALIAS;

/**
 * @brief ::proto_raw_load for an address the caller has already aligned to @p n.
 *
 * The contract is the whole point: @p p MUST be @p n-byte aligned. In exchange the compiler emits
 * the machine's own load instead of assembling the value from bytes, which on a die with no general
 * unaligned load (every xtensa in the target list) is the difference between roughly ten
 * instructions per word and one. Passing an unaligned address here is undefined and on a
 * strict-alignment part it faults, which is the honest trade for what it buys.
 *
 * PC_HW_UNALIGNED_LOAD says whether a die needs any of this. Where it is 1 the raw load is already a
 * single instruction and a caller has no reason to walk to a boundary first.
 */
static inline uint64_t proto_al_load(const void *p, size_t n)
{
    switch (n)
    {
    case 1:
        return *(const unsigned char *)p;
    case 2:
        return *(const proto_al_u16_t *)p;
    case 4:
        return *(const proto_al_u32_t *)p;
    case 8:
        return *(const proto_al_u64_t *)p;
    default:
        return 0;
    }
}

/** @brief Write @p v as 2 bytes at @p p in machine order, whatever the alignment. */
static inline void proto_raw_put_u16(void *p, uint16_t v)
{
    *(proto_raw_u16_t *)p = v;
}

/** @brief Write @p v as 4 bytes at @p p in machine order, whatever the alignment. */
static inline void proto_raw_put_u32(void *p, uint32_t v)
{
    *(proto_raw_u32_t *)p = v;
}

/** @brief Write @p v as 8 bytes at @p p in machine order, whatever the alignment. */
static inline void proto_raw_put_u64(void *p, uint64_t v)
{
    *(proto_raw_u64_t *)p = v;
}

/**
 * @brief Move @p sz bytes at @p p into @p dst, at any alignment - the arbitrary-width form.
 *
 * For a payload no scalar covers and no limb layout fits: a header struct overlaid on a byte
 * stream, or a span landing in a ring. Declare an overlaid struct ::PROTO_RAW so its members are
 * reachable once it lands, then move the whole thing here rather than member by member off a
 * moving offset.
 *
 * It enters the ladder at ::PROTO_RAW_WORD and shifts down a rung at a time to the odd tail - the
 * same widths the rest of this header is built from. Only the first loop runs more than once: each
 * one below it starts with fewer bytes left than the loop above needed, so it iterates at most
 * once. A rung wider than the bus is compiled out rather than left for the compiler to decompose.
 * The source and destination must not overlap.
 */
/// @brief The store side of ::proto_al_load. @p p must be @p n-byte aligned; the caller guarantees it.
static inline void proto_al_put_u16(void *p, uint16_t v)
{
    *(proto_al_u16_t *)p = v;
}
static inline void proto_al_put_u32(void *p, uint32_t v)
{
    *(proto_al_u32_t *)p = v;
}
static inline void proto_al_put_u64(void *p, uint64_t v)
{
    *(proto_al_u64_t *)p = v;
}

/// @brief The mover's step, as a type. One rung; anything wider is this repeated.
#if PROTO_RAW_WORD >= 8
typedef uint64_t proto_mv_word;
#elif PROTO_RAW_WORD >= 4
typedef uint32_t proto_mv_word;
#else
typedef uint16_t proto_mv_word;
#endif

#define PROTO_MV_BITS (PROTO_RAW_WORD * 8u)

/// @brief Aligned load and store at the mover's own rung.
static inline proto_mv_word proto_mv_load(const unsigned char *p)
{
    return (proto_mv_word)proto_al_load(p, PROTO_RAW_WORD);
}
static inline void proto_mv_put(unsigned char *p, proto_mv_word v)
{
#if PROTO_RAW_WORD >= 8
    proto_al_put_u64(p, (uint64_t)v);
#elif PROTO_RAW_WORD >= 4
    proto_al_put_u32(p, (uint32_t)v);
#else
    proto_al_put_u16(p, (uint16_t)v);
#endif
}

static inline void proto_raw_read(void *dst, const void *p, size_t sz)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *u = (const unsigned char *)p;
    const uintptr_t mask = (uintptr_t)(PROTO_RAW_WORD - 1u);
    size_t i = 0;

    // Head: bytes until the DESTINATION is on a boundary. Fewer than one word, once. The
    // destination is the side that gets aligned because every step below writes it, and a store
    // that has to be assembled from bytes costs the same as a load that does.
    while (i < sz && ((uintptr_t)(d + i) & mask) != 0u)
    {
        d[i] = u[i];
        i++;
    }

    const size_t off = (size_t)((uintptr_t)(u + i) & mask);
    if (off == 0u)
    {
        // Both on a boundary: the machine's own load and its own store, nothing in between.
        while (sz - i >= PROTO_RAW_WORD)
        {
            proto_mv_put(d + i, proto_mv_load(u + i));
            i += PROTO_RAW_WORD;
        }
    }
    else
    {
        // Not co-aligned, and the answer is NOT to fall back to bytes. Read the source at its own
        // boundary and funnel each adjacent PAIR of aligned words into the word the destination
        // wants: one shifts down by the misalignment, the next shifts up by the rest, and the OR
        // joins them. Both shifts move every lane at once, so the misalignment costs two shifts and
        // an OR per word instead of a byte loop - and every access stays a real load and a real
        // store, which on a part with no unaligned instruction is the whole difference.
        //
        // Address order decides which way each shift goes: on a little-endian load the lowest byte
        // sits in the low bits, so the earlier word shifts DOWN; big-endian is the mirror.
        const unsigned char *sa = (u + i) - off;
        const unsigned lo = (unsigned)(off * 8u);
        const unsigned hi = (unsigned)(PROTO_MV_BITS - (off * 8u));
        proto_mv_word prev = proto_mv_load(sa);
        while (sz - i >= PROTO_RAW_WORD)
        {
            sa += PROTO_RAW_WORD;
            proto_mv_word cur = proto_mv_load(sa);
#if PC_HW_BIG_ENDIAN
            proto_mv_put(d + i, (proto_mv_word)((prev << lo) | (cur >> hi)));
#else
            proto_mv_put(d + i, (proto_mv_word)((prev >> lo) | (cur << hi)));
#endif
            prev = cur;
            i += PROTO_RAW_WORD;
        }
    }

    // Tail: fewer than a word left.
    while (i < sz)
    {
        d[i] = u[i];
        i++;
    }
}

PROTO_END_DECLS

#endif // PROTOCORE_RAWMEMCPY_H
