// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file types.h
 * @brief The primitive types every other file is written in, and the one place <stdint.h> and
 *        <stddef.h> appear.
 *
 * A library that targets xtensa, riscv, arm and c2000 cannot spend a type it has not established.
 * `uint8_t` is optional in C11 - a conforming implementation omits it when it has no 8-bit type,
 * which is exactly the c2000 case, where a byte is 16 bits wide. Naming the widths once here makes
 * that one file to port and one place to look.
 *
 * **Widths are stated, never inherited.** `size_t` is the type this file exists to replace at an
 * offset or a length. Its width is whatever the target's pointer happens to be, so the same
 * expression is 32-bit index math on a device and 64-bit on the host that tests it - different
 * emitted code from the same source, which is the one thing this library does not accept.
 * ::proto_idx is a stated width the build can override, so the arithmetic has the same shape
 * everywhere.
 *
 * **Narrow values are carried in the register width.** On every ISA in the target list an operation
 * narrower than the register costs an extra mask or sign-extend to keep the unused half honest, so
 * the cheapest 16-bit index on a 32-bit part is a 32-bit one truncated at the boundary, exactly as
 * a 32-bit index on a 64-bit host is. ::proto_word is that register width.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TYPES_H
#define PROTOCORE_TYPES_H

// Reached through protocore_config.h, which is the single entry point: it declares the widths and
// then includes this file. Including this one directly would bind the types to whatever the
// defaults happened to be, so the widths are checked for presence rather than assumed.
#ifndef PROTO_WORD_BITS
#error "include protocore_config.h instead of this file - it is the entry point that sets the widths"
#endif

#include <assert.h> // C11 spells static_assert here; C++ has it built in
#include <stddef.h> // size_t, and the one place it enters the library
#include <stdint.h>

/// @name Fixed-width integers
/// The exact-width names, aliased once. A port that lacks one of these replaces it here.
/// @{
typedef uint8_t proto_u8;
typedef uint16_t proto_u16;
typedef uint32_t proto_u32;
typedef uint64_t proto_u64;
typedef int8_t proto_i8;
typedef int16_t proto_i16;
typedef int32_t proto_i32;
typedef int64_t proto_i64;
/// @}

/**
 * @brief The truth value.
 *
 * Each language's own boolean, so this needs no header and cannot collide with a vendor `bool`
 * macro - which several SDKs in the target list define, and which is why `<stdbool.h>` is not
 * included here. Both spellings are one byte and both normalize any nonzero to 1, so a value
 * crossing between the library and a caller compares the same on either side.
 *
 * The C++ arm is not conversion scaffolding: this type reaches the public surface through
 * protocore.h, and the sketches that surface is written for are compiled as C++.
 */
#ifdef __cplusplus
typedef bool proto_bool;
#else
typedef _Bool proto_bool;
#endif

#define PROTO_TRUE ((proto_bool)1)  ///< the true value, spelled so a caller never writes a bare 1
#define PROTO_FALSE ((proto_bool)0) ///< the false value

/**
 * @brief Give a header's declarations C linkage, so their symbol names carry no parameter types.
 *
 * Wraps the declarations between them in `extern "C"` under a C++ compiler, and expands to nothing
 * under a C one. The sketches, ESP-IDF app code and Unity suites that call this library are C++;
 * src/ is C.
 */
#ifdef __cplusplus
#define PROTO_BEGIN_DECLS                                                                                              \
    extern "C"                                                                                                         \
    {
#define PROTO_END_DECLS }
#else
#define PROTO_BEGIN_DECLS
#define PROTO_END_DECLS
#endif

/**
 * @brief Give an enum the narrowest type its values fit in. Carried by every enum in this library.
 *
 * C11 leaves an enum's underlying type to the implementation, and the target list does not agree:
 * xtensa, riscv32 and the host give an int, while arm-none-eabi defaults to the narrow form. An enum
 * declared inside a struct is BSS, so without this the same connection slot is one size on an ESP32
 * and another on a Cortex-M, and the footprint stops being a single number that can be computed
 * before flashing. C23 spells the same thing `enum E : proto_u8`; until every compiler in the list
 * is C23, the attribute is what states it.
 *
 * TI's compiler takes `--small_enum` on the command line and has no attribute for it, so this
 * expands to nothing there and the assert below fails rather than the width being wrong quietly.
 */
#if defined(__GNUC__) || defined(__clang__)
#define PROTO_ENUM_PACKED __attribute__((packed))
#else
#define PROTO_ENUM_PACKED
#endif

/**
 * @brief The natural register width, as a type.
 *
 * What an index or a count is carried in while it is being worked on. Narrower arithmetic is not
 * cheaper on these parts: it costs the mask that keeps the unused half correct.
 */
#if PROTO_WORD_BITS == 64
typedef proto_u64 proto_word;
#elif PROTO_WORD_BITS == 32
typedef proto_u32 proto_word;
#elif PROTO_WORD_BITS == 16
typedef proto_u16 proto_word;
#else
#error "PROTO_WORD_BITS must be 16, 32 or 64 - see protocore_config.h"
#endif

/**
 * @brief Every offset, length and capacity in this library. Never `size_t`.
 *
 * Stated at ::PROTO_INDEX_BITS so an offset is the same width in the device build and in the host
 * test that proves it. 32 bits addresses far more than any pool this library reserves; a target
 * whose every buffer is under 64 KB can set 16 and pay one narrower register per index.
 */
#if PROTO_INDEX_BITS == 32
typedef proto_u32 proto_idx;
#elif PROTO_INDEX_BITS == 16
typedef proto_u16 proto_idx;
#else
#error "PROTO_INDEX_BITS must be 16 or 32 - see protocore_config.h"
#endif

// The knobs above say what the widths should be; these check the target actually provides them.
// A platform missing an exact-width type fails here, naming itself, rather than at the first
// expression that assumed it.
//
// `static_assert` rather than C11's `_Static_assert`: this header is reached from the sketches
// compiled as C++, where the underscored spelling is not a keyword. C11's <assert.h> defines the
// unprefixed name and C++ has it built in, so one spelling is correct on both sides.
static_assert(sizeof(proto_u8) == 1, "proto_u8 must be exactly 8 bits: this target has no 8-bit type");
static_assert(sizeof(proto_u16) * 8u == 16u, "proto_u16 must be exactly 16 bits");
static_assert(sizeof(proto_u32) * 8u == 32u, "proto_u32 must be exactly 32 bits");
static_assert(sizeof(proto_u64) * 8u == 64u, "proto_u64 must be exactly 64 bits");
static_assert(sizeof(proto_word) * 8u == PROTO_WORD_BITS, "proto_word must be exactly PROTO_WORD_BITS wide");
static_assert(sizeof(proto_idx) * 8u == PROTO_INDEX_BITS, "proto_idx must be exactly PROTO_INDEX_BITS wide");
static_assert(sizeof(proto_idx) <= sizeof(proto_word), "an index must fit the register it is carried in");

// ::PROTO_ENUM_PACKED is a toolchain feature rather than a per-declaration one, so proving it is
// honored once proves it for every enum that carries it. An enum needing more than a byte still
// widens on its own: the attribute asks for the narrowest type the values fit, not for one byte.
typedef enum PROTO_ENUM_PACKED
{
    PROTO_ENUM_PROBE_MIN = 0,
    PROTO_ENUM_PROBE_MAX = 255,
} proto_enum_probe;
static_assert(sizeof(proto_enum_probe) == 1,
              "PROTO_ENUM_PACKED is not honored here, so no enum keeps its declared width (TI: pass --small_enum)");

#endif // PROTOCORE_TYPES_H
