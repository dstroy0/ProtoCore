// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/storage/filesystem.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>

static int s_root;

void setUp()
{
    Mnt.args.backend = protocore_mnt_ram();
    Mnt.mount(Mnt.internal);
    protocore_mnt_ram_format();
    s_root = protocore_fs_begin("/");
}
void tearDown()
{
}

void test_write_then_read_file()
{
    const char *msg = "hello vfs";
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/a.txt", "", msg, strlen(msg)));
    TEST_ASSERT_TRUE(protocore_fs_exists(s_root, "/a.txt", ""));
    TEST_ASSERT_EQUAL_INT32((long)strlen(msg), protocore_fs_size(s_root, "/a.txt", ""));

    char buf[32];
    long n = protocore_fs_read_file(s_root, "/a.txt", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT32((long)strlen(msg), n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING(msg, buf);
}

void test_streamed_write_and_read()
{
    int h = protocore_fs_open(s_root, "/s.bin", "", PROTOCORE_MNT_WRITE);
    TEST_ASSERT_TRUE(h >= 0);
    TEST_ASSERT_EQUAL_INT(3, protocore_fs_write(h, "abc", 3));
    TEST_ASSERT_EQUAL_INT(3, protocore_fs_write(h, "def", 3));
    protocore_fs_close(h);
    TEST_ASSERT_EQUAL_INT32(6, protocore_fs_size(s_root, "/s.bin", ""));

    h = protocore_fs_open(s_root, "/s.bin", "", PROTOCORE_MNT_READ);
    TEST_ASSERT_TRUE(h >= 0);
    char buf[8] = {0};
    TEST_ASSERT_EQUAL_INT(4, protocore_fs_read(h, buf, 4));
    TEST_ASSERT_EQUAL_STRING("abcd", buf);
    TEST_ASSERT_EQUAL_INT(2, protocore_fs_read(h, buf, 4));
    TEST_ASSERT_EQUAL_INT(0, protocore_fs_read(h, buf, 4));
    protocore_fs_close(h);
}

void test_write_mode_truncates()
{
    protocore_fs_write_file(s_root, "/t.txt", "", "longer original", 15);
    protocore_fs_write_file(s_root, "/t.txt", "", "short", 5);
    TEST_ASSERT_EQUAL_INT32(5, protocore_fs_size(s_root, "/t.txt", ""));
    char buf[16];
    long n = protocore_fs_read_file(s_root, "/t.txt", "", buf, sizeof(buf));
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("short", buf);
}

void test_append_extends()
{
    protocore_fs_write_file(s_root, "/log", "", "line1\n", 6);
    int h = protocore_fs_open(s_root, "/log", "", PROTOCORE_MNT_APPEND);
    TEST_ASSERT_TRUE(h >= 0);
    protocore_fs_write(h, "line2\n", 6);
    protocore_fs_close(h);
    TEST_ASSERT_EQUAL_INT32(12, protocore_fs_size(s_root, "/log", ""));
    char buf[16];
    long n = protocore_fs_read_file(s_root, "/log", "", buf, sizeof(buf));
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("line1\nline2\n", buf);
}

void test_remove_and_rename()
{
    protocore_fs_write_file(s_root, "/old", "", "data", 4);
    TEST_ASSERT_TRUE(protocore_fs_rename(s_root, "/old", "", "/new", ""));
    TEST_ASSERT_FALSE(protocore_fs_exists(s_root, "/old", ""));
    TEST_ASSERT_TRUE(protocore_fs_exists(s_root, "/new", ""));
    char buf[8];
    long n = protocore_fs_read_file(s_root, "/new", "", buf, sizeof(buf));
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("data", buf);

    TEST_ASSERT_TRUE(protocore_fs_remove(s_root, "/new", ""));
    TEST_ASSERT_FALSE(protocore_fs_exists(s_root, "/new", ""));
    TEST_ASSERT_EQUAL_INT32(-1, protocore_fs_size(s_root, "/new", ""));
}

void test_missing_file_fails_closed()
{
    TEST_ASSERT_FALSE(protocore_fs_exists(s_root, "/nope", ""));
    TEST_ASSERT_EQUAL_INT32(-1, protocore_fs_size(s_root, "/nope", ""));
    TEST_ASSERT_TRUE(protocore_fs_open(s_root, "/nope", "", PROTOCORE_MNT_READ) < 0);
    char buf[8];
    TEST_ASSERT_EQUAL_INT32(-1, protocore_fs_read_file(s_root, "/nope", "", buf, sizeof(buf)));
    TEST_ASSERT_FALSE(protocore_fs_remove(s_root, "/nope", ""));
    TEST_ASSERT_FALSE(protocore_fs_rename(s_root, "/nope", "", "/x", ""));
}

void test_remove_refuses_the_root_itself()
{
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/keep.txt", "", "data", 4));
    TEST_ASSERT_TRUE(protocore_fs_mkdir(s_root, "/sub", ""));
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/sub/deep.txt", "", "more", 4));

    TEST_ASSERT_FALSE(protocore_fs_remove(s_root, "/", ""));
    TEST_ASSERT_FALSE(protocore_fs_remove(s_root, "", ""));

    TEST_ASSERT_TRUE(protocore_fs_exists(s_root, "/keep.txt", ""));
    TEST_ASSERT_TRUE(protocore_fs_exists(s_root, "/sub/deep.txt", ""));
}

void test_read_buffer_too_small_fails_closed()
{
    protocore_fs_write_file(s_root, "/big", "", "0123456789", 10);
    char tiny[4];
    TEST_ASSERT_EQUAL_INT32(-1, protocore_fs_read_file(s_root, "/big", "", tiny, sizeof(tiny)));
}

void test_file_full_is_bounded()
{
    int h = protocore_fs_open(s_root, "/full", "", PROTOCORE_MNT_WRITE);
    TEST_ASSERT_TRUE(h >= 0);
    static uint8_t chunk[256];
    memset(chunk, 'x', sizeof(chunk));
    size_t written = 0;
    for (int i = 0; i < 100; i++)
    {
        int w = protocore_fs_write(h, chunk, sizeof(chunk));
        if (w <= 0)
        {
            break;
        }
        written += (size_t)w;
    }
    protocore_fs_close(h);

    TEST_ASSERT_EQUAL_INT32((long)PROTOCORE_MNT_RAM_FILE_SIZE, protocore_fs_size(s_root, "/full", ""));
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_MNT_RAM_FILE_SIZE, (uint32_t)written);
}

void test_file_pool_exhaustion()
{
    char name[16];
    for (int i = 0; i < PROTOCORE_MNT_RAM_FILES; i++)
    {
        snprintf(name, sizeof(name), "/f%d", i);
        TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, name, "", "x", 1));
    }

    TEST_ASSERT_FALSE(protocore_fs_write_file(s_root, "/overflow", "", "x", 1));
}

