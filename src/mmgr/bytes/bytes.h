// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bytes.h
 * @brief The byte verbs - append into a protocore_span, take out of a protocore_cspan.
 *
 * A bounded byte region is one thing with two accessors. span.h is the region: where the storage came
 * from, how big it is, how much has been produced, and whether anything overran. This file is what you
 * do to it.
 *
 * A write past the capacity stores nothing, latches the sticky overflow flag, and still advances
 * `pos`, so `pos` reports the capacity the payload needs. Widths go out and come back big-endian
 * (network order).
 *
 * These take protocore_span / protocore_cspan directly rather than being written per codec, so one
 * concrete pair serves every codec and the field names are fixed rather than restated.
 *
 * The surface is @ref bytes. The walks are in bytes.c.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BYTES_H
#define PROTOCORE_BYTES_H

#include "mmgr/endian/endian.h"   // protocore_rd32be - the fixed-width serializers the reads step with
#include "mmgr/protomem/protomem.h" // mem.set / mem.cpy - the byte movers
#include "mmgr/protostr/protostr.h" // str.len - the bounded run length
#include "mmgr/span/span.h"     // protocore_span / protocore_cspan - the region these verbs act on


PROTOCORE_BEGIN_DECLS

/**
 * @brief The byte-verb module: append into a write region, take out of a read region, and walk a
 *        caller-owned payload by offset.
 *
 * @var BytesNs::put
 * Append one byte to @c w. Past @c cap nothing is stored and the overflow flag latches; @c pos counts
 * the byte either way.
 *
 * @var BytesNs::put_be
 * Append the low @c nbytes of @c val most significant byte first, one @ref BytesNs::put per byte.
 *
 * @var BytesNs::raw
 * Append @c n bytes from @c src. Stores when @c w has storage and @c n fits the room that remains,
 * otherwise latches the overflow flag; @c pos advances by @c n either way.
 *
 * @var BytesNs::take_be
 * Read @c nbytes big-endian at @c r's cursor and advance past them. A read reaching past the end sets
 * the sticky err, leaves the cursor and @c out untouched, and returns false.
 *
 * Reads at the cursor and nowhere else: no framing byte is consumed here. A codec that leads with a
 * tag - CBOR's head byte, MessagePack's format byte - advances past it itself.
 *
 * @var BytesNs::rd_u32
 * Read a big-endian u32 at @c *off and advance it by 4. False when fewer than four bytes of @c len
 * remain, with @c *off left where it started.
 *
 * @var BytesNs::rd_str
 * Read a u32-length-prefixed blob: @c out points into @c p and @c slen is its length. Nothing is
 * copied, so the result must not outlive @c p. A length reaching past the end leaves @c *off where it
 * started and returns false. SSH names this shape a "string" (RFC 4251 sec 5).
 *
 * Every bound here subtracts against the space that remains, after establishing @c *off is within
 * @c len so the subtraction cannot wrap. size_t is 32 bits on some targets and the length prefix is a
 * full u32, so the sum form wraps on a peer-chosen length.
 *
 * @var BytesNs::mpint_fixed
 * Strip an mpint's leading zero bytes and right-align the rest into @c out[@c outlen], zeroing the
 * lanes ahead of it. False when the magnitude is wider than @c outlen.
 *
 * No storage member: every verb works on the caller's region or pointers and holds nothing of its own.
 */
typedef struct
{
    void (*put)(protocore_span *w, uint8_t b);
    void (*put_be)(protocore_span *w, uint64_t val, int32_t nbytes);
    void (*raw)(protocore_span *w, const void *src, size_t n);
    proto_bool (*take_be)(protocore_cspan *r, size_t nbytes, uint64_t *out);
    proto_bool (*rd_u32)(const uint8_t *p, size_t len, size_t *off, uint32_t *out);
    proto_bool (*rd_str)(const uint8_t *p, size_t len, size_t *off, const uint8_t **out, uint32_t *slen);
    proto_bool (*mpint_fixed)(const uint8_t *m, uint32_t mlen, uint8_t *out, size_t outlen);
} BytesNs;

// The verbs, in bytes.c. Named here because the table below has to name them, and prefixed because
// that puts them in the linker's namespace.
void protocore_bw_put(protocore_span *w, uint8_t b);
void protocore_bw_put_be(protocore_span *w, uint64_t val, int32_t nbytes);
void protocore_bw_bytes(protocore_span *w, const void *src, size_t n);
proto_bool protocore_br_take_be(protocore_cspan *r, size_t nbytes, uint64_t *out);
proto_bool protocore_rd_u32(const uint8_t *p, size_t len, size_t *off, uint32_t *out);
proto_bool protocore_rd_str(const uint8_t *p, size_t len, size_t *off, const uint8_t **out, uint32_t *slen);
proto_bool protocore_mpint_to_fixed(const uint8_t *m, uint32_t mlen, uint8_t *out, size_t outlen);

/**
 * @brief The names, aliased.
 *
 * `static const` and initialized here, not declared `extern` against a definition in the .c: a
 * translation unit that can see this initializer knows which function each member holds, so a member
 * read folds away and the call to the verb in bytes.c is direct, leaving the table referenced by
 * nothing for the linker to drop.
 *
 * `unused` because a header this wide is included by files that take none of it.
 */
static const BytesNs bytes
    __attribute__((unused)) = {protocore_bw_put, protocore_bw_put_be, protocore_bw_bytes,      protocore_br_take_be,
                               protocore_rd_u32, protocore_rd_str,    protocore_mpint_to_fixed};

PROTOCORE_END_DECLS

#endif // PROTOCORE_BYTES_H
