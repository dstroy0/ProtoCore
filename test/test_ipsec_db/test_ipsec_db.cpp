// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the IPsec SPD + SAD (RFC 4301), services/system/esp/ipsec_db: ordered first-match policy
// lookup, selector range matching, TSi/TSr bridging, and SPI-keyed SA management.

#include "services/system/esp/ipsec_db.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// 192.168.1.0/24-ish ranges as big-endian octets.
static const uint8_t src_lo[4] = {192, 168, 1, 0};
static const uint8_t src_hi[4] = {192, 168, 1, 255};
static const uint8_t dst_lo[4] = {10, 0, 0, 0};
static const uint8_t dst_hi[4] = {10, 0, 0, 255};
static const uint8_t esp_key[PC_ESP_KEY_LEN] = {0x11};
static const uint8_t esp_salt[PC_ESP_SALT_LEN] = {0xde, 0xad, 0xbe, 0xef};

static IpsecSelector make_sel(uint8_t proto, uint16_t dport_lo, uint16_t dport_hi)
{
    IpsecSelector s;
    memset(&s, 0, sizeof(s));
    s.addr_len = 4;
    s.ip_protocol = proto;
    memcpy(s.src_lo, src_lo, 4);
    memcpy(s.src_hi, src_hi, 4);
    memcpy(s.dst_lo, dst_lo, 4);
    memcpy(s.dst_hi, dst_hi, 4);
    s.src_port_lo = 0;
    s.src_port_hi = 65535;
    s.dst_port_lo = dport_lo;
    s.dst_port_hi = dport_hi;
    return s;
}

static IpsecFlow make_flow(const uint8_t *src, const uint8_t *dst, uint8_t proto, uint16_t sport, uint16_t dport)
{
    IpsecFlow f;
    f.addr_len = 4;
    f.ip_protocol = proto;
    f.src = src;
    f.dst = dst;
    f.src_port = sport;
    f.dst_port = dport;
    return f;
}

void test_selector_match_basics()
{
    IpsecSelector sel = make_sel(6 /*TCP*/, 443, 443);
    const uint8_t s[4] = {192, 168, 1, 10};
    const uint8_t d[4] = {10, 0, 0, 5};
    IpsecFlow in = make_flow(s, d, 6, 51000, 443);
    TEST_ASSERT_TRUE(pc_ipsec_selector_match(&sel, &in));

    // Wrong protocol, wrong port, and out-of-range addresses each miss.
    IpsecFlow wrong_proto = make_flow(s, d, 17 /*UDP*/, 51000, 443);
    TEST_ASSERT_FALSE(pc_ipsec_selector_match(&sel, &wrong_proto));
    IpsecFlow wrong_port = make_flow(s, d, 6, 51000, 80);
    TEST_ASSERT_FALSE(pc_ipsec_selector_match(&sel, &wrong_port));
    const uint8_t s_out[4] = {192, 168, 2, 10}; // outside src range
    IpsecFlow src_out = make_flow(s_out, d, 6, 51000, 443);
    TEST_ASSERT_FALSE(pc_ipsec_selector_match(&sel, &src_out));
    const uint8_t d_out[4] = {10, 0, 1, 5}; // outside dst range
    IpsecFlow dst_out = make_flow(s, d_out, 6, 51000, 443);
    TEST_ASSERT_FALSE(pc_ipsec_selector_match(&sel, &dst_out));
}

void test_selector_range_edges_and_any()
{
    IpsecSelector any = make_sel(0 /*any proto*/, 0, 65535);
    // Both address range edges are inclusive.
    const uint8_t s_lo[4] = {192, 168, 1, 0};
    const uint8_t s_hi[4] = {192, 168, 1, 255};
    const uint8_t d[4] = {10, 0, 0, 128};
    IpsecFlow lo_edge = make_flow(s_lo, d, 17, 1000, 53);
    IpsecFlow hi_edge = make_flow(s_hi, d, 1, 0, 0);
    TEST_ASSERT_TRUE(pc_ipsec_selector_match(&any, &lo_edge));
    TEST_ASSERT_TRUE(pc_ipsec_selector_match(&any, &hi_edge)); // any-proto matches ICMP too
    // One below the low edge misses.
    const uint8_t s_below[4] = {192, 168, 0, 255};
    IpsecFlow below = make_flow(s_below, d, 17, 1000, 53);
    TEST_ASSERT_FALSE(pc_ipsec_selector_match(&any, &below));

    // Family mismatch (v6 flow vs v4 selector) never matches.
    IpsecFlow v6 = make_flow(s_lo, d, 17, 1000, 53);
    v6.addr_len = 16;
    TEST_ASSERT_FALSE(pc_ipsec_selector_match(&any, &v6));
    // Null guards.
    TEST_ASSERT_FALSE(pc_ipsec_selector_match(nullptr, &lo_edge));
    TEST_ASSERT_FALSE(pc_ipsec_selector_match(&any, nullptr));
}

