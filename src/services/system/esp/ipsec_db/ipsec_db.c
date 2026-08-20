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

void protocore_ipsec_db_protocore_ipsec_sad_find(uint8_t *restrict work);
void protocore_ipsec_db_protocore_ipsec_selector_match(uint8_t *restrict work);

void protocore_ipsec_db_protocore_ipsec_spd_init(uint8_t *restrict work)
{
    (void)work;
    IpsecSpd *spd = IpsecDbV.protocore_ipsec_spd_init_args.spd;

    if (!spd)
    {
        return;
    }
    spd->count = 0;
}

void protocore_ipsec_db_protocore_ipsec_spd_add(uint8_t *restrict work)
{
    (void)work;
    IpsecSpd *spd = IpsecDbV.protocore_ipsec_spd_add_args.spd;
    const IpsecSelector *sel = IpsecDbV.protocore_ipsec_spd_add_args.sel;
    IpsecAction action = IpsecDbV.protocore_ipsec_spd_add_args.action;
    uint32_t sa_spi = IpsecDbV.protocore_ipsec_spd_add_args.sa_spi;

    if (!spd || !sel || spd->count >= PROTOCORE_IPSEC_SPD_MAX)
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    IpsecPolicy *p = &spd->entries[spd->count];
    p->sel = *sel;
    p->action = action;
    p->sa_spi = (action == IPSEC_ACTION_PROTECT) ? sa_spi : 0;
    spd->count++;
    IpsecDbV.ok = PROTO_TRUE;
}

void protocore_ipsec_db_protocore_ipsec_selector_match(uint8_t *restrict work)
{
    (void)work;
    const IpsecSelector *sel = IpsecDbV.protocore_ipsec_selector_match_args.sel;
    const IpsecFlow *flow = IpsecDbV.protocore_ipsec_selector_match_args.flow;

    if (!sel || !flow || !flow->src || !flow->dst)
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    if (sel->addr_len != flow->addr_len) // different address family
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    if (sel->addr_len != 4 && sel->addr_len != 16)
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    if (sel->ip_protocol != 0 && sel->ip_protocol != flow->ip_protocol)
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    if (!in_range(flow->src, sel->src_lo, sel->src_hi, sel->addr_len))
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    if (!in_range(flow->dst, sel->dst_lo, sel->dst_hi, sel->addr_len))
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    if (!port_in(flow->src_port, sel->src_port_lo, sel->src_port_hi))
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    if (!port_in(flow->dst_port, sel->dst_port_lo, sel->dst_port_hi))
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    IpsecDbV.ok = PROTO_TRUE;
}

void protocore_ipsec_db_protocore_ipsec_spd_lookup(uint8_t *restrict work)
{
    const IpsecSpd *spd = IpsecDbV.protocore_ipsec_spd_lookup_args.spd;
    const IpsecFlow *flow = IpsecDbV.protocore_ipsec_spd_lookup_args.flow;

    if (!spd || !flow)
    {
        IpsecDbV.ptr = NULL;
        return;
    }
    for (size_t i = 0; i < spd->count; i++) // first match wins (order is significant)
    {
        IpsecDbV.protocore_ipsec_selector_match_args.sel = &spd->entries[i].sel;
        IpsecDbV.protocore_ipsec_selector_match_args.flow = flow;
        protocore_ipsec_db_protocore_ipsec_selector_match(work);
        if (IpsecDbV.ok)
        {
            IpsecDbV.ptr = &spd->entries[i];
            return;
        }
    }
    IpsecDbV.ptr = NULL;
}

