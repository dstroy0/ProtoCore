// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file auth_lockout.c
 * @brief Per-peer brute-force lockout state machine (PROTOCORE_ENABLE_AUTH_LOCKOUT).
 *
 * A bounded BSS table of buckets keyed by the source address (a protocore_ip, so IPv4 and IPv6 peers are
 * each their own bucket - never a lossy hash that a colliding address could poison). Each bucket
 * holds the consecutive-failure count and, once the threshold is crossed, the start and duration
 * of the current lockout (exponential backoff, capped). Compiled only when PROTOCORE_ENABLE_AUTH_LOCKOUT
 * is set; the host unit tests enable it.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

#if PROTOCORE_ENABLE_AUTH_LOCKOUT

#include "mmgr/secure/secure.h" // the persistent end the table is taken from
#include "server/security/auth_lockout/auth_lockout.h"
#include "shared/ip/ip.h" // protocore_ip: the address a bucket is keyed on

PROTOCORE_BEGIN_DECLS

typedef struct
{
    protocore_ip addr;      ///< source address (family protocore_ip_family::PROTOCORE_IP_NONE marks an empty bucket).
    uint32_t lock_start_ms; ///< millis() when the current lockout began.
    uint32_t lock_ms;       ///< current lockout duration (0 = not locked).
    uint32_t last_ms;       ///< millis() of the last recorded failure (LRU eviction).
    uint16_t fails;         ///< consecutive failures from this address.
} LockoutBucket;

// The one definition, private to this TU. It sits at LOCKOUT_OFF_CTX in the caller's borrow, so its
// size never leaves this file and no consumer can name it.
typedef struct
{
    LockoutBucket buckets[PROTOCORE_AUTH_LOCKOUT_SLOTS];
} LockoutCtx;

// The caller's borrow, split: the whole table, at its offset.
#define LOCKOUT_OFF_CTX 0u
static_assert(LOCKOUT_OFF_CTX + sizeof(LockoutCtx) <= PROTOCORE_AUTH_LOCKOUT_BORROW,
              "PROTOCORE_AUTH_LOCKOUT_BORROW is short of the bucket table - raise it in protocore_config.h, "
              "which derives PROTOCORE_SECURE_ARENA_SIZE from it");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(LOCKOUT_OFF_CTX % _Alignof(LockoutCtx) == 0,
              "LOCKOUT_OFF_CTX is not a multiple of alignof(LockoutCtx) - LOCKOUT_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define LOCKOUT_CTX(w) ((LockoutCtx *)(void *)((w) + LOCKOUT_OFF_CTX))

// Whether @p a and @p b are the same family and address.
static proto_bool ip_same(const protocore_ip *a, const protocore_ip *b)
{
    Ip.args.ip = a;
    Ip.args.b = b;
    Ip.equal(ip_work);
    return Ip.ok;
}

// Whether @p ip names nothing: no family, or the all-zero address.
static proto_bool ip_none(const protocore_ip *ip)
{
    Ip.args.ip = ip;
    Ip.is_unspecified(ip_work);
    return Ip.ok;
}

// Returns a mutable bucket (callers mutate it), so it takes the owner by non-const reference.
LockoutBucket *find_bucket(LockoutCtx *c, const protocore_ip *ip)
{
    for (int i = 0; i < PROTOCORE_AUTH_LOCKOUT_SLOTS; i++)
    {
        if (c->buckets[i].addr.family != PROTOCORE_IP_NONE && ip_same(&c->buckets[i].addr, ip))
        {
            return &c->buckets[i];
        }
    }
    return NULL;
}

proto_bool bucket_locked(const LockoutBucket *b, uint32_t now_ms)
{
    // Unsigned subtraction wraps correctly across the millis() rollover.
    return b->lock_ms != 0 && (uint32_t)(now_ms - b->lock_start_ms) < b->lock_ms;
}

// --- the program's shared table, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for itself.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_AUTH_LOCKOUT_BORROW persistent bytes
} LockoutOwnCtx;
static LockoutOwnCtx s_own;

uint8_t *protocore_auth_lockout_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_AUTH_LOCKOUT_BORROW).buf;
    }
    return s_own.span;
}

// --- the entries -----------------------------------------------------------

static void lockout_remaining(uint8_t *restrict work)
{
    AuthLockout.ok = PROTO_FALSE;
    AuthLockout.ms = 0;
    const protocore_ip *ip = AuthLockout.args.ip;
    if (ip_none(ip))
    {
        return; // untrackable source -> never reported as locked
    }
    LockoutCtx *s_lock = LOCKOUT_CTX(work);
    AuthLockout.ok = PROTO_TRUE;
    LockoutBucket *b = find_bucket(s_lock, ip);
    if (!b || b->lock_ms == 0)
    {
        return;
    }
    uint32_t elapsed = AuthLockout.args.now_ms - b->lock_start_ms; // wraps correctly across rollover
    if (elapsed >= b->lock_ms)
    {
        return; // the lockout window has passed
    }
    AuthLockout.ms = b->lock_ms - elapsed;
}

