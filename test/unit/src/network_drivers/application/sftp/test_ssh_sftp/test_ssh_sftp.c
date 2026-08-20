// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "network_drivers/application/sftp/sftp/sftp.h"
#include <string.h>

#include <unity.h>

static uint8_t sftp_work[16]; // the borrow an entry takes; Sftp never reads it

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
    SftpV.build_version_args.out = out;
    SftpV.build_version_args.cap = sizeof(out);
    Sftp.build_version(sftp_work);
    size_t n = SftpV.n;
    TEST_ASSERT_EQUAL_size_t(9, n);

    static const uint8_t WANT[9] = {
        0x00, 0x00, 0x00, 0x05, // length = the 5 octets of data that follow
        0x02,                   // SSH_FXP_VERSION
        0x00, 0x00, 0x00, 0x03  // version 3
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    // Stated the other way: the length field is always the total minus the four it sits in.
    SftpReader r;
    SftpV.rd_init_args.r = &r;
    SftpV.rd_init_args.payload = out;
    SftpV.rd_init_args.len = n;
    Sftp.rd_init(sftp_work);
    SftpV.rd_u32_args.r = &r;
    Sftp.rd_u32(sftp_work);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(n - 4), SftpV.u32);
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
    SftpV.build_status_args.id = 7;
    SftpV.build_status_args.code = PROTOCORE_SSH_FX_NO_SUCH_FILE;
    SftpV.build_status_args.msg = "no";
    SftpV.build_status_args.out = out;
    SftpV.build_status_args.cap = sizeof(out);
    Sftp.build_status(sftp_work);
    size_t n = SftpV.n;
    static const uint8_t WANT[23] = {0x00, 0x00, 0x00, 0x13, 0x65, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
                                     0x02, 0x00, 0x00, 0x00, 0x02, 0x6E, 0x6F, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, n);

    // A null message is still a present, zero-length string: the language tag must follow it.
    SftpV.build_status_args.id = 1;
    SftpV.build_status_args.code = PROTOCORE_SSH_FX_OK;
    SftpV.build_status_args.msg = NULL;
    SftpV.build_status_args.out = out;
    SftpV.build_status_args.cap = sizeof(out);
    Sftp.build_status(sftp_work);
    n = SftpV.n;
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
    SftpV.build_handle_args.id = 0x11223344u;
    SftpV.build_handle_args.handle = HANDLE;
    SftpV.build_handle_args.hlen = sizeof(HANDLE);
    SftpV.build_handle_args.out = out;
    SftpV.build_handle_args.cap = sizeof(out);
    Sftp.build_handle(sftp_work);
    size_t n = SftpV.n;
    static const uint8_t WANT_H[17] = {0x00, 0x00, 0x00, 0x0D, 0x66, 0x11, 0x22, 0x33, 0x44,
                                       0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT_H), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_H, out, n);

    SftpV.build_data_args.id = 2;
    SftpV.build_data_args.data = "abc";
    SftpV.build_data_args.dlen = 3;
    SftpV.build_data_args.out = out;
    SftpV.build_data_args.cap = sizeof(out);
    Sftp.build_data(sftp_work);
    n = SftpV.n;
    static const uint8_t WANT_D[16] = {0x00, 0x00, 0x00, 0x0C, 0x67, 0x00, 0x00, 0x00,
                                       0x02, 0x00, 0x00, 0x00, 0x03, 0x61, 0x62, 0x63};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT_D), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_D, out, n);

    // A zero-length DATA string is the legal way to say "nothing read", not an omitted field.
    SftpV.build_data_args.id = 2;
    SftpV.build_data_args.data = "";
    SftpV.build_data_args.dlen = 0;
    SftpV.build_data_args.out = out;
    SftpV.build_data_args.cap = sizeof(out);
    Sftp.build_data(sftp_work);
    n = SftpV.n;
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
    SftpV.build_attrs_args.id = 3;
    SftpV.build_attrs_args.a = &a;
    SftpV.build_attrs_args.out = out;
    SftpV.build_attrs_args.cap = sizeof(out);
    Sftp.build_attrs(sftp_work);
    size_t n = SftpV.n;
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
    SftpV.build_attrs_args.id = 0;
    SftpV.build_attrs_args.a = &a;
    SftpV.build_attrs_args.out = out;
    SftpV.build_attrs_args.cap = sizeof(out);
    Sftp.build_attrs(sftp_work);
    n = SftpV.n;
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
        SftpV.wr_init_args.w = &w;
        SftpV.wr_init_args.out = buf;
        SftpV.wr_init_args.cap = sizeof(buf);
        Sftp.wr_init(sftp_work);
        SftpV.wr_attrs_args.w = &w;
        SftpV.wr_attrs_args.a = &a;
        Sftp.wr_attrs(sftp_work);
        SftpV.wr_finish_args.w = &w;
        Sftp.wr_finish(sftp_work);
        size_t n = SftpV.n;
        TEST_ASSERT_TRUE(n > 4);

        SftpReader r;
        SftpV.rd_init_args.r = &r;
        SftpV.rd_init_args.payload = buf + 4;
        SftpV.rd_init_args.len = n - 4;
        Sftp.rd_init(sftp_work);
        SftpAttrs got;
        SftpV.rd_attrs_args.r = &r;
        SftpV.rd_attrs_args.a = &got;
        Sftp.rd_attrs(sftp_work);
        TEST_ASSERT_TRUE(SftpV.ok);
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
        0x80, 0x00, 0x00, 0x01,                         // flags = EXTENDED | SIZE
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2A, // size = 42
        0x00, 0x00, 0x00, 0x01,                         // extended_count = 1
        0x00, 0x00, 0x00, 0x03, 'a',  '@',  'b',        // extended_type
        0x00, 0x00, 0x00, 0x02, 0xAA, 0xBB,             // extended_data
        0xC0, 0xDE                                      // a trailing field the caller reads next
    };
    SftpReader r;
    SftpV.rd_init_args.r = &r;
    SftpV.rd_init_args.payload = BLOB;
    SftpV.rd_init_args.len = sizeof(BLOB);
    Sftp.rd_init(sftp_work);
    SftpAttrs a;
    SftpV.rd_attrs_args.r = &r;
    SftpV.rd_attrs_args.a = &a;
    Sftp.rd_attrs(sftp_work);
    TEST_ASSERT_TRUE(SftpV.ok);
    TEST_ASSERT_EQUAL_HEX32(0x80000001u, a.flags);
    TEST_ASSERT_EQUAL_UINT64(42, a.size);
    SftpV.rd_u8_args.r = &r;
    Sftp.rd_u8(sftp_work);
    TEST_ASSERT_EQUAL_HEX8(0xC0, SftpV.value);
    SftpV.rd_u8_args.r = &r;
    Sftp.rd_u8(sftp_work);
    TEST_ASSERT_EQUAL_HEX8(0xDE, SftpV.value);
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
    SftpV.build_name1_args.id = 9;
    SftpV.build_name1_args.name = "/";
    SftpV.build_name1_args.longname = "drwxr-xr-x 1 0 0 0 Jan 1 1970 /";
    SftpV.build_name1_args.a = &a;
    SftpV.build_name1_args.out = out;
    SftpV.build_name1_args.cap = sizeof(out);
    Sftp.build_name1(sftp_work);
    size_t n = SftpV.n;
    TEST_ASSERT_TRUE(n > 0);

    SftpReader r;
    SftpV.rd_init_args.r = &r;
    SftpV.rd_init_args.payload = out;
    SftpV.rd_init_args.len = n;
    Sftp.rd_init(sftp_work);
    SftpV.rd_u32_args.r = &r;
    Sftp.rd_u32(sftp_work);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(n - 4), SftpV.u32);
    SftpV.rd_u8_args.r = &r;
    Sftp.rd_u8(sftp_work);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_SSH_FXP_NAME, SftpV.value);
    SftpV.rd_u32_args.r = &r;
    Sftp.rd_u32(sftp_work);
    TEST_ASSERT_EQUAL_UINT32(9, SftpV.u32);
    SftpV.rd_u32_args.r = &r;
    Sftp.rd_u32(sftp_work);
    TEST_ASSERT_EQUAL_UINT32(1, SftpV.u32);

    const uint8_t *s = NULL;
    uint32_t sl = 0;
    SftpV.rd_string_args.r = &r;
    SftpV.rd_string_args.out = &s;
    SftpV.rd_string_args.out_len = &sl;
    Sftp.rd_string(sftp_work);
    TEST_ASSERT_TRUE(SftpV.ok);
    TEST_ASSERT_EQUAL_UINT32(1, sl);
    TEST_ASSERT_EQUAL_HEX8('/', s[0]);
    SftpV.rd_string_args.r = &r;
    SftpV.rd_string_args.out = &s;
    SftpV.rd_string_args.out_len = &sl;
    Sftp.rd_string(sftp_work);
    TEST_ASSERT_TRUE(SftpV.ok);
    TEST_ASSERT_EQUAL_UINT32(strlen("drwxr-xr-x 1 0 0 0 Jan 1 1970 /"), sl);

    SftpAttrs got;
    SftpV.rd_attrs_args.r = &r;
    SftpV.rd_attrs_args.a = &got;
    Sftp.rd_attrs(sftp_work);
    TEST_ASSERT_TRUE(SftpV.ok);
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

    SftpV.frame_len_args.buf = P;
    SftpV.frame_len_args.have = 0;
    SftpV.frame_len_args.max = 4096;
    Sftp.frame_len(sftp_work);
    TEST_ASSERT_EQUAL_size_t(0, SftpV.n);
    SftpV.frame_len_args.buf = P;
    SftpV.frame_len_args.have = 3;
    SftpV.frame_len_args.max = 4096;
    Sftp.frame_len(sftp_work);
    TEST_ASSERT_EQUAL_size_t(0, SftpV.n);
    SftpV.frame_len_args.buf = P;
    SftpV.frame_len_args.have = 4;
    SftpV.frame_len_args.max = 4096;
    Sftp.frame_len(sftp_work);
    TEST_ASSERT_EQUAL_size_t(9, SftpV.n); // the prefix alone is enough
    SftpV.frame_len_args.buf = P;
    SftpV.frame_len_args.have = sizeof(P);
    SftpV.frame_len_args.max = 4096;
    Sftp.frame_len(sftp_work);
    TEST_ASSERT_EQUAL_size_t(9, SftpV.n);
    SftpV.frame_len_args.buf = P;
    SftpV.frame_len_args.have = sizeof(P);
    SftpV.frame_len_args.max = 9;
    Sftp.frame_len(sftp_work);
    TEST_ASSERT_EQUAL_size_t(9, SftpV.n); // exactly the limit

    SftpV.frame_len_args.buf = P;
    SftpV.frame_len_args.have = sizeof(P);
    SftpV.frame_len_args.max = 8;
    Sftp.frame_len(sftp_work);
    TEST_ASSERT_EQUAL_size_t((size_t)-1, SftpV.n); // one short

    static const uint8_t ZERO[4] = {0x00, 0x00, 0x00, 0x00};
    SftpV.frame_len_args.buf = ZERO;
    SftpV.frame_len_args.have = sizeof(ZERO);
    SftpV.frame_len_args.max = 4096;
    Sftp.frame_len(sftp_work);
    TEST_ASSERT_EQUAL_size_t((size_t)-1, SftpV.n);

    static const uint8_t OVERSIZE[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    SftpV.frame_len_args.buf = OVERSIZE;
    SftpV.frame_len_args.have = sizeof(OVERSIZE);
    SftpV.frame_len_args.max = 34000;
    Sftp.frame_len(sftp_work);
    TEST_ASSERT_EQUAL_size_t((size_t)-1, SftpV.n);
}

