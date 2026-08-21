// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "lfs_mock.h"
#include <string.h>

#include <unity.h>

void setUp()
{
    lfsm_format();
}
void tearDown()
{
}

void test_format_mounts_an_empty_volume()
{
    const protocore_mnt_backend *b = lfsm();
    TEST_ASSERT_FALSE(b->exists("/nothing.txt"));
    int d = b->opendir("/");
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, d);
    protocore_mnt_stat st;
    char name[64];
    TEST_ASSERT_FALSE(b->readdir(d, &st, name, sizeof(name)));
    b->close(d);
}

void test_write_then_read_round_trips()
{
    const protocore_mnt_backend *b = lfsm();
    const char *body = "hello littlefs";
    int h = b->open("/a.txt", PROTOCORE_MNT_WRITE);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    TEST_ASSERT_EQUAL_INT((int)strlen(body), b->write(h, body, strlen(body)));
    b->close(h);

    TEST_ASSERT_TRUE(b->exists("/a.txt"));
    TEST_ASSERT_EQUAL_INT((long)strlen(body), b->size("/a.txt"));

    char back[64] = {0};
    h = b->open("/a.txt", PROTOCORE_MNT_READ);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    TEST_ASSERT_EQUAL_INT((int)strlen(body), b->read(h, back, sizeof(back)));
    b->close(h);
    TEST_ASSERT_EQUAL_STRING(body, back);
}

void test_seek_reads_from_the_offset()
{
    const protocore_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(lfsm_write_text("/s.txt", "0123456789"));
    int h = b->open("/s.txt", PROTOCORE_MNT_READ);
    TEST_ASSERT_TRUE(b->seek(h, 4));
    char back[8] = {0};
    TEST_ASSERT_EQUAL_INT(6, b->read(h, back, sizeof(back)));
    b->close(h);
    TEST_ASSERT_EQUAL_STRING("456789", back);
}

void test_directory_lists_its_children_only()
{
    const protocore_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(b->mkdir("/d"));
    TEST_ASSERT_TRUE(lfsm_write_text("/d/one.txt", "1"));
    TEST_ASSERT_TRUE(lfsm_write_text("/d/two.txt", "22"));
    TEST_ASSERT_TRUE(b->mkdir("/d/sub"));

    int d = b->opendir("/d");
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, d);
    protocore_mnt_stat st;
    char name[64];
    int files = 0, dirs = 0;
    while (b->readdir(d, &st, name, sizeof(name)))
    {

        TEST_ASSERT_NOT_EQUAL(0, strcmp(name, "."));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(name, ".."));
        if (st.is_dir)
        {
            dirs++;
        }
        else
        {
            files++;
        }
    }
    b->close(d);
    TEST_ASSERT_EQUAL_INT(2, files);
    TEST_ASSERT_EQUAL_INT(1, dirs);
}

void test_stat_tells_a_directory_from_a_file()
{
    const protocore_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(b->mkdir("/dir"));
    TEST_ASSERT_TRUE(lfsm_write_text("/f.txt", "abc"));

    protocore_mnt_stat st;
    TEST_ASSERT_TRUE(b->stat("/dir", &st));
    TEST_ASSERT_TRUE(st.is_dir);
    TEST_ASSERT_EQUAL_UINT64(0, st.size);

    TEST_ASSERT_TRUE(b->stat("/f.txt", &st));
    TEST_ASSERT_FALSE(st.is_dir);
    TEST_ASSERT_EQUAL_UINT64(3, st.size);

    TEST_ASSERT_FALSE(b->stat("/absent", &st));
}

void test_rename_and_remove()
{
    const protocore_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(lfsm_write_text("/from.txt", "x"));
    TEST_ASSERT_TRUE(b->rename("/from.txt", "/to.txt"));
    TEST_ASSERT_FALSE(b->exists("/from.txt"));
    TEST_ASSERT_TRUE(b->exists("/to.txt"));

    TEST_ASSERT_TRUE(b->remove("/to.txt"));
    TEST_ASSERT_FALSE(b->exists("/to.txt"));
}

void test_append_adds_to_the_end()
{
    const protocore_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(lfsm_write_text("/ap.txt", "one"));
    int h = b->open("/ap.txt", PROTOCORE_MNT_APPEND);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    TEST_ASSERT_EQUAL_INT(3, b->write(h, "two", 3));
    b->close(h);
    TEST_ASSERT_EQUAL_INT(6, b->size("/ap.txt"));
}

void test_open_missing_for_read_fails()
{
    const protocore_mnt_backend *b = lfsm();
    TEST_ASSERT_EQUAL_INT(-1, b->open("/nope.txt", PROTOCORE_MNT_READ));
}

