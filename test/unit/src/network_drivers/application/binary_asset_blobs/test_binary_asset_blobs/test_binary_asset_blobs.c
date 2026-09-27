// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the embedded theme registry (network_drivers/application/binary_asset_blobs).
//
// The lookup is an exact match. The cases that carry weight are the near misses: a prefix of a
// real name, a real name with a byte appended, a name that differs only in case, and a name longer
// than every embedded one. Each of those has to come back NULL, and every name the registry holds
// has to come back as its own entry.

#include "network_drivers/application/binary_asset_blobs/binary_asset_blobs.h"
#include <string.h>

#include <unity.h>

static uint8_t blobs_work[16]; // the borrow an entry takes; BinaryAssetBlobs never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

void test_every_embedded_name_finds_its_own_css(void)
{
    TEST_ASSERT_TRUE(PROTOCORE_THEME_BLOB_COUNT > 0u);
    for (size_t i = 0; i < PROTOCORE_THEME_BLOB_COUNT; i++)
    {
        TEST_ASSERT_EQUAL_PTR(PROTOCORE_THEME_BLOBS[i].css,
                              BinaryAssetBlobs.css(blobs_work, PROTOCORE_THEME_BLOBS[i].name));
    }
}

void test_registry_is_sorted_by_name(void)
{
    for (size_t i = 1; i < PROTOCORE_THEME_BLOB_COUNT; i++)
    {
        TEST_ASSERT_TRUE(strcmp(PROTOCORE_THEME_BLOBS[i - 1].name, PROTOCORE_THEME_BLOBS[i].name) < 0);
    }
}

void test_known_theme_is_minified_css(void)
{
    const char *css = BinaryAssetBlobs.css(blobs_work, "amber-crt");
    TEST_ASSERT_NOT_NULL(css);
    TEST_ASSERT_EQUAL_INT(0, strncmp(css, ":root{--bg:#0a0600;", 19));
}

void test_near_misses_do_not_match(void)
{
    TEST_ASSERT_NULL(BinaryAssetBlobs.css(blobs_work, "amber"));      // a prefix of a real name
    TEST_ASSERT_NULL(BinaryAssetBlobs.css(blobs_work, "amber-crtx")); // a real name plus a byte
    TEST_ASSERT_NULL(BinaryAssetBlobs.css(blobs_work, "Amber-CRT"));  // the match is case-sensitive
    TEST_ASSERT_NULL(BinaryAssetBlobs.css(blobs_work, ""));
    TEST_ASSERT_NULL(BinaryAssetBlobs.css(blobs_work, NULL));
}

void test_name_longer_than_every_theme_does_not_match(void)
{
    // Longer than the longest embedded name, so the length scan stops at its cap and nothing
    // compares equal, whatever the prefix.
    TEST_ASSERT_NULL(BinaryAssetBlobs.css(blobs_work, "amber-crt-amber-crt-amber-crt-amber-crt-amber-crt"));
}

void test_trademarked_theme_follows_its_gate(void)
{
#if PROTOCORE_THEMES_INCLUDE_TRADEMARKED
    TEST_ASSERT_NOT_NULL(BinaryAssetBlobs.css(blobs_work, "barbie"));
#else
    TEST_ASSERT_NULL(BinaryAssetBlobs.css(blobs_work, "barbie"));
#endif
}
