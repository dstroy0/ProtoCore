// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/storage/filesystem/filesystem.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>

static uint8_t mnt_work[16]; // the borrow an entry takes; Mnt never reads it

static int s_root;

void setUp()
{
    Mnt.ram(mnt_work);
    MntV.args.backend = MntV.backend;
    Mnt.mount(mnt_work);
    Mnt.ram_format(mnt_work);
    FsV.mount = "/";
    Fs.begin(protocore_filesystem_span());
    s_root = FsV.i32;
}
void tearDown()
{
}

void test_write_then_read_file()
{
    const char *msg = "hello vfs";
    FsV.path.root = s_root;
    FsV.path.dir = "/a.txt";
    FsV.path.name = "";
    FsV.io.wbuf = msg;
    FsV.io.n = strlen(msg);
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/a.txt";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/a.txt";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32((long)strlen(msg), FsV.len);

    char buf[32];
    FsV.path.root = s_root;
    FsV.path.dir = "/a.txt";
    FsV.path.name = "";
    FsV.io.buf = buf;
    FsV.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = FsV.len;
    TEST_ASSERT_EQUAL_INT32((long)strlen(msg), n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING(msg, buf);
}

void test_streamed_write_and_read()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/s.bin";
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_WRITE;
    Fs.open(protocore_filesystem_span());
    int h = FsV.i32;
    TEST_ASSERT_TRUE(h >= 0);
    FsV.io.handle = h;
    FsV.io.wbuf = "abc";
    FsV.io.n = 3;
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(3, FsV.i32);
    FsV.io.handle = h;
    FsV.io.wbuf = "def";
    FsV.io.n = 3;
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(3, FsV.i32);
    FsV.io.handle = h;
    Fs.close(protocore_filesystem_span());
    FsV.path.root = s_root;
    FsV.path.dir = "/s.bin";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(6, FsV.len);

    FsV.path.root = s_root;
    FsV.path.dir = "/s.bin";
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    h = FsV.i32;
    TEST_ASSERT_TRUE(h >= 0);
    char buf[8] = {0};
    FsV.io.handle = h;
    FsV.io.buf = buf;
    FsV.io.n = 4;
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(4, FsV.i32);
    TEST_ASSERT_EQUAL_STRING("abcd", buf);
    FsV.io.handle = h;
    FsV.io.buf = buf;
    FsV.io.n = 4;
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(2, FsV.i32);
    FsV.io.handle = h;
    FsV.io.buf = buf;
    FsV.io.n = 4;
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(0, FsV.i32);
    FsV.io.handle = h;
    Fs.close(protocore_filesystem_span());
}

void test_write_mode_truncates()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/t.txt";
    FsV.path.name = "";
    FsV.io.wbuf = "longer original";
    FsV.io.n = 15;
    Fs.write_file(protocore_filesystem_span());
    FsV.path.root = s_root;
    FsV.path.dir = "/t.txt";
    FsV.path.name = "";
    FsV.io.wbuf = "short";
    FsV.io.n = 5;
    Fs.write_file(protocore_filesystem_span());
    FsV.path.root = s_root;
    FsV.path.dir = "/t.txt";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(5, FsV.len);
    char buf[16];
    FsV.path.root = s_root;
    FsV.path.dir = "/t.txt";
    FsV.path.name = "";
    FsV.io.buf = buf;
    FsV.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = FsV.len;
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("short", buf);
}

void test_append_extends()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/log";
    FsV.path.name = "";
    FsV.io.wbuf = "line1\n";
    FsV.io.n = 6;
    Fs.write_file(protocore_filesystem_span());
    FsV.path.root = s_root;
    FsV.path.dir = "/log";
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_APPEND;
    Fs.open(protocore_filesystem_span());
    int h = FsV.i32;
    TEST_ASSERT_TRUE(h >= 0);
    FsV.io.handle = h;
    FsV.io.wbuf = "line2\n";
    FsV.io.n = 6;
    Fs.write(protocore_filesystem_span());
    FsV.io.handle = h;
    Fs.close(protocore_filesystem_span());
    FsV.path.root = s_root;
    FsV.path.dir = "/log";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(12, FsV.len);
    char buf[16];
    FsV.path.root = s_root;
    FsV.path.dir = "/log";
    FsV.path.name = "";
    FsV.io.buf = buf;
    FsV.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = FsV.len;
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("line1\nline2\n", buf);
}

