// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for layer 1 driven through a real backend (network_drivers/physical/physical.h).
//
// The env declares PROTOCORE_PHYSICAL_HAS_BACKEND=1, so test/mocks/physical stands in for silicon
// and the link can actually be up. That is what makes the readouts checkable: the no-op stubs
// answer every one of them with an empty value, so a caller that mishandles a live link reads
// exactly like one that works.
//
// test_i_egress_mac_tracks_the_route is the load-bearing case. RFC 826 sizes a hardware address at
// ar$hln = 6 octets and RFC 1122 sec 3.3.1.2 makes the default route the interface traffic leaves
// by, so the address a peer will see is the address of THAT interface - not of whichever radio the
// device also has. Only a build with a link up can tell the two readouts apart.
//
// The backend holds one static link state with no reset, so bring-up is monotonic and the cases run
// in the order the file lists them.

#include "network_drivers/physical/physical.h"
#include "network_drivers/physical/radio_power.h"

#include <string.h>

#include <unity.h>

// The dotted quad as protocore_net_*_ip() returns it: network byte order in a u32 (RFC 791 app. B),
// first octet in the low byte. Assembled here so the expected value is the test's own.
#define IP4(a, b, c, d) (((uint32_t)(d) << 24) | ((uint32_t)(c) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

// RFC 5737 TEST-NET-1: the documentation range the backend answers from.
#define STA_IP IP4(192, 0, 2, 10)
#define AP_IP IP4(192, 0, 2, 1)
#define ETH_IP IP4(192, 0, 2, 20)

void setUp(void)
{
}

void tearDown(void)
{
}

static protocore_if_kind egress(void)
{
    Physical.egress(Physical.internal);
    return Physical.if_kind;
}

static uint32_t egress_ip(void)
{
    Physical.egress_ip(Physical.internal);
    return Physical.u32;
}

static uint8_t channel(void)
{
    Physical.wifi_channel(Physical.internal);
    return Physical.u8;
}

static proto_bool join(const char *ssid)
{
    Physical.wifi.ssid = ssid;
    Physical.wifi.password = "passphrase";
    Physical.wifi_init(Physical.internal);
    return Physical.ok;
}

static size_t read_ssid(char *out, size_t cap)
{
    Physical.read.text = out;
    Physical.read.cap = cap;
    Physical.wifi_ssid(Physical.internal);
    return Physical.n;
}

static protocore_phy_ps ps_mode(void)
{
    Physical.radio->ps_mode(Physical.radio->internal);
    return Physical.radio->mode;
}

static proto_bool ps_set(protocore_phy_ps mode)
{
    Physical.radio->ps.mode = mode;
    Physical.radio->ps_set(Physical.radio->internal);
    return Physical.radio->ok;
}

// ---- nothing up -----------------------------------------------------------

// Before any bring-up there is no default route, so egress names no interface and no address.
void test_a_no_link_reports_no_route(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ANY, egress());
    TEST_ASSERT_EQUAL_UINT32(0, egress_ip());

    Physical.wifi_ready(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);

    Physical.wifi_ap_ip(Physical.internal);
    TEST_ASSERT_EQUAL_UINT32(0, Physical.u32);

    Physical.wifi_rssi(Physical.internal);
    TEST_ASSERT_EQUAL_INT8(0, Physical.i8);

    TEST_ASSERT_EQUAL_UINT8(0, channel());

    // No route means no egress address, even before any interface has one of its own.
    uint8_t mac[6];
    memset(mac, 0xFF, sizeof(mac));
    Physical.read.mac = mac;
    Physical.egress_mac(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);
    TEST_ASSERT_EQUAL_HEX8(0xFF, mac[0]);
}

// ---- the station ----------------------------------------------------------

// Associating brings the station up, and every station readout answers from that link.
void test_b_station_bring_up_is_live(void)
{
    TEST_ASSERT_TRUE(join("protocore-net"));

    Physical.wifi_ready(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_WIFI_STA, egress());
    TEST_ASSERT_EQUAL_UINT32(STA_IP, egress_ip());

    // RSSI is a signed dBm reading, negative for any real association.
    Physical.wifi_rssi(Physical.internal);
    TEST_ASSERT_TRUE(Physical.i8 < 0);

    // IEEE 802.11 channel numbering: an associated station sits on a valid channel, never 0.
    uint8_t ch = channel();
    TEST_ASSERT_TRUE(ch >= 1u && ch <= 14u);
}

