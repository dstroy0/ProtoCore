// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/timing_position/gnss/ntrip_caster.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static proto_bool req(const char *s, NtripRequest *out)
{
    return protocore_ntrip_request_parse(s, strlen(s), out);
}

void test_rfc9112_header_block_terminates_the_request(void)
{
    static const char *const FULL = "GET /BASE1 HTTP/1.1\r\n"
                                    "Host: caster.example\r\n"
                                    "Ntrip-Version: Ntrip/2.0\r\n"
                                    "User-Agent: NTRIP rover\r\n"
                                    "\r\n";
    NtripRequest r;
    const size_t n = strlen(FULL);
    for (size_t have = 0; have < n; have++)
    {
        TEST_ASSERT_FALSE(protocore_ntrip_request_parse(FULL, have, &r));
    }
    TEST_ASSERT_TRUE(protocore_ntrip_request_parse(FULL, n, &r));
    TEST_ASSERT_TRUE(r.complete);
    TEST_ASSERT_TRUE(r.is_get);
    TEST_ASSERT_EQUAL_INT(NTRIP_V2, (int)r.version);
    TEST_ASSERT_EQUAL_STRING("BASE1", r.mountpoint);
    TEST_ASSERT_FALSE(r.want_sourcetable);

    TEST_ASSERT_TRUE(req("GET /BASE1 HTTP/1.0\nUser-Agent: NTRIP rover\n\n", &r));
    TEST_ASSERT_TRUE(r.is_get);
    TEST_ASSERT_EQUAL_STRING("BASE1", r.mountpoint);
}

void test_rfc7617_basic_credentials(void)
{
    static const char *const WITH_AUTH = "GET /BASE1 HTTP/1.1\r\n"
                                         "Ntrip-Version: Ntrip/2.0\r\n"
                                         "Authorization: Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==\r\n"
                                         "\r\n";
    NtripRequest r;
    TEST_ASSERT_TRUE(req(WITH_AUTH, &r));
    TEST_ASSERT_NOT_NULL(r.auth_b64);
    TEST_ASSERT_EQUAL_UINT16(28, r.auth_b64_len);
    TEST_ASSERT_EQUAL_MEMORY("QWxhZGRpbjpvcGVuIHNlc2FtZQ==", r.auth_b64, 28);

    TEST_ASSERT_TRUE(req("GET /BASE1 HTTP/1.1\r\n\r\n", &r));
    TEST_ASSERT_NULL(r.auth_b64);
    TEST_ASSERT_EQUAL_UINT16(0, r.auth_b64_len);
}

void test_header_names_are_case_insensitive(void)
{
    NtripRequest r;
    TEST_ASSERT_TRUE(req("GET /M1 HTTP/1.1\r\nntrip-version: Ntrip/2.0\r\n"
                         "AUTHORIZATION: Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==\r\n\r\n",
                         &r));
    TEST_ASSERT_EQUAL_INT(NTRIP_V2, (int)r.version);
    TEST_ASSERT_EQUAL_MEMORY("QWxhZGRpbjpvcGVuIHNlc2FtZQ==", r.auth_b64, 28);
}

void test_version_comes_from_the_ntrip_version_header(void)
{
    NtripRequest r;
    TEST_ASSERT_TRUE(req("GET /BASE1 HTTP/1.0\r\nUser-Agent: NTRIP r\r\n\r\n", &r));
    TEST_ASSERT_EQUAL_INT(NTRIP_V1, (int)r.version);

    TEST_ASSERT_TRUE(req("GET /BASE1 HTTP/1.1\r\nNtrip-Version: Ntrip/2.0\r\n\r\n", &r));
    TEST_ASSERT_EQUAL_INT(NTRIP_V2, (int)r.version);
}

void test_root_request_asks_for_the_source_table(void)
{
    NtripRequest r;
    TEST_ASSERT_TRUE(req("GET / HTTP/1.1\r\n\r\n", &r));
    TEST_ASSERT_TRUE(r.is_get);
    TEST_ASSERT_TRUE(r.want_sourcetable);
    TEST_ASSERT_EQUAL_STRING("", r.mountpoint);

    TEST_ASSERT_TRUE(req("GET /BASE1 HTTP/1.1\r\n\r\n", &r));
    TEST_ASSERT_FALSE(r.want_sourcetable);
}

