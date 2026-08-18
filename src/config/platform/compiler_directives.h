// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file compiler_directives.h
 * @brief Compiler-specific directives for the ProtoCore library.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_COMPILER_DIRECTIVES_H
#define PROTOCORE_COMPILER_DIRECTIVES_H

/**
 * @brief Linkage for a leaf primitive whose body is cheaper than the call that reaches it.
 *
 * Stated here rather than in protocore_config.h because that header reaches this one through
 * board_profile.h before it defines anything of its own, so this is the earliest point the
 * linkage can be settled. protocore_config.h keeps the same definition behind #ifndef, which
 * covers a translation unit that arrives without this header.
 */
#ifndef PROTOCORE_INLINE
#if defined(__GNUC__)
#define PROTOCORE_INLINE static inline __attribute__((always_inline))
#else
#define PROTOCORE_INLINE static inline
#endif
#endif

#endif // PROTOCORE_COMPILER_DIRECTIVES_H