static void lockout_fail(uint8_t *restrict work)
{
    AuthLockout.ok = PROTO_FALSE;
    const protocore_ip *ip = AuthLockout.args.ip;
    const uint32_t now_ms = AuthLockout.args.now_ms;
    if (ip_none(ip))
    {
        return; // untrackable source
    }
    LockoutCtx *s_lock = LOCKOUT_CTX(work);
    AuthLockout.ok = PROTO_TRUE;

    LockoutBucket *b = find_bucket(s_lock, ip);
    if (!b)
    {
        // Claim a bucket: an empty one first; else evict the least-recently-used
        // address that is NOT currently locked; only if every bucket is locked do
        // we evict the overall LRU (so an active attacker cannot evict their own
        // lockout by flooding from other addresses).
        int slot = -1;
        int lru = 0;
        for (int i = 0; i < PROTOCORE_AUTH_LOCKOUT_SLOTS; i++)
        {
            if (s_lock->buckets[i].addr.family == PROTOCORE_IP_NONE)
            {
                slot = i;
                break;
            }
            if ((uint32_t)(now_ms - s_lock->buckets[i].last_ms) > (uint32_t)(now_ms - s_lock->buckets[lru].last_ms))
            {
                lru = i;
            }
            if (!bucket_locked(&s_lock->buckets[i], now_ms) &&
                (slot < 0 ||
                 (uint32_t)(now_ms - s_lock->buckets[i].last_ms) > (uint32_t)(now_ms - s_lock->buckets[slot].last_ms)))
            {
                slot = i;
            }
        }
        if (slot < 0)
        {
            slot = lru; // table full of active lockouts
        }
        b = &s_lock->buckets[slot];
        b->addr = *ip;
        b->fails = 0;
        b->lock_ms = 0;
        b->lock_start_ms = now_ms;
    }

    b->last_ms = now_ms;
    if (b->fails < 0xFFFF)
    {
        b->fails++;
    }

    if (b->fails >= PROTOCORE_AUTH_LOCKOUT_THRESHOLD)
    {
        // Exponential backoff: base << (fails - threshold), capped at max. Step the
        // double so the shift can never overflow: the cap is hit (and the loop
        // breaks) before dur could exceed MAX_MS, and the config caps MAX_MS at
        // 0x80000000 so the surviving dur << 1 always fits in a uint32.
        uint32_t dur = PROTOCORE_AUTH_LOCKOUT_BASE_MS;
        for (uint16_t n = (uint16_t)(b->fails - PROTOCORE_AUTH_LOCKOUT_THRESHOLD); n > 0; n--)
        {
            if (dur >= PROTOCORE_AUTH_LOCKOUT_MAX_MS)
            {
                dur = PROTOCORE_AUTH_LOCKOUT_MAX_MS;
                break;
            }
            dur <<= 1;
        }
        if (dur > PROTOCORE_AUTH_LOCKOUT_MAX_MS)
        {
            dur = PROTOCORE_AUTH_LOCKOUT_MAX_MS;
        }
        b->lock_ms = dur;
        b->lock_start_ms = now_ms;
    }
}

static void lockout_succeed(uint8_t *restrict work)
{
    AuthLockout.ok = PROTO_FALSE;
    const protocore_ip *ip = AuthLockout.args.ip;
    if (ip_none(ip))
    {
        return;
    }
    LockoutCtx *s_lock = LOCKOUT_CTX(work);
    AuthLockout.ok = PROTO_TRUE;
    LockoutBucket *b = find_bucket(s_lock, ip);
    if (b)
    {
        b->addr.family = PROTOCORE_IP_NONE;
        b->fails = 0;
        b->lock_ms = 0;
        b->lock_start_ms = 0;
        b->last_ms = 0;
    }
}

static void lockout_reset(uint8_t *restrict work)
{
    AuthLockout.ok = PROTO_FALSE;
    LockoutCtx *s_lock = LOCKOUT_CTX(work);
    AuthLockout.ok = PROTO_TRUE;
    for (int i = 0; i < PROTOCORE_AUTH_LOCKOUT_SLOTS; i++)
    {
        s_lock->buckets[i].addr.family = PROTOCORE_IP_NONE;
        s_lock->buckets[i].lock_start_ms = 0;
        s_lock->buckets[i].lock_ms = 0;
        s_lock->buckets[i].last_ms = 0;
        s_lock->buckets[i].fails = 0;
    }
}

AuthLockoutNs AuthLockout = {
    .remaining = lockout_remaining, .fail = lockout_fail, .succeed = lockout_succeed, .reset = lockout_reset};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AUTH_LOCKOUT
