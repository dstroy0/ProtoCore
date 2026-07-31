// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the NTRIP caster protocol codec (services/timing_position/gnss/pc_ntrip_caster): rover request parsing
// (mountpoint, NTRIP version, HTTP Basic auth), the stream-accept / error responses, and the RTCM
// source table (STR records + ENDSOURCETABLE, with a self-consistent Content-Length). Pure host tests.

#include "services/timing_position/gnss/ntrip_caster.h"
#include "shared_primitives/numparse.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_parse_v1_stream_request()
{
    const char *req = "GET /BASE1 HTTP/1.0\r\nUser-Agent: NTRIP testclient/1.0\r\n\r\n";
    NtripRequest r;
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(req, strlen(req), &r));
    TEST_ASSERT_TRUE(r.complete);
    TEST_ASSERT_TRUE(r.is_get);
    TEST_ASSERT_EQUAL(NtripVersion::NTRIP_V1, r.version);
    TEST_ASSERT_EQUAL_STRING("BASE1", r.mountpoint);
    TEST_ASSERT_FALSE(r.want_sourcetable);
    TEST_ASSERT_NULL(r.auth_b64);
}

void test_parse_v2_request_detects_version()
{
    const char *req = "GET /BASE1 HTTP/1.1\r\n"
                      "Host: caster.example\r\n"
                      "Ntrip-Version: Ntrip/2.0\r\n"
                      "User-Agent: NTRIP rover/2\r\n\r\n";
    NtripRequest r;
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(req, strlen(req), &r));
    TEST_ASSERT_EQUAL(NtripVersion::NTRIP_V2, r.version);
    TEST_ASSERT_EQUAL_STRING("BASE1", r.mountpoint);
}

void test_parse_sourcetable_request()
{
    const char *req = "GET / HTTP/1.0\r\n\r\n";
    NtripRequest r;
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(req, strlen(req), &r));
    TEST_ASSERT_TRUE(r.want_sourcetable);
    TEST_ASSERT_EQUAL_STRING("", r.mountpoint);
}

void test_parse_extracts_basic_auth()
{
    // The parser spans the base64 token verbatim (it does not decode it).
    const char *req = "GET /M HTTP/1.0\r\nAuthorization: Basic dXNlcjpwYXNz\r\n\r\n";
    NtripRequest r;
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(req, strlen(req), &r));
    TEST_ASSERT_NOT_NULL(r.auth_b64);
    TEST_ASSERT_EQUAL_UINT16(12, r.auth_b64_len);
    TEST_ASSERT_EQUAL_INT(0, strncmp(r.auth_b64, "dXNlcjpwYXNz", 12));
}

void test_parse_incomplete_needs_more()
{
    const char *req = "GET /BASE1 HTTP/1.0\r\nUser-Agent: x\r\n"; // no blank line yet
    NtripRequest r;
    TEST_ASSERT_FALSE(pc_ntrip_request_parse(req, strlen(req), &r));
}

void test_parse_rejects_non_get()
{
    const char *req = "POST /BASE1 HTTP/1.0\r\n\r\n";
    NtripRequest r;
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(req, strlen(req), &r)); // header block complete...
    TEST_ASSERT_FALSE(r.is_get);                                    // ...but not a GET
}

void test_stream_response_v1_v2()
{
    char buf[256];
    size_t n1 = pc_ntrip_build_stream_response(buf, sizeof(buf), NtripVersion::NTRIP_V1);
    TEST_ASSERT_EQUAL_size_t(strlen("ICY 200 OK\r\n\r\n"), n1);
    TEST_ASSERT_EQUAL_STRING("ICY 200 OK\r\n\r\n", buf);

    size_t n2 = pc_ntrip_build_stream_response(buf, sizeof(buf), NtripVersion::NTRIP_V2);
    TEST_ASSERT_TRUE(n2 > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "HTTP/1.1 200 OK\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Content-Type: gnss/data\r\n"));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[n2]); // NUL-terminated at the returned length
}

