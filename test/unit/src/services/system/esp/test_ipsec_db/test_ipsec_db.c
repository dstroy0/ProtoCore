// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IPsec policy and SA databases (services/system/esp/ipsec_db.h).
//
// RFC 4301 sec 4.4.1 makes the SPD an ORDERED list matched against a packet's selectors, where the
// first matching entry decides PROTECT, BYPASS or DISCARD and nothing later is consulted; sec 4.4.4.1
// makes an unmatched packet a discard. test_rfc4301_spd_is_ordered_first_match is the load-bearing
// case: the whole security posture of the stack is that ordering, and a lookup that returned any
// match other than the earliest would silently bypass traffic a policy above it protects. The SAD
// side is sec 4.4.2's SPI-keyed lookup, with RFC 4303 sec 3.3.3 fixing the outbound sequence number
// at 1 for the first packet and forbidding a cycle.

#include "services/system/esp/ipsec_db.h"
#include <string.h>

#include <unity.h>

static uint8_t ipsec_db_work[16]; // the borrow an entry takes; IpsecDb never reads it

static uint8_t esp_work[16]; // the borrow an entry takes; Esp never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// Big-endian IPv4 octets, the form RFC 4301 selectors and packets both carry.
static void v4(uint8_t out[4], uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    out[0] = a;
    out[1] = b;
    out[2] = c;
    out[3] = d;
}

// A selector over one IPv4 source range, one destination range, any protocol and any port.
static void sel_v4(IpsecSelector *s, const uint8_t *src_lo, const uint8_t *src_hi, const uint8_t *dst_lo,
                   const uint8_t *dst_hi, uint8_t proto)
{
    memset(s, 0, sizeof(*s));
    s->addr_len = 4;
    s->ip_protocol = proto;
    memcpy(s->src_lo, src_lo, 4);
    memcpy(s->src_hi, src_hi, 4);
    memcpy(s->dst_lo, dst_lo, 4);
    memcpy(s->dst_hi, dst_hi, 4);
    s->src_port_lo = 0;
    s->src_port_hi = 65535;
    s->dst_port_lo = 0;
    s->dst_port_hi = 65535;
}

// The load-bearing case: three policies whose selectors overlap, matched in the order they were
// added. 10.0.0.5 falls inside all three ranges, so only the first may answer for it; deleting the
// ordering would hand back BYPASS or DISCARD for traffic the first policy protects.
void test_rfc4301_spd_is_ordered_first_match(void)
{
    uint8_t any_lo[4];
    uint8_t any_hi[4];
    uint8_t host_lo[4];
    uint8_t host_hi[4];
    uint8_t net_lo[4];
    uint8_t net_hi[4];
    v4(any_lo, 0, 0, 0, 0);
    v4(any_hi, 255, 255, 255, 255);
    v4(host_lo, 10, 0, 0, 5);
    v4(host_hi, 10, 0, 0, 5);
    v4(net_lo, 10, 0, 0, 0);
    v4(net_hi, 10, 0, 0, 255);

    IpsecSpd spd;
    IpsecDb.protocore_ipsec_spd_init_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_init(ipsec_db_work);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)spd.count);

    IpsecSelector host;
    IpsecSelector net;
    IpsecSelector any;
    sel_v4(&host, host_lo, host_hi, any_lo, any_hi, 0);
    sel_v4(&net, net_lo, net_hi, any_lo, any_hi, 0);
    sel_v4(&any, any_lo, any_hi, any_lo, any_hi, 0);

    IpsecDb.protocore_ipsec_spd_add_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_add_args.sel = &host;
    IpsecDb.protocore_ipsec_spd_add_args.action = IPSEC_ACTION_PROTECT;
    IpsecDb.protocore_ipsec_spd_add_args.sa_spi = 0xAAAA1111u;
    IpsecDb.protocore_ipsec_spd_add(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    IpsecDb.protocore_ipsec_spd_add_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_add_args.sel = &net;
    IpsecDb.protocore_ipsec_spd_add_args.action = IPSEC_ACTION_BYPASS;
    IpsecDb.protocore_ipsec_spd_add_args.sa_spi = 0;
    IpsecDb.protocore_ipsec_spd_add(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    IpsecDb.protocore_ipsec_spd_add_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_add_args.sel = &any;
    IpsecDb.protocore_ipsec_spd_add_args.action = IPSEC_ACTION_DISCARD;
    IpsecDb.protocore_ipsec_spd_add_args.sa_spi = 0;
    IpsecDb.protocore_ipsec_spd_add(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)spd.count);

    uint8_t src[4];
    uint8_t dst[4];
    v4(dst, 192, 168, 1, 1);
    IpsecFlow flow = {4, 6, src, dst, 1024, 443};

    v4(src, 10, 0, 0, 5); // matches all three: the first wins
    IpsecDb.protocore_ipsec_spd_lookup_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_lookup_args.flow = &flow;
    IpsecDb.protocore_ipsec_spd_lookup(ipsec_db_work);
    const IpsecPolicy *p = IpsecDb.ptr;
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(IPSEC_ACTION_PROTECT, (int)p->action);
    TEST_ASSERT_EQUAL_HEX32(0xAAAA1111u, p->sa_spi);

    v4(src, 10, 0, 0, 6); // matches the /24 and the catch-all
    IpsecDb.protocore_ipsec_spd_lookup_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_lookup_args.flow = &flow;
    IpsecDb.protocore_ipsec_spd_lookup(ipsec_db_work);
    p = IpsecDb.ptr;
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(IPSEC_ACTION_BYPASS, (int)p->action);

    v4(src, 172, 16, 0, 1); // only the catch-all
    IpsecDb.protocore_ipsec_spd_lookup_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_lookup_args.flow = &flow;
    IpsecDb.protocore_ipsec_spd_lookup(ipsec_db_work);
    p = IpsecDb.ptr;
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(IPSEC_ACTION_DISCARD, (int)p->action);
}

