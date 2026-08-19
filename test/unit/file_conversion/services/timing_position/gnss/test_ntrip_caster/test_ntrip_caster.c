// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NTRIP caster codec (services/timing_position/gnss/ntrip_caster.h).
//
// Documents obtained for this suite:
//   [N1] RTCM "Networked Transport of RTCM via Internet Protocol (Ntrip) Version 1.0", the freely
//        published copy (sec 3.5, 4, 5.1, 5.2, 6, Table 1, Appendix A). It prints the source-table
//        header block, the ENDSOURCETABLE terminator, the ";" field delimiter, the 19-field STR
//        record table, and a complete "SOURCETABLE 200 OK" example.
//   [N2] software.rtcm-ntrip.org/wiki/STR, the same 19-field STR table with each field's permitted
//        content, latitude and longitude "decimal number, two digits after dot", nmea "1 or 0".
//   RFC 9112 (sec 2.1, 2.2, 3, 4), RFC 9110 (sec 5.1, 8.6, 15.5.2, 15.5.5), RFC 7617 sec 2.
//   [N1] sec 3.5: "With respect to the message format and status code, the NtripClient-NtripCaster
//        communication is fully compatible to HTTP 1.1", which is what makes the HTTP RFCs apply.
//
// Not obtained: RTCM 10410.1 (Ntrip Version 2.0) is members-only, and [N1]'s Figures 2-4 (the
// request and reply message forms) are redacted in the free copy - each is replaced by "(See
// official RTCM documentation available from http://www.rtcm.org/orderinfo.php for further
// details.)". So no document here fixes the "ICY 200 OK" line, the "Ntrip-Version: Ntrip/2.0"
// header spelling, or the gnss/data and gnss/sourcetable media types. Every case touching those
// asserts properties instead: that the two versions produce different responses, that a stream
// response is not a source-table response, that each response is a complete message, and that the
// buffer bounds are refused. Their exact octets are deliberately not asserted.
//
// test_ntrip_v1_sourcetable_server_field FAILS. [N1] sec 6 fixes the source-table's Server field as
// "Server: <NtripCasterIdentifier>/<NtripVersion>", spelling out that the text after the slash is
// "an Ntrip version number (e.g. NtripV1.0 or 1.0) in order to allow an NtripClient to understand
// which version of Ntrip is supported by a particular NtripCaster"; Appendix A prints
// "Server : NTRIP Caster 1.5.5/1.0". ntrip_caster.c emits "Server: PC", with no slash and no
// version, so a conforming client cannot learn the caster's version.
//
// The load-bearing cases are test_str_record_field_positions and test_sourcetable_frames_its_body:
// both rest on [N1] sec 6 / Table 1 and [N2], which publish the field order, the delimiter, the
// Content-Length meaning and the terminator.

#include "services/timing_position/gnss/ntrip_caster/ntrip_caster.h"
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

// RFC 9112 sec 2.1: "HTTP-message = start-line CRLF *( field-line CRLF ) CRLF [ message-body ]" -
// an empty line, and nothing earlier, ends the header section. So every prefix short of that line
// is an incomplete request and the parser must ask for more bytes rather than act on a half-read
// request line.
//
// RFC 9112 sec 3: "request-line = method SP request-target SP HTTP-version".
// RFC 9112 sec 2.2: "a recipient MAY recognize a single LF as a line terminator and ignore any
// preceding CR", which is why the bare-LF spelling parses to the same request.
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
    TEST_ASSERT_EQUAL_STRING("BASE1", r.mountpoint);
    TEST_ASSERT_FALSE(r.want_sourcetable);

    TEST_ASSERT_TRUE(req("GET /BASE1 HTTP/1.0\nUser-Agent: NTRIP rover\n\n", &r));
    TEST_ASSERT_TRUE(r.is_get);
    TEST_ASSERT_EQUAL_STRING("BASE1", r.mountpoint);
}

