// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SCP (rcp) wire codec (network_drivers/session/scp/scp.h).
//
// The rcp line protocol scp rides on has no published specification: it is defined by 4.2BSD rcp
// and by OpenSSH's scp, and no standards text states its grammar. So except where noted the
// expectations here are PROPERTIES - round-trip identity, refusals, and boundaries - rather than
// values copied from a spec, and this is said plainly rather than dressed up as conformance.
//
// The one part that does have a published definition is the mode field: it is a POSIX file
// permission word written in octal, and POSIX.1 (IEEE Std 1003.1, <sys/stat.h>) fixes the bit
// values. test_mode_is_the_posix_permission_word_in_octal derives 0644 and 0755 from those bits by
// hand and is the load-bearing case, because a mode that survives the round trip but decodes to the
// wrong number silently changes a file's permissions on the receiving side.

#include "network_drivers/session/scp/scp.h"
#include <string.h>

#include <unity.h>

static uint8_t scp_work[16]; // the borrow an entry takes; Scp never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static ScpMode parse_cmd(const char *cmd, char *path, size_t cap)
{
    Scp.parse_cmd_args.cmd = cmd;
    Scp.parse_cmd_args.cmd_len = strlen(cmd);
    Scp.parse_cmd_args.path_out = path;
    Scp.parse_cmd_args.path_cap = cap;
    Scp.parse_cmd(scp_work);
    return Scp.value;
}

static proto_bool parse_cline(const char *line, uint32_t *mode, uint64_t *size, char *name, size_t cap)
{
    Scp.parse_cline_args.line = line;
    Scp.parse_cline_args.len = strlen(line);
    Scp.parse_cline_args.mode_out = mode;
    Scp.parse_cline_args.size_out = size;
    Scp.parse_cline_args.name_out = name;
    Scp.parse_cline_args.name_cap = cap;
    Scp.parse_cline(scp_work);
    return Scp.ok;
}

