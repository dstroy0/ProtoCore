// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/sftp: the SFTP protocol v3 wire codec. Covers the reader/writer round-trips, the
// ATTRS blob encode/decode (all flag combos + skip of unknown/extended fields), packet length-framing
// (need-more / complete / malformed / too-big), every response builder (VERSION / STATUS / HANDLE / DATA /
// ATTRS / NAME), a hand-built PC_SSH_FXP_OPEN request parsed back, a multi-entry NAME via the writer API, the
// ls -l longname formatter, reader bounds safety, and builder overflow. Pure host tests (no fs, no SSH).

#include "network_drivers/application/sftp/sftp.h"
#include "server/filesystem/filesystem.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// --- reader / writer primitives ------------------------------------------------------------------
static void test_rw_roundtrip()
{
    uint8_t buf[64];
    SftpWriter w;
    pc_sftp_wr_init(&w, buf, sizeof(buf));
    pc_sftp_wr_u8(&w, 0xAB);
    pc_sftp_wr_u32(&w, 0x11223344);
    pc_sftp_wr_u64(&w, 0x0102030405060708ULL);
    pc_sftp_wr_string(&w, "hello", 5);
    size_t total = pc_sftp_wr_finish(&w);
    TEST_ASSERT_TRUE(total > 0);
    // length prefix == payload size
    TEST_ASSERT_EQUAL_UINT(total - 4, ((uint32_t)buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);

    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, total - 4);
    TEST_ASSERT_EQUAL_HEX8(0xAB, pc_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_HEX32(0x11223344, pc_sftp_rd_u32(&r));
    TEST_ASSERT_EQUAL_HEX64(0x0102030405060708ULL, pc_sftp_rd_u64(&r));
    const uint8_t *s = NULL;
    uint32_t sl = 0;
    TEST_ASSERT_TRUE(pc_sftp_rd_string(&r, &s, &sl));
    TEST_ASSERT_EQUAL_UINT(5, sl);
    TEST_ASSERT_EQUAL_MEMORY("hello", s, 5);
    TEST_ASSERT_TRUE(r.ok);
}

static void test_reader_bounds()
{
    uint8_t buf[3] = {0, 0, 1};
    SftpReader r;
    pc_sftp_rd_init(&r, buf, sizeof(buf));
    pc_sftp_rd_u32(&r); // wants 4, only 3 -> underflow
    TEST_ASSERT_FALSE(r.ok);
    // a string with a length past the end fails without over-reading
    uint8_t s[6] = {0, 0, 0, 100, 'a', 'b'}; // claims 100 bytes, only 2 present
    pc_sftp_rd_init(&r, s, sizeof(s));
    const uint8_t *p = NULL;
    uint32_t n = 0;
    TEST_ASSERT_FALSE(pc_sftp_rd_string(&r, &p, &n));
    TEST_ASSERT_FALSE(r.ok);
}

// --- ATTRS round-trip ----------------------------------------------------------------------------
static void encode_attrs(const SftpAttrs *a, uint8_t *buf, size_t cap, size_t *plen)
{
    SftpWriter w;
    pc_sftp_wr_init(&w, buf, cap);
    pc_sftp_wr_attrs(&w, a);
    size_t total = pc_sftp_wr_finish(&w);
    *plen = total - 4; // payload only
}

static void test_attrs_roundtrip()
{
    SftpAttrs a;
    a.flags = PC_SSH_FILEXFER_ATTR_SIZE | PC_SSH_FILEXFER_ATTR_PERMS | PC_SSH_FILEXFER_ATTR_ACMODTIME;
    a.size = 0x1122334455667788ULL;
    a.permissions = PC_SFTP_S_IFREG | 0644;
    a.atime = 111;
    a.mtime = 222;

    uint8_t buf[64];
    size_t plen = 0;
    encode_attrs(&a, buf, sizeof(buf), &plen);

    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, plen);
    SftpAttrs out;
    TEST_ASSERT_TRUE(pc_sftp_rd_attrs(&r, &out));
    TEST_ASSERT_EQUAL_HEX32(a.flags, out.flags);
    TEST_ASSERT_EQUAL_HEX64(a.size, out.size);
    TEST_ASSERT_EQUAL_HEX32(a.permissions, out.permissions);
    TEST_ASSERT_EQUAL_UINT(a.atime, out.atime);
    TEST_ASSERT_EQUAL_UINT(a.mtime, out.mtime);
    TEST_ASSERT_EQUAL_UINT(plen, r.off); // consumed exactly the blob
}