// RFC 7617 sec 2 builds the credentials by concatenating "the user-id, a single colon (':')
// character, and the password" and Base64-encoding the result, then prints the worked example:
//
//   If the user agent wishes to send the user-id "Aladdin" and password "open sesame", it would
//   use the following header field:
//      Authorization: Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==
//
// [N1] sec 5.2 names the same pair for an NTRIP client: the password "is coded like the HTTP Basic
// Authentication Scheme", "user-ID and password, separated by a single colon (':') character and
// within a 'base64' encoded string".
//
// Its length is arithmetic on that definition: "Aladdin:open sesame" is 7 + 1 + 11 = 19 octets, and
// Base64 (RFC 4648 sec 4) emits 4 characters per 3 octets with the last group padded, so
// 19 = 6*3 + 1 gives 6 full groups (24 characters) plus one padded group (4) = 28, ending "==".
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

    // No Authorization line is no credentials, not empty ones: an empty span would authenticate as
    // the user-id "" with password "".
    TEST_ASSERT_TRUE(req("GET /BASE1 HTTP/1.1\r\n\r\n", &r));
    TEST_ASSERT_NULL(r.auth_b64);
    TEST_ASSERT_EQUAL_UINT16(0, r.auth_b64_len);
}

// RFC 9110 sec 5.1: "Field names are case-insensitive." The credentials and the version must be
// found however the rover spelled the field names.
void test_rfc9110_header_names_are_case_insensitive(void)
{
    NtripRequest r;
    TEST_ASSERT_TRUE(req("GET /M1 HTTP/1.1\r\nntrip-version: Ntrip/2.0\r\n"
                         "AUTHORIZATION: Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==\r\n\r\n",
                         &r));
    TEST_ASSERT_EQUAL_INT(NTRIP_V2, (int)r.version);
    TEST_ASSERT_EQUAL_MEMORY("QWxhZGRpbjpvcGVuIHNlc2FtZQ==", r.auth_b64, 28);
}

// RTCM 10410.1 (Ntrip 2.0), which fixes the version header's spelling, could not be obtained, so
// only the discrimination is asserted: a request carrying the version header and one without it
// must not resolve to the same revision, since the two get different reply forms. A request with
// no version header is the legacy revision by construction, that being the revision that had none.
void test_the_version_header_discriminates_the_two_revisions(void)
{
    NtripRequest v1;
    NtripRequest v2;
    TEST_ASSERT_TRUE(req("GET /BASE1 HTTP/1.0\r\nUser-Agent: NTRIP r\r\n\r\n", &v1));
    TEST_ASSERT_TRUE(req("GET /BASE1 HTTP/1.1\r\nNtrip-Version: Ntrip/2.0\r\n\r\n", &v2));

    TEST_ASSERT_EQUAL_INT(NTRIP_V1, (int)v1.version);
    TEST_ASSERT_TRUE(v1.version != v2.version);
}

// [N1] sec 6: "Note that to request a source-table from the NtripCaster, the NtripClient uses the
// client message (see Fig. 3) while leaving out the mountpoint parameter." A request-target of "/"
// carries no mountpoint, so it is the source-table request; any other target names a stream.
void test_ntrip_a_request_without_a_mountpoint_asks_for_the_source_table(void)
{
    NtripRequest r;
    TEST_ASSERT_TRUE(req("GET / HTTP/1.1\r\n\r\n", &r));
    TEST_ASSERT_TRUE(r.is_get);
    TEST_ASSERT_TRUE(r.want_sourcetable);
    TEST_ASSERT_EQUAL_STRING("", r.mountpoint);

    TEST_ASSERT_TRUE(req("GET /BASE1 HTTP/1.1\r\n\r\n", &r));
    TEST_ASSERT_FALSE(r.want_sourcetable);
    TEST_ASSERT_EQUAL_STRING("BASE1", r.mountpoint);
}

// [N1] sec 4: "The NtripServer-NtripCaster communication extends HTTP by the additional message
// format 'SOURCE'". A SOURCE message is a base pushing a stream in, not a rover pulling one out, so
// it is not a GET and must not be answered with a stream. RFC 9112 sec 3.1 makes the method
// case-sensitive and a token, so POST is likewise not a GET.
//
// [N2] field 2 caps a mountpoint at 100 characters; this codec's own bound is
// PROTOCORE_NTRIP_MOUNT_MAX. Either way an over-long target must land inside the fixed buffer,
// NUL-terminated, rather than running past it.
void test_non_get_requests_and_the_mountpoint_bound(void)
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

