// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
    protocore_ipsec_spd_init(&spd);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)spd.count);

    IpsecSelector host;
    IpsecSelector net;
    IpsecSelector any;
    sel_v4(&host, host_lo, host_hi, any_lo, any_hi, 0);
    sel_v4(&net, net_lo, net_hi, any_lo, any_hi, 0);
    sel_v4(&any, any_lo, any_hi, any_lo, any_hi, 0);

    TEST_ASSERT_TRUE(protocore_ipsec_spd_add(&spd, &host, IPSEC_ACTION_PROTECT, 0xAAAA1111u));
    TEST_ASSERT_TRUE(protocore_ipsec_spd_add(&spd, &net, IPSEC_ACTION_BYPASS, 0));
    TEST_ASSERT_TRUE(protocore_ipsec_spd_add(&spd, &any, IPSEC_ACTION_DISCARD, 0));
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)spd.count);

    uint8_t src[4];
    uint8_t dst[4];
    v4(dst, 192, 168, 1, 1);
    IpsecFlow flow = {4, 6, src, dst, 1024, 443};

    v4(src, 10, 0, 0, 5); // matches all three: the first wins
    const IpsecPolicy *p = protocore_ipsec_spd_lookup(&spd, &flow);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(IPSEC_ACTION_PROTECT, (int)p->action);
    TEST_ASSERT_EQUAL_HEX32(0xAAAA1111u, p->sa_spi);

    v4(src, 10, 0, 0, 6); // matches the /24 and the catch-all
    p = protocore_ipsec_spd_lookup(&spd, &flow);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(IPSEC_ACTION_BYPASS, (int)p->action);

    v4(src, 172, 16, 0, 1); // only the catch-all
    p = protocore_ipsec_spd_lookup(&spd, &flow);
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
    protocore_ipsec_spd_init(&spd);
    IpsecSelector s;
    sel_v4(&s, lo, hi, lo, hi, 0);
    TEST_ASSERT_TRUE(protocore_ipsec_spd_add(&spd, &s, IPSEC_ACTION_PROTECT, 1));

    uint8_t src[4];
    uint8_t dst[4];
    v4(src, 10, 0, 1, 1); // outside the /24
    v4(dst, 10, 0, 0, 1);
    IpsecFlow flow = {4, 6, src, dst, 1, 1};
    TEST_ASSERT_NULL(protocore_ipsec_spd_lookup(&spd, &flow));

    // An empty SPD matches nothing at all.
    IpsecSpd empty;
    protocore_ipsec_spd_init(&empty);
    v4(src, 10, 0, 0, 1);
    TEST_ASSERT_NULL(protocore_ipsec_spd_lookup(&empty, &flow));
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
    TEST_ASSERT_TRUE(protocore_ipsec_selector_match(&s, &f));

    v4(src, 10, 0, 0, 10); // the low endpoint
    TEST_ASSERT_TRUE(protocore_ipsec_selector_match(&s, &f));
    v4(src, 10, 0, 0, 20); // the high endpoint
    TEST_ASSERT_TRUE(protocore_ipsec_selector_match(&s, &f));
    v4(src, 10, 0, 0, 9); // one below
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f));
    v4(src, 10, 0, 0, 21); // one above
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f));

    v4(src, 10, 0, 0, 15);
    f.src_port = 1000;
    TEST_ASSERT_TRUE(protocore_ipsec_selector_match(&s, &f));
    f.src_port = 2000;
    TEST_ASSERT_TRUE(protocore_ipsec_selector_match(&s, &f));
    f.src_port = 999;
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f));
    f.src_port = 2001;
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f));

    f.src_port = 1500;
    f.dst_port = 444;
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f));
    f.dst_port = 443;

    f.ip_protocol = 17; // UDP against a TCP-only selector
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f));
    s.ip_protocol = 0; // "any" matches every protocol
    TEST_ASSERT_TRUE(protocore_ipsec_selector_match(&s, &f));
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
    TEST_ASSERT_TRUE(protocore_ipsec_selector_match(&s, &f));
    v4(src, 10, 0, 2, 1); // outside: above the high endpoint
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f));
    v4(src, 10, 0, 0, 255); // outside: below the low endpoint
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f));

    // An IPv6-length flow cannot match an IPv4 selector.
    v4(src, 10, 0, 1, 1);
    uint8_t src6[16] = {0};
    uint8_t dst6[16] = {0};
    IpsecFlow f6 = {16, 6, src6, dst6, 1, 1};
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f6));

    // A selector whose address length is neither 4 nor 16 names no family.
    IpsecSelector bad = s;
    bad.addr_len = 8;
    IpsecFlow f8 = {8, 6, src, dst, 1, 1};
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&bad, &f8));
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
    TEST_ASSERT_TRUE(protocore_ipsec_selector_match(&s, &f));
    memcpy(src, HI, 16);
    TEST_ASSERT_TRUE(protocore_ipsec_selector_match(&s, &f));
    memcpy(src, HI, 16);
    src[13] = 0x01; // 2001:db8::1:ffff, past the high endpoint
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f));
    memcpy(src, LO, 16);
    src[3] = 0xb7; // 2001:db7::, below the low endpoint
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f));
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
    protocore_ipsec_spd_init(&spd);
    for (unsigned i = 0; i < PROTOCORE_IPSEC_SPD_MAX; i++)
    {
        TEST_ASSERT_TRUE(protocore_ipsec_spd_add(&spd, &s, IPSEC_ACTION_BYPASS, 0));
    }
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_IPSEC_SPD_MAX, (uint32_t)spd.count);
    TEST_ASSERT_FALSE(protocore_ipsec_spd_add(&spd, &s, IPSEC_ACTION_BYPASS, 0));
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
    TEST_ASSERT_TRUE(protocore_ipsec_selector_from_ts(&s, &tsi, &tsr));
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
    TEST_ASSERT_TRUE(protocore_ipsec_selector_match(&s, &f));
    f.dst_port = 80;
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f));

    // Both selectors must name the same address family.
    tsr.addr_len = 16;
    TEST_ASSERT_FALSE(protocore_ipsec_selector_from_ts(&s, &tsi, &tsr));
    tsr.addr_len = 4;
    TEST_ASSERT_FALSE(protocore_ipsec_selector_from_ts(NULL, &tsi, &tsr));
    TEST_ASSERT_FALSE(protocore_ipsec_selector_from_ts(&s, NULL, &tsr));
    TEST_ASSERT_FALSE(protocore_ipsec_selector_from_ts(&s, &tsi, NULL));
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
    protocore_ipsec_sad_init(&sad);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)sad.count);
    TEST_ASSERT_NULL(protocore_ipsec_sad_find(&sad, 0x1000u));

    IpsecSaEntry *out = protocore_ipsec_sad_add(&sad, 0x1000u, dst, 4, SA_KEY, SA_SALT, PROTO_FALSE);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_HEX32(0x1000u, out->spi);
    TEST_ASSERT_FALSE(out->inbound);
    TEST_ASSERT_TRUE(out->valid);
    TEST_ASSERT_EQUAL_UINT32(0, out->seq); // no packet issued yet
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SA_KEY, out->key, PROTOCORE_ESP_KEY_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SA_SALT, out->salt, PROTOCORE_ESP_SALT_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(dst, out->dst, 4);

    IpsecSaEntry *in = protocore_ipsec_sad_add(&sad, 0x2000u, dst, 4, SA_KEY, SA_SALT, PROTO_TRUE);
    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_TRUE(in->inbound);
    TEST_ASSERT_FALSE(in->replay.seen_any); // the anti-replay window starts empty

    TEST_ASSERT_EQUAL_PTR(out, protocore_ipsec_sad_find(&sad, 0x1000u));
    TEST_ASSERT_EQUAL_PTR(in, protocore_ipsec_sad_find(&sad, 0x2000u));
    TEST_ASSERT_NULL(protocore_ipsec_sad_find(&sad, 0x3000u));

    // A duplicate SPI is refused: two SAs answering one demux key would be ambiguous.
    TEST_ASSERT_NULL(protocore_ipsec_sad_add(&sad, 0x1000u, dst, 4, SA_KEY, SA_SALT, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)sad.count);
}

