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

#include <assert.h> // static_assert, which every pin below is written with
#include <stddef.h> // offsetof

/**
 * @brief Suppress the unused warning. Every module ends in a namespace most callers only partly use.
 *
 * The table is `static const` in the header, so a translation unit that includes the module and
 * calls two of its twelve entries still declares all twelve, and a plain -Wunused build would
 * report the ones it did not reach.
 */
#ifndef PROTOCORE_UNUSED
#if defined(__GNUC__) || defined(__clang__)
#define PROTOCORE_UNUSED __attribute__((unused))
#else
#define PROTOCORE_UNUSED
#endif
#endif

/** @brief Paste two tokens after expanding both. */
#define PROTOCORE_CAT_(a, b) a##b
#define PROTOCORE_CAT(a, b) PROTOCORE_CAT_(a, b)

/** @brief Count variadic arguments, up to 72 - the longest dispatch table in the tree is 69. */
#define PROTOCORE_NARG(...)                                                                                            \
    PROTOCORE_NARG_(__VA_ARGS__, 72, 71, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52,   \
                    51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28,    \
                    27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, \
                    0)
#define PROTOCORE_NARG_(...) PROTOCORE_ARG_N(__VA_ARGS__)
#define PROTOCORE_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,     \
                        _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, \
                        _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, \
                        _59, _60, _61, _62, _63, _64, _65, _66, _67, _68, _69, _70, _71, _72, N, ...)                  \
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
#define PROTOCORE_NS_L1(T, a1) PROTOCORE_NS_SLOT(T, a1, 0);
#define PROTOCORE_NS_L2(T, a1, a2) PROTOCORE_NS_L1(T, a1) PROTOCORE_NS_SLOT(T, a2, 1);
#define PROTOCORE_NS_L3(T, a1, a2, a3) PROTOCORE_NS_L2(T, a1, a2) PROTOCORE_NS_SLOT(T, a3, 2);
#define PROTOCORE_NS_L4(T, a1, a2, a3, a4) PROTOCORE_NS_L3(T, a1, a2, a3) PROTOCORE_NS_SLOT(T, a4, 3);
#define PROTOCORE_NS_L5(T, a1, a2, a3, a4, a5) PROTOCORE_NS_L4(T, a1, a2, a3, a4) PROTOCORE_NS_SLOT(T, a5, 4);
#define PROTOCORE_NS_L6(T, a1, a2, a3, a4, a5, a6) PROTOCORE_NS_L5(T, a1, a2, a3, a4, a5) PROTOCORE_NS_SLOT(T, a6, 5);
#define PROTOCORE_NS_L7(T, a1, a2, a3, a4, a5, a6, a7)                                                                 \
    PROTOCORE_NS_L6(T, a1, a2, a3, a4, a5, a6) PROTOCORE_NS_SLOT(T, a7, 6);
#define PROTOCORE_NS_L8(T, a1, a2, a3, a4, a5, a6, a7, a8)                                                             \
    PROTOCORE_NS_L7(T, a1, a2, a3, a4, a5, a6, a7) PROTOCORE_NS_SLOT(T, a8, 7);
#define PROTOCORE_NS_L9(T, a1, a2, a3, a4, a5, a6, a7, a8, a9)                                                         \
    PROTOCORE_NS_L8(T, a1, a2, a3, a4, a5, a6, a7, a8) PROTOCORE_NS_SLOT(T, a9, 8);
#define PROTOCORE_NS_L10(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10)                                                   \
    PROTOCORE_NS_L9(T, a1, a2, a3, a4, a5, a6, a7, a8, a9) PROTOCORE_NS_SLOT(T, a10, 9);
#define PROTOCORE_NS_L11(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11)                                              \
    PROTOCORE_NS_L10(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) PROTOCORE_NS_SLOT(T, a11, 10);
#define PROTOCORE_NS_L12(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12)                                         \
    PROTOCORE_NS_L11(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) PROTOCORE_NS_SLOT(T, a12, 11);
#define PROTOCORE_NS_L13(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13)                                    \
    PROTOCORE_NS_L12(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) PROTOCORE_NS_SLOT(T, a13, 12);
#define PROTOCORE_NS_L14(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14)                               \
    PROTOCORE_NS_L13(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) PROTOCORE_NS_SLOT(T, a14, 13);
