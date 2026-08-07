// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Layer 1 driven through a REAL backend: PROTOCORE_HOT_FORCE selects PC_VENDOR_MOCK, so
// PC_PHYSICAL_HAS_BACKEND is 1 and core_setup/physical/mock stands in for silicon. These are the
// same lines a target runs, against a link state that can actually be up - not the no-op stubs.
//
// That is what makes the readouts checkable at all: a stub answers every one of them with an empty
// value, so a caller that mishandles a live link reads identically to one that works. It is also
// what tells same-signature members apart, which no stub build can do - eth->init from eth->ready,
// and link->mac from link->egress_mac, which return DIFFERENT addresses once a wired link is up.
//
// The backend holds one static link state with no reset entry point, so bring-up here is monotonic
// and the cases run in the order main() lists them.

#include "network_drivers/physical/physical.h"
#include "network_drivers/physical/radio_power.h"

#include <string.h>
#include <unity.h>

// The dotted quad as the pc_net_*_ip() contract returns it: network byte order in a u32, which on
// every target in the list puts the first octet in the low byte. Assembled here so the expected
// value is the test's own, not the backend's macro.
#define IP4(a, b, c, d) (((uint32_t)(d) << 24) | ((uint32_t)(c) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

// RFC 5737 TEST-NET-1: the addresses the mock is documented to use.
#define STA_IP IP4(192, 0, 2, 10)
#define AP_IP IP4(192, 0, 2, 1)
#define ETH_IP IP4(192, 0, 2, 20)

void setUp(void)
{
}

void tearDown(void)
{
}

// ---- nothing up -----------------------------------------------------------

// Before any bring-up there is no default route, so egress reports no interface and no address.
// Runs first: the backend's link state is static and only ever comes up.
void test_a_no_link_reports_no_route()
{
    TEST_ASSERT_EQUAL_INT(PC_IF_ANY, Physical.link->egress());
    TEST_ASSERT_EQUAL_UINT32(0, Physical.link->egress_ip());
    TEST_ASSERT_FALSE(Physical.wifi->ready());
    TEST_ASSERT_EQUAL_UINT32(0, Physical.wifi->ap_ip());
    TEST_ASSERT_EQUAL_INT8(0, Physical.wifi->rssi()); // not associated
    TEST_ASSERT_EQUAL_UINT8(0, Physical.wifi->channel());

    // No route means no egress MAC, even though the interface has an address of its own.
    uint8_t mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(Physical.link->egress_mac(mac));
    TEST_ASSERT_EQUAL_HEX8(0xFF, mac[0]); // untouched on refusal
}

// ---- the station ----------------------------------------------------------

// Associating brings the station up and every station readout answers from it.
void test_b_station_bring_up_is_live()
{
    TEST_ASSERT_TRUE(Physical.wifi->init("protocore-net", "passphrase"));
    TEST_ASSERT_TRUE(Physical.wifi->ready()); // ready() reflects the state init() set

    TEST_ASSERT_EQUAL_INT(PC_IF_WIFI_STA, Physical.link->egress());
    TEST_ASSERT_EQUAL_UINT32(STA_IP, Physical.link->egress_ip());

    // RSSI is a signed dBm reading, negative for any real association.
    TEST_ASSERT_TRUE(Physical.wifi->rssi() < 0);

    // IEEE 802.11 channel numbering: an associated station is on a valid channel, never 0.
    uint8_t ch = Physical.wifi->channel();
    TEST_ASSERT_TRUE(ch >= 1u && ch <= 14u);
}

