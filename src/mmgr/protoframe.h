// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protoframe.h
 * @brief Declarative frame builder: a frame is a static table of typed fields, built by one engine.
 *
 * A frame's shape is data, not code: a `static const protocore_field[]` in rodata, walked by one engine.
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
 *   - A NULL `PROTOCORE_FK_STR` argument renders as empty, never as a crash or "(null)".
 *
 *
 * **Arguments** are one ::protocore_fval per field that declares one (PROTOCORE_FK_LIT and PROTOCORE_FK_END declare
 * none), in spec order, passed as an array with its count. Each value carries the ::protocore_fk it was
 * written as, so the engine compares it against the spec's and refuses a frame whose values do not
 * match - the arity and the type are both checked at the call instead of trusting the caller.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PROTOFRAME_H
#define PROTOCORE_PROTOFRAME_H

#include "mmgr/membuild.h"

/**
 * @brief Field kinds. The value is an opcode, so the enum is the name for a byte, not a type gate.
 *
 * The width is carried by protocore_field::kind (a uint8_t), not by the enum: C has no fixed underlying
 * type, and the storage is what the wire sees.
 */
typedef enum
{
    PROTOCORE_FK_END = 0, ///< terminator; takes no argument
    PROTOCORE_FK_LIT,     ///< literal text from `lit`; takes no argument
    PROTOCORE_FK_STR,     ///< const char * (NULL renders as empty)
    PROTOCORE_FK_U32,     ///< uint32_t, plain decimal
    PROTOCORE_FK_U64,     ///< uint64_t, plain decimal
    PROTOCORE_FK_I64,     ///< int64_t, signed decimal
    PROTOCORE_FK_DEC,     ///< uint32_t, decimal zero-padded to `width`
    PROTOCORE_FK_HEX,     ///< uint64_t, lowercase hex zero-padded to `width`
    PROTOCORE_FK_OCT,     ///< uint64_t, octal zero-padded to `width`
    PROTOCORE_FK_G,       ///< double, printf %.<width>g (width 0 means 6)
    PROTOCORE_FK_FIX,     ///< double, printf %.<width>f
    PROTOCORE_FK_CH,      ///< char
    PROTOCORE_FK_JSON,    ///< const char *, emitted as a quoted JSON string literal
    PROTOCORE_FK_XML,     ///< const char *, XML-escaped
} protocore_fk;

/**
 * @brief One field of a frame. Frames are `static const protocore_field[]`, so they live in rodata.
 *
 * @c len carries a literal's length, written out in the spec and verified by
 * ci_tooling/check/check_frame_specs.py. The length is fixed when the spec is written, so the
 * engine reads it rather than scanning each literal for its NUL on every call.
 */
typedef struct protocore_field
{
    uint8_t kind;    ///< a protocore_fk
    uint8_t width;   ///< min digits (DEC/HEX/OCT), significant digits (G), decimals (FIX)
    uint16_t len;    ///< PROTOCORE_FK_LIT: byte length of @c lit; gated by check_frame_specs.py
    const char *lit; ///< PROTOCORE_FK_LIT only
} protocore_field;

// Spec constructors, one per valued field carrying neither a width nor a literal. Field order is
// {kind, width, len, lit}; a field that does carry one is written as a plain aggregate, because a
// macro taking it as a parameter would be function-like (AUTOSAR A16-0-1).
//
//   static const protocore_field RESP[] = {
//       {PROTOCORE_FK_LIT, 0, 9, "HTTP/1.1 "},   // 9 == the literal's length
//       PROTOCORE_U32,
//       {PROTOCORE_FK_HEX, 8, 0, NULL},          // 8 == zero-pad width
//       PROTOCORE_END,
//   };
//
// check_frame_specs.py fails the build when a len disagrees with its literal; --fix rewrites it.
#define PROTOCORE_STR {PROTOCORE_FK_STR, 0, 0, NULL}
#define PROTOCORE_U32 {PROTOCORE_FK_U32, 0, 0, NULL}
#define PROTOCORE_U64 {PROTOCORE_FK_U64, 0, 0, NULL}
#define PROTOCORE_I64 {PROTOCORE_FK_I64, 0, 0, NULL}
#define PROTOCORE_CH {PROTOCORE_FK_CH, 0, 0, NULL}
#define PROTOCORE_JSON {PROTOCORE_FK_JSON, 0, 0, NULL}
#define PROTOCORE_XML {PROTOCORE_FK_XML, 0, 0, NULL}
#define PROTOCORE_END {PROTOCORE_FK_END, 0, 0, NULL}

/**
 * @brief One argument to a frame: the ::protocore_fk it was written as, and the value under that tag.
 *
 * The tag is the discriminant the engine dispatches on, and it is compared against the spec's own
 * kind before the value is read, so a caller that passes a string where the spec wants a number is
 * refused rather than reinterpreted.
 */
typedef struct protocore_fval
{
    uint8_t kind; ///< a protocore_fk; must equal the spec field's kind
    union {
        const char *s; ///< PROTOCORE_FK_STR, PROTOCORE_FK_JSON, PROTOCORE_FK_XML
        uint32_t u32;  ///< PROTOCORE_FK_U32, PROTOCORE_FK_DEC
        uint64_t u64;  ///< PROTOCORE_FK_U64, PROTOCORE_FK_HEX, PROTOCORE_FK_OCT
        int64_t i64;   ///< PROTOCORE_FK_I64
        double d;      ///< PROTOCORE_FK_G, PROTOCORE_FK_FIX
        char c;        ///< PROTOCORE_FK_CH
    } as;
} protocore_fval;