void test_non_get_and_oversized_mountpoints(void)
{
    NtripRequest r;
    TEST_ASSERT_TRUE(req("POST /BASE1 HTTP/1.1\r\n\r\n", &r));
    TEST_ASSERT_TRUE(r.complete);
    TEST_ASSERT_FALSE(r.is_get);

    TEST_ASSERT_TRUE(req("SOURCE pass /BASE1\r\n\r\n", &r));
    TEST_ASSERT_FALSE(r.is_get);

    char big[256];
    size_t o = 0;
    memcpy(big + o, "GET /", 5);
    o += 5;
    for (unsigned i = 0; i < PROTOCORE_NTRIP_MOUNT_MAX + 8; i++)
    {
        big[o++] = 'A';
    }
    memcpy(big + o, " HTTP/1.1\r\n\r\n", 13);
    o += 13;
    TEST_ASSERT_TRUE(protocore_ntrip_request_parse(big, o, &r));

    TEST_ASSERT_TRUE(strlen(r.mountpoint) < PROTOCORE_NTRIP_MOUNT_MAX);
}

void test_stream_response_forms(void)
{
    char out[256];
    size_t n = protocore_ntrip_build_stream_response(out, sizeof(out), NTRIP_V1);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_STRING("ICY 200 OK\r\n\r\n", out);

    n = protocore_ntrip_build_stream_response(out, sizeof(out), NTRIP_V2);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_MEMORY("HTTP/1.1 200 OK\r\n", out, 17);
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: gnss/data\r\n"));
    TEST_ASSERT_EQUAL_MEMORY("\r\n\r\n", out + n - 4, 4);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_stream_response(out, 4, NTRIP_V1));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_stream_response(out, 8, NTRIP_V2));
}

void test_error_and_unauthorized_responses(void)
{
    char out[256];
    size_t n = protocore_ntrip_build_error_response(out, sizeof(out), NTRIP_V2);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_MEMORY("HTTP/1.1 404", out, 12);
    n = protocore_ntrip_build_error_response(out, sizeof(out), NTRIP_V1);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_MEMORY("ERROR", out, 5);

    n = protocore_ntrip_build_unauthorized_response(out, sizeof(out), NTRIP_V2);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_MEMORY("HTTP/1.1 401", out, 12);
    TEST_ASSERT_NOT_NULL(strstr(out, "WWW-Authenticate: Basic"));
    n = protocore_ntrip_build_unauthorized_response(out, sizeof(out), NTRIP_V1);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_MEMORY("ERROR", out, 5);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_error_response(out, 4, NTRIP_V2));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_unauthorized_response(out, 4, NTRIP_V2));
}

static unsigned fields_in(const char *rec)
{
    unsigned n = 1;
    for (const char *p = rec; *p; p++)
    {
        if (*p == ';')
        {
            n++;
        }
    }
    return n;
}

static void field_at(const char *rec, unsigned idx, char *out, size_t cap)
{
    unsigned n = 0;
    const char *start = rec;
    for (const char *p = rec;; p++)
    {
        if (*p == ';' || *p == '\0')
        {
            if (n == idx)
            {
                size_t len = (size_t)(p - start);
                if (len >= cap)
                {
                    len = cap - 1;
                }
                memcpy(out, start, len);
                out[len] = '\0';
                return;
            }
            if (*p == '\0')
            {
                break;
            }
            n++;
            start = p + 1;
        }
    }
    out[0] = '\0';
}