void test_remove_and_rename()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/old";
    FsV.path.name = "";
    FsV.io.wbuf = "data";
    FsV.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    FsV.path.root = s_root;
    FsV.path.dir = "/old";
    FsV.path.name = "";
    FsV.dest.dir = "/new";
    FsV.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/old";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/new";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    char buf[8];
    FsV.path.root = s_root;
    FsV.path.dir = "/new";
    FsV.path.name = "";
    FsV.io.buf = buf;
    FsV.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = FsV.len;
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("data", buf);

    FsV.path.root = s_root;
    FsV.path.dir = "/new";
    FsV.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/new";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/new";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, FsV.len);
}

void test_missing_file_fails_closed()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/nope";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/nope";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, FsV.len);
    FsV.path.root = s_root;
    FsV.path.dir = "/nope";
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.i32 < 0);
    char buf[8];
    FsV.path.root = s_root;
    FsV.path.dir = "/nope";
    FsV.path.name = "";
    FsV.io.buf = buf;
    FsV.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, FsV.len);
    FsV.path.root = s_root;
    FsV.path.dir = "/nope";
    FsV.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/nope";
    FsV.path.name = "";
    FsV.dest.dir = "/x";
    FsV.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
}

void test_remove_refuses_the_root_itself()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/keep.txt";
    FsV.path.name = "";
    FsV.io.wbuf = "data";
    FsV.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/sub";
    FsV.path.name = "";
    Fs.mkdir(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/sub/deep.txt";
    FsV.path.name = "";
    FsV.io.wbuf = "more";
    FsV.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);

    FsV.path.root = s_root;
    FsV.path.dir = "/";
    FsV.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "";
    FsV.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);

    FsV.path.root = s_root;
    FsV.path.dir = "/keep.txt";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/sub/deep.txt";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
}

void test_read_buffer_too_small_fails_closed()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/big";
    FsV.path.name = "";
    FsV.io.wbuf = "0123456789";
    FsV.io.n = 10;
    Fs.write_file(protocore_filesystem_span());
    char tiny[4];
    FsV.path.root = s_root;
    FsV.path.dir = "/big";
    FsV.path.name = "";
    FsV.io.buf = tiny;
    FsV.io.n = sizeof(tiny);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, FsV.len);
}

void test_file_full_is_bounded()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/full";
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_WRITE;
    Fs.open(protocore_filesystem_span());
    int h = FsV.i32;
    TEST_ASSERT_TRUE(h >= 0);
    static uint8_t chunk[256];
    memset(chunk, 'x', sizeof(chunk));
    size_t written = 0;
    for (int i = 0; i < 100; i++)
    {
        FsV.io.handle = h;
        FsV.io.wbuf = chunk;
        FsV.io.n = sizeof(chunk);
        Fs.write(protocore_filesystem_span());
        int w = FsV.i32;
        if (w <= 0)
        {
            break;
        }
        written += (size_t)w;
    }
    FsV.io.handle = h;
    Fs.close(protocore_filesystem_span());

    FsV.path.root = s_root;
    FsV.path.dir = "/full";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32((long)PROTOCORE_MNT_RAM_FILE_SIZE, FsV.len);
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_MNT_RAM_FILE_SIZE, (uint32_t)written);
}

void test_file_pool_exhaustion()
{
    char name[16];
    for (int i = 0; i < PROTOCORE_MNT_RAM_FILES; i++)
    {
        snprintf(name, sizeof(name), "/f%d", i);
        FsV.path.root = s_root;
        FsV.path.dir = name;
        FsV.path.name = "";
        FsV.io.wbuf = "x";
        FsV.io.n = 1;
        Fs.write_file(protocore_filesystem_span());
        TEST_ASSERT_TRUE(FsV.ok);
    }

    FsV.path.root = s_root;
    FsV.path.dir = "/overflow";
    FsV.path.name = "";
    FsV.io.wbuf = "x";
    FsV.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
}