void test_str_record_format()
{
    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "BASE1";
    m.identifier = "Lab roof";
    m.format_details = "1005(1),1006(10)";
    m.nav_system = "GPS";
    m.country = "USA";
    m.generator = "PC";
    m.lat_deg = 37.77;
    m.lon_deg = -122.42;
    m.nmea_required = false;

    char rec[192];
    size_t n = pc_ntrip_build_str_record(rec, sizeof(rec), &m);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("STR;BASE1;Lab roof;RTCM 3.3;1005(1),1006(10);0;GPS;none;USA;37.77;-122.42;0;0;"
                             "PC;none;N;N;9600;",
                             rec);
    // A well-formed STR record has 19 fields (18 semicolons).
    int semis = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (rec[i] == ';')
        {
            semis++;
        }
    }
    TEST_ASSERT_EQUAL_INT(18, semis);
}

void test_str_record_defaults_and_negative_small_lon()
{
    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "M";
    m.lat_deg = 0.0;
    m.lon_deg = -0.05; // exercises the "-0.05" small-negative path
    char rec[192];
    size_t n = pc_ntrip_build_str_record(rec, sizeof(rec), &m);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(rec, ";GPS;"));        // nav-system default
    TEST_ASSERT_NOT_NULL(strstr(rec, ";1005(1);"));    // format-details default
    TEST_ASSERT_NOT_NULL(strstr(rec, ";PC;"));         // generator default
    TEST_ASSERT_NOT_NULL(strstr(rec, ";0.00;-0.05;")); // lat/lon formatting incl. small negative
}

// Read the Content-Length header value out of a response.
static long content_length_of(const char *resp)
{
    const char *h = strstr(resp, "Content-Length:");
    if (!h)
    {
        return -1;
    }
    const char *end = h;
    return pc_strtol(h + 15, &end);
}

void test_sourcetable_has_records_and_correct_length()
{
    NtripMount mounts[2];
    memset(mounts, 0, sizeof(mounts));
    mounts[0].mountpoint = "BASE1";
    mounts[0].identifier = "Roof";
    mounts[0].lat_deg = 37.77;
    mounts[0].lon_deg = -122.42;
    mounts[1].mountpoint = "BASE2";
    mounts[1].identifier = "Field";
    mounts[1].lat_deg = 40.0;
    mounts[1].lon_deg = -105.0;

    char buf[1024];
    size_t n = pc_ntrip_build_sourcetable(buf, sizeof(buf), NtripVersion::NTRIP_V1, mounts, 2);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "SOURCETABLE 200 OK\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "STR;BASE1;"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "STR;BASE2;"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "ENDSOURCETABLE\r\n"));

    // The advertised Content-Length must equal the actual body (everything after the blank line).
    const char *body = strstr(buf, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(body);
    body += 4;
    long declared = content_length_of(buf);
    TEST_ASSERT_EQUAL_INT((long)strlen(body), declared);
}

void test_sourcetable_v2_content_type()
{
    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "BASE1";
    char buf[512];
    size_t n = pc_ntrip_build_sourcetable(buf, sizeof(buf), NtripVersion::NTRIP_V2, &m, 1);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "HTTP/1.1 200 OK\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Content-Type: gnss/sourcetable\r\n"));
}

void test_error_response()
{
    char buf[128];
    size_t n1 = pc_ntrip_build_error_response(buf, sizeof(buf), NtripVersion::NTRIP_V1);
    TEST_ASSERT_TRUE(n1 > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "ERROR"));
    size_t n2 = pc_ntrip_build_error_response(buf, sizeof(buf), NtripVersion::NTRIP_V2);
    TEST_ASSERT_TRUE(n2 > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "400 Bad Request"));
}

