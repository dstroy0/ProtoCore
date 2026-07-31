// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/southbound: the driver registry + name-dispatched read/write facade.

#include "services/net/southbound/southbound.h"
#include <unity.h>

// A fake driver backed by a small register file, to prove dispatch reaches the right ctx.
struct FakeCtx
{
    int32_t regs[16];
    int fail_next; // if nonzero, the next read/write returns this negative code once.
};

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
        return Sb::SB_ERR_ARG;
    }
    *out = f->regs[p];
    return Sb::SB_OK;
}

static int fake_write(void *ctx, uint32_t p, int32_t v)
{
    FakeCtx *f = (FakeCtx *)ctx;
    if (p >= 16)
    {
        return Sb::SB_ERR_ARG;
    }
    f->regs[p] = v;
    return Sb::SB_OK;
}

static int fake_read_block(void *ctx, uint32_t first, int32_t *out, size_t n)
{
    FakeCtx *f = (FakeCtx *)ctx;
    if (first + n > 16)
    {
        return Sb::SB_ERR_ARG;
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
        return Sb::SB_ERR_ARG;
    }
    for (size_t i = 0; i < n; i++)
    {
        f->regs[first + i] = in[i];
    }
    return (int)n;
}

static FakeCtx g_ctx;
static SouthboundDriver g_full = {"fake", fake_read, fake_write, fake_read_block, fake_write_block, &g_ctx};

void setUp(void)
{
    pc_southbound_clear();
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
    TEST_ASSERT_EQUAL_INT(Sb::SB_OK, pc_southbound_register(&g_full));
    TEST_ASSERT_EQUAL_size_t(1, pc_southbound_count());
    TEST_ASSERT_EQUAL_PTR(&g_full, pc_southbound_find("fake"));
    TEST_ASSERT_NULL(pc_southbound_find("nope"));
    // Duplicate name rejected.
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_DUP, pc_southbound_register(&g_full));
    // Bad args.
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_ARG, pc_southbound_register(nullptr));
    SouthboundDriver noname = {nullptr, fake_read, nullptr, nullptr, nullptr, nullptr};
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_ARG, pc_southbound_register(&noname));
}

void test_read_write_dispatch(void)
{
    pc_southbound_register(&g_full);
    int32_t v = -1;
    TEST_ASSERT_EQUAL_INT(Sb::SB_OK, pc_southbound_read("fake", 3, &v));
    TEST_ASSERT_EQUAL_INT32(30, v);
    TEST_ASSERT_EQUAL_INT(Sb::SB_OK, pc_southbound_write("fake", 3, 999));
    TEST_ASSERT_EQUAL_INT(Sb::SB_OK, pc_southbound_read("fake", 3, &v));
    TEST_ASSERT_EQUAL_INT32(999, v);
    // Unknown driver.
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_NOT_FOUND, pc_southbound_read("x", 0, &v));
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_NOT_FOUND, pc_southbound_write("x", 0, 0));
    // Null out.
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_ARG, pc_southbound_read("fake", 0, nullptr));
    // Driver transport error propagated unchanged.
    g_ctx.fail_next = -42;
    TEST_ASSERT_EQUAL_INT(-42, pc_southbound_read("fake", 0, &v));
}

void test_block_atomic(void)
{
    pc_southbound_register(&g_full);
    int32_t out[4] = {0};
    TEST_ASSERT_EQUAL_INT(4, pc_southbound_read_block("fake", 2, out, 4));
    TEST_ASSERT_EQUAL_INT32(20, out[0]);
    TEST_ASSERT_EQUAL_INT32(50, out[3]);
    int32_t in[3] = {7, 8, 9};
    TEST_ASSERT_EQUAL_INT(3, pc_southbound_write_block("fake", 5, in, 3));
    int32_t v = 0;
    pc_southbound_read("fake", 6, &v);
    TEST_ASSERT_EQUAL_INT32(8, v);
    // Zero count / null rejected.
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_ARG, pc_southbound_read_block("fake", 0, out, 0));
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_ARG, pc_southbound_write_block("fake", 0, nullptr, 3));
}

