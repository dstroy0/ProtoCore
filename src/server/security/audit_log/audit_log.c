// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "mmgr/membuild/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h" // str: the bounded-run walks
#include "shared/hex/hex.h"

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t hex_work[16]; // the borrow an entry takes; Hex never reads it

#if PROTOCORE_ENABLE_AUDIT_LOG

#include "crypto/hash/sha256/sha256.h"
#include "mmgr/secure/secure.h" // the chain hash's working set, wiped on release
#include "server/clock/clock.h"

PROTOCORE_BEGIN_DECLS

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
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define AUDIT_LOG_OFF_CTX 0u
static_assert(AUDIT_LOG_OFF_CTX + sizeof(AuditCtx) <= PROTOCORE_AUDIT_LOG_BORROW,
              "PROTOCORE_AUDIT_LOG_BORROW is short of the module context - raise it in protocore_config.h, which\n"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(AUDIT_LOG_OFF_CTX % _Alignof(AuditCtx) == 0,
              "AUDIT_LOG_OFF_CTX is not a multiple of alignof(AuditCtx) - AUDIT_LOG_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define AUDIT_LOG_CTX(w) ((AuditCtx *)(void *)((w) + AUDIT_LOG_OFF_CTX))

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
    uint8_t *c;
    c = work;
    Sha256.init(c);
    Sha256V.update_args.data = prev;
    Sha256V.update_args.len = PROTOCORE_AUDIT_HASH_LEN;
    Sha256.update(c);
    uint8_t le[4];
    put_le32(le, e->seq);
    Sha256V.update_args.data = le;
    Sha256V.update_args.len = 4;
    Sha256.update(c);
    put_le32(le, e->ts);
    Sha256V.update_args.data = le;
    Sha256V.update_args.len = 4;
    Sha256.update(c);
    Sha256V.update_args.data = (const uint8_t *)&e->category;
    Sha256V.update_args.len = 1;
    Sha256.update(c); // hash the raw category byte
    uint8_t mlen = (uint8_t)str.len(e->msg, PROTOCORE_AUDIT_MSG_LEN - 1);
    Sha256V.update_args.data = &mlen;
    Sha256V.update_args.len = 1;
    Sha256.update(c);
    Sha256V.update_args.data = (const uint8_t *)e->msg;
    Sha256V.update_args.len = mlen;
    Sha256.update(c);
    Sha256V.final_args.out = out;
    Sha256.final(c);
}

// The lowercase hex character for one nibble.
static char hex_digit(uint8_t nibble)
{
    Hex.args.nibble = nibble;
    Hex.args.upper = PROTO_FALSE;
    Hex.digit(hex_work);
    return Hex.ch;
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
            ub[4] = hex_digit((uint8_t)((ch >> 4) & 0xF));
            ub[5] = hex_digit((uint8_t)(ch & 0xF));
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
        out[pos++] = hex_digit((uint8_t)((h[i] >> 4) & 0xF));
        out[pos++] = hex_digit((uint8_t)(h[i] & 0xF));
    }
    return pos;
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_AUDIT_LOG_BORROW persistent bytes
} AuditLogOwnCtx;
static AuditLogOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_audit_log_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_AUDIT_LOG_BORROW).buf;
    }
    return s_own.span;
}

void protocore_audit_log_reset(uint8_t *restrict work)
{
    (void)work;

    AUDIT_LOG_CTX(work)->head = 0;
    AUDIT_LOG_CTX(work)->count = 0;
    AUDIT_LOG_CTX(work)->seq = 0;
    mem.set(AUDIT_LOG_CTX(work)->anchor, 0, sizeof(AUDIT_LOG_CTX(work)->anchor)); // genesis
}

void protocore_audit_log_set_sink(uint8_t *restrict work)
{
    (void)work;
    protocore_audit_sink_fn sink = AuditLogV.set_sink_args.sink;

    AUDIT_LOG_CTX(work)->sink = sink;
}