// The SSID reads back exactly what was joined.
void test_c_ssid_reads_back()
{
    char out[64];
    memset(out, 0x7F, sizeof(out));
    size_t n = Physical.wifi->ssid(out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(strlen("protocore-net"), n);
    TEST_ASSERT_EQUAL_STRING("protocore-net", out);
    TEST_ASSERT_EQUAL_CHAR('\0', out[n]); // always terminated
}

// The caller's capacity bounds the copy, and the result is still terminated.
void test_d_ssid_truncates_to_the_callers_cap()
{
    char small[6];
    memset(small, 0x7F, sizeof(small));
    size_t n = Physical.wifi->ssid(small, sizeof(small));
    TEST_ASSERT_EQUAL_size_t(sizeof(small) - 1u, n); // one byte is the terminator
    TEST_ASSERT_EQUAL_CHAR('\0', small[n]);
    TEST_ASSERT_EQUAL_MEMORY("proto", small, 5);

    TEST_ASSERT_EQUAL_size_t(0, Physical.wifi->ssid(NULL, sizeof(small)));
    char untouched[4] = {'y', '\0'};
    TEST_ASSERT_EQUAL_size_t(0, Physical.wifi->ssid(untouched, 0));
    TEST_ASSERT_EQUAL_STRING("y", untouched);
}

// IEEE 802.11-2020 9.4.2.2: an SSID element carries at most 32 octets, so a longer one is cut to
// the standard's limit by the backend and never reported past it.
void test_e_ssid_is_capped_at_the_802_11_limit()
{
    static const char long_ssid[] = "0123456789012345678901234567890123456789"; // 40 octets
    TEST_ASSERT_TRUE(Physical.wifi->init(long_ssid, "passphrase"));

    char out[64];
    size_t n = Physical.wifi->ssid(out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(32, n);
    TEST_ASSERT_EQUAL_MEMORY(long_ssid, out, 32);
    TEST_ASSERT_EQUAL_CHAR('\0', out[32]);

    TEST_ASSERT_TRUE(Physical.wifi->init("protocore-net", "passphrase")); // back to the short one
}

// The station MAC is a locally administered unicast address: IEEE 802 / RFC 7042 put the U/L bit
// in bit 1 of the first octet and the I/G bit in bit 0.
void test_f_station_mac_is_locally_administered_unicast()
{
    uint8_t mac[6] = {0};
    TEST_ASSERT_TRUE(Physical.link->mac(mac));
    TEST_ASSERT_EQUAL_HEX8(0x02, mac[0] & 0x02u); // U/L set: not an IEEE-assigned OUI
    TEST_ASSERT_EQUAL_HEX8(0x00, mac[0] & 0x01u); // I/G clear: unicast, not a group address
    TEST_ASSERT_FALSE(Physical.link->mac(NULL));
}

// ---- the softAP -----------------------------------------------------------

// The softAP comes up alongside the station and reports its own address.
void test_g_softap_has_its_own_address()
{
    TEST_ASSERT_TRUE(Physical.wifi->init_ap("protocore-ap", "ap-passphrase"));
    TEST_ASSERT_EQUAL_UINT32(AP_IP, Physical.wifi->ap_ip());
    // The station still carries the route: a softAP does not become the default egress.
    TEST_ASSERT_EQUAL_INT(PC_IF_WIFI_STA, Physical.link->egress());
    TEST_ASSERT_EQUAL_UINT32(STA_IP, Physical.link->egress_ip());
}

// ---- the wired link -------------------------------------------------------

// Bringing Ethernet up takes the default route from the station, which is the order a stack picks
// a route in when both are up.
void test_h_wired_wins_the_route()
{
    TEST_ASSERT_FALSE(Physical.eth->ready()); // not up yet: ready() is not init()
    TEST_ASSERT_TRUE(Physical.eth->init());
    TEST_ASSERT_TRUE(Physical.eth->ready()); // and now it is

    TEST_ASSERT_EQUAL_INT(PC_IF_ETH, Physical.link->egress());
    TEST_ASSERT_EQUAL_UINT32(ETH_IP, Physical.link->egress_ip());
    TEST_ASSERT_TRUE(Physical.wifi->ready()); // the station is still associated underneath
}

// The two MAC readouts answer differently once a wired link carries the traffic: link->mac is the
// 802.11 station address, link->egress_mac is whichever interface is actually on the wire. No stub
// build can tell these apart - both refuse - which is the whole reason this env exists.
void test_i_egress_mac_tracks_the_route_and_differs_from_the_station_mac()
{
    uint8_t sta[6] = {0};
    uint8_t egress[6] = {0};

    TEST_ASSERT_TRUE(Physical.link->mac(sta));
    TEST_ASSERT_TRUE(Physical.link->egress_mac(egress));

    // Ethernet carries the route, so the egress address is the wired PHY's, not the radio's.
    TEST_ASSERT_TRUE(memcmp(sta, egress, 6) != 0);
    TEST_ASSERT_EQUAL_HEX8(0x02, egress[0] & 0x02u); // still locally administered
    TEST_ASSERT_EQUAL_HEX8(0x00, egress[0] & 0x01u); // still unicast
}

// ---- IPv6 -----------------------------------------------------------------

// Enabling IPv6 yields a global address in the v6 family, and ready() tracks it.
void test_j_ipv6_global_address()
{
    pc_ip addr;
    memset(&addr, 0, sizeof(addr));

    TEST_ASSERT_FALSE(Physical.ip6->ready()); // not enabled yet: ready() is not init()
    TEST_ASSERT_TRUE(Physical.ip6->init());
    TEST_ASSERT_TRUE(Physical.ip6->ready());

    TEST_ASSERT_TRUE(Physical.ip6->global_addr(&addr));
    TEST_ASSERT_EQUAL_INT(PC_IP_V6, addr.family);

    // RFC 3849 reserves 2001:db8::/32 for documentation, which is what the backend answers with.
    TEST_ASSERT_EQUAL_HEX8(0x20, addr.bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, addr.bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x0D, addr.bytes[2]);
    TEST_ASSERT_EQUAL_HEX8(0xB8, addr.bytes[3]);

    // RFC 4291 sec 2.5.6: a link-local address is fe80::/10. A GLOBAL address must not be one.
    TEST_ASSERT_FALSE(addr.bytes[0] == 0xFEu && (addr.bytes[1] & 0xC0u) == 0x80u);

    TEST_ASSERT_FALSE(Physical.ip6->global_addr(NULL));
}

// ---- radio control --------------------------------------------------------

// With a radio under it, a power-save mode set is a mode that reads back.
void test_k_power_save_mode_round_trips()
{
    TEST_ASSERT_TRUE(Physical.radio->ps_set(PC_PHY_PS_MAX_MODEM));
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_PS_MAX_MODEM, Physical.radio->ps_mode());

    TEST_ASSERT_TRUE(Physical.radio->ps_set(PC_PHY_PS_MIN_MODEM));
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_PS_MIN_MODEM, Physical.radio->ps_mode());

    TEST_ASSERT_TRUE(Physical.radio->ps_set(PC_PHY_PS_NONE));
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_PS_NONE, Physical.radio->ps_mode());

    TEST_ASSERT_TRUE(Physical.radio->tx_power_set(11));
    TEST_ASSERT_TRUE(Physical.radio->tx_power_set(-4)); // a cap may be negative dBm
}

