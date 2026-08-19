// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * The module exports @ref raw. Each scalar rung is one load or store and stays here, where a call
 * site folds it; the ladder @ref RawNs::read steps down has loops and lives in rawmemcpy.c.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RAWMEMCPY_H
#define PROTOCORE_RAWMEMCPY_H

#include "protocore_config.h" // PROTO_WORD_BITS: the register width the die declared

PROTOCORE_BEGIN_DECLS

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
 *     static uint8_t PROTO_ALIGN(PROTOCORE_ARENA_MAX_ALIGN) arena_store[PROTOCORE_ARENA_SIZE];
 *
 * This is the declaration, not the arithmetic: rounding a runtime offset up to a boundary is the
 * arena's job (mmgr/arena.h), which is where that already lives.
 */
#define PROTO_ALIGN(n) __attribute__((aligned(n)))

/**
 * @brief The aliasing half of ::PROTO_RAW without the alignment disclaimer.
 *
 * Reading one object through a pointer of another type still needs `may_alias` or the optimizer may
 * reorder or drop the access. What it does NOT need, once the caller has proven the address is
 * aligned, is `aligned(1)` - and dropping that is the difference between one load instruction and a
 * hand-built word.
 */
#define PROTO_ALIAS __attribute__((may_alias))

/**
 * @brief The widest step a multi-byte move takes here: the register width the die declared.
 *
 * There are only three distinct operations in this file - load 2, load 4, load 8 - and every width
 * above the top rung is that same load repeated, not a new one, because C has no wider scalar and
 * asking for one only makes the compiler build it out of these anyway, with the halves parked in
 * registers the caller cannot see. Anything past 64 bits is the caller's own walk over @ref
 * RawNs::u32, in whatever limb order that caller's arithmetic already stores.
 *
 * Below the top rung a move shifts down: each rung is half the one above, so the tail costs at most
 * one pass each.
 *
 * Taken from PROTO_WORD_BITS, which the die states in vendor/board_profiles/, rather than
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

/// @brief The mover's rung in bits.
#define PROTO_MV_BITS (PROTO_RAW_WORD * 8u)

/// @brief Unaligned, aliasing-permitted views of each scalar width. Declared once so the two
///        attributes that make the load legal cannot be omitted at a call site.
typedef uint16_t proto_raw_u16_t PROTO_RAW;
typedef uint32_t proto_raw_u32_t PROTO_RAW;
typedef uint64_t proto_raw_u64_t PROTO_RAW;

/// @brief Aliasing-permitted views at NATURAL alignment. The caller guarantees the address.
typedef uint16_t proto_al_u16_t PROTO_ALIAS;
typedef uint32_t proto_al_u32_t PROTO_ALIAS;
typedef uint64_t proto_al_u64_t PROTO_ALIAS;

/// @brief The mover's step, as a type. One rung; anything wider is this repeated.
#if PROTO_RAW_WORD >= 8
typedef uint64_t proto_mv_word;
#elif PROTO_RAW_WORD >= 4
typedef uint32_t proto_mv_word;
#else
typedef uint16_t proto_mv_word;
#endif

/**
 * @brief The raw access module: one scalar width read or written at a pointer, in machine order.
 *
 * @var RawNs::u16
 * Read the 2 bytes at @c p as a uint16_t in machine order, whatever the alignment.
 *
 * @var RawNs::u32
 * Read the 4 bytes at @c p as a uint32_t in machine order, whatever the alignment.
 *
 * @var RawNs::u64
 * Read the 8 bytes at @c p as a uint64_t in machine order, whatever the alignment.
 *
 * @var RawNs::load
 * Read @c n bytes (1, 2, 4 or 8) at @c p as one integer in machine order. @c n outside {1,2,4,8}
 * reads nothing and yields 0.
 *
 * For a caller whose width is a constant it does not spell out - the lane carrier in swar.h is set
 * by PROTO_SWAR_BITS, so its load follows that knob rather than naming a width. The octet case needs
 * no attribute: every address is aligned for one byte and a character type may alias anything, so it
 * is the plain dereference the wider rungs are masked down to.
 *
 * **Costs a whole byte-assembly sequence on a machine with no unaligned load.** ::PROTO_RAW states
 * that the address carries no alignment guarantee, which is right for a wire offset and wrong for a
 * caller that already walked its pointer to a boundary - the compiler must then synthesize each word
 * from bytes and shifts. A caller that has established alignment says so with ::RawNs::al_load.
 *
 * @var RawNs::put_u16
 * Write @c v as 2 bytes at @c p in machine order, whatever the alignment.
 *
 * @var RawNs::put_u32
 * Write @c v as 4 bytes at @c p in machine order, whatever the alignment.
 *
 * @var RawNs::put_u64
 * Write @c v as 8 bytes at @c p in machine order, whatever the alignment.
 *
 * @var RawNs::al_load
 * ::RawNs::load for an address the caller has already aligned to @c n.
 *
 * The contract is the whole point: @c p MUST be @c n-byte aligned. In exchange the compiler emits
 * the machine's own load instead of assembling the value from bytes, which on a die with no general
 * unaligned load is the difference between roughly ten instructions per word and one. Passing an
 * unaligned address here is undefined and on a strict-alignment part it faults.
 *
 * PROTOCORE_HW_UNALIGNED_LOAD says whether a die needs any of this. Where it is 1 the raw load is
 * already a single instruction and a caller has no reason to walk to a boundary first.
 *
 * @var RawNs::al_put_u16
 * The store side of ::RawNs::al_load at 2 bytes. @c p must carry that alignment.
 *
 * @var RawNs::al_put_u32
 * The store side of ::RawNs::al_load at 4 bytes. @c p must carry that alignment.
 *
 * @var RawNs::al_put_u64
 * The store side of ::RawNs::al_load at 8 bytes. @c p must carry that alignment.
 *
 * @var RawNs::mv_load
 * One aligned load at the mover's own rung, ::PROTO_RAW_WORD bytes at @c p.
 *
 * @var RawNs::mv_put
 * The store side of ::RawNs::mv_load.
 *
 * @var RawNs::read
 * Move @c sz bytes at @c p into @c dst, at any alignment - the arbitrary-width form.
 *
 * For a payload no scalar covers and no limb layout fits: a header struct overlaid on a byte stream,
 * or a span landing in a ring. Declare an overlaid struct ::PROTO_RAW so its members are reachable
 * once it lands, then move the whole thing here rather than member by member off a moving offset.
 *
 * It enters the ladder at ::PROTO_RAW_WORD and shifts down a rung at a time to the odd tail - the
 * same widths the rest of this header is built from. Only the first loop runs more than once: each
 * one below it starts with fewer bytes left than the loop above needed, so it iterates at most once.
 * A rung wider than the bus is compiled out rather than left for the compiler to decompose. The
 * source and destination must not overlap.
 *
 * No storage member: every rung works on the caller's pointers, the widths are compile-time, and the
 * ladder carries its position in a local.
 */