void protocore_audit_log_append(uint8_t *restrict work)
{
    (void)work;
    AuditLogV.ms = 0;
    protocore_audit_cat category = AuditLogV.append_args.category;
    const char *msg = AuditLogV.append_args.msg;

    // prev = hash of the current newest record (anchor if the ring is empty).
    uint8_t prev[PROTOCORE_AUDIT_HASH_LEN];
    if (AUDIT_LOG_CTX(work)->count == 0)
    {
        mem.cpy(prev, AUDIT_LOG_CTX(work)->anchor, PROTOCORE_AUDIT_HASH_LEN);
    }
    else
    {
        mem.cpy(prev,
                AUDIT_LOG_CTX(work)->ring[idx(AUDIT_LOG_CTX(work), (uint16_t)(AUDIT_LOG_CTX(work)->count - 1))].hash,
                PROTOCORE_AUDIT_HASH_LEN);
    }

    // Full ring: evict the oldest; its hash advances the chain anchor so the
    // retained window still verifies. (Eviction touches only the oldest, never
    // the newest we just read as prev.)
    if (AUDIT_LOG_CTX(work)->count == PROTOCORE_AUDIT_LOG_ENTRIES)
    {
        mem.cpy(AUDIT_LOG_CTX(work)->anchor, AUDIT_LOG_CTX(work)->ring[AUDIT_LOG_CTX(work)->head].hash,
                PROTOCORE_AUDIT_HASH_LEN);
        AUDIT_LOG_CTX(work)->head = (uint16_t)((AUDIT_LOG_CTX(work)->head + 1) % PROTOCORE_AUDIT_LOG_ENTRIES);
        AUDIT_LOG_CTX(work)->count--;
    }

    protocore_audit_entry *e = &AUDIT_LOG_CTX(work)->ring[idx(AUDIT_LOG_CTX(work), AUDIT_LOG_CTX(work)->count)];
    e->seq = ++AUDIT_LOG_CTX(work)->seq;
    e->ts = Clock.ms;
    e->category = category;
    if (msg)
    {
        size_t n = str.len(msg, PROTOCORE_AUDIT_MSG_LEN - 1);
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
    AUDIT_LOG_CTX(work)->count++;

    if (AUDIT_LOG_CTX(work)->sink)
    {
        AUDIT_LOG_CTX(work)->sink(e);
    }
    AuditLogV.ms = e->seq;
    return;
}

void protocore_audit_log_count(uint8_t *restrict work)
{
    (void)work;
    AuditLogV.value = 0;

    AuditLogV.value = AUDIT_LOG_CTX(work)->count;
    return;
}

void protocore_audit_log_at(uint8_t *restrict work)
{
    (void)work;
    AuditLogV.ptr = 0;
    uint16_t i = AuditLogV.at_args.i;

    if (i >= AUDIT_LOG_CTX(work)->count)
    {
        AuditLogV.ptr = NULL;
        return;
    }
    AuditLogV.ptr = &AUDIT_LOG_CTX(work)->ring[idx(AUDIT_LOG_CTX(work), i)];
    return;
}

void protocore_audit_log_verify(uint8_t *restrict work)
{
    (void)work;
    uint32_t *first_broken_seq = AuditLogV.verify_args.first_broken_seq;

    // One borrow for the whole walk: the chain hash re-runs per entry out of the same bytes.
    size_t mark = protocore_secure_mark();
    protocore_span ws = protocore_secure_span(PROTOCORE_SHA256_BORROW, _Alignof(uint32_t));
    if (!span.ok(ws))
    {
        protocore_secure_release(mark);
        AuditLogV.ok = PROTO_FALSE;
        return;
    }
    uint8_t expected[PROTOCORE_AUDIT_HASH_LEN];
    mem.cpy(expected, AUDIT_LOG_CTX(work)->anchor, PROTOCORE_AUDIT_HASH_LEN);
    for (uint16_t i = 0; i < AUDIT_LOG_CTX(work)->count; i++)
    {
        const protocore_audit_entry *e = &AUDIT_LOG_CTX(work)->ring[idx(AUDIT_LOG_CTX(work), i)];
        uint8_t h[PROTOCORE_AUDIT_HASH_LEN];
        chain_hash(ws.buf, expected, e, h);
        if (mem.cmp(h, e->hash, PROTOCORE_AUDIT_HASH_LEN) != 0)
        {
            if (first_broken_seq)
            {
                *first_broken_seq = e->seq;
            }
            protocore_secure_release(mark);
            AuditLogV.ok = PROTO_FALSE;
            return;
        }
        mem.cpy(expected, e->hash, PROTOCORE_AUDIT_HASH_LEN);
    }
    protocore_secure_release(mark);
    AuditLogV.ok = PROTO_TRUE;
    return;
}

void protocore_audit_log_cat_name(uint8_t *restrict work)
{
    (void)work;
    AuditLogV.text = 0;
    protocore_audit_cat category = AuditLogV.cat_name_args.category;

    switch (category)
    {
    case PROTOCORE_AUDIT_AUTH:
        AuditLogV.text = "auth";
        return;
    case PROTOCORE_AUDIT_AUTH_FAIL:
        AuditLogV.text = "auth_fail";
        return;
    case PROTOCORE_AUDIT_ACCESS:
        AuditLogV.text = "access";
        return;
    case PROTOCORE_AUDIT_CONFIG:
        AuditLogV.text = "config";
        return;
    case PROTOCORE_AUDIT_ADMIN:
        AuditLogV.text = "admin";
        return;
    default:
        AuditLogV.text = "system";
        return;
    }
}

void protocore_audit_log_format(uint8_t *restrict work)
{
    (void)work;
    AuditLogV.n = 0;
    const protocore_audit_entry *e = AuditLogV.format_args.entry;
    char *out = AuditLogV.format_args.out;
    size_t cap = AuditLogV.format_args.cap;

    if (!e || !out || cap == 0)
    {
        AuditLogV.n = 0;
        return;
    }
    protocore_sb sb_out = {out, cap, 0, PROTO_TRUE};
    Sb.put(&sb_out, "{\"seq\":");
    Sb.u32(&sb_out, (uint32_t)((unsigned long)e->seq));
    Sb.put(&sb_out, ",\"ts\":");
    Sb.u32(&sb_out, (uint32_t)((unsigned long)e->ts));
    Sb.put(&sb_out, ",\"cat\":\"");
    AuditLogV.cat_name_args.category = e->category;
    protocore_audit_log_cat_name(work);
    Sb.put(&sb_out, AuditLogV.text);
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
        AuditLogV.n = 0;
        return;
    }
    size_t pos = (size_t)head;
    pos = json_escape(out, pos, cap, e->msg);
    if (pos > cap)
    {
        AuditLogV.n = 0;
        return;
    }
    static const char mid[] = "\",\"hash\":\"";
    size_t mid_len = sizeof(mid) - 1;
    if (pos + mid_len > cap)
    {
        AuditLogV.n = 0;
        return;
    }
    mem.cpy(out + pos, mid, mid_len);
    pos += mid_len;
    pos = hex_hash(out, pos, cap, e->hash);
    if (pos > cap)
    {
        AuditLogV.n = 0;
        return;
    }
    if (pos + 2 > cap) // closing "} plus NUL space
    {
        AuditLogV.n = 0;
        return;
    }
    out[pos++] = '"';
    out[pos++] = '}';
    if (pos >= cap)
    {
        AuditLogV.n = 0;
        return;
    }
    out[pos] = '\0';
    AuditLogV.n = (int)pos;
    return;
}