// Copy the value of header @p name out of a response head, or "" if absent.
static void header_value_of(const char *resp, const char *name, char *out, size_t cap)
{
    out[0] = '\0';
    const size_t nl = strlen(name);
    for (const char *p = strstr(resp, "\r\n"); p; p = strstr(p, "\r\n"))
    {
        p += 2;
        if (strncmp(p, name, nl) != 0 || p[nl] != ':')
        {
            continue;
        }
        const char *v = p + nl + 1;
        while (*v == ' ')
        {
            v++;
        }
        const char *e = strstr(v, "\r\n");
        size_t len = e ? (size_t)(e - v) : strlen(v);
        if (len >= cap)
        {
            len = cap - 1;
        }
        memcpy(out, v, len);
        out[len] = '\0';
        return;
    }
}

// The reply forms are in RTCM 10410.1 and in [N1]'s redacted figures, so no octet of either status
// line is asserted. What is asserted holds whatever they say:
//   - the two revisions do not share one reply, or a rover that announced its revision gets an
//     answer meant for the other one,
//   - each reply is a complete message, which RFC 9112 sec 2.1 ends with an empty line, so the
//     rover knows where the RTCM stream starts,
//   - a buffer that cannot hold the reply yields 0, never a truncated head.
void test_stream_response_is_one_complete_message_per_revision(void)
{
    char v1[256];
    char v2[256];

    size_t n1 = protocore_ntrip_build_stream_response(v1, sizeof(v1), NTRIP_V1);
    size_t n2 = protocore_ntrip_build_stream_response(v2, sizeof(v2), NTRIP_V2);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n1);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n2);

    TEST_ASSERT_NOT_EQUAL(0, strcmp(v1, v2));
    TEST_ASSERT_EQUAL_MEMORY("\r\n\r\n", v1 + n1 - 4, 4);
    TEST_ASSERT_EQUAL_MEMORY("\r\n\r\n", v2 + n2 - 4, 4);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(v1), (uint32_t)n1);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(v2), (uint32_t)n2);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_stream_response(v1, 4, NTRIP_V1));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_stream_response(v2, 8, NTRIP_V2));
}

// A rover must be able to tell a stream from a source-table without parsing the body, so the two
// V2 replies cannot carry the same media type. Which media types they are is RTCM 10410.1's, not
// assertable here.
void test_a_stream_response_is_not_a_sourcetable_response(void)
{
    char stream[256];
    char table[512];
    char a[64];
    char b[64];
    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "BASE1";

    TEST_ASSERT_GREATER_THAN_UINT32(0,
                                    (uint32_t)protocore_ntrip_build_stream_response(stream, sizeof(stream), NTRIP_V2));
    TEST_ASSERT_GREATER_THAN_UINT32(0,
                                    (uint32_t)protocore_ntrip_build_sourcetable(table, sizeof(table), NTRIP_V2, &m, 1));

    header_value_of(stream, "Content-Type", a, sizeof(a));
    header_value_of(table, "Content-Type", b, sizeof(b));
    TEST_ASSERT_TRUE(a[0] != '\0');
    TEST_ASSERT_TRUE(b[0] != '\0');
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a, b));
}

// The V2 replies are HTTP, so RFC 9112 sec 4 fixes their first line as
// "status-line = HTTP-version SP status-code SP [ reason-phrase ]" with "status-code = 3DIGIT",
// and the codes are RFC 9110's:
//   sec 15.5.5, 404: "the origin server did not find a current representation for the target
//                     resource", which is an unknown mountpoint.
//   sec 15.5.2, 401: "The server generating a 401 response MUST send a WWW-Authenticate header
//                     field (Section 11.6.1) containing at least one challenge" - so the challenge
//                     is not optional, and RFC 7617 sec 2 names the scheme "Basic", which [N1]
//                     sec 5.2 is the scheme NTRIP uses.
// The V1 error lines are in the redacted figures; only their distinctness is asserted.
void test_rfc9110_error_and_unauthorized_responses(void)
{
    char out[256];
    char v1_err[256];
    char v1_401[256];

    size_t n = protocore_ntrip_build_error_response(out, sizeof(out), NTRIP_V2);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_MEMORY("HTTP/1.1 404 ", out, 13);

    n = protocore_ntrip_build_unauthorized_response(out, sizeof(out), NTRIP_V2);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_MEMORY("HTTP/1.1 401 ", out, 13);
    TEST_ASSERT_NOT_NULL(strstr(out, "\r\nWWW-Authenticate: Basic"));

    TEST_ASSERT_GREATER_THAN_UINT32(0,
                                    (uint32_t)protocore_ntrip_build_error_response(v1_err, sizeof(v1_err), NTRIP_V1));
    TEST_ASSERT_GREATER_THAN_UINT32(
        0, (uint32_t)protocore_ntrip_build_unauthorized_response(v1_401, sizeof(v1_401), NTRIP_V1));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(v1_err, v1_401));

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_error_response(out, 4, NTRIP_V2));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_unauthorized_response(out, 4, NTRIP_V2));
}