// The SSID reads back exactly what was joined, terminated.
void test_c_ssid_reads_back(void)
{
    char out[64];
    memset(out, 0x7F, sizeof(out));

    size_t n = read_ssid(out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(strlen("protocore-net"), n);
    TEST_ASSERT_EQUAL_STRING("protocore-net", out);
    TEST_ASSERT_EQUAL_CHAR('\0', out[n]);
}

// The caller's capacity bounds the copy, and the result is still terminated.
void test_d_ssid_truncates_to_the_callers_cap(void)
{
    char small[6];
    memset(small, 0x7F, sizeof(small));

    size_t n = read_ssid(small, sizeof(small));
    TEST_ASSERT_EQUAL_size_t(sizeof(small) - 1u, n);
    TEST_ASSERT_EQUAL_CHAR('\0', small[n]);
    TEST_ASSERT_EQUAL_MEMORY("proto", small, 5);

    TEST_ASSERT_EQUAL_size_t(0, read_ssid(NULL, sizeof(small)));

    char untouched[4];
    untouched[0] = 'y';
    untouched[1] = '\0';
    TEST_ASSERT_EQUAL_size_t(0, read_ssid(untouched, 0));
    TEST_ASSERT_EQUAL_STRING("y", untouched);
}

// IEEE 802.11-2020 9.4.2.2: an SSID element carries at most 32 octets, so a longer one is cut to
// the standard's limit and never reported past it.
void test_e_ssid_is_capped_at_the_802_11_limit(void)
{
    static const char LONG_SSID[] = "0123456789012345678901234567890123456789"; // 40 octets
    TEST_ASSERT_TRUE(join(LONG_SSID));

    char out[64];
    size_t n = read_ssid(out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(32, n);
    TEST_ASSERT_EQUAL_MEMORY(LONG_SSID, out, 32);
    TEST_ASSERT_EQUAL_CHAR('\0', out[32]);

    TEST_ASSERT_TRUE(join("protocore-net")); // back to the short one
}

// RFC 7042 sec 2.1: bit 0 of the first octet is the I/G bit and bit 1 is the U/L bit. A station
// address that no IEEE OUI backs is locally administered unicast: U/L set, I/G clear.
void test_f_station_mac_is_locally_administered_unicast(void)
{
    uint8_t mac[6];
    memset(mac, 0, sizeof(mac));

    Physical.read.mac = mac;
    Physical.wifi_mac(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);
    TEST_ASSERT_EQUAL_HEX8(0x02, mac[0] & 0x02u);
    TEST_ASSERT_EQUAL_HEX8(0x00, mac[0] & 0x01u);

    Physical.read.mac = NULL;
    Physical.wifi_mac(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);
}

// ---- the softAP -----------------------------------------------------------

// The softAP comes up alongside the station and holds its own address without taking the route.
void test_g_softap_has_its_own_address(void)
{
    Physical.wifi.ssid = "protocore-ap";
    Physical.wifi.password = "ap-passphrase";
    Physical.wifi_ap_init(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);

    Physical.wifi_ap_ip(Physical.internal);
    TEST_ASSERT_EQUAL_UINT32(AP_IP, Physical.u32);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_WIFI_STA, egress());
    TEST_ASSERT_EQUAL_UINT32(STA_IP, egress_ip());
}

// ---- the wired link -------------------------------------------------------

// Bringing Ethernet up takes the default route from the station, the order a stack picks a route in
// when both are up (RFC 894 framing on the wired one).
void test_h_wired_wins_the_route(void)
{
    Physical.eth_ready(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok); // ready() is not init()

    Physical.eth_init(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);
    Physical.eth_ready(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ETH, egress());
    TEST_ASSERT_EQUAL_UINT32(ETH_IP, egress_ip());

    Physical.wifi_ready(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok); // the station is still associated underneath
}

// Once the wire carries the traffic the two readouts answer differently: wifi_mac is the 802.11
// station address, egress_mac is the address of whichever interface the route uses. Both are still
// locally administered unicast (RFC 7042 sec 2.1).
void test_i_egress_mac_tracks_the_route(void)
{
    uint8_t sta[6];
    uint8_t out[6];
    memset(sta, 0, sizeof(sta));
    memset(out, 0, sizeof(out));

    Physical.read.mac = sta;
    Physical.wifi_mac(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);

    Physical.read.mac = out;
    Physical.egress_mac(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);

    TEST_ASSERT_TRUE(memcmp(sta, out, 6) != 0);
    TEST_ASSERT_EQUAL_HEX8(0x02, out[0] & 0x02u);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0] & 0x01u);
}

// ---- IPv6 -----------------------------------------------------------------

// Dual IP layer operation (RFC 4213 sec 2) yields a global unicast address (RFC 4291 sec 2.5.4),
// and ready() tracks it.
void test_j_ipv6_global_address(void)
{
    protocore_ip addr;
    memset(&addr, 0, sizeof(addr));

    Physical.ip6_ready(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok); // ready() is not init()

    Physical.ip6_init(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);
    Physical.ip6_ready(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);

    Physical.read.ip6 = &addr;
    Physical.ip6_global(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_V6, addr.family);

    // RFC 3849 reserves 2001:db8::/32 for documentation, which is what the backend answers with.
    TEST_ASSERT_EQUAL_HEX8(0x20, addr.bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, addr.bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x0D, addr.bytes[2]);
    TEST_ASSERT_EQUAL_HEX8(0xB8, addr.bytes[3]);

    // RFC 4291 sec 2.5.6 puts link-local in fe80::/10, which a global address must not be.
    TEST_ASSERT_FALSE(addr.bytes[0] == 0xFEu && (addr.bytes[1] & 0xC0u) == 0x80u);

    Physical.read.ip6 = NULL;
    Physical.ip6_global(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);
}