// The reader is the only thing standing between a hostile packet and the parser above it. Once a
// read runs off the end it must stay failed, and every later read must return zero rather than
// whatever lies past the buffer.
void test_reader_stays_failed_after_a_short_read(void)
{
    static const uint8_t BUF[3] = {0x01, 0x02, 0x03};
    SftpReader r;
    SftpV.rd_init_args.r = &r;
    SftpV.rd_init_args.payload = BUF;
    SftpV.rd_init_args.len = sizeof(BUF);
    Sftp.rd_init(sftp_work);
    SftpV.rd_u8_args.r = &r;
    Sftp.rd_u8(sftp_work);
    TEST_ASSERT_EQUAL_HEX8(0x01, SftpV.value);
    TEST_ASSERT_TRUE(r.ok);
    SftpV.rd_u32_args.r = &r;
    Sftp.rd_u32(sftp_work);
    TEST_ASSERT_EQUAL_UINT32(0, SftpV.u32); // only 2 octets left
    TEST_ASSERT_FALSE(r.ok);
    SftpV.rd_u8_args.r = &r;
    Sftp.rd_u8(sftp_work);
    TEST_ASSERT_EQUAL_HEX8(0, SftpV.value); // the remaining octet is not handed out
    SftpV.rd_u64_args.r = &r;
    Sftp.rd_u64(sftp_work);
    TEST_ASSERT_EQUAL_UINT64(0, SftpV.u64);
    TEST_ASSERT_FALSE(r.ok);

    // A u64 that needs eight octets from a seven-octet buffer fails without consuming any.
    static const uint8_t SEVEN[7] = {1, 2, 3, 4, 5, 6, 7};
    SftpV.rd_init_args.r = &r;
    SftpV.rd_init_args.payload = SEVEN;
    SftpV.rd_init_args.len = sizeof(SEVEN);
    Sftp.rd_init(sftp_work);
    SftpV.rd_u64_args.r = &r;
    Sftp.rd_u64(sftp_work);
    TEST_ASSERT_EQUAL_UINT64(0, SftpV.u64);
    TEST_ASSERT_FALSE(r.ok);
}