void test_handle_pool_exhaustion()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/h";
    FsV.path.name = "";
    FsV.io.wbuf = "data";
    FsV.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    int handles[PROTOCORE_MNT_MAX_OPEN];
    for (int i = 0; i < PROTOCORE_MNT_MAX_OPEN; i++)
    {
        FsV.path.root = s_root;
        FsV.path.dir = "/h";
        FsV.path.name = "";
        FsV.io.mode = PROTOCORE_MNT_READ;
        Fs.open(protocore_filesystem_span());
        handles[i] = FsV.i32;
        TEST_ASSERT_TRUE(handles[i] >= 0);
    }
    FsV.path.root = s_root;
    FsV.path.dir = "/h";
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.i32 < 0);
    FsV.io.handle = handles[0];
    Fs.close(protocore_filesystem_span());
    FsV.path.root = s_root;
    FsV.path.dir = "/h";
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.i32 >= 0);
}

void test_unmounted_fails_closed()
{
    MntV.args.backend = NULL;
    Mnt.mount(mnt_work);
    FsV.path.root = s_root;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.i32 < 0);
    FsV.path.root = s_root;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, FsV.len);
    FsV.path.root = s_root;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    FsV.io.wbuf = "x";
    FsV.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
}

void test_ram_guard_subconditions()
{
    Mnt.ram(mnt_work);
    MntV.args.backend = MntV.backend;
    Mnt.mount(mnt_work);
    Mnt.ram_format(mnt_work);
    uint8_t b[8] = {0};

    FsV.path.root = s_root;
    FsV.path.dir = NULL;
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_WRITE;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, FsV.i32);
    char longname[256];
    for (int i = 0; i < 255; i++)
    {
        longname[i] = 'a';
    }
    longname[255] = '\0';
    FsV.path.root = s_root;
    FsV.path.dir = longname;
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_WRITE;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, FsV.i32);

    FsV.io.handle = 999;
    FsV.io.buf = b;
    FsV.io.n = sizeof(b);
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, FsV.i32);
    FsV.io.handle = 999;
    FsV.io.wbuf = b;
    FsV.io.n = sizeof(b);
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, FsV.i32);
    FsV.io.handle = 999;
    Fs.close(protocore_filesystem_span());

    FsV.path.root = s_root;
    FsV.path.dir = "/nope";
    FsV.path.name = "";
    FsV.io.buf = b;
    FsV.io.n = sizeof(b);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.len < 0);
}

void test_unmounted_all_entry_points()
{
    MntV.args.backend = NULL;
    Mnt.mount(mnt_work);
    uint8_t b[8] = {0};
    FsV.io.handle = 0;
    FsV.io.buf = b;
    FsV.io.n = sizeof(b);
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, FsV.i32);
    FsV.io.handle = 0;
    FsV.io.wbuf = b;
    FsV.io.n = sizeof(b);
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, FsV.i32);
    FsV.io.handle = 0;
    Fs.close(protocore_filesystem_span());
    FsV.path.root = s_root;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    FsV.dest.dir = "/b";
    FsV.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    FsV.io.buf = b;
    FsV.io.n = sizeof(b);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.len < 0);
    Mnt.ram(mnt_work);
    MntV.args.backend = MntV.backend;
    Mnt.mount(mnt_work);
    FsV.path.root = s_root;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    FsV.io.wbuf = "x";
    FsV.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
}

void test_handle_validity_edges()
{
    uint8_t b[8] = {0};
    FsV.io.handle = -1;
    FsV.io.buf = b;
    FsV.io.n = sizeof(b);
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, FsV.i32);
    FsV.io.handle = -1;
    FsV.io.wbuf = b;
    FsV.io.n = sizeof(b);
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, FsV.i32);
    FsV.io.handle = -1;
    Fs.close(protocore_filesystem_span());

    FsV.path.root = s_root;
    FsV.path.dir = "/hv";
    FsV.path.name = "";
    FsV.io.wbuf = "data";
    FsV.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/hv";
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    int h = FsV.i32;
    TEST_ASSERT_TRUE(h >= 0);
    FsV.io.handle = h;
    Fs.close(protocore_filesystem_span());

    FsV.io.handle = h;
    FsV.io.buf = b;
    FsV.io.n = sizeof(b);
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, FsV.i32);
    FsV.io.handle = h;
    FsV.io.wbuf = b;
    FsV.io.n = sizeof(b);
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, FsV.i32);
    FsV.path.root = s_root;
    FsV.path.dir = "/hv";
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.i32 >= 0);
}