// The keep-awake refcount: the first hold forces modem sleep off so a bulk transfer is not
// interrupted, and the matching release restores the configured mode once the count reaches zero.
void test_l_busy_hold_refcount_gates_modem_sleep()
{
    TEST_ASSERT_TRUE(Physical.radio->ps_set(PC_PHY_PS_MAX_MODEM));

    Physical.radio->busy_hold();
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_PS_NONE, Physical.radio->ps_mode()); // sleep off for the transfer

    Physical.radio->busy_hold(); // nested: still held
    Physical.radio->busy_release();
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_PS_NONE, Physical.radio->ps_mode()); // one hold outstanding

    Physical.radio->busy_release(); // count reaches zero: the configured mode comes back
    TEST_ASSERT_EQUAL_UINT8((uint8_t)PC_RADIO_WIFI_PS, Physical.radio->ps_mode());

    // An unbalanced release cannot drive the count negative or change the mode again.
    pc_phy_ps before = Physical.radio->ps_mode();
    Physical.radio->busy_release();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)before, Physical.radio->ps_mode());
}

// power() applies the configured mode directly.
void test_m_power_applies_the_configured_mode()
{
    TEST_ASSERT_TRUE(Physical.radio->ps_set(PC_PHY_PS_MIN_MODEM));
    Physical.radio->power();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)PC_RADIO_WIFI_PS, Physical.radio->ps_mode());
}

static uint16_t g_frames;
static void on_frame(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel)
{
    (void)frame;
    (void)len;
    (void)rssi;
    (void)channel;
    g_frames++;
}

// Monitor mode retunes the radio and refuses a capture with nowhere to deliver.
void test_n_monitor_mode_tunes_and_refuses_a_null_sink()
{
    TEST_ASSERT_FALSE(Physical.radio->monitor_begin(6, NULL)); // no sink is a caller bug

    TEST_ASSERT_TRUE(Physical.radio->monitor_begin(6, on_frame));
    TEST_ASSERT_EQUAL_UINT8(6, Physical.wifi->channel());

    Physical.radio->monitor_set_channel(11);
    TEST_ASSERT_EQUAL_UINT8(11, Physical.wifi->channel());

    Physical.radio->monitor_set_channel(0); // 0 leaves the channel to whoever captures
    TEST_ASSERT_EQUAL_UINT8(11, Physical.wifi->channel());

    Physical.radio->monitor_end();
    TEST_ASSERT_EQUAL_UINT8(11, Physical.wifi->channel()); // ending does not retune
}

// The names are the layer's own vocabulary, not a vendor's.
void test_o_power_save_names()
{
    TEST_ASSERT_EQUAL_STRING("none", Physical.radio->ps_name(PC_PHY_PS_NONE));
    TEST_ASSERT_EQUAL_STRING("min_modem", Physical.radio->ps_name(PC_PHY_PS_MIN_MODEM));
    TEST_ASSERT_EQUAL_STRING("max_modem", Physical.radio->ps_name(PC_PHY_PS_MAX_MODEM));
    TEST_ASSERT_EQUAL_STRING("none", Physical.radio->ps_name((pc_phy_ps)99));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_no_link_reports_no_route);
    RUN_TEST(test_b_station_bring_up_is_live);
    RUN_TEST(test_c_ssid_reads_back);
    RUN_TEST(test_d_ssid_truncates_to_the_callers_cap);
    RUN_TEST(test_e_ssid_is_capped_at_the_802_11_limit);
    RUN_TEST(test_f_station_mac_is_locally_administered_unicast);
    RUN_TEST(test_g_softap_has_its_own_address);
    RUN_TEST(test_h_wired_wins_the_route);
    RUN_TEST(test_i_egress_mac_tracks_the_route_and_differs_from_the_station_mac);
    RUN_TEST(test_j_ipv6_global_address);
    RUN_TEST(test_k_power_save_mode_round_trips);
    RUN_TEST(test_l_busy_hold_refcount_gates_modem_sleep);
    RUN_TEST(test_m_power_applies_the_configured_mode);
    RUN_TEST(test_n_monitor_mode_tunes_and_refuses_a_null_sink);
    RUN_TEST(test_o_power_save_names);
    return UNITY_END();
}
