// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SFTP version 3 wire codec (network_drivers/application/sftp/sftp.h).
//
// draft-ietf-secsh-filexfer-02 sec 3 states the framing in one sentence: "The `length' is the
// length of the data area, and does not include the `length' field itself." Every packet this
// module builds stands or falls on that one arithmetic, because an SFTP peer reads the next
// packet's boundary from it - a length off by four resynchronizes the whole stream onto garbage.
// test_packet_length_excludes_the_length_field is therefore the load-bearing case, checked against
// a SSH_FXP_VERSION packet whose payload sec 4 fixes at exactly five octets.
//
// The rest is anchored on the draft's published constants (sec 3 packet types, sec 5 ATTRS flag
// bits and field order, sec 6.3 pflags, sec 7 status codes and response layouts) and on POSIX.1
// permission bits for the longname's mode column.

#include "network_drivers/application/sftp/sftp.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// sec 3: "uint32 length / byte type / byte[length - 1] data payload". sec 4 gives SSH_FXP_VERSION
// a single uint32 body, so its data area is 1 (type) + 4 (version) = 5 octets and the whole packet
// is 4 + 5 = 9. A length field of 9 would be the count including itself, which the draft forbids.
void test_packet_length_excludes_the_length_field(void)
{
    uint8_t out[32];
    size_t n = protocore_sftp_build_version(out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(9, n);

    static const uint8_t WANT[9] = {
        0x00, 0x00, 0x00, 0x05, // length = the 5 octets of data that follow
        0x02,                   // SSH_FXP_VERSION
        0x00, 0x00, 0x00, 0x03  // version 3
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    // Stated the other way: the length field is always the total minus the four it sits in.
    SftpReader r;
    protocore_sftp_rd_init(&r, out, n);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(n - 4), protocore_sftp_rd_u32(&r));
}

// sec 3 lists the packet type numbers, sec 7 the status codes, sec 5 the ATTRS flag bits and
// sec 6.3 the SSH_FXP_OPEN pflags. Every one of these is a value a peer decodes by number.
void test_protocol_constants(void)
{
    TEST_ASSERT_EQUAL_UINT32(3, PROTOCORE_SFTP_VERSION);

    TEST_ASSERT_EQUAL_UINT8(1, PROTOCORE_SSH_FXP_INIT);
    TEST_ASSERT_EQUAL_UINT8(2, PROTOCORE_SSH_FXP_VERSION);
    TEST_ASSERT_EQUAL_UINT8(3, PROTOCORE_SSH_FXP_OPEN);
    TEST_ASSERT_EQUAL_UINT8(4, PROTOCORE_SSH_FXP_CLOSE);
    TEST_ASSERT_EQUAL_UINT8(5, PROTOCORE_SSH_FXP_READ);
    TEST_ASSERT_EQUAL_UINT8(6, PROTOCORE_SSH_FXP_WRITE);
    TEST_ASSERT_EQUAL_UINT8(7, PROTOCORE_SSH_FXP_LSTAT);
    TEST_ASSERT_EQUAL_UINT8(8, PROTOCORE_SSH_FXP_FSTAT);
    TEST_ASSERT_EQUAL_UINT8(9, PROTOCORE_SSH_FXP_SETSTAT);
    TEST_ASSERT_EQUAL_UINT8(10, PROTOCORE_SSH_FXP_FSETSTAT);
    TEST_ASSERT_EQUAL_UINT8(11, PROTOCORE_SSH_FXP_OPENDIR);
    TEST_ASSERT_EQUAL_UINT8(12, PROTOCORE_SSH_FXP_READDIR);
    TEST_ASSERT_EQUAL_UINT8(13, PROTOCORE_SSH_FXP_REMOVE);
    TEST_ASSERT_EQUAL_UINT8(14, PROTOCORE_SSH_FXP_MKDIR);
    TEST_ASSERT_EQUAL_UINT8(15, PROTOCORE_SSH_FXP_RMDIR);
    TEST_ASSERT_EQUAL_UINT8(16, PROTOCORE_SSH_FXP_REALPATH);
    TEST_ASSERT_EQUAL_UINT8(17, PROTOCORE_SSH_FXP_STAT);
    TEST_ASSERT_EQUAL_UINT8(18, PROTOCORE_SSH_FXP_RENAME);
    TEST_ASSERT_EQUAL_UINT8(101, PROTOCORE_SSH_FXP_STATUS);
    TEST_ASSERT_EQUAL_UINT8(102, PROTOCORE_SSH_FXP_HANDLE);
    TEST_ASSERT_EQUAL_UINT8(103, PROTOCORE_SSH_FXP_DATA);
    TEST_ASSERT_EQUAL_UINT8(104, PROTOCORE_SSH_FXP_NAME);
    TEST_ASSERT_EQUAL_UINT8(105, PROTOCORE_SSH_FXP_ATTRS);

    TEST_ASSERT_EQUAL_UINT32(0, PROTOCORE_SSH_FX_OK);
    TEST_ASSERT_EQUAL_UINT32(1, PROTOCORE_SSH_FX_EOF);
    TEST_ASSERT_EQUAL_UINT32(2, PROTOCORE_SSH_FX_NO_SUCH_FILE);
    TEST_ASSERT_EQUAL_UINT32(3, PROTOCORE_SSH_FX_PERMISSION_DENIED);
    TEST_ASSERT_EQUAL_UINT32(4, PROTOCORE_SSH_FX_FAILURE);
    TEST_ASSERT_EQUAL_UINT32(5, PROTOCORE_SSH_FX_BAD_MESSAGE);
    TEST_ASSERT_EQUAL_UINT32(8, PROTOCORE_SSH_FX_OP_UNSUPPORTED);

    TEST_ASSERT_EQUAL_HEX32(0x00000001u, PROTOCORE_SSH_FILEXFER_ATTR_SIZE);
    TEST_ASSERT_EQUAL_HEX32(0x00000002u, PROTOCORE_SSH_FILEXFER_ATTR_UIDGID);
    TEST_ASSERT_EQUAL_HEX32(0x00000004u, PROTOCORE_SSH_FILEXFER_ATTR_PERMS);
    TEST_ASSERT_EQUAL_HEX32(0x00000008u, PROTOCORE_SSH_FILEXFER_ATTR_ACMODTIME);
    TEST_ASSERT_EQUAL_HEX32(0x80000000u, PROTOCORE_SSH_FILEXFER_ATTR_EXTENDED);

    TEST_ASSERT_EQUAL_HEX32(0x00000001u, PROTOCORE_SSH_FXF_READ);
    TEST_ASSERT_EQUAL_HEX32(0x00000002u, PROTOCORE_SSH_FXF_WRITE);
    TEST_ASSERT_EQUAL_HEX32(0x00000004u, PROTOCORE_SSH_FXF_APPEND);
    TEST_ASSERT_EQUAL_HEX32(0x00000008u, PROTOCORE_SSH_FXF_CREAT);
    TEST_ASSERT_EQUAL_HEX32(0x00000010u, PROTOCORE_SSH_FXF_TRUNC);
    TEST_ASSERT_EQUAL_HEX32(0x00000020u, PROTOCORE_SSH_FXF_EXCL);
}

// sec 7: "uint32 id / uint32 error/status code / string error message / string language tag".
// Octet by octet, with id 7 and SSH_FX_NO_SUCH_FILE:
//   00 00 00 13   length = 19 = 1 type + 4 id + 4 code + (4+2) message + (4+0) language tag
//   65            SSH_FXP_STATUS = 101
//   00 00 00 07   id
//   00 00 00 02   SSH_FX_NO_SUCH_FILE
//   00 00 00 02 6E 6F   "no"
//   00 00 00 00   the empty language tag, still a string
void test_status_response_layout(void)
{
    uint8_t out[64];
    size_t n = protocore_sftp_build_status(7, PROTOCORE_SSH_FX_NO_SUCH_FILE, "no", out, sizeof(out));
    static const uint8_t WANT[23] = {0x00, 0x00, 0x00, 0x13, 0x65, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
                                     0x02, 0x00, 0x00, 0x00, 0x02, 0x6E, 0x6F, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, n);

    // A null message is still a present, zero-length string: the language tag must follow it.
    n = protocore_sftp_build_status(1, PROTOCORE_SSH_FX_OK, NULL, out, sizeof(out));
    static const uint8_t WANT_EMPTY[21] = {0x00, 0x00, 0x00, 0x11, 0x65, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT_EMPTY), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_EMPTY, out, n);
}