void test_unsupported_capability(void)
{
    // A driver that only implements single-point read.
    SouthboundDriver ro = {"ro", fake_read, nullptr, nullptr, nullptr, &g_ctx};
    pc_southbound_register(&ro);
    int32_t v = 0, out[2] = {0};
    TEST_ASSERT_EQUAL_INT(Sb::SB_OK, pc_southbound_read("ro", 0, &v));
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_UNSUPPORTED, pc_southbound_write("ro", 0, 1));
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_UNSUPPORTED, pc_southbound_read_block("ro", 0, out, 2));
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_UNSUPPORTED, pc_southbound_write_block("ro", 0, out, 2));
}

void test_registry_full(void)
{
    // Fill the registry with distinct-named drivers, then overflow.
    static SouthboundDriver drv[9];
    static char names[9][8];
    int registered = 0;
    for (int i = 0; i < 9; i++)
    {
        names[i][0] = 'd';
        names[i][1] = (char)('0' + i);
        names[i][2] = '\0';
        drv[i] = {names[i], fake_read, nullptr, nullptr, nullptr, &g_ctx};
        if (pc_southbound_register(&drv[i]) == Sb::SB_OK)
        {
            registered++;
        }
        else
        {
            TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_FULL, pc_southbound_register(&drv[i]));
        }
    }
    // Default cap is 8.
    TEST_ASSERT_EQUAL_size_t(8, pc_southbound_count());
    TEST_ASSERT_EQUAL_INT(8, registered);
}

void test_dispatch_not_found_guards()
{
    pc_southbound_clear();
    TEST_ASSERT_NULL(pc_southbound_find("nope")); // not registered -> null
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_NOT_FOUND, pc_southbound_read("nope", 0, &v));  // no driver
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_NOT_FOUND, pc_southbound_write("nope", 0, 42)); // no driver
}

void test_find_null_name(void)
{
    // pc_southbound_find's own null-name guard, independent of any dispatch caller.
    TEST_ASSERT_NULL(pc_southbound_find(nullptr));
}

void test_read_missing_capability(void)
{
    // A driver that implements write but not read, to hit pc_southbound_read's
    // "d->read == nullptr" guard (distinct from the read-side "ro" driver above).
    SouthboundDriver wo = {"wo", nullptr, fake_write, nullptr, nullptr, &g_ctx};
    TEST_ASSERT_EQUAL_INT(Sb::SB_OK, pc_southbound_register(&wo));
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_UNSUPPORTED, pc_southbound_read("wo", 0, &v));
}

void test_find_skips_driver_mutated_name_null(void)
{
    // pc_southbound_find() stores a *borrowed* pointer (const SouthboundDriver *), not a copy: the
    // registry has no control over the pointed-to object after register() returns. register()'s own
    // null-name guard only proves drv->name was non-null at registration time; it says nothing about
    // later mutation through the caller's own (non-const) handle to that same object. So find()'s
    // "s_sb.drivers[i]->name &&" guard is a live defense against exactly that: a driver whose name
    // field goes null out from under the registry between register() and find().
    static SouthboundDriver mutable_drv = {"mutable", fake_read, nullptr, nullptr, nullptr, &g_ctx};
    TEST_ASSERT_EQUAL_INT(Sb::SB_OK, pc_southbound_register(&mutable_drv));
    mutable_drv.name = nullptr; // mutate the borrowed driver's name field after registration.
    TEST_ASSERT_NULL(pc_southbound_find("mutable"));
}

void test_block_not_found_and_arg_edges(void)
{
    pc_southbound_register(&g_full);
    int32_t out[2] = {0};
    int32_t in[2] = {1, 2};
    // Unknown driver name reaches the block paths' own "not found" guard.
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_NOT_FOUND, pc_southbound_read_block("nope", 0, out, 2));
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_NOT_FOUND, pc_southbound_write_block("nope", 0, in, 2));
    // Null out pointer (read_block) and zero count with a valid pointer (write_block).
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_ARG, pc_southbound_read_block("fake", 0, nullptr, 2));
    TEST_ASSERT_EQUAL_INT(Sb::SB_ERR_ARG, pc_southbound_write_block("fake", 0, in, 0));
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
