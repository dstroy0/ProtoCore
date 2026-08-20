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
    Fs.mount = "/";
    Fs.begin(protocore_filesystem_span());
    s_root = Fs.i32;
}
void tearDown()
{
}

void test_write_then_read_file()
{
    const char *msg = "hello vfs";
    Fs.path.root = s_root;
    Fs.path.dir = "/a.txt";
    Fs.path.name = "";
    Fs.io.wbuf = msg;
    Fs.io.n = strlen(msg);
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/a.txt";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/a.txt";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32((long)strlen(msg), Fs.len);

    char buf[32];
    Fs.path.root = s_root;
    Fs.path.dir = "/a.txt";
    Fs.path.name = "";
    Fs.io.buf = buf;
    Fs.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = Fs.len;
    TEST_ASSERT_EQUAL_INT32((long)strlen(msg), n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING(msg, buf);
}

void test_streamed_write_and_read()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/s.bin";
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_WRITE;
    Fs.open(protocore_filesystem_span());
    int h = Fs.i32;
    TEST_ASSERT_TRUE(h >= 0);
    Fs.io.handle = h;
    Fs.io.wbuf = "abc";
    Fs.io.n = 3;
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(3, Fs.i32);
    Fs.io.handle = h;
    Fs.io.wbuf = "def";
    Fs.io.n = 3;
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(3, Fs.i32);
    Fs.io.handle = h;
    Fs.close(protocore_filesystem_span());
    Fs.path.root = s_root;
    Fs.path.dir = "/s.bin";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(6, Fs.len);

    Fs.path.root = s_root;
    Fs.path.dir = "/s.bin";
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    h = Fs.i32;
    TEST_ASSERT_TRUE(h >= 0);
    char buf[8] = {0};
    Fs.io.handle = h;
    Fs.io.buf = buf;
    Fs.io.n = 4;
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(4, Fs.i32);
    TEST_ASSERT_EQUAL_STRING("abcd", buf);
    Fs.io.handle = h;
    Fs.io.buf = buf;
    Fs.io.n = 4;
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(2, Fs.i32);
    Fs.io.handle = h;
    Fs.io.buf = buf;
    Fs.io.n = 4;
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(0, Fs.i32);
    Fs.io.handle = h;
    Fs.close(protocore_filesystem_span());
}

void test_write_mode_truncates()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/t.txt";
    Fs.path.name = "";
    Fs.io.wbuf = "longer original";
    Fs.io.n = 15;
    Fs.write_file(protocore_filesystem_span());
    Fs.path.root = s_root;
    Fs.path.dir = "/t.txt";
    Fs.path.name = "";
    Fs.io.wbuf = "short";
    Fs.io.n = 5;
    Fs.write_file(protocore_filesystem_span());
    Fs.path.root = s_root;
    Fs.path.dir = "/t.txt";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(5, Fs.len);
    char buf[16];
    Fs.path.root = s_root;
    Fs.path.dir = "/t.txt";
    Fs.path.name = "";
    Fs.io.buf = buf;
    Fs.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = Fs.len;
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("short", buf);
}

void test_append_extends()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/log";
    Fs.path.name = "";
    Fs.io.wbuf = "line1\n";
    Fs.io.n = 6;
    Fs.write_file(protocore_filesystem_span());
    Fs.path.root = s_root;
    Fs.path.dir = "/log";
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_APPEND;
    Fs.open(protocore_filesystem_span());
    int h = Fs.i32;
    TEST_ASSERT_TRUE(h >= 0);
    Fs.io.handle = h;
    Fs.io.wbuf = "line2\n";
    Fs.io.n = 6;
    Fs.write(protocore_filesystem_span());
    Fs.io.handle = h;
    Fs.close(protocore_filesystem_span());
    Fs.path.root = s_root;
    Fs.path.dir = "/log";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(12, Fs.len);
    char buf[16];
    Fs.path.root = s_root;
    Fs.path.dir = "/log";
    Fs.path.name = "";
    Fs.io.buf = buf;
    Fs.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = Fs.len;
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("line1\nline2\n", buf);
}

