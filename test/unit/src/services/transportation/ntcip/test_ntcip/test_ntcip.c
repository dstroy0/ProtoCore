// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NTCIP transportation-device object identifiers
// (services/transportation/ntcip/ntcip.h).
//
// The load-bearing case is test_roots_sit_under_the_nema_enterprise_arc. Two registries fix that
// prefix: RFC 2578 sec 4 defines iso(1).org(3).dod(6).internet(1).private(4).enterprises(1), giving
// 1.3.6.1.4.1, and the IANA Private Enterprise Number registry assigns 1206 to the National
// Electrical Manufacturers Association (NEMA). An OID that does not start with those arcs is not
// addressing an NTCIP device at all, whatever the rest of it says.
//
// The NTCIP 1202 and 1203 object trees are paid NEMA standards and were not obtainable here, so the
// column arcs below the device-class node are asserted structurally only (distinct, self-consistent
// lengths, correctly extended by the builder), never against an invented published number.

#include "services/transportation/ntcip/ntcip.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// 1.3.6.1.4.1 = enterprises, .1206 = NEMA, .4.2 = the transportation-device subtree.
static const uint32_t NEMA_PREFIX[] = {1, 3, 6, 1, 4, 1, 1206, 4, 2};

static void assert_under_nema(const uint32_t *root, size_t len, const char *what)
{
    TEST_ASSERT_TRUE_MESSAGE(len > sizeof(NEMA_PREFIX) / sizeof(NEMA_PREFIX[0]), what);
    for (size_t i = 0; i < sizeof(NEMA_PREFIX) / sizeof(NEMA_PREFIX[0]); i++)
    {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(NEMA_PREFIX[i], root[i], what);
    }
}

void test_roots_sit_under_the_nema_enterprise_arc(void)
{
    assert_under_nema(NTCIP_1202_MAX_PHASES, NTCIP_1202_MAX_PHASES_LEN, "maxPhases");
    assert_under_nema(NTCIP_1202_PHASE_MIN_GREEN, NTCIP_1202_PHASE_MIN_GREEN_LEN, "phaseMinimumGreen");
    assert_under_nema(NTCIP_1202_PHASE_STATUS, NTCIP_1202_PHASE_STATUS_LEN, "phaseStatusGroupGreens");
    assert_under_nema(NTCIP_1203_DMS_MAX_LENGTH, NTCIP_1203_DMS_MAX_LENGTH_LEN, "dmsMaxMultiStringLength");
    assert_under_nema(NTCIP_1203_DMS_MESSAGE_MULTI, NTCIP_1203_DMS_MESSAGE_MULTI_LEN, "dmsMessageMultiString");
}

// The arc after 1206.4.2 selects the device class: NTCIP 1202 signal controllers hang off .1, NTCIP
// 1203 dynamic message signs off .3, so a controller object can never be read as a sign object.
void test_device_class_arc_separates_1202_from_1203(void)
{
    const size_t k = sizeof(NEMA_PREFIX) / sizeof(NEMA_PREFIX[0]); // index of the device-class arc

    TEST_ASSERT_EQUAL_UINT32(1u, NTCIP_1202_MAX_PHASES[k]);
    TEST_ASSERT_EQUAL_UINT32(1u, NTCIP_1202_PHASE_MIN_GREEN[k]);
    TEST_ASSERT_EQUAL_UINT32(1u, NTCIP_1202_PHASE_STATUS[k]);
    TEST_ASSERT_EQUAL_UINT32(3u, NTCIP_1203_DMS_MAX_LENGTH[k]);
    TEST_ASSERT_EQUAL_UINT32(3u, NTCIP_1203_DMS_MESSAGE_MULTI[k]);
}

// No two objects may share an OID: an SNMP agent keys its registry on exactly this.
void test_every_root_is_distinct(void)
{
    const uint32_t *const ROOT[5] = {NTCIP_1202_MAX_PHASES, NTCIP_1202_PHASE_MIN_GREEN, NTCIP_1202_PHASE_STATUS,
                                     NTCIP_1203_DMS_MAX_LENGTH, NTCIP_1203_DMS_MESSAGE_MULTI};
    const size_t LEN[5] = {NTCIP_1202_MAX_PHASES_LEN, NTCIP_1202_PHASE_MIN_GREEN_LEN, NTCIP_1202_PHASE_STATUS_LEN,
                           NTCIP_1203_DMS_MAX_LENGTH_LEN, NTCIP_1203_DMS_MESSAGE_MULTI_LEN};

    for (size_t a = 0; a < 5; a++)
    {
        TEST_ASSERT_TRUE(LEN[a] > 0);
        for (size_t b = a + 1; b < 5; b++)
        {
            proto_bool same = (LEN[a] == LEN[b]) ? PROTO_TRUE : PROTO_FALSE;
            if (same)
            {
                for (size_t i = 0; i < LEN[a]; i++)
                {
                    if (ROOT[a][i] != ROOT[b][i])
                    {
                        same = PROTO_FALSE;
                        break;
                    }
                }
            }
            TEST_ASSERT_FALSE(same);
        }
    }
}

