// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the unified VFS (services/storage/vfs) exercised through its built-in
// RAM backend: read/write/append/truncate, whole-file helpers, exists/size/
// remove/rename, and the bounded fail-closed paths (file-full, pool/handle
// exhaustion, unmounted, undersized read buffer). The same API drives the
// Arduino FS backend on hardware.

#include "services/storage/vfs/vfs.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp()
{
    pc_vfs_mount(pc_vfs_ram());
    pc_vfs_ram_format();
}
void tearDown()
{
}

void test_write_then_read_file()
{
    const char *msg = "hello vfs";
    TEST_ASSERT_TRUE(pc_vfs_write_file("/a.txt", msg, strlen(msg)));
    TEST_ASSERT_TRUE(pc_vfs_exists("/a.txt"));
    TEST_ASSERT_EQUAL_INT32((long)strlen(msg), pc_vfs_size("/a.txt"));

    char buf[32];
    long n = pc_vfs_read_file("/a.txt", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT32((long)strlen(msg), n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING(msg, buf);
}

void test_streamed_write_and_read()
{
    int h = pc_vfs_open("/s.bin", pc_vfs_mode::PC_VFS_WRITE);
    TEST_ASSERT_TRUE(h >= 0);
    TEST_ASSERT_EQUAL_INT(3, pc_vfs_write(h, "abc", 3));
    TEST_ASSERT_EQUAL_INT(3, pc_vfs_write(h, "def", 3));
    pc_vfs_close(h);
    TEST_ASSERT_EQUAL_INT32(6, pc_vfs_size("/s.bin"));

    h = pc_vfs_open("/s.bin", pc_vfs_mode::PC_VFS_READ);
    TEST_ASSERT_TRUE(h >= 0);
    char buf[8] = {0};
    TEST_ASSERT_EQUAL_INT(4, pc_vfs_read(h, buf, 4));
    TEST_ASSERT_EQUAL_STRING("abcd", buf);
    TEST_ASSERT_EQUAL_INT(2, pc_vfs_read(h, buf, 4)); // only 2 left
    TEST_ASSERT_EQUAL_INT(0, pc_vfs_read(h, buf, 4)); // EOF
    pc_vfs_close(h);
}

void test_write_mode_truncates()
{
    pc_vfs_write_file("/t.txt", "longer original", 15);
    pc_vfs_write_file("/t.txt", "short", 5);
    TEST_ASSERT_EQUAL_INT32(5, pc_vfs_size("/t.txt"));
    char buf[16];
    long n = pc_vfs_read_file("/t.txt", buf, sizeof(buf));
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("short", buf);
}

void test_append_extends()
{
    pc_vfs_write_file("/log", "line1\n", 6);
    int h = pc_vfs_open("/log", pc_vfs_mode::PC_VFS_APPEND);
    TEST_ASSERT_TRUE(h >= 0);
    pc_vfs_write(h, "line2\n", 6);
    pc_vfs_close(h);
    TEST_ASSERT_EQUAL_INT32(12, pc_vfs_size("/log"));
    char buf[16];
    long n = pc_vfs_read_file("/log", buf, sizeof(buf));
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("line1\nline2\n", buf);
}

void test_remove_and_rename()
{
    pc_vfs_write_file("/old", "data", 4);
    TEST_ASSERT_TRUE(pc_vfs_rename("/old", "/new"));
    TEST_ASSERT_FALSE(pc_vfs_exists("/old"));
    TEST_ASSERT_TRUE(pc_vfs_exists("/new"));
    char buf[8];
    long n = pc_vfs_read_file("/new", buf, sizeof(buf));
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("data", buf);

    TEST_ASSERT_TRUE(pc_vfs_remove("/new"));
    TEST_ASSERT_FALSE(pc_vfs_exists("/new"));
    TEST_ASSERT_EQUAL_INT32(-1, pc_vfs_size("/new"));
}

void test_missing_file_fails_closed()
{
    TEST_ASSERT_FALSE(pc_vfs_exists("/nope"));
    TEST_ASSERT_EQUAL_INT32(-1, pc_vfs_size("/nope"));
    TEST_ASSERT_TRUE(pc_vfs_open("/nope", pc_vfs_mode::PC_VFS_READ) < 0);
    char buf[8];
    TEST_ASSERT_EQUAL_INT32(-1, pc_vfs_read_file("/nope", buf, sizeof(buf)));
    TEST_ASSERT_FALSE(pc_vfs_remove("/nope"));
    TEST_ASSERT_FALSE(pc_vfs_rename("/nope", "/x"));
}

void test_read_buffer_too_small_fails_closed()
{
    pc_vfs_write_file("/big", "0123456789", 10);
    char tiny[4];
    TEST_ASSERT_EQUAL_INT32(-1, pc_vfs_read_file("/big", tiny, sizeof(tiny)));
}

void test_file_full_is_bounded()
{
    int h = pc_vfs_open("/full", pc_vfs_mode::PC_VFS_WRITE);
    TEST_ASSERT_TRUE(h >= 0);
    static uint8_t chunk[256];
    memset(chunk, 'x', sizeof(chunk));
    size_t written = 0;
    for (int i = 0; i < 100; i++) // try to write far more than PC_VFS_RAM_FILE_SIZE
    {
        int w = pc_vfs_write(h, chunk, sizeof(chunk));
        if (w <= 0)
        {
            break;
        }
        written += (size_t)w;
    }
    pc_vfs_close(h);
    // Never exceeds the fixed per-file capacity (fail-closed, no overflow).
    TEST_ASSERT_EQUAL_INT32((long)PC_VFS_RAM_FILE_SIZE, pc_vfs_size("/full"));
    TEST_ASSERT_EQUAL_UINT32(PC_VFS_RAM_FILE_SIZE, (uint32_t)written);
}

void test_file_pool_exhaustion()
{
    char name[16];
    for (int i = 0; i < PC_VFS_RAM_FILES; i++)
    {
        snprintf(name, sizeof(name), "/f%d", i);
        TEST_ASSERT_TRUE(pc_vfs_write_file(name, "x", 1));
    }
    // One more distinct file must fail (pool full), not corrupt anything.
    TEST_ASSERT_FALSE(pc_vfs_write_file("/overflow", "x", 1));
}

void test_handle_pool_exhaustion()
{
    pc_vfs_write_file("/h", "data", 4);
    int handles[PC_VFS_MAX_OPEN];
    for (int i = 0; i < PC_VFS_MAX_OPEN; i++)
    {
        handles[i] = pc_vfs_open("/h", pc_vfs_mode::PC_VFS_READ);
        TEST_ASSERT_TRUE(handles[i] >= 0);
    }
    TEST_ASSERT_TRUE(pc_vfs_open("/h", pc_vfs_mode::PC_VFS_READ) < 0); // no handles left
    pc_vfs_close(handles[0]);
    TEST_ASSERT_TRUE(pc_vfs_open("/h", pc_vfs_mode::PC_VFS_READ) >= 0); // one freed
}

void test_unmounted_fails_closed()
{
    pc_vfs_mount(nullptr);
    TEST_ASSERT_TRUE(pc_vfs_open("/a", pc_vfs_mode::PC_VFS_READ) < 0);
    TEST_ASSERT_FALSE(pc_vfs_exists("/a"));
    TEST_ASSERT_EQUAL_INT32(-1, pc_vfs_size("/a"));
    TEST_ASSERT_FALSE(pc_vfs_write_file("/a", "x", 1));
}

void test_ram_guard_subconditions()
{
    pc_vfs_mount(pc_vfs_ram());
    pc_vfs_ram_format();
    uint8_t b[8] = {0};
    // Null path and an over-long name both fail closed on open.
    TEST_ASSERT_EQUAL_INT(-1, pc_vfs_open(nullptr, pc_vfs_mode::PC_VFS_WRITE));
    char longname[256];
    for (int i = 0; i < 255; i++)
    {
        longname[i] = 'a';
    }
    longname[255] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pc_vfs_open(longname, pc_vfs_mode::PC_VFS_WRITE));
    // Reads / writes / close on an out-of-range handle fail closed (no crash).
    TEST_ASSERT_EQUAL_INT(-1, pc_vfs_read(999, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, pc_vfs_write(999, b, sizeof(b)));
    pc_vfs_close(999);
    // read_file on a missing path fails closed.
    TEST_ASSERT_TRUE(pc_vfs_read_file("/nope", b, sizeof(b)) < 0);
}

// Every dispatch entry point fails closed when no backend is mounted, and the
// dispatcher recovers once one is remounted.
void test_unmounted_all_entry_points()
{
    pc_vfs_mount(nullptr);
    uint8_t b[8] = {0};
    TEST_ASSERT_EQUAL_INT(-1, pc_vfs_read(0, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, pc_vfs_write(0, b, sizeof(b)));
    pc_vfs_close(0); // nothing to forward to: must be a no-op, not a crash
    TEST_ASSERT_FALSE(pc_vfs_remove("/a"));
    TEST_ASSERT_FALSE(pc_vfs_rename("/a", "/b"));
    TEST_ASSERT_TRUE(pc_vfs_read_file("/a", b, sizeof(b)) < 0);
    pc_vfs_mount(pc_vfs_ram());
    TEST_ASSERT_TRUE(pc_vfs_write_file("/a", "x", 1));
}

// A handle is valid only when it is non-negative, inside the pool, and still open.
void test_handle_validity_edges()
{
    uint8_t b[8] = {0};
    TEST_ASSERT_EQUAL_INT(-1, pc_vfs_read(-1, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, pc_vfs_write(-1, b, sizeof(b)));
    pc_vfs_close(-1); // negative handle: ignored

    TEST_ASSERT_TRUE(pc_vfs_write_file("/hv", "data", 4));
    int h = pc_vfs_open("/hv", pc_vfs_mode::PC_VFS_READ);
    TEST_ASSERT_TRUE(h >= 0);
    pc_vfs_close(h);
    // In range, but no longer open.
    TEST_ASSERT_EQUAL_INT(-1, pc_vfs_read(h, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, pc_vfs_write(h, b, sizeof(b)));
    TEST_ASSERT_TRUE(pc_vfs_open("/hv", pc_vfs_mode::PC_VFS_READ) >= 0); // pool still usable
}

// A handle opened for reading refuses writes and the file is left untouched.
void test_write_to_read_handle_rejected()
{
    TEST_ASSERT_TRUE(pc_vfs_write_file("/ro", "data", 4));
    int h = pc_vfs_open("/ro", pc_vfs_mode::PC_VFS_READ);
    TEST_ASSERT_TRUE(h >= 0);
    TEST_ASSERT_EQUAL_INT(-1, pc_vfs_write(h, "xx", 2));
    pc_vfs_close(h);
    TEST_ASSERT_EQUAL_INT32(4, pc_vfs_size("/ro"));
}

// rename validates both names before touching the file pool.
void test_rename_argument_guards()
{
    TEST_ASSERT_TRUE(pc_vfs_write_file("/r1", "data", 4));
    char longname[PC_VFS_NAME_MAX + 8];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    TEST_ASSERT_FALSE(pc_vfs_rename(nullptr, "/r2"));
    TEST_ASSERT_FALSE(pc_vfs_rename("/r1", nullptr));
    TEST_ASSERT_FALSE(pc_vfs_rename("/r1", longname)); // destination name too long
    TEST_ASSERT_TRUE(pc_vfs_exists("/r1"));            // untouched by the rejected renames
    TEST_ASSERT_FALSE(pc_vfs_exists("/r2"));
}

// Renaming onto an existing name frees that file and takes over its name.
void test_rename_overwrites_destination()
{
    TEST_ASSERT_TRUE(pc_vfs_write_file("/src", "NEW", 3));
    TEST_ASSERT_TRUE(pc_vfs_write_file("/dst", "oldcontent", 10));
    TEST_ASSERT_TRUE(pc_vfs_rename("/src", "/dst"));
    TEST_ASSERT_FALSE(pc_vfs_exists("/src"));
    TEST_ASSERT_TRUE(pc_vfs_exists("/dst"));
    TEST_ASSERT_EQUAL_INT32(3, pc_vfs_size("/dst")); // the source's contents won
    char buf[16];
    long n = pc_vfs_read_file("/dst", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT32(3, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("NEW", buf);
}

// read_file fails when the file exists but no handle is left to open it with.
void test_read_file_handle_exhaustion()
{
    TEST_ASSERT_TRUE(pc_vfs_write_file("/rf", "0123456789", 10));
    int handles[PC_VFS_MAX_OPEN];
    for (int i = 0; i < PC_VFS_MAX_OPEN; i++)
    {
        handles[i] = pc_vfs_open("/rf", pc_vfs_mode::PC_VFS_READ);
        TEST_ASSERT_TRUE(handles[i] >= 0);
    }
    char buf[16];
    TEST_ASSERT_EQUAL_INT32(-1, pc_vfs_read_file("/rf", buf, sizeof(buf)));
    for (int i = 0; i < PC_VFS_MAX_OPEN; i++)
    {
        pc_vfs_close(handles[i]);
    }
    TEST_ASSERT_EQUAL_INT32(10, pc_vfs_read_file("/rf", buf, sizeof(buf))); // works again
}

// write_file stops at the fixed per-file capacity and reports the short write.
void test_write_file_larger_than_capacity()
{
    static uint8_t big[PC_VFS_RAM_FILE_SIZE + 16];
    memset(big, 'z', sizeof(big));
    TEST_ASSERT_FALSE(pc_vfs_write_file("/cap", big, sizeof(big)));
    TEST_ASSERT_EQUAL_INT32((long)PC_VFS_RAM_FILE_SIZE, pc_vfs_size("/cap"));
}

// A backend that always reports "0 bytes transferred": the whole-file helpers must
// give up rather than spin forever, and report the shortfall.
static int stall_open(const char *path, int mode)
{
    (void)path;
    (void)mode;
    return 0;
}
static int stall_read(int handle, void *buf, size_t n)
{
    (void)handle;
    (void)buf;
    (void)n;
    return 0;
}
static int stall_write(int handle, const void *buf, size_t n)
{
    (void)handle;
    (void)buf;
    (void)n;
    return 0;
}
static void stall_close(int handle)
{
    (void)handle;
}
static long stall_size(const char *path)
{
    (void)path;
    return 8;
}
static bool stall_true(const char *path)
{
    (void)path;
    return true;
}
static bool stall_rename(const char *from, const char *to)
{
    (void)from;
    (void)to;
    return true;
}
static const pc_vfs_backend s_stall_backend = {stall_open, stall_read, stall_write, stall_close,
                                               stall_size, stall_true, stall_true,  stall_rename};

void test_zero_progress_backend_terminates()
{
    pc_vfs_mount(&s_stall_backend);
    char buf[16];
    TEST_ASSERT_EQUAL_INT32(0, pc_vfs_read_file("/x", buf, sizeof(buf))); // gave up at 0 bytes
    TEST_ASSERT_FALSE(pc_vfs_write_file("/x", "abcd", 4));                // no progress -> failure
    pc_vfs_mount(pc_vfs_ram());
    TEST_ASSERT_TRUE(pc_vfs_write_file("/x", "abcd", 4)); // real backend still fine
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_write_then_read_file);
    RUN_TEST(test_streamed_write_and_read);
    RUN_TEST(test_write_mode_truncates);
    RUN_TEST(test_append_extends);
    RUN_TEST(test_remove_and_rename);
    RUN_TEST(test_missing_file_fails_closed);
    RUN_TEST(test_read_buffer_too_small_fails_closed);
    RUN_TEST(test_file_full_is_bounded);
    RUN_TEST(test_file_pool_exhaustion);
    RUN_TEST(test_handle_pool_exhaustion);
    RUN_TEST(test_unmounted_fails_closed);
    RUN_TEST(test_ram_guard_subconditions);
    RUN_TEST(test_unmounted_all_entry_points);
    RUN_TEST(test_handle_validity_edges);
    RUN_TEST(test_write_to_read_handle_rejected);
    RUN_TEST(test_rename_argument_guards);
    RUN_TEST(test_rename_overwrites_destination);
    RUN_TEST(test_read_file_handle_exhaustion);
    RUN_TEST(test_write_file_larger_than_capacity);
    RUN_TEST(test_zero_progress_backend_terminates);
    return UNITY_END();
}
