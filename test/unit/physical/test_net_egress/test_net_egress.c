// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for egress-interface reporting (network_drivers/physical). The lwIP
// default-route + WiFi lookups are ESP32-only; the pure classifier that maps an
// egress IP against the WiFi station / softAP IPs is host-tested here.

#include "network_drivers/physical/physical.h"
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// Egress IP matching the station IP -> WiFi station.
void test_classify_sta()
{
    TEST_ASSERT_EQUAL_INT(PC_IF_WIFI_STA, Physical.link->classify_ip(0x0A000005u, 0x0A000005u, 0xC0A80401u));
}

// Egress IP matching the softAP IP -> softAP.
void test_classify_ap()
{
    TEST_ASSERT_EQUAL_INT(PC_IF_WIFI_AP, Physical.link->classify_ip(0xC0A80401u, 0x0A000005u, 0xC0A80401u));
}

// A live egress IP that is neither WiFi IP -> wired (Ethernet).
void test_classify_eth()
{
    TEST_ASSERT_EQUAL_INT(PC_IF_ETH, Physical.link->classify_ip(0xC0A80105u, 0x0A000005u, 0));
    TEST_ASSERT_EQUAL_INT(PC_IF_ETH, Physical.link->classify_ip(0xC0A80105u, 0, 0)); // ETH only, no WiFi
    // softAP is up (ap_ip != 0) but the egress IP matches neither WiFi IP -> still wired.
    TEST_ASSERT_EQUAL_INT(PC_IF_ETH, Physical.link->classify_ip(0xC0A80105u, 0x0A000005u, 0xC0A80402u));
}

// No route -> ANY, regardless of the WiFi IPs.
void test_classify_none()
{
    TEST_ASSERT_EQUAL_INT(PC_IF_ANY, Physical.link->classify_ip(0, 0x0A000005u, 0xC0A80401u));
}

// On a host build there is no default route, so egress reports ANY / 0.
void test_egress_host_stub()
{
    TEST_ASSERT_EQUAL_INT(PC_IF_ANY, Physical.link->egress());
    TEST_ASSERT_EQUAL_UINT32(0, Physical.link->egress_ip());
}

// Ethernet bring-up is ESP32-only; on host (and when disabled) it reports not-ready.
void test_eth_host_stub()
{
    TEST_ASSERT_FALSE(Physical.eth->init());
    TEST_ASSERT_FALSE(Physical.eth->ready());
}

// WiFi/AP bring-up on a host build: fire-and-forget calls that always report success
// (there's no radio to fail), matching the "always true on host builds" contract.
void test_wifi_bringup_host_stub()
{
    TEST_ASSERT_TRUE(Physical.wifi->init("ssid", "password"));
    TEST_ASSERT_TRUE(Physical.wifi->ready());
    TEST_ASSERT_TRUE(Physical.wifi->init_radio(6));
    TEST_ASSERT_TRUE(Physical.wifi->init_ap("ap-ssid", "ap-password"));
}

// IPv6 is ESP32-only; on host (and when disabled) it reports not-ready / no address.
void test_ipv6_host_stub()
{
    pc_ip addr;
    TEST_ASSERT_FALSE(Physical.ip6->init());
    TEST_ASSERT_FALSE(Physical.ip6->global_addr(&addr));
    TEST_ASSERT_FALSE(Physical.ip6->ready());
}

// Radio-derived readouts (AP IP, RSSI, MAC, SSID, channel) are ESP32-only; on host
// they report the "not associated" values without touching the radio at all.
void test_radio_readouts_host_stub()
{
    TEST_ASSERT_EQUAL_UINT32(0, Physical.wifi->ap_ip());
    TEST_ASSERT_EQUAL_INT(0, Physical.wifi->rssi());
    TEST_ASSERT_EQUAL_UINT8(0, Physical.wifi->channel());

    uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_FALSE(Physical.link->mac(mac));

    char ssid[16] = {'x', '\0'};
    TEST_ASSERT_EQUAL_UINT32(0, Physical.wifi->ssid(ssid, sizeof(ssid)));
    TEST_ASSERT_EQUAL_STRING("", ssid); // host stub null-terminates out[0] when cap > 0

    // Both `if (out && cap)` subconditions, false side: null out, then zero cap.
    TEST_ASSERT_EQUAL_UINT32(0, Physical.wifi->ssid(NULL, sizeof(ssid)));
    char untouched[4] = {'y', '\0'};
    TEST_ASSERT_EQUAL_UINT32(0, Physical.wifi->ssid(untouched, 0));
    TEST_ASSERT_EQUAL_STRING("y", untouched); // cap==0 -> out left untouched
}