// sec 4.4.4.1: with no policy matching, the lookup names none and the caller drops the packet.
void test_spd_lookup_reports_no_match(void)
{
    uint8_t lo[4];
    uint8_t hi[4];
    v4(lo, 10, 0, 0, 0);
    v4(hi, 10, 0, 0, 255);

    IpsecSpd spd;
    IpsecDb.protocore_ipsec_spd_init_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_init(ipsec_db_work);
    IpsecSelector s;
    sel_v4(&s, lo, hi, lo, hi, 0);
    IpsecDb.protocore_ipsec_spd_add_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_add_args.sel = &s;
    IpsecDb.protocore_ipsec_spd_add_args.action = IPSEC_ACTION_PROTECT;
    IpsecDb.protocore_ipsec_spd_add_args.sa_spi = 1;
    IpsecDb.protocore_ipsec_spd_add(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);

    uint8_t src[4];
    uint8_t dst[4];
    v4(src, 10, 0, 1, 1); // outside the /24
    v4(dst, 10, 0, 0, 1);
    IpsecFlow flow = {4, 6, src, dst, 1, 1};
    IpsecDb.protocore_ipsec_spd_lookup_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_lookup_args.flow = &flow;
    IpsecDb.protocore_ipsec_spd_lookup(ipsec_db_work);
    TEST_ASSERT_NULL(IpsecDb.ptr);

    // An empty SPD matches nothing at all.
    IpsecSpd empty;
    IpsecDb.protocore_ipsec_spd_init_args.spd = &empty;
    IpsecDb.protocore_ipsec_spd_init(ipsec_db_work);
    v4(src, 10, 0, 0, 1);
    IpsecDb.protocore_ipsec_spd_lookup_args.spd = &empty;
    IpsecDb.protocore_ipsec_spd_lookup_args.flow = &flow;
    IpsecDb.protocore_ipsec_spd_lookup(ipsec_db_work);
    TEST_ASSERT_NULL(IpsecDb.ptr);
}

