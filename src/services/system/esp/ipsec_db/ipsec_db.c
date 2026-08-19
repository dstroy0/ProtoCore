// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ipsec_db.c
 * @brief IPsec SPD + SAD (RFC 4301) - see ipsec_db.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_IKEV2

#include "mmgr/protomem/protomem.h"
#include "services/system/esp/ipsec_db/ipsec_db.h"

static uint8_t esp_work[16]; // the borrow an entry takes; Esp never reads it

PROTOCORE_BEGIN_DECLS

// addr is within the inclusive [lo, hi] range. Addresses are big-endian, so a byte-wise unsigned compare
// (memcmp) is the numeric compare.
static proto_bool in_range(const uint8_t *addr, const uint8_t *lo, const uint8_t *hi, uint8_t len)
{
    return mem.cmp(addr, lo, len) >= 0 && mem.cmp(addr, hi, len) <= 0;
}
static proto_bool port_in(uint16_t p, uint16_t lo, uint16_t hi)
{
    return p >= lo && p <= hi;
}

// ── SPD ─────────────────────────────────────────────────────────────────────────────────────────

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void ipsec_db_protocore_ipsec_sad_find(uint8_t *restrict work);
static void ipsec_db_protocore_ipsec_selector_match(uint8_t *restrict work);

static void ipsec_db_protocore_ipsec_spd_init(uint8_t *restrict work)
{
    (void)work;
    IpsecSpd *spd = IpsecDb.protocore_ipsec_spd_init_args.spd;

    if (!spd)
    {
        return;
    }
    spd->count = 0;
}

static void ipsec_db_protocore_ipsec_spd_add(uint8_t *restrict work)
{
    (void)work;
    IpsecSpd *spd = IpsecDb.protocore_ipsec_spd_add_args.spd;
    const IpsecSelector *sel = IpsecDb.protocore_ipsec_spd_add_args.sel;
    IpsecAction action = IpsecDb.protocore_ipsec_spd_add_args.action;
    uint32_t sa_spi = IpsecDb.protocore_ipsec_spd_add_args.sa_spi;

    if (!spd || !sel || spd->count >= PROTOCORE_IPSEC_SPD_MAX)
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    IpsecPolicy *p = &spd->entries[spd->count];
    p->sel = *sel;
    p->action = action;
    p->sa_spi = (action == IPSEC_ACTION_PROTECT) ? sa_spi : 0;
    spd->count++;
    IpsecDb.ok = PROTO_TRUE;
}

static void ipsec_db_protocore_ipsec_selector_match(uint8_t *restrict work)
{
    (void)work;
    const IpsecSelector *sel = IpsecDb.protocore_ipsec_selector_match_args.sel;
    const IpsecFlow *flow = IpsecDb.protocore_ipsec_selector_match_args.flow;

    if (!sel || !flow || !flow->src || !flow->dst)
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    if (sel->addr_len != flow->addr_len) // different address family
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    if (sel->addr_len != 4 && sel->addr_len != 16)
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    if (sel->ip_protocol != 0 && sel->ip_protocol != flow->ip_protocol)
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    if (!in_range(flow->src, sel->src_lo, sel->src_hi, sel->addr_len))
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    if (!in_range(flow->dst, sel->dst_lo, sel->dst_hi, sel->addr_len))
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    if (!port_in(flow->src_port, sel->src_port_lo, sel->src_port_hi))
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    if (!port_in(flow->dst_port, sel->dst_port_lo, sel->dst_port_hi))
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    IpsecDb.ok = PROTO_TRUE;
}

static void ipsec_db_protocore_ipsec_spd_lookup(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const IpsecSpd *spd = IpsecDb.protocore_ipsec_spd_lookup_args.spd;
    const IpsecFlow *flow = IpsecDb.protocore_ipsec_spd_lookup_args.flow;

    if (!spd || !flow)
    {
        IpsecDb.ptr = NULL;
        return;
    }
    for (size_t i = 0; i < spd->count; i++) // first match wins (order is significant)
    {
        IpsecDb.protocore_ipsec_selector_match_args.sel = &spd->entries[i].sel;
        IpsecDb.protocore_ipsec_selector_match_args.flow = flow;
        ipsec_db_protocore_ipsec_selector_match(work);
        if (IpsecDb.ok)
        {
            IpsecDb.ptr = &spd->entries[i];
            return;
        }
    }
    IpsecDb.ptr = NULL;
}

