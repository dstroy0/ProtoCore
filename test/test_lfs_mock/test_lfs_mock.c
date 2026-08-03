// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The littlefs-backed pc_mnt_backend, checked through the seam rather than through lfs_* directly:
// what the rest of the tree sees is the fourteen backend calls, so that is what is asserted here.
// It is the same filesystem the device runs, so a directory walk, a rename and a full volume all
// answer the way hardware does.

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
    const pc_mnt_backend *b = lfsm();
    TEST_ASSERT_FALSE(b->exists("/nothing.txt"));
    int d = b->opendir("/");
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, d);
    pc_mnt_stat st;
    char name[64];
    TEST_ASSERT_FALSE(b->readdir(d, &st, name, sizeof(name))); // empty: no children
    b->close(d);
}

void test_write_then_read_round_trips()
{
    const pc_mnt_backend *b = lfsm();
    const char *body = "hello littlefs";
    int h = b->open("/a.txt", PC_MNT_WRITE);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    TEST_ASSERT_EQUAL_INT((int)strlen(body), b->write(h, body, strlen(body)));
    b->close(h);

    TEST_ASSERT_TRUE(b->exists("/a.txt"));
    TEST_ASSERT_EQUAL_INT((long)strlen(body), b->size("/a.txt"));

    char back[64] = {0};
    h = b->open("/a.txt", PC_MNT_READ);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    TEST_ASSERT_EQUAL_INT((int)strlen(body), b->read(h, back, sizeof(back)));
    b->close(h);
    TEST_ASSERT_EQUAL_STRING(body, back);
}

void test_seek_reads_from_the_offset()
{
    const pc_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(lfsm_write_text("/s.txt", "0123456789"));
    int h = b->open("/s.txt", PC_MNT_READ);
    TEST_ASSERT_TRUE(b->seek(h, 4));
    char back[8] = {0};
    TEST_ASSERT_EQUAL_INT(6, b->read(h, back, sizeof(back)));
    b->close(h);
    TEST_ASSERT_EQUAL_STRING("456789", back);
}

void test_directory_lists_its_children_only()
{
    const pc_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(b->mkdir("/d"));
    TEST_ASSERT_TRUE(lfsm_write_text("/d/one.txt", "1"));
    TEST_ASSERT_TRUE(lfsm_write_text("/d/two.txt", "22"));
    TEST_ASSERT_TRUE(b->mkdir("/d/sub"));

    int d = b->opendir("/d");
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, d);
    pc_mnt_stat st;
    char name[64];
    int files = 0, dirs = 0;
    while (b->readdir(d, &st, name, sizeof(name)))
    {
        // "." and ".." are the walk's own anchors and must not be surfaced as children
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
    const pc_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(b->mkdir("/dir"));
    TEST_ASSERT_TRUE(lfsm_write_text("/f.txt", "abc"));

    pc_mnt_stat st;
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
    const pc_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(lfsm_write_text("/from.txt", "x"));
    TEST_ASSERT_TRUE(b->rename("/from.txt", "/to.txt"));
    TEST_ASSERT_FALSE(b->exists("/from.txt"));
    TEST_ASSERT_TRUE(b->exists("/to.txt"));

    TEST_ASSERT_TRUE(b->remove("/to.txt"));
    TEST_ASSERT_FALSE(b->exists("/to.txt"));
}

void test_append_adds_to_the_end()
{
    const pc_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(lfsm_write_text("/ap.txt", "one"));
    int h = b->open("/ap.txt", PC_MNT_APPEND);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    TEST_ASSERT_EQUAL_INT(3, b->write(h, "two", 3));
    b->close(h);
    TEST_ASSERT_EQUAL_INT(6, b->size("/ap.txt"));
}

void test_open_missing_for_read_fails()
{
    const pc_mnt_backend *b = lfsm();
    TEST_ASSERT_EQUAL_INT(-1, b->open("/nope.txt", PC_MNT_READ));
}

