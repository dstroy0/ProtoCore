// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/ntcip: the NTCIP object OID definitions + the OID builder.

#include "services/transportation/ntcip/ntcip.h"
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_roots_under_nema(void)
{
    // Every NTCIP object is under 1.3.6.1.4.1.1206.4.2.
    const uint32_t prefix[] = {1, 3, 6, 1, 4, 1, 1206, 4, 2};
    const uint32_t *roots[] = {NTCIP_1202_MAX_PHASES, NTCIP_1202_PHASE_MIN_GREEN, NTCIP_1202_PHASE_STATUS,
                               NTCIP_1203_DMS_MAX_LENGTH, NTCIP_1203_DMS_MESSAGE_MULTI};
    const size_t lens[] = {NTCIP_1202_MAX_PHASES_LEN, NTCIP_1202_PHASE_MIN_GREEN_LEN, NTCIP_1202_PHASE_STATUS_LEN,
                           NTCIP_1203_DMS_MAX_LENGTH_LEN, NTCIP_1203_DMS_MESSAGE_MULTI_LEN};
    for (int r = 0; r < 5; r++)
    {
        TEST_ASSERT_TRUE(lens[r] > 9);
        for (int i = 0; i < 9; i++)
        {
            TEST_ASSERT_EQUAL_UINT32(prefix[i], roots[r][i]);
        }
    }
    // 1202 vs 1203 differ at the device-class arc (index 9): 1 (asc) vs 3 (dms).
    TEST_ASSERT_EQUAL_UINT32(1, NTCIP_1202_MAX_PHASES[9]);
    TEST_ASSERT_EQUAL_UINT32(3, NTCIP_1203_DMS_MAX_LENGTH[9]);
}

void test_oid_builder_scalar_and_index(void)
{
    uint32_t out[24];
    // A scalar takes .0.
    size_t n = pc_ntcip_oid(NTCIP_1202_MAX_PHASES, NTCIP_1202_MAX_PHASES_LEN, 0, out, sizeof(out) / sizeof(out[0]));
    TEST_ASSERT_EQUAL_size_t(NTCIP_1202_MAX_PHASES_LEN + 1, n);
    TEST_ASSERT_EQUAL_UINT32(0, out[n - 1]);

    // A table column takes the row index.
    n = pc_ntcip_oid(NTCIP_1202_PHASE_MIN_GREEN, NTCIP_1202_PHASE_MIN_GREEN_LEN, 4, out, sizeof(out) / sizeof(out[0]));
    TEST_ASSERT_EQUAL_size_t(NTCIP_1202_PHASE_MIN_GREEN_LEN + 1, n);
    TEST_ASSERT_EQUAL_UINT32(4, out[n - 1]);
    // The root is copied verbatim before the index.
    for (size_t i = 0; i < NTCIP_1202_PHASE_MIN_GREEN_LEN; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(NTCIP_1202_PHASE_MIN_GREEN[i], out[i]);
    }
}

void test_oid_builder_overflow(void)
{
    uint32_t out[4]; // too small
    TEST_ASSERT_EQUAL_size_t(0,
                             pc_ntcip_oid(NTCIP_1203_DMS_MESSAGE_MULTI, NTCIP_1203_DMS_MESSAGE_MULTI_LEN, 1, out, 4));
}

void test_oid_builder_invalid_args(void)
{
    uint32_t out[24];
    // NULL root.
    TEST_ASSERT_EQUAL_size_t(0, pc_ntcip_oid(NULL, NTCIP_1202_MAX_PHASES_LEN, 0, out, sizeof(out) / sizeof(out[0])));
    // NULL out.
    TEST_ASSERT_EQUAL_size_t(
        0, pc_ntcip_oid(NTCIP_1202_MAX_PHASES, NTCIP_1202_MAX_PHASES_LEN, 0, NULL, sizeof(out) / sizeof(out[0])));
    // Zero-length root.
    TEST_ASSERT_EQUAL_size_t(0, pc_ntcip_oid(NTCIP_1202_MAX_PHASES, 0, 0, out, sizeof(out) / sizeof(out[0])));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_roots_under_nema);
    RUN_TEST(test_oid_builder_scalar_and_index);
    RUN_TEST(test_oid_builder_overflow);
    RUN_TEST(test_oid_builder_invalid_args);
    return UNITY_END();
}