// ---- radio control --------------------------------------------------------

// IEEE 802.11-2020 6.3.2.2 MLME-POWERMGT.request selects active mode or PS mode; with a radio under
// it, a mode that is set is a mode that reads back.
void test_k_power_save_mode_round_trips(void)
{
    TEST_ASSERT_TRUE(ps_set(PROTOCORE_PHY_PS_MAX_MODEM));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_MAX_MODEM, ps_mode());

    TEST_ASSERT_TRUE(ps_set(PROTOCORE_PHY_PS_MIN_MODEM));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_MIN_MODEM, ps_mode());

    TEST_ASSERT_TRUE(ps_set(PROTOCORE_PHY_PS_NONE));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());

    // 802.11-2020 11.7.6 selects a transmit power in dBm; a cap may be negative.
    Physical.radio->tx.dbm = 11;
    Physical.radio->tx_power_set(Physical.radio->internal);
    TEST_ASSERT_TRUE(Physical.radio->ok);
    Physical.radio->tx.dbm = -4;
    Physical.radio->tx_power_set(Physical.radio->internal);
    TEST_ASSERT_TRUE(Physical.radio->ok);
}

// The keep-awake refcount: the first hold puts the radio in active mode so a bulk transfer crosses
// no doze interval, and the balancing release restores PROTOCORE_RADIO_WIFI_PS once it reaches zero.
void test_l_busy_hold_refcount_gates_doze(void)
{
    TEST_ASSERT_TRUE(ps_set(PROTOCORE_PHY_PS_MAX_MODEM));

    Physical.radio->busy_hold(Physical.radio->internal);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());

    Physical.radio->busy_hold(Physical.radio->internal); // nested: still held
    Physical.radio->busy_release(Physical.radio->internal);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());

    Physical.radio->busy_release(Physical.radio->internal);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)PROTOCORE_RADIO_WIFI_PS, ps_mode());

    // An unbalanced release cannot drive the count negative or change the mode again.
    protocore_phy_ps before = ps_mode();
    Physical.radio->busy_release(Physical.radio->internal);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)before, ps_mode());
}

// power() applies the configured mode directly.
void test_m_power_applies_the_configured_mode(void)
{
    TEST_ASSERT_TRUE(ps_set(PROTOCORE_PHY_PS_MIN_MODEM));
    Physical.radio->power(Physical.radio->internal);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)PROTOCORE_RADIO_WIFI_PS, ps_mode());
}

static void on_frame(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t ch)
{
    (void)frame;
    (void)len;
    (void)rssi;
    (void)ch;
}

// Monitor capture tunes the radio to the named channel and refuses a capture with nowhere to
// deliver its frames.
void test_n_monitor_mode_tunes_and_refuses_a_null_sink(void)
{
    Physical.radio->monitor.channel = 6;
    Physical.radio->monitor.on_frame = NULL;
    Physical.radio->monitor_begin(Physical.radio->internal);
    TEST_ASSERT_FALSE(Physical.radio->ok);

    Physical.radio->monitor.on_frame = on_frame;
    Physical.radio->monitor_begin(Physical.radio->internal);
    TEST_ASSERT_TRUE(Physical.radio->ok);
    TEST_ASSERT_EQUAL_UINT8(6, channel());

    Physical.radio->monitor.channel = 11;
    Physical.radio->monitor_set_channel(Physical.radio->internal);
    TEST_ASSERT_EQUAL_UINT8(11, channel());

    Physical.radio->monitor.channel = 0; // 0 leaves the channel to whoever captures
    Physical.radio->monitor_set_channel(Physical.radio->internal);
    TEST_ASSERT_EQUAL_UINT8(11, channel());

    Physical.radio->monitor_end(Physical.radio->internal);
    TEST_ASSERT_EQUAL_UINT8(11, channel()); // ending does not retune
}

// The mode names are the layer's own vocabulary, not a vendor's, and an unknown value renders as
// the active mode of 802.11-2020 11.2.3.2 rather than as a made-up name.
void test_o_power_save_names(void)
{
    Physical.radio->ps.mode = PROTOCORE_PHY_PS_NONE;
    Physical.radio->ps_name(Physical.radio->internal);
    TEST_ASSERT_EQUAL_STRING("none", Physical.radio->text);

    Physical.radio->ps.mode = PROTOCORE_PHY_PS_MIN_MODEM;
    Physical.radio->ps_name(Physical.radio->internal);
    TEST_ASSERT_EQUAL_STRING("min_modem", Physical.radio->text);

    Physical.radio->ps.mode = PROTOCORE_PHY_PS_MAX_MODEM;
    Physical.radio->ps_name(Physical.radio->internal);
    TEST_ASSERT_EQUAL_STRING("max_modem", Physical.radio->text);

    Physical.radio->ps.mode = (protocore_phy_ps)99;
    Physical.radio->ps_name(Physical.radio->internal);
    TEST_ASSERT_EQUAL_STRING("none", Physical.radio->text);
}