// A string whose declared count runs past the payload is the classic SFTP overread. It must be
// refused on the count, before any pointer into the payload is handed back.
void test_reader_refuses_a_string_longer_than_the_payload(void)
{
    static const uint8_t BAD[6] = {0x00, 0x00, 0x00, 0x10, 0xAA, 0xBB}; // declares 16, carries 2
    SftpReader r;
    SftpV.rd_init_args.r = &r;
    SftpV.rd_init_args.payload = BAD;
    SftpV.rd_init_args.len = sizeof(BAD);
    Sftp.rd_init(sftp_work);
    const uint8_t *s = (const uint8_t *)BAD;
    uint32_t sl = 0xFFFFFFFFu;
    SftpV.rd_string_args.r = &r;
    SftpV.rd_string_args.out = &s;
    SftpV.rd_string_args.out_len = &sl;
    Sftp.rd_string(sftp_work);
    TEST_ASSERT_FALSE(SftpV.ok);
    TEST_ASSERT_FALSE(r.ok);

    // A zero-length string is legal and consumes only its count.
    static const uint8_t EMPTY[6] = {0x00, 0x00, 0x00, 0x00, 0xC0, 0xDE};
    SftpV.rd_init_args.r = &r;
    SftpV.rd_init_args.payload = EMPTY;
    SftpV.rd_init_args.len = sizeof(EMPTY);
    Sftp.rd_init(sftp_work);
    SftpV.rd_string_args.r = &r;
    SftpV.rd_string_args.out = &s;
    SftpV.rd_string_args.out_len = &sl;
    Sftp.rd_string(sftp_work);
    TEST_ASSERT_TRUE(SftpV.ok);
    TEST_ASSERT_EQUAL_UINT32(0, sl);
    SftpV.rd_u8_args.r = &r;
    Sftp.rd_u8(sftp_work);
    TEST_ASSERT_EQUAL_HEX8(0xC0, SftpV.value);

    // Reading an ATTRS blob out of a truncated payload fails rather than inventing fields.
    static const uint8_t SHORT_ATTRS[6] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00}; // SIZE set, 2 of 8 octets
    SftpV.rd_init_args.r = &r;
    SftpV.rd_init_args.payload = SHORT_ATTRS;
    SftpV.rd_init_args.len = sizeof(SHORT_ATTRS);
    Sftp.rd_init(sftp_work);
    SftpAttrs a;
    SftpV.rd_attrs_args.r = &r;
    SftpV.rd_attrs_args.a = &a;
    Sftp.rd_attrs(sftp_work);
    TEST_ASSERT_FALSE(SftpV.ok);
}

