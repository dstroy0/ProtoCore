// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for egress-interface reporting, driven against the L1 backend the host build compiles
// (network_drivers/physical/physical.h).
//
// RFC 1122 sec 3.3.1.2 makes the default route the thing a host sends through when nothing more
// specific applies, so "which interface carries outbound traffic" is answered from the live route
// and never from which link was brought up last. test_classify_maps_the_live_route is the
// load-bearing case: it is the one call whose answer varies with its input, and every arm returns a
// different kind, so a swapped binding or a reordered comparison shows up here and nowhere else.
//
// The remaining cases drive a link that actually comes up and read the layer back: a readout has to
// answer from the link rather than from a constant, and a null destination has to be refused without
// writing, so a caller that ignores a return transmits nothing it did not write itself.

#include "network_drivers/physical/physical/physical.h"
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
    PhysicalV.route.egress_ip = egress_ip;
    PhysicalV.route.sta_ip = sta_ip;
    PhysicalV.route.ap_ip = ap_ip;
    Physical.classify_ip(protocore_physical_span());
    return PhysicalV.if_kind;
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

// RFC 1122 sec 3.3.1.2: egress names the interface the live default route is on. Nothing is up
// at first, so it names none; once the station associates it names the station and its address.
void test_egress_follows_the_live_default_route(void)
{
    Physical.egress(protocore_physical_span());
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ANY, PhysicalV.if_kind);
    Physical.egress_ip(protocore_physical_span());
    TEST_ASSERT_EQUAL_UINT32(0, PhysicalV.u32);

    PhysicalV.wifi.ssid = "protocore-net";
    PhysicalV.wifi.password = "passphrase";
    Physical.wifi_init(protocore_physical_span());
    TEST_ASSERT_TRUE(PhysicalV.ok);

    Physical.egress(protocore_physical_span());
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_WIFI_STA, PhysicalV.if_kind);
    Physical.egress_ip(protocore_physical_span());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, PhysicalV.u32);
}

// A wired link comes up and takes the default route from the station: a stack prefers wired when
// both are up, so egress reclassifies to Ethernet.
void test_a_wired_link_takes_the_route(void)
{
    Physical.eth_init(protocore_physical_span());
    TEST_ASSERT_TRUE(PhysicalV.ok);
    Physical.eth_ready(protocore_physical_span());
    TEST_ASSERT_TRUE(PhysicalV.ok);

    Physical.egress(protocore_physical_span());
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ETH, PhysicalV.if_kind);
}

// Bring-up associates the station and the readiness poll answers from the link, not from a
// constant. IPv6 is a separate bring-up, so it is not ready until it is asked for.
void test_wifi_bring_up_associates_the_station(void)
{
    PhysicalV.wifi.ssid = "protocore-net";
    PhysicalV.wifi.password = "passphrase";
    Physical.wifi_init(protocore_physical_span());
    TEST_ASSERT_TRUE(PhysicalV.ok);

    Physical.wifi_ready(protocore_physical_span());
    TEST_ASSERT_TRUE(PhysicalV.ok);

    PhysicalV.wifi.channel = 6;
    Physical.wifi_radio_init(protocore_physical_span());
    TEST_ASSERT_TRUE(PhysicalV.ok);
    Physical.wifi_channel(protocore_physical_span());
    TEST_ASSERT_EQUAL_UINT8(6, PhysicalV.u8);

    Physical.ip6_ready(protocore_physical_span());
    TEST_ASSERT_FALSE(PhysicalV.ok);
}

// Dual IP layer operation (RFC 4213 sec 2): bring-up autoconfigures the interface, after which
// the global unicast address (RFC 4291 sec 2.5.4) is readable and the readiness poll answers yes.
// A null destination is a caller bug and is refused.
void test_ipv6_autoconfigures_a_global_address(void)
{
    protocore_ip addr;
    memset(&addr, 0, sizeof(addr));

    Physical.ip6_ready(protocore_physical_span());
    TEST_ASSERT_FALSE(PhysicalV.ok); // not brought up yet

    Physical.ip6_init(protocore_physical_span());
    TEST_ASSERT_TRUE(PhysicalV.ok);

    Physical.ip6_ready(protocore_physical_span());
    TEST_ASSERT_TRUE(PhysicalV.ok);

    PhysicalV.read.ip6 = &addr;
    Physical.ip6_global(protocore_physical_span());
    TEST_ASSERT_TRUE(PhysicalV.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_IP_V6, addr.family);

    PhysicalV.read.ip6 = NULL;
    Physical.ip6_global(protocore_physical_span());
    TEST_ASSERT_FALSE(PhysicalV.ok);
}

