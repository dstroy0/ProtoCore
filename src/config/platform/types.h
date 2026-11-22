// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file types.h
 * @brief The primitive types every other file is written in. They are those of embedded_types.
 *
 * embed_types.h states each width, embed_bool, ::embed_word and ::embed_index. The build states
 * EMBED_WORD_BITS and EMBED_INDEX_BITS for each target, so that ProtoCore, MMgr and idemIP read one word and
 * one index.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TYPES_H
#define PROTOCORE_TYPES_H

#include <assert.h> // C11 spells static_assert here; C++ has it built in
#include <stddef.h> // size_t, and the one place it enters the library
#include <stdint.h>

#include "embed_atomic.h"              // the atomic words, for each field that two tasks reach
#include "embed_compiler_directives.h" // EMBED_INLINE, EMBED_BEGIN_DECLS, EMBED_ENUM_BYTE
#include "embed_dispatch_layout.h"     // EMBED_CALL, which the C++ arm below restates
#include "embed_types.h"               // the widths, embed_bool, embed_word and embed_index


// C++ has no compound literal to take the address of, so a C++ translation unit builds the
// argument struct as a temporary. It lives to the end of the full expression, which is the call.
#ifdef __cplusplus
extern "C++"
{
    template <typename ArgsType> const ArgsType *protocore_call_args(const ArgsType &args)
    {
        return &args;
    }
}
#undef EMBED_CALL
#define EMBED_CALL(entry_, ArgsType_, ...) entry_(protocore_call_args(ArgsType_{__VA_ARGS__}))
#endif

#endif // PROTOCORE_TYPES_H