// sec 4.4.1.1: a selector's address and port fields are inclusive ranges, so both endpoints match
// and the value just outside does not. The protocol is "any" when zero.
void test_selector_ranges_are_inclusive(void)
{
    uint8_t slo[4];
    uint8_t shi[4];
    uint8_t dlo[4];
    uint8_t dhi[4];
    v4(slo, 10, 0, 0, 10);
    v4(shi, 10, 0, 0, 20);
    v4(dlo, 192, 168, 0, 0);
    v4(dhi, 192, 168, 0, 255);

    IpsecSelector s;
    sel_v4(&s, slo, shi, dlo, dhi, 6); // TCP only
    s.src_port_lo = 1000;
    s.src_port_hi = 2000;
    s.dst_port_lo = 443;
    s.dst_port_hi = 443;

    uint8_t src[4];
    uint8_t dst[4];
    v4(src, 10, 0, 0, 15);
    v4(dst, 192, 168, 0, 7);
    IpsecFlow f = {4, 6, src, dst, 1500, 443};
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);

    v4(src, 10, 0, 0, 10); // the low endpoint
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    v4(src, 10, 0, 0, 20); // the high endpoint
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    v4(src, 10, 0, 0, 9); // one below
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    v4(src, 10, 0, 0, 21); // one above
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);

    v4(src, 10, 0, 0, 15);
    f.src_port = 1000;
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    f.src_port = 2000;
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    f.src_port = 999;
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    f.src_port = 2001;
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);

    f.src_port = 1500;
    f.dst_port = 444;
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    f.dst_port = 443;

    f.ip_protocol = 17; // UDP against a TCP-only selector
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    s.ip_protocol = 0; // "any" matches every protocol
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
}

// An address range compares whole addresses, not octets: 10.0.1.255 is above 10.0.2.0 in no
// ordering, and a v4 flow never matches a v6 selector.
void test_selector_compares_whole_addresses_and_families(void)
{
    uint8_t slo[4];
    uint8_t shi[4];
    uint8_t any_lo[4];
    uint8_t any_hi[4];
    v4(slo, 10, 0, 1, 0);
    v4(shi, 10, 0, 2, 0);
    v4(any_lo, 0, 0, 0, 0);
    v4(any_hi, 255, 255, 255, 255);

    IpsecSelector s;
    sel_v4(&s, slo, shi, any_lo, any_hi, 0);

    uint8_t src[4];
    uint8_t dst[4];
    v4(dst, 1, 1, 1, 1);
    IpsecFlow f = {4, 6, src, dst, 1, 1};

    v4(src, 10, 0, 1, 255); // inside: below 10.0.2.0 as a whole address
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    v4(src, 10, 0, 2, 1); // outside: above the high endpoint
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    v4(src, 10, 0, 0, 255); // outside: below the low endpoint
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);

    // An IPv6-length flow cannot match an IPv4 selector.
    v4(src, 10, 0, 1, 1);
    uint8_t src6[16] = {0};
    uint8_t dst6[16] = {0};
    IpsecFlow f6 = {16, 6, src6, dst6, 1, 1};
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f6;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);

    // A selector whose address length is neither 4 nor 16 names no family.
    IpsecSelector bad = s;
    bad.addr_len = 8;
    IpsecFlow f8 = {8, 6, src, dst, 1, 1};
    IpsecDb.protocore_ipsec_selector_match_args.sel = &bad;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f8;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
}

// An IPv6 selector compares all sixteen octets.
void test_selector_matches_ipv6_ranges(void)
{
    IpsecSelector s;
    memset(&s, 0, sizeof(s));
    s.addr_len = 16;
    s.ip_protocol = 0;
    // 2001:db8:: through 2001:db8::ffff, any destination.
    static const uint8_t LO[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00};
    static const uint8_t HI[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    static const uint8_t ANY_LO[16] = {0};
    static const uint8_t ANY_HI[16] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    memcpy(s.src_lo, LO, 16);
    memcpy(s.src_hi, HI, 16);
    memcpy(s.dst_lo, ANY_LO, 16);
    memcpy(s.dst_hi, ANY_HI, 16);
    s.src_port_hi = 65535;
    s.dst_port_hi = 65535;

    uint8_t src[16];
    uint8_t dst[16] = {0};
    IpsecFlow f = {16, 6, src, dst, 0, 0};

    memcpy(src, LO, 16);
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    memcpy(src, HI, 16);
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    memcpy(src, HI, 16);
    src[13] = 0x01; // 2001:db8::1:ffff, past the high endpoint
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    memcpy(src, LO, 16);
    src[3] = 0xb7; // 2001:db7::, below the low endpoint
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
}

// The SPD is bounded storage: it takes exactly PROTOCORE_IPSEC_SPD_MAX policies and refuses the
// next one rather than overwriting an earlier, higher-priority entry.
void test_spd_is_bounded(void)
{
    uint8_t lo[4];
    uint8_t hi[4];
    v4(lo, 0, 0, 0, 0);
    v4(hi, 255, 255, 255, 255);
    IpsecSelector s;
    sel_v4(&s, lo, hi, lo, hi, 0);

    IpsecSpd spd;
    IpsecDb.protocore_ipsec_spd_init_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_init(ipsec_db_work);
    for (unsigned i = 0; i < PROTOCORE_IPSEC_SPD_MAX; i++)
    {
        IpsecDb.protocore_ipsec_spd_add_args.spd = &spd;
        IpsecDb.protocore_ipsec_spd_add_args.sel = &s;
        IpsecDb.protocore_ipsec_spd_add_args.action = IPSEC_ACTION_BYPASS;
        IpsecDb.protocore_ipsec_spd_add_args.sa_spi = 0;
        IpsecDb.protocore_ipsec_spd_add(ipsec_db_work);
        TEST_ASSERT_TRUE(IpsecDb.ok);
    }
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_IPSEC_SPD_MAX, (uint32_t)spd.count);
    IpsecDb.protocore_ipsec_spd_add_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_add_args.sel = &s;
    IpsecDb.protocore_ipsec_spd_add_args.action = IPSEC_ACTION_BYPASS;
    IpsecDb.protocore_ipsec_spd_add_args.sa_spi = 0;
    IpsecDb.protocore_ipsec_spd_add(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_IPSEC_SPD_MAX, (uint32_t)spd.count);
}