void test_handle_pool_exhaustion()
{
    protocore_fs_write_file(s_root, "/h", "", "data", 4);
    int handles[PROTOCORE_MNT_MAX_OPEN];
    for (int i = 0; i < PROTOCORE_MNT_MAX_OPEN; i++)
    {
        handles[i] = protocore_fs_open(s_root, "/h", "", PROTOCORE_MNT_READ);
        TEST_ASSERT_TRUE(handles[i] >= 0);
    }
    TEST_ASSERT_TRUE(protocore_fs_open(s_root, "/h", "", PROTOCORE_MNT_READ) < 0);
    protocore_fs_close(handles[0]);
    TEST_ASSERT_TRUE(protocore_fs_open(s_root, "/h", "", PROTOCORE_MNT_READ) >= 0);
}

void test_unmounted_fails_closed()
{
    Mnt.args.backend = NULL;
    Mnt.mount(Mnt.internal);
    TEST_ASSERT_TRUE(protocore_fs_open(s_root, "/a", "", PROTOCORE_MNT_READ) < 0);
    TEST_ASSERT_FALSE(protocore_fs_exists(s_root, "/a", ""));
    TEST_ASSERT_EQUAL_INT32(-1, protocore_fs_size(s_root, "/a", ""));
    TEST_ASSERT_FALSE(protocore_fs_write_file(s_root, "/a", "", "x", 1));
}

