// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for mounted storage (server/filesystem/mnt) exercised the way every real caller
// reaches it - through the filesystem accessor - over the built-in RAM backend: read/write/
// append/truncate, whole-file helpers, exists/size/remove/rename, and the bounded fail-closed
// paths (file-full, pool/handle exhaustion, unmounted, undersized read buffer). The same API
// drives the Arduino FS backend on hardware.

#include "server/filesystem/filesystem.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp()
{
    pc_mnt_mount(pc_mnt_ram());
    pc_mnt_ram_format();
    pc_fs_begin("/"); // resolve request paths against the bare root, so they reach the backend as written
}
void tearDown()
{
}

void test_write_then_read_file()
{
    const char *msg = "hello vfs";
    TEST_ASSERT_TRUE(pc_fs_write_file("/a.txt", "", msg, strlen(msg)));
    TEST_ASSERT_TRUE(pc_fs_exists("/a.txt", ""));
    TEST_ASSERT_EQUAL_INT32((long)strlen(msg), pc_fs_size("/a.txt", ""));

    char buf[32];
    long n = pc_fs_read_file("/a.txt", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT32((long)strlen(msg), n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING(msg, buf);
}

void test_streamed_write_and_read()
{
    int h = pc_fs_open("/s.bin", "", pc_mnt_mode::PC_MNT_WRITE);
    TEST_ASSERT_TRUE(h >= 0);
    TEST_ASSERT_EQUAL_INT(3, pc_fs_write(h, "abc", 3));
    TEST_ASSERT_EQUAL_INT(3, pc_fs_write(h, "def", 3));
    pc_fs_close(h);
    TEST_ASSERT_EQUAL_INT32(6, pc_fs_size("/s.bin", ""));

    h = pc_fs_open("/s.bin", "", pc_mnt_mode::PC_MNT_READ);
    TEST_ASSERT_TRUE(h >= 0);
    char buf[8] = {0};
    TEST_ASSERT_EQUAL_INT(4, pc_fs_read(h, buf, 4));
    TEST_ASSERT_EQUAL_STRING("abcd", buf);
    TEST_ASSERT_EQUAL_INT(2, pc_fs_read(h, buf, 4)); // only 2 left
    TEST_ASSERT_EQUAL_INT(0, pc_fs_read(h, buf, 4)); // EOF
    pc_fs_close(h);
}

void test_write_mode_truncates()
{
    pc_fs_write_file("/t.txt", "", "longer original", 15);
    pc_fs_write_file("/t.txt", "", "short", 5);
    TEST_ASSERT_EQUAL_INT32(5, pc_fs_size("/t.txt", ""));
    char buf[16];
    long n = pc_fs_read_file("/t.txt", "", buf, sizeof(buf));
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("short", buf);
}

void test_append_extends()
{
    pc_fs_write_file("/log", "", "line1\n", 6);
    int h = pc_fs_open("/log", "", pc_mnt_mode::PC_MNT_APPEND);
    TEST_ASSERT_TRUE(h >= 0);
    pc_fs_write(h, "line2\n", 6);
    pc_fs_close(h);
    TEST_ASSERT_EQUAL_INT32(12, pc_fs_size("/log", ""));
    char buf[16];
    long n = pc_fs_read_file("/log", "", buf, sizeof(buf));
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("line1\nline2\n", buf);
}

void test_remove_and_rename()
{
    pc_fs_write_file("/old", "", "data", 4);
    TEST_ASSERT_TRUE(pc_fs_rename("/old", "", "/new", ""));
    TEST_ASSERT_FALSE(pc_fs_exists("/old", ""));
    TEST_ASSERT_TRUE(pc_fs_exists("/new", ""));
    char buf[8];
    long n = pc_fs_read_file("/new", "", buf, sizeof(buf));
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("data", buf);

    TEST_ASSERT_TRUE(pc_fs_remove("/new", ""));
    TEST_ASSERT_FALSE(pc_fs_exists("/new", ""));
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_size("/new", ""));
}

void test_missing_file_fails_closed()
{
    TEST_ASSERT_FALSE(pc_fs_exists("/nope", ""));
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_size("/nope", ""));
    TEST_ASSERT_TRUE(pc_fs_open("/nope", "", pc_mnt_mode::PC_MNT_READ) < 0);
    char buf[8];
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_read_file("/nope", "", buf, sizeof(buf)));
    TEST_ASSERT_FALSE(pc_fs_remove("/nope", ""));
    TEST_ASSERT_FALSE(pc_fs_rename("/nope", "", "/x", ""));
}

