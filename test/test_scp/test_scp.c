// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/scp: the SCP (RCP) wire codec. Covers parsing an `scp -t/-f <path>` exec command
// into its sink/source role + target path (with extra flags), parsing + building the `C<mode> <size> <name>`
// control line (octal mode, decimal size, name; malformed/D-record rejection), and the ack byte constants.

#include "network_drivers/application/scp/scp.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static void test_parse_cmd_sink()
{
    char path[128];
    ScpMode m = pc_scp_parse_cmd("scp -t /gcode/prog.nc", strlen("scp -t /gcode/prog.nc"), path, sizeof(path));
    TEST_ASSERT_EQUAL(SINK, m);
    TEST_ASSERT_EQUAL_STRING("/gcode/prog.nc", path);
}

static void test_parse_cmd_source_with_flags()
{
    char path[128];
    ScpMode m = pc_scp_parse_cmd("scp -v -p -f /a/b.txt", strlen("scp -v -p -f /a/b.txt"), path, sizeof(path));
    TEST_ASSERT_EQUAL(SOURCE, m);
    TEST_ASSERT_EQUAL_STRING("/a/b.txt", path);
}

static void test_parse_cmd_invalid()
{
    char path[128];
    // no -t/-f role
    TEST_ASSERT_EQUAL(INVALID, pc_scp_parse_cmd("scp /x", strlen("scp /x"), path, sizeof(path)));
    // path too long for the buffer
    char small[4];
    TEST_ASSERT_EQUAL(INVALID,
                      pc_scp_parse_cmd("scp -t /long/path", strlen("scp -t /long/path"), small, sizeof(small)));
}

static void test_parse_cline()
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char name[64];
    TEST_ASSERT_TRUE(
        pc_scp_parse_cline("C0644 60000 prog.nc\n", strlen("C0644 60000 prog.nc\n"), &mode, &size, name, sizeof(name)));
    TEST_ASSERT_EQUAL_HEX32(0644, mode);
    TEST_ASSERT_EQUAL_UINT64(60000, size);
    TEST_ASSERT_EQUAL_STRING("prog.nc", name);

    // no trailing newline is fine
    TEST_ASSERT_TRUE(pc_scp_parse_cline("C0755 0 empty", strlen("C0755 0 empty"), &mode, &size, name, sizeof(name)));
    TEST_ASSERT_EQUAL_HEX32(0755, mode);
    TEST_ASSERT_EQUAL_UINT64(0, size);
    TEST_ASSERT_EQUAL_STRING("empty", name);
}

static void test_parse_cline_malformed()
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char name[64];
    // a directory record (D) is not a file record
    TEST_ASSERT_FALSE(pc_scp_parse_cline("D0755 0 dir\n", strlen("D0755 0 dir\n"), &mode, &size, name, sizeof(name)));
    // missing size field
    TEST_ASSERT_FALSE(pc_scp_parse_cline("C0644 name\n", strlen("C0644 name\n"), &mode, &size, name, sizeof(name)));
    // empty name
    TEST_ASSERT_FALSE(pc_scp_parse_cline("C0644 10 \n", strlen("C0644 10 \n"), &mode, &size, name, sizeof(name)));
    // non-octal mode digit (8)
    TEST_ASSERT_FALSE(pc_scp_parse_cline("C0648 10 n\n", strlen("C0648 10 n\n"), &mode, &size, name, sizeof(name)));
}

