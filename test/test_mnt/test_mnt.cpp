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

// The root every test resolves against. pc_fs_begin hands back a handle now, and a service is
// expected to keep it: that is what lets two of them sit over different storage. Re-binding a name
// already bound returns the same handle, so calling this each setUp costs one root, not one per test.
static int s_root;

void setUp()
{
    pc_mnt_mount(pc_mnt_ram());
    pc_mnt_ram_format();
    s_root = pc_fs_begin("/"); // resolve request paths against the bare root, so they reach the backend as written
}
void tearDown()
{
}

void test_write_then_read_file()
{
    const char *msg = "hello vfs";
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/a.txt", "", msg, strlen(msg)));
    TEST_ASSERT_TRUE(pc_fs_exists(s_root, "/a.txt", ""));
    TEST_ASSERT_EQUAL_INT32((long)strlen(msg), pc_fs_size(s_root, "/a.txt", ""));

    char buf[32];
    long n = pc_fs_read_file(s_root, "/a.txt", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT32((long)strlen(msg), n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING(msg, buf);
}

void test_streamed_write_and_read()
{
    int h = pc_fs_open(s_root, "/s.bin", "", pc_mnt_mode::PC_MNT_WRITE);
    TEST_ASSERT_TRUE(h >= 0);
    TEST_ASSERT_EQUAL_INT(3, pc_fs_write(h, "abc", 3));
    TEST_ASSERT_EQUAL_INT(3, pc_fs_write(h, "def", 3));
    pc_fs_close(h);
    TEST_ASSERT_EQUAL_INT32(6, pc_fs_size(s_root, "/s.bin", ""));

    h = pc_fs_open(s_root, "/s.bin", "", pc_mnt_mode::PC_MNT_READ);
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
    pc_fs_write_file(s_root, "/t.txt", "", "longer original", 15);
    pc_fs_write_file(s_root, "/t.txt", "", "short", 5);
    TEST_ASSERT_EQUAL_INT32(5, pc_fs_size(s_root, "/t.txt", ""));
    char buf[16];
    long n = pc_fs_read_file(s_root, "/t.txt", "", buf, sizeof(buf));
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("short", buf);
}

void test_append_extends()
{
    pc_fs_write_file(s_root, "/log", "", "line1\n", 6);
    int h = pc_fs_open(s_root, "/log", "", pc_mnt_mode::PC_MNT_APPEND);
    TEST_ASSERT_TRUE(h >= 0);
    pc_fs_write(h, "line2\n", 6);
    pc_fs_close(h);
    TEST_ASSERT_EQUAL_INT32(12, pc_fs_size(s_root, "/log", ""));
    char buf[16];
    long n = pc_fs_read_file(s_root, "/log", "", buf, sizeof(buf));
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("line1\nline2\n", buf);
}

void test_remove_and_rename()
{
    pc_fs_write_file(s_root, "/old", "", "data", 4);
    TEST_ASSERT_TRUE(pc_fs_rename(s_root, "/old", "", "/new", ""));
    TEST_ASSERT_FALSE(pc_fs_exists(s_root, "/old", ""));
    TEST_ASSERT_TRUE(pc_fs_exists(s_root, "/new", ""));
    char buf[8];
    long n = pc_fs_read_file(s_root, "/new", "", buf, sizeof(buf));
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("data", buf);

    TEST_ASSERT_TRUE(pc_fs_remove(s_root, "/new", ""));
    TEST_ASSERT_FALSE(pc_fs_exists(s_root, "/new", ""));
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_size(s_root, "/new", ""));
}

void test_missing_file_fails_closed()
{
    TEST_ASSERT_FALSE(pc_fs_exists(s_root, "/nope", ""));
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_size(s_root, "/nope", ""));
    TEST_ASSERT_TRUE(pc_fs_open(s_root, "/nope", "", pc_mnt_mode::PC_MNT_READ) < 0);
    char buf[8];
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_read_file(s_root, "/nope", "", buf, sizeof(buf)));
    TEST_ASSERT_FALSE(pc_fs_remove(s_root, "/nope", ""));
    TEST_ASSERT_FALSE(pc_fs_rename(s_root, "/nope", "", "/x", ""));
}

