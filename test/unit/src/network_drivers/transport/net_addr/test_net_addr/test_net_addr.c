// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for src/network_drivers/transport/net_addr/net_addr.h - the stack's address as the
// library's protocore_ip.
//
// Two surfaces, and both are driven here: the flat pair every caller in the tree uses, and the
// NetAddr entries, which until now no call site in src/ or test/ ever reached.
//
// The cases assert on the BYTES rather than on a word, because that is the bug the module exists
// to avoid: the stack hands the four v4 octets back inside a uint32_t whose memory is the address
// in network order, so reading its arithmetic value byte reverses it on a little-endian host and
// the round trip still passes if both directions make the same mistake. Each direction is checked
// against a literal, so a shared byte swap fails.

#include "network_drivers/transport/net_addr/net_addr.h"
#include "shared/ip/ip.h"
#include <string.h>

#include <unity.h>

static protocore_net_ip g_stack;
static protocore_ip g_lib;

void setUp(void)
{
    memset(&g_stack, 0, sizeof(g_stack));
    memset(&g_lib, 0, sizeof(g_lib));
    NetAddr.ok = PROTO_FALSE;
}

void tearDown(void)
{
}

static const uint8_t V6_DOC[16] = {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

// ---------------------------------------------------------------------------
// Inbound: the stack's address, read into the library's
// ---------------------------------------------------------------------------

// The four octets land in the first four bytes, in the order they are written in.
void test_v4_reads_into_the_library_address_in_network_order(void)
{
    protocore_net_ip4_set(&g_stack, 192, 0, 2, 13);

    protocore_net_addr_to_ip(&g_stack, &g_lib);

    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_IP_V4, g_lib.family);
    TEST_ASSERT_EQUAL_UINT8(192, g_lib.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0, g_lib.bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(2, g_lib.bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(13, g_lib.bytes[3]);
}

// The sixteen bytes cross unchanged.
void test_v6_reads_into_the_library_address_unchanged(void)
{
    g_stack.type = PROTOCORE_NET_TYPE_V6;
    memcpy(g_stack.bytes, V6_DOC, sizeof(V6_DOC));

    protocore_net_addr_to_ip(&g_stack, &g_lib);

    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_IP_V6, g_lib.family);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(V6_DOC, g_lib.bytes, 16);
}

// A family the stack tagged neither v4 nor v6 converts to nothing, not to a wrong v4.
void test_an_untagged_family_reads_as_none(void)
{
    g_stack.type = PROTOCORE_NET_TYPE_ANY;
    g_stack.bytes[0] = 0xFF;

    g_lib = protocore_ip_from_v4_octets(10, 0, 0, 1); // something to overwrite
    protocore_net_addr_to_ip(&g_stack, &g_lib);

    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_IP_NONE, g_lib.family);
    TEST_ASSERT_EQUAL_UINT8(0, g_lib.bytes[0]);
}

// A null source empties the destination rather than leaving what was there.
void test_a_null_source_empties_the_destination(void)
{
    g_lib = protocore_ip_from_v4_octets(10, 0, 0, 1);

    protocore_net_addr_to_ip(NULL, &g_lib);

    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_IP_NONE, g_lib.family);
    TEST_ASSERT_EQUAL_UINT8(0, g_lib.bytes[0]);
}

// A null destination is nowhere to write, so nothing is written.
void test_a_null_destination_reads_nothing(void)
{
    protocore_net_ip4_set(&g_stack, 192, 0, 2, 13);
    protocore_net_addr_to_ip(&g_stack, NULL);
}

// ---------------------------------------------------------------------------
// Outbound: the library's address, written into the stack's
// ---------------------------------------------------------------------------

// The four octets land in the stack's own v4 form, tagged v4.
void test_v4_writes_into_the_stack_address(void)
{
    g_lib = protocore_ip_from_v4_octets(198, 51, 100, 7);

    TEST_ASSERT_TRUE(protocore_net_addr_from_ip(&g_lib, &g_stack));

    TEST_ASSERT_TRUE(protocore_net_ip_is_v4(&g_stack));
    uint32_t word = protocore_net_ip4_u32(protocore_net_ip_as_v4(&g_stack));
    uint8_t octets[4];
    memcpy(octets, &word, 4);
    TEST_ASSERT_EQUAL_UINT8(198, octets[0]);
    TEST_ASSERT_EQUAL_UINT8(51, octets[1]);
    TEST_ASSERT_EQUAL_UINT8(100, octets[2]);
    TEST_ASSERT_EQUAL_UINT8(7, octets[3]);
}

// The sixteen bytes cross unchanged, and the address is tagged v6.
void test_v6_writes_into_the_stack_address(void)
{
    g_lib = protocore_ip_from_v6_bytes(V6_DOC);

    proto_bool ok = protocore_net_addr_from_ip(&g_lib, &g_stack);

#if PROTOCORE_NET_HAS_IPV6
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(protocore_net_ip_is_v6(&g_stack));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(V6_DOC, protocore_net_ip6_bytes(&g_stack), 16);
#else
    // A stack built without v6 has nowhere to put the address, and says so.
    TEST_ASSERT_FALSE(ok);
#endif
}

