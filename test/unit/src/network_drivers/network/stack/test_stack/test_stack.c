// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// The stack module over the fake PHY of idemIP. A case puts a frame on the wire, runs the engine and one
// tick of the stack, and reads back what the stack transmitted.

#include "network_drivers/network/stack/stack.h"

#include "test/support/fake_phy.h" // the link driver of idemIP for the host, from include/idemIP

#include <string.h>
#include <unity.h>

IDEMIP_FAKE_PHY_STORAGE;
IDEMIP_FAKE_PHY_DRIVER(0);

static EMBED_ALIGN(64) uint8_t s_rx_bufs[IDEMIP_RX_DESCRIPTORS * IDEMIP_DMA_BUF_STRIDE];
static EMBED_ALIGN(64) uint8_t s_tx_bufs[IDEMIP_TX_DESCRIPTORS * IDEMIP_DMA_BUF_STRIDE];

static const uint8_t MAC_OWN[IDEMIP_MAC_LEN] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x02u};
static const uint8_t MAC_PEER[IDEMIP_MAC_LEN] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x09u};
static const uint8_t ISN_SECRET[IDEMIP_TCP_ISN_SECRET_BYTES] = {0x11u, 0x22u, 0x33u, 0x44u};

#define IP_OWN 0xC0A80102u  // 192.168.1.2
#define IP_PEER 0xC0A80109u // 192.168.1.9
#define IP_MASK 0xFFFFFF00u

static uint32_t s_now_ms;
static embed_bool s_up;

static void put16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)(value >> 8);
    at[1] = (uint8_t)value;
}

static void put32(uint8_t *at, uint32_t value)
{
    put16(at, (uint16_t)(value >> 16));
    put16(at + 2, (uint16_t)value);
}

static uint16_t get16(const uint8_t *at)
{
    return (uint16_t)((at[0] << 8) | at[1]);
}

static uint32_t get32(const uint8_t *at)
{
    return ((uint32_t)get16(at) << 16) | get16(at + 2);
}

