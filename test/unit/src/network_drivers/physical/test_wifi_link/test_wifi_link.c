// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// The Wi-Fi link over a radio that this suite answers. The suite gives frames to the link as the radio
// task does. It claims and releases them through the driver table, and reads back what the link sent.

#include "network_drivers/physical/wifi_link/wifi_link.h"

#include "network_drivers/network/stack/stack.h"

#include <string.h>
#include <unity.h>

static const uint8_t MAC_OWN[IDEMIP_MAC_LEN] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x22u};
static const uint8_t MAC_PEER[IDEMIP_MAC_LEN] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x09u};
static const uint8_t ISN_SECRET[IDEMIP_TCP_ISN_SECRET_BYTES] = {0x55u, 0x66u};

#define IP_OWN 0xC0A80122u // 192.168.1.34

// The radio of this suite: each send lands here.
static uint8_t s_sent[IDEMIP_DMA_FRAME_MAX];
static size_t s_sent_len;
static int s_sends;
static embed_bool s_stack_up;

embed_bool protocore_wifi_radio_frame_send(const WifiLinkFrameArgs *args)
{
    memcpy(s_sent, args->frame, args->len);
    s_sent_len = args->len;
    s_sends++;
    return EMBED_TRUE;
}

embed_bool protocore_wifi_radio_mac_read(uint8_t *mac)
{
    memcpy(mac, MAC_OWN, IDEMIP_MAC_LEN);
    return EMBED_TRUE;
}

embed_bool protocore_wifi_radio_start(const WifiLinkStartArgs *args)
{
    return (args->ssid != NULL) ? EMBED_TRUE : EMBED_FALSE;
}

// A frame on a word boundary, as the link requires, with its first octet stamped.
static EMBED_ALIGN(8) uint8_t s_frame[IDEMIP_DMA_FRAME_MAX + 8u];

static embed_bool feed(uint8_t stamp, size_t len)
{
    memset(s_frame, 0, len);
    s_frame[0] = stamp;
    const WifiLinkFrameArgs frame = {.frame = s_frame, .len = len};
    return protocore_wifi_link_frame_receive(&frame);
}

// Claims and releases every frame in the ring. Each case thus starts on an empty ring.
static void drain(void)
{
    const IdemIpPhyDriver *const driver = protocore_wifi_link_driver();
    const uint8_t *frame = NULL;
    while (driver->rx_claim(&frame) != 0u)
    {
        driver->rx_release(frame);
    }
}

void setUp(void)
{
    s_sends = 0;
    s_sent_len = 0u;
}

void tearDown(void)
{
    drain();
}

// Claims come out in arrival order. A release frees only the buffer that it names. A refill thus never
// lands on a buffer that a claim still has.
void test_a_release_frees_only_the_buffer_that_it_names(void)
{
    const IdemIpPhyDriver *const driver = protocore_wifi_link_driver();
    TEST_ASSERT_TRUE(feed(0xA1u, 60u));
    TEST_ASSERT_TRUE(feed(0xB2u, 61u));

    const uint8_t *first = NULL;
    const uint8_t *second = NULL;
    TEST_ASSERT_EQUAL(60u, driver->rx_claim(&first));
    TEST_ASSERT_EQUAL(61u, driver->rx_claim(&second));
    TEST_ASSERT_EQUAL_HEX8(0xA1u, first[0]);
    TEST_ASSERT_EQUAL_HEX8(0xB2u, second[0]);

    driver->rx_release(second);
    for (uint32_t refill = 0u; refill < (uint32_t)(IDEMIP_RX_DESCRIPTORS - 1u); refill++)
    {
        TEST_ASSERT_TRUE_MESSAGE(feed(0xC3u, 62u), "a free buffer took no frame");
    }
    TEST_ASSERT_FALSE_MESSAGE(feed(0xD4u, 63u), "a frame landed on the buffer that the first claim holds");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xA1u, first[0], "a refill wrote over a claimed buffer");

    driver->rx_release(first);
    TEST_ASSERT_TRUE(feed(0xE5u, 64u));
}

// A full ring drops the next frame and counts it, and one release makes room for one frame.
void test_a_full_ring_drops_and_counts(void)
{
    const IdemIpPhyDriver *const driver = protocore_wifi_link_driver();
    const uint32_t dropped = protocore_wifi_link_dropped();
    for (uint32_t fill = 0u; fill < (uint32_t)IDEMIP_RX_DESCRIPTORS; fill++)
    {
        TEST_ASSERT_TRUE(feed(0x11u, 60u));
    }
    TEST_ASSERT_FALSE(feed(0x22u, 60u));
    TEST_ASSERT_EQUAL_UINT32(dropped + 1u, protocore_wifi_link_dropped());

    const uint8_t *frame = NULL;
    TEST_ASSERT_EQUAL(60u, driver->rx_claim(&frame));
    driver->rx_release(frame);
    TEST_ASSERT_TRUE(feed(0x33u, 60u));
}