void test_spd_first_match_wins()
{
    // Policy order: a specific PROTECT for TCP/443, then a broad BYPASS. First match wins.
    IpsecSpd spd;
    pc_ipsec_spd_init(&spd);
    IpsecSelector https = make_sel(6, 443, 443);
    IpsecSelector all = make_sel(0, 0, 65535);
    TEST_ASSERT_TRUE(pc_ipsec_spd_add(&spd, &https, IpsecAction::PROTECT, 0xABCD));
    TEST_ASSERT_TRUE(pc_ipsec_spd_add(&spd, &all, IpsecAction::BYPASS, 0));

    const uint8_t s[4] = {192, 168, 1, 10};
    const uint8_t d[4] = {10, 0, 0, 5};
    IpsecFlow https_flow = make_flow(s, d, 6, 51000, 443);
    const IpsecPolicy *p = pc_ipsec_spd_lookup(&spd, &https_flow);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL(IpsecAction::PROTECT, p->action);
    TEST_ASSERT_EQUAL_HEX32(0xABCD, p->sa_spi);

    // A non-443 flow falls through to the BYPASS.
    IpsecFlow http_flow = make_flow(s, d, 6, 51000, 80);
    p = pc_ipsec_spd_lookup(&spd, &http_flow);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL(IpsecAction::BYPASS, p->action);

    // A flow outside all selectors gets no policy (default deny).
    const uint8_t s_far[4] = {8, 8, 8, 8};
    IpsecFlow far = make_flow(s_far, d, 6, 51000, 443);
    TEST_ASSERT_NULL(pc_ipsec_spd_lookup(&spd, &far));
}

void test_spd_order_matters_and_bounds()
{
    // Reversed order: the broad BYPASS first shadows the specific PROTECT.
    IpsecSpd spd;
    pc_ipsec_spd_init(&spd);
    IpsecSelector https = make_sel(6, 443, 443);
    IpsecSelector all = make_sel(0, 0, 65535);
    pc_ipsec_spd_add(&spd, &all, IpsecAction::BYPASS, 0);
    pc_ipsec_spd_add(&spd, &https, IpsecAction::PROTECT, 0xABCD);
    const uint8_t s[4] = {192, 168, 1, 10};
    const uint8_t d[4] = {10, 0, 0, 5};
    IpsecFlow f = make_flow(s, d, 6, 51000, 443);
    const IpsecPolicy *p = pc_ipsec_spd_lookup(&spd, &f);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL(IpsecAction::BYPASS, p->action); // the broad rule won

    // The SPD fills at PC_IPSEC_SPD_MAX and then refuses more.
    pc_ipsec_spd_init(&spd);
    for (int i = 0; i < PC_IPSEC_SPD_MAX; i++)
    {
        TEST_ASSERT_TRUE(pc_ipsec_spd_add(&spd, &all, IpsecAction::DISCARD, 0));
    }
    TEST_ASSERT_FALSE(pc_ipsec_spd_add(&spd, &all, IpsecAction::DISCARD, 0));
    // A non-PROTECT action does not carry an SPI.
    TEST_ASSERT_EQUAL_HEX32(0, spd.entries[0].sa_spi);
}

void test_selector_from_ts()
{
    // Build an SPD selector from an IKEv2 TSi (local) / TSr (peer) pair, then match a flow through it.
    const uint8_t tsi_lo[4] = {192, 168, 1, 0}, tsi_hi[4] = {192, 168, 1, 255};
    const uint8_t tsr_lo[4] = {10, 0, 0, 0}, tsr_hi[4] = {10, 0, 0, 255};
    IkeTrafficSelector tsi;
    tsi.ts_type = IkeTsType::IKE_TS_IPV4_ADDR_RANGE;
    tsi.ip_protocol = 6;
    tsi.start_port = 0;
    tsi.end_port = 65535;
    tsi.start_addr = tsi_lo;
    tsi.end_addr = tsi_hi;
    tsi.addr_len = 4;
    IkeTrafficSelector tsr = tsi;
    tsr.start_port = 443;
    tsr.end_port = 443;
    tsr.start_addr = tsr_lo;
    tsr.end_addr = tsr_hi;

    IpsecSelector sel;
    TEST_ASSERT_TRUE(pc_ipsec_selector_from_ts(&sel, &tsi, &tsr));
    TEST_ASSERT_EQUAL_UINT8(4, sel.addr_len);
    TEST_ASSERT_EQUAL_UINT8(6, sel.ip_protocol);
    TEST_ASSERT_EQUAL_MEMORY(tsi_lo, sel.src_lo, 4);
    TEST_ASSERT_EQUAL_MEMORY(tsr_hi, sel.dst_hi, 4);
    TEST_ASSERT_EQUAL_UINT16(443, sel.dst_port_lo);

    const uint8_t s[4] = {192, 168, 1, 42}, d[4] = {10, 0, 0, 7};
    IpsecFlow f = make_flow(s, d, 6, 51000, 443);
    TEST_ASSERT_TRUE(pc_ipsec_selector_match(&sel, &f));

    // Mismatched families and a null are refused.
    IkeTrafficSelector tsr_v6 = tsr;
    tsr_v6.ts_type = IkeTsType::IKE_TS_IPV6_ADDR_RANGE;
    tsr_v6.addr_len = 16;
    TEST_ASSERT_FALSE(pc_ipsec_selector_from_ts(&sel, &tsi, &tsr_v6));
    TEST_ASSERT_FALSE(pc_ipsec_selector_from_ts(&sel, nullptr, &tsr));
}