// sec 7: SSH_FXP_HANDLE is "uint32 id / string handle", SSH_FXP_DATA is "uint32 id / string data".
// A string is a uint32 count followed by that many raw octets, so a handle containing a NUL is
// carried whole rather than cut at it.
void test_handle_and_data_responses(void)
{
    uint8_t out[64];
    static const uint8_t HANDLE[4] = {0x00, 0x01, 0x00, 0x02};
    size_t n = protocore_sftp_build_handle(0x11223344u, HANDLE, sizeof(HANDLE), out, sizeof(out));
    static const uint8_t WANT_H[17] = {0x00, 0x00, 0x00, 0x0D, 0x66, 0x11, 0x22, 0x33, 0x44,
                                       0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT_H), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_H, out, n);

    n = protocore_sftp_build_data(2, "abc", 3, out, sizeof(out));
    static const uint8_t WANT_D[16] = {0x00, 0x00, 0x00, 0x0C, 0x67, 0x00, 0x00, 0x00,
                                       0x02, 0x00, 0x00, 0x00, 0x03, 0x61, 0x62, 0x63};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT_D), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_D, out, n);

    // A zero-length DATA string is the legal way to say "nothing read", not an omitted field.
    n = protocore_sftp_build_data(2, "", 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(13, n); // 4 length + 1 type + 4 id + 4 string count
}