typedef struct
{
    uint16_t (*u16)(const void *p);
    uint32_t (*u32)(const void *p);
    uint64_t (*u64)(const void *p);
    uint64_t (*load)(const void *p, size_t n);
    void (*put_u16)(void *p, uint16_t v);
    void (*put_u32)(void *p, uint32_t v);
    void (*put_u64)(void *p, uint64_t v);
    uint64_t (*al_load)(const void *p, size_t n);
    void (*al_put_u16)(void *p, uint16_t v);
    void (*al_put_u32)(void *p, uint32_t v);
    void (*al_put_u64)(void *p, uint64_t v);
    proto_mv_word (*mv_load)(const unsigned char *p);
    void (*mv_put)(unsigned char *p, proto_mv_word v);
    void (*read)(void *dst, const void *p, size_t sz);
} RawNs;

/** @brief Read the 2 bytes at @p p as a uint16_t in machine order, whatever the alignment. */
PROTOCORE_INLINE uint16_t proto_raw_u16(const void *p)
{
    return *(const proto_raw_u16_t *)p;
}

/** @brief Read the 4 bytes at @p p as a uint32_t in machine order, whatever the alignment. */
PROTOCORE_INLINE uint32_t proto_raw_u32(const void *p)
{
    return *(const proto_raw_u32_t *)p;
}