static void test_attrs_skips_uidgid_and_extended()
{
    // Manually craft an ATTRS with UIDGID + PERMISSIONS + one EXTENDED pair, and confirm perms are recovered.
    uint8_t buf[80];
    SftpWriter w;
    pc_sftp_wr_init(&w, buf, sizeof(buf));
    pc_sftp_wr_u32(&w, PC_SSH_FILEXFER_ATTR_UIDGID | PC_SSH_FILEXFER_ATTR_PERMS | PC_SSH_FILEXFER_ATTR_EXTENDED);
    pc_sftp_wr_u32(&w, 1000); // uid
    pc_sftp_wr_u32(&w, 1000); // gid
    pc_sftp_wr_u32(&w, PC_SFTP_S_IFDIR | 0755);
    pc_sftp_wr_u32(&w, 1); // extended_count
    pc_sftp_wr_string(&w, "x@y", 3);
    pc_sftp_wr_string(&w, "v", 1);
    size_t total = pc_sftp_wr_finish(&w);

    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, total - 4);
    SftpAttrs out;
    TEST_ASSERT_TRUE(pc_sftp_rd_attrs(&r, &out));
    TEST_ASSERT_EQUAL_HEX32(PC_SFTP_S_IFDIR | 0755, out.permissions);
    TEST_ASSERT_EQUAL_UINT(total - 4, r.off); // consumed uid/gid + extended too
}

// --- framing -------------------------------------------------------------------------------------
static void test_framing()
{
    uint8_t buf[16] = {0, 0, 0, 5, PC_SSH_FXP_INIT, 0, 0, 0, 3};
    TEST_ASSERT_EQUAL_UINT(0, pc_sftp_frame_len(buf, 3, sizeof(buf))); // < 4 -> need more
    TEST_ASSERT_EQUAL_UINT(9, pc_sftp_frame_len(buf, 4, sizeof(buf))); // header present -> total 4+5
    TEST_ASSERT_EQUAL_UINT(9, pc_sftp_frame_len(buf, 9, sizeof(buf))); // whole packet
    uint8_t zero[4] = {0, 0, 0, 0};                                    // zero-length -> malformed
    TEST_ASSERT_EQUAL_UINT((size_t)-1, pc_sftp_frame_len(zero, 4, sizeof(buf)));
    uint8_t big[4] = {0, 0, 0xFF, 0xFF}; // 65535 payload, max 16 -> too big
    TEST_ASSERT_EQUAL_UINT((size_t)-1, pc_sftp_frame_len(big, 4, 16));
}

// --- a real request parsed back ------------------------------------------------------------------
static void test_parse_open_request()
{
    uint8_t buf[64];
    SftpWriter w;
    pc_sftp_wr_init(&w, buf, sizeof(buf));
    pc_sftp_wr_u8(&w, PC_SSH_FXP_OPEN);
    pc_sftp_wr_u32(&w, 42);
    pc_sftp_wr_string(&w, "/foo.txt", 8);
    pc_sftp_wr_u32(&w, PC_SSH_FXF_WRITE | PC_SSH_FXF_CREAT | PC_SSH_FXF_TRUNC);
    SftpAttrs empty = {0, 0, 0, 0, 0};
    pc_sftp_wr_attrs(&w, &empty);
    size_t total = pc_sftp_wr_finish(&w);

    TEST_ASSERT_EQUAL_UINT(total, pc_sftp_frame_len(buf, total, sizeof(buf)));
    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, total - 4);
    TEST_ASSERT_EQUAL_UINT8(PC_SSH_FXP_OPEN, pc_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_UINT32(42, pc_sftp_rd_u32(&r));
    const uint8_t *path = NULL;
    uint32_t plen = 0;
    TEST_ASSERT_TRUE(pc_sftp_rd_string(&r, &path, &plen));
    TEST_ASSERT_EQUAL_UINT(8, plen);
    TEST_ASSERT_EQUAL_MEMORY("/foo.txt", path, 8);
    TEST_ASSERT_EQUAL_HEX32(PC_SSH_FXF_WRITE | PC_SSH_FXF_CREAT | PC_SSH_FXF_TRUNC, pc_sftp_rd_u32(&r));
    SftpAttrs a;
    TEST_ASSERT_TRUE(pc_sftp_rd_attrs(&r, &a));
    TEST_ASSERT_EQUAL_HEX32(0, a.flags);
    TEST_ASSERT_TRUE(r.ok);
}