// The builder copies the root and appends one instance arc, so the result is root_len + 1 arcs.
void test_oid_builder_appends_the_instance(void)
{
    uint32_t out[24];
    size_t n = protocore_ntcip_oid(NTCIP_1202_PHASE_MIN_GREEN, NTCIP_1202_PHASE_MIN_GREEN_LEN, 4, out,
                                   sizeof(out) / sizeof(out[0]));

    TEST_ASSERT_EQUAL_UINT(NTCIP_1202_PHASE_MIN_GREEN_LEN + 1u, n);
    for (size_t i = 0; i < NTCIP_1202_PHASE_MIN_GREEN_LEN; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(NTCIP_1202_PHASE_MIN_GREEN[i], out[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(4u, out[n - 1]); // phase 4's row in the phase table
}

// A scalar takes instance 0 - the .0 an SNMP scalar object is always read at.
void test_oid_builder_scalar_takes_zero(void)
{
    uint32_t out[24];
    size_t n =
        protocore_ntcip_oid(NTCIP_1202_MAX_PHASES, NTCIP_1202_MAX_PHASES_LEN, 0, out, sizeof(out) / sizeof(out[0]));

    TEST_ASSERT_EQUAL_UINT(NTCIP_1202_MAX_PHASES_LEN + 1u, n);
    TEST_ASSERT_EQUAL_UINT32(0u, out[n - 1]);

    // the same root with a different index is a different OID
    uint32_t other[24];
    TEST_ASSERT_EQUAL_UINT(n, protocore_ntcip_oid(NTCIP_1202_MAX_PHASES, NTCIP_1202_MAX_PHASES_LEN, 1, other,
                                                  sizeof(other) / sizeof(other[0])));
    TEST_ASSERT_EQUAL_UINT32(1u, other[n - 1]);
    TEST_ASSERT_NOT_EQUAL(out[n - 1], other[n - 1]);
}

// A buffer that cannot hold root_len + 1 arcs reports 0 and is not written into.
void test_oid_builder_refuses_a_short_buffer(void)
{
    uint32_t out[24];
    const size_t need = NTCIP_1203_DMS_MESSAGE_MULTI_LEN + 1u;

    out[0] = 0xDEADBEEFu;
    TEST_ASSERT_EQUAL_UINT(
        0u, protocore_ntcip_oid(NTCIP_1203_DMS_MESSAGE_MULTI, NTCIP_1203_DMS_MESSAGE_MULTI_LEN, 1, out, need - 1u));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, out[0]);

    TEST_ASSERT_EQUAL_UINT(
        need, protocore_ntcip_oid(NTCIP_1203_DMS_MESSAGE_MULTI, NTCIP_1203_DMS_MESSAGE_MULTI_LEN, 1, out, need));
}

// Null pointers and an empty root are refused rather than dereferenced.
void test_oid_builder_null_guards(void)
{
    uint32_t out[24];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ntcip_oid(NULL, 4, 0, out, sizeof(out) / sizeof(out[0])));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ntcip_oid(NTCIP_1202_MAX_PHASES, NTCIP_1202_MAX_PHASES_LEN, 0, NULL, 24));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ntcip_oid(NTCIP_1202_MAX_PHASES, 0, 0, out, sizeof(out) / sizeof(out[0])));
}

// The published length constant must match the array it describes, or every caller walks off the end.
void test_lengths_are_self_consistent(void)
{
    TEST_ASSERT_EQUAL_UINT(12u, NTCIP_1202_MAX_PHASES_LEN);
    TEST_ASSERT_EQUAL_UINT(13u, NTCIP_1202_PHASE_MIN_GREEN_LEN);
    TEST_ASSERT_EQUAL_UINT(14u, NTCIP_1202_PHASE_STATUS_LEN);
    TEST_ASSERT_EQUAL_UINT(12u, NTCIP_1203_DMS_MAX_LENGTH_LEN);
    TEST_ASSERT_EQUAL_UINT(14u, NTCIP_1203_DMS_MESSAGE_MULTI_LEN);

    // a table column carries entry + column arcs below the table node, so it is longer than a scalar
    TEST_ASSERT_TRUE(NTCIP_1202_PHASE_MIN_GREEN_LEN > NTCIP_1202_MAX_PHASES_LEN);
    TEST_ASSERT_TRUE(NTCIP_1203_DMS_MESSAGE_MULTI_LEN > NTCIP_1203_DMS_MAX_LENGTH_LEN);
}