// The MAC readouts are ESP32-only. Both report failure on host and leave the caller's buffer
// untouched, so a caller that ignores the return transmits nothing it did not write itself.
void test_mac_readouts_leave_the_buffer_untouched()
{
    static const uint8_t before[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};

    TEST_ASSERT_FALSE(Physical.link->egress_mac(mac));
    TEST_ASSERT_EQUAL_MEMORY(before, mac, 6);
    TEST_ASSERT_FALSE(Physical.link->mac(mac));
    TEST_ASSERT_EQUAL_MEMORY(before, mac, 6);

    TEST_ASSERT_FALSE(Physical.link->egress_mac(NULL)); // a null out is its own reason to refuse
    TEST_ASSERT_FALSE(Physical.link->mac(NULL));
}

// Radio control with no radio reports failure rather than pretending: a caller that asks for
// monitor mode on a target without one must be able to tell, and NONE is the truthful mode when
// nothing can sleep.
void test_radio_control_host_stub()
{
    // The L1 entry points themselves, not the Radio table that wraps them: RadioNs is incomplete
    // here by design, so this file reaches physical.c's own stubs directly.
    TEST_ASSERT_FALSE(pc_phy_ps_set(PC_PHY_PS_MAX_MODEM));
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_PS_NONE, pc_phy_ps_get()); // the set did not take
    TEST_ASSERT_FALSE(pc_phy_ps_set(PC_PHY_PS_MIN_MODEM));
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_PS_NONE, pc_phy_ps_get());
    TEST_ASSERT_FALSE(pc_phy_tx_power_set(11));
    TEST_ASSERT_FALSE(pc_phy_tx_power_set(-1));
    TEST_ASSERT_FALSE(pc_phy_monitor_begin(6, NULL));

    pc_phy_monitor_set_channel(11); // no-ops, and must not crash
    pc_phy_monitor_end();
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_PS_NONE, pc_phy_ps_get());
}

// The layer handle carries every child. A child is a pointer, so a missing one is a null deref at
// the first call rather than a link error.
void test_layer_handle_carries_every_child()
{
    TEST_ASSERT_NOT_NULL(Physical.wifi);
    TEST_ASSERT_NOT_NULL(Physical.eth);
    TEST_ASSERT_NOT_NULL(Physical.ip6);
    TEST_ASSERT_NOT_NULL(Physical.link);
    TEST_ASSERT_NOT_NULL(Physical.iface);
    TEST_ASSERT_NOT_NULL(Physical.radio);
}

// The classifier is the one link member whose answer varies with its input, so it is the one a
// swapped binding would show up in. Every arm, and the boundary between them.
void test_classify_is_bound_to_the_classifier()
{
    // Each arm returns a different kind, so no other member could stand in for this one.
    TEST_ASSERT_EQUAL_INT(PC_IF_ANY, Physical.link->classify_ip(0, 0, 0));
    TEST_ASSERT_EQUAL_INT(PC_IF_WIFI_STA, Physical.link->classify_ip(7u, 7u, 9u));
    TEST_ASSERT_EQUAL_INT(PC_IF_WIFI_AP, Physical.link->classify_ip(9u, 7u, 9u));
    TEST_ASSERT_EQUAL_INT(PC_IF_ETH, Physical.link->classify_ip(5u, 7u, 9u));

    // A zero station or softAP IP never matches, so an interface that is down cannot claim the
    // route by comparing equal to a zero egress that was already ruled out.
    TEST_ASSERT_EQUAL_INT(PC_IF_ETH, Physical.link->classify_ip(5u, 0, 0));
    TEST_ASSERT_EQUAL_INT(PC_IF_ANY, Physical.link->classify_ip(0, 0, 0));

    // The station is tested before the softAP, so a shared IP reads as the station.
    TEST_ASSERT_EQUAL_INT(PC_IF_WIFI_STA, Physical.link->classify_ip(7u, 7u, 7u));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_mac_readouts_leave_the_buffer_untouched);
    RUN_TEST(test_radio_control_host_stub);
    RUN_TEST(test_layer_handle_carries_every_child);
    RUN_TEST(test_classify_is_bound_to_the_classifier);
    RUN_TEST(test_classify_sta);
    RUN_TEST(test_classify_ap);
    RUN_TEST(test_classify_eth);
    RUN_TEST(test_classify_none);
    RUN_TEST(test_egress_host_stub);
    RUN_TEST(test_eth_host_stub);
    RUN_TEST(test_wifi_bringup_host_stub);
    RUN_TEST(test_ipv6_host_stub);
    RUN_TEST(test_radio_readouts_host_stub);
    return UNITY_END();
}