// [N1] sec 6: "All data fields in the source-table records are separated using the semicolon
// character ';' as a field delimiter."
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

// Copy field @p idx (0-based; [N1] Table 1 and [N2] number the same fields from 1) into @p out.
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

static void assert_field(const char *rec, unsigned idx, const char *want)
{
    char f[64];
    field_at(rec, idx, f, sizeof(f));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want, f, rec);
}

static proto_bool field_is_one_of(const char *rec, unsigned idx, const char *const *allowed, unsigned count)
{
    char f[64];
    field_at(rec, idx, f, sizeof(f));
    for (unsigned i = 0; i < count; i++)
    {
        if (strcmp(f, allowed[i]) == 0)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

// [N1] Table 1 and [N2] publish the STR record as 19 numbered fields in this order (1-based):
//    1 type=STR      2 mountpoint    3 identifier   4 format        5 format-details
//    6 carrier       7 nav-system    8 network      9 country      10 latitude
//   11 longitude    12 nmea         13 solution    14 generator    15 compression
//   16 authentication  17 fee       18 bitrate     19 misc
// so the 0-based positions asserted here are one less. A record whose fields sit anywhere else is
// read wrong by every client, since the format carries no names.
//
// [N2] also fixes the permitted content of the fields this codec fills in on its own:
//   carrier   Number, [N1] Table 1: 0 = no carrier phase, 1 = L1, 2 = L1&L2
//   nmea      "Content: 1 or 0"
//   solution  "Content: 1 or 0" (0 = single base, 1 = network)
//   auth      "Content: B, D, N or a comma separated list of these"
//   fee       "Content: Y or N"
// Those are asserted as the published sets, not as this codec's particular choice inside them.
void test_str_record_field_positions(void)
{
    static const char *const BIT01[] = {"0", "1"};
    static const char *const CARRIER[] = {"0", "1", "2"};
    static const char *const AUTH[] = {"N", "B", "D"};
    static const char *const FEE[] = {"Y", "N"};

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

    assert_field(rec, 0, "STR");              // field 1, "Content: STR"
    assert_field(rec, 1, "BASE1");            // field 2, mountpoint
    assert_field(rec, 2, "Lab roof");         // field 3, identifier
    assert_field(rec, 4, "1005(1),1006(10)"); // field 5, format-details
    assert_field(rec, 6, "GPS+GLO");          // field 7, nav-system
    assert_field(rec, 8, "USA");              // field 9, ISO 3166 country code, 3 characters
    assert_field(rec, 13, "ProtoCore");       // field 14, generator

    TEST_ASSERT_TRUE(field_is_one_of(rec, 5, CARRIER, 3)); // field 6, carrier
    TEST_ASSERT_TRUE(field_is_one_of(rec, 11, BIT01, 2));  // field 12, nmea
    TEST_ASSERT_TRUE(field_is_one_of(rec, 12, BIT01, 2));  // field 13, solution
    TEST_ASSERT_TRUE(field_is_one_of(rec, 15, AUTH, 3));   // field 16, authentication
    TEST_ASSERT_TRUE(field_is_one_of(rec, 16, FEE, 2));    // field 17, fee

    // field 12 nmea: "Caster requires NMEA input (1) or not (0)", so the flag is the digit.
    assert_field(rec, 11, "0");
    m.nmea_required = PROTO_TRUE;
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)protocore_ntrip_build_str_record(rec, sizeof(rec), &m));
    assert_field(rec, 11, "1");

    // A record with no mountpoint names no stream, and a record that does not fit is not emitted
    // half-written: a truncated record shifts every field after the cut.
    m.mountpoint = NULL;
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_str_record(rec, sizeof(rec), &m));
    m.mountpoint = "BASE1";
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_str_record(rec, 8, &m));
}

