// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protostr.h
 * @brief Operations over a bounded run of bytes: where it ends, where two part company, whether one
 *        occurs inside another, and moving one into a bounded destination.
 *
 * In mmgr beside @ref mem because both walk what mmgr hands out. That file moves a span given its
 * length; this one answers where the length is.
 *
 * Every entry point takes an explicit capacity. A bound is the caller's statement about the object,
 * not a hint: `nul_cap` is how many bytes may be read looking for a terminator, `read_cap` is a
 * promise that many bytes ARE readable, and `dst_cap` is what a write may fill.
 *
 * Every comparison takes @c ci, which folds bit 5 on ASCII letters so a compare ignores case. It is
 * the switch inside the one body, not a second entry point.
 *
 * The module exports one symbol, @ref str. Everything in protostr.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PROTOSTR_H
#define PROTOCORE_PROTOSTR_H

#include "mmgr/swar.h" // pc_swar_word and PC_SWAR_GO/YES/NO: the step members' currency
#include "protocore_config.h"

PROTO_BEGIN_DECLS

/**
 * @brief The bounded-run module. Every walk steps one register-width load at a time.
 *
 * @var StrNs::len
 * Index of the first NUL in @c s within @c nul_cap bytes, or @c nul_cap if there is none. The
 * bounded strnlen this library wants, a word per test instead of a byte.
 *
 * **@c nul_cap is a willingness, not a promise.** It says how far this may keep looking, and nothing
 * about how much of @c s exists: `str.len(payload, 0xFFFF)` over a five-byte literal is the intended
 * use. That is the opposite of the @c read_cap every compare below takes.
 *
 * @var StrNs::diff
 * Index of the first byte where @c a and @c b differ, or @c read_cap if they agree throughout. XOR
 * zeroes every matching lane, so the lanes that differ are exactly the nonzero ones and the lane
 * math names the first of them.
 *
 * **@c read_cap is a promise, not a willingness.** Both operands must hold that many readable bytes,
 * because this never looks for a terminator and so has nothing else to stop it. Every compare below
 * carries the same contract.
 *
 * @var StrNs::eq
 * True when @c a and @c b are the same NUL-terminated string within @c read_cap bytes. A prefix
 * never passes as the whole: the terminator has to arrive strictly before the first disagreement.
 *
 * @var StrNs::starts
 * True when @c s begins with @c pre, both holding @c read_cap readable bytes. The same walk @ref
 * StrNs::eq runs, reading the tie the other way: the pattern ended, whatever the subject does next.
 *
 * @var StrNs::find
 * First occurrence of @c needle in @c hay within @c read_cap bytes, or NULL. The haystack is walked
 * once: one load marks every lane holding the needle's anchor byte and the lane the haystack ends
 * on, and a candidate is settled by a masked difference against the needle word.
 *
 * An empty needle matches at the start. A haystack NUL ends the search but does not truncate it:
 * @c read_cap bounds the walk, and a needle byte is never NUL, so a terminated lane always fails
 * the match rather than passing.
 *
 * @var StrNs::has
 * Whether @c needle occurs in @c hay. The same scan @ref StrNs::find runs, asked for less.
 *
 * @var StrNs::copy
 * Copy the NUL-terminated @c src into @c dst, which holds @c dst_cap bytes, and return the length
 * written. Always terminates, and writes only the bytes it copies. A @c dst_cap of 0 writes nothing.
 * The bound belongs to the DESTINATION: nothing is claimed about how long @c src is.
 *
 * @var StrNs::step_word
 * One word of the agreement test, given the two words the caller already loaded: @ref PC_SWAR_GO to
 * keep stepping, @ref PC_SWAR_YES or @ref PC_SWAR_NO once this word settles it. Whichever lane fires
 * lower decides, so nothing past this word is read. @c end_wins reads the tie as a match, which is
 * what a prefix test wants and what an equality test does not.
 *
 * This is what a walk steps with instead of a byte at a time.
 *
 * @var StrNs::step_byte
 * @ref StrNs::step_word for one byte, serving the head and the tail a whole word cannot cover. Same
 * rule, one lane.
 *
 * @var StrNs::ws
 * Whether @c c is one of the six ASCII whitespace characters.
 *
 * @var StrNs::digit
 * Whether @c c is an ASCII decimal digit.
 *
 * @var StrNs::to_long
 * Read a base-10 long from @c s. Skips leading whitespace, takes an optional sign, consumes digits,
 * and points @c end past them - or back at @c s when no digit converted, which is how a caller tells
 * "no number" from a parsed zero. @c end may be NULL.
 *
 * @var StrNs::to_ulong
 * @ref StrNs::to_long without the sign: a leading `+` is consumed, a `-` is not.
 *
 * @var StrNs::to_double
 * Read `integer[.frac][e[+/-]exp]` from @c s, with @ref StrNs::to_long's @c end contract. The
 * exponent is clamped, because past ten to the four hundredth the double is already infinite.
 *
 * @var StrNs::to_float
 * @ref StrNs::to_double narrowed. It parses at double precision first, which is what a sub-meter
 * value like a GGA latitude needs to land on the right float.
 *
 * **None of these are constant time.** Each stops at the byte that settles it, so how long it runs
 * states how many leading bytes matched. A secret comparison uses pc_ct_eq (crypto/ct_eq.h).
 *
 * No storage member: every operation works on the caller's pointers and holds nothing of its own.
 */
typedef struct
{
    size_t (*len)(const char *s, size_t nul_cap);
    size_t (*diff)(const char *a, const char *b, size_t read_cap, proto_bool ci);
    proto_bool (*eq)(const char *a, const char *b, size_t read_cap, proto_bool ci);
    proto_bool (*starts)(const char *s, const char *pre, size_t read_cap, proto_bool ci);
    const char *(*find)(const char *hay, size_t read_cap, const char *needle, size_t needle_cap, proto_bool ci);
    proto_bool (*has)(const char *hay, size_t read_cap, const char *needle, size_t needle_cap, proto_bool ci);
    size_t (*copy)(char *dst, const char *src, size_t dst_cap);
    int (*step_word)(pc_swar_word wa, pc_swar_word wb, proto_bool ci, int end_wins);
    int (*step_byte)(unsigned char ca, unsigned char cb, proto_bool ci, int end_wins);
    proto_bool (*ws)(char c);
    proto_bool (*digit)(char c);
    long (*to_long)(const char *s, const char **end);
    unsigned long (*to_ulong)(const char *s, const char **end);
    double (*to_double)(const char *s, const char **end);
    float (*to_float)(const char *s, const char **end);
} StrNs;

/** @brief The one symbol this module exports. */
extern const StrNs str;

PROTO_END_DECLS

#endif // PROTOCORE_PROTOSTR_H