// A packet that does not fit reports zero rather than a truncated one, and the failure sticks: a
// writer that overflowed midway must not produce a length prefix for the part that did fit.
void test_writer_overflow_is_final(void)
{
    uint8_t small[12];
    SftpWriter w;
    SftpV.wr_init_args.w = &w;
    SftpV.wr_init_args.out = small;
    SftpV.wr_init_args.cap = sizeof(small);
    Sftp.wr_init(sftp_work);
    SftpV.wr_u8_args.w = &w;
    SftpV.wr_u8_args.v = PROTOCORE_SSH_FXP_DATA;
    Sftp.wr_u8(sftp_work); // 4 reserved + 1 = 5
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = 1;
    Sftp.wr_u32(sftp_work); // 9
    TEST_ASSERT_FALSE(w.ovf);
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = 2;
    Sftp.wr_u32(sftp_work); // would reach 13 > 12
    TEST_ASSERT_TRUE(w.ovf);
    SftpV.wr_u8_args.w = &w;
    SftpV.wr_u8_args.v = 0;
    Sftp.wr_u8(sftp_work); // still refused once failed
    SftpV.wr_finish_args.w = &w;
    Sftp.wr_finish(sftp_work);
    TEST_ASSERT_EQUAL_size_t(0, SftpV.n);

    // A buffer too small even for the length prefix fails at init.
    uint8_t tiny[3];
    SftpV.wr_init_args.w = &w;
    SftpV.wr_init_args.out = tiny;
    SftpV.wr_init_args.cap = sizeof(tiny);
    Sftp.wr_init(sftp_work);
    TEST_ASSERT_TRUE(w.ovf);
    SftpV.wr_finish_args.w = &w;
    Sftp.wr_finish(sftp_work);
    TEST_ASSERT_EQUAL_size_t(0, SftpV.n);

    // Every builder inherits that: a nine-octet VERSION packet needs nine octets.
    uint8_t out[16];
    SftpV.build_version_args.out = out;
    SftpV.build_version_args.cap = 8;
    Sftp.build_version(sftp_work);
    TEST_ASSERT_EQUAL_size_t(0, SftpV.n);
    SftpV.build_version_args.out = out;
    SftpV.build_version_args.cap = 9;
    Sftp.build_version(sftp_work);
    TEST_ASSERT_EQUAL_size_t(9, SftpV.n);
    SftpV.build_status_args.id = 1;
    SftpV.build_status_args.code = 0;
    SftpV.build_status_args.msg = "msg";
    SftpV.build_status_args.out = out;
    SftpV.build_status_args.cap = 10;
    Sftp.build_status(sftp_work);
    TEST_ASSERT_EQUAL_size_t(0, SftpV.n);
    SftpV.build_data_args.id = 1;
    SftpV.build_data_args.data = "abcdefgh";
    SftpV.build_data_args.dlen = 8;
    SftpV.build_data_args.out = out;
    SftpV.build_data_args.cap = 16;
    Sftp.build_data(sftp_work);
    TEST_ASSERT_EQUAL_size_t(0, SftpV.n);
}