void protocore_ipsec_db_protocore_ipsec_selector_from_ts(uint8_t *restrict work)
{
    (void)work;
    IpsecSelector *out = IpsecDbV.protocore_ipsec_selector_from_ts_args.out;
    const IkeTrafficSelector *ts_src = IpsecDbV.protocore_ipsec_selector_from_ts_args.ts_src;
    const IkeTrafficSelector *ts_dst = IpsecDbV.protocore_ipsec_selector_from_ts_args.ts_dst;

    if (!out || !ts_src || !ts_dst)
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    if (ts_src->ts_type != ts_dst->ts_type)
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    if (ts_src->addr_len != ts_dst->addr_len || (ts_src->addr_len != 4 && ts_src->addr_len != 16))
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    if (!ts_src->start_addr || !ts_src->end_addr || !ts_dst->start_addr || !ts_dst->end_addr)
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    // Protocol: honor "any" (0) on either side; if both name a protocol they must agree.
    if (ts_src->ip_protocol != 0 && ts_dst->ip_protocol != 0 && ts_src->ip_protocol != ts_dst->ip_protocol)
    {
        IpsecDbV.ok = PROTO_FALSE;
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
    IpsecDbV.ok = PROTO_TRUE;
}

// ── SAD ─────────────────────────────────────────────────────────────────────────────────────────

void protocore_ipsec_db_protocore_ipsec_sad_init(uint8_t *restrict work)
{
    (void)work;
    IpsecSad *sad = IpsecDbV.protocore_ipsec_sad_init_args.sad;

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

void protocore_ipsec_db_protocore_ipsec_sad_add(uint8_t *restrict work)
{
    IpsecSad *sad = IpsecDbV.protocore_ipsec_sad_add_args.sad;
    uint32_t spi = IpsecDbV.protocore_ipsec_sad_add_args.spi;
    const uint8_t *dst = IpsecDbV.protocore_ipsec_sad_add_args.dst;
    uint8_t addr_len = IpsecDbV.protocore_ipsec_sad_add_args.addr_len;
    const uint8_t *key = IpsecDbV.protocore_ipsec_sad_add_args.key;
    const uint8_t *salt = IpsecDbV.protocore_ipsec_sad_add_args.salt;
    proto_bool inbound = IpsecDbV.protocore_ipsec_sad_add_args.inbound;

    if (!sad || !dst || !key || !salt || (addr_len != 4 && addr_len != 16))
    {
        IpsecDbV.sa = NULL;
        return;
    }
    IpsecDbV.protocore_ipsec_sad_find_args.sad = sad;
    IpsecDbV.protocore_ipsec_sad_find_args.spi = spi;
    protocore_ipsec_db_protocore_ipsec_sad_find(work);
    if (IpsecDbV.sa) // SPIs are unique within a SAD
    {
        IpsecDbV.sa = NULL;
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
        IpsecDbV.sa = NULL;
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
        EspV.replay_init_args.r = &e->replay;
        Esp.replay_init(esp_work);
    }
    e->valid = PROTO_TRUE;
    sad->count++;
    IpsecDbV.sa = e;
}

void protocore_ipsec_db_protocore_ipsec_sad_find(uint8_t *restrict work)
{
    (void)work;
    IpsecSad *sad = IpsecDbV.protocore_ipsec_sad_find_args.sad;
    uint32_t spi = IpsecDbV.protocore_ipsec_sad_find_args.spi;

    if (!sad)
    {
        IpsecDbV.sa = NULL;
        return;
    }
    for (size_t i = 0; i < PROTOCORE_IPSEC_SAD_MAX; i++)
    {
        if (sad->entries[i].valid && sad->entries[i].spi == spi)
        {
            IpsecDbV.sa = &sad->entries[i];
            return;
        }
    }
    IpsecDbV.sa = NULL;
}

void protocore_ipsec_db_protocore_ipsec_sad_remove(uint8_t *restrict work)
{
    IpsecSad *sad = IpsecDbV.protocore_ipsec_sad_remove_args.sad;
    uint32_t spi = IpsecDbV.protocore_ipsec_sad_remove_args.spi;

    IpsecDbV.protocore_ipsec_sad_find_args.sad = sad;
    IpsecDbV.protocore_ipsec_sad_find_args.spi = spi;
    protocore_ipsec_db_protocore_ipsec_sad_find(work);
    IpsecSaEntry *e = IpsecDbV.sa;
    if (!e)
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    mem.set(e, 0, sizeof(*e)); // wipe the key material with the slot
    e->valid = PROTO_FALSE;
    if (sad->count)
    {
        sad->count--;
    }
    IpsecDbV.ok = PROTO_TRUE;
}

void protocore_ipsec_db_protocore_ipsec_sad_next_seq(uint8_t *restrict work)
{
    (void)work;
    IpsecSaEntry *sa = IpsecDbV.protocore_ipsec_sad_next_seq_args.sa;
    uint32_t *seq_out = IpsecDbV.protocore_ipsec_sad_next_seq_args.seq_out;

    if (!sa || !seq_out)
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    if (sa->seq == 0xFFFFFFFFu) // counter exhausted - must rekey before sending more (RFC 4303 §3.3.3)
    {
        IpsecDbV.ok = PROTO_FALSE;
        return;
    }
    sa->seq++; // pre-increment: the first packet uses sequence number 1
    *seq_out = sa->seq;
    IpsecDbV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
IpsecDbVars IpsecDbV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IKEV2