void test_read_buffer_too_small_fails_closed()
{
    pc_fs_write_file("/big", "", "0123456789", 10);
    char tiny[4];
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_read_file("/big", "", tiny, sizeof(tiny)));
}

void test_file_full_is_bounded()
{
    int h = pc_fs_open("/full", "", pc_mnt_mode::PC_MNT_WRITE);
    TEST_ASSERT_TRUE(h >= 0);
    static uint8_t chunk[256];
    memset(chunk, 'x', sizeof(chunk));
    size_t written = 0;
    for (int i = 0; i < 100; i++) // try to write far more than PC_MNT_RAM_FILE_SIZE
    {
        int w = pc_fs_write(h, chunk, sizeof(chunk));
        if (w <= 0)
        {
            break;
        }
        written += (size_t)w;
    }
    pc_fs_close(h);
    // Never exceeds the fixed per-file capacity (fail-closed, no overflow).
    TEST_ASSERT_EQUAL_INT32((long)PC_MNT_RAM_FILE_SIZE, pc_fs_size("/full", ""));
    TEST_ASSERT_EQUAL_UINT32(PC_MNT_RAM_FILE_SIZE, (uint32_t)written);
}

void test_file_pool_exhaustion()
{
    char name[16];
    for (int i = 0; i < PC_MNT_RAM_FILES; i++)
    {
        snprintf(name, sizeof(name), "/f%d", i);
        TEST_ASSERT_TRUE(pc_fs_write_file(name, "", "x", 1));
    }
    // One more distinct file must fail (pool full), not corrupt anything.
    TEST_ASSERT_FALSE(pc_fs_write_file("/overflow", "", "x", 1));
}

void test_handle_pool_exhaustion()
{
    pc_fs_write_file("/h", "", "data", 4);
    int handles[PC_MNT_MAX_OPEN];
    for (int i = 0; i < PC_MNT_MAX_OPEN; i++)
    {
        handles[i] = pc_fs_open("/h", "", pc_mnt_mode::PC_MNT_READ);
        TEST_ASSERT_TRUE(handles[i] >= 0);
    }
    TEST_ASSERT_TRUE(pc_fs_open("/h", "", pc_mnt_mode::PC_MNT_READ) < 0); // no handles left
    pc_fs_close(handles[0]);
    TEST_ASSERT_TRUE(pc_fs_open("/h", "", pc_mnt_mode::PC_MNT_READ) >= 0); // one freed
}

void test_unmounted_fails_closed()
{
    pc_mnt_mount(nullptr);
    TEST_ASSERT_TRUE(pc_fs_open("/a", "", pc_mnt_mode::PC_MNT_READ) < 0);
    TEST_ASSERT_FALSE(pc_fs_exists("/a", ""));
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_size("/a", ""));
    TEST_ASSERT_FALSE(pc_fs_write_file("/a", "", "x", 1));
}

void test_ram_guard_subconditions()
{
    pc_mnt_mount(pc_mnt_ram());
    pc_mnt_ram_format();
    uint8_t b[8] = {0};
    // Null path and an over-long name both fail closed on open.
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_open(nullptr, "", pc_mnt_mode::PC_MNT_WRITE));
    char longname[256];
    for (int i = 0; i < 255; i++)
    {
        longname[i] = 'a';
    }
    longname[255] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_open(longname, "", pc_mnt_mode::PC_MNT_WRITE));
    // Reads / writes / close on an out-of-range handle fail closed (no crash).
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_read(999, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_write(999, b, sizeof(b)));
    pc_fs_close(999);
    // read_file on a missing path fails closed.
    TEST_ASSERT_TRUE(pc_fs_read_file("/nope", "", b, sizeof(b)) < 0);
}