// --- response builders parsed back ---------------------------------------------------------------
static void test_build_version()
{
    uint8_t buf[16];
    size_t n = pc_sftp_build_version(buf, sizeof(buf));
    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, n - 4);
    TEST_ASSERT_EQUAL_UINT8(PC_SSH_FXP_VERSION, pc_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_UINT32(PC_SFTP_VERSION, pc_sftp_rd_u32(&r));
}

static void test_build_status()
{
    uint8_t buf[64];
    size_t n = pc_sftp_build_status(7, PC_SSH_FX_NO_SUCH_FILE, "nope", buf, sizeof(buf));
    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, n - 4);
    TEST_ASSERT_EQUAL_UINT8(PC_SSH_FXP_STATUS, pc_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_UINT32(7, pc_sftp_rd_u32(&r));
    TEST_ASSERT_EQUAL_UINT32(PC_SSH_FX_NO_SUCH_FILE, pc_sftp_rd_u32(&r));
    const uint8_t *m = NULL;
    uint32_t ml = 0;
    TEST_ASSERT_TRUE(pc_sftp_rd_string(&r, &m, &ml));
    TEST_ASSERT_EQUAL_MEMORY("nope", m, 4);
    TEST_ASSERT_TRUE(pc_sftp_rd_string(&r, NULL, &ml)); // language tag (empty)
    TEST_ASSERT_EQUAL_UINT(0, ml);
}