// POSIX.1 <sys/stat.h> permission bits:
//   S_IRUSR 0400  S_IWUSR 0200  S_IXUSR 0100
//   S_IRGRP 0040  S_IWGRP 0020  S_IXGRP 0010
//   S_IROTH 0004  S_IWOTH 0002  S_IXOTH 0001
// so rw-r--r-- = 0400|0200|0040|0004 = 256+128+32+4 = 420 = 0644
// and rwxr-xr-x = 0400|0200|0100|0040|0010|0004|0001 = 256+128+64+32+8+4+1 = 493 = 0755.
// The parser must reach those two numbers from the four text digits, and the builder must write
// the four digits back from the numbers.
void test_mode_is_the_posix_permission_word_in_octal(void)
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char name[64];

    TEST_ASSERT_TRUE(parse_cline("C0644 0 f\n", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT32(420u, mode);

    TEST_ASSERT_TRUE(parse_cline("C0755 0 f\n", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT32(493u, mode);

    TEST_ASSERT_TRUE(parse_cline("C0000 0 f\n", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT32(0u, mode);

    TEST_ASSERT_TRUE(parse_cline("C0777 0 f\n", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT32(511u, mode); // 0777 = 7*64 + 7*8 + 7

    char out[64];
    Scp.build_cline_args.mode = 420u;
    Scp.build_cline_args.size = 0;
    Scp.build_cline_args.name = "f";
    Scp.build_cline_args.out = out;
    Scp.build_cline_args.cap = sizeof(out);
    Scp.build_cline(scp_work);
    TEST_ASSERT_EQUAL_size_t(strlen("C0644 0 f\n"), Scp.n);
    TEST_ASSERT_EQUAL_STRING("C0644 0 f\n", out);
    Scp.build_cline_args.mode = 493u;
    Scp.build_cline_args.size = 0;
    Scp.build_cline_args.name = "f";
    Scp.build_cline_args.out = out;
    Scp.build_cline_args.cap = sizeof(out);
    Scp.build_cline(scp_work);
    TEST_ASSERT_EQUAL_size_t(strlen("C0755 0 f\n"), Scp.n);
    TEST_ASSERT_EQUAL_STRING("C0755 0 f\n", out);
}

// Octal has no digit 8 or 9, so a mode containing one is not a mode. The parser must refuse rather
// than stop early and read the rest of the line as the size.
void test_mode_rejects_non_octal_digits(void)
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char name[64];
    TEST_ASSERT_FALSE(parse_cline("C0648 10 n\n", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_FALSE(parse_cline("C0649 10 n\n", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_FALSE(parse_cline("Cxyz 10 n\n", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_FALSE(parse_cline("C 10 n\n", &mode, &size, name, sizeof(name))); // no digits at all
}

// Build then parse must return the same three fields. The size field is decimal, so a value with
// an 8 or a 9 in it distinguishes a decimal reader from an octal one.
void test_control_line_round_trip(void)
{
    struct
    {
        uint32_t mode;
        uint64_t size;
        const char *name;
        const char *line;
    } static const CASES[] = {
        {0644, 1234, "part.nc", "C0644 1234 part.nc\n"},
        {0755, 0, "empty", "C0755 0 empty\n"},
        {0600, 89, "eightnine", "C0600 89 eightnine\n"},
        {0666, 4294967296ULL, "big", "C0666 4294967296 big\n"}, // past 32 bits
        {0644, 18446744073709551615ULL, "max", "C0644 18446744073709551615 max\n"},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        char out[64];
        Scp.build_cline_args.mode = CASES[i].mode;
        Scp.build_cline_args.size = CASES[i].size;
        Scp.build_cline_args.name = CASES[i].name;
        Scp.build_cline_args.out = out;
        Scp.build_cline_args.cap = sizeof(out);
        Scp.build_cline(scp_work);
        size_t n = Scp.n;
        TEST_ASSERT_EQUAL_size_t(strlen(CASES[i].line), n);
        TEST_ASSERT_EQUAL_STRING(CASES[i].line, out);

        uint32_t mode = 0;
        uint64_t size = 0;
        char name[32];
        Scp.parse_cline_args.line = out;
        Scp.parse_cline_args.len = n;
        Scp.parse_cline_args.mode_out = &mode;
        Scp.parse_cline_args.size_out = &size;
        Scp.parse_cline_args.name_out = name;
        Scp.parse_cline_args.name_cap = sizeof(name);
        Scp.parse_cline(scp_work);
        TEST_ASSERT_TRUE(Scp.ok);
        TEST_ASSERT_EQUAL_UINT32(CASES[i].mode, mode);
        TEST_ASSERT_EQUAL_UINT64(CASES[i].size, size);
        TEST_ASSERT_EQUAL_STRING(CASES[i].name, name);
    }
}

// The builder writes the mode in four columns and masks it to the twelve permission bits, so a
// caller passing a full POSIX mode word (with S_IFREG 0100000 in it) still emits a four-digit mode.
void test_build_masks_the_file_type_bits(void)
{
    char out[64];
    Scp.build_cline_args.mode = 0100644u;
    Scp.build_cline_args.size = 1;
    Scp.build_cline_args.name = "f";
    Scp.build_cline_args.out = out;
    Scp.build_cline_args.cap = sizeof(out);
    Scp.build_cline(scp_work);
    TEST_ASSERT_EQUAL_size_t(strlen("C0644 1 f\n"), Scp.n);
    TEST_ASSERT_EQUAL_STRING("C0644 1 f\n", out);
    // The setuid bit is inside the twelve, so it survives as a fifth-column-free 4-digit mode.
    Scp.build_cline_args.mode = 04755u;
    Scp.build_cline_args.size = 1;
    Scp.build_cline_args.name = "f";
    Scp.build_cline_args.out = out;
    Scp.build_cline_args.cap = sizeof(out);
    Scp.build_cline(scp_work);
    TEST_ASSERT_EQUAL_size_t(strlen("C4755 1 f\n"), Scp.n);
    TEST_ASSERT_EQUAL_STRING("C4755 1 f\n", out);
}

// The record type letter selects the record: only a file record `C` is handled here. A directory
// record `D`, its terminator `E`, and the time record `T` are all other records and must not be
// read as a file.
void test_only_c_records_are_file_records(void)
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char name[64];
    TEST_ASSERT_FALSE(parse_cline("D0755 0 dir\n", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_FALSE(parse_cline("E\n", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_FALSE(parse_cline("T1234567890 0 1234567890 0\n", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_FALSE(parse_cline("c0644 1 f\n", &mode, &size, name, sizeof(name))); // case matters
}

// A record that ends inside a field is refused rather than completed with whatever follows in
// memory: the parser only accepts a line whose every field is terminated by its own separator.
void test_truncated_records_are_refused(void)
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char name[64];
    TEST_ASSERT_FALSE(parse_cline("C0644", &mode, &size, name, sizeof(name)));      // ends in the mode
    TEST_ASSERT_FALSE(parse_cline("C0644 ", &mode, &size, name, sizeof(name)));     // no size
    TEST_ASSERT_FALSE(parse_cline("C0644 123", &mode, &size, name, sizeof(name)));  // ends in the size
    TEST_ASSERT_FALSE(parse_cline("C0644 123 ", &mode, &size, name, sizeof(name))); // no name
    TEST_ASSERT_FALSE(parse_cline("C0644 10 \n", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_FALSE(parse_cline("C0644 12x name\n", &mode, &size, name, sizeof(name))); // junk in the size
    TEST_ASSERT_FALSE(parse_cline("C0644  10 n\n", &mode, &size, name, sizeof(name)));    // two separators
    Scp.parse_cline_args.line = NULL;
    Scp.parse_cline_args.len = 12;
    Scp.parse_cline_args.mode_out = &mode;
    Scp.parse_cline_args.size_out = &size;
    Scp.parse_cline_args.name_out = name;
    Scp.parse_cline_args.name_cap = sizeof(name);
    Scp.parse_cline(scp_work);
    TEST_ASSERT_FALSE(Scp.ok);
    Scp.parse_cline_args.line = "C0644 1 x";
    Scp.parse_cline_args.len = 0;
    Scp.parse_cline_args.mode_out = &mode;
    Scp.parse_cline_args.size_out = &size;
    Scp.parse_cline_args.name_out = name;
    Scp.parse_cline_args.name_cap = sizeof(name);
    Scp.parse_cline(scp_work);
    TEST_ASSERT_FALSE(Scp.ok);
}

// The trailing newline delimits the record but is not part of the name, and a record handed over
// without it parses the same way.
void test_name_ends_at_the_newline_or_the_length(void)
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char name[64];

    TEST_ASSERT_TRUE(parse_cline("C0644 7 a b c\n", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("a b c", name); // spaces inside the name are kept
    TEST_ASSERT_EQUAL_UINT64(7, size);

    TEST_ASSERT_TRUE(parse_cline("C0644 7 a b c", &mode, &size, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("a b c", name);

    // An embedded NUL terminates the name the same way a newline does.
    static const char REC[] = "C0644 10 abc\0xyz";
    Scp.parse_cline_args.line = REC;
    Scp.parse_cline_args.len = sizeof(REC) - 1;
    Scp.parse_cline_args.mode_out = &mode;
    Scp.parse_cline_args.size_out = &size;
    Scp.parse_cline_args.name_out = name;
    Scp.parse_cline_args.name_cap = sizeof(name);
    Scp.parse_cline(scp_work);
    TEST_ASSERT_TRUE(Scp.ok);
    TEST_ASSERT_EQUAL_STRING("abc", name);
}

// A name longer than the caller's buffer is refused, never truncated: half a filename names a
// different file, and the receiver would create it.
void test_name_too_long_is_refused_not_truncated(void)
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char small[4];
    memset(small, 'Z', sizeof(small));
    TEST_ASSERT_FALSE(parse_cline("C0644 10 longname\n", &mode, &size, small, sizeof(small)));
    TEST_ASSERT_EQUAL_CHAR('Z', small[0]); // nothing was written

    // Exactly one short of the buffer fits; the buffer's worth does not (the NUL needs a byte).
    char four[4];
    TEST_ASSERT_TRUE(parse_cline("C0644 10 abc\n", &mode, &size, four, sizeof(four)));
    TEST_ASSERT_EQUAL_STRING("abc", four);
    TEST_ASSERT_FALSE(parse_cline("C0644 10 abcd\n", &mode, &size, four, sizeof(four)));
}

// mode_out and size_out are optional, so a caller that only wants the filename may pass null for
// both without losing the name.
void test_mode_and_size_outputs_are_optional(void)
{
    char name[64];
    TEST_ASSERT_TRUE(parse_cline("C0644 10 n\n", NULL, NULL, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("n", name);
}

// The builder refuses a destination that cannot hold the line plus its terminator, rather than
// emitting a truncated record.
void test_build_refuses_a_short_buffer(void)
{
    char tiny[6];
    memset(tiny, 'Z', sizeof(tiny));
    Scp.build_cline_args.mode = 0644;
    Scp.build_cline_args.size = 1234;
    Scp.build_cline_args.name = "part.nc";
    Scp.build_cline_args.out = tiny;
    Scp.build_cline_args.cap = sizeof(tiny);
    Scp.build_cline(scp_work);
    TEST_ASSERT_EQUAL_size_t(0, Scp.n);

    // "C0644 1 f\n" is 10 octets, so 10 leaves no room for the NUL and 11 does.
    char exact[11];
    Scp.build_cline_args.mode = 0644;
    Scp.build_cline_args.size = 1;
    Scp.build_cline_args.name = "f";
    Scp.build_cline_args.out = exact;
    Scp.build_cline_args.cap = 10;
    Scp.build_cline(scp_work);
    TEST_ASSERT_EQUAL_size_t(0, Scp.n);
    Scp.build_cline_args.mode = 0644;
    Scp.build_cline_args.size = 1;
    Scp.build_cline_args.name = "f";
    Scp.build_cline_args.out = exact;
    Scp.build_cline_args.cap = 11;
    Scp.build_cline(scp_work);
    TEST_ASSERT_EQUAL_size_t(10, Scp.n);
    TEST_ASSERT_EQUAL_STRING("C0644 1 f\n", exact);
}

// The exec command's role flag decides the direction: -t makes the device the sink (it receives),
// -f makes it the source (it sends). Everything after the flags is the target path.
void test_role_flag_selects_sink_or_source(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(SCP_MODE_SINK, parse_cmd("scp -t /gcode/prog.nc", path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("/gcode/prog.nc", path);

    TEST_ASSERT_EQUAL_INT(SCP_MODE_SOURCE, parse_cmd("scp -f /a/b.txt", path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("/a/b.txt", path);

    // The other flags scp sends (-v verbose, -r recursive, -p preserve, -d directory) are accepted
    // and do not disturb the role.
    TEST_ASSERT_EQUAL_INT(SCP_MODE_SOURCE, parse_cmd("scp -v -p -f /a/b.txt", path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("/a/b.txt", path);
    TEST_ASSERT_EQUAL_INT(SCP_MODE_SINK, parse_cmd("scp -r -d -t /dir", path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("/dir", path);

    // Flags may be bundled into one token, as scp itself does.
    TEST_ASSERT_EQUAL_INT(SCP_MODE_SINK, parse_cmd("scp -vrt /dir", path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("/dir", path);
    TEST_ASSERT_EQUAL_INT(SCP_MODE_SOURCE, parse_cmd("scp -pf /dir", path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("/dir", path);
}

// A command with no role flag names no direction, so it is not an scp invocation this side serves.
void test_command_without_a_role_is_invalid(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(SCP_MODE_INVALID, parse_cmd("scp /x", path, sizeof(path)));
    TEST_ASSERT_EQUAL_INT(SCP_MODE_INVALID, parse_cmd("scp -v /x", path, sizeof(path)));
    TEST_ASSERT_EQUAL_INT(SCP_MODE_INVALID, parse_cmd("", path, sizeof(path)));
    TEST_ASSERT_EQUAL_INT(SCP_MODE_INVALID, parse_cmd("   ", path, sizeof(path)));
    TEST_ASSERT_EQUAL_INT(SCP_MODE_INVALID, parse_cmd("-t", path, sizeof(path))); // role but no token
    TEST_ASSERT_EQUAL_INT(SCP_MODE_INVALID, parse_cmd("-f -v -r", path, sizeof(path)));
}

// Whitespace is a separator, not data: repeated and trailing spaces produce no empty token, and a
// one-character token cannot be a flag group so it is the path.
void test_command_tokenizing(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(SCP_MODE_SINK, parse_cmd("scp   -t    /x   ", path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("/x", path);
    TEST_ASSERT_EQUAL_INT(SCP_MODE_SINK, parse_cmd("scp -t a", path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("a", path);
    TEST_ASSERT_EQUAL_INT(SCP_MODE_SINK, parse_cmd("scp -t -", path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("-", path); // a lone '-' is too short to be a flag group
}

// A path that does not fit the caller's buffer is refused, so no half path reaches the filesystem.
void test_path_too_long_is_refused(void)
{
    char small[4];
    memset(small, 'Z', sizeof(small));
    TEST_ASSERT_EQUAL_INT(SCP_MODE_INVALID, parse_cmd("scp -t /long/path", small, sizeof(small)));
    TEST_ASSERT_EQUAL_CHAR('Z', small[0]);

    TEST_ASSERT_EQUAL_INT(SCP_MODE_SINK, parse_cmd("scp -t abc", small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("abc", small);
    TEST_ASSERT_EQUAL_INT(SCP_MODE_INVALID, parse_cmd("scp -t abcd", small, sizeof(small)));
}

// Null arguments and a zero capacity are refused before anything is parsed.
void test_command_null_arguments_are_refused(void)
{
    char path[128];
    Scp.parse_cmd_args.cmd = NULL;
    Scp.parse_cmd_args.cmd_len = 9;
    Scp.parse_cmd_args.path_out = path;
    Scp.parse_cmd_args.path_cap = sizeof(path);
    Scp.parse_cmd(scp_work);
    TEST_ASSERT_EQUAL_INT(SCP_MODE_INVALID, Scp.value);
    Scp.parse_cmd_args.cmd = "scp -t /x";
    Scp.parse_cmd_args.cmd_len = 9;
    Scp.parse_cmd_args.path_out = NULL;
    Scp.parse_cmd_args.path_cap = sizeof(path);
    Scp.parse_cmd(scp_work);
    TEST_ASSERT_EQUAL_INT(SCP_MODE_INVALID, Scp.value);
    Scp.parse_cmd_args.cmd = "scp -t /x";
    Scp.parse_cmd_args.cmd_len = 9;
    Scp.parse_cmd_args.path_out = path;
    Scp.parse_cmd_args.path_cap = 0;
    Scp.parse_cmd(scp_work);
    TEST_ASSERT_EQUAL_INT(SCP_MODE_INVALID, Scp.value);
}

// The acknowledgement octets between records: 0 proceeds, 1 is a warning and 2 a fatal error, each
// of the latter followed by a message and a newline.
void test_ack_octets(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_SCP_ACK_OK);
    TEST_ASSERT_EQUAL_UINT8(1, PROTOCORE_SCP_ACK_WARN);
    TEST_ASSERT_EQUAL_UINT8(2, PROTOCORE_SCP_ACK_ERROR);
}