void test_read_buffer_too_small_fails_closed()
{
    pc_fs_write_file(s_root, "/big", "", "0123456789", 10);
    char tiny[4];
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_read_file(s_root, "/big", "", tiny, sizeof(tiny)));
}

void test_file_full_is_bounded()
{
    int h = pc_fs_open(s_root, "/full", "", pc_mnt_mode::PC_MNT_WRITE);
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
    TEST_ASSERT_EQUAL_INT32((long)PC_MNT_RAM_FILE_SIZE, pc_fs_size(s_root, "/full", ""));
    TEST_ASSERT_EQUAL_UINT32(PC_MNT_RAM_FILE_SIZE, (uint32_t)written);
}

void test_file_pool_exhaustion()
{
    char name[16];
    for (int i = 0; i < PC_MNT_RAM_FILES; i++)
    {
        snprintf(name, sizeof(name), "/f%d", i);
        TEST_ASSERT_TRUE(pc_fs_write_file(s_root, name, "", "x", 1));
    }
    // One more distinct file must fail (pool full), not corrupt anything.
    TEST_ASSERT_FALSE(pc_fs_write_file(s_root, "/overflow", "", "x", 1));
}

void test_handle_pool_exhaustion()
{
    pc_fs_write_file(s_root, "/h", "", "data", 4);
    int handles[PC_MNT_MAX_OPEN];
    for (int i = 0; i < PC_MNT_MAX_OPEN; i++)
    {
        handles[i] = pc_fs_open(s_root, "/h", "", pc_mnt_mode::PC_MNT_READ);
        TEST_ASSERT_TRUE(handles[i] >= 0);
    }
    TEST_ASSERT_TRUE(pc_fs_open(s_root, "/h", "", pc_mnt_mode::PC_MNT_READ) < 0); // no handles left
    pc_fs_close(handles[0]);
    TEST_ASSERT_TRUE(pc_fs_open(s_root, "/h", "", pc_mnt_mode::PC_MNT_READ) >= 0); // one freed
}

void test_unmounted_fails_closed()
{
    pc_mnt_mount(nullptr);
    TEST_ASSERT_TRUE(pc_fs_open(s_root, "/a", "", pc_mnt_mode::PC_MNT_READ) < 0);
    TEST_ASSERT_FALSE(pc_fs_exists(s_root, "/a", ""));
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_size(s_root, "/a", ""));
    TEST_ASSERT_FALSE(pc_fs_write_file(s_root, "/a", "", "x", 1));
}

void test_ram_guard_subconditions()
{
    pc_mnt_mount(pc_mnt_ram());
    pc_mnt_ram_format();
    uint8_t b[8] = {0};
    // Null path and an over-long name both fail closed on open.
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_open(s_root, nullptr, "", pc_mnt_mode::PC_MNT_WRITE));
    char longname[256];
    for (int i = 0; i < 255; i++)
    {
        longname[i] = 'a';
    }
    longname[255] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_open(s_root, longname, "", pc_mnt_mode::PC_MNT_WRITE));
    // Reads / writes / close on an out-of-range handle fail closed (no crash).
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_read(999, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_write(999, b, sizeof(b)));
    pc_fs_close(999);
    // read_file on a missing path fails closed.
    TEST_ASSERT_TRUE(pc_fs_read_file(s_root, "/nope", "", b, sizeof(b)) < 0);
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
    TEST_ASSERT_FALSE(pc_fs_remove(s_root, "/a", ""));
    TEST_ASSERT_FALSE(pc_fs_rename(s_root, "/a", "", "/b", ""));
    TEST_ASSERT_TRUE(pc_fs_read_file(s_root, "/a", "", b, sizeof(b)) < 0);
    pc_mnt_mount(pc_mnt_ram());
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/a", "", "x", 1));
}