// RFC 4301 sec 4.4.1.2 builds an SPD entry from the traffic selectors IKEv2 negotiated: the local
// TS becomes the source range and the peer TS the destination range, with the protocol carried over.
void test_selector_from_ikev2_traffic_selectors(void)
{
    static const uint8_t SRC_LO[4] = {10, 0, 0, 0};
    static const uint8_t SRC_HI[4] = {10, 0, 0, 255};
    static const uint8_t DST_LO[4] = {192, 168, 1, 0};
    static const uint8_t DST_HI[4] = {192, 168, 1, 255};

    IkeTrafficSelector tsi;
    IkeTrafficSelector tsr;
    memset(&tsi, 0, sizeof(tsi));
    memset(&tsr, 0, sizeof(tsr));
    tsi.ip_protocol = 6;
    tsi.start_port = 0;
    tsi.end_port = 65535;
    tsi.start_addr = SRC_LO;
    tsi.end_addr = SRC_HI;
    tsi.addr_len = 4;
    tsr.ip_protocol = 6;
    tsr.start_port = 443;
    tsr.end_port = 443;
    tsr.start_addr = DST_LO;
    tsr.end_addr = DST_HI;
    tsr.addr_len = 4;

    IpsecSelector s;
    IpsecDb.protocore_ipsec_selector_from_ts_args.out = &s;
    IpsecDb.protocore_ipsec_selector_from_ts_args.ts_src = &tsi;
    IpsecDb.protocore_ipsec_selector_from_ts_args.ts_dst = &tsr;
    IpsecDb.protocore_ipsec_selector_from_ts(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    TEST_ASSERT_EQUAL_UINT8(4, s.addr_len);
    TEST_ASSERT_EQUAL_UINT8(6, s.ip_protocol);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SRC_LO, s.src_lo, 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SRC_HI, s.src_hi, 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DST_LO, s.dst_lo, 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DST_HI, s.dst_hi, 4);
    TEST_ASSERT_EQUAL_UINT16(0, s.src_port_lo);
    TEST_ASSERT_EQUAL_UINT16(65535, s.src_port_hi);
    TEST_ASSERT_EQUAL_UINT16(443, s.dst_port_lo);
    TEST_ASSERT_EQUAL_UINT16(443, s.dst_port_hi);

    // The selector it produced matches traffic inside the negotiated ranges and nothing outside.
    uint8_t src[4];
    uint8_t dst[4];
    v4(src, 10, 0, 0, 7);
    v4(dst, 192, 168, 1, 9);
    IpsecFlow f = {4, 6, src, dst, 5000, 443};
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    f.dst_port = 80;
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);

    // Both selectors must name the same address family.
    tsr.addr_len = 16;
    IpsecDb.protocore_ipsec_selector_from_ts_args.out = &s;
    IpsecDb.protocore_ipsec_selector_from_ts_args.ts_src = &tsi;
    IpsecDb.protocore_ipsec_selector_from_ts_args.ts_dst = &tsr;
    IpsecDb.protocore_ipsec_selector_from_ts(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    tsr.addr_len = 4;
    IpsecDb.protocore_ipsec_selector_from_ts_args.out = NULL;
    IpsecDb.protocore_ipsec_selector_from_ts_args.ts_src = &tsi;
    IpsecDb.protocore_ipsec_selector_from_ts_args.ts_dst = &tsr;
    IpsecDb.protocore_ipsec_selector_from_ts(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    IpsecDb.protocore_ipsec_selector_from_ts_args.out = &s;
    IpsecDb.protocore_ipsec_selector_from_ts_args.ts_src = NULL;
    IpsecDb.protocore_ipsec_selector_from_ts_args.ts_dst = &tsr;
    IpsecDb.protocore_ipsec_selector_from_ts(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    IpsecDb.protocore_ipsec_selector_from_ts_args.out = &s;
    IpsecDb.protocore_ipsec_selector_from_ts_args.ts_src = &tsi;
    IpsecDb.protocore_ipsec_selector_from_ts_args.ts_dst = NULL;
    IpsecDb.protocore_ipsec_selector_from_ts(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
}

static const uint8_t SA_KEY[PROTOCORE_ESP_KEY_LEN] = {1};
static const uint8_t SA_SALT[PROTOCORE_ESP_SALT_LEN] = {0xde, 0xad, 0xbe, 0xef};

// RFC 4301 sec 4.4.2: the SAD is keyed by SPI, and an inbound ESP packet is demuxed to its SA by
// that key alone. SPIs are unique within the database.
void test_rfc4301_sad_is_keyed_by_spi(void)
{
    uint8_t dst[4];
    v4(dst, 192, 168, 1, 1);

    IpsecSad sad;
    IpsecDb.protocore_ipsec_sad_init_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_init(ipsec_db_work);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)sad.count);
    IpsecDb.protocore_ipsec_sad_find_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_find_args.spi = 0x1000u;
    IpsecDb.protocore_ipsec_sad_find(ipsec_db_work);
    TEST_ASSERT_NULL(IpsecDb.ptr);

    IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 0x1000u;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_FALSE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    IpsecSaEntry *out = IpsecDb.ptr;
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_HEX32(0x1000u, out->spi);
    TEST_ASSERT_FALSE(out->inbound);
    TEST_ASSERT_TRUE(out->valid);
    TEST_ASSERT_EQUAL_UINT32(0, out->seq); // no packet issued yet
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SA_KEY, out->key, PROTOCORE_ESP_KEY_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SA_SALT, out->salt, PROTOCORE_ESP_SALT_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(dst, out->dst, 4);

    IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 0x2000u;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_TRUE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    IpsecSaEntry *in = IpsecDb.ptr;
    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_TRUE(in->inbound);
    TEST_ASSERT_FALSE(in->replay.seen_any); // the anti-replay window starts empty

    IpsecDb.protocore_ipsec_sad_find_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_find_args.spi = 0x1000u;
    IpsecDb.protocore_ipsec_sad_find(ipsec_db_work);
    TEST_ASSERT_EQUAL_PTR(out, IpsecDb.ptr);
    IpsecDb.protocore_ipsec_sad_find_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_find_args.spi = 0x2000u;
    IpsecDb.protocore_ipsec_sad_find(ipsec_db_work);
    TEST_ASSERT_EQUAL_PTR(in, IpsecDb.ptr);
    IpsecDb.protocore_ipsec_sad_find_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_find_args.spi = 0x3000u;
    IpsecDb.protocore_ipsec_sad_find(ipsec_db_work);
    TEST_ASSERT_NULL(IpsecDb.ptr);

    // A duplicate SPI is refused: two SAs answering one demux key would be ambiguous.
    IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 0x1000u;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_TRUE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    TEST_ASSERT_NULL(IpsecDb.ptr);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)sad.count);
}