// sec 5 fixes the ATTRS field order: flags, then size, then uid and gid, then permissions, then
// atime and mtime, each present only if its flag bit is set. With SIZE|PERMS set the uid/gid pair
// is absent, so permissions must sit immediately after the eight octets of size.
void test_attrs_field_order_and_presence(void)
{
    SftpAttrs a;
    memset(&a, 0, sizeof(a));
    a.flags = PROTOCORE_SSH_FILEXFER_ATTR_SIZE | PROTOCORE_SSH_FILEXFER_ATTR_PERMS;
    a.size = 0x0102030405060708ULL;
    a.permissions = 0100644u; // S_IFREG | rw-r--r--, which is 33188 = 0x81A4

    uint8_t out[64];
    size_t n = protocore_sftp_build_attrs(3, &a, out, sizeof(out));
    static const uint8_t WANT[25] = {
        0x00, 0x00, 0x00, 0x15,                         // length 21
        0x69,                                           // SSH_FXP_ATTRS = 105
        0x00, 0x00, 0x00, 0x03,                         // id
        0x00, 0x00, 0x00, 0x05,                         // flags = SIZE | PERMISSIONS
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // size
        0x00, 0x00, 0x81, 0xA4                          // permissions
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, n);

    // With no flags set the blob is the flag word alone.
    memset(&a, 0, sizeof(a));
    a.size = 999;
    a.permissions = 0777;
    n = protocore_sftp_build_attrs(0, &a, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(13, n); // 4 length + 1 type + 4 id + 4 flags
}

// The ATTRS reader must land on the same values the writer emitted, for every combination of the
// flag bits the module carries.
void test_attrs_round_trip(void)
{
    static const uint32_t FLAGS[] = {
        0,
        PROTOCORE_SSH_FILEXFER_ATTR_SIZE,
        PROTOCORE_SSH_FILEXFER_ATTR_PERMS,
        PROTOCORE_SSH_FILEXFER_ATTR_ACMODTIME,
        PROTOCORE_SSH_FILEXFER_ATTR_SIZE | PROTOCORE_SSH_FILEXFER_ATTR_PERMS,
        PROTOCORE_SSH_FILEXFER_ATTR_SIZE | PROTOCORE_SSH_FILEXFER_ATTR_UIDGID | PROTOCORE_SSH_FILEXFER_ATTR_PERMS |
            PROTOCORE_SSH_FILEXFER_ATTR_ACMODTIME,
    };
    for (size_t i = 0; i < sizeof(FLAGS) / sizeof(FLAGS[0]); i++)
    {
        SftpAttrs a;
        memset(&a, 0, sizeof(a));
        a.flags = FLAGS[i];
        a.size = 0xDEADBEEFCAFEBABEULL;
        a.permissions = 0100755u;
        a.atime = 0x11111111u;
        a.mtime = 0x22222222u;

        uint8_t buf[64];
        SftpWriter w;
        protocore_sftp_wr_init(&w, buf, sizeof(buf));
        protocore_sftp_wr_attrs(&w, &a);
        size_t n = protocore_sftp_wr_finish(&w);
        TEST_ASSERT_TRUE(n > 4);

        SftpReader r;
        protocore_sftp_rd_init(&r, buf + 4, n - 4);
        SftpAttrs got;
        TEST_ASSERT_TRUE(protocore_sftp_rd_attrs(&r, &got));
        TEST_ASSERT_EQUAL_HEX32(FLAGS[i], got.flags);
        TEST_ASSERT_EQUAL_UINT64((FLAGS[i] & PROTOCORE_SSH_FILEXFER_ATTR_SIZE) ? a.size : 0, got.size);
        TEST_ASSERT_EQUAL_HEX32((FLAGS[i] & PROTOCORE_SSH_FILEXFER_ATTR_PERMS) ? a.permissions : 0, got.permissions);
        TEST_ASSERT_EQUAL_HEX32((FLAGS[i] & PROTOCORE_SSH_FILEXFER_ATTR_ACMODTIME) ? a.atime : 0, got.atime);
        TEST_ASSERT_EQUAL_HEX32((FLAGS[i] & PROTOCORE_SSH_FILEXFER_ATTR_ACMODTIME) ? a.mtime : 0, got.mtime);
        // The whole blob was consumed: no field was skipped and none was read twice.
        TEST_ASSERT_EQUAL_size_t(n - 4, r.off);
    }
}

// sec 5: "Implementations SHOULD ignore extended data fields that they do not understand." An
// ATTRS blob carrying SSH_FILEXFER_ATTR_EXTENDED must be walked past, leaving the reader positioned
// at whatever follows the blob rather than inside it.
void test_attrs_skips_extended_fields(void)
{
    static const uint8_t BLOB[] = {
        0x80, 0x00, 0x00, 0x01,             // flags = EXTENDED | SIZE
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2A, // size = 42
        0x00, 0x00, 0x00, 0x01,             // extended_count = 1
        0x00, 0x00, 0x00, 0x03, 'a', '@', 'b',          // extended_type
        0x00, 0x00, 0x00, 0x02, 0xAA, 0xBB,             // extended_data
        0xC0, 0xDE                                      // a trailing field the caller reads next
    };
    SftpReader r;
    protocore_sftp_rd_init(&r, BLOB, sizeof(BLOB));
    SftpAttrs a;
    TEST_ASSERT_TRUE(protocore_sftp_rd_attrs(&r, &a));
    TEST_ASSERT_EQUAL_HEX32(0x80000001u, a.flags);
    TEST_ASSERT_EQUAL_UINT64(42, a.size);
    TEST_ASSERT_EQUAL_HEX8(0xC0, protocore_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_HEX8(0xDE, protocore_sftp_rd_u8(&r));
    TEST_ASSERT_TRUE(r.ok);
}

// sec 7: SSH_FXP_NAME is "uint32 id / uint32 count / repeats count times: string filename,
// string longname, ATTRS attrs". The single-entry builder must emit count = 1 and then exactly
// those three fields.
void test_name_response_layout(void)
{
    SftpAttrs a;
    memset(&a, 0, sizeof(a));
    a.flags = PROTOCORE_SSH_FILEXFER_ATTR_PERMS;
    a.permissions = 0040755u; // S_IFDIR | rwxr-xr-x

    uint8_t out[128];
    size_t n = protocore_sftp_build_name1(9, "/", "drwxr-xr-x 1 0 0 0 Jan 1 1970 /", &a, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);

    SftpReader r;
    protocore_sftp_rd_init(&r, out, n);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(n - 4), protocore_sftp_rd_u32(&r));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_SSH_FXP_NAME, protocore_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_UINT32(9, protocore_sftp_rd_u32(&r));
    TEST_ASSERT_EQUAL_UINT32(1, protocore_sftp_rd_u32(&r));

    const uint8_t *s = NULL;
    uint32_t sl = 0;
    TEST_ASSERT_TRUE(protocore_sftp_rd_string(&r, &s, &sl));
    TEST_ASSERT_EQUAL_UINT32(1, sl);
    TEST_ASSERT_EQUAL_HEX8('/', s[0]);
    TEST_ASSERT_TRUE(protocore_sftp_rd_string(&r, &s, &sl));
    TEST_ASSERT_EQUAL_UINT32(strlen("drwxr-xr-x 1 0 0 0 Jan 1 1970 /"), sl);

    SftpAttrs got;
    TEST_ASSERT_TRUE(protocore_sftp_rd_attrs(&r, &got));
    TEST_ASSERT_EQUAL_HEX32(PROTOCORE_SSH_FILEXFER_ATTR_PERMS, got.flags);
    TEST_ASSERT_EQUAL_HEX32(0040755u, got.permissions);
    TEST_ASSERT_EQUAL_size_t(n, r.off); // the packet ends exactly where its length said
}

// The framer sees the stream one read at a time. Fewer than four octets is "need more"; a complete
// prefix gives 4 + the declared payload; a declared length past what the caller can hold, and a
// zero length (a packet with no type octet, which sec 3 does not allow), are both unparseable and
// come back as SIZE_MAX so the caller drops the connection rather than waiting forever.
void test_frame_length(void)
{
    static const uint8_t P[] = {0x00, 0x00, 0x00, 0x05, 0x02, 0x00, 0x00, 0x00, 0x03};

    TEST_ASSERT_EQUAL_size_t(0, protocore_sftp_frame_len(P, 0, 4096));
    TEST_ASSERT_EQUAL_size_t(0, protocore_sftp_frame_len(P, 3, 4096));
    TEST_ASSERT_EQUAL_size_t(9, protocore_sftp_frame_len(P, 4, 4096)); // the prefix alone is enough
    TEST_ASSERT_EQUAL_size_t(9, protocore_sftp_frame_len(P, sizeof(P), 4096));
    TEST_ASSERT_EQUAL_size_t(9, protocore_sftp_frame_len(P, sizeof(P), 9)); // exactly the limit

    TEST_ASSERT_EQUAL_size_t((size_t)-1, protocore_sftp_frame_len(P, sizeof(P), 8)); // one short

    static const uint8_t ZERO[4] = {0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t((size_t)-1, protocore_sftp_frame_len(ZERO, sizeof(ZERO), 4096));

    static const uint8_t HUGE[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_size_t((size_t)-1, protocore_sftp_frame_len(HUGE, sizeof(HUGE), 34000));
}

// The reader is the only thing standing between a hostile packet and the parser above it. Once a
// read runs off the end it must stay failed, and every later read must return zero rather than
// whatever lies past the buffer.
void test_reader_stays_failed_after_a_short_read(void)
{
    static const uint8_t BUF[3] = {0x01, 0x02, 0x03};
    SftpReader r;
    protocore_sftp_rd_init(&r, BUF, sizeof(BUF));
    TEST_ASSERT_EQUAL_HEX8(0x01, protocore_sftp_rd_u8(&r));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_UINT32(0, protocore_sftp_rd_u32(&r)); // only 2 octets left
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_HEX8(0, protocore_sftp_rd_u8(&r)); // the remaining octet is not handed out
    TEST_ASSERT_EQUAL_UINT64(0, protocore_sftp_rd_u64(&r));
    TEST_ASSERT_FALSE(r.ok);

    // A u64 that needs eight octets from a seven-octet buffer fails without consuming any.
    static const uint8_t SEVEN[7] = {1, 2, 3, 4, 5, 6, 7};
    protocore_sftp_rd_init(&r, SEVEN, sizeof(SEVEN));
    TEST_ASSERT_EQUAL_UINT64(0, protocore_sftp_rd_u64(&r));
    TEST_ASSERT_FALSE(r.ok);
}

// A string whose declared count runs past the payload is the classic SFTP overread. It must be
// refused on the count, before any pointer into the payload is handed back.
void test_reader_refuses_a_string_longer_than_the_payload(void)
{
    static const uint8_t BAD[6] = {0x00, 0x00, 0x00, 0x10, 0xAA, 0xBB}; // declares 16, carries 2
    SftpReader r;
    protocore_sftp_rd_init(&r, BAD, sizeof(BAD));
    const uint8_t *s = (const uint8_t *)BAD;
    uint32_t sl = 0xFFFFFFFFu;
    TEST_ASSERT_FALSE(protocore_sftp_rd_string(&r, &s, &sl));
    TEST_ASSERT_FALSE(r.ok);

    // A zero-length string is legal and consumes only its count.
    static const uint8_t EMPTY[6] = {0x00, 0x00, 0x00, 0x00, 0xC0, 0xDE};
    protocore_sftp_rd_init(&r, EMPTY, sizeof(EMPTY));
    TEST_ASSERT_TRUE(protocore_sftp_rd_string(&r, &s, &sl));
    TEST_ASSERT_EQUAL_UINT32(0, sl);
    TEST_ASSERT_EQUAL_HEX8(0xC0, protocore_sftp_rd_u8(&r));

    // Reading an ATTRS blob out of a truncated payload fails rather than inventing fields.
    static const uint8_t SHORT_ATTRS[6] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00}; // SIZE set, 2 of 8 octets
    protocore_sftp_rd_init(&r, SHORT_ATTRS, sizeof(SHORT_ATTRS));
    SftpAttrs a;
    TEST_ASSERT_FALSE(protocore_sftp_rd_attrs(&r, &a));
}

// A packet that does not fit reports zero rather than a truncated one, and the failure sticks: a
// writer that overflowed midway must not produce a length prefix for the part that did fit.
void test_writer_overflow_is_final(void)
{
    uint8_t small[12];
    SftpWriter w;
    protocore_sftp_wr_init(&w, small, sizeof(small));
    protocore_sftp_wr_u8(&w, PROTOCORE_SSH_FXP_DATA); // 4 reserved + 1 = 5
    protocore_sftp_wr_u32(&w, 1);                     // 9
    TEST_ASSERT_FALSE(w.ovf);
    protocore_sftp_wr_u32(&w, 2); // would reach 13 > 12
    TEST_ASSERT_TRUE(w.ovf);
    protocore_sftp_wr_u8(&w, 0); // still refused once failed
    TEST_ASSERT_EQUAL_size_t(0, protocore_sftp_wr_finish(&w));

    // A buffer too small even for the length prefix fails at init.
    uint8_t tiny[3];
    protocore_sftp_wr_init(&w, tiny, sizeof(tiny));
    TEST_ASSERT_TRUE(w.ovf);
    TEST_ASSERT_EQUAL_size_t(0, protocore_sftp_wr_finish(&w));

    // Every builder inherits that: a nine-octet VERSION packet needs nine octets.
    uint8_t out[16];
    TEST_ASSERT_EQUAL_size_t(0, protocore_sftp_build_version(out, 8));
    TEST_ASSERT_EQUAL_size_t(9, protocore_sftp_build_version(out, 9));
    TEST_ASSERT_EQUAL_size_t(0, protocore_sftp_build_status(1, 0, "msg", out, 10));
    TEST_ASSERT_EQUAL_size_t(0, protocore_sftp_build_data(1, "abcdefgh", 8, out, 16));
}

// A READDIR reply cannot know its entry count until it has written the entries, so the count is
// reserved and backfilled. The patch must land on the four octets wr_pos named and disturb nothing
// around them.
void test_patch_u32_backfills_a_reserved_count(void)
{
    uint8_t buf[32];
    SftpWriter w;
    protocore_sftp_wr_init(&w, buf, sizeof(buf));
    protocore_sftp_wr_u8(&w, PROTOCORE_SSH_FXP_NAME);
    protocore_sftp_wr_u32(&w, 42); // id
    size_t at = protocore_sftp_wr_pos(&w);
    protocore_sftp_wr_u32(&w, 0); // count placeholder
    protocore_sftp_wr_string(&w, "ab", 2);
    protocore_sftp_wr_patch_u32(&w, at, 1);
    size_t n = protocore_sftp_wr_finish(&w);

    SftpReader r;
    protocore_sftp_rd_init(&r, buf, n);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(n - 4), protocore_sftp_rd_u32(&r));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_SSH_FXP_NAME, protocore_sftp_rd_u8(&r));
    TEST_ASSERT_EQUAL_UINT32(42, protocore_sftp_rd_u32(&r));
    TEST_ASSERT_EQUAL_UINT32(1, protocore_sftp_rd_u32(&r));
    const uint8_t *s = NULL;
    uint32_t sl = 0;
    TEST_ASSERT_TRUE(protocore_sftp_rd_string(&r, &s, &sl));
    TEST_ASSERT_EQUAL_UINT32(2, sl);

    // A patch aimed past the buffer writes nothing.
    uint8_t guard[8];
    memset(guard, 0x5A, sizeof(guard));
    SftpWriter g;
    protocore_sftp_wr_init(&g, guard, sizeof(guard));
    protocore_sftp_wr_patch_u32(&g, 5, 0xFFFFFFFFu); // 5 + 4 > 8
    TEST_ASSERT_EQUAL_HEX8(0x5A, guard[5]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, guard[7]);
}