// A handle is valid only when it is non-negative, inside the pool, and still open.
void test_handle_validity_edges()
{
    uint8_t b[8] = {0};
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_read(-1, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_write(-1, b, sizeof(b)));
    pc_fs_close(-1); // negative handle: ignored

    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/hv", "", "data", 4));
    int h = pc_fs_open(s_root, "/hv", "", pc_mnt_mode::PC_MNT_READ);
    TEST_ASSERT_TRUE(h >= 0);
    pc_fs_close(h);
    // In range, but no longer open.
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_read(h, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_write(h, b, sizeof(b)));
    TEST_ASSERT_TRUE(pc_fs_open(s_root, "/hv", "", pc_mnt_mode::PC_MNT_READ) >= 0); // pool still usable
}

// A handle opened for reading refuses writes and the file is left untouched.
void test_write_to_read_handle_rejected()
{
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/ro", "", "data", 4));
    int h = pc_fs_open(s_root, "/ro", "", pc_mnt_mode::PC_MNT_READ);
    TEST_ASSERT_TRUE(h >= 0);
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_write(h, "xx", 2));
    pc_fs_close(h);
    TEST_ASSERT_EQUAL_INT32(4, pc_fs_size(s_root, "/ro", ""));
}

// rename validates both names before touching the file pool.
void test_rename_argument_guards()
{
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/r1", "", "data", 4));
    char longname[PC_MNT_NAME_MAX + 8];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    TEST_ASSERT_FALSE(pc_fs_rename(s_root, nullptr, "", "/r2", ""));
    TEST_ASSERT_FALSE(pc_fs_rename(s_root, "/r1", "", nullptr, ""));
    TEST_ASSERT_FALSE(pc_fs_rename(s_root, "/r1", "", longname, "")); // destination name too long
    TEST_ASSERT_TRUE(pc_fs_exists(s_root, "/r1", ""));                // untouched by the rejected renames
    TEST_ASSERT_FALSE(pc_fs_exists(s_root, "/r2", ""));
}

