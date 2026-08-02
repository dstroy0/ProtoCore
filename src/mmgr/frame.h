// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file frame.h
 * @brief Declarative frame builder: a frame is a static table of typed fields, built by one engine.
 *
 * A frame's shape is data, not code: a `static const pc_field[]` in rodata, walked by one engine.
 * The spec is pre-decoded, so the engine reads an opcode and a width out of a struct and jumps -
 * nothing is parsed at runtime, and no float formatter is linked unless a frame declares a float
 * field. Adding a frame adds a table, not logic.
 *
 * **Contract.**
 *   - Returns the number of bytes written (excluding the NUL) on success.
 *   - Returns 0 if the frame does not fit, and writes `out[0] = '\0'`. There is no truncation:
 *     a partial frame is a protocol violation, and a caller that ignores the return still finds a
 *     valid empty C string rather than half a header or stale bytes from a previous build.
 *   - `out` is always NUL-terminated on both paths, so a caller may always read it as a string.
 *   - A NULL `PC_FK_STR` argument renders as empty, never as a crash or "(null)".
 *
 *
 * **Arguments** are passed variadically in spec order, one per field that declares one (PC_FK_LIT
 * and PC_FK_END take none). They are read at their default-promoted types, so a `uint8_t` passed
 * to PC_FK_U32 arrives as `unsigned` and a `float` passed to PC_FK_G arrives as `double`, which is
 * what the engine expects. The compiler cannot check that arity: a mismatched spec is caught by
 * its test, not by the build.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FRAME_H
#define PROTOCORE_FRAME_H

#include "mmgr/membuild.h"
#include <stdarg.h>

/**
 * @brief Field kinds. The value is an opcode, so the enum is the name for a byte, not a type gate.
 *
 * The width is carried by pc_field::kind (a uint8_t), not by the enum: C has no fixed underlying
 * type, and the storage is what the wire sees.
 */
typedef enum
{
    PC_FK_END = 0, ///< terminator; takes no argument
    PC_FK_LIT,     ///< literal text from `lit`; takes no argument
    PC_FK_STR,     ///< const char * (NULL renders as empty)
    PC_FK_U32,     ///< uint32_t, plain decimal
    PC_FK_U64,     ///< uint64_t, plain decimal
    PC_FK_I64,     ///< int64_t, signed decimal
    PC_FK_DEC,     ///< uint32_t, decimal zero-padded to `width`
    PC_FK_HEX,     ///< uint64_t, lowercase hex zero-padded to `width`
    PC_FK_OCT,     ///< uint64_t, octal zero-padded to `width`
    PC_FK_G,       ///< double, printf %.<width>g (width 0 means 6)
    PC_FK_FIX,     ///< double, printf %.<width>f
    PC_FK_CH,      ///< char
    PC_FK_JSON,    ///< const char *, emitted as a quoted JSON string literal
    PC_FK_XML,     ///< const char *, XML-escaped
} pc_fk;

/**
 * @brief One field of a frame. Frames are `static const pc_field[]`, so they live in rodata.
 *
 * @c len carries a literal's length, written out in the spec and verified by
 * ci_tooling/check/check_frame_specs.py. The length is fixed when the spec is written, so the
 * engine reads it rather than scanning each literal for its NUL on every call.
 */
typedef struct
{
    uint8_t kind;    ///< a pc_fk
    uint8_t width;   ///< min digits (DEC/HEX/OCT), significant digits (G), decimals (FIX)
    uint16_t len;    ///< PC_FK_LIT: byte length of @c lit; gated by check_frame_specs.py
    const char *lit; ///< PC_FK_LIT only
} pc_field;

// Spec constructors, one per valued field carrying neither a width nor a literal. Field order is
// {kind, width, len, lit}; a field that does carry one is written as a plain aggregate, because a
// macro taking it as a parameter would be function-like (AUTOSAR A16-0-1).
//
//   static const pc_field RESP[] = {
//       {PC_FK_LIT, 0, 9, "HTTP/1.1 "},   // 9 == the literal's length
//       PC_U32,
//       {PC_FK_HEX, 8, 0, NULL},          // 8 == zero-pad width
//       PC_END,
//   };
//
// check_frame_specs.py fails the build when a len disagrees with its literal; --fix rewrites it.
#define PC_STR {PC_FK_STR, 0, 0, NULL}
#define PC_U32 {PC_FK_U32, 0, 0, NULL}
#define PC_U64 {PC_FK_U64, 0, 0, NULL}
#define PC_I64 {PC_FK_I64, 0, 0, NULL}
#define PC_CH {PC_FK_CH, 0, 0, NULL}
#define PC_JSON {PC_FK_JSON, 0, 0, NULL}
#define PC_XML {PC_FK_XML, 0, 0, NULL}
#define PC_END {PC_FK_END, 0, 0, NULL}

/**
 * @brief Build @p spec into @p out (capacity @p cap), taking one variadic argument per valued field.
 * @return bytes written, or 0 if the frame did not fit (in which case @p out is set empty).
 */
size_t pc_frame_build(char *out, size_t cap, const pc_field *spec, ...);

/** @brief va_list form, for a caller that already has one. */
size_t pc_frame_vbuild(char *out, size_t cap, const pc_field *spec, va_list ap);

/**
 * @brief Append @p spec to the NUL-terminated contents already in @p out.
 *
 * The append idiom this library uses for header and cookie accumulation: on overflow the buffer is
 * rewound to its previous length, so a frame is added whole or not at all and a half-written line
 * never reaches the wire.
 *
 * @return the new total length, or 0 if the frame did not fit (previous contents preserved).
 */
size_t pc_frame_append(char *out, size_t cap, const pc_field *spec, ...);

#endif // PROTOCORE_FRAME_H