void test_a_full_volume_refuses_rather_than_pretending()
{

    const protocore_mnt_backend *b = lfsm();
    static uint8_t chunk[512];
    memset(chunk, 'A', sizeof(chunk));

    int h = b->open("/big.bin", PROTOCORE_MNT_WRITE);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    int total = 0;
    int rc = 0;
    for (int i = 0; i < LFSM_BLOCK_COUNT * 2; i++)
    {
        rc = b->write(h, chunk, sizeof(chunk));
        if (rc < 0)
        {
            break;
        }
        total += rc;
    }
    b->close(h);
    TEST_ASSERT_LESS_THAN_INT(0, rc);
    TEST_ASSERT_LESS_THAN_INT(LFSM_BLOCK_SIZE * LFSM_BLOCK_COUNT * 2, total);
}

void test_fill_volume_leaves_nothing_creatable()
{
    const protocore_mnt_backend *b = lfsm();
    lfsm_fill_volume();
    TEST_ASSERT_EQUAL_INT(-1, b->open("/after_fill.txt", PROTOCORE_MNT_WRITE));
    TEST_ASSERT_FALSE(b->mkdir("/after_fill_dir"));
}

void test_fill_leaving_room_still_creates_but_cannot_write()
{
    const protocore_mnt_backend *b = lfsm();
    lfsm_fill_volume_leaving(2);
    int h = b->open("/hdr.bin", PROTOCORE_MNT_WRITE);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    static uint8_t big[4096];
    memset(big, 'B', sizeof(big));
    int total = 0, rc = 0;
    for (int i = 0; i < 8; i++)
    {
        rc = b->write(h, big, sizeof(big));
        if (rc < 0)
        {
            break;
        }
        total += rc;
    }
    b->close(h);
    TEST_ASSERT_LESS_THAN_INT(8 * (int)sizeof(big), total);
}

void test_read_one_file_while_writing_another()
{
    const protocore_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(lfsm_write_text("/src.txt", "source-bytes"));

    int r = b->open("/src.txt", PROTOCORE_MNT_READ);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, r);
    int w = b->open("/dst.txt", PROTOCORE_MNT_WRITE);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, w);

    char buf[8];
    for (;;)
    {
        int n = b->read(r, buf, sizeof(buf));
        if (n <= 0)
        {
            break;
        }
        TEST_ASSERT_EQUAL_INT(n, b->write(w, buf, (size_t)n));
    }
    b->close(w);
    b->close(r);
    TEST_ASSERT_EQUAL_INT(12, b->size("/dst.txt"));
}

void test_two_writers_at_once()
{
    const protocore_mnt_backend *b = lfsm();
    int a = b->open("/a.bin", PROTOCORE_MNT_WRITE);
    int c = b->open("/b.bin", PROTOCORE_MNT_WRITE);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, a);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, c);
    for (int i = 0; i < 40; i++)
    {
        TEST_ASSERT_EQUAL_INT(4, b->write(a, "aaaa", 4));
        TEST_ASSERT_EQUAL_INT(4, b->write(c, "bbbb", 4));
    }
    b->close(a);
    b->close(c);
    TEST_ASSERT_EQUAL_INT(160, b->size("/a.bin"));
    TEST_ASSERT_EQUAL_INT(160, b->size("/b.bin"));
}

void test_store_still_answers_after_a_full_fill()
{
    const protocore_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(lfsm_write_text("/keep.txt", "kept"));
    lfsm_fill_volume();

    int h = b->open("/keep.txt", PROTOCORE_MNT_READ);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    char buf[8] = {0};
    TEST_ASSERT_EQUAL_INT(4, b->read(h, buf, sizeof(buf)));
    b->close(h);

    h = b->open("/keep.txt", PROTOCORE_MNT_APPEND);
    if (h >= 0)
    {
        (void)b->write(h, "more", 4);
        b->close(h);
    }
    TEST_ASSERT_TRUE(b->exists("/keep.txt"));
}

void test_medium_error_refuses_a_write_and_leaves_the_store_usable()
{
    const protocore_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(lfsm_write_text("/before.txt", "ok"));

    lfsm_fail_prog_after(1);
    int h = b->open("/fails.txt", PROTOCORE_MNT_WRITE);
    int rc = 0;
    if (h >= 0)
    {
        rc = b->write(h, "payload", 7);
        b->close(h);
    }
    lfsm_no_prog_failure();
    TEST_ASSERT_TRUE(h < 0 || rc < 7);

    TEST_ASSERT_TRUE(b->exists("/before.txt"));
    TEST_ASSERT_TRUE(lfsm_write_text("/after.txt", "still-here"));
    TEST_ASSERT_EQUAL_INT(10, b->size("/after.txt"));
}