// Every dispatch entry point fails closed when no backend is mounted, and the
// dispatcher recovers once one is remounted.
void test_unmounted_all_entry_points()
{
    pc_mnt_mount(nullptr);
    uint8_t b[8] = {0};
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_read(0, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_write(0, b, sizeof(b)));
    pc_fs_close(0); // nothing to forward to: must be a no-op, not a crash
    TEST_ASSERT_FALSE(pc_fs_remove("/a", ""));
    TEST_ASSERT_FALSE(pc_fs_rename("/a", "", "/b", ""));
    TEST_ASSERT_TRUE(pc_fs_read_file("/a", "", b, sizeof(b)) < 0);
    pc_mnt_mount(pc_mnt_ram());
    TEST_ASSERT_TRUE(pc_fs_write_file("/a", "", "x", 1));
}

// A handle is valid only when it is non-negative, inside the pool, and still open.
void test_handle_validity_edges()
{
    uint8_t b[8] = {0};
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_read(-1, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_write(-1, b, sizeof(b)));
    pc_fs_close(-1); // negative handle: ignored

    TEST_ASSERT_TRUE(pc_fs_write_file("/hv", "", "data", 4));
    int h = pc_fs_open("/hv", "", pc_mnt_mode::PC_MNT_READ);
    TEST_ASSERT_TRUE(h >= 0);
    pc_fs_close(h);
    // In range, but no longer open.
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_read(h, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_write(h, b, sizeof(b)));
    TEST_ASSERT_TRUE(pc_fs_open("/hv", "", pc_mnt_mode::PC_MNT_READ) >= 0); // pool still usable
}

// A handle opened for reading refuses writes and the file is left untouched.
void test_write_to_read_handle_rejected()
{
    TEST_ASSERT_TRUE(pc_fs_write_file("/ro", "", "data", 4));
    int h = pc_fs_open("/ro", "", pc_mnt_mode::PC_MNT_READ);
    TEST_ASSERT_TRUE(h >= 0);
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_write(h, "xx", 2));
    pc_fs_close(h);
    TEST_ASSERT_EQUAL_INT32(4, pc_fs_size("/ro", ""));
}

// rename validates both names before touching the file pool.
void test_rename_argument_guards()
{
    TEST_ASSERT_TRUE(pc_fs_write_file("/r1", "", "data", 4));
    char longname[PC_MNT_NAME_MAX + 8];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    TEST_ASSERT_FALSE(pc_fs_rename(nullptr, "", "/r2", ""));
    TEST_ASSERT_FALSE(pc_fs_rename("/r1", "", nullptr, ""));
    TEST_ASSERT_FALSE(pc_fs_rename("/r1", "", longname, "")); // destination name too long
    TEST_ASSERT_TRUE(pc_fs_exists("/r1", ""));                // untouched by the rejected renames
    TEST_ASSERT_FALSE(pc_fs_exists("/r2", ""));
}