void test_remove_and_rename()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/old";
    Fs.path.name = "";
    Fs.io.wbuf = "data";
    Fs.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    Fs.path.root = s_root;
    Fs.path.dir = "/old";
    Fs.path.name = "";
    Fs.dest.dir = "/new";
    Fs.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/old";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/new";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    char buf[8];
    Fs.path.root = s_root;
    Fs.path.dir = "/new";
    Fs.path.name = "";
    Fs.io.buf = buf;
    Fs.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = Fs.len;
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("data", buf);

    Fs.path.root = s_root;
    Fs.path.dir = "/new";
    Fs.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/new";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/new";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, Fs.len);
}

void test_missing_file_fails_closed()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/nope";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/nope";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, Fs.len);
    Fs.path.root = s_root;
    Fs.path.dir = "/nope";
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.i32 < 0);
    char buf[8];
    Fs.path.root = s_root;
    Fs.path.dir = "/nope";
    Fs.path.name = "";
    Fs.io.buf = buf;
    Fs.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, Fs.len);
    Fs.path.root = s_root;
    Fs.path.dir = "/nope";
    Fs.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/nope";
    Fs.path.name = "";
    Fs.dest.dir = "/x";
    Fs.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
}

void test_remove_refuses_the_root_itself()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/keep.txt";
    Fs.path.name = "";
    Fs.io.wbuf = "data";
    Fs.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/sub";
    Fs.path.name = "";
    Fs.mkdir(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/sub/deep.txt";
    Fs.path.name = "";
    Fs.io.wbuf = "more";
    Fs.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);

    Fs.path.root = s_root;
    Fs.path.dir = "/";
    Fs.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "";
    Fs.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);

    Fs.path.root = s_root;
    Fs.path.dir = "/keep.txt";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/sub/deep.txt";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
}

void test_read_buffer_too_small_fails_closed()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/big";
    Fs.path.name = "";
    Fs.io.wbuf = "0123456789";
    Fs.io.n = 10;
    Fs.write_file(protocore_filesystem_span());
    char tiny[4];
    Fs.path.root = s_root;
    Fs.path.dir = "/big";
    Fs.path.name = "";
    Fs.io.buf = tiny;
    Fs.io.n = sizeof(tiny);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, Fs.len);
}

void test_file_full_is_bounded()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/full";
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_WRITE;
    Fs.open(protocore_filesystem_span());
    int h = Fs.i32;
    TEST_ASSERT_TRUE(h >= 0);
    static uint8_t chunk[256];
    memset(chunk, 'x', sizeof(chunk));
    size_t written = 0;
    for (int i = 0; i < 100; i++)
    {
        Fs.io.handle = h;
        Fs.io.wbuf = chunk;
        Fs.io.n = sizeof(chunk);
        Fs.write(protocore_filesystem_span());
        int w = Fs.i32;
        if (w <= 0)
        {
            break;
        }
        written += (size_t)w;
    }
    Fs.io.handle = h;
    Fs.close(protocore_filesystem_span());

    Fs.path.root = s_root;
    Fs.path.dir = "/full";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32((long)PROTOCORE_MNT_RAM_FILE_SIZE, Fs.len);
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_MNT_RAM_FILE_SIZE, (uint32_t)written);
}

void test_file_pool_exhaustion()
{
    char name[16];
    for (int i = 0; i < PROTOCORE_MNT_RAM_FILES; i++)
    {
        snprintf(name, sizeof(name), "/f%d", i);
        Fs.path.root = s_root;
        Fs.path.dir = name;
        Fs.path.name = "";
        Fs.io.wbuf = "x";
        Fs.io.n = 1;
        Fs.write_file(protocore_filesystem_span());
        TEST_ASSERT_TRUE(Fs.ok);
    }

    Fs.path.root = s_root;
    Fs.path.dir = "/overflow";
    Fs.path.name = "";
    Fs.io.wbuf = "x";
    Fs.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
}

