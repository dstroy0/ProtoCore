// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file frame.h
 * @brief Declarative frame builder: a frame is a static table of typed fields, built by one engine.
 *
 * The problem this solves. Replacing printf-style formatting with hand-written pc_sb append
 * sequences trades one call for a dozen, and every one of those dozens is new code with its own
 * chance of a wrong order, a missing separator, or a forgotten overflow test. Across the ~160
 * formatting sites in this library that is well over a thousand new lines, none of which any test
 * covers individually. A frame SPEC moves that structure into data: the shape of the frame is a
 * `static const pc_field[]` in rodata, the engine that walks it is one function, and the engine is
 * what gets tested. Adding a frame after that adds a table, not logic.
 *
 * Why not a format string. A format string encodes the same table, but re-parses it from text on
 * every call - scanning characters, decoding widths, dispatching per conversion - to rediscover
 * what was known when the code was written. The spec is pre-decoded: the engine reads an opcode
 * and a width from a struct and jumps. Nothing is parsed at runtime, and no float formatter is
 * linked in unless a frame actually declares a float field.
 *
 * **Contract.**
 *   - Returns the number of bytes written (excluding the NUL) on success.
 *   - Returns 0 if the frame does not fit, and writes `out[0] = '\0'`. There is no truncation:
 *     a partial frame is a protocol violation, and a caller that ignores the return still finds a
 *     valid empty C string rather than half a header or stale bytes from a previous build.
 *   - `out` is always NUL-terminated on both paths, so a caller may always read it as a string.
 *   - A NULL `PC_FK_STR` argument renders as empty, never as a crash or "(null)".
 *
 * **Arguments** are passed variadically in spec order, one per field that declares one (PC_FK_LIT
 * and PC_FK_END take none). They are read at their default-promoted types, so a `uint8_t` passed
 * to PC_FK_U32 arrives as `unsigned` and a `float` passed to PC_FK_G arrives as `double`, which is
 * what the engine expects. The compiler cannot check that arity, but a spec's field count and its
 * call sites are visible in the same translation unit, so it is checkable offline - that gate lands
 * with the rollout, and until it does a mismatched spec is caught only by its test.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FRAME_H
#define PROTOCORE_FRAME_H

#include "shared_primitives/strbuf.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

// PC_ALLOW_UNSCOPED_ENUM: the value IS the opcode byte written to the wire and compared as an
// integer, so an enum class would put a static_cast at every use to satisfy the lint without
// making anything safer.
/** @brief Field kinds. The value is an opcode, so this is deliberately a plain byte enum. */
enum pc_fk : uint8_t
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
};

/**
 * @brief One field of a frame. Frames are `static const pc_field[]`, so they live in rodata.
 *
 * @c len carries a literal's length, written out in the spec and verified by
 * ci_tooling/check/check_frame_specs.py. A spec is fixed when the code is written, so having the
 * engine re-scan each literal for its NUL at runtime is the same waste as re-parsing a format
 * string: measured at +54% on a response frame and +184% on a literal-only one.
 */
struct pc_field
{
    uint8_t kind;    ///< a pc_fk
    uint8_t width;   ///< min digits (DEC/HEX/OCT), significant digits (G), decimals (FIX)
    uint16_t len;    ///< PC_FK_LIT: byte length of @c lit; gated by check_frame_specs.py
    const char *lit; ///< PC_FK_LIT only
};

// Spec constructors. These read as the frame they describe:
//   static const pc_field RESP[] = {{PC_FK_LIT, 0, 9, "HTTP/1.1 "}, PC_U32, {PC_FK_LIT, 0, 1, " "}, PC_STR, PC_END};
// Field order is {kind, width, len, lit}. A valued field that needs no width or literal takes an
// object-like macro; a field carrying a width or a literal is written as a plain aggregate, because
// a macro that took the width or the string as a parameter would be a function-like macro
// (AUTOSAR A16-0-1). The literal's length is therefore spelled out rather than computed:
//
//   static const pc_field RESP[] = {
//       {PC_FK_LIT, 0, 9, "HTTP/1.1 "},   // 9 == the literal's length
//       PC_U32,
//       {PC_FK_HEX, 8, 0, nullptr},       // 8 == zero-pad width
//       PC_END,
//   };
//
// Hand-counting is not trusted: ci_tooling/check/check_frame_specs.py fails the build when any
// PC_FK_LIT field's len disagrees with its literal, and --fix rewrites it.
#define PC_STR {PC_FK_STR, 0, 0, nullptr}
#define PC_U32 {PC_FK_U32, 0, 0, nullptr}
#define PC_U64 {PC_FK_U64, 0, 0, nullptr}
#define PC_I64 {PC_FK_I64, 0, 0, nullptr}
#define PC_CH {PC_FK_CH, 0, 0, nullptr}
#define PC_JSON {PC_FK_JSON, 0, 0, nullptr}
#define PC_XML {PC_FK_XML, 0, 0, nullptr}
#define PC_END {PC_FK_END, 0, 0, nullptr}

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