// The memor copy needs a word boundary. The link drops a frame off one and counts it.
void test_a_frame_off_a_word_boundary_is_dropped_and_counted(void)
{
    const uint32_t dropped = protocore_wifi_link_dropped();
    const WifiLinkFrameArgs frame = {.frame = s_frame + 1u, .len = 60u};
    TEST_ASSERT_FALSE(protocore_wifi_link_frame_receive(&frame));
    TEST_ASSERT_EQUAL_UINT32(dropped + 1u, protocore_wifi_link_dropped());
    const uint8_t *claimed = NULL;
    TEST_ASSERT_EQUAL(0u, protocore_wifi_link_driver()->rx_claim(&claimed));
}

// The link reports the association that the radio records.
void test_the_link_reports_the_association(void)
{
    const IdemIpPhyDriver *const driver = protocore_wifi_link_driver();
    const WifiLinkStateArgs up = {.up = EMBED_TRUE};
    protocore_wifi_link_state_set(&up);
    TEST_ASSERT_TRUE(driver->link().up);
    const WifiLinkStateArgs down = {.up = EMBED_FALSE};
    protocore_wifi_link_state_set(&down);
    TEST_ASSERT_FALSE(driver->link().up);
    TEST_ASSERT_EQUAL_MEMORY(MAC_OWN, driver->mac(), IDEMIP_MAC_LEN);
}

// RFC 826 through the whole path: the radio gives a REQUEST to the link, the stack answers, and the
// REPLY goes out through the radio.
void test_an_arp_request_from_the_radio_gets_a_reply_through_the_radio(void)
{
    if (!s_stack_up)
    {
        StackV.start_args.isn_secret = ISN_SECRET;
        Stack.start(protocore_stack_span());
        TEST_ASSERT_TRUE_MESSAGE(StackV.ok, "the stack did not start");
        StackV.bind_args.driver = protocore_wifi_link_driver();
        StackV.bind_args.mac = protocore_wifi_link_driver()->mac();
        StackV.bind_args.rx_base = protocore_wifi_link_rx_base();
        StackV.bind_args.tx_base = protocore_wifi_link_tx_base();
        StackV.bind_args.phy_addr = 0u;
        StackV.bind_args.index = 0u;
        Stack.bind(protocore_stack_span());
        TEST_ASSERT_TRUE_MESSAGE(StackV.ok, "the link did not bind");
        StackV.addr4_args.address = IP_OWN;
        StackV.addr4_args.mask = 0xFFFFFF00u;
        StackV.addr4_args.gateway = 0u;
        StackV.addr4_args.index = 0u;
        Stack.set_addr4(protocore_stack_span());
        TEST_ASSERT_TRUE(StackV.ok);
        s_stack_up = EMBED_TRUE;
    }
    static const uint8_t broadcast[IDEMIP_MAC_LEN] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    memset(s_frame, 0, 60u);
    memcpy(s_frame, broadcast, IDEMIP_MAC_LEN);
    memcpy(s_frame + 6, MAC_PEER, IDEMIP_MAC_LEN);
    s_frame[12] = 0x08u; // EtherType: ARP
    s_frame[13] = 0x06u;
    s_frame[15] = 1u;    // hardware type: Ethernet
    s_frame[16] = 0x08u; // protocol type: IPv4
    s_frame[18] = IDEMIP_MAC_LEN;
    s_frame[19] = 4u;
    s_frame[21] = 1u; // opcode: REQUEST
    memcpy(s_frame + 22, MAC_PEER, IDEMIP_MAC_LEN);
    s_frame[28] = 0xC0u; // sender: 192.168.1.9
    s_frame[29] = 0xA8u;
    s_frame[30] = 0x01u;
    s_frame[31] = 0x09u;
    s_frame[38] = 0xC0u; // target: 192.168.1.34
    s_frame[39] = 0xA8u;
    s_frame[40] = 0x01u;
    s_frame[41] = 0x22u;
    const WifiLinkFrameArgs request = {.frame = s_frame, .len = 60u};
    TEST_ASSERT_TRUE(protocore_wifi_link_frame_receive(&request));

    StackV.tick_args.now_ms = 1000u;
    StackV.tick_args.random_word = 7u;
    Stack.tick(protocore_stack_span());
    TEST_ASSERT_TRUE(StackV.ok);

    TEST_ASSERT_EQUAL_INT(1, s_sends);
    TEST_ASSERT_EQUAL_MEMORY(MAC_PEER, s_sent, IDEMIP_MAC_LEN);
    TEST_ASSERT_EQUAL_MEMORY(MAC_OWN, s_sent + 6, IDEMIP_MAC_LEN);
    TEST_ASSERT_EQUAL_HEX8(0x08u, s_sent[12]);
    TEST_ASSERT_EQUAL_HEX8(0x06u, s_sent[13]);
    TEST_ASSERT_EQUAL_HEX8(2u, s_sent[21]); // opcode: REPLY
    TEST_ASSERT_EQUAL_MEMORY(s_frame + 38, s_sent + 28, 4u);
    TEST_ASSERT_EQUAL_MEMORY(MAC_PEER, s_sent + 32, IDEMIP_MAC_LEN);
}