static void test_build_cline_roundtrip()
{
    char out[64];
    size_t n = pc_scp_build_cline(0644, 1234, "part.nc", out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("C0644 1234 part.nc\n", out);

    // parse it back
    uint32_t mode = 0;
    uint64_t size = 0;
    char name[32];
    TEST_ASSERT_TRUE(pc_scp_parse_cline(out, n, &mode, &size, name, sizeof(name)));
    TEST_ASSERT_EQUAL_HEX32(0644, mode);
    TEST_ASSERT_EQUAL_UINT64(1234, size);
    TEST_ASSERT_EQUAL_STRING("part.nc", name);

    // overflow
    char tiny[6];
    TEST_ASSERT_EQUAL_UINT(0, pc_scp_build_cline(0644, 1234, "part.nc", tiny, sizeof(tiny)));
}

// Null/zero-capacity arguments are rejected before any parsing.
static void test_parse_cmd_null_args()
{
    char path[128];
    TEST_ASSERT_EQUAL(INVALID, pc_scp_parse_cmd(NULL, 9, path, sizeof(path)));
    TEST_ASSERT_EQUAL(INVALID, pc_scp_parse_cmd("scp -t /x", 9, NULL, sizeof(path)));
    TEST_ASSERT_EQUAL(INVALID, pc_scp_parse_cmd("scp -t /x", 9, path, 0));
}

// Trailing whitespace is consumed without inventing an empty final token.
static void test_parse_cmd_trailing_spaces()
{
    char path[128];
    const char *c = "scp -t /x   ";
    TEST_ASSERT_EQUAL(SINK, pc_scp_parse_cmd(c, strlen(c), path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("/x", path);
}

// A one-character token cannot be a flag group, so it is treated as the path.
static void test_parse_cmd_single_char_path()
{
    char path[128];
    const char *c = "scp -t a";
    TEST_ASSERT_EQUAL(SINK, pc_scp_parse_cmd(c, strlen(c), path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("a", path);
}

// A role flag with no path at all is invalid.
static void test_parse_cmd_flag_without_path()
{
    char path[128];
    TEST_ASSERT_EQUAL(INVALID, pc_scp_parse_cmd("-t", 2, path, sizeof(path)));
    TEST_ASSERT_EQUAL(INVALID, pc_scp_parse_cmd("   ", 3, path, sizeof(path)));
}

// The C-record header guards: null line and zero length.
static void test_parse_cline_null_and_empty()
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char name[64];
    TEST_ASSERT_FALSE(pc_scp_parse_cline(NULL, 12, &mode, &size, name, sizeof(name)));
    TEST_ASSERT_FALSE(pc_scp_parse_cline("C0644 1 x", 0, &mode, &size, name, sizeof(name)));
}

// A record that ends inside a numeric field is rejected rather than read past.
static void test_parse_cline_truncated_fields()
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char name[64];
    TEST_ASSERT_FALSE(pc_scp_parse_cline("C0644", 5, &mode, &size, name, sizeof(name)));     // ends in mode
    TEST_ASSERT_FALSE(pc_scp_parse_cline("C0644 123", 9, &mode, &size, name, sizeof(name))); // ends in size
}

// Non-numeric junk where a field or its separator belongs is rejected.
static void test_parse_cline_bad_separators()
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char name[64];
    // No octal digits at all after 'C'.
    TEST_ASSERT_FALSE(pc_scp_parse_cline("Cxyz 10 n\n", strlen("Cxyz 10 n\n"), &mode, &size, name, sizeof(name)));
    // Size field followed by junk instead of a space.
    TEST_ASSERT_FALSE(
        pc_scp_parse_cline("C0644 12x name\n", strlen("C0644 12x name\n"), &mode, &size, name, sizeof(name)));
}

// An embedded NUL terminates the name just as a newline does.
static void test_parse_cline_name_stops_at_nul()
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char name[64];
    const char rec[] = "C0644 10 abc\0xyz";
    TEST_ASSERT_TRUE(pc_scp_parse_cline(rec, sizeof(rec) - 1, &mode, &size, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("abc", name);
    TEST_ASSERT_EQUAL_UINT64(10, size);
}

// A name that does not fit the caller's buffer is rejected, not truncated.
static void test_parse_cline_name_too_long()
{
    uint32_t mode = 0;
    uint64_t size = 0;
    char tiny[4];
    const char *rec = "C0644 10 longname\n";
    TEST_ASSERT_FALSE(pc_scp_parse_cline(rec, strlen(rec), &mode, &size, tiny, sizeof(tiny)));
}

// mode_out / size_out are optional: the name is still produced without them.
static void test_parse_cline_optional_outputs()
{
    char name[64];
    const char *rec = "C0644 10 n\n";
    TEST_ASSERT_TRUE(pc_scp_parse_cline(rec, strlen(rec), NULL, NULL, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("n", name);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_cmd_sink);
    RUN_TEST(test_parse_cmd_source_with_flags);
    RUN_TEST(test_parse_cmd_invalid);
    RUN_TEST(test_parse_cline);
    RUN_TEST(test_parse_cline_malformed);
    RUN_TEST(test_build_cline_roundtrip);
    RUN_TEST(test_parse_cmd_null_args);
    RUN_TEST(test_parse_cmd_trailing_spaces);
    RUN_TEST(test_parse_cmd_single_char_path);
    RUN_TEST(test_parse_cmd_flag_without_path);
    RUN_TEST(test_parse_cline_null_and_empty);
    RUN_TEST(test_parse_cline_truncated_fields);
    RUN_TEST(test_parse_cline_bad_separators);
    RUN_TEST(test_parse_cline_name_stops_at_nul);
    RUN_TEST(test_parse_cline_name_too_long);
    RUN_TEST(test_parse_cline_optional_outputs);
    return UNITY_END();
}
