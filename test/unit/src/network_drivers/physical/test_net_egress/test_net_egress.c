// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for egress-interface reporting with no L1 backend
// (network_drivers/physical/physical.h).
//
// RFC 1122 sec 3.3.1.2 makes the default route the thing a host sends through when nothing more
// specific applies, so "which interface carries outbound traffic" is answered from the live route
// and never from which link was brought up last. test_classify_maps_the_live_route is the
// load-bearing case: it is the one call whose answer varies with its input, and every arm returns a
// different kind, so a swapped binding or a reordered comparison shows up here and nowhere else.
//
// The remaining cases pin the no-backend build (PROTOCORE_PHYSICAL_HAS_BACKEND == 0): a readout with
// no link under it must report the not-available value and leave the caller's buffer alone, so a
// caller that ignores a return transmits nothing it did not write itself.

#include "network_drivers/physical/physical.h"
#include <string.h>

#include <unity.h>

// A dotted quad in the network byte order protocore_net_*_ip() returns (RFC 791 app. B), first octet
// in the low byte. The classifier compares for equality alone, so the encoding does not change its
// answer; the addresses are RFC 5737 TEST-NET-1 so no expectation names a routable host.
#define IP4(a, b, c, d) (((uint32_t)(d) << 24) | ((uint32_t)(c) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

#define STA_IP IP4(192, 0, 2, 10)
#define AP_IP IP4(192, 0, 2, 1)
#define ETH_IP IP4(192, 0, 2, 20)

void setUp(void)
{
}

void tearDown(void)
{
}

static protocore_if_kind classify(uint32_t egress_ip, uint32_t sta_ip, uint32_t ap_ip)
{
    Physical.route.egress_ip = egress_ip;
    Physical.route.sta_ip = sta_ip;
    Physical.route.ap_ip = ap_ip;
    Physical.classify_ip(Physical.internal);
    return Physical.if_kind;
}

// Each arm answers with a different kind, so no other member could stand in for this one.
void test_classify_maps_the_live_route(void)
{
    // The route's address is the station's -> the station carries it.
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_WIFI_STA, classify(STA_IP, STA_IP, AP_IP));
    // The route's address is the softAP's -> the softAP carries it.
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_WIFI_AP, classify(AP_IP, STA_IP, AP_IP));
    // A live route that is neither WiFi address is the wired one (RFC 894 framing).
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ETH, classify(ETH_IP, STA_IP, AP_IP));
    // No route at all: no interface carries outbound traffic.
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ANY, classify(0, STA_IP, AP_IP));
}

// An interface that is down carries no address, and 0 must not compare equal to the "no route"
// egress that was already ruled out: a down link can never claim the route.
void test_a_down_interface_never_claims_the_route(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ETH, classify(ETH_IP, 0, 0));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ETH, classify(ETH_IP, STA_IP, 0));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ANY, classify(0, 0, 0));
    // softAP up, station up, route matching neither -> still wired.
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ETH, classify(ETH_IP, STA_IP, IP4(192, 0, 2, 2)));
}

// The station is compared first, so an address shared by both WiFi interfaces reads as the station.
void test_station_is_compared_before_the_softap(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_WIFI_STA, classify(STA_IP, STA_IP, STA_IP));
}

// With no backend there is no default route, so egress names no interface and no address.
void test_no_backend_reports_no_route(void)
{
    Physical.egress(Physical.internal);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ANY, Physical.if_kind);
    Physical.egress_ip(Physical.internal);
    TEST_ASSERT_EQUAL_UINT32(0, Physical.u32);
}

// A wired PHY that is not there cannot be started and never comes ready.
void test_no_backend_has_no_wired_link(void)
{
    Physical.eth_init(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);
    Physical.eth_ready(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);
}

// The radio bring-up calls have nothing to do and report success; the link never comes up behind
// them, which is what the readouts below show.
void test_no_backend_wifi_bring_up_is_a_no_op(void)
{
    Physical.wifi.ssid = "protocore-net";
    Physical.wifi.password = "passphrase";
    Physical.wifi_init(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);

    Physical.wifi_ready(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);

    Physical.wifi.channel = 6;
    Physical.wifi_radio_init(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);

    Physical.wifi.ssid = "protocore-ap";
    Physical.wifi_ap_init(Physical.internal);
    TEST_ASSERT_TRUE(Physical.ok);
}

