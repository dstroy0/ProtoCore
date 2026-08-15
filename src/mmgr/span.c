// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file span.c
 * @brief The bounded byte-region accessors - see span.h.
 *
 * A constructor normalizes the empty case: a pointer with no capacity, or a capacity with no
 * pointer, becomes `{NULL, 0}`, so the two fields never disagree. A sub-region clamps its offset and
 * its length to what the parent holds, so a truncated frame yields a zero-capacity region instead of
 * a pointer past the allocation.
 *
 * The one symbol this file exports is @ref span.
 */

#include "mmgr/span.h"

protocore_span protocore_span_from(uint8_t *p, size_t cap)
{
    protocore_span s;
    s.buf = (p != NULL && cap != 0) ? p : NULL;
    s.cap = (s.buf != NULL) ? cap : 0;
    s.pos = 0;
    s.overflow = PROTO_FALSE;
    return s;
}

protocore_cspan protocore_cspan_from(const uint8_t *p, size_t len)
{
    protocore_cspan s;
    s.buf = (p != NULL && len != 0) ? p : NULL;
    s.len = (s.buf != NULL) ? len : 0;
    s.pos = 0;
    s.err = PROTO_FALSE;
    return s;
}

proto_bool protocore_span_ok(protocore_span s)
{
    return s.buf != NULL && !s.overflow;
}

proto_bool protocore_span_has_storage(protocore_span s)
{
    return s.buf != NULL;
}

proto_bool protocore_cspan_ok(protocore_cspan s)
{
    return s.buf != NULL && !s.err;
}

size_t protocore_span_len(protocore_span s)
{
    return s.pos;
}

size_t protocore_span_room(protocore_span s)
{
    return (s.pos < s.cap) ? (s.cap - s.pos) : 0;
}

void protocore_span_reset(protocore_span *s)
{
    s->pos = 0;
    s->overflow = PROTO_FALSE;
}

protocore_span protocore_span_after(protocore_span s, size_t off)
{
    if (!protocore_span_has_storage(s) || off >= s.cap)
    {
        return protocore_span_from(NULL, 0);
    }
    return protocore_span_from(s.buf + off, s.cap - off);
}

protocore_span protocore_span_first(protocore_span s, size_t n)
{
    if (!protocore_span_has_storage(s))
    {
        return protocore_span_from(NULL, 0);
    }
    return protocore_span_from(s.buf, (n < s.cap) ? n : s.cap);
}

protocore_cspan protocore_span_produced(protocore_span s)
{
    if (!protocore_span_ok(s))
    {
        return protocore_cspan_from(NULL, 0);
    }
    return protocore_cspan_from(s.buf, s.pos);
}

protocore_cspan protocore_span_read(protocore_span s, size_t len)
{
    if (!protocore_span_has_storage(s))
    {
        return protocore_cspan_from(NULL, 0);
    }
    return protocore_cspan_from(s.buf, (len < s.cap) ? len : s.cap);
}
