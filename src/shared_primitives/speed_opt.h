// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file speed_opt.h
 * @brief Build one file at a named optimization level instead of the framework's.
 *
 * The framework's size-optimized level is appended after any build-system flags, so a consumer
 * cannot raise it from outside. That level declines to inline small functions, and an appender
 * chain that does not inline cannot fold: a literal's length stays a runtime scan and a bounded
 * copy stays a call. Put @ref PC_OPTIMIZE_O2 at the top of such a `.cpp`, after its includes.
 *
 * The level is in the name rather than behind a knob, so a file states what it is built at and a
 * reader does not have to resolve a second macro to find out. A macro rather than the pragma itself
 * because core carries no toolchain language.
 *
 * O2 is the only level here because O2 is what has been measured (docs/FEATURE_PERFORMANCE.md 2b).
 * O3 is untested on this path - not rejected. A file that needs another level adds the macro for it
 * here rather than redefining this one.
 *
 * Apply only where there are no secrets or the code is constant-time by structure. An optimizer can
 * turn a branchless mask-select into a data-dependent branch; `crypto/crypto_opt.h` is the crypto
 * policy layer with per-die levels and those caveats.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SPEED_OPT_H
#define PROTOCORE_SPEED_OPT_H

// The capability is branched on ONCE, here. Each level below is then defined exactly once, so a
// level cannot end up spelled differently in two arms, and adding one does not touch this test.
// A toolchain without the per-file pragma expands to nothing and keeps its own level - speed only,
// never behaviour.
#if defined(__GNUC__) && !defined(__clang__)
#define PC_TU_PRAGMA(directive) _Pragma(#directive)
#else
#define PC_TU_PRAGMA(directive)
#endif

/** @brief Build this file at optimization level 2, overriding the framework's size-optimized level. */
#define PC_OPTIMIZE_O2 PC_TU_PRAGMA(GCC optimize("O2"))

#endif // PROTOCORE_SPEED_OPT_H