// Renaming onto an existing name frees that file and takes over its name.
void test_rename_overwrites_destination()
{
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/src", "", "NEW", 3));
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/dst", "", "oldcontent", 10));
    TEST_ASSERT_TRUE(pc_fs_rename(s_root, "/src", "", "/dst", ""));
    TEST_ASSERT_FALSE(pc_fs_exists(s_root, "/src", ""));
    TEST_ASSERT_TRUE(pc_fs_exists(s_root, "/dst", ""));
    TEST_ASSERT_EQUAL_INT32(3, pc_fs_size(s_root, "/dst", "")); // the source's contents won
    char buf[16];
    long n = pc_fs_read_file(s_root, "/dst", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT32(3, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("NEW", buf);
}

// read_file fails when the file exists but no handle is left to open it with.
void test_read_file_handle_exhaustion()
{
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/rf", "", "0123456789", 10));
    int handles[PC_MNT_MAX_OPEN];
    for (int i = 0; i < PC_MNT_MAX_OPEN; i++)
    {
        handles[i] = pc_fs_open(s_root, "/rf", "", pc_mnt_mode::PC_MNT_READ);
        TEST_ASSERT_TRUE(handles[i] >= 0);
    }
    char buf[16];
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_read_file(s_root, "/rf", "", buf, sizeof(buf)));
    for (int i = 0; i < PC_MNT_MAX_OPEN; i++)
    {
        pc_fs_close(handles[i]);
    }
    TEST_ASSERT_EQUAL_INT32(10, pc_fs_read_file(s_root, "/rf", "", buf, sizeof(buf))); // works again
}

// write_file stops at the fixed per-file capacity and reports the short write.
void test_write_file_larger_than_capacity()
{
    static uint8_t big[PC_MNT_RAM_FILE_SIZE + 16];
    memset(big, 'z', sizeof(big));
    TEST_ASSERT_FALSE(pc_fs_write_file(s_root, "/cap", "", big, sizeof(big)));
    TEST_ASSERT_EQUAL_INT32((long)PC_MNT_RAM_FILE_SIZE, pc_fs_size(s_root, "/cap", ""));
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
    TEST_ASSERT_EQUAL_INT32(0, pc_fs_read_file(s_root, "/x", "", buf, sizeof(buf))); // gave up at 0 bytes
    TEST_ASSERT_FALSE(pc_fs_write_file(s_root, "/x", "", "abcd", 4));                // no progress -> failure
    pc_mnt_mount(pc_mnt_ram());
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/x", "", "abcd", 4)); // real backend still fine
}

// docs/BUGS.md: a root without a trailing slash used to concatenate - pc_ssh_sftp_begin(fs,
// "/gcode") resolved "/part.nc" to "/gcodepart.nc", a sibling of the mount rather than a file in
// it. pc_fs_begin owns the root, so it adds the separator once instead of every join assuming it.
void test_root_without_trailing_slash()
{
    // Each root is its own handle, so the test names all three at once rather than re-pointing one
    // global at a different prefix. That is also the arrangement the accessor exists to support: two
    // services over different storage, live at the same time.
    int gcode = pc_fs_begin("/gcode"); // the documented form, and the one that used to concatenate
    int bare = pc_fs_begin("/");
    int gcode_slash = pc_fs_begin("/gcode/"); // already carries the separator
    TEST_ASSERT_TRUE(gcode >= 0 && bare >= 0 && gcode_slash >= 0);

    TEST_ASSERT_TRUE(pc_fs_write_file(gcode, "/p.nc", "", "G0", 2));
    TEST_ASSERT_EQUAL_INT32(2, pc_fs_size(gcode, "/p.nc", ""));
    // The file landed inside the root, not glued onto its name.
    TEST_ASSERT_TRUE(pc_fs_exists(bare, "/gcode/p.nc", ""));
    TEST_ASSERT_FALSE(pc_fs_exists(bare, "/gcodep.nc", ""));

    // A root that already carries the separator is unchanged (no "//").
    TEST_ASSERT_TRUE(pc_fs_exists(gcode_slash, "/p.nc", ""));
}

// A directory destination takes the leaf; a file destination is the whole path. One frame either
// way - this is the shape SCP resolves a received filename with.
void test_leaf_joins_onto_a_directory()
{
    TEST_ASSERT_TRUE(pc_fs_mkdir(s_root, "/d", ""));
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/d/", "f.txt", "xy", 2));
    TEST_ASSERT_TRUE(pc_fs_exists(s_root, "/d/f.txt", ""));
    TEST_ASSERT_EQUAL_INT32(2, pc_fs_size(s_root, "/d/", "f.txt"));
    // Traversal is refused in the leaf as well as the dir.
    TEST_ASSERT_FALSE(pc_fs_write_file(s_root, "/d/", "../esc", "x", 1));
    TEST_ASSERT_FALSE(pc_fs_write_file(s_root, "/../d/", "f.txt", "x", 1));
}

// remove() on a directory takes the directory and everything under it. Every protocol that offers a
// delete needs this (WebDAV DELETE on a collection, SFTP on a tree), and each used to carry its own
// walk - a level stack, a depth bound, and a rule for not invalidating the cursor it was standing on.
// The walk is the accessor's, so there is one of it.
void test_remove_takes_the_whole_subtree()
{
    TEST_ASSERT_TRUE(pc_fs_mkdir(s_root, "/tree", ""));
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/tree/", "a", "1", 1));
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/tree/", "b", "22", 2));

    // rmdir keeps its POSIX meaning - it refuses a directory that still has members - so a caller
    // that wants the empty-only guarantee can still ask for it.
    TEST_ASSERT_FALSE(pc_fs_rmdir(s_root, "/tree", ""));

    TEST_ASSERT_TRUE(pc_fs_remove(s_root, "/tree", ""));
    TEST_ASSERT_FALSE(pc_fs_exists(s_root, "/tree", ""));
    TEST_ASSERT_FALSE(pc_fs_exists(s_root, "/tree/a", ""));
    TEST_ASSERT_FALSE(pc_fs_exists(s_root, "/tree/b", ""));
}

// A plain file still removes as one call; a path that names nothing still fails.
void test_remove_file_and_missing_unchanged()
{
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/one.txt", "", "x", 1));
    TEST_ASSERT_TRUE(pc_fs_remove(s_root, "/one.txt", ""));
    TEST_ASSERT_FALSE(pc_fs_exists(s_root, "/one.txt", ""));
    TEST_ASSERT_FALSE(pc_fs_remove(s_root, "/one.txt", "")); // already gone
}

// copy() duplicates a file's bytes, leaving the source alone. The transfer moves whole blocks so the
// store never read-modify-writes a partial one; the caller never sees a block.
void test_copy_file()
{
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/c1", "", "payload", 7));
    TEST_ASSERT_TRUE(pc_fs_copy(s_root, "/c1", "", "/c2", ""));
    TEST_ASSERT_TRUE(pc_fs_exists(s_root, "/c1", "")); // source survives
    TEST_ASSERT_EQUAL_INT32(7, pc_fs_size(s_root, "/c2", ""));
    char buf[16];
    long n = pc_fs_read_file(s_root, "/c2", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT32(7, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("payload", buf);
}

// copy() on a directory takes the tree: the collection and its members, bytes included.
void test_copy_takes_the_whole_subtree()
{
    TEST_ASSERT_TRUE(pc_fs_mkdir(s_root, "/s", ""));
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/s/", "f", "xyz", 3));

    TEST_ASSERT_TRUE(pc_fs_copy(s_root, "/s", "", "/t", ""));
    TEST_ASSERT_TRUE(pc_fs_exists(s_root, "/t", ""));
    TEST_ASSERT_TRUE(pc_fs_exists(s_root, "/t/f", ""));
    char buf[8];
    long n = pc_fs_read_file(s_root, "/t/f", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT32(3, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("xyz", buf);
    TEST_ASSERT_TRUE(pc_fs_exists(s_root, "/s/f", "")); // source tree intact
}

// Both tree operations refuse a traversal before touching storage, exactly as the per-node ones do -
// the guard is on the resolve, so it cannot be bypassed by reaching for the tree entry point.
void test_tree_ops_refuse_traversal()
{
    TEST_ASSERT_FALSE(pc_fs_remove(s_root, "/../escape", ""));
    TEST_ASSERT_FALSE(pc_fs_copy(s_root, "/../escape", "", "/x", ""));
    TEST_ASSERT_FALSE(pc_fs_copy(s_root, "/x", "", "/../escape", ""));
}

// An unbound root fails every operation closed rather than resolving against somebody else's storage.
void test_unbound_root_fails_closed()
{
    TEST_ASSERT_TRUE(pc_fs_write_file(s_root, "/u", "", "x", 1));
    TEST_ASSERT_FALSE(pc_fs_exists(-1, "/u", ""));
    TEST_ASSERT_FALSE(pc_fs_remove(-1, "/u", ""));
    TEST_ASSERT_FALSE(pc_fs_copy(-1, "/u", "", "/v", ""));
    TEST_ASSERT_TRUE(pc_fs_exists(s_root, "/u", "")); // untouched by the refused calls
}

// A filesystem with no store is a configuration, not a fault. Binding a root and resolving a path
// still work; the operations that need storage fail closed AND say why, so an application can tell
// "there is nowhere to put this" from "that path was wrong" without guessing.
void test_null_store_is_intentional_and_says_so()
{
    pc_mnt_mount(nullptr);
    pc_fs_clear_status();

    // The local-only half still works: a root binds and a path resolves with nothing mounted.
    int r = pc_fs_begin("/local");
    TEST_ASSERT_TRUE(r >= 0);
    TEST_ASSERT_FALSE(pc_fs_storage_present());
    TEST_ASSERT_EQUAL_UINT32(PC_FS_OK, pc_fs_status()); // binding a root touches no store

    // The half that needs storage fails closed, and the reason is readable.
    TEST_ASSERT_FALSE(pc_fs_write_file(r, "/a", "", "x", 1));
    TEST_ASSERT_TRUE((pc_fs_status() & PC_FS_STORAGE_EXHAUSTED) != 0);
    TEST_ASSERT_FALSE(pc_fs_exists(r, "/a", ""));
    TEST_ASSERT_EQUAL_INT32(-1, pc_fs_size(r, "/a", ""));

    // Attaching a store clears the condition; the mask is sticky until cleared, which is what lets a
    // caller test a whole sequence at once rather than branching per call.
    pc_mnt_mount(pc_mnt_ram());
    pc_mnt_ram_format();
    TEST_ASSERT_TRUE(pc_fs_storage_present());
    TEST_ASSERT_TRUE((pc_fs_status() & PC_FS_STORAGE_EXHAUSTED) != 0); // still set: nothing cleared it
    pc_fs_clear_status();
    TEST_ASSERT_EQUAL_UINT32(PC_FS_OK, pc_fs_status());
    TEST_ASSERT_TRUE(pc_fs_write_file(r, "/a", "", "x", 1));
    TEST_ASSERT_EQUAL_UINT32(PC_FS_OK, pc_fs_status());
}

// The other reasons are distinct bits, so a caller masks for the one it cares about.
void test_status_separates_the_reasons()
{
    pc_fs_clear_status();
    TEST_ASSERT_FALSE(pc_fs_exists(-1, "/a", "")); // never-bound root
    TEST_ASSERT_TRUE((pc_fs_status() & PC_FS_BAD_ROOT) != 0);
    TEST_ASSERT_EQUAL_UINT32(0u, pc_fs_status() & PC_FS_TRAVERSAL);

    pc_fs_clear_status();
    TEST_ASSERT_FALSE(pc_fs_write_file(s_root, "/../escape", "", "x", 1));
    TEST_ASSERT_TRUE((pc_fs_status() & PC_FS_TRAVERSAL) != 0);
    TEST_ASSERT_EQUAL_UINT32(0u, pc_fs_status() & PC_FS_BAD_ROOT);

    // A write that the store cannot take in full is the same "nowhere to put this" bit as no store.
    pc_fs_clear_status();
    static uint8_t big[PC_MNT_RAM_FILE_SIZE + 16];
    memset(big, 'z', sizeof(big));
    TEST_ASSERT_FALSE(pc_fs_write_file(s_root, "/toobig", "", big, sizeof(big)));
    TEST_ASSERT_TRUE((pc_fs_status() & PC_FS_STORAGE_EXHAUSTED) != 0);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_null_store_is_intentional_and_says_so);
    RUN_TEST(test_status_separates_the_reasons);
    RUN_TEST(test_root_without_trailing_slash);
    RUN_TEST(test_leaf_joins_onto_a_directory);
    RUN_TEST(test_remove_takes_the_whole_subtree);
    RUN_TEST(test_remove_file_and_missing_unchanged);
    RUN_TEST(test_copy_file);
    RUN_TEST(test_copy_takes_the_whole_subtree);
    RUN_TEST(test_tree_ops_refuse_traversal);
    RUN_TEST(test_unbound_root_fails_closed);
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