static void ipsec_db_protocore_ipsec_selector_from_ts(uint8_t *restrict work)
{
    (void)work;
    IpsecSelector *out = IpsecDb.protocore_ipsec_selector_from_ts_args.out;
    const IkeTrafficSelector *ts_src = IpsecDb.protocore_ipsec_selector_from_ts_args.ts_src;
    const IkeTrafficSelector *ts_dst = IpsecDb.protocore_ipsec_selector_from_ts_args.ts_dst;

    if (!out || !ts_src || !ts_dst)
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    if (ts_src->ts_type != ts_dst->ts_type)
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    if (ts_src->addr_len != ts_dst->addr_len || (ts_src->addr_len != 4 && ts_src->addr_len != 16))
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    if (!ts_src->start_addr || !ts_src->end_addr || !ts_dst->start_addr || !ts_dst->end_addr)
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    // Protocol: honor "any" (0) on either side; if both name a protocol they must agree.
    if (ts_src->ip_protocol != 0 && ts_dst->ip_protocol != 0 && ts_src->ip_protocol != ts_dst->ip_protocol)
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }

    mem.set(out, 0, sizeof(*out));
    uint8_t len = (uint8_t)ts_src->addr_len; // addr_len is an IP address length (<= 16), fits a uint8_t
    out->addr_len = len;
    out->ip_protocol = ts_src->ip_protocol ? ts_src->ip_protocol : ts_dst->ip_protocol;
    mem.cpy(out->src_lo, ts_src->start_addr, len);
    mem.cpy(out->src_hi, ts_src->end_addr, len);
    mem.cpy(out->dst_lo, ts_dst->start_addr, len);
    mem.cpy(out->dst_hi, ts_dst->end_addr, len);
    out->src_port_lo = ts_src->start_port;
    out->src_port_hi = ts_src->end_port;
    out->dst_port_lo = ts_dst->start_port;
    out->dst_port_hi = ts_dst->end_port;
    IpsecDb.ok = PROTO_TRUE;
}

// ── SAD ─────────────────────────────────────────────────────────────────────────────────────────

static void ipsec_db_protocore_ipsec_sad_init(uint8_t *restrict work)
{
    (void)work;
    IpsecSad *sad = IpsecDb.protocore_ipsec_sad_init_args.sad;

    if (!sad)
    {
        return;
    }
    sad->count = 0;
    for (size_t i = 0; i < PROTOCORE_IPSEC_SAD_MAX; i++)
    {
        sad->entries[i].valid = PROTO_FALSE;
    }
}

static void ipsec_db_protocore_ipsec_sad_add(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    IpsecSad *sad = IpsecDb.protocore_ipsec_sad_add_args.sad;
    uint32_t spi = IpsecDb.protocore_ipsec_sad_add_args.spi;
    const uint8_t *dst = IpsecDb.protocore_ipsec_sad_add_args.dst;
    uint8_t addr_len = IpsecDb.protocore_ipsec_sad_add_args.addr_len;
    const uint8_t *key = IpsecDb.protocore_ipsec_sad_add_args.key;
    const uint8_t *salt = IpsecDb.protocore_ipsec_sad_add_args.salt;
    proto_bool inbound = IpsecDb.protocore_ipsec_sad_add_args.inbound;

    if (!sad || !dst || !key || !salt || (addr_len != 4 && addr_len != 16))
    {
        IpsecDb.sa = NULL;
        return;
    }
    IpsecDb.protocore_ipsec_sad_find_args.sad = sad;
    IpsecDb.protocore_ipsec_sad_find_args.spi = spi;
    ipsec_db_protocore_ipsec_sad_find(work);
    if (IpsecDb.sa) // SPIs are unique within a SAD
    {
        IpsecDb.sa = NULL;
        return;
    }
    IpsecSaEntry *e = NULL;
    for (size_t i = 0; i < PROTOCORE_IPSEC_SAD_MAX; i++)
    {
        if (!sad->entries[i].valid)
        {
            e = &sad->entries[i];
            break;
        }
    }
    if (!e) // full
    {
        IpsecDb.sa = NULL;
        return;
    }

    mem.set(e, 0, sizeof(*e));
    e->spi = spi;
    e->addr_len = addr_len;
    mem.cpy(e->dst, dst, addr_len);
    mem.cpy(e->key, key, PROTOCORE_ESP_KEY_LEN);
    mem.cpy(e->salt, salt, PROTOCORE_ESP_SALT_LEN);
    e->seq = 0;
    e->inbound = inbound;
    if (inbound)
    {
        Esp.replay_init_args.r = &e->replay;
        Esp.replay_init(esp_work);
    }
    e->valid = PROTO_TRUE;
    sad->count++;
    IpsecDb.sa = e;
}

