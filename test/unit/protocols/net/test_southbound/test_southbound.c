// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/southbound/southbound.h"
#include <unity.h>

typedef struct
{
    int32_t regs[16];
    int fail_next;
} FakeCtx;

static int fake_read(void *ctx, uint32_t p, int32_t *out)
{
    FakeCtx *f = (FakeCtx *)ctx;
    if (f->fail_next)
    {
        int e = f->fail_next;
        f->fail_next = 0;
        return e;
    }
    if (p >= 16)
    {
        return SB_ERR_ARG;
    }
    *out = f->regs[p];
    return SB_OK;
}

static int fake_write(void *ctx, uint32_t p, int32_t v)
{
    FakeCtx *f = (FakeCtx *)ctx;
    if (p >= 16)
    {
        return SB_ERR_ARG;
    }
    f->regs[p] = v;
    return SB_OK;
}

static int fake_read_block(void *ctx, uint32_t first, int32_t *out, size_t n)
{
    FakeCtx *f = (FakeCtx *)ctx;
    if (first + n > 16)
    {
        return SB_ERR_ARG;
    }
    for (size_t i = 0; i < n; i++)
    {
        out[i] = f->regs[first + i];
    }
    return (int)n;
}

static int fake_write_block(void *ctx, uint32_t first, const int32_t *in, size_t n)
{
    FakeCtx *f = (FakeCtx *)ctx;
    if (first + n > 16)
    {
        return SB_ERR_ARG;
    }
    for (size_t i = 0; i < n; i++)
    {
        f->regs[first + i] = in[i];
    }
    return (int)n;
}

static FakeCtx g_ctx;
static SouthboundDriver g_full = {"fake", fake_read, fake_write, fake_read_block, fake_write_block, &g_ctx};

// Register one borrowed driver and report the code.
static int32_t sb_add(const SouthboundDriver *drv)
{
    Southbound.drv = drv;
    Southbound.add(Southbound.internal);
    return Southbound.i32;
}

// Look up a name and report the driver it matched.
static const SouthboundDriver *sb_find(const char *name)
{
    Southbound.name = name;
    Southbound.find(Southbound.internal);
    return Southbound.driver;
}

static int32_t sb_read(const char *name, uint32_t point, int32_t *value_out)
{
    Southbound.name = name;
    Southbound.point.point = point;
    Southbound.point.value_out = value_out;
    Southbound.read(Southbound.internal);
    return Southbound.i32;
}

static int32_t sb_write(const char *name, uint32_t point, int32_t value)
{
    Southbound.name = name;
    Southbound.point.point = point;
    Southbound.point.value = value;
    Southbound.write(Southbound.internal);
    return Southbound.i32;
}

static int32_t sb_read_block(const char *name, uint32_t first, int32_t *out, size_t n)
{
    Southbound.name = name;
    Southbound.block.first = first;
    Southbound.block.out = out;
    Southbound.block.n = n;
    Southbound.read_block(Southbound.internal);
    return Southbound.i32;
}

static int32_t sb_write_block(const char *name, uint32_t first, const int32_t *in, size_t n)
{
    Southbound.name = name;
    Southbound.block.first = first;
    Southbound.block.in = in;
    Southbound.block.n = n;
    Southbound.write_block(Southbound.internal);
    return Southbound.i32;
}

static size_t sb_count(void)
{
    Southbound.count(Southbound.internal);
    return Southbound.n;
}

void setUp(void)
{
    Southbound.clear(Southbound.internal);
    for (int i = 0; i < 16; i++)
    {
        g_ctx.regs[i] = i * 10;
    }
    g_ctx.fail_next = 0;
}
void tearDown(void)
{
}

void test_register_and_find(void)
{
    TEST_ASSERT_EQUAL_INT(SB_OK, sb_add(&g_full));
    TEST_ASSERT_EQUAL_size_t(1, sb_count());
    TEST_ASSERT_EQUAL_PTR(&g_full, sb_find("fake"));
    TEST_ASSERT_NULL(sb_find("nope"));

    TEST_ASSERT_EQUAL_INT(SB_ERR_DUP, sb_add(&g_full));

    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_add(NULL));
    SouthboundDriver noname = {NULL, fake_read, NULL, NULL, NULL, NULL};
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_add(&noname));
}

void test_read_write_dispatch(void)
{
    sb_add(&g_full);
    int32_t v = -1;
    TEST_ASSERT_EQUAL_INT(SB_OK, sb_read("fake", 3, &v));
    TEST_ASSERT_EQUAL_INT32(30, v);
    TEST_ASSERT_EQUAL_INT(SB_OK, sb_write("fake", 3, 999));
    TEST_ASSERT_EQUAL_INT(SB_OK, sb_read("fake", 3, &v));
    TEST_ASSERT_EQUAL_INT32(999, v);

    TEST_ASSERT_EQUAL_INT(SB_ERR_NOT_FOUND, sb_read("x", 0, &v));
    TEST_ASSERT_EQUAL_INT(SB_ERR_NOT_FOUND, sb_write("x", 0, 0));

    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_read("fake", 0, NULL));

    g_ctx.fail_next = -42;
    TEST_ASSERT_EQUAL_INT(-42, sb_read("fake", 0, &v));
}