void test_write_to_read_handle_rejected()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/ro";
    FsV.path.name = "";
    FsV.io.wbuf = "data";
    FsV.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/ro";
    FsV.path.name = "";
    FsV.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    int h = FsV.i32;
    TEST_ASSERT_TRUE(h >= 0);
    FsV.io.handle = h;
    FsV.io.wbuf = "xx";
    FsV.io.n = 2;
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, FsV.i32);
    FsV.io.handle = h;
    Fs.close(protocore_filesystem_span());
    FsV.path.root = s_root;
    FsV.path.dir = "/ro";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(4, FsV.len);
}

void test_rename_argument_guards()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/r1";
    FsV.path.name = "";
    FsV.io.wbuf = "data";
    FsV.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    char longname[PROTOCORE_MNT_NAME_MAX + 8];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    FsV.path.root = s_root;
    FsV.path.dir = NULL;
    FsV.path.name = "";
    FsV.dest.dir = "/r2";
    FsV.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/r1";
    FsV.path.name = "";
    FsV.dest.dir = NULL;
    FsV.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/r1";
    FsV.path.name = "";
    FsV.dest.dir = longname;
    FsV.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/r1";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/r2";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
}

void test_rename_overwrites_destination()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/src";
    FsV.path.name = "";
    FsV.io.wbuf = "NEW";
    FsV.io.n = 3;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/dst";
    FsV.path.name = "";
    FsV.io.wbuf = "oldcontent";
    FsV.io.n = 10;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/src";
    FsV.path.name = "";
    FsV.dest.dir = "/dst";
    FsV.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/src";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/dst";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/dst";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(3, FsV.len);
    char buf[16];
    FsV.path.root = s_root;
    FsV.path.dir = "/dst";
    FsV.path.name = "";
    FsV.io.buf = buf;
    FsV.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = FsV.len;
    TEST_ASSERT_EQUAL_INT32(3, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("NEW", buf);
}

void test_read_file_handle_exhaustion()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/rf";
    FsV.path.name = "";
    FsV.io.wbuf = "0123456789";
    FsV.io.n = 10;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    int handles[PROTOCORE_MNT_MAX_OPEN];
    for (int i = 0; i < PROTOCORE_MNT_MAX_OPEN; i++)
    {
        FsV.path.root = s_root;
        FsV.path.dir = "/rf";
        FsV.path.name = "";
        FsV.io.mode = PROTOCORE_MNT_READ;
        Fs.open(protocore_filesystem_span());
        handles[i] = FsV.i32;
        TEST_ASSERT_TRUE(handles[i] >= 0);
    }
    char buf[16];
    FsV.path.root = s_root;
    FsV.path.dir = "/rf";
    FsV.path.name = "";
    FsV.io.buf = buf;
    FsV.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, FsV.len);
    for (int i = 0; i < PROTOCORE_MNT_MAX_OPEN; i++)
    {
        FsV.io.handle = handles[i];
        Fs.close(protocore_filesystem_span());
    }
    FsV.path.root = s_root;
    FsV.path.dir = "/rf";
    FsV.path.name = "";
    FsV.io.buf = buf;
    FsV.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(10, FsV.len);
}

