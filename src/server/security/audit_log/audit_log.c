// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file audit_log.c
 * @brief Hash-chained audit log - implementation.
 *
 * Fixed RAM ring of records. Each record's hash chains the previous record's
 * hash; the oldest retained record chains a moving "anchor" (the hash of the
 * last evicted record, genesis-zero before any eviction), so the retained window
 * verifies as a complete chain. SHA-256 comes from protocore_sha256 (HW-accelerated on
 * ESP32); the timestamp comes from the pluggable protocore_clock.
 */

#include "server/security/audit_log/audit_log.h"
#include "mmgr/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem.h"
#include "shared/hex/hex.h"

#if PROTOCORE_ENABLE_AUDIT_LOG

#include "crypto/hash/sha256.h"
#include "mmgr/secure.h" // the chain hash's working set, wiped on release
#include "server/clock/clock.h"

// All audit-log state, owned by one instance (internal linkage): the record ring, its
// head/count/seq cursors, the moving chain anchor, and the sink, grouped so it is one
// named owner, unreachable from any other translation unit.
typedef struct
{
    protocore_audit_entry ring[PROTOCORE_AUDIT_LOG_ENTRIES];
    uint16_t head;                            // index of the oldest retained record
    uint16_t count;                           // records currently retained
    uint32_t seq;                             // last assigned sequence number (monotonic)
    uint8_t anchor[PROTOCORE_AUDIT_HASH_LEN]; // prev-hash for the oldest retained record
    protocore_audit_sink_fn sink;
} AuditCtx;
static AuditCtx s_audit;

static inline uint16_t idx(const AuditCtx *c, uint16_t i)
{
    return (uint16_t)((c->head + i) % PROTOCORE_AUDIT_LOG_ENTRIES);
}

static void put_le32(uint8_t out[4], uint32_t v)
{
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
    out[2] = (uint8_t)((v >> 16) & 0xFF);
    out[3] = (uint8_t)((v >> 24) & 0xFF);
}

// hash = SHA-256(prev_hash || seq_le || ts_le || category || msg_len || msg).
// msg_len is length-prefixed so two records can never serialize ambiguously.
static void chain_hash(uint8_t *work, const uint8_t prev[PROTOCORE_AUDIT_HASH_LEN], const protocore_audit_entry *e,
                       uint8_t out[PROTOCORE_AUDIT_HASH_LEN])
{
    protocore_sha256_ctx c;
    protocore_sha256_init(&c, work);
    protocore_sha256_update(&c, prev, PROTOCORE_AUDIT_HASH_LEN);
    uint8_t le[4];
    put_le32(le, e->seq);
    protocore_sha256_update(&c, le, 4);
    put_le32(le, e->ts);
    protocore_sha256_update(&c, le, 4);
    protocore_sha256_update(&c, (const uint8_t *)&e->category, 1); // hash the raw category byte
    uint8_t mlen = (uint8_t)strnlen(e->msg, PROTOCORE_AUDIT_MSG_LEN - 1);
    protocore_sha256_update(&c, &mlen, 1);
    protocore_sha256_update(&c, (const uint8_t *)e->msg, mlen);
    protocore_sha256_final(&c, out);
}

// Append @p n JSON-escaped bytes of @p s into out[pos..cap); returns new pos, or
// cap+1 on overflow (caller checks pos <= cap).
static size_t json_escape(char *out, size_t pos, size_t cap, const char *s)
{
    for (size_t i = 0; s[i]; i++)
    {
        unsigned char ch = (unsigned char)s[i];
        const char *esc = NULL;
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
            ub[4] = protocore_hex_digit((ch >> 4) & 0xF, PROTO_FALSE);
            ub[5] = protocore_hex_digit(ch & 0xF, PROTO_FALSE);
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
        mem.cpy(out + pos, esc, n);
        pos += n;
    }
    return pos;
}

static size_t hex_hash(char *out, size_t pos, size_t cap, const uint8_t *h)
{
    if (pos + PROTOCORE_AUDIT_HASH_LEN * 2 > cap)
    {
        return cap + 1;
    }
    for (size_t i = 0; i < PROTOCORE_AUDIT_HASH_LEN; i++)
    {
        out[pos++] = protocore_hex_digit((h[i] >> 4) & 0xF, PROTO_FALSE);
        out[pos++] = protocore_hex_digit(h[i] & 0xF, PROTO_FALSE);
    }
    return pos;
}

