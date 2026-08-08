// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rng.c
 * @brief The seed each worker inherits, and the draw over it.
 *
 * The seed is key material a worker holds for the life of the program, so it is a persistent borrow
 * from that worker's secure pool rather than declared storage: the pool is what hands out aligned
 * bytes and wipes them on reclaim. The slot resolves itself out of the caller's worker the same way
 * every other pool borrow does, so nothing is passed in and no caller is told which seed it uses.
 *
 * A draw takes its keystream under the current seed and replaces the seed from that same keystream
 * before returning. Block 0 supplies the replacement and the caller's bytes start at block 1, so no
 * byte is both handed out and kept, and the state that produced a value is gone by the time the
 * value is. Every PC_RAND_RESEED_BYTES the seed is redrawn from the platform instead.
 */

#include "crypto/rng/rng.h"

#include "core_setup/board_profiles/pc_platform.h" // pc_platform_rand_fill: the entropy source
#include "crypto/cipher/chacha20.h"
#include "mmgr/arena.h" // pc_worker_self: the slot the pools index by
#include "mmgr/protomem.h"
#include "mmgr/secure.h"

// key || iv || next, one contiguous borrow per worker.
#define RNG_IV_LEN 8
#define RNG_SEEDED_LEN (PC_RAND_SEED_LEN + RNG_IV_LEN)
#define RNG_STATE_LEN (RNG_SEEDED_LEN + PC_RAND_SEED_LEN)

// The pool is sized from PC_WORK_RNG, so the borrow below cannot come up short: a generator that
// could would hand a caller an unwritten buffer, which is the one failure it must not have. This is
// the proof the sizing declaration matches what is actually taken.
static_assert(RNG_STATE_LEN <= PC_WORK_RNG, "PC_WORK_RNG must cover the generator's seed, nonce and scratch");

// The borrows and the budget each has spent. Only these live here; every byte of key material is in
// the pool.
typedef struct
{
    uint8_t *state[PC_SEC_POOL_SLOTS];
    size_t drawn[PC_SEC_POOL_SLOTS];
} RngCtx;

static RngCtx s_rng = {{NULL}, {0}};

// This worker's slot, clamped the way the pools clamp it: a caller that is not a server worker lands
// on the ghost rather than on worker 0.
static int rng_slot(void)
{
    int w = pc_worker_self();
    if (w < 0 || w >= PC_SEC_POOL_SLOTS)
    {
        w = PC_GHOST_WORKER_SLOT;
    }
    return w;
}

// Redraw the seed and its nonce from the platform, and start the budget over.
static void rng_reseed(uint8_t *st, int w)
{
    pc_platform_rand_fill(st, RNG_SEEDED_LEN);
    s_rng.drawn[w] = 0;
}

// This worker's seed, bound and drawn on first use. The static_assert above sizes the pool for it,
// so the borrow is there: no test, no branch, no failure path to write or to execute.
static uint8_t *rng_state(int w)
{
    if (s_rng.state[w] != NULL)
    {
        return s_rng.state[w];
    }
    pc_span s = secure.persist_span(RNG_STATE_LEN);
    s_rng.state[w] = s.buf;
    rng_reseed(s.buf, w);
    return s.buf;
}

void pc_rand_fill(uint8_t *out, size_t len)
{
    if (out == NULL || len == 0)
    {
        return;
    }
    int w = rng_slot();
    uint8_t *st = rng_state(w);
    if (s_rng.drawn[w] >= PC_RAND_RESEED_BYTES || len >= PC_RAND_RESEED_BYTES - s_rng.drawn[w])
    {
        rng_reseed(st, w);
    }
    uint8_t *iv = st + PC_RAND_SEED_LEN;
    uint8_t *next = st + RNG_SEEDED_LEN;
    pc_chacha20_xor(st, iv, 0, NULL, next, PC_RAND_SEED_LEN);
    pc_chacha20_xor(st, iv, 1, NULL, out, len);
    mem.cpy(st, next, PC_RAND_SEED_LEN);
    pc_secure_wipe(next, PC_RAND_SEED_LEN);
    s_rng.drawn[w] += len;
}