void test_write_file_larger_than_capacity()
{
    static uint8_t big[PROTOCORE_MNT_RAM_FILE_SIZE + 16];
    memset(big, 'z', sizeof(big));
    FsV.path.root = s_root;
    FsV.path.dir = "/cap";
    FsV.path.name = "";
    FsV.io.wbuf = big;
    FsV.io.n = sizeof(big);
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/cap";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32((long)PROTOCORE_MNT_RAM_FILE_SIZE, FsV.len);
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
    MntV.args.backend = &s_stall_backend;
    Mnt.mount(mnt_work);
    char buf[16];
    FsV.path.root = s_root;
    FsV.path.dir = "/x";
    FsV.path.name = "";
    FsV.io.buf = buf;
    FsV.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(0, FsV.len);
    FsV.path.root = s_root;
    FsV.path.dir = "/x";
    FsV.path.name = "";
    FsV.io.wbuf = "abcd";
    FsV.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    Mnt.ram(mnt_work);
    MntV.args.backend = MntV.backend;
    Mnt.mount(mnt_work);
    FsV.path.root = s_root;
    FsV.path.dir = "/x";
    FsV.path.name = "";
    FsV.io.wbuf = "abcd";
    FsV.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
}

void test_root_without_trailing_slash()
{

    FsV.mount = "/gcode";
    Fs.begin(protocore_filesystem_span());
    int gcode = FsV.i32;
    FsV.mount = "/";
    Fs.begin(protocore_filesystem_span());
    int bare = FsV.i32;
    FsV.mount = "/gcode/";
    Fs.begin(protocore_filesystem_span());
    int gcode_slash = FsV.i32;
    TEST_ASSERT_TRUE(gcode >= 0 && bare >= 0 && gcode_slash >= 0);

    FsV.path.root = gcode;
    FsV.path.dir = "/p.nc";
    FsV.path.name = "";
    FsV.io.wbuf = "G0";
    FsV.io.n = 2;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = gcode;
    FsV.path.dir = "/p.nc";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(2, FsV.len);

    FsV.path.root = bare;
    FsV.path.dir = "/gcode/p.nc";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = bare;
    FsV.path.dir = "/gcodep.nc";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);

    FsV.path.root = gcode_slash;
    FsV.path.dir = "/p.nc";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
}

void test_leaf_joins_onto_a_directory()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/d";
    FsV.path.name = "";
    Fs.mkdir(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/d/";
    FsV.path.name = "f.txt";
    FsV.io.wbuf = "xy";
    FsV.io.n = 2;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/d/f.txt";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/d/";
    FsV.path.name = "f.txt";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(2, FsV.len);

    FsV.path.root = s_root;
    FsV.path.dir = "/d/";
    FsV.path.name = "../esc";
    FsV.io.wbuf = "x";
    FsV.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/../d/";
    FsV.path.name = "f.txt";
    FsV.io.wbuf = "x";
    FsV.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
}

void test_remove_takes_the_whole_subtree()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/tree";
    FsV.path.name = "";
    Fs.mkdir(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/tree/";
    FsV.path.name = "a";
    FsV.io.wbuf = "1";
    FsV.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/tree/";
    FsV.path.name = "b";
    FsV.io.wbuf = "22";
    FsV.io.n = 2;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);

    FsV.path.root = s_root;
    FsV.path.dir = "/tree";
    FsV.path.name = "";
    Fs.rmdir(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);

    FsV.path.root = s_root;
    FsV.path.dir = "/tree";
    FsV.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/tree";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/tree/a";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/tree/b";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
}

void test_remove_file_and_missing_unchanged()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/one.txt";
    FsV.path.name = "";
    FsV.io.wbuf = "x";
    FsV.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/one.txt";
    FsV.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/one.txt";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/one.txt";
    FsV.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
}