void test_sad_add_find_remove()
{
    IpsecSad sad;
    pc_ipsec_sad_init(&sad);
    const uint8_t dst[4] = {10, 0, 0, 5};

    IpsecSaEntry *out_sa = pc_ipsec_sad_add(&sad, 0x1000, dst, 4, esp_key, esp_salt, /*inbound=*/false);
    TEST_ASSERT_NOT_NULL(out_sa);
    IpsecSaEntry *in_sa = pc_ipsec_sad_add(&sad, 0x2000, dst, 4, esp_key, esp_salt, /*inbound=*/true);
    TEST_ASSERT_NOT_NULL(in_sa);
    TEST_ASSERT_EQUAL_size_t(2, sad.count);

    // Demux by SPI returns the right SA; an unknown SPI is null.
    TEST_ASSERT_EQUAL_PTR(out_sa, pc_ipsec_sad_find(&sad, 0x1000));
    TEST_ASSERT_EQUAL_PTR(in_sa, pc_ipsec_sad_find(&sad, 0x2000));
    TEST_ASSERT_NULL(pc_ipsec_sad_find(&sad, 0x9999));

    // A duplicate SPI is rejected.
    TEST_ASSERT_NULL(pc_ipsec_sad_add(&sad, 0x1000, dst, 4, esp_key, esp_salt, false));

    // The inbound SA's replay window is live and rejects a replay.
    TEST_ASSERT_TRUE(pc_esp_replay_check(&in_sa->replay, 1));
    TEST_ASSERT_FALSE(pc_esp_replay_check(&in_sa->replay, 1));

    // Remove frees the slot and wipes it; the SPI is gone.
    TEST_ASSERT_TRUE(pc_ipsec_sad_remove(&sad, 0x1000));
    TEST_ASSERT_NULL(pc_ipsec_sad_find(&sad, 0x1000));
    TEST_ASSERT_EQUAL_size_t(1, sad.count);
    TEST_ASSERT_FALSE(pc_ipsec_sad_remove(&sad, 0x1000)); // already gone
}

void test_sad_full_and_seq()
{
    IpsecSad sad;
    pc_ipsec_sad_init(&sad);
    const uint8_t dst[4] = {10, 0, 0, 5};
    for (int i = 0; i < PC_IPSEC_SAD_MAX; i++)
    {
        TEST_ASSERT_NOT_NULL(pc_ipsec_sad_add(&sad, 0x100 + i, dst, 4, esp_key, esp_salt, false));
    }
    TEST_ASSERT_NULL(pc_ipsec_sad_add(&sad, 0x999, dst, 4, esp_key, esp_salt, false)); // full

    IpsecSaEntry *sa = pc_ipsec_sad_find(&sad, 0x100);
    // Outbound sequence numbers pre-increment from 1 and stay monotonic.
    uint32_t seq = 0;
    TEST_ASSERT_TRUE(pc_ipsec_sad_next_seq(sa, &seq));
    TEST_ASSERT_EQUAL_UINT32(1, seq);
    TEST_ASSERT_TRUE(pc_ipsec_sad_next_seq(sa, &seq));
    TEST_ASSERT_EQUAL_UINT32(2, seq);

    // At the 32-bit ceiling the allocation fails (the SA must be rekeyed - a repeat would break GCM).
    sa->seq = 0xFFFFFFFFu;
    TEST_ASSERT_FALSE(pc_ipsec_sad_next_seq(sa, &seq));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, sa->seq); // left unchanged
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_selector_match_basics);
    RUN_TEST(test_selector_range_edges_and_any);
    RUN_TEST(test_spd_first_match_wins);
    RUN_TEST(test_spd_order_matters_and_bounds);
    RUN_TEST(test_selector_from_ts);
    RUN_TEST(test_sad_add_find_remove);
    RUN_TEST(test_sad_full_and_seq);
    return UNITY_END();
}