void test_ram_guard_subconditions()
{
    Mnt.args.backend = protocore_mnt_ram();
    Mnt.mount(Mnt.internal);
    protocore_mnt_ram_format();
    uint8_t b[8] = {0};

    TEST_ASSERT_EQUAL_INT(-1, protocore_fs_open(s_root, NULL, "", PROTOCORE_MNT_WRITE));
    char longname[256];
    for (int i = 0; i < 255; i++)
    {
        longname[i] = 'a';
    }
    longname[255] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, protocore_fs_open(s_root, longname, "", PROTOCORE_MNT_WRITE));

    TEST_ASSERT_EQUAL_INT(-1, protocore_fs_read(999, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, protocore_fs_write(999, b, sizeof(b)));
    protocore_fs_close(999);

    TEST_ASSERT_TRUE(protocore_fs_read_file(s_root, "/nope", "", b, sizeof(b)) < 0);
}

void test_unmounted_all_entry_points()
{
    Mnt.args.backend = NULL;
    Mnt.mount(Mnt.internal);
    uint8_t b[8] = {0};
    TEST_ASSERT_EQUAL_INT(-1, protocore_fs_read(0, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, protocore_fs_write(0, b, sizeof(b)));
    protocore_fs_close(0);
    TEST_ASSERT_FALSE(protocore_fs_remove(s_root, "/a", ""));
    TEST_ASSERT_FALSE(protocore_fs_rename(s_root, "/a", "", "/b", ""));
    TEST_ASSERT_TRUE(protocore_fs_read_file(s_root, "/a", "", b, sizeof(b)) < 0);
    Mnt.args.backend = protocore_mnt_ram();
    Mnt.mount(Mnt.internal);
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/a", "", "x", 1));
}

void test_handle_validity_edges()
{
    uint8_t b[8] = {0};
    TEST_ASSERT_EQUAL_INT(-1, protocore_fs_read(-1, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, protocore_fs_write(-1, b, sizeof(b)));
    protocore_fs_close(-1);

    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/hv", "", "data", 4));
    int h = protocore_fs_open(s_root, "/hv", "", PROTOCORE_MNT_READ);
    TEST_ASSERT_TRUE(h >= 0);
    protocore_fs_close(h);

    TEST_ASSERT_EQUAL_INT(-1, protocore_fs_read(h, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(-1, protocore_fs_write(h, b, sizeof(b)));
    TEST_ASSERT_TRUE(protocore_fs_open(s_root, "/hv", "", PROTOCORE_MNT_READ) >= 0);
}

void test_write_to_read_handle_rejected()
{
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/ro", "", "data", 4));
    int h = protocore_fs_open(s_root, "/ro", "", PROTOCORE_MNT_READ);
    TEST_ASSERT_TRUE(h >= 0);
    TEST_ASSERT_EQUAL_INT(-1, protocore_fs_write(h, "xx", 2));
    protocore_fs_close(h);
    TEST_ASSERT_EQUAL_INT32(4, protocore_fs_size(s_root, "/ro", ""));
}

void test_rename_argument_guards()
{
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/r1", "", "data", 4));
    char longname[PROTOCORE_MNT_NAME_MAX + 8];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    TEST_ASSERT_FALSE(protocore_fs_rename(s_root, NULL, "", "/r2", ""));
    TEST_ASSERT_FALSE(protocore_fs_rename(s_root, "/r1", "", NULL, ""));
    TEST_ASSERT_FALSE(protocore_fs_rename(s_root, "/r1", "", longname, ""));
    TEST_ASSERT_TRUE(protocore_fs_exists(s_root, "/r1", ""));
    TEST_ASSERT_FALSE(protocore_fs_exists(s_root, "/r2", ""));
}

void test_rename_overwrites_destination()
{
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/src", "", "NEW", 3));
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/dst", "", "oldcontent", 10));
    TEST_ASSERT_TRUE(protocore_fs_rename(s_root, "/src", "", "/dst", ""));
    TEST_ASSERT_FALSE(protocore_fs_exists(s_root, "/src", ""));
    TEST_ASSERT_TRUE(protocore_fs_exists(s_root, "/dst", ""));
    TEST_ASSERT_EQUAL_INT32(3, protocore_fs_size(s_root, "/dst", ""));
    char buf[16];
    long n = protocore_fs_read_file(s_root, "/dst", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT32(3, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("NEW", buf);
}

void test_read_file_handle_exhaustion()
{
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/rf", "", "0123456789", 10));
    int handles[PROTOCORE_MNT_MAX_OPEN];
    for (int i = 0; i < PROTOCORE_MNT_MAX_OPEN; i++)
    {
        handles[i] = protocore_fs_open(s_root, "/rf", "", PROTOCORE_MNT_READ);
        TEST_ASSERT_TRUE(handles[i] >= 0);
    }
    char buf[16];
    TEST_ASSERT_EQUAL_INT32(-1, protocore_fs_read_file(s_root, "/rf", "", buf, sizeof(buf)));
    for (int i = 0; i < PROTOCORE_MNT_MAX_OPEN; i++)
    {
        protocore_fs_close(handles[i]);
    }
    TEST_ASSERT_EQUAL_INT32(10, protocore_fs_read_file(s_root, "/rf", "", buf, sizeof(buf)));
}

void test_write_file_larger_than_capacity()
{
    static uint8_t big[PROTOCORE_MNT_RAM_FILE_SIZE + 16];
    memset(big, 'z', sizeof(big));
    TEST_ASSERT_FALSE(protocore_fs_write_file(s_root, "/cap", "", big, sizeof(big)));
    TEST_ASSERT_EQUAL_INT32((long)PROTOCORE_MNT_RAM_FILE_SIZE, protocore_fs_size(s_root, "/cap", ""));
}

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
static proto_bool stall_true(const char *path)
{
    (void)path;
    return PROTO_TRUE;
}
static proto_bool stall_rename(const char *from, const char *to)
{
    (void)from;
    (void)to;
    return PROTO_TRUE;
}
static proto_bool stall_seek(int handle, uint64_t off)
{
    (void)handle;
    (void)off;
    return PROTO_TRUE;
}
static proto_bool stall_stat(const char *path, protocore_mnt_stat *out)
{
    (void)path;
    out->is_dir = PROTO_FALSE;
    out->size = 8;
    out->mtime = 0;
    return PROTO_TRUE;
}
static int stall_opendir(const char *path)
{
    (void)path;
    return 0;
}
static proto_bool stall_readdir(int handle, protocore_mnt_stat *out, char *name, size_t name_cap)
{
    (void)handle;
    (void)out;
    (void)name;
    (void)name_cap;
    return PROTO_FALSE;
}
static const protocore_mnt_backend s_stall_backend = {stall_open, stall_read, stall_write,   stall_close,  stall_seek,
                                                      stall_size, stall_true, stall_true,    stall_rename, stall_true,
                                                      stall_true, stall_stat, stall_opendir, stall_readdir};

void test_zero_progress_backend_terminates()
{
    Mnt.args.backend = &s_stall_backend;
    Mnt.mount(Mnt.internal);
    char buf[16];
    TEST_ASSERT_EQUAL_INT32(0, protocore_fs_read_file(s_root, "/x", "", buf, sizeof(buf)));
    TEST_ASSERT_FALSE(protocore_fs_write_file(s_root, "/x", "", "abcd", 4));
    Mnt.args.backend = protocore_mnt_ram();
    Mnt.mount(Mnt.internal);
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/x", "", "abcd", 4));
}

void test_root_without_trailing_slash()
{

    int gcode = protocore_fs_begin("/gcode");
    int bare = protocore_fs_begin("/");
    int gcode_slash = protocore_fs_begin("/gcode/");
    TEST_ASSERT_TRUE(gcode >= 0 && bare >= 0 && gcode_slash >= 0);

    TEST_ASSERT_TRUE(protocore_fs_write_file(gcode, "/p.nc", "", "G0", 2));
    TEST_ASSERT_EQUAL_INT32(2, protocore_fs_size(gcode, "/p.nc", ""));

    TEST_ASSERT_TRUE(protocore_fs_exists(bare, "/gcode/p.nc", ""));
    TEST_ASSERT_FALSE(protocore_fs_exists(bare, "/gcodep.nc", ""));

    TEST_ASSERT_TRUE(protocore_fs_exists(gcode_slash, "/p.nc", ""));
}

void test_leaf_joins_onto_a_directory()
{
    TEST_ASSERT_TRUE(protocore_fs_mkdir(s_root, "/d", ""));
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/d/", "f.txt", "xy", 2));
    TEST_ASSERT_TRUE(protocore_fs_exists(s_root, "/d/f.txt", ""));
    TEST_ASSERT_EQUAL_INT32(2, protocore_fs_size(s_root, "/d/", "f.txt"));

    TEST_ASSERT_FALSE(protocore_fs_write_file(s_root, "/d/", "../esc", "x", 1));
    TEST_ASSERT_FALSE(protocore_fs_write_file(s_root, "/../d/", "f.txt", "x", 1));
}

void test_remove_takes_the_whole_subtree()
{
    TEST_ASSERT_TRUE(protocore_fs_mkdir(s_root, "/tree", ""));
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/tree/", "a", "1", 1));
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/tree/", "b", "22", 2));

    TEST_ASSERT_FALSE(protocore_fs_rmdir(s_root, "/tree", ""));

    TEST_ASSERT_TRUE(protocore_fs_remove(s_root, "/tree", ""));
    TEST_ASSERT_FALSE(protocore_fs_exists(s_root, "/tree", ""));
    TEST_ASSERT_FALSE(protocore_fs_exists(s_root, "/tree/a", ""));
    TEST_ASSERT_FALSE(protocore_fs_exists(s_root, "/tree/b", ""));
}

void test_remove_file_and_missing_unchanged()
{
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/one.txt", "", "x", 1));
    TEST_ASSERT_TRUE(protocore_fs_remove(s_root, "/one.txt", ""));
    TEST_ASSERT_FALSE(protocore_fs_exists(s_root, "/one.txt", ""));
    TEST_ASSERT_FALSE(protocore_fs_remove(s_root, "/one.txt", ""));
}

void test_copy_file()
{
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/c1", "", "payload", 7));
    TEST_ASSERT_TRUE(protocore_fs_copy(s_root, "/c1", "", "/c2", ""));
    TEST_ASSERT_TRUE(protocore_fs_exists(s_root, "/c1", ""));
    TEST_ASSERT_EQUAL_INT32(7, protocore_fs_size(s_root, "/c2", ""));
    char buf[16];
    long n = protocore_fs_read_file(s_root, "/c2", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT32(7, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("payload", buf);
}

void test_copy_takes_the_whole_subtree()
{
    TEST_ASSERT_TRUE(protocore_fs_mkdir(s_root, "/s", ""));
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/s/", "f", "xyz", 3));

    TEST_ASSERT_TRUE(protocore_fs_copy(s_root, "/s", "", "/t", ""));
    TEST_ASSERT_TRUE(protocore_fs_exists(s_root, "/t", ""));
    TEST_ASSERT_TRUE(protocore_fs_exists(s_root, "/t/f", ""));
    char buf[8];
    long n = protocore_fs_read_file(s_root, "/t/f", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT32(3, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("xyz", buf);
    TEST_ASSERT_TRUE(protocore_fs_exists(s_root, "/s/f", ""));
}

void test_tree_ops_refuse_traversal()
{
    TEST_ASSERT_FALSE(protocore_fs_remove(s_root, "/../escape", ""));
    TEST_ASSERT_FALSE(protocore_fs_copy(s_root, "/../escape", "", "/x", ""));
    TEST_ASSERT_FALSE(protocore_fs_copy(s_root, "/x", "", "/../escape", ""));
}

void test_unbound_root_fails_closed()
{
    TEST_ASSERT_TRUE(protocore_fs_write_file(s_root, "/u", "", "x", 1));
    TEST_ASSERT_FALSE(protocore_fs_exists(-1, "/u", ""));
    TEST_ASSERT_FALSE(protocore_fs_remove(-1, "/u", ""));
    TEST_ASSERT_FALSE(protocore_fs_copy(-1, "/u", "", "/v", ""));
    TEST_ASSERT_TRUE(protocore_fs_exists(s_root, "/u", ""));
}

void test_null_store_is_intentional_and_says_so()
{
    Mnt.args.backend = NULL;
    Mnt.mount(Mnt.internal);
    protocore_fs_clear_status();

    int r = protocore_fs_begin("/local");
    TEST_ASSERT_TRUE(r >= 0);
    TEST_ASSERT_FALSE(protocore_fs_storage_present());
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_FS_OK, protocore_fs_status());

    TEST_ASSERT_FALSE(protocore_fs_write_file(r, "/a", "", "x", 1));
    TEST_ASSERT_TRUE((protocore_fs_status() & PROTOCORE_FS_STORAGE_EXHAUSTED) != 0);
    TEST_ASSERT_FALSE(protocore_fs_exists(r, "/a", ""));
    TEST_ASSERT_EQUAL_INT32(-1, protocore_fs_size(r, "/a", ""));

    Mnt.args.backend = protocore_mnt_ram();
    Mnt.mount(Mnt.internal);
    protocore_mnt_ram_format();
    TEST_ASSERT_TRUE(protocore_fs_storage_present());
    TEST_ASSERT_TRUE((protocore_fs_status() & PROTOCORE_FS_STORAGE_EXHAUSTED) != 0);
    protocore_fs_clear_status();
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_FS_OK, protocore_fs_status());
    TEST_ASSERT_TRUE(protocore_fs_write_file(r, "/a", "", "x", 1));
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_FS_OK, protocore_fs_status());
}

void test_status_separates_the_reasons()
{
    protocore_fs_clear_status();
    TEST_ASSERT_FALSE(protocore_fs_exists(-1, "/a", ""));
    TEST_ASSERT_TRUE((protocore_fs_status() & PROTOCORE_FS_BAD_ROOT) != 0);
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_fs_status() & PROTOCORE_FS_TRAVERSAL);

    protocore_fs_clear_status();
    TEST_ASSERT_FALSE(protocore_fs_write_file(s_root, "/../escape", "", "x", 1));
    TEST_ASSERT_TRUE((protocore_fs_status() & PROTOCORE_FS_TRAVERSAL) != 0);
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_fs_status() & PROTOCORE_FS_BAD_ROOT);

    protocore_fs_clear_status();
    static uint8_t big[PROTOCORE_MNT_RAM_FILE_SIZE + 16];
    memset(big, 'z', sizeof(big));
    TEST_ASSERT_FALSE(protocore_fs_write_file(s_root, "/toobig", "", big, sizeof(big)));
    TEST_ASSERT_TRUE((protocore_fs_status() & PROTOCORE_FS_STORAGE_EXHAUSTED) != 0);
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
    RUN_TEST(test_remove_refuses_the_root_itself);
    return UNITY_END();
}