static void ipsec_db_protocore_ipsec_sad_find(uint8_t *restrict work)
{
    (void)work;
    IpsecSad *sad = IpsecDb.protocore_ipsec_sad_find_args.sad;
    uint32_t spi = IpsecDb.protocore_ipsec_sad_find_args.spi;

    if (!sad)
    {
        IpsecDb.sa = NULL;
        return;
    }
    for (size_t i = 0; i < PROTOCORE_IPSEC_SAD_MAX; i++)
    {
        if (sad->entries[i].valid && sad->entries[i].spi == spi)
        {
            IpsecDb.sa = &sad->entries[i];
            return;
        }
    }
    IpsecDb.sa = NULL;
}

static void ipsec_db_protocore_ipsec_sad_remove(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    IpsecSad *sad = IpsecDb.protocore_ipsec_sad_remove_args.sad;
    uint32_t spi = IpsecDb.protocore_ipsec_sad_remove_args.spi;

    IpsecDb.protocore_ipsec_sad_find_args.sad = sad;
    IpsecDb.protocore_ipsec_sad_find_args.spi = spi;
    ipsec_db_protocore_ipsec_sad_find(work);
    IpsecSaEntry *e = IpsecDb.sa;
    if (!e)
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    mem.set(e, 0, sizeof(*e)); // wipe the key material with the slot
    e->valid = PROTO_FALSE;
    if (sad->count)
    {
        sad->count--;
    }
    IpsecDb.ok = PROTO_TRUE;
}

static void ipsec_db_protocore_ipsec_sad_next_seq(uint8_t *restrict work)
{
    (void)work;
    IpsecSaEntry *sa = IpsecDb.protocore_ipsec_sad_next_seq_args.sa;
    uint32_t *seq_out = IpsecDb.protocore_ipsec_sad_next_seq_args.seq_out;

    if (!sa || !seq_out)
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    if (sa->seq == 0xFFFFFFFFu) // counter exhausted - must rekey before sending more (RFC 4303 §3.3.3)
    {
        IpsecDb.ok = PROTO_FALSE;
        return;
    }
    sa->seq++; // pre-increment: the first packet uses sequence number 1
    *seq_out = sa->seq;
    IpsecDb.ok = PROTO_TRUE;
}

IpsecDbNs IpsecDb = {
    .protocore_ipsec_spd_init = ipsec_db_protocore_ipsec_spd_init,
    .protocore_ipsec_spd_add = ipsec_db_protocore_ipsec_spd_add,
    .protocore_ipsec_spd_lookup = ipsec_db_protocore_ipsec_spd_lookup,
    .protocore_ipsec_selector_match = ipsec_db_protocore_ipsec_selector_match,
    .protocore_ipsec_selector_from_ts = ipsec_db_protocore_ipsec_selector_from_ts,
    .protocore_ipsec_sad_init = ipsec_db_protocore_ipsec_sad_init,
    .protocore_ipsec_sad_add = ipsec_db_protocore_ipsec_sad_add,
    .protocore_ipsec_sad_find = ipsec_db_protocore_ipsec_sad_find,
    .protocore_ipsec_sad_remove = ipsec_db_protocore_ipsec_sad_remove,
    .protocore_ipsec_sad_next_seq = ipsec_db_protocore_ipsec_sad_next_seq,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IKEV2