void test_str_record_field_positions(void)
{
    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "BASE1";
    m.identifier = "Lab roof";
    m.format_details = "1005(1),1006(10)";
    m.nav_system = "GPS+GLO";
    m.country = "USA";
    m.generator = "ProtoCore";
    m.lat_deg = 44.07;
    m.lon_deg = -121.31;
    m.nmea_required = PROTO_FALSE;

    char rec[256];
    size_t n = protocore_ntrip_build_str_record(rec, sizeof(rec), &m);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(rec), (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT32(19, fields_in(rec));

    char f[64];
    field_at(rec, 0, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("STR", f);
    field_at(rec, 1, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("BASE1", f);
    field_at(rec, 2, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("Lab roof", f);
    field_at(rec, 4, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("1005(1),1006(10)", f);
    field_at(rec, 6, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("GPS+GLO", f);
    field_at(rec, 8, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("USA", f);
    field_at(rec, 9, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("44.07", f);
    field_at(rec, 10, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("-121.31", f);
    field_at(rec, 11, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("0", f);
    field_at(rec, 13, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("ProtoCore", f);

    m.nmea_required = PROTO_TRUE;
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)protocore_ntrip_build_str_record(rec, sizeof(rec), &m));
    field_at(rec, 11, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("1", f);

    m.mountpoint = NULL;
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_str_record(rec, sizeof(rec), &m));
    m.mountpoint = "BASE1";
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_str_record(rec, 8, &m));
}

void test_str_record_defaults(void)
{
    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "M";
    char rec[256];
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)protocore_ntrip_build_str_record(rec, sizeof(rec), &m));
    TEST_ASSERT_EQUAL_UINT32(19, fields_in(rec));

    char f[64];
    field_at(rec, 4, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("1005(1)", f);
    field_at(rec, 6, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("GPS", f);
    field_at(rec, 13, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("PC", f);
    field_at(rec, 9, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING("0.00", f);
}

static long content_length_of(const char *s)
{
    const char *p = strstr(s, "Content-Length: ");
    if (!p)
    {
        return -1;
    }
    p += 16;
    long v = 0;
    while (*p >= '0' && *p <= '9')
    {
        v = v * 10 + (*p - '0');
        p++;
    }
    return v;
}

void test_sourcetable_body_is_self_consistent(void)
{
    NtripMount mounts[3];
    memset(mounts, 0, sizeof(mounts));
    mounts[0].mountpoint = "BASE1";
    mounts[0].identifier = "Roof";
    mounts[0].lat_deg = 44.07;
    mounts[0].lon_deg = -121.31;
    mounts[1].mountpoint = "BASE2";
    mounts[1].country = "DEU";
    mounts[1].lat_deg = 52.52;
    mounts[1].lon_deg = 13.40;
    mounts[2].mountpoint = "BASE3";
    mounts[2].nmea_required = PROTO_TRUE;

    char out[2048];
    size_t n = protocore_ntrip_build_sourcetable(out, sizeof(out), NTRIP_V2, mounts, 3);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(out), (uint32_t)n);
    TEST_ASSERT_EQUAL_MEMORY("HTTP/1.1 200 OK\r\n", out, 17);
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Type: gnss/sourcetable\r\n"));

    const char *body = strstr(out, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(body);
    body += 4;
    const long declared = content_length_of(out);
    TEST_ASSERT_TRUE(declared > 0);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)declared, (uint32_t)strlen(body));

    unsigned records = 0;
    for (const char *p = body; (p = strstr(p, "STR;")) != NULL; p += 4)
    {
        records++;
    }
    TEST_ASSERT_EQUAL_UINT32(3, records);
    TEST_ASSERT_NOT_NULL(strstr(body, "STR;BASE1;"));
    TEST_ASSERT_NOT_NULL(strstr(body, "STR;BASE2;"));
    TEST_ASSERT_NOT_NULL(strstr(body, "STR;BASE3;"));
    TEST_ASSERT_TRUE(strstr(body, "STR;BASE1;") < strstr(body, "STR;BASE2;"));
    TEST_ASSERT_NOT_NULL(strstr(body, "ENDSOURCETABLE\r\n"));
    TEST_ASSERT_EQUAL_MEMORY("ENDSOURCETABLE\r\n", body + strlen(body) - 16, 16);

    n = protocore_ntrip_build_sourcetable(out, sizeof(out), NTRIP_V1, mounts, 3);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_MEMORY("SOURCETABLE 200 OK\r\n", out, 20);
    TEST_ASSERT_NOT_NULL(strstr(out, "ENDSOURCETABLE\r\n"));

    n = protocore_ntrip_build_sourcetable(out, sizeof(out), NTRIP_V2, NULL, 0);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    body = strstr(out, "\r\n\r\n") + 4;
    TEST_ASSERT_EQUAL_STRING("ENDSOURCETABLE\r\n", body);
    TEST_ASSERT_EQUAL_UINT32(16, (uint32_t)content_length_of(out));

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_sourcetable(out, 64, NTRIP_V2, mounts, 3));
}
