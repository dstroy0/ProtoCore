// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hostlink.h
 * @brief Omron Host Link (C-mode) frame codec (PROTOCORE_ENABLE_HOSTLINK) - zero-heap ASCII
 *        command/response framing for the Omron serial host-link protocol, the RS-232/485
 *        sibling of FINS.
 *
 * A Host Link frame is ASCII:
 * @code
 *   @ UU XX <text> FF * CR
 * @endcode
 *  - `@` start, `UU` the 2-digit unit/node number, `XX` the 2-char header code (e.g. `RD`),
 *    `<text>` the data, `FF` the 2-hex-char FCS, then the `*` + CR (0x0D) terminator.
 *  - FCS = the 8-bit XOR of every character from `@` through the last text character,
 *    rendered as two uppercase hex digits.
 *  - A response's text begins with a 2-char end code (00 = normal).
 *
 * This is the frame codec (build + FCS-validated parse); the serial transport is the app's.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HOSTLINK_H
#define PROTOCORE_HOSTLINK_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HOSTLINK

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief A parsed frame; @ref text points INTO the source buffer (after the header, before the FCS). */
typedef struct
{
    uint8_t node;
    char header_code[3]; ///< 2 chars + NUL
    const char *text;
    size_t text_len;
} HostlinkFrame;

/** @brief What fcs takes: data, len. */
typedef struct
{
    const char *data;
    size_t len;
} HostlinkFcsArgs;

/** @brief What build takes: buf, cap, node, header_code, text, ... */
typedef struct
{
    char *buf;
    size_t cap;
    uint8_t node;            ///< unit/node number (0-99, rendered as 2 BCD-style digits)
    const char *header_code; ///< the 2-character header code (e.g. "RD"); must be 2 chars
    const char *text;
    size_t text_len;
} HostlinkBuildArgs;

/** @brief What parse takes: buf, len, out. */
typedef struct
{
    const char *buf;
    size_t len;
    HostlinkFrame *out;
} HostlinkParseArgs;

/** @brief What end_code takes: f, code. */
typedef struct
{
    const HostlinkFrame *f;
    uint8_t *code;
} HostlinkEndCodeArgs;

/** @brief What build_read takes: buf, cap, node, address, count. */
typedef struct
{
    char *buf;
    size_t cap;
    uint8_t node;
    uint16_t address;
    uint16_t count;
} HostlinkBuildReadArgs;

/** @brief What read_word takes: f, index, out. */
typedef struct
{
    const HostlinkFrame *f;
    size_t index;
    uint16_t *out;
} HostlinkReadWordArgs;

/** @brief What build_write takes: buf, cap, node, address, words, ... */
typedef struct
{
    char *buf;
    size_t cap;
    uint8_t node;
    uint16_t address;
    const uint16_t *words;
    size_t word_count;
} HostlinkBuildWriteArgs;

/**
 * @brief Omron Host Link (C-mode) frame codec (PROTOCORE_ENABLE_HOSTLINK) - zero-heap ASCII command/response framing
 * for the Omron serial host-link protocol, the RS-232/485 sibling of FINS.
 *
 * A caller sets the members a call takes, invokes it through ::Hostlink with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Hostlink.fcs_args.data = ...;
 *   Hostlink.fcs_args.len = ...;
 *   Hostlink.fcs(work);
 *   // Hostlink.value is what the call reports
 *
 * @var HostlinkNs::fcs_args  what fcs takes: data, len
 * @var HostlinkNs::build_args  what build takes: buf, cap, node, header_code, text,
 * @var HostlinkNs::parse_args  what parse takes: buf, len, out
 * @var HostlinkNs::end_code_args  what end_code takes: f, code
 * @var HostlinkNs::build_read_args  what build_read takes: buf, cap, node, address, count
 * @var HostlinkNs::read_word_args  what read_word takes: f, index, out
 * @var HostlinkNs::build_write_args  what build_write takes: buf, cap, node, address, words,
 * @var HostlinkNs::ok  true on a complete, FCS-valid `@...*CR` frame; false otherwise
 * @var HostlinkNs::value  the value a call reports
 * @var HostlinkNs::n  total characters written (NOT counting the NUL), or 0 on overflow / ...
 * @var HostlinkNs::fcs  FCS: 8-bit XOR of [data, data+len)
 * @var HostlinkNs::build  build a frame: `@UU` + header_code(2) + text + FCS(2 hex) + `*` + CR
 * @var HostlinkNs::parse  parse + FCS-validate a frame (command or response)
 * @var HostlinkNs::end_code  read a response's 2-char end code (the first two text characters) ...
 * @var HostlinkNs::build_read  build an RD (DM-area read) command: `@UU` + `RD` + a 4-digit ...
 * @var HostlinkNs::read_word  extract word index (0-based) from an RD response's text: a ...
 * @var HostlinkNs::build_write  build a WR (DM-area write) command: `@UU` + `WR` + a 4-digit ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    HostlinkFcsArgs fcs_args;
    HostlinkBuildArgs build_args;
    HostlinkParseArgs parse_args;
    HostlinkEndCodeArgs end_code_args;
    HostlinkBuildReadArgs build_read_args;
    HostlinkReadWordArgs read_word_args;
    HostlinkBuildWriteArgs build_write_args;
    proto_bool ok;
    uint8_t value;
    size_t n;
} HostlinkVars;

/** @brief The operands and the outcome. */
extern HostlinkVars HostlinkV;

/** @brief The entries. */
typedef struct
{
    void (*const fcs)(uint8_t *restrict work);
    void (*const build)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
    void (*const end_code)(uint8_t *restrict work);
    void (*const build_read)(uint8_t *restrict work);
    void (*const read_word)(uint8_t *restrict work);
    void (*const build_write)(uint8_t *restrict work);
} HostlinkNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HostlinkV or a region of the borrow at a fixed offset.
void protocore_hostlink_fcs(uint8_t *restrict work);
void protocore_hostlink_build(uint8_t *restrict work);
void protocore_hostlink_parse(uint8_t *restrict work);
void protocore_hostlink_end_code(uint8_t *restrict work);
void protocore_hostlink_build_read(uint8_t *restrict work);
void protocore_hostlink_read_word(uint8_t *restrict work);
void protocore_hostlink_build_write(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Hostlink.fcs(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HostlinkNs Hostlink __attribute__((unused)) = {
    .fcs = protocore_hostlink_fcs,
    .build = protocore_hostlink_build,
    .parse = protocore_hostlink_parse,
    .end_code = protocore_hostlink_end_code,
    .build_read = protocore_hostlink_build_read,
    .read_word = protocore_hostlink_read_word,
    .build_write = protocore_hostlink_build_write,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HOSTLINK

#endif // PROTOCORE_HOSTLINK_H
