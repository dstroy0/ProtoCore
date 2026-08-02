// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file audit_log.cpp
 * @brief Hash-chained audit log - implementation.
 *
 * Fixed RAM ring of records. Each record's hash chains the previous record's
 * hash; the oldest retained record chains a moving "anchor" (the hash of the
 * last evicted record, genesis-zero before any eviction), so the retained window
 * verifies as a complete chain. SHA-256 comes from pc_sha256 (HW-accelerated on
 * ESP32); the timestamp comes from the pluggable pc_clock.
 */

#include "services/security/audit_log/audit_log.h"
#include "mmgr/membuild.h" // pc_sb frame builder
#include "shared_primitives/hex.h"

#if PC_ENABLE_AUDIT_LOG

#include "crypto/hash/sha256.h"
#include "services/system/clock.h"
#include <string.h>

namespace
{
// All audit-log state, owned by one instance (internal linkage): the record ring, its
// head/count/seq cursors, the moving chain anchor, and the sink, grouped so it is one
// named owner, unreachable from any other translation unit.
struct AuditCtx
{
    pc_audit_entry ring[PC_AUDIT_LOG_ENTRIES];
    uint16_t head = 0;                      // index of the oldest retained record
    uint16_t count = 0;                     // records currently retained
    uint32_t seq = 0;                       // last assigned sequence number (monotonic)
    uint8_t anchor[PC_AUDIT_HASH_LEN] = {}; // prev-hash for the oldest retained record
    pc_audit_sink_fn sink = nullptr;
};
AuditCtx s_audit;

inline uint16_t idx(const AuditCtx &c, uint16_t i)
{
    return (uint16_t)((c.head + i) % PC_AUDIT_LOG_ENTRIES);
}

void put_le32(uint8_t out[4], uint32_t v)
{
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
    out[2] = (uint8_t)((v >> 16) & 0xFF);
    out[3] = (uint8_t)((v >> 24) & 0xFF);
}

// hash = SHA-256(prev_hash || seq_le || ts_le || category || msg_len || msg).
// msg_len is length-prefixed so two records can never serialize ambiguously.
void chain_hash(const uint8_t prev[PC_AUDIT_HASH_LEN], const pc_audit_entry *e, uint8_t out[PC_AUDIT_HASH_LEN])
{
    pc_sha256_ctx c;
    pc_sha256_init(&c);
    pc_sha256_update(&c, prev, PC_AUDIT_HASH_LEN);
    uint8_t le[4];
    put_le32(le, e->seq);
    pc_sha256_update(&c, le, 4);
    put_le32(le, e->ts);
    pc_sha256_update(&c, le, 4);
    pc_sha256_update(&c, (const uint8_t *)&e->category, 1); // hash the raw category byte
    uint8_t mlen = (uint8_t)strnlen(e->msg, PC_AUDIT_MSG_LEN - 1);
    pc_sha256_update(&c, &mlen, 1);
    pc_sha256_update(&c, (const uint8_t *)e->msg, mlen);
    pc_sha256_final(&c, out);
}

// Append @p n JSON-escaped bytes of @p s into out[pos..cap); returns new pos, or
// cap+1 on overflow (caller checks pos <= cap).
size_t json_escape(char *out, size_t pos, size_t cap, const char *s)
{
    for (size_t i = 0; s[i]; i++)
    {
        unsigned char ch = (unsigned char)s[i];
        const char *esc = nullptr;
        char ub[7];
        size_t n;
        if (ch == '"')
        {
            esc = "\\\"", n = 2;
        }
        else if (ch == '\\')
        {
            esc = "\\\\", n = 2;
        }
        else if (ch == '\n')
        {
            esc = "\\n", n = 2;
        }
        else if (ch == '\r')
        {
            esc = "\\r", n = 2;
        }
        else if (ch == '\t')
        {
            esc = "\\t", n = 2;
        }
        else if (ch < 0x20)
        {
            ub[0] = '\\', ub[1] = 'u', ub[2] = '0', ub[3] = '0';
            ub[4] = pc_hex_digit((ch >> 4) & 0xF);
            ub[5] = pc_hex_digit(ch & 0xF);
            ub[6] = '\0';
            esc = ub, n = 6;
        }
        else
        {
            if (pos + 1 > cap)
            {
                return cap + 1;
            }
            out[pos++] = (char)ch;
            continue;
        }
        if (pos + n > cap)
        {
            return cap + 1;
        }
        memcpy(out + pos, esc, n);
        pos += n;
    }
    return pos;
}

size_t hex_hash(char *out, size_t pos, size_t cap, const uint8_t *h)
{
    if (pos + PC_AUDIT_HASH_LEN * 2 > cap)
    {
        return cap + 1;
    }
    for (size_t i = 0; i < PC_AUDIT_HASH_LEN; i++)
    {
        out[pos++] = pc_hex_digit((h[i] >> 4) & 0xF);
        out[pos++] = pc_hex_digit(h[i] & 0xF);
    }
    return pos;
}
} // namespace