// Dual IP layer operation (RFC 4213 sec 2) needs an interface; with none, it reports not-ready and
// hands back no address.
void test_no_backend_has_no_ipv6(void)
{
    protocore_ip addr;
    memset(&addr, 0, sizeof(addr));

    Physical.ip6_init(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);

    Physical.read.ip6 = &addr;
    Physical.ip6_global(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);

    Physical.ip6_ready(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);
}

// The radio-derived readouts report the not-associated values without touching a radio.
void test_no_backend_readouts_are_empty(void)
{
    Physical.wifi_ap_ip(Physical.internal);
    TEST_ASSERT_EQUAL_UINT32(0, Physical.u32);

    Physical.wifi_rssi(Physical.internal);
    TEST_ASSERT_EQUAL_INT8(0, Physical.i8);

    Physical.wifi_channel(Physical.internal);
    TEST_ASSERT_EQUAL_UINT8(0, Physical.u8);

    char ssid[16];
    ssid[0] = 'x';
    ssid[1] = '\0';
    Physical.read.text = ssid;
    Physical.read.cap = sizeof(ssid);
    Physical.wifi_ssid(Physical.internal);
    TEST_ASSERT_EQUAL_size_t(0, Physical.n);
    TEST_ASSERT_EQUAL_STRING("", ssid);

    // A null destination and a zero capacity are each their own reason to write nothing.
    Physical.read.text = NULL;
    Physical.read.cap = sizeof(ssid);
    Physical.wifi_ssid(Physical.internal);
    TEST_ASSERT_EQUAL_size_t(0, Physical.n);

    char untouched[4];
    untouched[0] = 'y';
    untouched[1] = '\0';
    Physical.read.text = untouched;
    Physical.read.cap = 0;
    Physical.wifi_ssid(Physical.internal);
    TEST_ASSERT_EQUAL_size_t(0, Physical.n);
    TEST_ASSERT_EQUAL_STRING("y", untouched);
}

// RFC 826 sizes a hardware address at ar$hln = 6 octets. With no link there is no address to give,
// and both readouts refuse without writing into the caller's six octets.
void test_no_backend_mac_readouts_leave_the_buffer_untouched(void)
{
    static const uint8_t BEFORE[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    uint8_t mac[6];
    memcpy(mac, BEFORE, sizeof(mac));

    Physical.read.mac = mac;
    Physical.egress_mac(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);
    TEST_ASSERT_EQUAL_MEMORY(BEFORE, mac, 6);

    Physical.read.mac = mac;
    Physical.wifi_mac(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);
    TEST_ASSERT_EQUAL_MEMORY(BEFORE, mac, 6);

    Physical.read.mac = NULL;
    Physical.egress_mac(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);
    Physical.read.mac = NULL;
    Physical.wifi_mac(Physical.internal);
    TEST_ASSERT_FALSE(Physical.ok);
}

// Radio control with no radio reports failure rather than pretending, and PROTOCORE_PHY_PS_NONE is
// the truthful mode when nothing can doze. These are the L1 seam entry points themselves: RadioNs is
// incomplete in this translation unit, so the handle that wraps them is not reachable from here.
void test_no_backend_radio_control_refuses(void)
{
    TEST_ASSERT_FALSE(protocore_phy_ps_set(PROTOCORE_PHY_PS_MAX_MODEM));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, protocore_phy_ps_get());
    TEST_ASSERT_FALSE(protocore_phy_ps_set(PROTOCORE_PHY_PS_MIN_MODEM));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, protocore_phy_ps_get());

    TEST_ASSERT_FALSE(protocore_phy_tx_power_set(11));
    TEST_ASSERT_FALSE(protocore_phy_tx_power_set(-1));
    TEST_ASSERT_FALSE(protocore_phy_monitor_begin(6, NULL));

    protocore_phy_monitor_set_channel(11);
    protocore_phy_monitor_end();
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, protocore_phy_ps_get());
}

// The layer handle carries the registry it owns and the radio child, so a missing binding is a null
// deref at the first call rather than a link error.
void test_layer_handle_is_bound(void)
{
    TEST_ASSERT_NOT_NULL(Physical.internal);
    TEST_ASSERT_NOT_NULL(Physical.radio);
}