void test_unauthorized_response()
{
    char buf[128];
    size_t n1 = pc_ntrip_build_unauthorized_response(buf, sizeof(buf), NtripVersion::NTRIP_V1);
    TEST_ASSERT_TRUE(n1 > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Bad Password"));
    size_t n2 = pc_ntrip_build_unauthorized_response(buf, sizeof(buf), NtripVersion::NTRIP_V2);
    TEST_ASSERT_TRUE(n2 > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "401 Unauthorized"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "WWW-Authenticate: Basic"));
}

void test_response_overflow_fails_closed()
{
    char tiny[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_stream_response(tiny, sizeof(tiny), NtripVersion::NTRIP_V2));
    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "BASE1";
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_sourcetable(tiny, sizeof(tiny), NtripVersion::NTRIP_V1, &m, 1));
}

void test_parse_bare_lf_header_block()
{
    // Some minimal rovers terminate with bare LFs. The LFLF fallback must find the block end, and the
    // header scan must cope with lines that have no trailing CR and with the empty final line.
    const char *req = "GET /BASE1 HTTP/1.0\nNtrip-Version: Ntrip/2.0\n\n";
    NtripRequest r;
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(req, strlen(req), &r));
    TEST_ASSERT_TRUE(r.complete);
    TEST_ASSERT_TRUE(r.is_get);
    TEST_ASSERT_EQUAL_STRING("BASE1", r.mountpoint);
    TEST_ASSERT_EQUAL(NtripVersion::NTRIP_V2, r.version); // headers were still scanned
}

void test_parse_target_terminators()
{
    NtripRequest r;
    // A query string ends the mountpoint (it is not part of it).
    const char *q = "GET /BASE1?x=1 HTTP/1.0\r\n\r\n";
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(q, strlen(q), &r));
    TEST_ASSERT_EQUAL_STRING("BASE1", r.mountpoint);

    // So does an HTTP/0.9-style request line with no version token, CRLF- or LF-terminated.
    const char *cr = "GET /BASE1\r\n\r\n";
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(cr, strlen(cr), &r));
    TEST_ASSERT_EQUAL_STRING("BASE1", r.mountpoint);
    const char *lf = "GET /BASE1\n\n";
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(lf, strlen(lf), &r));
    TEST_ASSERT_EQUAL_STRING("BASE1", r.mountpoint);

    // An empty target is the source-table request, same as "GET /".
    const char *empty = "GET \r\n\r\n";
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(empty, strlen(empty), &r));
    TEST_ASSERT_TRUE(r.want_sourcetable);
    TEST_ASSERT_EQUAL_STRING("", r.mountpoint);

    // A one-character target that is not "/" is a mountpoint, not a source-table request.
    const char *one = "GET X HTTP/1.0\r\n\r\n";
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(one, strlen(one), &r));
    TEST_ASSERT_FALSE(r.want_sourcetable);
    TEST_ASSERT_EQUAL_STRING("X", r.mountpoint);
}

void test_parse_overlong_mountpoint_is_truncated_to_the_field()
{
    // A target longer than the mountpoint field is clipped rather than overflowing it.
    char req[128];
    char longmp[64];
    memset(longmp, 'M', sizeof(longmp) - 1);
    longmp[sizeof(longmp) - 1] = '\0';
    snprintf(req, sizeof(req), "GET /%s HTTP/1.0\r\n\r\n", longmp);
    NtripRequest r;
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(req, strlen(req), &r));
    TEST_ASSERT_EQUAL_size_t(PC_NTRIP_MOUNT_MAX - 1, strlen(r.mountpoint));
    TEST_ASSERT_EQUAL_INT(0, strncmp(r.mountpoint, longmp, PC_NTRIP_MOUNT_MAX - 1));
}