static void test_build_handle_and_data()
{
    uint8_t buf[64];
    size_t n = pc_sftp_build_handle(3, "\x00\x00\x00\x02", 4, buf, sizeof(buf));
    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, n - 4);
    TEST_ASSERT_EQUAL_UINT8(PC_SSH_FXP_HANDLE, pc_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_UINT32(3, pc_sftp_rd_u32(&r));
    const uint8_t *h = NULL;
    uint32_t hl = 0;
    pc_sftp_rd_string(&r, &h, &hl);
    TEST_ASSERT_EQUAL_UINT(4, hl);

    static const uint8_t payload[] = {'a', 0x00, 'b', 0xFF};
    n = pc_sftp_build_data(9, payload, sizeof(payload), buf, sizeof(buf));
    pc_sftp_rd_init(&r, buf + 4, n - 4);
    TEST_ASSERT_EQUAL_UINT8(PC_SSH_FXP_DATA, pc_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_UINT32(9, pc_sftp_rd_u32(&r));
    const uint8_t *d = NULL;
    uint32_t dl = 0;
    pc_sftp_rd_string(&r, &d, &dl);
    TEST_ASSERT_EQUAL_UINT(4, dl);
    TEST_ASSERT_EQUAL_MEMORY(payload, d, 4);
}

static void test_build_name1_realpath()
{
    SftpAttrs a;
    a.flags = PC_SSH_FILEXFER_ATTR_SIZE | PC_SSH_FILEXFER_ATTR_PERMS;
    a.size = 1234;
    a.permissions = PC_SFTP_S_IFREG | 0644;
    a.atime = a.mtime = 0;
    uint8_t buf[128];
    size_t n =
        pc_sftp_build_name1(5, "/gcode/part.nc", "-rw-r--r-- 1 0 0 1234 Jan  1 2026 part.nc", &a, buf, sizeof(buf));
    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, n - 4);
    TEST_ASSERT_EQUAL_UINT8(PC_SSH_FXP_NAME, pc_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_UINT32(5, pc_sftp_rd_u32(&r));
    TEST_ASSERT_EQUAL_UINT32(1, pc_sftp_rd_u32(&r)); // one entry
    const uint8_t *nm = NULL;
    uint32_t nl = 0;
    pc_sftp_rd_string(&r, &nm, &nl);
    TEST_ASSERT_EQUAL_MEMORY("/gcode/part.nc", nm, nl);
    pc_sftp_rd_string(&r, NULL, NULL); // longname
    SftpAttrs ra;
    TEST_ASSERT_TRUE(pc_sftp_rd_attrs(&r, &ra));
    TEST_ASSERT_EQUAL_HEX64(1234, ra.size);
    TEST_ASSERT_TRUE(r.ok);
}

// --- multi-entry NAME via the writer API (READDIR) -----------------------------------------------
static void test_name_multi_entry()
{
    const char *names[3] = {".", "a.nc", "sub"};
    proto_bool dirs[3] = {PROTO_TRUE, PROTO_FALSE, PROTO_TRUE};
    uint8_t buf[256];
    SftpWriter w;
    pc_sftp_wr_init(&w, buf, sizeof(buf));
    pc_sftp_wr_u8(&w, PC_SSH_FXP_NAME);
    pc_sftp_wr_u32(&w, 11);
    size_t count_at = pc_sftp_wr_pos(&w); // remember where the count u32 goes
    pc_sftp_wr_u32(&w, 0);                // placeholder count
    uint32_t count = 0;
    for (int i = 0; i < 3; i++)
    {
        SftpAttrs a;
        a.flags = PC_SSH_FILEXFER_ATTR_PERMS;
        a.permissions = (dirs[i] ? PC_SFTP_S_IFDIR | 0755 : PC_SFTP_S_IFREG | 0644);
        a.size = 0;
        a.atime = a.mtime = 0;
        char ln[64];
        pc_sftp_format_longname(dirs[i], a.permissions, 0, 0, names[i], ln, sizeof(ln));
        pc_sftp_wr_string(&w, names[i], (uint32_t)strlen(names[i]));
        pc_sftp_wr_string(&w, ln, (uint32_t)strlen(ln));
        pc_sftp_wr_attrs(&w, &a);
        count++;
    }
    pc_sftp_wr_patch_u32(&w, count_at, count);
    size_t total = pc_sftp_wr_finish(&w);

    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, total - 4);
    TEST_ASSERT_EQUAL_UINT8(PC_SSH_FXP_NAME, pc_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_UINT32(11, pc_sftp_rd_u32(&r));
    TEST_ASSERT_EQUAL_UINT32(3, pc_sftp_rd_u32(&r)); // count patched
    for (int i = 0; i < 3; i++)
    {
        const uint8_t *nm = NULL;
        uint32_t nl = 0;
        pc_sftp_rd_string(&r, &nm, &nl);
        TEST_ASSERT_EQUAL_MEMORY(names[i], nm, nl);
        pc_sftp_rd_string(&r, NULL, NULL); // longname
        SftpAttrs a;
        pc_sftp_rd_attrs(&r, &a);
    }
    TEST_ASSERT_TRUE(r.ok);
}

static void test_longname_format()
{
    char out[64];
    size_t n = pc_sftp_format_longname(PROTO_FALSE, 0644, 1234, 0, "file.nc", out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_MEMORY("-rw-r--r-- ", out, 11); // mode string for a regular 0644 file
    TEST_ASSERT_NOT_NULL(strstr(out, "1234"));
    TEST_ASSERT_NOT_NULL(strstr(out, "file.nc"));

    n = pc_sftp_format_longname(PROTO_TRUE, 0755, 0, 0, "dir", out, sizeof(out));
    TEST_ASSERT_EQUAL_MEMORY("drwxr-xr-x ", out, 11); // directory
}

static void test_builder_overflow()
{
    uint8_t tiny[6];
    TEST_ASSERT_EQUAL_UINT(
        0, pc_sftp_build_status(1, PC_SSH_FX_FAILURE, "a message too long for the buffer", tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_UINT(0, pc_sftp_build_data(1, "0123456789", 10, tiny, sizeof(tiny)));
}

// --- shared path-traversal guard (server/filesystem/filesystem.h) -------------------------------------------
static void test_pc_fs_resolve()
{
    char out[128];
    TEST_ASSERT_EQUAL_INT(0, pc_fs_resolve("/gcode/", "/part.nc", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/gcode/part.nc", out);
    // root "/" + "/x" collapses the double slash
    TEST_ASSERT_EQUAL_INT(0, pc_fs_resolve("/", "/x", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/x", out);
    // a trailing slash is dropped
    TEST_ASSERT_EQUAL_INT(0, pc_fs_resolve("/gcode/", "/sub/", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/gcode/sub", out);
    // traversal is refused
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_resolve("/gcode/", "/../etc/passwd", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_resolve("/gcode/", "/sub/../../x", "", out, sizeof(out)));
    // overflow is refused
    char small[8];
    TEST_ASSERT_EQUAL_INT(-2, pc_fs_resolve("/gcode/", "/a-very-long-subpath-name", "", small, sizeof(small)));
}

// --- reader / writer failure latching -------------------------------------------------------------
static void test_reader_latches_the_first_underflow()
{
    // Once a read runs past the end the reader stays failed: every later read short-circuits on !ok and
    // returns a zero rather than reading whatever follows in memory.
    uint8_t one[1] = {0x7E};
    SftpReader r;
    pc_sftp_rd_init(&r, one, sizeof(one));
    TEST_ASSERT_EQUAL_HEX8(0x7E, pc_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_HEX8(0, pc_sftp_rd_u8(&r)); // one byte past the end
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_HEX8(0, pc_sftp_rd_u8(&r)); // already failed
    TEST_ASSERT_EQUAL_HEX32(0, pc_sftp_rd_u32(&r));
    TEST_ASSERT_EQUAL_HEX64(0, pc_sftp_rd_u64(&r));

    uint8_t three[3] = {1, 2, 3};
    pc_sftp_rd_init(&r, three, sizeof(three));
    TEST_ASSERT_EQUAL_HEX64(0, pc_sftp_rd_u64(&r)); // wants 8, only 3
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_HEX64(0, pc_sftp_rd_u64(&r)); // already failed

    // A string whose own length prefix cannot be read fails on the prefix, not on the body.
    uint8_t two[2] = {0, 0};
    pc_sftp_rd_init(&r, two, sizeof(two));
    TEST_ASSERT_FALSE(pc_sftp_rd_string(&r, NULL, NULL));
    TEST_ASSERT_FALSE(r.ok);
}

static void test_attrs_extended_stops_on_a_truncated_pair()
{
    // The extension walk is bounded by the reader's health as well as the declared count, so a count
    // that overstates what is present stops rather than spinning on a dead reader.
    uint8_t buf[64];
    SftpWriter w;
    pc_sftp_wr_init(&w, buf, sizeof(buf));
    pc_sftp_wr_u32(&w, PC_SSH_FILEXFER_ATTR_EXTENDED);
    pc_sftp_wr_u32(&w, 3);         // claims three extension pairs...
    pc_sftp_wr_string(&w, "a", 1); // ...but only one string is actually present
    size_t total = pc_sftp_wr_finish(&w);

    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, total - 4);
    SftpAttrs out;
    TEST_ASSERT_FALSE(pc_sftp_rd_attrs(&r, &out));
    TEST_ASSERT_FALSE(r.ok);
}

static void test_writer_latches_overflow_at_every_primitive()
{
    // A writer that has overflowed stays overflowed and writes nothing more, so a caller that ignores
    // the flag until finish() still cannot emit a partial packet.
    uint8_t buf[16];
    memset(buf, 0x5A, sizeof(buf));

    SftpWriter w;
    pc_sftp_wr_init(&w, buf, 4); // the length prefix uses the whole buffer
    TEST_ASSERT_FALSE(w.ovf);
    pc_sftp_wr_u8(&w, 0x11);
    TEST_ASSERT_TRUE(w.ovf);
    pc_sftp_wr_u8(&w, 0x22); // already overflowed
    TEST_ASSERT_EQUAL_UINT(0, pc_sftp_wr_finish(&w));

    pc_sftp_wr_init(&w, buf, 8); // 4 spare bytes, a u64 needs 8
    pc_sftp_wr_u64(&w, 0x1122334455667788ULL);
    TEST_ASSERT_TRUE(w.ovf);
    pc_sftp_wr_u64(&w, 0);         // already overflowed
    pc_sftp_wr_bytes(&w, "xy", 2); // already overflowed
    TEST_ASSERT_EQUAL_UINT(0, pc_sftp_wr_finish(&w));

    // A capacity that cannot even hold the length prefix starts out overflowed.
    pc_sftp_wr_init(&w, buf, 3);
    TEST_ASSERT_TRUE(w.ovf);
    TEST_ASSERT_EQUAL_UINT(0, pc_sftp_wr_finish(&w));

    for (size_t i = 0; i < sizeof(buf); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x5A, buf[i]); // not one byte was written
    }

    // A healthy writer whose remaining room fits the length prefix but not the body overflows on the body.
    uint8_t body[14];
    TEST_ASSERT_EQUAL_UINT(0, pc_sftp_build_data(1, "abcde", 5, body, sizeof(body)));
}

static void test_build_attrs()
{
    // PC_SSH_FXP_ATTRS is the STAT/FSTAT reply: header, request id, then the attribute blob.
    SftpAttrs a;
    a.flags = PC_SSH_FILEXFER_ATTR_SIZE | PC_SSH_FILEXFER_ATTR_PERMS | PC_SSH_FILEXFER_ATTR_ACMODTIME;
    a.size = 4096;
    a.permissions = PC_SFTP_S_IFREG | 0644;
    a.atime = 1700000000u;
    a.mtime = 1700000123u;

    uint8_t buf[64];
    size_t n = pc_sftp_build_attrs(77, &a, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, n - 4);
    TEST_ASSERT_EQUAL_UINT8(PC_SSH_FXP_ATTRS, pc_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_UINT32(77, pc_sftp_rd_u32(&r));
    SftpAttrs out;
    TEST_ASSERT_TRUE(pc_sftp_rd_attrs(&r, &out));
    TEST_ASSERT_EQUAL_HEX32(a.flags, out.flags);
    TEST_ASSERT_EQUAL_HEX64(a.size, out.size);
    TEST_ASSERT_EQUAL_HEX32(a.permissions, out.permissions);
    TEST_ASSERT_EQUAL_UINT32(a.atime, out.atime);
    TEST_ASSERT_EQUAL_UINT32(a.mtime, out.mtime);
    TEST_ASSERT_EQUAL_UINT(n - 4, r.off);

    // And it fails closed rather than emitting a partial blob when the buffer is too small.
    uint8_t tiny[8];
    TEST_ASSERT_EQUAL_UINT(0, pc_sftp_build_attrs(77, &a, tiny, sizeof(tiny)));
}

static void test_wr_attrs_emits_uidgid()
{
    // UIDGID is written as a zero pair (the server has no user model) and must round-trip past the reader.
    SftpAttrs a;
    a.flags = PC_SSH_FILEXFER_ATTR_UIDGID | PC_SSH_FILEXFER_ATTR_PERMS;
    a.size = 0;
    a.permissions = PC_SFTP_S_IFREG | 0600;
    a.atime = a.mtime = 0;

    uint8_t buf[64];
    SftpWriter w;
    pc_sftp_wr_init(&w, buf, sizeof(buf));
    pc_sftp_wr_attrs(&w, &a);
    size_t total = pc_sftp_wr_finish(&w);
    TEST_ASSERT_EQUAL_UINT(4 + 4 + 4 + 4 + 4, total); // flags + uid + gid + permissions

    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, total - 4);
    SftpAttrs out;
    TEST_ASSERT_TRUE(pc_sftp_rd_attrs(&r, &out));
    TEST_ASSERT_EQUAL_HEX32(a.flags, out.flags);
    TEST_ASSERT_EQUAL_HEX32(a.permissions, out.permissions);
    TEST_ASSERT_EQUAL_UINT(total - 4, r.off);
}

static void test_patch_u32_past_the_buffer_is_ignored()
{
    // The count patch is a blind poke at a remembered offset; a patch that would run off the end must
    // do nothing rather than write past the caller's buffer.
    uint8_t buf[16];
    memset(buf, 0x5A, sizeof(buf));
    SftpWriter w;
    pc_sftp_wr_init(&w, buf, sizeof(buf));
    pc_sftp_wr_patch_u32(&w, sizeof(buf) - 3, 0x11223344); // needs 4 bytes, only 3 left
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x5A, buf[i]);
    }
}

static void test_build_status_without_a_message()
{
    // A null message is a legal STATUS: it goes on the wire as a zero-length string, not a crash.
    uint8_t buf[64];
    size_t n = pc_sftp_build_status(4, PC_SSH_FX_OK, NULL, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, n - 4);
    TEST_ASSERT_EQUAL_UINT8(PC_SSH_FXP_STATUS, pc_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_UINT32(4, pc_sftp_rd_u32(&r));
    TEST_ASSERT_EQUAL_UINT32(PC_SSH_FX_OK, pc_sftp_rd_u32(&r));
    uint32_t ml = 0xFFFF;
    TEST_ASSERT_TRUE(pc_sftp_rd_string(&r, NULL, &ml));
    TEST_ASSERT_EQUAL_UINT32(0, ml); // empty message
    TEST_ASSERT_TRUE(pc_sftp_rd_string(&r, NULL, &ml));
    TEST_ASSERT_EQUAL_UINT32(0, ml); // empty language tag
    TEST_ASSERT_TRUE(r.ok);
}

static void test_longname_truncates_to_the_buffer()
{
    // A longname that does not fit is clipped to the buffer (NUL included) and the clipped length is
    // reported, so the caller never emits more bytes than it owns.
    char full[64];
    size_t nfull = pc_sftp_format_longname(PROTO_FALSE, 0644, 1234, 0, "file.nc", full, sizeof(full));
    TEST_ASSERT_TRUE(nfull > 12);

    char small[12];
    memset(small, 0x7F, sizeof(small));
    size_t n = pc_sftp_format_longname(PROTO_FALSE, 0644, 1234, 0, "file.nc", small, sizeof(small));
    TEST_ASSERT_EQUAL_UINT(sizeof(small) - 1, n);
    TEST_ASSERT_EQUAL_CHAR('\0', small[sizeof(small) - 1]);
    TEST_ASSERT_EQUAL_MEMORY(full, small, sizeof(small) - 1); // the prefix that did fit

    // A zero-capacity buffer writes nothing and reports nothing.
    char none[1] = {0x7F};
    TEST_ASSERT_EQUAL_UINT(0, pc_sftp_format_longname(PROTO_FALSE, 0644, 1234, 0, "file.nc", none, 0));
    TEST_ASSERT_EQUAL_CHAR(0x7F, none[0]);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_pc_fs_resolve);
    RUN_TEST(test_rw_roundtrip);
    RUN_TEST(test_reader_bounds);
    RUN_TEST(test_attrs_roundtrip);
    RUN_TEST(test_attrs_skips_uidgid_and_extended);
    RUN_TEST(test_framing);
    RUN_TEST(test_parse_open_request);
    RUN_TEST(test_build_version);
    RUN_TEST(test_build_status);
    RUN_TEST(test_build_handle_and_data);
    RUN_TEST(test_build_name1_realpath);
    RUN_TEST(test_name_multi_entry);
    RUN_TEST(test_longname_format);
    RUN_TEST(test_builder_overflow);
    RUN_TEST(test_reader_latches_the_first_underflow);
    RUN_TEST(test_attrs_extended_stops_on_a_truncated_pair);
    RUN_TEST(test_writer_latches_overflow_at_every_primitive);
    RUN_TEST(test_build_attrs);
    RUN_TEST(test_wr_attrs_emits_uidgid);
    RUN_TEST(test_patch_u32_past_the_buffer_is_ignored);
    RUN_TEST(test_build_status_without_a_message);
    RUN_TEST(test_longname_truncates_to_the_buffer);
    return UNITY_END();
}