// sec 7's sample longname begins "-rwxr-xr-x", the ls-style rendering of a regular file with POSIX
// mode 0755. Each character below is derived from the POSIX.1 permission bit it reports:
//   S_IRUSR 0400 S_IWUSR 0200 S_IXUSR 0100 S_IRGRP 0040 S_IWGRP 0020 S_IXGRP 0010
//   S_IROTH 0004 S_IWOTH 0002 S_IXOTH 0001
// so 0755 = r,w,x / r,-,x / r,-,x and 0644 = r,w,- / r,-,- / r,-,-. The leading character is 'd'
// for a directory and '-' otherwise.
void test_longname_permission_column(void)
{
    struct
    {
        proto_bool dir;
        uint32_t perms;
        const char *want;
    } static const CASES[] = {
        {PROTO_FALSE, 0755, "-rwxr-xr-x"}, // the draft's own sample line
        {PROTO_FALSE, 0644, "-rw-r--r--"},
        {PROTO_FALSE, 0000, "----------"},
        {PROTO_FALSE, 0777, "-rwxrwxrwx"},
        {PROTO_FALSE, 0400, "-r--------"},
        {PROTO_FALSE, 0001, "---------x"},
        {PROTO_TRUE, 0755, "drwxr-xr-x"},
        {PROTO_TRUE, 0000, "d---------"},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        char out[128];
        size_t n = protocore_sftp_format_longname(CASES[i].dir, CASES[i].perms, 0, 0, "f", out, sizeof(out));
        TEST_ASSERT_TRUE(n > 10);
        TEST_ASSERT_EQUAL_STRING_LEN(CASES[i].want, out, 10);
        TEST_ASSERT_EQUAL_CHAR(' ', out[10]); // sec 7: "Fields are separated by spaces"
    }
}