void protocore_audit_reset(void)
{
    s_audit.head = 0;
    s_audit.count = 0;
    s_audit.seq = 0;
    mem.set(s_audit.anchor, 0, sizeof(s_audit.anchor)); // genesis
}

void protocore_audit_set_sink(protocore_audit_sink_fn sink)
{
    s_audit.sink = sink;
}

uint32_t protocore_audit_append(protocore_audit_cat category, const char *msg)
{
    // prev = hash of the current newest record (anchor if the ring is empty).
    uint8_t prev[PROTOCORE_AUDIT_HASH_LEN];
    if (s_audit.count == 0)
    {
        mem.cpy(prev, s_audit.anchor, PROTOCORE_AUDIT_HASH_LEN);
    }
    else
    {
        mem.cpy(prev, s_audit.ring[idx(&s_audit, (uint16_t)(s_audit.count - 1))].hash, PROTOCORE_AUDIT_HASH_LEN);
    }

    // Full ring: evict the oldest; its hash advances the chain anchor so the
    // retained window still verifies. (Eviction touches only the oldest, never
    // the newest we just read as prev.)
    if (s_audit.count == PROTOCORE_AUDIT_LOG_ENTRIES)
    {
        mem.cpy(s_audit.anchor, s_audit.ring[s_audit.head].hash, PROTOCORE_AUDIT_HASH_LEN);
        s_audit.head = (uint16_t)((s_audit.head + 1) % PROTOCORE_AUDIT_LOG_ENTRIES);
        s_audit.count--;
    }

    protocore_audit_entry *e = &s_audit.ring[idx(&s_audit, s_audit.count)];
    e->seq = ++s_audit.seq;
    e->ts = protocore_millis();
    e->category = category;
    if (msg)
    {
        size_t n = strnlen(msg, PROTOCORE_AUDIT_MSG_LEN - 1);
        mem.cpy(e->msg, msg, n);
        e->msg[n] = '\0';
    }
    else
    {
        e->msg[0] = '\0';
    }
    {
        // One borrow for this entry's chain hash, returned before the sink runs.
        size_t mark = protocore_secure_mark();
        protocore_span ws = protocore_secure_span(PROTOCORE_SHA256_BORROW, _Alignof(uint32_t));
        if (span.ok(ws))
        {
            chain_hash(ws.buf, prev, e, e->hash);
        }
        protocore_secure_release(mark);
    }
    s_audit.count++;

    if (s_audit.sink)
    {
        s_audit.sink(e);
    }
    return e->seq;
}

uint16_t protocore_audit_count(void)
{
    return s_audit.count;
}

const protocore_audit_entry *protocore_audit_at(uint16_t i)
{
    if (i >= s_audit.count)
    {
        return NULL;
    }
    return &s_audit.ring[idx(&s_audit, i)];
}

proto_bool protocore_audit_verify(uint32_t *first_broken_seq)
{
    // One borrow for the whole walk: the chain hash re-runs per entry out of the same bytes.
    size_t mark = protocore_secure_mark();
    protocore_span ws = protocore_secure_span(PROTOCORE_SHA256_BORROW, _Alignof(uint32_t));
    if (!span.ok(ws))
    {
        protocore_secure_release(mark);
        return PROTO_FALSE;
    }
    uint8_t expected[PROTOCORE_AUDIT_HASH_LEN];
    mem.cpy(expected, s_audit.anchor, PROTOCORE_AUDIT_HASH_LEN);
    for (uint16_t i = 0; i < s_audit.count; i++)
    {
        const protocore_audit_entry *e = &s_audit.ring[idx(&s_audit, i)];
        uint8_t h[PROTOCORE_AUDIT_HASH_LEN];
        chain_hash(ws.buf, expected, e, h);
        if (mem.cmp(h, e->hash, PROTOCORE_AUDIT_HASH_LEN) != 0)
        {
            if (first_broken_seq)
            {
                *first_broken_seq = e->seq;
            }
            protocore_secure_release(mark);
            return PROTO_FALSE;
        }
        mem.cpy(expected, e->hash, PROTOCORE_AUDIT_HASH_LEN);
    }
    protocore_secure_release(mark);
    return PROTO_TRUE;
}