void test_handle_pool_exhaustion()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/h";
    Fs.path.name = "";
    Fs.io.wbuf = "data";
    Fs.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    int handles[PROTOCORE_MNT_MAX_OPEN];
    for (int i = 0; i < PROTOCORE_MNT_MAX_OPEN; i++)
    {
        Fs.path.root = s_root;
        Fs.path.dir = "/h";
        Fs.path.name = "";
        Fs.io.mode = PROTOCORE_MNT_READ;
        Fs.open(protocore_filesystem_span());
        handles[i] = Fs.i32;
        TEST_ASSERT_TRUE(handles[i] >= 0);
    }
    Fs.path.root = s_root;
    Fs.path.dir = "/h";
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.i32 < 0);
    Fs.io.handle = handles[0];
    Fs.close(protocore_filesystem_span());
    Fs.path.root = s_root;
    Fs.path.dir = "/h";
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.i32 >= 0);
}

void test_unmounted_fails_closed()
{
    MntV.args.backend = NULL;
    Mnt.mount(mnt_work);
    Fs.path.root = s_root;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.i32 < 0);
    Fs.path.root = s_root;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, Fs.len);
    Fs.path.root = s_root;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.io.wbuf = "x";
    Fs.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
}

void test_ram_guard_subconditions()
{
    Mnt.ram(mnt_work);
    MntV.args.backend = MntV.backend;
    Mnt.mount(mnt_work);
    Mnt.ram_format(mnt_work);
    uint8_t b[8] = {0};

    Fs.path.root = s_root;
    Fs.path.dir = NULL;
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_WRITE;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, Fs.i32);
    char longname[256];
    for (int i = 0; i < 255; i++)
    {
        longname[i] = 'a';
    }
    longname[255] = '\0';
    Fs.path.root = s_root;
    Fs.path.dir = longname;
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_WRITE;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, Fs.i32);

    Fs.io.handle = 999;
    Fs.io.buf = b;
    Fs.io.n = sizeof(b);
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, Fs.i32);
    Fs.io.handle = 999;
    Fs.io.wbuf = b;
    Fs.io.n = sizeof(b);
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, Fs.i32);
    Fs.io.handle = 999;
    Fs.close(protocore_filesystem_span());

    Fs.path.root = s_root;
    Fs.path.dir = "/nope";
    Fs.path.name = "";
    Fs.io.buf = b;
    Fs.io.n = sizeof(b);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.len < 0);
}

void test_unmounted_all_entry_points()
{
    MntV.args.backend = NULL;
    Mnt.mount(mnt_work);
    uint8_t b[8] = {0};
    Fs.io.handle = 0;
    Fs.io.buf = b;
    Fs.io.n = sizeof(b);
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, Fs.i32);
    Fs.io.handle = 0;
    Fs.io.wbuf = b;
    Fs.io.n = sizeof(b);
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, Fs.i32);
    Fs.io.handle = 0;
    Fs.close(protocore_filesystem_span());
    Fs.path.root = s_root;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.dest.dir = "/b";
    Fs.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.io.buf = b;
    Fs.io.n = sizeof(b);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.len < 0);
    Mnt.ram(mnt_work);
    MntV.args.backend = MntV.backend;
    Mnt.mount(mnt_work);
    Fs.path.root = s_root;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.io.wbuf = "x";
    Fs.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
}

void test_handle_validity_edges()
{
    uint8_t b[8] = {0};
    Fs.io.handle = -1;
    Fs.io.buf = b;
    Fs.io.n = sizeof(b);
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, Fs.i32);
    Fs.io.handle = -1;
    Fs.io.wbuf = b;
    Fs.io.n = sizeof(b);
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, Fs.i32);
    Fs.io.handle = -1;
    Fs.close(protocore_filesystem_span());

    Fs.path.root = s_root;
    Fs.path.dir = "/hv";
    Fs.path.name = "";
    Fs.io.wbuf = "data";
    Fs.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/hv";
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    int h = Fs.i32;
    TEST_ASSERT_TRUE(h >= 0);
    Fs.io.handle = h;
    Fs.close(protocore_filesystem_span());

    Fs.io.handle = h;
    Fs.io.buf = b;
    Fs.io.n = sizeof(b);
    Fs.read(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, Fs.i32);
    Fs.io.handle = h;
    Fs.io.wbuf = b;
    Fs.io.n = sizeof(b);
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, Fs.i32);
    Fs.path.root = s_root;
    Fs.path.dir = "/hv";
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.i32 >= 0);
}