// [N1] Table 1 fields 10 and 11 and [N2]: latitude and longitude are a "decimal number, two digits
// after dot" ("Floating point number, two digits after decimal point"). Two digits exactly, so a
// third decimal rounds into the second and a value with fewer is padded out.
//   50.12 and 8.68 are the coordinates [N1] Appendix A prints for STR;FFMJ2;Frankfurt.
//   -33.92 is the latitude it prints for STR;SYDN0;Sydney, a southern base.
//   44.069 rounds to 44.07; 8.6 pads to 8.60.
void test_str_record_latitude_and_longitude_carry_two_decimals(void)
{
    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "M";
    char rec[256];

    m.lat_deg = 50.12;
    m.lon_deg = 8.68;
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)protocore_ntrip_build_str_record(rec, sizeof(rec), &m));
    assert_field(rec, 9, "50.12");
    assert_field(rec, 10, "8.68");

    m.lat_deg = -33.92;
    m.lon_deg = 151.23;
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)protocore_ntrip_build_str_record(rec, sizeof(rec), &m));
    assert_field(rec, 9, "-33.92");
    assert_field(rec, 10, "151.23");

    m.lat_deg = 44.069;
    m.lon_deg = 8.6;
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)protocore_ntrip_build_str_record(rec, sizeof(rec), &m));
    assert_field(rec, 9, "44.07");
    assert_field(rec, 10, "8.60");
}

// A mount described by nothing but its mountpoint still has to produce a record every client can
// index, so the field count and the positions after the gaps are what is asserted.
//
// No document publishes a default for field 5 (format-details) or field 7 (nav-system): [N1]
// Table 1 and [N2] give their meaning and content type and stop there. Their substituted text is
// therefore not asserted - only that the fields are filled, since an empty field 7 advertises a
// stream for no navigation system at all. ntrip_caster.h line 93 does publish the generator
// default ("null -> 'PC'"), so that one is asserted, and field 10's "0.00" is the two-decimal form
// [N1] Table 1 requires of a latitude of 0.
//
// Field 9 (country) stays empty: it is an "ISO 3166 country code" of exactly 3 characters, and no
// three characters can be invented for a caster that did not say where it is.
void test_str_record_unset_fields_keep_the_layout(void)
{
    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "M";

    char rec[256];
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)protocore_ntrip_build_str_record(rec, sizeof(rec), &m));
    TEST_ASSERT_EQUAL_UINT32(19, fields_in(rec));

    assert_field(rec, 0, "STR");
    assert_field(rec, 1, "M");
    assert_field(rec, 8, "");
    assert_field(rec, 9, "0.00");
    assert_field(rec, 13, "PC");

    char f[64];
    field_at(rec, 4, f, sizeof(f));
    TEST_ASSERT_TRUE_MESSAGE(f[0] != '\0', "format-details");
    field_at(rec, 6, f, sizeof(f));
    TEST_ASSERT_TRUE_MESSAGE(f[0] != '\0', "nav-system");
}

// Read the "Content-Length" value, or -1 if it is absent. RFC 9110 sec 8.6: "Content-Length =
// 1*DIGIT", "a decimal non-negative integer number of octets".
static long content_length_of(const char *s)
{
    char v[32];
    header_value_of(s, "Content-Length", v, sizeof(v));
    if (v[0] < '0' || v[0] > '9')
    {
        return -1;
    }
    long n = 0;
    for (const char *p = v; *p >= '0' && *p <= '9'; p++)
    {
        n = n * 10 + (*p - '0');
    }
    return n;
}