// An IKE DELETE removes an SA; its SPI then names nothing, and the slot can be reused.
void test_sad_remove(void)
{
    uint8_t dst[4];
    v4(dst, 10, 0, 0, 1);
    IpsecSad sad;
    protocore_ipsec_sad_init(&sad);
    TEST_ASSERT_NOT_NULL(protocore_ipsec_sad_add(&sad, 1u, dst, 4, SA_KEY, SA_SALT, PROTO_FALSE));
    TEST_ASSERT_NOT_NULL(protocore_ipsec_sad_add(&sad, 2u, dst, 4, SA_KEY, SA_SALT, PROTO_TRUE));

    TEST_ASSERT_TRUE(protocore_ipsec_sad_remove(&sad, 1u));
    TEST_ASSERT_NULL(protocore_ipsec_sad_find(&sad, 1u));
    TEST_ASSERT_NOT_NULL(protocore_ipsec_sad_find(&sad, 2u));
    TEST_ASSERT_FALSE(protocore_ipsec_sad_remove(&sad, 1u)); // already gone
    TEST_ASSERT_FALSE(protocore_ipsec_sad_remove(&sad, 99u));

    // The SPI is free again after the delete.
    TEST_ASSERT_NOT_NULL(protocore_ipsec_sad_add(&sad, 1u, dst, 4, SA_KEY, SA_SALT, PROTO_TRUE));
    TEST_ASSERT_NOT_NULL(protocore_ipsec_sad_find(&sad, 1u));
}

