// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PROTOCORE_NS_CONTRACT_H
#define PROTOCORE_NS_CONTRACT_H

/**
 * @file ns_contract.h
 * @brief What a namespace IS: how its table is stored, how its slots are pinned, and how a call
 *        with several operands is written.
 *
 * Taken from MMgr (include/MMgr/src/mmgr_compiler_directives.h), which is the shape this tree is
 * converging on. The three pieces are the whole contract:
 *
 *   PROTOCORE_NS         the table is `static const`, which is what lets a call through it inline
 *   PROTOCORE_NS_LAYOUT  every entry is pinned to its dispatch slot, at compile time
 *   PROTOCORE_CALL       an entry's operands travel as a compound literal, not on a shared global
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#include "config/platform/compiler_directives.h" // PROTOCORE_STATIC_ASSERT and the attribute probes

#include <stddef.h> // offsetof

/** @brief Paste two tokens after expanding both. */
#define PROTOCORE_CAT_(a, b) a##b
#define PROTOCORE_CAT(a, b) PROTOCORE_CAT_(a, b)

/** @brief Count variadic arguments, up to 24. */
#define PROTOCORE_NARG(...)                                                                                            \
    PROTOCORE_NARG_(__VA_ARGS__, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2,   \
                    1, 0)
#define PROTOCORE_NARG_(...) PROTOCORE_ARG_N(__VA_ARGS__)
#define PROTOCORE_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,     \
                        _21, _22, _23, _24, N, ...)                                                                    \
    N

/**
 * @brief Call an entry with a compound literal, so the aggregate never appears at the call site.
 *
 *     #define protocore_sha256_hash(...) PROTOCORE_CALL(protocore_sha256_hash_at, Sha256HashArgs, __VA_ARGS__)
 *     Sha256.hash(.data = p, .len = n, .out = digest);
 *
 * This is what replaces the operands global. Writing `Sha256V.hash_args.data = p;` before every call
 * put one module's arguments in a place every other translation unit can reach and every reentrant
 * path can overwrite; a compound literal is the caller's, lives on the caller's stack, and cannot be
 * read by anyone else.
 *
 * Two properties come from the standard rather than the compiler, so they hold on every target:
 * unnamed fields are zero initialised, which is how a default costs nothing and is never spelled;
 * and a compound literal in argument position lives until the end of the enclosing block, so the
 * entry may hold the pointer for the whole call.
 *
 * Designated initialisers are the intended spelling. A positional call still compiles, but -Wextra
 * reports the fields it left behind and designated calls it does not.
 */
#define PROTOCORE_CALL(entry, ArgsType, ...) entry(&(ArgsType){__VA_ARGS__})

/** @brief Size of a function pointer. Not void *, which is the wrong ruler where code and data
 *         pointers differ in width. */
#define PROTOCORE_FP_SIZE (sizeof(void (*)(void)))