void test_parse_stray_cr_does_not_end_the_header_block()
{
    // A CR not followed by LF, and a CRLF+CR not followed by LF, are ordinary bytes: only a real
    // blank line terminates the block.
    const char *stray = "GET /M\rX HTTP/1.0\r\n\rY\r\n\r\n";
    NtripRequest r;
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(stray, strlen(stray), &r));
    TEST_ASSERT_TRUE(r.complete);
    TEST_ASSERT_TRUE(r.is_get);
    TEST_ASSERT_EQUAL_STRING("M", r.mountpoint); // the target still ends at the stray CR
}

void test_parse_ntrip_version_scan_skips_near_misses()
{
    // The version scan looks for the literal "2.0" anywhere in the value; digits that only partly
    // match must not trip it.
    const char *near = "GET /M HTTP/1.1\r\nNtrip-Version: 2x 2.5 2.0\r\n\r\n";
    NtripRequest r;
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(near, strlen(near), &r));
    TEST_ASSERT_EQUAL(NtripVersion::NTRIP_V2, r.version);

    // A version header that never contains "2.0" leaves the request at v1.
    const char *v1 = "GET /M HTTP/1.1\r\nNtrip-Version: Ntrip/1.0\r\n\r\n";
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(v1, strlen(v1), &r));
    TEST_ASSERT_EQUAL(NtripVersion::NTRIP_V1, r.version);
}

void test_parse_authorization_variants()
{
    NtripRequest r;
    // A non-Basic scheme is ignored (the caster only understands Basic).
    const char *bearer = "GET /M HTTP/1.0\r\nAuthorization: Bearer abc.def\r\n\r\n";
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(bearer, strlen(bearer), &r));
    TEST_ASSERT_NULL(r.auth_b64);

    // A header with no value at all is ignored rather than read past its line.
    const char *bare = "GET /M HTTP/1.0\r\nAuthorization:\r\n\r\n";
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(bare, strlen(bare), &r));
    TEST_ASSERT_NULL(r.auth_b64);

    // A tab between the colon and the scheme is legal OWS.
    const char *tabbed = "GET /M HTTP/1.0\r\nAuthorization:\tBasic dXNlcjpwYXNz\r\n\r\n";
    TEST_ASSERT_TRUE(pc_ntrip_request_parse(tabbed, strlen(tabbed), &r));
    TEST_ASSERT_NOT_NULL(r.auth_b64);
    TEST_ASSERT_EQUAL_UINT16(12, r.auth_b64_len);
    TEST_ASSERT_EQUAL_INT(0, strncmp(r.auth_b64, "dXNlcjpwYXNz", 12));
}

void test_error_and_unauthorized_responses_truncate_closed()
{
    // Every response builder reports 0 rather than emitting a half-written status line.
    char tiny[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_stream_response(tiny, sizeof(tiny), NtripVersion::NTRIP_V1));
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_error_response(tiny, sizeof(tiny), NtripVersion::NTRIP_V1));
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_error_response(tiny, sizeof(tiny), NtripVersion::NTRIP_V2));
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_unauthorized_response(tiny, sizeof(tiny), NtripVersion::NTRIP_V1));
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_unauthorized_response(tiny, sizeof(tiny), NtripVersion::NTRIP_V2));

    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "BASE1";
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_str_record(tiny, sizeof(tiny), &m));
}

void test_str_record_requires_a_mountpoint()
{
    char rec[192];
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_str_record(rec, sizeof(rec), nullptr));
    NtripMount m;
    memset(&m, 0, sizeof(m)); // mountpoint left null
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_str_record(rec, sizeof(rec), &m));
}

void test_str_record_nmea_required_flag()
{
    // The nmea field is 1 when the mount needs a GGA from the rover, 0 otherwise.
    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "VRS";
    m.nmea_required = true;
    char rec[192];
    size_t n = pc_ntrip_build_str_record(rec, sizeof(rec), &m);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(rec, ";0.00;0.00;1;0;")); // lat;lon;nmea;solution
}