void pc_audit_reset(void)
{
    s_audit.head = 0;
    s_audit.count = 0;
    s_audit.seq = 0;
    memset(s_audit.anchor, 0, sizeof(s_audit.anchor)); // genesis
}

void pc_audit_set_sink(pc_audit_sink_fn sink)
{
    s_audit.sink = sink;
}

uint32_t pc_audit_append(pc_audit_cat category, const char *msg)
{
    // prev = hash of the current newest record (anchor if the ring is empty).
    uint8_t prev[PC_AUDIT_HASH_LEN];
    if (s_audit.count == 0)
    {
        memcpy(prev, s_audit.anchor, PC_AUDIT_HASH_LEN);
    }
    else
    {
        memcpy(prev, s_audit.ring[idx(s_audit, (uint16_t)(s_audit.count - 1))].hash, PC_AUDIT_HASH_LEN);
    }

    // Full ring: evict the oldest; its hash advances the chain anchor so the
    // retained window still verifies. (Eviction touches only the oldest, never
    // the newest we just read as prev.)
    if (s_audit.count == PC_AUDIT_LOG_ENTRIES)
    {
        memcpy(s_audit.anchor, s_audit.ring[s_audit.head].hash, PC_AUDIT_HASH_LEN);
        s_audit.head = (uint16_t)((s_audit.head + 1) % PC_AUDIT_LOG_ENTRIES);
        s_audit.count--;
    }

    pc_audit_entry *e = &s_audit.ring[idx(s_audit, s_audit.count)];
    e->seq = ++s_audit.seq;
    e->ts = pc_millis();
    e->category = category;
    if (msg)
    {
        size_t n = strnlen(msg, PC_AUDIT_MSG_LEN - 1);
        memcpy(e->msg, msg, n);
        e->msg[n] = '\0';
    }
    else
    {
        e->msg[0] = '\0';
    }
    chain_hash(prev, e, e->hash);
    s_audit.count++;

    if (s_audit.sink)
    {
        s_audit.sink(e);
    }
    return e->seq;
}

uint16_t pc_audit_count(void)
{
    return s_audit.count;
}

const pc_audit_entry *pc_audit_at(uint16_t i)
{
    if (i >= s_audit.count)
    {
        return nullptr;
    }
    return &s_audit.ring[idx(s_audit, i)];
}

bool pc_audit_verify(uint32_t *first_broken_seq)
{
    uint8_t expected[PC_AUDIT_HASH_LEN];
    memcpy(expected, s_audit.anchor, PC_AUDIT_HASH_LEN);
    for (uint16_t i = 0; i < s_audit.count; i++)
    {
        const pc_audit_entry *e = &s_audit.ring[idx(s_audit, i)];
        uint8_t h[PC_AUDIT_HASH_LEN];
        chain_hash(expected, e, h);
        if (memcmp(h, e->hash, PC_AUDIT_HASH_LEN) != 0)
        {
            if (first_broken_seq)
            {
                *first_broken_seq = e->seq;
            }
            return false;
        }
        memcpy(expected, e->hash, PC_AUDIT_HASH_LEN);
    }
    return true;
}

const char *pc_audit_cat_name(pc_audit_cat category)
{
    switch (category)
    {
    case pc_audit_cat::PC_AUDIT_AUTH:
        return "auth";
    case pc_audit_cat::PC_AUDIT_AUTH_FAIL:
        return "auth_fail";
    case pc_audit_cat::PC_AUDIT_ACCESS:
        return "access";
    case pc_audit_cat::PC_AUDIT_CONFIG:
        return "config";
    case pc_audit_cat::PC_AUDIT_ADMIN:
        return "admin";
    default:
        return "system";
    }
}