/** @brief Assert one member sits at one dispatch slot. */
#define PROTOCORE_NS_SLOT(T, member, slot)                                                                             \
    static_assert(offsetof(T, member) == (size_t)(slot) * PROTOCORE_FP_SIZE,                                           \
                  #T "." #member " is not at dispatch slot " #slot)

/*
 * Unrolled rather than recursive, because a failed assert should name the member and the slot and
 * nothing else. One line per arity.
 */
#define PROTOCORE_NS_L1(T, a) PROTOCORE_NS_SLOT(T, a, 0);
#define PROTOCORE_NS_L2(T, a, b) PROTOCORE_NS_L1(T, a) PROTOCORE_NS_SLOT(T, b, 1);
#define PROTOCORE_NS_L3(T, a, b, c) PROTOCORE_NS_L2(T, a, b) PROTOCORE_NS_SLOT(T, c, 2);
#define PROTOCORE_NS_L4(T, a, b, c, d) PROTOCORE_NS_L3(T, a, b, c) PROTOCORE_NS_SLOT(T, d, 3);
#define PROTOCORE_NS_L5(T, a, b, c, d, e) PROTOCORE_NS_L4(T, a, b, c, d) PROTOCORE_NS_SLOT(T, e, 4);
#define PROTOCORE_NS_L6(T, a, b, c, d, e, f) PROTOCORE_NS_L5(T, a, b, c, d, e) PROTOCORE_NS_SLOT(T, f, 5);
#define PROTOCORE_NS_L7(T, a, b, c, d, e, f, g) PROTOCORE_NS_L6(T, a, b, c, d, e, f) PROTOCORE_NS_SLOT(T, g, 6);
#define PROTOCORE_NS_L8(T, a, b, c, d, e, f, g, h)                                                                     \
    PROTOCORE_NS_L7(T, a, b, c, d, e, f, g) PROTOCORE_NS_SLOT(T, h, 7);
#define PROTOCORE_NS_L9(T, a, b, c, d, e, f, g, h, i)                                                                  \
    PROTOCORE_NS_L8(T, a, b, c, d, e, f, g, h) PROTOCORE_NS_SLOT(T, i, 8);
#define PROTOCORE_NS_L10(T, a, b, c, d, e, f, g, h, i, j)                                                              \
    PROTOCORE_NS_L9(T, a, b, c, d, e, f, g, h, i) PROTOCORE_NS_SLOT(T, j, 9);
#define PROTOCORE_NS_L11(T, a, b, c, d, e, f, g, h, i, j, k)                                                           \
    PROTOCORE_NS_L10(T, a, b, c, d, e, f, g, h, i, j) PROTOCORE_NS_SLOT(T, k, 10);
#define PROTOCORE_NS_L12(T, a, b, c, d, e, f, g, h, i, j, k, l)                                                        \
    PROTOCORE_NS_L11(T, a, b, c, d, e, f, g, h, i, j, k) PROTOCORE_NS_SLOT(T, l, 11);
#define PROTOCORE_NS_L13(T, a, b, c, d, e, f, g, h, i, j, k, l, m)                                                     \
    PROTOCORE_NS_L12(T, a, b, c, d, e, f, g, h, i, j, k, l) PROTOCORE_NS_SLOT(T, m, 12);
#define PROTOCORE_NS_L14(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n)                                                  \
    PROTOCORE_NS_L13(T, a, b, c, d, e, f, g, h, i, j, k, l, m) PROTOCORE_NS_SLOT(T, n, 13);
#define PROTOCORE_NS_L15(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o)                                               \
    PROTOCORE_NS_L14(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n) PROTOCORE_NS_SLOT(T, o, 14);
#define PROTOCORE_NS_L16(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p)                                            \
    PROTOCORE_NS_L15(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o) PROTOCORE_NS_SLOT(T, p, 15);
#define PROTOCORE_NS_L17(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q)                                         \
    PROTOCORE_NS_L16(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) PROTOCORE_NS_SLOT(T, q, 16);
#define PROTOCORE_NS_L18(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r)                                      \
    PROTOCORE_NS_L17(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q) PROTOCORE_NS_SLOT(T, r, 17);
#define PROTOCORE_NS_L19(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s)                                   \
    PROTOCORE_NS_L18(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r) PROTOCORE_NS_SLOT(T, s, 18);
#define PROTOCORE_NS_L20(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t)                                \
    PROTOCORE_NS_L19(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s) PROTOCORE_NS_SLOT(T, t, 19);
#define PROTOCORE_NS_L21(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u)                             \
    PROTOCORE_NS_L20(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t) PROTOCORE_NS_SLOT(T, u, 20);
#define PROTOCORE_NS_L22(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v)                          \
    PROTOCORE_NS_L21(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u) PROTOCORE_NS_SLOT(T, v, 21);
#define PROTOCORE_NS_L23(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w)                       \
    PROTOCORE_NS_L22(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v) PROTOCORE_NS_SLOT(T, w, 22);
#define PROTOCORE_NS_L24(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x)                    \
    PROTOCORE_NS_L23(T, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w)                           \
    PROTOCORE_NS_SLOT(T, x, 23);

/**
 * @brief Pin every dispatch slot of a table that is nothing but function pointers.
 *
 *     PROTOCORE_NS_LAYOUT(Sha256Ns, init, update, final, hash);
 *
 * A `<X>Ns` is a run of function pointers, and a positional initialiser mis-wires silently when a
 * member is inserted, removed or moved - which this tree has already been bitten by: radio_power.c
 * initialised its table positionally across a feature-flag split, so an arm that compiled out
 * shifted every member below it onto the wrong function, and it still built. This asserts each
 * named member is at its own slot, in the order given, and that sizeof is exactly that many
 * pointers, so the mis-wiring fails at the declaration instead of at a wrong call.
 *
 * Costs nothing at run time.
 */
#define PROTOCORE_NS_LAYOUT(T, ...)                                                                                    \
    PROTOCORE_CAT(PROTOCORE_NS_L, PROTOCORE_NARG(__VA_ARGS__))(T, __VA_ARGS__)                                         \
    static_assert(sizeof(T) == (size_t)PROTOCORE_NARG(__VA_ARGS__) * PROTOCORE_FP_SIZE,                                \
                  #T " has a member that is not in its dispatch list, or is padded")

/**
 * @brief Pin the dispatch slots of a table that carries state after its entries.
 *
 * A table whose tail is not function pointers cannot be pinned by sizeof, so @p tail names the
 * first member after the run and its offset does the same job: insert or drop an entry and the tail
 * moves, which fails.
 */
#define PROTOCORE_NS_LAYOUT_OPEN(T, tail, ...)                                                                         \
    PROTOCORE_CAT(PROTOCORE_NS_L, PROTOCORE_NARG(__VA_ARGS__))(T, __VA_ARGS__)                                         \
    static_assert(offsetof(T, tail) == (size_t)PROTOCORE_NARG(__VA_ARGS__) * PROTOCORE_FP_SIZE,                        \
                  #T "." #tail " does not begin where the dispatch run ends")

/**
 * @brief Storage for a dispatch table. The const is load bearing.
 *
 * Measured on gcc: a call through a `static const <X>Ns` devirtualises to the inlined body, the same
 * instructions as calling the entry directly, and the same call through a non-const one does not -
 * that becomes a real call with a frame. clang devirtualises both, so const is what makes the two
 * agree.
 */
#define PROTOCORE_NS static const

#endif // PROTOCORE_NS_CONTRACT_H