#define PROTOCORE_NS_L15(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15)                          \
    PROTOCORE_NS_L14(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) PROTOCORE_NS_SLOT(T, a15, 14);
#define PROTOCORE_NS_L16(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16)                     \
    PROTOCORE_NS_L15(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) PROTOCORE_NS_SLOT(T, a16, 15);
#define PROTOCORE_NS_L17(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17)                \
    PROTOCORE_NS_L16(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16)                         \
    PROTOCORE_NS_SLOT(T, a17, 16);
#define PROTOCORE_NS_L18(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18)           \
    PROTOCORE_NS_L17(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17)                    \
    PROTOCORE_NS_SLOT(T, a18, 17);
#define PROTOCORE_NS_L19(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19)      \
    PROTOCORE_NS_L18(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18)               \
    PROTOCORE_NS_SLOT(T, a19, 18);
#define PROTOCORE_NS_L20(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20) \
    PROTOCORE_NS_L19(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19)          \
    PROTOCORE_NS_SLOT(T, a20, 19);
#define PROTOCORE_NS_L21(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21)                                                                                          \
    PROTOCORE_NS_L20(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20)     \
    PROTOCORE_NS_SLOT(T, a21, 20);
#define PROTOCORE_NS_L22(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22)                                                                                     \
    PROTOCORE_NS_L21(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21)                                                                                              \
    PROTOCORE_NS_SLOT(T, a22, 21);
#define PROTOCORE_NS_L23(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23)                                                                                \
    PROTOCORE_NS_L22(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22)                                                                                         \
    PROTOCORE_NS_SLOT(T, a23, 22);
#define PROTOCORE_NS_L24(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24)                                                                           \
    PROTOCORE_NS_L23(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23)                                                                                    \
    PROTOCORE_NS_SLOT(T, a24, 23);
#define PROTOCORE_NS_L25(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25)                                                                      \
    PROTOCORE_NS_L24(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24)                                                                               \
    PROTOCORE_NS_SLOT(T, a25, 24);
#define PROTOCORE_NS_L26(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26)                                                                 \
    PROTOCORE_NS_L25(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25)                                                                          \
    PROTOCORE_NS_SLOT(T, a26, 25);
#define PROTOCORE_NS_L27(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27)                                                            \
    PROTOCORE_NS_L26(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26)                                                                     \
    PROTOCORE_NS_SLOT(T, a27, 26);
#define PROTOCORE_NS_L28(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28)                                                       \
    PROTOCORE_NS_L27(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27)                                                                \
    PROTOCORE_NS_SLOT(T, a28, 27);
#define PROTOCORE_NS_L29(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29)                                                  \
    PROTOCORE_NS_L28(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28)                                                           \
    PROTOCORE_NS_SLOT(T, a29, 28);
#define PROTOCORE_NS_L30(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30)                                             \
    PROTOCORE_NS_L29(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29)                                                      \
    PROTOCORE_NS_SLOT(T, a30, 29);
#define PROTOCORE_NS_L31(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31)                                        \
    PROTOCORE_NS_L30(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30)                                                 \
    PROTOCORE_NS_SLOT(T, a31, 30);
#define PROTOCORE_NS_L32(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32)                                   \
    PROTOCORE_NS_L31(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31)                                            \
    PROTOCORE_NS_SLOT(T, a32, 31);
#define PROTOCORE_NS_L33(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33)                              \
    PROTOCORE_NS_L32(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32)                                       \
    PROTOCORE_NS_SLOT(T, a33, 32);
#define PROTOCORE_NS_L34(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34)                         \
    PROTOCORE_NS_L33(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33)                                  \
    PROTOCORE_NS_SLOT(T, a34, 33);
#define PROTOCORE_NS_L35(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35)                    \
    PROTOCORE_NS_L34(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34)                             \
    PROTOCORE_NS_SLOT(T, a35, 34);
#define PROTOCORE_NS_L36(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36)               \
    PROTOCORE_NS_L35(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35)                        \
    PROTOCORE_NS_SLOT(T, a36, 35);
#define PROTOCORE_NS_L37(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37)          \
    PROTOCORE_NS_L36(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36)                   \
    PROTOCORE_NS_SLOT(T, a37, 36);