const char *protocore_audit_cat_name(protocore_audit_cat category)
{
    switch (category)
    {
    case PROTOCORE_AUDIT_AUTH:
        return "auth";
    case PROTOCORE_AUDIT_AUTH_FAIL:
        return "auth_fail";
    case PROTOCORE_AUDIT_ACCESS:
        return "access";
    case PROTOCORE_AUDIT_CONFIG:
        return "config";
    case PROTOCORE_AUDIT_ADMIN:
        return "admin";
    default:
        return "system";
    }
}

int protocore_audit_format(const protocore_audit_entry *e, char *out, size_t cap)
{
    if (!e || !out || cap == 0)
    {
        return 0;
    }
    protocore_sb sb_out = {out, cap, 0, PROTO_TRUE};
    Sb.put(&sb_out, "{\"seq\":");
    Sb.u32(&sb_out, (uint32_t)((unsigned long)e->seq));
    Sb.put(&sb_out, ",\"ts\":");
    Sb.u32(&sb_out, (uint32_t)((unsigned long)e->ts));
    Sb.put(&sb_out, ",\"cat\":\"");
    Sb.put(&sb_out, protocore_audit_cat_name(e->category));
    Sb.put(&sb_out, "\",\"msg\":\"");
    int head = (int)Sb.finish(&sb_out);
    // Only head<0 is unreachable here: this format is fixed ("%lu%lu%s...", no floating point,
    // no wide chars) with always-valid args (protocore_audit_cat_name never returns null; the produced
    // text is bounded by PROTOCORE_AUDIT_MSG_LEN/PROTOCORE_AUDIT_HASH_LEN, both small fixed constants, so the
    // result can never approach INT_MAX either) - snprintf cannot signal an encoding error for it.
    // The (size_t)head>=cap arm IS reachable and tested (test_format_fails_closed_all_stages's cap
    // sweep); gcovr has no sub-line exclusion granularity, so silencing the dead head<0 arm means
    // excluding the whole line's branch data.
    if (head < 0 || (size_t)head >= cap)
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
    mem.cpy(out + pos, mid, mid_len);
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

int protocore_audit_dump_json(char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    uint32_t broken = 0;
    proto_bool intact = protocore_audit_verify(&broken);

    int head;
    if (intact)
    {
        protocore_sb sb_out2 = {out, cap, 0, PROTO_TRUE};
        Sb.put(&sb_out2, "{\"intact\":true,\"count\":");
        Sb.u32(&sb_out2, (uint32_t)((unsigned)s_audit.count));
        Sb.put(&sb_out2, ",\"entries\":[");
        head = (int)Sb.finish(&sb_out2);
    }
    else
    {
        protocore_sb sb_out3 = {out, cap, 0, PROTO_TRUE};
        Sb.put(&sb_out3, "{\"intact\":false,\"first_broken\":");
        Sb.u32(&sb_out3, (uint32_t)((unsigned long)broken));
        Sb.put(&sb_out3, ",\"count\":");
        Sb.u32(&sb_out3, (uint32_t)((unsigned)s_audit.count));
        Sb.put(&sb_out3, ",\"entries\":[");
        head = (int)Sb.finish(&sb_out3);
    }
    // Only head<0 is unreachable here, for the same reason as protocore_audit_format's identical guard:
    // both snprintf formats above are fixed ("%s...:[" with %u/%lu, no floating point, no wide
    // chars), the args are always valid, and the produced text is bounded by s_audit.count's small
    // fixed max (PROTOCORE_AUDIT_LOG_ENTRIES), so it can never approach INT_MAX. The (size_t)head>=cap
    // arm IS reachable and tested (test_dump_fails_closed_all_stages's cap sweep); gcovr has no
    // sub-line exclusion granularity, so silencing the dead head<0 arm means excluding the whole
    // line's branch data.
    if (head < 0 || (size_t)head >= cap)
    {
        return 0;
    }
    size_t pos = (size_t)head;

    for (uint16_t i = 0; i < s_audit.count; i++)
    {
        if (i > 0)
        {
            if (pos + 1 > cap)
            {
                return 0;
            }
            out[pos++] = ',';
        }
        int n = protocore_audit_format(&s_audit.ring[idx(&s_audit, i)], out + pos, cap - pos);
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

#endif // PROTOCORE_ENABLE_AUDIT_LOG