// The SAD is bounded storage and refuses the entry past its capacity.
void test_sad_is_bounded(void)
{
    uint8_t dst[4];
    v4(dst, 10, 0, 0, 1);
    IpsecSad sad;
    protocore_ipsec_sad_init(&sad);
    for (unsigned i = 0; i < PROTOCORE_IPSEC_SAD_MAX; i++)
    {
        TEST_ASSERT_NOT_NULL(protocore_ipsec_sad_add(&sad, (uint32_t)(i + 1), dst, 4, SA_KEY, SA_SALT, PROTO_FALSE));
    }
    TEST_ASSERT_NULL(protocore_ipsec_sad_add(&sad, 0xFFFFu, dst, 4, SA_KEY, SA_SALT, PROTO_FALSE));
    // Every SPI installed is still reachable.
    for (unsigned i = 0; i < PROTOCORE_IPSEC_SAD_MAX; i++)
    {
        TEST_ASSERT_NOT_NULL(protocore_ipsec_sad_find(&sad, (uint32_t)(i + 1)));
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
    protocore_ipsec_sad_init(&sad);
    IpsecSaEntry *sa = protocore_ipsec_sad_add(&sad, 7u, dst, 4, SA_KEY, SA_SALT, PROTO_FALSE);
    TEST_ASSERT_NOT_NULL(sa);

    uint32_t seq = 0xFFFFFFFFu;
    TEST_ASSERT_TRUE(protocore_ipsec_sad_next_seq(sa, &seq));
    TEST_ASSERT_EQUAL_HEX32(1u, seq);
    for (uint32_t expect = 2; expect <= 100; expect++)
    {
        TEST_ASSERT_TRUE(protocore_ipsec_sad_next_seq(sa, &seq));
        TEST_ASSERT_EQUAL_HEX32(expect, seq);
    }

    // One short of the wrap issues the last legal number; the next call refuses and the counter is
    // left where it was, so a repeated sequence number can never reach the wire.
    sa->seq = 0xFFFFFFFEu;
    TEST_ASSERT_TRUE(protocore_ipsec_sad_next_seq(sa, &seq));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, seq);
    seq = 0x5A5A5A5Au;
    TEST_ASSERT_FALSE(protocore_ipsec_sad_next_seq(sa, &seq));
    TEST_ASSERT_EQUAL_HEX32(0x5A5A5A5Au, seq); // untouched
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, sa->seq);
    TEST_ASSERT_FALSE(protocore_ipsec_sad_next_seq(sa, &seq)); // still refused
}