void test_write_to_read_handle_rejected()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/ro";
    Fs.path.name = "";
    Fs.io.wbuf = "data";
    Fs.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/ro";
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    int h = Fs.i32;
    TEST_ASSERT_TRUE(h >= 0);
    Fs.io.handle = h;
    Fs.io.wbuf = "xx";
    Fs.io.n = 2;
    Fs.write(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(-1, Fs.i32);
    Fs.io.handle = h;
    Fs.close(protocore_filesystem_span());
    Fs.path.root = s_root;
    Fs.path.dir = "/ro";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(4, Fs.len);
}

void test_rename_argument_guards()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/r1";
    Fs.path.name = "";
    Fs.io.wbuf = "data";
    Fs.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    char longname[PROTOCORE_MNT_NAME_MAX + 8];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    Fs.path.root = s_root;
    Fs.path.dir = NULL;
    Fs.path.name = "";
    Fs.dest.dir = "/r2";
    Fs.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/r1";
    Fs.path.name = "";
    Fs.dest.dir = NULL;
    Fs.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/r1";
    Fs.path.name = "";
    Fs.dest.dir = longname;
    Fs.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/r1";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/r2";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
}

void test_rename_overwrites_destination()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/src";
    Fs.path.name = "";
    Fs.io.wbuf = "NEW";
    Fs.io.n = 3;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/dst";
    Fs.path.name = "";
    Fs.io.wbuf = "oldcontent";
    Fs.io.n = 10;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/src";
    Fs.path.name = "";
    Fs.dest.dir = "/dst";
    Fs.dest.name = "";
    Fs.rename(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/src";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/dst";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/dst";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(3, Fs.len);
    char buf[16];
    Fs.path.root = s_root;
    Fs.path.dir = "/dst";
    Fs.path.name = "";
    Fs.io.buf = buf;
    Fs.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = Fs.len;
    TEST_ASSERT_EQUAL_INT32(3, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("NEW", buf);
}

void test_read_file_handle_exhaustion()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/rf";
    Fs.path.name = "";
    Fs.io.wbuf = "0123456789";
    Fs.io.n = 10;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    int handles[PROTOCORE_MNT_MAX_OPEN];
    for (int i = 0; i < PROTOCORE_MNT_MAX_OPEN; i++)
    {
        Fs.path.root = s_root;
        Fs.path.dir = "/rf";
        Fs.path.name = "";
        Fs.io.mode = PROTOCORE_MNT_READ;
        Fs.open(protocore_filesystem_span());
        handles[i] = Fs.i32;
        TEST_ASSERT_TRUE(handles[i] >= 0);
    }
    char buf[16];
    Fs.path.root = s_root;
    Fs.path.dir = "/rf";
    Fs.path.name = "";
    Fs.io.buf = buf;
    Fs.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, Fs.len);
    for (int i = 0; i < PROTOCORE_MNT_MAX_OPEN; i++)
    {
        Fs.io.handle = handles[i];
        Fs.close(protocore_filesystem_span());
    }
    Fs.path.root = s_root;
    Fs.path.dir = "/rf";
    Fs.path.name = "";
    Fs.io.buf = buf;
    Fs.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(10, Fs.len);
}

void test_write_file_larger_than_capacity()
{
    static uint8_t big[PROTOCORE_MNT_RAM_FILE_SIZE + 16];
    memset(big, 'z', sizeof(big));
    Fs.path.root = s_root;
    Fs.path.dir = "/cap";
    Fs.path.name = "";
    Fs.io.wbuf = big;
    Fs.io.n = sizeof(big);
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/cap";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32((long)PROTOCORE_MNT_RAM_FILE_SIZE, Fs.len);
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
    Fs.path.root = s_root;
    Fs.path.dir = "/x";
    Fs.path.name = "";
    Fs.io.buf = buf;
    Fs.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(0, Fs.len);
    Fs.path.root = s_root;
    Fs.path.dir = "/x";
    Fs.path.name = "";
    Fs.io.wbuf = "abcd";
    Fs.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Mnt.ram(mnt_work);
    MntV.args.backend = MntV.backend;
    Mnt.mount(mnt_work);
    Fs.path.root = s_root;
    Fs.path.dir = "/x";
    Fs.path.name = "";
    Fs.io.wbuf = "abcd";
    Fs.io.n = 4;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
}