void test_block_atomic(void)
{
    sb_add(&g_full);
    int32_t out[4] = {0};
    TEST_ASSERT_EQUAL_INT(4, sb_read_block("fake", 2, out, 4));
    TEST_ASSERT_EQUAL_INT32(20, out[0]);
    TEST_ASSERT_EQUAL_INT32(50, out[3]);
    int32_t in[3] = {7, 8, 9};
    TEST_ASSERT_EQUAL_INT(3, sb_write_block("fake", 5, in, 3));
    int32_t v = 0;
    sb_read("fake", 6, &v);
    TEST_ASSERT_EQUAL_INT32(8, v);

    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_read_block("fake", 0, out, 0));
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_write_block("fake", 0, NULL, 3));
}

void test_unsupported_capability(void)
{

    SouthboundDriver ro = {"ro", fake_read, NULL, NULL, NULL, &g_ctx};
    sb_add(&ro);
    int32_t v = 0, out[2] = {0};
    TEST_ASSERT_EQUAL_INT(SB_OK, sb_read("ro", 0, &v));
    TEST_ASSERT_EQUAL_INT(SB_ERR_UNSUPPORTED, sb_write("ro", 0, 1));
    TEST_ASSERT_EQUAL_INT(SB_ERR_UNSUPPORTED, sb_read_block("ro", 0, out, 2));
    TEST_ASSERT_EQUAL_INT(SB_ERR_UNSUPPORTED, sb_write_block("ro", 0, out, 2));
}

void test_registry_full(void)
{

    static SouthboundDriver drv[9];
    static char names[9][8];
    int registered = 0;
    for (int i = 0; i < 9; i++)
    {
        names[i][0] = 'd';
        names[i][1] = (char)('0' + i);
        names[i][2] = '\0';
        drv[i] = (SouthboundDriver){names[i], fake_read, NULL, NULL, NULL, &g_ctx};
        if (sb_add(&drv[i]) == SB_OK)
        {
            registered++;
        }
        else
        {
            TEST_ASSERT_EQUAL_INT(SB_ERR_FULL, sb_add(&drv[i]));
        }
    }

    TEST_ASSERT_EQUAL_size_t(8, sb_count());
    TEST_ASSERT_EQUAL_INT(8, registered);
}

void test_dispatch_not_found_guards()
{
    Southbound.clear(Southbound.internal);
    TEST_ASSERT_NULL(sb_find("nope"));
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_ERR_NOT_FOUND, sb_read("nope", 0, &v));
    TEST_ASSERT_EQUAL_INT(SB_ERR_NOT_FOUND, sb_write("nope", 0, 42));
}

void test_find_null_name(void)
{

    TEST_ASSERT_NULL(sb_find(NULL));
}

void test_read_missing_capability(void)
{

    SouthboundDriver wo = {"wo", NULL, fake_write, NULL, NULL, &g_ctx};
    TEST_ASSERT_EQUAL_INT(SB_OK, sb_add(&wo));
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_ERR_UNSUPPORTED, sb_read("wo", 0, &v));
}

void test_find_skips_driver_mutated_name_null(void)
{

    static SouthboundDriver mutable_drv = {"mutable", fake_read, NULL, NULL, NULL, &g_ctx};
    TEST_ASSERT_EQUAL_INT(SB_OK, sb_add(&mutable_drv));
    mutable_drv.name = NULL;
    TEST_ASSERT_NULL(sb_find("mutable"));
}

void test_block_not_found_and_arg_edges(void)
{
    sb_add(&g_full);
    int32_t out[2] = {0};
    int32_t in[2] = {1, 2};

    TEST_ASSERT_EQUAL_INT(SB_ERR_NOT_FOUND, sb_read_block("nope", 0, out, 2));
    TEST_ASSERT_EQUAL_INT(SB_ERR_NOT_FOUND, sb_write_block("nope", 0, in, 2));

    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_read_block("fake", 0, NULL, 2));
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_write_block("fake", 0, in, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_register_and_find);
    RUN_TEST(test_read_write_dispatch);
    RUN_TEST(test_block_atomic);
    RUN_TEST(test_unsupported_capability);
    RUN_TEST(test_registry_full);
    RUN_TEST(test_dispatch_not_found_guards);
    RUN_TEST(test_find_null_name);
    RUN_TEST(test_read_missing_capability);
    RUN_TEST(test_find_skips_driver_mutated_name_null);
    RUN_TEST(test_block_not_found_and_arg_edges);
    return UNITY_END();
}
