// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file auth_lockout.cpp
 * @brief Per-peer brute-force lockout state machine (PC_ENABLE_AUTH_LOCKOUT).
 *
 * A bounded BSS table of buckets keyed by the source address (a pc_ip, so IPv4 and IPv6 peers are
 * each their own bucket - never a lossy hash that a colliding address could poison). Each bucket
 * holds the consecutive-failure count and, once the threshold is crossed, the start and duration
 * of the current lockout (exponential backoff, capped). Compiled only when PC_ENABLE_AUTH_LOCKOUT
 * is set; the host unit tests enable it.
 */

#include "auth_lockout.h"

#if PC_ENABLE_AUTH_LOCKOUT

namespace
{

struct LockoutBucket
{
    pc_ip addr;             ///< source address (family pc_ip_family::PC_IP_NONE marks an empty bucket).
    uint32_t lock_start_ms; ///< millis() when the current lockout began.
    uint32_t lock_ms;       ///< current lockout duration (0 = not locked).
    uint32_t last_ms;       ///< millis() of the last recorded failure (LRU eviction).
    uint16_t fails;         ///< consecutive failures from this address.
};

// All lockout state, owned by one instance (internal linkage): the per-peer bucket table,
// so it is one named owner, unreachable from any other translation unit.
struct LockoutCtx
{
    LockoutBucket buckets[PC_AUTH_LOCKOUT_SLOTS];
};
LockoutCtx s_lock;

// Returns a mutable bucket (callers mutate it), so it takes the owner by non-const reference.
LockoutBucket *find_bucket(LockoutCtx &c, const pc_ip *ip)
{
    for (int i = 0; i < PC_AUTH_LOCKOUT_SLOTS; i++)
    {
        if (c.buckets[i].addr.family != pc_ip_family::PC_IP_NONE && pc_ip_equal(&c.buckets[i].addr, ip))
        {
            return &c.buckets[i];
        }
    }
    return nullptr;
}

bool bucket_locked(const LockoutBucket *b, uint32_t now_ms)
{
    // Unsigned subtraction wraps correctly across the millis() rollover.
    return b->lock_ms != 0 && (uint32_t)(now_ms - b->lock_start_ms) < b->lock_ms;
}

} // namespace

uint32_t auth_lockout_remaining_ms(const pc_ip *ip, uint32_t now_ms)
{
    if (pc_ip_is_unspecified(ip))
    {
        return 0; // untrackable source -> never reported as locked
    }
    LockoutBucket *b = find_bucket(s_lock, ip);
    if (!b || b->lock_ms == 0)
    {
        return 0;
    }
    uint32_t elapsed = now_ms - b->lock_start_ms; // wraps correctly across rollover
    if (elapsed >= b->lock_ms)
    {
        return 0; // the lockout window has passed
    }
    return b->lock_ms - elapsed;
}

void auth_lockout_fail(const pc_ip *ip, uint32_t now_ms)
{
    if (pc_ip_is_unspecified(ip))
    {
        return; // untrackable source
    }

    LockoutBucket *b = find_bucket(s_lock, ip);
    if (!b)
    {
        // Claim a bucket: an empty one first; else evict the least-recently-used
        // address that is NOT currently locked; only if every bucket is locked do
        // we evict the overall LRU (so an active attacker cannot evict their own
        // lockout by flooding from other addresses).
        int slot = -1;
        int lru = 0;
        for (int i = 0; i < PC_AUTH_LOCKOUT_SLOTS; i++)
        {
            if (s_lock.buckets[i].addr.family == pc_ip_family::PC_IP_NONE)
            {
                slot = i;
                break;
            }
            if ((uint32_t)(now_ms - s_lock.buckets[i].last_ms) > (uint32_t)(now_ms - s_lock.buckets[lru].last_ms))
            {
                lru = i;
            }
            if (!bucket_locked(&s_lock.buckets[i], now_ms) &&
                (slot < 0 ||
                 (uint32_t)(now_ms - s_lock.buckets[i].last_ms) > (uint32_t)(now_ms - s_lock.buckets[slot].last_ms)))
            {
                slot = i;
            }
        }
        if (slot < 0)
        {
            slot = lru; // table full of active lockouts
        }
        b = &s_lock.buckets[slot];
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

    if (b->fails >= PC_AUTH_LOCKOUT_THRESHOLD)
    {
        // Exponential backoff: base << (fails - threshold), capped at max. Step the
        // double so the shift can never overflow: the cap is hit (and the loop
        // breaks) before dur could exceed MAX_MS, and the config caps MAX_MS at
        // 0x80000000 so the surviving dur << 1 always fits in a uint32.
        uint32_t dur = PC_AUTH_LOCKOUT_BASE_MS;
        for (uint16_t n = (uint16_t)(b->fails - PC_AUTH_LOCKOUT_THRESHOLD); n > 0; n--)
        {
            if (dur >= PC_AUTH_LOCKOUT_MAX_MS)
            {
                dur = PC_AUTH_LOCKOUT_MAX_MS;
                break;
            }
            dur <<= 1;
        }
        if (dur > PC_AUTH_LOCKOUT_MAX_MS)
        {
            dur = PC_AUTH_LOCKOUT_MAX_MS;
        }
        b->lock_ms = dur;
        b->lock_start_ms = now_ms;
    }
}

void auth_lockout_succeed(const pc_ip *ip)
{
    if (pc_ip_is_unspecified(ip))
    {
        return;
    }
    LockoutBucket *b = find_bucket(s_lock, ip);
    if (b)
    {
        b->addr.family = pc_ip_family::PC_IP_NONE;
        b->fails = 0;
        b->lock_ms = 0;
        b->lock_start_ms = 0;
        b->last_ms = 0;
    }
}

void auth_lockout_reset(void)
{
    for (int i = 0; i < PC_AUTH_LOCKOUT_SLOTS; i++)
    {
        s_lock.buckets[i].addr.family = pc_ip_family::PC_IP_NONE;
        s_lock.buckets[i].lock_start_ms = 0;
        s_lock.buckets[i].lock_ms = 0;
        s_lock.buckets[i].last_ms = 0;
        s_lock.buckets[i].fails = 0;
    }
}

#endif // PC_ENABLE_AUTH_LOCKOUT