void test_root_without_trailing_slash()
{

    Fs.mount = "/gcode";
    Fs.begin(protocore_filesystem_span());
    int gcode = Fs.i32;
    Fs.mount = "/";
    Fs.begin(protocore_filesystem_span());
    int bare = Fs.i32;
    Fs.mount = "/gcode/";
    Fs.begin(protocore_filesystem_span());
    int gcode_slash = Fs.i32;
    TEST_ASSERT_TRUE(gcode >= 0 && bare >= 0 && gcode_slash >= 0);

    Fs.path.root = gcode;
    Fs.path.dir = "/p.nc";
    Fs.path.name = "";
    Fs.io.wbuf = "G0";
    Fs.io.n = 2;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = gcode;
    Fs.path.dir = "/p.nc";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(2, Fs.len);

    Fs.path.root = bare;
    Fs.path.dir = "/gcode/p.nc";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = bare;
    Fs.path.dir = "/gcodep.nc";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);

    Fs.path.root = gcode_slash;
    Fs.path.dir = "/p.nc";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
}

void test_leaf_joins_onto_a_directory()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/d";
    Fs.path.name = "";
    Fs.mkdir(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/d/";
    Fs.path.name = "f.txt";
    Fs.io.wbuf = "xy";
    Fs.io.n = 2;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/d/f.txt";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/d/";
    Fs.path.name = "f.txt";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(2, Fs.len);

    Fs.path.root = s_root;
    Fs.path.dir = "/d/";
    Fs.path.name = "../esc";
    Fs.io.wbuf = "x";
    Fs.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/../d/";
    Fs.path.name = "f.txt";
    Fs.io.wbuf = "x";
    Fs.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
}

void test_remove_takes_the_whole_subtree()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/tree";
    Fs.path.name = "";
    Fs.mkdir(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/tree/";
    Fs.path.name = "a";
    Fs.io.wbuf = "1";
    Fs.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/tree/";
    Fs.path.name = "b";
    Fs.io.wbuf = "22";
    Fs.io.n = 2;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);

    Fs.path.root = s_root;
    Fs.path.dir = "/tree";
    Fs.path.name = "";
    Fs.rmdir(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);

    Fs.path.root = s_root;
    Fs.path.dir = "/tree";
    Fs.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/tree";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/tree/a";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/tree/b";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
}

void test_remove_file_and_missing_unchanged()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/one.txt";
    Fs.path.name = "";
    Fs.io.wbuf = "x";
    Fs.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/one.txt";
    Fs.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/one.txt";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/one.txt";
    Fs.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
}