// Renaming onto an existing name frees that file and takes over its name.
void test_rename_overwrites_destination()
{
    TEST_ASSERT_TRUE(pc_fs_write_file("/src", "", "NEW", 3));
    TEST_ASSERT_TRUE(pc_fs_write_file("/dst", "", "oldcontent", 10));
    TEST_ASSERT_TRUE(pc_fs_rename("/src", "", "/dst", ""));
    TEST_ASSERT_FALSE(pc_fs_exists("/src", ""));
    TEST_ASSERT_TRUE(pc_fs_exists("/dst", ""));
    TEST_ASSERT_EQUAL_INT32(3, pc_fs_size("/dst", "")); // the source's contents won
    char buf[16];
    long n = pc_fs_read_file("/dst", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT32(3, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("NEW", buf);
}

// read_file fails when the file exists but no handle is left to open it with.
void test_read_file_handle_exhaustion()
{
    TEST_ASSERT_TRUE(pc_fs_write_file("/rf", "", "0123456789", 10));
    int handles[PC_MNT_MAX_OPEN];
    for (int i = 0; i < PC_MNT_MAX_OPEN; i++)
    {
        handles[i] = pc_fs_open("/rf", "", pc_mnt_mode::PC_MNT_READ);
        TEST_ASSERT_TRUE(handles[i] >= 0);
    }
    char buf[16];
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_read_file("/rf", "", buf, sizeof(buf)));
    for (int i = 0; i < PC_MNT_MAX_OPEN; i++)
    {
        pc_fs_close(handles[i]);
    }
    TEST_ASSERT_EQUAL_INT32(10, pc_fs_read_file("/rf", "", buf, sizeof(buf))); // works again
}

// write_file stops at the fixed per-file capacity and reports the short write.
void test_write_file_larger_than_capacity()
{
    static uint8_t big[PC_MNT_RAM_FILE_SIZE + 16];
    memset(big, 'z', sizeof(big));
    TEST_ASSERT_FALSE(pc_fs_write_file("/cap", "", big, sizeof(big)));
    TEST_ASSERT_EQUAL_INT32((long)PC_MNT_RAM_FILE_SIZE, pc_fs_size("/cap", ""));
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
static bool stall_seek(int handle, uint64_t off)
{
    (void)handle;
    (void)off;
    return true;
}
static bool stall_stat(const char *path, pc_mnt_stat *out)
{
    (void)path;
    out->is_dir = false;
    out->size = 8;
    out->mtime = 0;
    return true;
}
static int stall_opendir(const char *path)
{
    (void)path;
    return 0;
}
static bool stall_readdir(int handle, pc_mnt_stat *out, char *name, size_t name_cap)
{
    (void)handle;
    (void)out;
    (void)name;
    (void)name_cap;
    return false;
}
static const pc_mnt_backend s_stall_backend = {stall_open, stall_read, stall_write,   stall_close,  stall_seek,
                                               stall_size, stall_true, stall_true,    stall_rename, stall_true,
                                               stall_true, stall_stat, stall_opendir, stall_readdir};

void test_zero_progress_backend_terminates()
{
    pc_mnt_mount(&s_stall_backend);
    char buf[16];
    TEST_ASSERT_EQUAL_INT32(0, pc_fs_read_file("/x", "", buf, sizeof(buf))); // gave up at 0 bytes
    TEST_ASSERT_FALSE(pc_fs_write_file("/x", "", "abcd", 4));                // no progress -> failure
    pc_mnt_mount(pc_mnt_ram());
    TEST_ASSERT_TRUE(pc_fs_write_file("/x", "", "abcd", 4)); // real backend still fine
}

// docs/BUGS.md: a root without a trailing slash used to concatenate - pc_ssh_sftp_begin(fs,
// "/gcode") resolved "/part.nc" to "/gcodepart.nc", a sibling of the mount rather than a file in
// it. pc_fs_begin owns the root, so it adds the separator once instead of every join assuming it.
void test_root_without_trailing_slash()
{
    pc_fs_begin("/gcode"); // the documented form, and the one that used to concatenate
    TEST_ASSERT_TRUE(pc_fs_write_file("/p.nc", "", "G0", 2));
    TEST_ASSERT_EQUAL_INT32(2, pc_fs_size("/p.nc", ""));
    // The file landed inside the root, not glued onto its name.
    pc_fs_begin("/");
    TEST_ASSERT_TRUE(pc_fs_exists("/gcode/p.nc", ""));
    TEST_ASSERT_FALSE(pc_fs_exists("/gcodep.nc", ""));

    // A root that already carries the separator is unchanged (no "//").
    pc_fs_begin("/gcode/");
    TEST_ASSERT_TRUE(pc_fs_exists("/p.nc", ""));
    pc_fs_begin("/");
}

// A directory destination takes the leaf; a file destination is the whole path. One frame either
// way - this is the shape SCP resolves a received filename with.
void test_leaf_joins_onto_a_directory()
{
    pc_fs_begin("/");
    TEST_ASSERT_TRUE(pc_fs_mkdir("/d", ""));
    TEST_ASSERT_TRUE(pc_fs_write_file("/d/", "f.txt", "xy", 2));
    TEST_ASSERT_TRUE(pc_fs_exists("/d/f.txt", ""));
    TEST_ASSERT_EQUAL_INT32(2, pc_fs_size("/d/", "f.txt"));
    // Traversal is refused in the leaf as well as the dir.
    TEST_ASSERT_FALSE(pc_fs_write_file("/d/", "../esc", "x", 1));
    TEST_ASSERT_FALSE(pc_fs_write_file("/../d/", "f.txt", "x", 1));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_root_without_trailing_slash);
    RUN_TEST(test_leaf_joins_onto_a_directory);
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
