// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_memfns.c
 * @brief The four block functions a freestanding image still has to name, over mmgr's own.
 *
 * A freestanding build has no C library, but it is not free of these: the standard requires memcpy,
 * memmove, memset and memcmp even there, and the compiler emits calls to them on its own for a
 * struct assignment, a zero-init or a large aggregate compare - code that names none of them. So an
 * image has to carry them whatever the source says.
 *
 * Nothing new is written here. mmgr already moves and compares blocks, so these are the symbols the
 * linker asks for, resolved onto @ref memor. The library's own callers keep using @ref mem directly;
 * this exists for the calls the compiler makes.
 *
 * The optimizer is told to leave the bodies alone: it recognises a byte loop and rewrites it as a
 * call to the very function being defined, which on a freestanding target is an infinite recursion
 * rather than a library call.
 */

#include "memoria_operor/memoria_operor.h"

#include <stddef.h>

#if defined(__GNUC__) && !defined(__clang__)
#define PROTOCORE_NO_MEM_IDIOM __attribute__((optimize("no-tree-loop-distribute-patterns")))
#else
#define PROTOCORE_NO_MEM_IDIOM
#endif

PROTOCORE_NO_MEM_IDIOM void *memcpy(void *dst, const void *src, size_t n)
{
    EMBED_CALL(memor.cpy, MemoriaCfg, .dst = dst, .src = src, .bytes = n);
    return dst;
}

PROTOCORE_NO_MEM_IDIOM void *memmove(void *dst, const void *src, size_t n)
{
    if ((const unsigned char *)dst < (const unsigned char *)src)
    {
        EMBED_CALL(memor.move_down, MemoriaCfg, .dst = dst, .src = src, .bytes = n);
    }
    else
    {
        EMBED_CALL(memor.move_up, MemoriaCfg, .dst = dst, .src = src, .bytes = n);
    }
    return dst;
}

PROTOCORE_NO_MEM_IDIOM void *memset(void *dst, int v, size_t n)
{
    EMBED_CALL(memor.set, MemoriaCfg, .dst = dst, .val = (unsigned char)v, .bytes = n);
    return dst;
}

PROTOCORE_NO_MEM_IDIOM int memcmp(const void *a, const void *b, size_t n)
{
    return EMBED_CALL(memor.cmp, MemoriaCfg, .src = a, .other = b, .bytes = n);
}