// A READDIR reply cannot know its entry count until it has written the entries, so the count is
// reserved and backfilled. The patch must land on the four octets wr_pos named and disturb nothing
// around them.
void test_patch_u32_backfills_a_reserved_count(void)
{
    uint8_t buf[32];
    SftpWriter w;
    SftpV.wr_init_args.w = &w;
    SftpV.wr_init_args.out = buf;
    SftpV.wr_init_args.cap = sizeof(buf);
    Sftp.wr_init(sftp_work);
    SftpV.wr_u8_args.w = &w;
    SftpV.wr_u8_args.v = PROTOCORE_SSH_FXP_NAME;
    Sftp.wr_u8(sftp_work);
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = 42;
    Sftp.wr_u32(sftp_work); // id
    SftpV.wr_pos_args.w = &w;
    Sftp.wr_pos(sftp_work);
    size_t at = SftpV.n;
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = 0;
    Sftp.wr_u32(sftp_work); // count placeholder
    SftpV.wr_string_args.w = &w;
    SftpV.wr_string_args.s = "ab";
    SftpV.wr_string_args.n = 2;
    Sftp.wr_string(sftp_work);
    SftpV.wr_patch_u32_args.w = &w;
    SftpV.wr_patch_u32_args.at = at;
    SftpV.wr_patch_u32_args.v = 1;
    Sftp.wr_patch_u32(sftp_work);
    SftpV.wr_finish_args.w = &w;
    Sftp.wr_finish(sftp_work);
    size_t n = SftpV.n;

    SftpReader r;
    SftpV.rd_init_args.r = &r;
    SftpV.rd_init_args.payload = buf;
    SftpV.rd_init_args.len = n;
    Sftp.rd_init(sftp_work);
    SftpV.rd_u32_args.r = &r;
    Sftp.rd_u32(sftp_work);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(n - 4), SftpV.u32);
    SftpV.rd_u8_args.r = &r;
    Sftp.rd_u8(sftp_work);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_SSH_FXP_NAME, SftpV.value);
    SftpV.rd_u32_args.r = &r;
    Sftp.rd_u32(sftp_work);
    TEST_ASSERT_EQUAL_UINT32(42, SftpV.u32);
    SftpV.rd_u32_args.r = &r;
    Sftp.rd_u32(sftp_work);
    TEST_ASSERT_EQUAL_UINT32(1, SftpV.u32);
    const uint8_t *s = NULL;
    uint32_t sl = 0;
    SftpV.rd_string_args.r = &r;
    SftpV.rd_string_args.out = &s;
    SftpV.rd_string_args.out_len = &sl;
    Sftp.rd_string(sftp_work);
    TEST_ASSERT_TRUE(SftpV.ok);
    TEST_ASSERT_EQUAL_UINT32(2, sl);

    // A patch aimed past the buffer writes nothing.
    uint8_t guard[8];
    memset(guard, 0x5A, sizeof(guard));
    SftpWriter g;
    SftpV.wr_init_args.w = &g;
    SftpV.wr_init_args.out = guard;
    SftpV.wr_init_args.cap = sizeof(guard);
    Sftp.wr_init(sftp_work);
    SftpV.wr_patch_u32_args.w = &g;
    SftpV.wr_patch_u32_args.at = 5;
    SftpV.wr_patch_u32_args.v = 0xFFFFFFFFu;
    Sftp.wr_patch_u32(sftp_work); // 5 + 4 > 8
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
        {PROTO_FALSE, 0644, "-rw-r--r--"}, {PROTO_FALSE, 0000, "----------"}, {PROTO_FALSE, 0777, "-rwxrwxrwx"},
        {PROTO_FALSE, 0400, "-r--------"}, {PROTO_FALSE, 0001, "---------x"}, {PROTO_TRUE, 0755, "drwxr-xr-x"},
        {PROTO_TRUE, 0000, "d---------"},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        char out[128];
        SftpV.format_longname_args.is_dir = CASES[i].dir;
        SftpV.format_longname_args.perms = CASES[i].perms;
        SftpV.format_longname_args.size = 0;
        SftpV.format_longname_args.mtime = 0;
        SftpV.format_longname_args.name = "f";
        SftpV.format_longname_args.out = out;
        SftpV.format_longname_args.cap = sizeof(out);
        Sftp.format_longname(sftp_work);
        size_t n = SftpV.n;
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
    SftpV.format_longname_args.is_dir = PROTO_FALSE;
    SftpV.format_longname_args.perms = PROTOCORE_SFTP_S_IFREG | 0644u;
    SftpV.format_longname_args.size = 0;
    SftpV.format_longname_args.mtime = 0;
    SftpV.format_longname_args.name = "f";
    SftpV.format_longname_args.out = reg;
    SftpV.format_longname_args.cap = sizeof(reg);
    Sftp.format_longname(sftp_work);
    SftpV.format_longname_args.is_dir = PROTO_TRUE;
    SftpV.format_longname_args.perms = PROTOCORE_SFTP_S_IFDIR | 0755u;
    SftpV.format_longname_args.size = 0;
    SftpV.format_longname_args.mtime = 0;
    SftpV.format_longname_args.name = "d";
    SftpV.format_longname_args.out = dir;
    SftpV.format_longname_args.cap = sizeof(dir);
    Sftp.format_longname(sftp_work);
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
    SftpV.format_longname_args.is_dir = PROTO_FALSE;
    SftpV.format_longname_args.perms = 0644;
    SftpV.format_longname_args.size = 348911;
    SftpV.format_longname_args.mtime = 0;
    SftpV.format_longname_args.name = "t-filexfer";
    SftpV.format_longname_args.out = out;
    SftpV.format_longname_args.cap = sizeof(out);
    Sftp.format_longname(sftp_work);
    size_t n = SftpV.n;
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
    SftpV.format_longname_args.is_dir = PROTO_FALSE;
    SftpV.format_longname_args.perms = 0644;
    SftpV.format_longname_args.size = 1234;
    SftpV.format_longname_args.mtime = 0;
    SftpV.format_longname_args.name = "a-long-file-name";
    SftpV.format_longname_args.out = out;
    SftpV.format_longname_args.cap = sizeof(out);
    Sftp.format_longname(sftp_work);
    size_t n = SftpV.n;
    TEST_ASSERT_TRUE(n < sizeof(out));
    TEST_ASSERT_EQUAL_size_t(strlen(out), n);
    TEST_ASSERT_EQUAL_STRING_LEN("-rw-r--", out, n < 7 ? n : 7);
}