// The RFC 1071 checksum of len octets.
static uint16_t checksum(const uint8_t *data, size_t len)
{
    uint32_t sum = 0u;
    for (size_t at = 0u; at + 1u < len; at += 2u)
    {
        sum += get16(data + at);
    }
    if ((len & 1u) != 0u)
    {
        sum += (uint32_t)data[len - 1u] << 8;
    }
    while ((sum >> 16) != 0u)
    {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static void ethernet_header(uint8_t *frame, const uint8_t *dst, uint16_t type)
{
    memcpy(frame, dst, IDEMIP_MAC_LEN);
    memcpy(frame + IDEMIP_MAC_LEN, MAC_PEER, IDEMIP_MAC_LEN);
    put16(frame + 12, type);
}

// One pass: the engine lands the frame, the stack drains and answers, and the engine completes the
// transmission.
static void pass(void)
{
    idemip_fake_phy_poll(&g_idemip_fake_phy[0]);
    s_now_ms += 10u;
    StackV.tick_args.now_ms = s_now_ms;
    StackV.tick_args.random_word = 0x5A5Au;
    Stack.tick(protocore_stack_span());
    TEST_ASSERT_TRUE(StackV.ok);
    idemip_fake_phy_poll(&g_idemip_fake_phy[0]);
}

void setUp(void)
{
    s_now_ms = 1000u;
    if (s_up)
    {
        return;
    }
    idemip_fake_phy_attach(&g_idemip_fake_phy[0], s_rx_bufs, s_tx_bufs, MAC_OWN);
    StackV.start_args.isn_secret = ISN_SECRET;
    Stack.start(protocore_stack_span());
    TEST_ASSERT_TRUE_MESSAGE(StackV.ok, "the stack did not start");
    StackV.bind_args.driver = &idemip_fake_phy_drv_0;
    StackV.bind_args.mac = MAC_OWN;
    StackV.bind_args.rx_base = s_rx_bufs;
    StackV.bind_args.tx_base = s_tx_bufs;
    StackV.bind_args.phy_addr = 1u;
    StackV.bind_args.index = 0u;
    Stack.bind(protocore_stack_span());
    TEST_ASSERT_TRUE_MESSAGE(StackV.ok, "the interface did not bind");
    StackV.addr4_args.address = IP_OWN;
    StackV.addr4_args.mask = IP_MASK;
    StackV.addr4_args.gateway = 0u;
    StackV.addr4_args.index = 0u;
    Stack.set_addr4(protocore_stack_span());
    TEST_ASSERT_TRUE_MESSAGE(StackV.ok, "the interface did not take its address");
    s_up = EMBED_TRUE;
}

void tearDown(void)
{
    while (idemip_fake_phy_tx_count(&g_idemip_fake_phy[0]) != 0u)
    {
        idemip_fake_phy_tx_drop(&g_idemip_fake_phy[0]);
    }
}

// RFC 826: a REQUEST for the address of the interface gets a REPLY to the hardware address that asked.
void test_an_arp_request_for_the_interface_gets_a_reply(void)
{
    static const uint8_t broadcast[IDEMIP_MAC_LEN] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint8_t frame[60] = {0};
    ethernet_header(frame, broadcast, 0x0806u);
    put16(frame + 14, 1u);       // hardware type: Ethernet
    put16(frame + 16, 0x0800u);  // protocol type: IPv4
    frame[18] = IDEMIP_MAC_LEN;  // hardware address length
    frame[19] = 4u;              // protocol address length
    put16(frame + 20, 1u);       // opcode: REQUEST
    memcpy(frame + 22, MAC_PEER, IDEMIP_MAC_LEN);
    put32(frame + 28, IP_PEER);
    put32(frame + 38, IP_OWN);
    TEST_ASSERT_TRUE(idemip_fake_phy_feed(&g_idemip_fake_phy[0], frame, sizeof frame));

    pass();

    TEST_ASSERT_EQUAL_UINT16(1u, StackV.frames);
    TEST_ASSERT_EQUAL(1u, idemip_fake_phy_tx_count(&g_idemip_fake_phy[0]));
    size_t len = 0u;
    const uint8_t *reply = idemip_fake_phy_tx_peek(&g_idemip_fake_phy[0], &len);
    TEST_ASSERT_NOT_NULL(reply);
    TEST_ASSERT_EQUAL_MEMORY(MAC_PEER, reply, IDEMIP_MAC_LEN);
    TEST_ASSERT_EQUAL_HEX16(0x0806u, get16(reply + 12));
    TEST_ASSERT_EQUAL_UINT16(2u, get16(reply + 20)); // opcode: REPLY
    TEST_ASSERT_EQUAL_MEMORY(MAC_OWN, reply + 22, IDEMIP_MAC_LEN);
    TEST_ASSERT_EQUAL_HEX32(IP_OWN, get32(reply + 28));
    TEST_ASSERT_EQUAL_HEX32(IP_PEER, get32(reply + 38));
}

// RFC 792: an Echo to the address of the interface gets an Echo Reply with the same identifier, sequence
// number and data.
void test_an_echo_to_the_interface_gets_an_echo_reply(void)
{
    uint8_t frame[14 + 20 + 12] = {0};
    ethernet_header(frame, MAC_OWN, 0x0800u);
    uint8_t *const ip = frame + 14;
    ip[0] = 0x45u; // version 4, header of 5 words
    put16(ip + 2, 20u + 12u);
    put16(ip + 4, 0x1234u);
    ip[8] = 64u; // time to live
    ip[9] = 1u;  // protocol: ICMP
    put32(ip + 12, IP_PEER);
    put32(ip + 16, IP_OWN);
    put16(ip + 10, checksum(ip, 20u));
    uint8_t *const icmp = ip + 20;
    icmp[0] = 8u; // type: Echo
    put16(icmp + 4, 0xBEEFu);
    put16(icmp + 6, 7u);
    memcpy(icmp + 8, "ping", 4u);
    put16(icmp + 2, checksum(icmp, 12u));
    TEST_ASSERT_TRUE(idemip_fake_phy_feed(&g_idemip_fake_phy[0], frame, sizeof frame));

    pass();

    TEST_ASSERT_EQUAL(1u, idemip_fake_phy_tx_count(&g_idemip_fake_phy[0]));
    size_t len = 0u;
    const uint8_t *reply = idemip_fake_phy_tx_peek(&g_idemip_fake_phy[0], &len);
    TEST_ASSERT_NOT_NULL(reply);
    TEST_ASSERT_EQUAL_MEMORY(MAC_PEER, reply, IDEMIP_MAC_LEN);
    TEST_ASSERT_EQUAL_MEMORY(MAC_OWN, reply + IDEMIP_MAC_LEN, IDEMIP_MAC_LEN);
    TEST_ASSERT_EQUAL_HEX16(0x0800u, get16(reply + 12));
    const uint8_t *const reply_ip = reply + 14;
    const size_t header_len = (size_t)(reply_ip[0] & 0x0Fu) * 4u;
    TEST_ASSERT_EQUAL_UINT16(20u + 12u, get16(reply_ip + 2)); // total length
    TEST_ASSERT_EQUAL_UINT8(1u, reply_ip[9]);
    TEST_ASSERT_EQUAL_HEX32(IP_OWN, get32(reply_ip + 12));
    TEST_ASSERT_EQUAL_HEX32(IP_PEER, get32(reply_ip + 16));
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0u, checksum(reply_ip, header_len), "the IPv4 header checksum does not hold");
    const uint8_t *const reply_icmp = reply_ip + header_len;
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0u, checksum(reply_icmp, 12u), "the ICMP checksum does not hold");
    TEST_ASSERT_EQUAL_UINT8(0u, reply_icmp[0]); // type: Echo Reply
    TEST_ASSERT_EQUAL_HEX16(0xBEEFu, get16(reply_icmp + 4));
    TEST_ASSERT_EQUAL_UINT16(7u, get16(reply_icmp + 6));
    TEST_ASSERT_EQUAL_MEMORY("ping", reply_icmp + 8, 4u);
}