// [N1] sec 6 fixes the whole source-table response. The header block is
//
//   Server: <NtripCasterIdentifier>/<NtripVersion><CR><LF>
//   Content-Type: text/plain<CR><LF>
//   Content-Length: <Content-Length><CR><LF>
//   <CR><LF>
//
// "followed by the actual source-table records", where "The content-length indicates the size of
// the source-table records (decimal number of octets, e.g. 'Content-Length: 243')" and "The end of
// the source-table is notified by the string: ENDSOURCETABLE". Appendix A prints the V1 status line
// "SOURCETABLE 200 OK" over exactly that block.
//
// So for the V1 response: the status line, the media type, a Content-Length equal to the octets
// that follow the blank line, one ";"-delimited STR record per mountpoint in the order given, and
// the terminator last. The V2 header block is RTCM 10410.1's, so only the framing is asserted
// there - RFC 9112 sec 4's status line and the same Content-Length arithmetic.
void test_ntrip_sourcetable_frames_its_body(void)
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
    char ctype[64];

    size_t n = protocore_ntrip_build_sourcetable(out, sizeof(out), NTRIP_V1, mounts, 3);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(out), (uint32_t)n);
    TEST_ASSERT_EQUAL_MEMORY("SOURCETABLE 200 OK\r\n", out, 20);
    header_value_of(out, "Content-Type", ctype, sizeof(ctype));
    TEST_ASSERT_EQUAL_STRING("text/plain", ctype);

    const char *body = strstr(out, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(body);
    body += 4;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(body), (uint32_t)content_length_of(out));

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
    TEST_ASSERT_TRUE(strstr(body, "STR;BASE2;") < strstr(body, "STR;BASE3;"));
    TEST_ASSERT_EQUAL_MEMORY("ENDSOURCETABLE\r\n", body + strlen(body) - 16, 16);

    n = protocore_ntrip_build_sourcetable(out, sizeof(out), NTRIP_V2, mounts, 3);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_MEMORY("HTTP/1.1 200 ", out, 13);
    body = strstr(out, "\r\n\r\n") + 4;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(body), (uint32_t)content_length_of(out));
    TEST_ASSERT_EQUAL_MEMORY("ENDSOURCETABLE\r\n", body + strlen(body) - 16, 16);
}

// [N1] sec 6: the Server field of a source-table response is
// "Server: <NtripCasterIdentifier>/<NtripVersion>", and the section spells out both halves - before
// the slash "an NtripCaster identifier to be defined by the NtripCaster operator", after it "an
// Ntrip version number (e.g. NtripV1.0 or 1.0) in order to allow an NtripClient to understand which
// version of Ntrip is supported by a particular NtripCaster". Appendix A prints
// "Server : NTRIP Caster 1.5.5/1.0".
//
// This case FAILS: ntrip_caster.c emits "Server: PC", which carries no slash and no version, so a
// client reading it cannot learn the caster's revision. Only the separator and a non-empty version
// are asserted, since the identifier before it is the operator's to choose.
void test_ntrip_v1_sourcetable_server_field(void)
{
    NtripMount m;
    memset(&m, 0, sizeof(m));
    m.mountpoint = "BASE1";

    char out[1024];
    char server[64];
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)protocore_ntrip_build_sourcetable(out, sizeof(out), NTRIP_V1, &m, 1));
    header_value_of(out, "Server", server, sizeof(server));

    const char *slash = strchr(server, '/');
    TEST_ASSERT_NOT_NULL_MESSAGE(slash, server);
    TEST_ASSERT_TRUE_MESSAGE(slash > server, server);
    TEST_ASSERT_TRUE_MESSAGE(slash[1] != '\0', server);
}

// An empty caster still answers with a well-formed source-table: [N1] sec 6 ends every one with
// ENDSOURCETABLE, so the body is that string and nothing else. Its length is arithmetic:
// "ENDSOURCETABLE" is 14 characters plus the CRLF that closes the line = 16 octets.
//
// A buffer too small for the whole table yields 0: half a source-table ends mid-record, and a
// client reading it stores a stream description that was never advertised.
void test_ntrip_sourcetable_empty_and_overflow(void)
{
    NtripMount mounts[3];
    memset(mounts, 0, sizeof(mounts));
    mounts[0].mountpoint = "BASE1";
    mounts[1].mountpoint = "BASE2";
    mounts[2].mountpoint = "BASE3";

    char out[2048];
    size_t n = protocore_ntrip_build_sourcetable(out, sizeof(out), NTRIP_V2, NULL, 0);
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    const char *body = strstr(out, "\r\n\r\n") + 4;
    TEST_ASSERT_EQUAL_STRING("ENDSOURCETABLE\r\n", body);
    TEST_ASSERT_EQUAL_UINT32(16, (uint32_t)content_length_of(out));

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_ntrip_build_sourcetable(out, 64, NTRIP_V2, mounts, 3));
}