// An IKE DELETE removes an SA; its SPI then names nothing, and the slot can be reused.
void test_sad_remove(void)
{
    uint8_t dst[4];
    v4(dst, 10, 0, 0, 1);
    IpsecSad sad;
    IpsecDb.protocore_ipsec_sad_init_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_init(ipsec_db_work);
    IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 1u;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_FALSE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    TEST_ASSERT_NOT_NULL(IpsecDb.ptr);
    IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 2u;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_TRUE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    TEST_ASSERT_NOT_NULL(IpsecDb.ptr);

    IpsecDb.protocore_ipsec_sad_remove_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_remove_args.spi = 1u;
    IpsecDb.protocore_ipsec_sad_remove(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    IpsecDb.protocore_ipsec_sad_find_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_find_args.spi = 1u;
    IpsecDb.protocore_ipsec_sad_find(ipsec_db_work);
    TEST_ASSERT_NULL(IpsecDb.ptr);
    IpsecDb.protocore_ipsec_sad_find_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_find_args.spi = 2u;
    IpsecDb.protocore_ipsec_sad_find(ipsec_db_work);
    TEST_ASSERT_NOT_NULL(IpsecDb.ptr);
    IpsecDb.protocore_ipsec_sad_remove_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_remove_args.spi = 1u;
    IpsecDb.protocore_ipsec_sad_remove(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok); // already gone
    IpsecDb.protocore_ipsec_sad_remove_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_remove_args.spi = 99u;
    IpsecDb.protocore_ipsec_sad_remove(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);

    // The SPI is free again after the delete.
    IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 1u;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_TRUE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    TEST_ASSERT_NOT_NULL(IpsecDb.ptr);
    IpsecDb.protocore_ipsec_sad_find_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_find_args.spi = 1u;
    IpsecDb.protocore_ipsec_sad_find(ipsec_db_work);
    TEST_ASSERT_NOT_NULL(IpsecDb.ptr);
}

// The SAD is bounded storage and refuses the entry past its capacity.
void test_sad_is_bounded(void)
{
    uint8_t dst[4];
    v4(dst, 10, 0, 0, 1);
    IpsecSad sad;
    IpsecDb.protocore_ipsec_sad_init_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_init(ipsec_db_work);
    for (unsigned i = 0; i < PROTOCORE_IPSEC_SAD_MAX; i++)
    {
        IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
        IpsecDb.protocore_ipsec_sad_add_args.spi = (uint32_t)(i + 1);
        IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
        IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
        IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
        IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
        IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_FALSE;
        IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
        TEST_ASSERT_NOT_NULL(IpsecDb.ptr);
    }
    IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 0xFFFFu;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_FALSE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    TEST_ASSERT_NULL(IpsecDb.ptr);
    // Every SPI installed is still reachable.
    for (unsigned i = 0; i < PROTOCORE_IPSEC_SAD_MAX; i++)
    {
        IpsecDb.protocore_ipsec_sad_find_args.sad = &sad;
        IpsecDb.protocore_ipsec_sad_find_args.spi = (uint32_t)(i + 1);
        IpsecDb.protocore_ipsec_sad_find(ipsec_db_work);
        TEST_ASSERT_NOT_NULL(IpsecDb.ptr);
    }
}

// RFC 4303 sec 3.3.3: "The sender's counter is initialized to 0 when an SA is established ... Thus,
// the first packet sent using a given SA will contain a sequence number of 1", and the sender MUST
// NOT send a packet that would cause the counter to cycle.
void test_rfc4303_outbound_sequence_starts_at_one_and_never_cycles(void)
{
    uint8_t dst[4];
    v4(dst, 10, 0, 0, 1);
    IpsecSad sad;
    IpsecDb.protocore_ipsec_sad_init_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_init(ipsec_db_work);
    IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 7u;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_FALSE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    IpsecSaEntry *sa = IpsecDb.ptr;
    TEST_ASSERT_NOT_NULL(sa);

    uint32_t seq = 0xFFFFFFFFu;
    IpsecDb.protocore_ipsec_sad_next_seq_args.sa = sa;
    IpsecDb.protocore_ipsec_sad_next_seq_args.seq_out = &seq;
    IpsecDb.protocore_ipsec_sad_next_seq(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    TEST_ASSERT_EQUAL_HEX32(1u, seq);
    for (uint32_t expect = 2; expect <= 100; expect++)
    {
        IpsecDb.protocore_ipsec_sad_next_seq_args.sa = sa;
        IpsecDb.protocore_ipsec_sad_next_seq_args.seq_out = &seq;
        IpsecDb.protocore_ipsec_sad_next_seq(ipsec_db_work);
        TEST_ASSERT_TRUE(IpsecDb.ok);
        TEST_ASSERT_EQUAL_HEX32(expect, seq);
    }

    // One short of the wrap issues the last legal number; the next call refuses and the counter is
    // left where it was, so a repeated sequence number can never reach the wire.
    sa->seq = 0xFFFFFFFEu;
    IpsecDb.protocore_ipsec_sad_next_seq_args.sa = sa;
    IpsecDb.protocore_ipsec_sad_next_seq_args.seq_out = &seq;
    IpsecDb.protocore_ipsec_sad_next_seq(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, seq);
    seq = 0x5A5A5A5Au;
    IpsecDb.protocore_ipsec_sad_next_seq_args.sa = sa;
    IpsecDb.protocore_ipsec_sad_next_seq_args.seq_out = &seq;
    IpsecDb.protocore_ipsec_sad_next_seq(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    TEST_ASSERT_EQUAL_HEX32(0x5A5A5A5Au, seq); // untouched
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, sa->seq);
    IpsecDb.protocore_ipsec_sad_next_seq_args.sa = sa;
    IpsecDb.protocore_ipsec_sad_next_seq_args.seq_out = &seq;
    IpsecDb.protocore_ipsec_sad_next_seq(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok); // still refused
}

// Two SAs count independently: the sequence number belongs to the SA, not the database.
void test_sequence_numbers_are_per_sa(void)
{
    uint8_t dst[4];
    v4(dst, 10, 0, 0, 1);
    IpsecSad sad;
    IpsecDb.protocore_ipsec_sad_init_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_init(ipsec_db_work);
    IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 1u;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_FALSE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    IpsecSaEntry *a = IpsecDb.ptr;
    IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 2u;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_FALSE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    IpsecSaEntry *b = IpsecDb.ptr;

    uint32_t sa_seq = 0;
    uint32_t sb_seq = 0;
    for (uint32_t i = 1; i <= 5; i++)
    {
        IpsecDb.protocore_ipsec_sad_next_seq_args.sa = a;
        IpsecDb.protocore_ipsec_sad_next_seq_args.seq_out = &sa_seq;
        IpsecDb.protocore_ipsec_sad_next_seq(ipsec_db_work);
        TEST_ASSERT_TRUE(IpsecDb.ok);
        TEST_ASSERT_EQUAL_HEX32(i, sa_seq);
    }
    IpsecDb.protocore_ipsec_sad_next_seq_args.sa = b;
    IpsecDb.protocore_ipsec_sad_next_seq_args.seq_out = &sb_seq;
    IpsecDb.protocore_ipsec_sad_next_seq(ipsec_db_work);
    TEST_ASSERT_TRUE(IpsecDb.ok);
    TEST_ASSERT_EQUAL_HEX32(1u, sb_seq);
    TEST_ASSERT_EQUAL_HEX32(5u, a->seq);
}

// An inbound SA carries its own anti-replay window, which the ESP receive path drives.
void test_inbound_sa_carries_its_replay_window(void)
{
    uint8_t dst[4];
    v4(dst, 10, 0, 0, 1);
    IpsecSad sad;
    IpsecDb.protocore_ipsec_sad_init_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_init(ipsec_db_work);
    IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 3u;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_TRUE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    IpsecSaEntry *sa = IpsecDb.ptr;
    TEST_ASSERT_NOT_NULL(sa);

    Esp.replay_check_args.r = &sa->replay;
    Esp.replay_check_args.seq = 1u;
    Esp.replay_check(esp_work);
    TEST_ASSERT_TRUE(Esp.ok);
    Esp.replay_check_args.r = &sa->replay;
    Esp.replay_check_args.seq = 1u;
    Esp.replay_check(esp_work);
    TEST_ASSERT_FALSE(Esp.ok);
    Esp.replay_check_args.r = &sa->replay;
    Esp.replay_check_args.seq = 2u;
    Esp.replay_check(esp_work);
    TEST_ASSERT_TRUE(Esp.ok);

    // A second inbound SA's window is independent of the first's.
    IpsecDb.protocore_ipsec_sad_add_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 4u;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_TRUE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    IpsecSaEntry *other = IpsecDb.ptr;
    TEST_ASSERT_NOT_NULL(other);
    Esp.replay_check_args.r = &other->replay;
    Esp.replay_check_args.seq = 1u;
    Esp.replay_check(esp_work);
    TEST_ASSERT_TRUE(Esp.ok);
}

// Null arguments are reported rather than followed.
void test_null_arguments_are_refused(void)
{
    uint8_t lo[4];
    v4(lo, 0, 0, 0, 0);
    IpsecSelector s;
    sel_v4(&s, lo, lo, lo, lo, 0);
    IpsecSpd spd;
    IpsecDb.protocore_ipsec_spd_init_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_init(ipsec_db_work);
    IpsecSad sad;
    IpsecDb.protocore_ipsec_sad_init_args.sad = &sad;
    IpsecDb.protocore_ipsec_sad_init(ipsec_db_work);

    IpsecDb.protocore_ipsec_spd_add_args.spd = NULL;
    IpsecDb.protocore_ipsec_spd_add_args.sel = &s;
    IpsecDb.protocore_ipsec_spd_add_args.action = IPSEC_ACTION_BYPASS;
    IpsecDb.protocore_ipsec_spd_add_args.sa_spi = 0;
    IpsecDb.protocore_ipsec_spd_add(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    IpsecDb.protocore_ipsec_spd_add_args.spd = &spd;
    IpsecDb.protocore_ipsec_spd_add_args.sel = NULL;
    IpsecDb.protocore_ipsec_spd_add_args.action = IPSEC_ACTION_BYPASS;
    IpsecDb.protocore_ipsec_spd_add_args.sa_spi = 0;
    IpsecDb.protocore_ipsec_spd_add(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    IpsecDb.protocore_ipsec_spd_lookup_args.spd = NULL;
    IpsecDb.protocore_ipsec_spd_lookup_args.flow = NULL;
    IpsecDb.protocore_ipsec_spd_lookup(ipsec_db_work);
    TEST_ASSERT_NULL(IpsecDb.ptr);
    IpsecDb.protocore_ipsec_selector_match_args.sel = NULL;
    IpsecDb.protocore_ipsec_selector_match_args.flow = NULL;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = NULL;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);

    uint8_t dst[4];
    v4(dst, 10, 0, 0, 1);
    IpsecDb.protocore_ipsec_sad_add_args.sad = NULL;
    IpsecDb.protocore_ipsec_sad_add_args.spi = 1u;
    IpsecDb.protocore_ipsec_sad_add_args.dst = dst;
    IpsecDb.protocore_ipsec_sad_add_args.addr_len = 4;
    IpsecDb.protocore_ipsec_sad_add_args.key = SA_KEY;
    IpsecDb.protocore_ipsec_sad_add_args.salt = SA_SALT;
    IpsecDb.protocore_ipsec_sad_add_args.inbound = PROTO_FALSE;
    IpsecDb.protocore_ipsec_sad_add(ipsec_db_work);
    TEST_ASSERT_NULL(IpsecDb.ptr);
    IpsecDb.protocore_ipsec_sad_find_args.sad = NULL;
    IpsecDb.protocore_ipsec_sad_find_args.spi = 1u;
    IpsecDb.protocore_ipsec_sad_find(ipsec_db_work);
    TEST_ASSERT_NULL(IpsecDb.ptr);
    IpsecDb.protocore_ipsec_sad_remove_args.sad = NULL;
    IpsecDb.protocore_ipsec_sad_remove_args.spi = 1u;
    IpsecDb.protocore_ipsec_sad_remove(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
    uint32_t seq = 0;
    IpsecDb.protocore_ipsec_sad_next_seq_args.sa = NULL;
    IpsecDb.protocore_ipsec_sad_next_seq_args.seq_out = &seq;
    IpsecDb.protocore_ipsec_sad_next_seq(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);

    // A flow with no address octets cannot be matched against anything.
    IpsecFlow f = {4, 6, NULL, NULL, 0, 0};
    IpsecDb.protocore_ipsec_selector_match_args.sel = &s;
    IpsecDb.protocore_ipsec_selector_match_args.flow = &f;
    IpsecDb.protocore_ipsec_selector_match(ipsec_db_work);
    TEST_ASSERT_FALSE(IpsecDb.ok);
}