// Two SAs count independently: the sequence number belongs to the SA, not the database.
void test_sequence_numbers_are_per_sa(void)
{
    uint8_t dst[4];
    v4(dst, 10, 0, 0, 1);
    IpsecSad sad;
    protocore_ipsec_sad_init(&sad);
    IpsecSaEntry *a = protocore_ipsec_sad_add(&sad, 1u, dst, 4, SA_KEY, SA_SALT, PROTO_FALSE);
    IpsecSaEntry *b = protocore_ipsec_sad_add(&sad, 2u, dst, 4, SA_KEY, SA_SALT, PROTO_FALSE);

    uint32_t sa_seq = 0;
    uint32_t sb_seq = 0;
    for (uint32_t i = 1; i <= 5; i++)
    {
        TEST_ASSERT_TRUE(protocore_ipsec_sad_next_seq(a, &sa_seq));
        TEST_ASSERT_EQUAL_HEX32(i, sa_seq);
    }
    TEST_ASSERT_TRUE(protocore_ipsec_sad_next_seq(b, &sb_seq));
    TEST_ASSERT_EQUAL_HEX32(1u, sb_seq);
    TEST_ASSERT_EQUAL_HEX32(5u, a->seq);
}

// An inbound SA carries its own anti-replay window, which the ESP receive path drives.
void test_inbound_sa_carries_its_replay_window(void)
{
    uint8_t dst[4];
    v4(dst, 10, 0, 0, 1);
    IpsecSad sad;
    protocore_ipsec_sad_init(&sad);
    IpsecSaEntry *sa = protocore_ipsec_sad_add(&sad, 3u, dst, 4, SA_KEY, SA_SALT, PROTO_TRUE);
    TEST_ASSERT_NOT_NULL(sa);

    TEST_ASSERT_TRUE(protocore_esp_replay_check(&sa->replay, 1u));
    TEST_ASSERT_FALSE(protocore_esp_replay_check(&sa->replay, 1u));
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&sa->replay, 2u));

    // A second inbound SA's window is independent of the first's.
    IpsecSaEntry *other = protocore_ipsec_sad_add(&sad, 4u, dst, 4, SA_KEY, SA_SALT, PROTO_TRUE);
    TEST_ASSERT_NOT_NULL(other);
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&other->replay, 1u));
}

// Null arguments are reported rather than followed.
void test_null_arguments_are_refused(void)
{
    uint8_t lo[4];
    v4(lo, 0, 0, 0, 0);
    IpsecSelector s;
    sel_v4(&s, lo, lo, lo, lo, 0);
    IpsecSpd spd;
    protocore_ipsec_spd_init(&spd);
    IpsecSad sad;
    protocore_ipsec_sad_init(&sad);

    TEST_ASSERT_FALSE(protocore_ipsec_spd_add(NULL, &s, IPSEC_ACTION_BYPASS, 0));
    TEST_ASSERT_FALSE(protocore_ipsec_spd_add(&spd, NULL, IPSEC_ACTION_BYPASS, 0));
    TEST_ASSERT_NULL(protocore_ipsec_spd_lookup(NULL, NULL));
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(NULL, NULL));
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, NULL));

    uint8_t dst[4];
    v4(dst, 10, 0, 0, 1);
    TEST_ASSERT_NULL(protocore_ipsec_sad_add(NULL, 1u, dst, 4, SA_KEY, SA_SALT, PROTO_FALSE));
    TEST_ASSERT_NULL(protocore_ipsec_sad_find(NULL, 1u));
    TEST_ASSERT_FALSE(protocore_ipsec_sad_remove(NULL, 1u));
    uint32_t seq = 0;
    TEST_ASSERT_FALSE(protocore_ipsec_sad_next_seq(NULL, &seq));

    // A flow with no address octets cannot be matched against anything.
    IpsecFlow f = {4, 6, NULL, NULL, 0, 0};
    TEST_ASSERT_FALSE(protocore_ipsec_selector_match(&s, &f));
}