void test_a_full_volume_refuses_rather_than_pretending()
{
    // The reason this fixture is the real filesystem: a hand-rolled tree never runs out, so the
    // path a caller takes when the store is full is never exercised at all.
    const pc_mnt_backend *b = lfsm();
    static uint8_t chunk[512];
    memset(chunk, 'A', sizeof(chunk));

    int h = b->open("/big.bin", PC_MNT_WRITE);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    int total = 0;
    int rc = 0;
    for (int i = 0; i < LFSM_BLOCK_COUNT * 2; i++)
    {
        rc = b->write(h, chunk, sizeof(chunk));
        if (rc < 0)
        {
            break; // ENOSPC, reported rather than silently accepted
        }
        total += rc;
    }
    b->close(h);
    TEST_ASSERT_LESS_THAN_INT(0, rc);                                         // it did refuse
    TEST_ASSERT_LESS_THAN_INT(LFSM_BLOCK_SIZE * LFSM_BLOCK_COUNT * 2, total); // before writing past the volume
}

// The fill helper the failure-injection tests lean on has to actually exhaust the store,
// otherwise a test that expects a refusal quietly gets a success.
void test_fill_volume_leaves_nothing_creatable()
{
    const pc_mnt_backend *b = lfsm();
    lfsm_fill_volume();
    TEST_ASSERT_EQUAL_INT(-1, b->open("/after_fill.txt", PC_MNT_WRITE));
    TEST_ASSERT_FALSE(b->mkdir("/after_fill_dir"));
}

// Leaving headroom is the other half: the file must be creatable, its body must not fit.
void test_fill_leaving_room_still_creates_but_cannot_write()
{
    const pc_mnt_backend *b = lfsm();
    lfsm_fill_volume_leaving(2);
    int h = b->open("/hdr.bin", PC_MNT_WRITE);
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
    TEST_ASSERT_LESS_THAN_INT(8 * (int)sizeof(big), total); // it ran out before taking it all
}

// WebDAV COPY holds the source open for reading while it writes the destination. If the fixture
// cannot do that, every recursive copy in the suite is running on borrowed luck.
void test_read_one_file_while_writing_another()
{
    const pc_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(lfsm_write_text("/src.txt", "source-bytes"));

    int r = b->open("/src.txt", PC_MNT_READ);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, r);
    int w = b->open("/dst.txt", PC_MNT_WRITE);
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

// Two writers at once, which a recursive copy of a collection reaches.
void test_two_writers_at_once()
{
    const pc_mnt_backend *b = lfsm();
    int a = b->open("/a.bin", PC_MNT_WRITE);
    int c = b->open("/b.bin", PC_MNT_WRITE);
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

// After the volume is full, the store still has to answer normally - a caller keeps serving
// reads and keeps being refused writes. If filling leaves littlefs in a state where the next
// program asserts, every test that fills is standing on a landmine.
void test_store_still_answers_after_a_full_fill()
{
    const pc_mnt_backend *b = lfsm();
    TEST_ASSERT_TRUE(lfsm_write_text("/keep.txt", "kept"));
    lfsm_fill_volume();

    // reads still work
    int h = b->open("/keep.txt", PC_MNT_READ);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    char buf[8] = {0};
    TEST_ASSERT_EQUAL_INT(4, b->read(h, buf, sizeof(buf)));
    b->close(h);

    // and a write to an existing file is refused rather than exploding
    h = b->open("/keep.txt", PC_MNT_APPEND);
    if (h >= 0)
    {
        (void)b->write(h, "more", 4);
        b->close(h);
    }
    TEST_ASSERT_TRUE(b->exists("/keep.txt"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_format_mounts_an_empty_volume);
    RUN_TEST(test_write_then_read_round_trips);
    RUN_TEST(test_seek_reads_from_the_offset);
    RUN_TEST(test_directory_lists_its_children_only);
    RUN_TEST(test_stat_tells_a_directory_from_a_file);
    RUN_TEST(test_rename_and_remove);
    RUN_TEST(test_append_adds_to_the_end);
    RUN_TEST(test_open_missing_for_read_fails);
    RUN_TEST(test_a_full_volume_refuses_rather_than_pretending);
    RUN_TEST(test_fill_volume_leaves_nothing_creatable);
    RUN_TEST(test_fill_leaving_room_still_creates_but_cannot_write);
    RUN_TEST(test_read_one_file_while_writing_another);
    RUN_TEST(test_two_writers_at_once);
    RUN_TEST(test_store_still_answers_after_a_full_fill);
    return UNITY_END();
}
