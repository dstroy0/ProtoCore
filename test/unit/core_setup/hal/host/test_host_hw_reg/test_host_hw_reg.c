// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the modelled register bus (test/core_setup/hal/host/host_hw_reg.h).
//
// A register driver is written against a width and a byte order it never states, and gets both from
// whatever the part happens to do. That assumption is invisible until the part changes, which is why
// the window models the carriage rather than just storing a word: a driver that only works at 32 bits
// little-endian is wrong somewhere, and the only place that can be shown off target is here.
//
// What each case asserts is a PROPERTY of a bus, not an implementation detail:
//   - at 32 bits little-endian the pair is the identity, which is what a native build takes;
//   - a register is whatever was last written to it, at any setting, when read back at that setting;
//   - the order is observable: a value written big-endian and read little-endian comes back with its
//     units reversed, and reversing twice is the identity;
//   - a narrow bus reverses within its unit, not across the whole register, which is what separates
//     a 16-bit big-endian bus from a 32-bit one;
//   - addresses do not alias, and an unseen address reads zero.

#include "test/core_setup/hal/host/host_hw_reg.h"

#include <unity.h>

// Three addresses inside one peripheral window, word aligned as every register is.
#define A0 0x60000000u
#define A1 0x60000004u
#define A2 0x60000100u

void setUp(void)
{
    protocore_hw_reg_host_reset();
    protocore_hw_reg_host_bus(32u, PROTO_FALSE); // the setting a native build takes
}

void tearDown(void)
{
    protocore_hw_reg_host_bus(32u, PROTO_FALSE);
}

void test_unseen_address_reads_zero(void)
{
    TEST_ASSERT_EQUAL_HEX32(0u, protocore_hw_reg_host_read(A0));
}

void test_32_le_is_the_identity(void)
{
    protocore_hw_reg_host_write(A0, 0x11223344u);
    TEST_ASSERT_EQUAL_HEX32(0x11223344u, protocore_hw_reg_host_read(A0));
}

// Whatever the bus, a register read back at the setting it was written at is what was written.
void test_round_trip_at_every_setting(void)
{
    static const unsigned BITS[3] = {16u, 32u, 64u};
    for (unsigned i = 0; i < 3u; i++)
    {
        for (unsigned be = 0; be < 2u; be++)
        {
            protocore_hw_reg_host_reset();
            protocore_hw_reg_host_bus(BITS[i], be ? PROTO_TRUE : PROTO_FALSE);
            protocore_hw_reg_host_write(A0, 0xdeadbeefu);
            TEST_ASSERT_EQUAL_HEX32(0xdeadbeefu, protocore_hw_reg_host_read(A0));
        }
    }
}

// The order is observable: what a big-endian bus laid down is read back reversed by a little-endian
// one. This is the case that would catch a driver assuming one order.
void test_order_is_observable_at_32(void)
{
    protocore_hw_reg_host_bus(32u, PROTO_TRUE);
    protocore_hw_reg_host_write(A0, 0x11223344u);
    protocore_hw_reg_host_bus(32u, PROTO_FALSE);
    TEST_ASSERT_EQUAL_HEX32(0x44332211u, protocore_hw_reg_host_read(A0));
}

// A 16-bit bus reverses inside each halfword, not across the register, so its disagreement with a
// little-endian reader is per unit. That is what separates it from the 32-bit case above.
void test_narrow_bus_reverses_within_its_unit(void)
{
    protocore_hw_reg_host_bus(16u, PROTO_TRUE);
    protocore_hw_reg_host_write(A0, 0x11223344u);
    protocore_hw_reg_host_bus(32u, PROTO_FALSE);
    TEST_ASSERT_EQUAL_HEX32(0x22114433u, protocore_hw_reg_host_read(A0));
}

// A bus wider than the register carries it in one unit, so 64 and 32 lay a register down the same
// way. A driver cannot tell them apart from one register, which is the honest answer.
void test_wide_bus_matches_32(void)
{
    protocore_hw_reg_host_bus(64u, PROTO_TRUE);
    protocore_hw_reg_host_write(A0, 0x11223344u);
    protocore_hw_reg_host_bus(32u, PROTO_FALSE);
    uint32_t wide = protocore_hw_reg_host_read(A0);
    protocore_hw_reg_host_reset();
    protocore_hw_reg_host_bus(32u, PROTO_TRUE);
    protocore_hw_reg_host_write(A0, 0x11223344u);
    protocore_hw_reg_host_bus(32u, PROTO_FALSE);
    TEST_ASSERT_EQUAL_HEX32(protocore_hw_reg_host_read(A0), wide);
}

// Reversing twice is the identity, at every width.
void test_double_reversal_is_identity(void)
{
    static const unsigned BITS[3] = {16u, 32u, 64u};
    for (unsigned i = 0; i < 3u; i++)
    {
        protocore_hw_reg_host_reset();
        protocore_hw_reg_host_bus(BITS[i], PROTO_TRUE);
        protocore_hw_reg_host_write(A0, 0x0f1e2d3cu);
        protocore_hw_reg_host_bus(BITS[i], PROTO_FALSE);
        uint32_t once = protocore_hw_reg_host_read(A0);
        protocore_hw_reg_host_write(A1, once);
        protocore_hw_reg_host_bus(BITS[i], PROTO_TRUE);
        TEST_ASSERT_EQUAL_HEX32(0x0f1e2d3cu, protocore_hw_reg_host_read(A1));
    }
}

// Distinct addresses are distinct registers, including two that fold to the same window index.
void test_addresses_do_not_alias(void)
{
    protocore_hw_reg_host_write(A0, 0xaaaaaaaau);
    protocore_hw_reg_host_write(A1, 0xbbbbbbbbu);
    protocore_hw_reg_host_write(A2, 0xccccccccu);
    TEST_ASSERT_EQUAL_HEX32(0xaaaaaaaau, protocore_hw_reg_host_read(A0));
    TEST_ASSERT_EQUAL_HEX32(0xbbbbbbbbu, protocore_hw_reg_host_read(A1));
    TEST_ASSERT_EQUAL_HEX32(0xccccccccu, protocore_hw_reg_host_read(A2));
}

// A reset drops every slot, so one test's writes are not another's starting state.
void test_reset_drops_every_slot(void)
{
    protocore_hw_reg_host_write(A0, 0xffffffffu);
    protocore_hw_reg_host_reset();
    TEST_ASSERT_EQUAL_HEX32(0u, protocore_hw_reg_host_read(A0));
}

// A width the bus does not have leaves the current setting standing rather than selecting something
// arbitrary, so a caller's typo cannot silently change what every later access means.
void test_bad_width_leaves_the_setting_standing(void)
{
    protocore_hw_reg_host_bus(32u, PROTO_FALSE);
    protocore_hw_reg_host_bus(24u, PROTO_FALSE); // not a width this models
    protocore_hw_reg_host_write(A0, 0x11223344u);
    TEST_ASSERT_EQUAL_HEX32(0x11223344u, protocore_hw_reg_host_read(A0));
}

// The macros the register drivers are written in reach the same window as the functions.
void test_macros_reach_the_same_window(void)
{
    PROTOCORE_HW_WR(A0, 0x12345678u);
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, protocore_hw_reg_host_read(A0));
    protocore_hw_reg_host_write(A1, 0x87654321u);
    TEST_ASSERT_EQUAL_HEX32(0x87654321u, PROTOCORE_HW_RD(A1));
}