/** @brief Read the 8 bytes at @p p as a uint64_t in machine order, whatever the alignment. */
PROTOCORE_INLINE uint64_t proto_raw_u64(const void *p)
{
    return *(const proto_raw_u64_t *)p;
}

/** @brief Read @p n bytes (1, 2, 4 or 8) at @p p as one integer in machine order. See ::RawNs::load. */
PROTOCORE_INLINE uint64_t proto_raw_load(const void *p, size_t n)
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

/** @brief Write @p v as 2 bytes at @p p in machine order, whatever the alignment. */
PROTOCORE_INLINE void proto_raw_put_u16(void *p, uint16_t v)
{
    *(proto_raw_u16_t *)p = v;
}

/** @brief Write @p v as 4 bytes at @p p in machine order, whatever the alignment. */
PROTOCORE_INLINE void proto_raw_put_u32(void *p, uint32_t v)
{
    *(proto_raw_u32_t *)p = v;
}

/** @brief Write @p v as 8 bytes at @p p in machine order, whatever the alignment. */
PROTOCORE_INLINE void proto_raw_put_u64(void *p, uint64_t v)
{
    *(proto_raw_u64_t *)p = v;
}

/** @brief ::proto_raw_load for an address the caller has already aligned to @p n. See ::RawNs::al_load. */
PROTOCORE_INLINE uint64_t proto_al_load(const void *p, size_t n)
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

/// @brief The store side of ::proto_al_load. @p p must be 2-byte aligned; the caller guarantees it.
PROTOCORE_INLINE void proto_al_put_u16(void *p, uint16_t v)
{
    *(proto_al_u16_t *)p = v;
}

/// @brief The store side of ::proto_al_load. @p p must be 4-byte aligned; the caller guarantees it.
PROTOCORE_INLINE void proto_al_put_u32(void *p, uint32_t v)
{
    *(proto_al_u32_t *)p = v;
}

/// @brief The store side of ::proto_al_load. @p p must be 8-byte aligned; the caller guarantees it.
PROTOCORE_INLINE void proto_al_put_u64(void *p, uint64_t v)
{
    *(proto_al_u64_t *)p = v;
}

/// @brief Aligned load at the mover's own rung.
PROTOCORE_INLINE proto_mv_word proto_mv_load(const unsigned char *p)
{
    return (proto_mv_word)proto_al_load(p, PROTO_RAW_WORD);
}

/// @brief Aligned store at the mover's own rung.
PROTOCORE_INLINE void proto_mv_put(unsigned char *p, proto_mv_word v)
{
#if PROTO_RAW_WORD >= 8
    proto_al_put_u64(p, (uint64_t)v);
#elif PROTO_RAW_WORD >= 4
    proto_al_put_u32(p, (uint32_t)v);
#else
    proto_al_put_u16(p, (uint16_t)v);
#endif
}

// The span move, in rawmemcpy.c. Named here because the table below has to name it. The rungs above
// are one instruction each and fold at the call site; this one has loops and is compiled once.
void proto_raw_read(void *dst, const void *p, size_t sz);

/**
 * @brief The names, aliased.
 *
 * `static const` and initialized here, not declared `extern` against a definition in the .c: a
 * translation unit that can see this initializer knows which function each member holds, so a member
 * read folds away, a rung defined above inlines, and the table is left referenced by nothing for the
 * linker to drop.
 *
 * `unused` because a header this wide is included by files that take none of it.
 */
// Designated, so a member's position in the struct does not decide what it binds to.
static const RawNs raw __attribute__((unused)) = {.u16 = proto_raw_u16,
                                                  .u32 = proto_raw_u32,
                                                  .u64 = proto_raw_u64,
                                                  .load = proto_raw_load,
                                                  .put_u16 = proto_raw_put_u16,
                                                  .put_u32 = proto_raw_put_u32,
                                                  .put_u64 = proto_raw_put_u64,
                                                  .al_load = proto_al_load,
                                                  .al_put_u16 = proto_al_put_u16,
                                                  .al_put_u32 = proto_al_put_u32,
                                                  .al_put_u64 = proto_al_put_u64,
                                                  .mv_load = proto_mv_load,
                                                  .mv_put = proto_mv_put,
                                                  .read = proto_raw_read};

PROTOCORE_END_DECLS

#endif // PROTOCORE_RAWMEMCPY_H