// An empty address names no family this stack can send to.
void test_an_empty_address_cannot_be_written(void)
{
    g_lib.family = PROTOCORE_IP_NONE;

    TEST_ASSERT_FALSE(protocore_net_addr_from_ip(&g_lib, &g_stack));
}

// A null source leaves the destination at the zeroed v4 the write starts from.
void test_a_null_source_cannot_be_written(void)
{
    TEST_ASSERT_FALSE(protocore_net_addr_from_ip(NULL, &g_stack));
}

// A null destination is nowhere to write, and reports false rather than writing.
void test_a_null_destination_cannot_be_written(void)
{
    g_lib = protocore_ip_from_v4_octets(198, 51, 100, 7);

    TEST_ASSERT_FALSE(protocore_net_addr_from_ip(&g_lib, NULL));
}

// ---------------------------------------------------------------------------
// The namespace
// ---------------------------------------------------------------------------

// to_ip carries the operands off the handle and produces what the flat call produces.
void test_the_entry_reads_the_operands_off_the_handle(void)
{
    protocore_net_ip4_set(&g_stack, 203, 0, 113, 42);
    NetAddr.in.addr = &g_stack;
    NetAddr.in.out_ip = &g_lib;

    NetAddr.to_ip(NULL);

    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_IP_V4, g_lib.family);
    TEST_ASSERT_EQUAL_UINT8(203, g_lib.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0, g_lib.bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(113, g_lib.bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(42, g_lib.bytes[3]);
}

// from_ip reports on the handle, so a caller reads the outcome where it set the operands.
void test_the_entry_reports_the_outcome_on_the_handle(void)
{
    g_lib = protocore_ip_from_v4_octets(203, 0, 113, 42);
    NetAddr.out.ip = &g_lib;
    NetAddr.out.out_addr = &g_stack;

    NetAddr.from_ip(NULL);

    TEST_ASSERT_TRUE(NetAddr.ok);
    TEST_ASSERT_TRUE(protocore_net_ip_is_v4(&g_stack));
}

// The false the flat call reports reaches the handle too, rather than being dropped on the way.
void test_the_entry_reports_a_refusal_on_the_handle(void)
{
    g_lib.family = PROTOCORE_IP_NONE;
    NetAddr.out.ip = &g_lib;
    NetAddr.out.out_addr = &g_stack;
    NetAddr.ok = PROTO_TRUE;

    NetAddr.from_ip(NULL);

    TEST_ASSERT_FALSE(NetAddr.ok);
}

// A table of same-typed function pointers initialized by position binds to whatever order the
// struct declares, so a member inserted into the header shifts every binding after it and still
// compiles. Each entry is called for behavior only that entry produces.
void test_each_entry_reaches_the_call_its_name_promises(void)
{
    protocore_net_ip4_set(&g_stack, 203, 0, 113, 42);
    NetAddr.in.addr = &g_stack;
    NetAddr.in.out_ip = &g_lib;
    NetAddr.ok = PROTO_TRUE;

    NetAddr.to_ip(NULL); // reads; leaves ok alone

    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_IP_V4, g_lib.family);
    TEST_ASSERT_TRUE(NetAddr.ok);

    protocore_net_ip other;
    memset(&other, 0, sizeof(other));
    NetAddr.out.ip = &g_lib;
    NetAddr.out.out_addr = &other;

    NetAddr.from_ip(NULL); // writes; reports

    TEST_ASSERT_TRUE(NetAddr.ok);
    TEST_ASSERT_TRUE(protocore_net_ip_is_v4(&other));
}

// A round trip through both directions is the address it started as, byte for byte.
void test_a_v4_round_trip_is_the_address_it_started_as(void)
{
    protocore_net_ip4_set(&g_stack, 192, 0, 2, 13);

    protocore_net_addr_to_ip(&g_stack, &g_lib);

    protocore_net_ip back;
    memset(&back, 0, sizeof(back));
    TEST_ASSERT_TRUE(protocore_net_addr_from_ip(&g_lib, &back));

    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_stack.bytes, back.bytes, 4);
    TEST_ASSERT_EQUAL_UINT8(g_stack.type, back.type);
}

#if PROTOCORE_NET_HAS_IPV6
// The same, for the sixteen-byte address.
void test_a_v6_round_trip_is_the_address_it_started_as(void)
{
    g_stack.type = PROTOCORE_NET_TYPE_V6;
    memcpy(g_stack.bytes, V6_DOC, sizeof(V6_DOC));

    protocore_net_addr_to_ip(&g_stack, &g_lib);

    protocore_net_ip back;
    memset(&back, 0, sizeof(back));
    TEST_ASSERT_TRUE(protocore_net_addr_from_ip(&g_lib, &back));

    TEST_ASSERT_EQUAL_UINT8_ARRAY(V6_DOC, protocore_net_ip6_bytes(&back), 16);
    TEST_ASSERT_TRUE(protocore_net_ip_is_v6(&back));
}
#endif
