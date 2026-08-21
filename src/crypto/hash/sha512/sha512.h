// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_SHA512_H
#define PROTOCORE_SHA512_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file sha512.h
 * @brief SHA-512 (FIPS 180-4) - streaming and one-shot digest.
 *
 * The shared SHA-512 primitive for the whole library (SSH Ed25519 / kex hashing, PQC, SMB 3.1.1
 * preauth integrity). The entries below are one surface over both arms: a part with a hashing
 * peripheral compresses on it, a part without runs the FIPS 180-4 rounds. Mirrors the sha256
 * structure.
 *
 * @ref Sha512Ns::final leaves the running digest where it was: the padded blocks compress into a copy
 * of the state, so the hash keeps taking data afterwards. That is what lets SSH read the exchange
 * hash at every stage the key exchange asks for without snapshotting anything.
 *
 * @c work is PROTOCORE_SHA512_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. The
 * borrow IS the digest, so two running hashes are two borrows and never collide.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief SHA-512 digest length in bytes. */
#define PROTOCORE_SHA512_DIGEST_LEN 64

/** @brief SHA-512 block size in bytes. */
#define PROTOCORE_SHA512_BLOCK_LEN 128

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*init)(uint8_t *restrict);
    proto_bool (*update)(uint8_t *restrict, const uint8_t *, size_t);
    proto_bool (*final)(uint8_t *restrict, uint8_t *);
    proto_bool (*hash)(uint8_t *restrict, const uint8_t *, size_t, uint8_t *);
} Sha512Ns;
PROTOCORE_NS_LAYOUT(Sha512Ns, init, update, final, hash);

/**
 * @brief Start a digest.
 * @param work PROTOCORE_SHA512_BORROW bytes the caller took. Not held past the call.
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sha512_init(uint8_t *restrict work);
/**
 * @brief Feed the running digest a chunk.
 * @param work PROTOCORE_SHA512_BORROW bytes the caller took. Not held past the call.
 * @param data the bytes
 * @param len how many
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sha512_update(uint8_t *restrict work, const uint8_t *data, size_t len);
/**
 * @brief Pad, compress the last block, write the 64 bytes out.
 * @param work PROTOCORE_SHA512_BORROW bytes the caller took. Not held past the call.
 * @param out PROTOCORE_SHA512_DIGEST_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sha512_final(uint8_t *restrict work, uint8_t *out);
/**
 * @brief Init, update and final in one call, for a message already whole.
 * @param work PROTOCORE_SHA512_BORROW bytes the caller took. Not held past the call.
 * @param data the message
 * @param len its length
 * @param out PROTOCORE_SHA512_DIGEST_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sha512_hash(uint8_t *restrict work, const uint8_t *data, size_t len, uint8_t *out);

/** @brief Module namespace. */
PROTOCORE_NS Sha512Ns Sha512 PROTOCORE_UNUSED = {.init = protocore_sha512_init,
                                                 .update = protocore_sha512_update,
                                                 .final = protocore_sha512_final,
                                                 .hash = protocore_sha512_hash};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SHA512_H