// Value constructors, one per valued kind. Written as a compound literal at the call, so the
// argument list stays a list and no named array appears at function scope.
#define PROTOCORE_VSTR(x)                                                                                              \
    {                                                                                                                  \
        PROTOCORE_FK_STR,                                                                                              \
        {                                                                                                              \
            .s = (x)                                                                                                   \
        }                                                                                                              \
    }
#define PROTOCORE_VU32(x)                                                                                              \
    {                                                                                                                  \
        PROTOCORE_FK_U32,                                                                                              \
        {                                                                                                              \
            .u32 = (x)                                                                                                 \
        }                                                                                                              \
    }
#define PROTOCORE_VU64(x)                                                                                              \
    {                                                                                                                  \
        PROTOCORE_FK_U64,                                                                                              \
        {                                                                                                              \
            .u64 = (x)                                                                                                 \
        }                                                                                                              \
    }
#define PROTOCORE_VI64(x)                                                                                              \
    {                                                                                                                  \
        PROTOCORE_FK_I64,                                                                                              \
        {                                                                                                              \
            .i64 = (x)                                                                                                 \
        }                                                                                                              \
    }
#define PROTOCORE_VDEC(x)                                                                                              \
    {                                                                                                                  \
        PROTOCORE_FK_DEC,                                                                                              \
        {                                                                                                              \
            .u32 = (x)                                                                                                 \
        }                                                                                                              \
    }
#define PROTOCORE_VHEX(x)                                                                                              \
    {                                                                                                                  \
        PROTOCORE_FK_HEX,                                                                                              \
        {                                                                                                              \
            .u64 = (x)                                                                                                 \
        }                                                                                                              \
    }
#define PROTOCORE_VOCT(x)                                                                                              \
    {                                                                                                                  \
        PROTOCORE_FK_OCT,                                                                                              \
        {                                                                                                              \
            .u64 = (x)                                                                                                 \
        }                                                                                                              \
    }
#define PROTOCORE_VG(x)                                                                                                \
    {                                                                                                                  \
        PROTOCORE_FK_G,                                                                                                \
        {                                                                                                              \
            .d = (x)                                                                                                   \
        }                                                                                                              \
    }
#define PROTOCORE_VFIX(x)                                                                                              \
    {                                                                                                                  \
        PROTOCORE_FK_FIX,                                                                                              \
        {                                                                                                              \
            .d = (x)                                                                                                   \
        }                                                                                                              \
    }
#define PROTOCORE_VCH(x)                                                                                               \
    {                                                                                                                  \
        PROTOCORE_FK_CH,                                                                                               \
        {                                                                                                              \
            .c = (x)                                                                                                   \
        }                                                                                                              \
    }
#define PROTOCORE_VJSON(x)                                                                                             \
    {                                                                                                                  \
        PROTOCORE_FK_JSON,                                                                                             \
        {                                                                                                              \
            .s = (x)                                                                                                   \
        }                                                                                                              \
    }
#define PROTOCORE_VXML(x)                                                                                              \
    {                                                                                                                  \
        PROTOCORE_FK_XML,                                                                                              \
        {                                                                                                              \
            .s = (x)                                                                                                   \
        }                                                                                                              \
    }

/**
 * @brief Build @p spec into @p out (capacity @p cap) from @p nv values in spec order.
 * @return bytes written, or 0 if the frame did not fit or the values did not match the spec (in
 *         which case @p out is set empty).
 */
size_t protocore_frame_build(char *out, size_t cap, const protocore_field *spec, const protocore_fval *v, size_t nv);

/**
 * @brief Append @p spec to the NUL-terminated contents already in @p out.
 *
 * The append idiom this library uses for header and cookie accumulation: on overflow the buffer is
 * rewound to its previous length, so a frame is added whole or not at all and a half-written line
 * never reaches the wire.
 *
 * @return the new total length, or 0 if the frame did not fit (previous contents preserved).
 */
size_t protocore_frame_append(char *out, size_t cap, const protocore_field *spec, const protocore_fval *v, size_t nv);

/**
 * @brief The two builds a caller reaches.
 *
 * @var FrameNs::build   write a frame into an empty buffer
 * @var FrameNs::append  add a frame to what the buffer already holds
 */
typedef struct
{
    size_t (*build)(char *out, size_t cap, const protocore_field *spec, const protocore_fval *v, size_t nv);
    size_t (*append)(char *out, size_t cap, const protocore_field *spec, const protocore_fval *v, size_t nv);
} FrameNs;

/**
 * @brief The names, aliased.
 *
 * `static const` and initialized here, not declared `extern` against a definition in the .c: a
 * translation unit that can see this initializer knows which function each member holds, so a member
 * read folds away and the call to the build in protoframe.c is direct, leaving the table referenced
 * by nothing for the linker to drop.
 *
 * `unused` because a header this wide is included by files that take none of it.
 */
static const FrameNs frame __attribute__((unused)) = {protocore_frame_build, protocore_frame_append};

#endif // PROTOCORE_PROTOFRAME_H