#define PROTOCORE_NS_L38(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38)     \
    PROTOCORE_NS_L37(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37)              \
    PROTOCORE_NS_SLOT(T, a38, 37);
#define PROTOCORE_NS_L39(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39)                                                                                          \
    PROTOCORE_NS_L38(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38)         \
    PROTOCORE_NS_SLOT(T, a39, 38);
#define PROTOCORE_NS_L40(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40)                                                                                     \
    PROTOCORE_NS_L39(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39)    \
    PROTOCORE_NS_SLOT(T, a40, 39);
#define PROTOCORE_NS_L41(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41)                                                                                \
    PROTOCORE_NS_L40(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40)                                                                                              \
    PROTOCORE_NS_SLOT(T, a41, 40);
#define PROTOCORE_NS_L42(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42)                                                                           \
    PROTOCORE_NS_L41(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41)                                                                                         \
    PROTOCORE_NS_SLOT(T, a42, 41);
#define PROTOCORE_NS_L43(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43)                                                                      \
    PROTOCORE_NS_L42(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42)                                                                                    \
    PROTOCORE_NS_SLOT(T, a43, 42);
#define PROTOCORE_NS_L44(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44)                                                                 \
    PROTOCORE_NS_L43(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43)                                                                               \
    PROTOCORE_NS_SLOT(T, a44, 43);
#define PROTOCORE_NS_L45(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45)                                                            \
    PROTOCORE_NS_L44(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44)                                                                          \
    PROTOCORE_NS_SLOT(T, a45, 44);
#define PROTOCORE_NS_L46(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46)                                                       \
    PROTOCORE_NS_L45(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45)                                                                     \
    PROTOCORE_NS_SLOT(T, a46, 45);
#define PROTOCORE_NS_L47(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47)                                                  \
    PROTOCORE_NS_L46(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46)                                                                \
    PROTOCORE_NS_SLOT(T, a47, 46);
#define PROTOCORE_NS_L48(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48)                                             \
    PROTOCORE_NS_L47(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47)                                                           \
    PROTOCORE_NS_SLOT(T, a48, 47);
#define PROTOCORE_NS_L49(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49)                                        \
    PROTOCORE_NS_L48(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48)                                                      \
    PROTOCORE_NS_SLOT(T, a49, 48);
#define PROTOCORE_NS_L50(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50)                                   \
    PROTOCORE_NS_L49(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49)                                                 \
    PROTOCORE_NS_SLOT(T, a50, 49);
#define PROTOCORE_NS_L51(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51)                              \
    PROTOCORE_NS_L50(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50)                                            \
    PROTOCORE_NS_SLOT(T, a51, 50);
#define PROTOCORE_NS_L52(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52)                         \
    PROTOCORE_NS_L51(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51)                                       \
    PROTOCORE_NS_SLOT(T, a52, 51);
#define PROTOCORE_NS_L53(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53)                    \
    PROTOCORE_NS_L52(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52)                                  \
    PROTOCORE_NS_SLOT(T, a53, 52);
#define PROTOCORE_NS_L54(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54)               \
    PROTOCORE_NS_L53(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53)                             \
    PROTOCORE_NS_SLOT(T, a54, 53);
#define PROTOCORE_NS_L55(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55)          \
    PROTOCORE_NS_L54(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54)                        \
    PROTOCORE_NS_SLOT(T, a55, 54);
#define PROTOCORE_NS_L56(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56)     \
    PROTOCORE_NS_L55(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55)                   \
    PROTOCORE_NS_SLOT(T, a56, 55);
#define PROTOCORE_NS_L57(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57)                                                                                          \
    PROTOCORE_NS_L56(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56)              \
    PROTOCORE_NS_SLOT(T, a57, 56);
#define PROTOCORE_NS_L58(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58)                                                                                     \
    PROTOCORE_NS_L57(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57)         \
    PROTOCORE_NS_SLOT(T, a58, 57);
#define PROTOCORE_NS_L59(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59)                                                                                \
    PROTOCORE_NS_L58(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58)    \
    PROTOCORE_NS_SLOT(T, a59, 58);