void test_copy_file()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/c1";
    Fs.path.name = "";
    Fs.io.wbuf = "payload";
    Fs.io.n = 7;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/c1";
    Fs.path.name = "";
    Fs.dest.dir = "/c2";
    Fs.dest.name = "";
    Fs.copy(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/c1";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/c2";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(7, Fs.len);
    char buf[16];
    Fs.path.root = s_root;
    Fs.path.dir = "/c2";
    Fs.path.name = "";
    Fs.io.buf = buf;
    Fs.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = Fs.len;
    TEST_ASSERT_EQUAL_INT32(7, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("payload", buf);
}

void test_copy_takes_the_whole_subtree()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/s";
    Fs.path.name = "";
    Fs.mkdir(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/s/";
    Fs.path.name = "f";
    Fs.io.wbuf = "xyz";
    Fs.io.n = 3;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);

    Fs.path.root = s_root;
    Fs.path.dir = "/s";
    Fs.path.name = "";
    Fs.dest.dir = "/t";
    Fs.dest.name = "";
    Fs.copy(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/t";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/t/f";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    char buf[8];
    Fs.path.root = s_root;
    Fs.path.dir = "/t/f";
    Fs.path.name = "";
    Fs.io.buf = buf;
    Fs.io.n = sizeof(buf);
    Fs.read_file(protocore_filesystem_span());
    long n = Fs.len;
    TEST_ASSERT_EQUAL_INT32(3, n);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("xyz", buf);
    Fs.path.root = s_root;
    Fs.path.dir = "/s/f";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
}

void test_tree_ops_refuse_traversal()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/../escape";
    Fs.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/../escape";
    Fs.path.name = "";
    Fs.dest.dir = "/x";
    Fs.dest.name = "";
    Fs.copy(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/x";
    Fs.path.name = "";
    Fs.dest.dir = "/../escape";
    Fs.dest.name = "";
    Fs.copy(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
}

void test_unbound_root_fails_closed()
{
    Fs.path.root = s_root;
    Fs.path.dir = "/u";
    Fs.path.name = "";
    Fs.io.wbuf = "x";
    Fs.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.path.root = -1;
    Fs.path.dir = "/u";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = -1;
    Fs.path.dir = "/u";
    Fs.path.name = "";
    Fs.remove(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = -1;
    Fs.path.dir = "/u";
    Fs.path.name = "";
    Fs.dest.dir = "/v";
    Fs.dest.name = "";
    Fs.copy(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = s_root;
    Fs.path.dir = "/u";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
}

void test_null_store_is_intentional_and_says_so()
{
    MntV.args.backend = NULL;
    Mnt.mount(mnt_work);
    Fs.clear(protocore_filesystem_span());

    Fs.mount = "/local";
    Fs.begin(protocore_filesystem_span());
    int r = Fs.i32;
    TEST_ASSERT_TRUE(r >= 0);
    Fs.present(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_FS_OK, Fs.bits);

    Fs.path.root = r;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.io.wbuf = "x";
    Fs.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_TRUE((Fs.bits & PROTOCORE_FS_STORAGE_EXHAUSTED) != 0);
    Fs.path.root = r;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.path.root = r;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.size(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT32(-1, Fs.len);

    Mnt.ram(mnt_work);
    MntV.args.backend = MntV.backend;
    Mnt.mount(mnt_work);
    Mnt.ram_format(mnt_work);
    Fs.present(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_TRUE((Fs.bits & PROTOCORE_FS_STORAGE_EXHAUSTED) != 0);
    Fs.clear(protocore_filesystem_span());
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_FS_OK, Fs.bits);
    Fs.path.root = r;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.io.wbuf = "x";
    Fs.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_TRUE(Fs.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_FS_OK, Fs.bits);
}

void test_status_separates_the_reasons()
{
    Fs.clear(protocore_filesystem_span());
    Fs.path.root = -1;
    Fs.path.dir = "/a";
    Fs.path.name = "";
    Fs.exists(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_TRUE((Fs.bits & PROTOCORE_FS_BAD_ROOT) != 0);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_UINT32(0u, Fs.bits & PROTOCORE_FS_TRAVERSAL);

    Fs.clear(protocore_filesystem_span());
    Fs.path.root = s_root;
    Fs.path.dir = "/../escape";
    Fs.path.name = "";
    Fs.io.wbuf = "x";
    Fs.io.n = 1;
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_TRUE((Fs.bits & PROTOCORE_FS_TRAVERSAL) != 0);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_UINT32(0u, Fs.bits & PROTOCORE_FS_BAD_ROOT);

    Fs.clear(protocore_filesystem_span());
    static uint8_t big[PROTOCORE_MNT_RAM_FILE_SIZE + 16];
    memset(big, 'z', sizeof(big));
    Fs.path.root = s_root;
    Fs.path.dir = "/toobig";
    Fs.path.name = "";
    Fs.io.wbuf = big;
    Fs.io.n = sizeof(big);
    Fs.write_file(protocore_filesystem_span());
    TEST_ASSERT_FALSE(Fs.ok);
    Fs.status(protocore_filesystem_span());
    TEST_ASSERT_TRUE((Fs.bits & PROTOCORE_FS_STORAGE_EXHAUSTED) != 0);
}