void test_copy_file()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/c1";
    FsV.path.name = "";
    FsV.io.wbuf = "payload";
    FsV.io.n = 7;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/c1";
    FsV.path.name = "";
    FsV.dest.dir = "/c2";
    FsV.dest.name = "";
    Fs.copy(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/c1";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/c2";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(7, FsV.len);
    char buf[16];
    FsV.path.root = s_root;
    FsV.path.dir = "/c2";
    FsV.path.name = "";
    FsV.io.buf = buf;
    FsV.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = FsV.len;
    TEST_ASSERT_EQUAL_INT32(7, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("payload", buf);
}

void test_copy_takes_the_whole_subtree()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/s";
    FsV.path.name = "";
    Fs.mkdir(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/s/";
    FsV.path.name = "f";
    FsV.io.wbuf = "xyz";
    FsV.io.n = 3;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);

    FsV.path.root = s_root;
    FsV.path.dir = "/s";
    FsV.path.name = "";
    FsV.dest.dir = "/t";
    FsV.dest.name = "";
    Fs.copy(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/t";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/t/f";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    char buf[8];
    FsV.path.root = s_root;
    FsV.path.dir = "/t/f";
    FsV.path.name = "";
    FsV.io.buf = buf;
    FsV.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = FsV.len;
    TEST_ASSERT_EQUAL_INT32(3, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("xyz", buf);
    FsV.path.root = s_root;
    FsV.path.dir = "/s/f";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
}

void test_tree_ops_refuse_traversal()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/../escape";
    FsV.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/../escape";
    FsV.path.name = "";
    FsV.dest.dir = "/x";
    FsV.dest.name = "";
    Fs.copy(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/x";
    FsV.path.name = "";
    FsV.dest.dir = "/../escape";
    FsV.dest.name = "";
    Fs.copy(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
}

void test_unbound_root_fails_closed()
{
    FsV.path.root = s_root;
    FsV.path.dir = "/u";
    FsV.path.name = "";
    FsV.io.wbuf = "x";
    FsV.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    FsV.path.root = -1;
    FsV.path.dir = "/u";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = -1;
    FsV.path.dir = "/u";
    FsV.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = -1;
    FsV.path.dir = "/u";
    FsV.path.name = "";
    FsV.dest.dir = "/v";
    FsV.dest.name = "";
    Fs.copy(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = s_root;
    FsV.path.dir = "/u";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
}

void test_null_store_is_intentional_and_says_so()
{
    MntV.args.backend = NULL;
    Mnt.mount(mnt_work);
    FsV.clear(protocore_filesystem_span());

    FsV.mount = "/local";
    Fs.begin(protocore_filesystem_span());
    int r = FsV.i32;
    TEST_ASSERT_TRUE(r >= 0);
    Fs.present(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_FS_OK, FsV.bits);

    FsV.path.root = r;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    FsV.io.wbuf = "x";
    FsV.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_TRUE((FsV.bits & PROTOCORE_FS_STORAGE_EXHAUSTED) != 0);
    FsV.path.root = r;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    FsV.path.root = r;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, FsV.len);

    Mnt.ram(mnt_work);
    MntV.args.backend = MntV.backend;
    Mnt.mount(mnt_work);
    Mnt.ram_format(mnt_work);
    Fs.present(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_TRUE((FsV.bits & PROTOCORE_FS_STORAGE_EXHAUSTED) != 0);
    FsV.clear(protocore_filesystem_span());
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_FS_OK, FsV.bits);
    FsV.path.root = r;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    FsV.io.wbuf = "x";
    FsV.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(FsV.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_FS_OK, FsV.bits);
}

void test_status_separates_the_reasons()
{
    FsV.clear(protocore_filesystem_span());
    FsV.path.root = -1;
    FsV.path.dir = "/a";
    FsV.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_TRUE((FsV.bits & PROTOCORE_FS_BAD_ROOT) != 0);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_UINT32(0u, FsV.bits & PROTOCORE_FS_TRAVERSAL);

    FsV.clear(protocore_filesystem_span());
    FsV.path.root = s_root;
    FsV.path.dir = "/../escape";
    FsV.path.name = "";
    FsV.io.wbuf = "x";
    FsV.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_TRUE((FsV.bits & PROTOCORE_FS_TRAVERSAL) != 0);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_UINT32(0u, FsV.bits & PROTOCORE_FS_BAD_ROOT);

    FsV.clear(protocore_filesystem_span());
    static uint8_t big[PROTOCORE_MNT_RAM_FILE_SIZE + 16];
    memset(big, 'z', sizeof(big));
    FsV.path.root = s_root;
    FsV.path.dir = "/toobig";
    FsV.path.name = "";
    FsV.io.wbuf = big;
    FsV.io.n = sizeof(big);
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(FsV.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_TRUE((FsV.bits & PROTOCORE_FS_STORAGE_EXHAUSTED) != 0);
}