int pc_audit_format(const pc_audit_entry *e, char *out, size_t cap)
{
    if (!e || !out || cap == 0)
    {
        return 0;
    }
    pc_sb sb_out = {out, cap, 0, true};
    pc_sb_put(&sb_out, "{\"seq\":");
    pc_sb_u32(&sb_out, (uint32_t)((unsigned long)e->seq));
    pc_sb_put(&sb_out, ",\"ts\":");
    pc_sb_u32(&sb_out, (uint32_t)((unsigned long)e->ts));
    pc_sb_put(&sb_out, ",\"cat\":\"");
    pc_sb_put(&sb_out, pc_audit_cat_name(e->category));
    pc_sb_put(&sb_out, "\",\"msg\":\"");
    int head = (int)pc_sb_finish(&sb_out);
    // Only head<0 is unreachable here: this format is fixed ("%lu%lu%s...", no floating point,
    // no wide chars) with always-valid args (pc_audit_cat_name never returns null; the produced
    // text is bounded by PC_AUDIT_MSG_LEN/PC_AUDIT_HASH_LEN, both small fixed constants, so the
    // result can never approach INT_MAX either) - snprintf cannot signal an encoding error for it.
    // The (size_t)head>=cap arm IS reachable and tested (test_format_fails_closed_all_stages's cap
    // sweep); gcovr has no sub-line exclusion granularity, so silencing the dead head<0 arm means
    // excluding the whole line's branch data.
    if (head < 0 || (size_t)head >= cap) // GCOVR_EXCL_BR_LINE
    {
        return 0;
    }
    size_t pos = (size_t)head;
    pos = json_escape(out, pos, cap, e->msg);
    if (pos > cap)
    {
        return 0;
    }
    static const char mid[] = "\",\"hash\":\"";
    size_t mid_len = sizeof(mid) - 1;
    if (pos + mid_len > cap)
    {
        return 0;
    }
    memcpy(out + pos, mid, mid_len);
    pos += mid_len;
    pos = hex_hash(out, pos, cap, e->hash);
    if (pos > cap)
    {
        return 0;
    }
    if (pos + 2 > cap) // closing "} plus NUL space
    {
        return 0;
    }
    out[pos++] = '"';
    out[pos++] = '}';
    if (pos >= cap)
    {
        return 0;
    }
    out[pos] = '\0';
    return (int)pos;
}

int pc_audit_dump_json(char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    uint32_t broken = 0;
    bool intact = pc_audit_verify(&broken);

    int head;
    if (intact)
    {
        pc_sb sb_out2 = {out, cap, 0, true};
        pc_sb_put(&sb_out2, "{\"intact\":true,\"count\":");
        pc_sb_u32(&sb_out2, (uint32_t)((unsigned)s_audit.count));
        pc_sb_put(&sb_out2, ",\"entries\":[");
        head = (int)pc_sb_finish(&sb_out2);
    }
    else
    {
        pc_sb sb_out3 = {out, cap, 0, true};
        pc_sb_put(&sb_out3, "{\"intact\":false,\"first_broken\":");
        pc_sb_u32(&sb_out3, (uint32_t)((unsigned long)broken));
        pc_sb_put(&sb_out3, ",\"count\":");
        pc_sb_u32(&sb_out3, (uint32_t)((unsigned)s_audit.count));
        pc_sb_put(&sb_out3, ",\"entries\":[");
        head = (int)pc_sb_finish(&sb_out3);
    }
    // Only head<0 is unreachable here, for the same reason as pc_audit_format's identical guard:
    // both snprintf formats above are fixed ("%s...:[" with %u/%lu, no floating point, no wide
    // chars), the args are always valid, and the produced text is bounded by s_audit.count's small
    // fixed max (PC_AUDIT_LOG_ENTRIES), so it can never approach INT_MAX. The (size_t)head>=cap
    // arm IS reachable and tested (test_dump_fails_closed_all_stages's cap sweep); gcovr has no
    // sub-line exclusion granularity, so silencing the dead head<0 arm means excluding the whole
    // line's branch data.
    if (head < 0 || (size_t)head >= cap) // GCOVR_EXCL_BR_LINE
    {
        return 0;
    }
    size_t pos = (size_t)head;

    for (uint16_t i = 0; i < s_audit.count; i++)
    {
        if (i > 0)
        {
            // GCOVR_EXCL_START  unreachable: reaching here means entry i-1 (or the header, for i==1)
            // rendered successfully, and its own overflow check (pc_audit_format's or snprintf's)
            // required strictly less than the cap it was given - so cap - pos is always >= 1 at this
            // point, exactly the one byte this comma needs. The guard can never fire.
            if (pos + 1 > cap)
            {
                return 0;
            }
            // GCOVR_EXCL_STOP
            out[pos++] = ',';
        }
        int n = pc_audit_format(&s_audit.ring[idx(s_audit, i)], out + pos, cap - pos);
        if (n <= 0)
        {
            return 0;
        }
        pos += (size_t)n;
    }
    if (pos + 2 > cap)
    {
        return 0;
    }
    out[pos++] = ']';
    out[pos++] = '}';
    if (pos >= cap)
    {
        return 0;
    }
    out[pos] = '\0';
    return (int)pos;
}

#endif // PC_ENABLE_AUDIT_LOG