void test_sourcetable_rejects_an_unbuildable_mount()
{
    // The length pass runs every record first, so one bad mount fails the whole table rather than
    // advertising a Content-Length that the second pass cannot honour.
    NtripMount mounts[2];
    memset(mounts, 0, sizeof(mounts));
    mounts[0].mountpoint = "BASE1";
    mounts[1].mountpoint = nullptr; // unbuildable
    char buf[1024];
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_sourcetable(buf, sizeof(buf), NtripVersion::NTRIP_V1, mounts, 2));
}

void test_sourcetable_capacity_boundaries()
{
    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "BASE1";
    m.identifier = "Roof";
    m.lat_deg = 37.77;
    m.lon_deg = -122.42;

    char rec[192];
    const size_t R = pc_ntrip_build_str_record(rec, sizeof(rec), &m);
    TEST_ASSERT_TRUE(R > 0);

    char buf[1024];
    const size_t full = pc_ntrip_build_sourcetable(buf, sizeof(buf), NtripVersion::NTRIP_V1, &m, 1);
    TEST_ASSERT_TRUE(full > 0);
    const char *body = strstr(buf, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(body);
    const size_t H = (size_t)(body + 4 - buf); // header block length
    const size_t END = strlen("ENDSOURCETABLE\r\n");
    TEST_ASSERT_EQUAL_size_t(H + R + 2 + END, full);

    // One byte short of the record itself: the record pass reports 0 and the table fails closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_sourcetable(buf, H + R, NtripVersion::NTRIP_V1, &m, 1));
    // The record fits but its CRLF does not.
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_sourcetable(buf, H + R + 1, NtripVersion::NTRIP_V1, &m, 1));
    // Records fit but the ENDSOURCETABLE terminator + NUL does not.
    TEST_ASSERT_EQUAL_size_t(0, pc_ntrip_build_sourcetable(buf, H + R + 2 + END, NtripVersion::NTRIP_V1, &m, 1));
    // Exactly one more byte is enough: the whole table plus its NUL.
    memset(buf, 0x7F, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(full, pc_ntrip_build_sourcetable(buf, H + R + 2 + END + 1, NtripVersion::NTRIP_V1, &m, 1));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[full]);
    TEST_ASSERT_NOT_NULL(strstr(buf, "ENDSOURCETABLE\r\n"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_v1_stream_request);
    RUN_TEST(test_parse_v2_request_detects_version);
    RUN_TEST(test_parse_sourcetable_request);
    RUN_TEST(test_parse_extracts_basic_auth);
    RUN_TEST(test_parse_incomplete_needs_more);
    RUN_TEST(test_parse_rejects_non_get);
    RUN_TEST(test_stream_response_v1_v2);
    RUN_TEST(test_str_record_format);
    RUN_TEST(test_str_record_defaults_and_negative_small_lon);
    RUN_TEST(test_sourcetable_has_records_and_correct_length);
    RUN_TEST(test_sourcetable_v2_content_type);
    RUN_TEST(test_error_response);
    RUN_TEST(test_unauthorized_response);
    RUN_TEST(test_response_overflow_fails_closed);
    RUN_TEST(test_parse_bare_lf_header_block);
    RUN_TEST(test_parse_target_terminators);
    RUN_TEST(test_parse_overlong_mountpoint_is_truncated_to_the_field);
    RUN_TEST(test_parse_stray_cr_does_not_end_the_header_block);
    RUN_TEST(test_parse_ntrip_version_scan_skips_near_misses);
    RUN_TEST(test_parse_authorization_variants);
    RUN_TEST(test_error_and_unauthorized_responses_truncate_closed);
    RUN_TEST(test_str_record_requires_a_mountpoint);
    RUN_TEST(test_str_record_nmea_required_flag);
    RUN_TEST(test_sourcetable_rejects_an_unbuildable_mount);
    RUN_TEST(test_sourcetable_capacity_boundaries);
    return UNITY_END();
}