// The mode column reads only the nine permission bits, so the file-type bits a POSIX mode carries
// above them (S_IFREG 0100000, S_IFDIR 0040000) must not leak into it.
void test_longname_ignores_the_file_type_bits(void)
{
    char reg[128];
    char dir[128];
    protocore_sftp_format_longname(PROTO_FALSE, PROTOCORE_SFTP_S_IFREG | 0644u, 0, 0, "f", reg, sizeof(reg));
    protocore_sftp_format_longname(PROTO_TRUE, PROTOCORE_SFTP_S_IFDIR | 0755u, 0, 0, "d", dir, sizeof(dir));
    TEST_ASSERT_EQUAL_STRING_LEN("-rw-r--r--", reg, 10);
    TEST_ASSERT_EQUAL_STRING_LEN("drwxr-xr-x", dir, 10);
    TEST_ASSERT_EQUAL_UINT32(0040000u, PROTOCORE_SFTP_S_IFDIR);
    TEST_ASSERT_EQUAL_UINT32(0100000u, PROTOCORE_SFTP_S_IFREG);
}

// sec 7 fixes the field order of the recommended longname: permissions, link count, owner, group,
// size, modification time, then the file name last. The size and the name are the two fields this
// module takes from its caller, so both must appear, in that order, with the name at the end.
void test_longname_carries_the_size_and_ends_with_the_name(void)
{
    char out[128];
    size_t n = protocore_sftp_format_longname(PROTO_FALSE, 0644, 348911, 0, "t-filexfer", out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(strlen(out), n);
    TEST_ASSERT_NOT_NULL(strstr(out, " 348911 "));
    TEST_ASSERT_EQUAL_STRING("t-filexfer", out + n - strlen("t-filexfer"));
    TEST_ASSERT_EQUAL_CHAR(' ', out[n - strlen("t-filexfer") - 1]);
}

// longname is a display field, so a short buffer clips it rather than refusing: sec 7 says clients
// "SHOULD NOT attempt to parse the longname field for file attributes", and an empty listing column
// is worse than a short one. The result is always NUL-terminated inside the caller's buffer.
void test_longname_clips_to_the_buffer(void)
{
    char out[8];
    memset(out, 0x7F, sizeof(out));
    size_t n = protocore_sftp_format_longname(PROTO_FALSE, 0644, 1234, 0, "a-long-file-name", out, sizeof(out));
    TEST_ASSERT_TRUE(n < sizeof(out));
    TEST_ASSERT_EQUAL_size_t(strlen(out), n);
    TEST_ASSERT_EQUAL_STRING_LEN("-rw-r--", out, n < 7 ? n : 7);
}