// The radio-derived readouts answer from the link. Not associated they are the empty values;
// associated they carry the softAP address, the RSSI and the channel the radio is on.
void test_radio_readouts_answer_from_the_link(void)
{
    Physical.wifi_ap_ip(protocore_physical_span());
    TEST_ASSERT_EQUAL_UINT32(0, PhysicalV.u32); // no softAP started

    PhysicalV.wifi.ssid = "protocore-net";
    PhysicalV.wifi.password = "passphrase";
    Physical.wifi_init(protocore_physical_span());

    Physical.wifi_rssi(protocore_physical_span());
    TEST_ASSERT_NOT_EQUAL_INT8(0, PhysicalV.i8); // associated: a real signal strength

    Physical.wifi_channel(protocore_physical_span());
    TEST_ASSERT_NOT_EQUAL_UINT8(0, PhysicalV.u8);

    PhysicalV.wifi.ssid = "protocore-ap";
    PhysicalV.wifi.password = "passphrase";
    Physical.wifi_ap_init(protocore_physical_span());
    TEST_ASSERT_TRUE(PhysicalV.ok);
    Physical.wifi_ap_ip(protocore_physical_span());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, PhysicalV.u32);
}

// RFC 826 sizes a hardware address at ar$hln = 6 octets. With a link up both readouts fill the
// caller's six; a null destination is a caller bug and is refused without writing anything.
void test_mac_readouts_fill_six_octets(void)
{
    static const uint8_t BEFORE[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    uint8_t mac[6];
    memcpy(mac, BEFORE, sizeof(mac));

    PhysicalV.wifi.ssid = "protocore-net";
    PhysicalV.wifi.password = "passphrase";
    Physical.wifi_init(protocore_physical_span());

    PhysicalV.read.mac = mac;
    Physical.egress_mac(protocore_physical_span());
    TEST_ASSERT_TRUE(PhysicalV.ok);
    TEST_ASSERT_TRUE(memcmp(BEFORE, mac, 6) != 0); // the readout wrote the six octets

    memcpy(mac, BEFORE, sizeof(mac));
    PhysicalV.read.mac = mac;
    Physical.wifi_mac(protocore_physical_span());
    TEST_ASSERT_TRUE(PhysicalV.ok);
    TEST_ASSERT_TRUE(memcmp(BEFORE, mac, 6) != 0); // the readout wrote the six octets

    PhysicalV.read.mac = NULL;
    Physical.egress_mac(protocore_physical_span());
    TEST_ASSERT_FALSE(PhysicalV.ok);
    PhysicalV.read.mac = NULL;
    Physical.wifi_mac(protocore_physical_span());
    TEST_ASSERT_FALSE(PhysicalV.ok);
}

// Radio control reaches a radio, so an applied mode reads back. These are the L1 seam entry
// points themselves: RadioNs is incomplete in this translation unit, so the handle that wraps them
// is not reachable from here.
void test_radio_control_applies_and_reads_back(void)
{
    TEST_ASSERT_TRUE(protocore_phy_ps_set(PROTOCORE_PHY_PS_MAX_MODEM));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_MAX_MODEM, protocore_phy_ps_get());
    TEST_ASSERT_TRUE(protocore_phy_ps_set(PROTOCORE_PHY_PS_MIN_MODEM));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_MIN_MODEM, protocore_phy_ps_get());

    TEST_ASSERT_TRUE(protocore_phy_tx_power_set(11));
    TEST_ASSERT_TRUE(protocore_phy_tx_power_set(-1));

    // Capture with nowhere to deliver is a caller bug, not a mode.
    TEST_ASSERT_FALSE(protocore_phy_monitor_begin(6, NULL));

    protocore_phy_monitor_set_channel(11);
    protocore_phy_monitor_end();
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_MIN_MODEM, protocore_phy_ps_get());
}

// The registry the layer owns is reached through the span it publishes, and the radio child through
// the handle, so a missing binding is a null deref at the first call rather than a link error.
void test_layer_handle_is_bound(void)
{
    TEST_ASSERT_NOT_NULL(protocore_physical_span());
    TEST_ASSERT_NOT_NULL(PhysicalV.radio);
}