#define PROTOCORE_NS_L60(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60)                                                                           \
    PROTOCORE_NS_L59(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59)                                                                                              \
    PROTOCORE_NS_SLOT(T, a60, 59);
#define PROTOCORE_NS_L61(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60, a61)                                                                      \
    PROTOCORE_NS_L60(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59, a60)                                                                                         \
    PROTOCORE_NS_SLOT(T, a61, 60);
#define PROTOCORE_NS_L62(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60, a61, a62)                                                                 \
    PROTOCORE_NS_L61(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59, a60, a61)                                                                                    \
    PROTOCORE_NS_SLOT(T, a62, 61);
#define PROTOCORE_NS_L63(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60, a61, a62, a63)                                                            \
    PROTOCORE_NS_L62(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59, a60, a61, a62)                                                                               \
    PROTOCORE_NS_SLOT(T, a63, 62);
#define PROTOCORE_NS_L64(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60, a61, a62, a63, a64)                                                       \
    PROTOCORE_NS_L63(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59, a60, a61, a62, a63)                                                                          \
    PROTOCORE_NS_SLOT(T, a64, 63);
#define PROTOCORE_NS_L65(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60, a61, a62, a63, a64, a65)                                                  \
    PROTOCORE_NS_L64(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59, a60, a61, a62, a63, a64)                                                                     \
    PROTOCORE_NS_SLOT(T, a65, 64);
#define PROTOCORE_NS_L66(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60, a61, a62, a63, a64, a65, a66)                                             \
    PROTOCORE_NS_L65(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59, a60, a61, a62, a63, a64, a65)                                                                \
    PROTOCORE_NS_SLOT(T, a66, 65);
#define PROTOCORE_NS_L67(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67)                                        \
    PROTOCORE_NS_L66(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59, a60, a61, a62, a63, a64, a65, a66)                                                           \
    PROTOCORE_NS_SLOT(T, a67, 66);
#define PROTOCORE_NS_L68(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68)                                   \
    PROTOCORE_NS_L67(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59, a60, a61, a62, a63, a64, a65, a66, a67)                                                      \
    PROTOCORE_NS_SLOT(T, a68, 67);
#define PROTOCORE_NS_L69(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69)                              \
    PROTOCORE_NS_L68(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59, a60, a61, a62, a63, a64, a65, a66, a67, a68)                                                 \
    PROTOCORE_NS_SLOT(T, a69, 68);
#define PROTOCORE_NS_L70(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70)                         \
    PROTOCORE_NS_L69(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69)                                            \
    PROTOCORE_NS_SLOT(T, a70, 69);
#define PROTOCORE_NS_L71(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71)                    \
    PROTOCORE_NS_L70(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70)                                       \
    PROTOCORE_NS_SLOT(T, a71, 70);
#define PROTOCORE_NS_L72(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, \
                         a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38,     \
                         a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56,     \
                         a57, a58, a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71, a72)               \
    PROTOCORE_NS_L71(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20,     \
                     a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,    \
                     a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58,    \
                     a59, a60, a61, a62, a63, a64, a65, a66, a67, a68, a69, a70, a71)                                  \
    PROTOCORE_NS_SLOT(T, a72, 71);

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
    PROTOCORE_CAT(PROTOCORE_NS_L, PROTOCORE_NARG(__VA_ARGS__))(T, __VA_ARGS__) static_assert(                          \
        sizeof(T) == (size_t)PROTOCORE_NARG(__VA_ARGS__) * PROTOCORE_FP_SIZE,                                          \
        #T " has a member that is not in its dispatch list, or is padded")

/**
 * @brief Pin the dispatch slots of a table that carries state after its entries.
 *
 * A table whose tail is not function pointers cannot be pinned by sizeof, so @p tail names the
 * first member after the run and its offset does the same job: insert or drop an entry and the tail
 * moves, which fails.
 */
#define PROTOCORE_NS_LAYOUT_OPEN(T, tail, ...)                                                                         \
    PROTOCORE_CAT(PROTOCORE_NS_L, PROTOCORE_NARG(__VA_ARGS__))(T, __VA_ARGS__) static_assert(                          \
        offsetof(T, tail) == (size_t)PROTOCORE_NARG(__VA_ARGS__) * PROTOCORE_FP_SIZE,                                  \
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