void protocore_audit_log_dump_json(uint8_t *restrict work)
{
    (void)work;
    AuditLogV.n = 0;
    char *out = AuditLogV.dump_json_args.out;
    size_t cap = AuditLogV.dump_json_args.cap;

    if (!out || cap == 0)
    {
        AuditLogV.n = 0;
        return;
    }
    uint32_t broken = 0;
    AuditLogV.verify_args.first_broken_seq = &broken;
    protocore_audit_log_verify(work);
    proto_bool intact = AuditLogV.ok;

    int head;
    if (intact)
    {
        protocore_sb sb_out2 = {out, cap, 0, PROTO_TRUE};
        Sb.put(&sb_out2, "{\"intact\":true,\"count\":");
        Sb.u32(&sb_out2, (uint32_t)((unsigned)AUDIT_LOG_CTX(work)->count));
        Sb.put(&sb_out2, ",\"entries\":[");
        head = (int)Sb.finish(&sb_out2);
    }
    else
    {
        protocore_sb sb_out3 = {out, cap, 0, PROTO_TRUE};
        Sb.put(&sb_out3, "{\"intact\":false,\"first_broken\":");
        Sb.u32(&sb_out3, (uint32_t)((unsigned long)broken));
        Sb.put(&sb_out3, ",\"count\":");
        Sb.u32(&sb_out3, (uint32_t)((unsigned)AUDIT_LOG_CTX(work)->count));
        Sb.put(&sb_out3, ",\"entries\":[");
        head = (int)Sb.finish(&sb_out3);
    }
    // Only head<0 is unreachable here, for the same reason as protocore_audit_format's identical guard:
    // both snprintf formats above are fixed ("%s...:[" with %u/%lu, no floating point, no wide
    // chars), the args are always valid, and the produced text is bounded by AUDIT_LOG_CTX(work)->count's small
    // fixed max (PROTOCORE_AUDIT_LOG_ENTRIES), so it can never approach INT_MAX. The (size_t)head>=cap
    // arm IS reachable and tested (test_dump_fails_closed_all_stages's cap sweep); gcovr has no
    // sub-line exclusion granularity, so silencing the dead head<0 arm means excluding the whole
    // line's branch data.
    if (head < 0 || (size_t)head >= cap)
    {
        AuditLogV.n = 0;
        return;
    }
    size_t pos = (size_t)head;

    for (uint16_t i = 0; i < AUDIT_LOG_CTX(work)->count; i++)
    {
        if (i > 0)
        {
            if (pos + 1 > cap)
            {
                AuditLogV.n = 0;
                return;
            }
            out[pos++] = ',';
        }
        AuditLogV.format_args.entry = &AUDIT_LOG_CTX(work)->ring[idx(AUDIT_LOG_CTX(work), i)];
        AuditLogV.format_args.out = out + pos;
        AuditLogV.format_args.cap = cap - pos;
        protocore_audit_log_format(work);
        int n = AuditLogV.n;
        if (n <= 0)
        {
            AuditLogV.n = 0;
            return;
        }
        pos += (size_t)n;
    }
    if (pos + 2 > cap)
    {
        AuditLogV.n = 0;
        return;
    }
    out[pos++] = ']';
    out[pos++] = '}';
    if (pos >= cap)
    {
        AuditLogV.n = 0;
        return;
    }
    out[pos] = '\0';
    AuditLogV.n = (int)pos;
    return;
}

/** @brief The operands and the outcome. */
AuditLogVars AuditLogV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AUDIT_LOG
