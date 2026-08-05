// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the NMEA 0183 codec (services/timing_position/nmea0183): the XOR checksum, sentence build,
// parse (field splitting, talker/type, checksum validation), and the field-value helpers. The
// canonical GGA example (checksum 0x47) is used as an independent known-answer vector. Pure
// host tests.

#include "services/timing_position/nmea0183/nmea0183.h"
#include <stdio.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// The standard GGA example; its documented checksum is 0x47.
static const char *GGA = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
static const char *GGA_BODY = "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";

void test_checksum_known_vector()
{
    TEST_ASSERT_EQUAL_HEX8(0x47, pc_nmea0183_checksum(GGA_BODY, strlen(GGA_BODY)));
}

void test_build()
{
    char buf[96];
    size_t n = pc_nmea0183_build(buf, sizeof(buf), GGA_BODY);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_CHAR('$', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('\n', buf[n - 1]);
    // The built sentence must carry the correct *47 checksum and CRLF.
    TEST_ASSERT_TRUE(strstr(buf, "*47\r\n") != NULL);
}

void test_parse_gga()
{
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(GGA, strlen(GGA), &m));
    TEST_ASSERT_EQUAL_STRING("GP", m.talker);
    TEST_ASSERT_EQUAL_STRING("GGA", m.type);
    // 15 fields: address + 14 data fields (two trailing empty).
    TEST_ASSERT_EQUAL_UINT8(15, m.field_count);
    TEST_ASSERT_EQUAL_UINT8(8, m.field_len[2]); // "4807.038"
    TEST_ASSERT_EQUAL_MEMORY("4807.038", m.fields[2], 8);
    TEST_ASSERT_EQUAL_MEMORY("N", m.fields[3], 1);
    TEST_ASSERT_EQUAL_UINT8(0, m.field_len[13]); // trailing empty field
}

void test_field_helpers()
{
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(GGA, strlen(GGA), &m));
    float lat = 0;
    TEST_ASSERT_TRUE(pc_nmea0183_field_float(&m, 2, &lat));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4807.038f, lat);
    long sats = 0;
    TEST_ASSERT_TRUE(pc_nmea0183_field_int(&m, 7, &sats)); // "08"
    TEST_ASSERT_EQUAL_INT(8, sats);
    // an empty field yields no value.
    float none;
    TEST_ASSERT_FALSE(pc_nmea0183_field_float(&m, 13, &none));
    // a non-numeric field (the "N" hemisphere) yields no value.
    TEST_ASSERT_FALSE(pc_nmea0183_field_int(&m, 3, &sats));
}

void test_parse_rejects_bad_checksum()
{
    // Flip the checksum digits.
    char bad[96];
    strcpy(bad, GGA);
    char *star = strchr(bad, '*');
    star[1] = '0';
    star[2] = '0';
    Nmea0183 m;
    TEST_ASSERT_FALSE(pc_nmea0183_parse(bad, strlen(bad), &m));
}

void test_parse_rejects_no_dollar()
{
    Nmea0183 m;
    TEST_ASSERT_FALSE(pc_nmea0183_parse("GPGGA,1,2*00\r\n", 14, &m));
}

// Round-trip: build from a body, then parse the result back.
void test_build_then_parse()
{
    char buf[96];
    size_t n = pc_nmea0183_build(buf, sizeof(buf), "GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W");
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    TEST_ASSERT_EQUAL_STRING("GP", m.talker);
    TEST_ASSERT_EQUAL_STRING("RMC", m.type);
    TEST_ASSERT_EQUAL_MEMORY("A", m.fields[2], 1); // status = active
}

// Builder guards, the lowercase / invalid checksum-hex paths, a no-checksum sentence,
// and the field-helper reject branches.
void test_nmea0183_error_paths()
{
    char buf[96];
    TEST_ASSERT_EQUAL_size_t(0, pc_nmea0183_build(NULL, sizeof(buf), "GPGGA")); // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_nmea0183_build(buf, sizeof(buf), NULL));     // null body
    TEST_ASSERT_EQUAL_size_t(0, pc_nmea0183_build(buf, 4, "GPGGA"));            // cap too small

    Nmea0183 m;
    // A lowercase checksum still validates (hex_val a-f). "GPGGA,1" -> checksum 0x4B -> "4b".
    const char *body = "GPGGA,1";
    uint8_t cs = pc_nmea0183_checksum(body, strlen(body));
    char lower[32];
    snprintf(lower, sizeof(lower), "$%s*%02x\r\n", body, cs); // %02x emits lowercase hex
    TEST_ASSERT_TRUE(pc_nmea0183_parse(lower, strlen(lower), &m));

    TEST_ASSERT_FALSE(pc_nmea0183_parse("$GPGGA,1*4G\r\n", 13, &m)); // 'G' is not a hex digit
    TEST_ASSERT_FALSE(pc_nmea0183_parse("$GPGGA,123\r\n", 12, &m));  // no '*' checksum introducer

    TEST_ASSERT_TRUE(pc_nmea0183_parse(GGA, strlen(GGA), &m));
    float f;
    TEST_ASSERT_FALSE(pc_nmea0183_field_float(&m, 3, &f)); // "N": non-empty but not a number
    long v;
    TEST_ASSERT_FALSE(pc_nmea0183_field_int(NULL, 0, &v)); // null message
    TEST_ASSERT_FALSE(pc_nmea0183_field_int(&m, 13, &v));  // empty field
}

// hex_val's low-end reject range (below '0') and lowercase-beyond-'f' reject range, plus the
// checksum guard's "first digit invalid" short-circuit (hi < 0, so lo is never evaluated).
void test_nmea0183_hex_val_edges()
{
    Nmea0183 m;
    TEST_ASSERT_FALSE(pc_nmea0183_parse("$GPGGA,1*.4\r\n", 13, &m)); // '.' < '0': hi invalid
    TEST_ASSERT_FALSE(pc_nmea0183_parse("$GPGGA,1*4z\r\n", 13, &m)); // 'z' > 'f': lo invalid
}

// Guard-clause branches in pc_nmea0183_parse: null s, null out, too-short len, and the '!'
// (AIS-encapsulated) leading character as an alternative to '$'.
void test_nmea0183_parse_guards()
{
    Nmea0183 m;
    TEST_ASSERT_FALSE(pc_nmea0183_parse(NULL, 10, &m));           // null s
    TEST_ASSERT_FALSE(pc_nmea0183_parse(GGA, strlen(GGA), NULL)); // null out
    TEST_ASSERT_FALSE(pc_nmea0183_parse("ab", 2, &m));            // len < 4

    char bang[96];
    strcpy(bang, GGA);
    bang[0] = '!'; // AIS-style leading char; checksum is unaffected (computed from s+1 onward).
    TEST_ASSERT_TRUE(pc_nmea0183_parse(bang, strlen(bang), &m));
    TEST_ASSERT_EQUAL_STRING("GP", m.talker);
}

// The '*'-search loop's natural (non-break) exit at i == len, a bare '\n' with no preceding '\r'
// as a stop character, and a '*' with only one trailing character (too few for two hex digits).
void test_nmea0183_parse_scan_edges()
{
    Nmea0183 m;
    TEST_ASSERT_FALSE(pc_nmea0183_parse("$GPGGA,123", 10, &m)); // no '*', no CR/LF: loop runs to len
    TEST_ASSERT_FALSE(pc_nmea0183_parse("$GPGGA,1\n", 9, &m));  // bare '\n' stops the scan
    TEST_ASSERT_FALSE(pc_nmea0183_parse("$GPGGA,1*4", 10, &m)); // '*' with only 1 trailing char
}

// The field-split loop's overflow guard (more comma-separated fields than
// PC_NMEA0183_MAX_FIELDS) and talker/type derivation when the address field is shorter than
// either fixed-width copy, so the field-length check (not the index cap) ends each inner loop.
void test_nmea0183_field_overflow_and_short_address()
{
    char body[80];
    size_t bp = 0;
    body[bp++] = 'G';
    body[bp++] = 'P';
    for (int i = 0; i < 40; i++)
    {
        body[bp++] = ',';
    }
    body[bp] = '\0';
    char buf[160];
    size_t n = pc_nmea0183_build(buf, sizeof(buf), body);
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    TEST_ASSERT_EQUAL_UINT8(PC_NMEA0183_MAX_FIELDS, m.field_count); // capped, extras dropped

    char buf2[32];
    size_t n2 = pc_nmea0183_build(buf2, sizeof(buf2), "G,1"); // 1-char address (< talker/type width)
    TEST_ASSERT_TRUE(n2 > 0);
    Nmea0183 m2;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf2, n2, &m2));
    TEST_ASSERT_EQUAL_STRING("G", m2.talker);
    TEST_ASSERT_EQUAL_STRING("", m2.type);
}

// Out-of-range idx and null out-pointer branches of the field-value helpers.
void test_nmea0183_field_helpers_more_guards()
{
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(GGA, strlen(GGA), &m));
    float f;
    TEST_ASSERT_FALSE(pc_nmea0183_field_float(NULL, 0, &f));           // null message
    TEST_ASSERT_FALSE(pc_nmea0183_field_float(&m, m.field_count, &f)); // idx out of range
    TEST_ASSERT_FALSE(pc_nmea0183_field_float(&m, 0, NULL));           // null out
    long v;
    TEST_ASSERT_FALSE(pc_nmea0183_field_int(&m, m.field_count, &v)); // idx out of range
    TEST_ASSERT_FALSE(pc_nmea0183_field_int(&m, 0, NULL));           // null out
}

// The classic textbook RMC (23 Mar 1994, 22.4 kn, course 84.4).
static const char *RMC = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n";

void test_decode_gga()
{
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(GGA, strlen(GGA), &m));
    pc_nmea_gga g;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_gga(&m, &g));
    TEST_ASSERT_EQUAL_UINT8(12, g.hour);
    TEST_ASSERT_EQUAL_UINT8(35, g.minute);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.0f, g.second);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 48.1173f, (float)g.lat_deg);   // 4807.038 N
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 11.516667f, (float)g.lon_deg); // 01131.000 E
    TEST_ASSERT_EQUAL_UINT8(1, g.fix_quality);
    TEST_ASSERT_EQUAL_UINT8(8, g.num_sats);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9f, g.hdop);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 545.4f, g.alt_m);
}

void test_decode_rmc()
{
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(RMC, strlen(RMC), &m));
    pc_nmea_rmc r;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_rmc(&m, &r));
    TEST_ASSERT_TRUE(r.valid); // status 'A'
    TEST_ASSERT_EQUAL_UINT8(12, r.hour);
    TEST_ASSERT_EQUAL_UINT8(35, r.minute);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 48.1173f, (float)r.lat_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 11.516667f, (float)r.lon_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 22.4f, r.speed_knots);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 84.4f, r.course_deg);
    TEST_ASSERT_EQUAL_UINT8(23, r.day);
    TEST_ASSERT_EQUAL_UINT8(3, r.month);
    TEST_ASSERT_EQUAL_UINT8(94, r.year);

    // A southern/western hemisphere sentence flips the coordinate signs, and a 'V' status is decoded as
    // invalid (but still parses). Build it so the checksum is correct.
    char buf[96];
    size_t bn = pc_nmea0183_build(buf, sizeof(buf), "GPRMC,000000,V,3345.678,S,15112.345,W,000.0,000.0,010100,,");
    TEST_ASSERT_TRUE(bn > 0);
    Nmea0183 m2;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, bn, &m2));
    pc_nmea_rmc r2;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_rmc(&m2, &r2));
    TEST_ASSERT_FALSE(r2.valid);
    TEST_ASSERT_TRUE(r2.lat_deg < 0.0); // S -> negative
    TEST_ASSERT_TRUE(r2.lon_deg < 0.0); // W -> negative
}

void test_decode_gsv()
{
    // Classic GSV: 3 sentences, this is #1, 11 satellites in view, 4 satellite records.
    const char *gsv = "$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74\r\n";
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(gsv, strlen(gsv), &m));
    pc_nmea_gsv g;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_gsv(&m, &g));
    TEST_ASSERT_EQUAL_UINT8(3, g.total_msgs);
    TEST_ASSERT_EQUAL_UINT8(1, g.msg_num);
    TEST_ASSERT_EQUAL_UINT8(11, g.sats_in_view);
    TEST_ASSERT_EQUAL_UINT8(4, g.sat_count);
    TEST_ASSERT_EQUAL_UINT8(3, g.sats[0].prn);
    TEST_ASSERT_EQUAL_INT16(3, g.sats[0].elev_deg);
    TEST_ASSERT_EQUAL_INT16(111, g.sats[0].azim_deg);
    TEST_ASSERT_TRUE(g.sats[0].snr_valid);
    TEST_ASSERT_EQUAL_UINT8(0, g.sats[0].snr_db);
    TEST_ASSERT_EQUAL_UINT8(13, g.sats[3].prn);
    TEST_ASSERT_EQUAL_INT16(292, g.sats[3].azim_deg);
}

void test_decode_gsv_blank_snr_and_partial()
{
    char buf[96];
    // A single-satellite GSV whose SNR field is blank (in view, not tracked).
    size_t n = pc_nmea0183_build(buf, sizeof(buf), "GPGSV,1,1,01,03,03,111,");
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    pc_nmea_gsv g;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_gsv(&m, &g));
    TEST_ASSERT_EQUAL_UINT8(1, g.sat_count);
    TEST_ASSERT_EQUAL_UINT8(3, g.sats[0].prn);
    TEST_ASSERT_FALSE(g.sats[0].snr_valid); // blank SNR

    // A last sentence carrying only three satellites decodes sat_count 3.
    n = pc_nmea0183_build(buf, sizeof(buf), "GPGSV,3,3,11,22,42,067,42,24,14,311,43,32,29,059,36");
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    TEST_ASSERT_TRUE(pc_nmea0183_parse_gsv(&m, &g));
    TEST_ASSERT_EQUAL_UINT8(3, g.sat_count);
    TEST_ASSERT_EQUAL_UINT8(32, g.sats[2].prn);
    TEST_ASSERT_TRUE(g.sats[2].snr_valid);
    TEST_ASSERT_EQUAL_UINT8(36, g.sats[2].snr_db);
}

void test_decode_zda()
{
    char buf[96];
    // Full ZDA: 20:15:30.50 UTC on 2026-07-04, local zone -05:30.
    size_t n = pc_nmea0183_build(buf, sizeof(buf), "GPZDA,201530.50,04,07,2026,-05,30");
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    pc_nmea_zda z;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_zda(&m, &z));
    TEST_ASSERT_EQUAL_UINT8(20, z.hour);
    TEST_ASSERT_EQUAL_UINT8(15, z.minute);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.50f, z.second);
    TEST_ASSERT_EQUAL_UINT8(4, z.day);
    TEST_ASSERT_EQUAL_UINT8(7, z.month);
    TEST_ASSERT_EQUAL_UINT16(2026, z.year); // full 4-digit year, unlike RMC
    TEST_ASSERT_EQUAL_INT8(-5, z.zone_hours);
    TEST_ASSERT_EQUAL_UINT8(30, z.zone_minutes);

    // A ZDA with the optional zone fields blank still decodes; the zone reads back 0.
    n = pc_nmea0183_build(buf, sizeof(buf), "GPZDA,083045.00,24,12,2025,,");
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    TEST_ASSERT_TRUE(pc_nmea0183_parse_zda(&m, &z));
    TEST_ASSERT_EQUAL_UINT16(2025, z.year);
    TEST_ASSERT_EQUAL_UINT8(24, z.day);
    TEST_ASSERT_EQUAL_INT8(0, z.zone_hours);
    TEST_ASSERT_EQUAL_UINT8(0, z.zone_minutes);

    // A GGA is not a ZDA, and null args are rejected.
    pc_nmea0183_parse(GGA, strlen(GGA), &m);
    TEST_ASSERT_FALSE(pc_nmea0183_parse_zda(&m, &z));
    TEST_ASSERT_FALSE(pc_nmea0183_parse_zda(NULL, &z));
}

void test_decode_vtg()
{
    char buf[96];
    // Course 54.7 T / 34.4 M, speed 5.5 kn / 10.2 km/h, autonomous mode.
    size_t n = pc_nmea0183_build(buf, sizeof(buf), "GPVTG,054.7,T,034.4,M,005.5,N,010.2,K,A");
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    pc_nmea_vtg v;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_vtg(&m, &v));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 54.7f, v.course_true_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 34.4f, v.course_mag_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.5f, v.speed_knots);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.2f, v.speed_kmh);
    TEST_ASSERT_EQUAL_CHAR('A', v.mode);

    // A pre-2.3 VTG without the mode field still decodes; mode reads back '\0'.
    n = pc_nmea0183_build(buf, sizeof(buf), "GPVTG,054.7,T,034.4,M,005.5,N,010.2,K");
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    TEST_ASSERT_TRUE(pc_nmea0183_parse_vtg(&m, &v));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.5f, v.speed_knots);
    TEST_ASSERT_EQUAL_CHAR('\0', v.mode);

    // A GGA is not a VTG, and null args are rejected.
    pc_nmea0183_parse(GGA, strlen(GGA), &m);
    TEST_ASSERT_FALSE(pc_nmea0183_parse_vtg(&m, &v));
    TEST_ASSERT_FALSE(pc_nmea0183_parse_vtg(NULL, &v));
}

void test_decode_gsa()
{
    char buf[96];
    // 3D auto fix on 5 satellites (blank PRN slots between them), PDOP 2.5 / HDOP 1.3 / VDOP 2.1.
    size_t n = pc_nmea0183_build(buf, sizeof(buf), "GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1");
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    pc_nmea_gsa g;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_gsa(&m, &g));
    TEST_ASSERT_EQUAL_CHAR('A', g.mode);
    TEST_ASSERT_EQUAL_UINT8(3, g.fix_type);
    TEST_ASSERT_EQUAL_UINT8(5, g.sat_count); // 04,05,09,12,24 - blanks skipped
    TEST_ASSERT_EQUAL_UINT8(4, g.sats[0]);
    TEST_ASSERT_EQUAL_UINT8(9, g.sats[2]);
    TEST_ASSERT_EQUAL_UINT8(24, g.sats[4]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, g.pdop);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.3f, g.hdop);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.1f, g.vdop);

    // A no-fix GSA (all PRN slots blank) decodes with sat_count 0.
    n = pc_nmea0183_build(buf, sizeof(buf), "GPGSA,A,1,,,,,,,,,,,,,99.9,99.9,99.9");
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    TEST_ASSERT_TRUE(pc_nmea0183_parse_gsa(&m, &g));
    TEST_ASSERT_EQUAL_UINT8(1, g.fix_type);
    TEST_ASSERT_EQUAL_UINT8(0, g.sat_count);

    // A GGA is not a GSA, and null args are rejected.
    pc_nmea0183_parse(GGA, strlen(GGA), &m);
    TEST_ASSERT_FALSE(pc_nmea0183_parse_gsa(&m, &g));
    TEST_ASSERT_FALSE(pc_nmea0183_parse_gsa(NULL, &g));
}

void test_decode_mwv()
{
    char buf[96];
    // Apparent wind at 214.8 deg relative, 10.5 knots, valid.
    size_t n = pc_nmea0183_build(buf, sizeof(buf), "WIMWV,214.8,R,10.5,N,A");
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    pc_nmea_mwv w;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_mwv(&m, &w));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 214.8f, w.wind_angle_deg);
    TEST_ASSERT_EQUAL_CHAR('R', w.reference);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.5f, w.wind_speed);
    TEST_ASSERT_EQUAL_CHAR('N', w.speed_units);
    TEST_ASSERT_TRUE(w.valid);

    // A true-wind, m/s, invalid-status variant decodes the other field values.
    n = pc_nmea0183_build(buf, sizeof(buf), "WIMWV,45.0,T,3.2,M,V");
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    TEST_ASSERT_TRUE(pc_nmea0183_parse_mwv(&m, &w));
    TEST_ASSERT_EQUAL_CHAR('T', w.reference);
    TEST_ASSERT_EQUAL_CHAR('M', w.speed_units);
    TEST_ASSERT_FALSE(w.valid);

    // A GGA is not an MWV, and null args are rejected.
    pc_nmea0183_parse(GGA, strlen(GGA), &m);
    TEST_ASSERT_FALSE(pc_nmea0183_parse_mwv(&m, &w));
    TEST_ASSERT_FALSE(pc_nmea0183_parse_mwv(NULL, &w));
}

void test_decode_dpt()
{
    char buf[96];
    // Depth 2.4 m relative to the transducer, offset +0.5 m (to waterline), range scale 200 m.
    size_t n = pc_nmea0183_build(buf, sizeof(buf), "SDDPT,2.4,0.5,200.0");
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    pc_nmea_dpt d;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_dpt(&m, &d));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.4f, d.depth_m);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, d.offset_m);
    TEST_ASSERT_TRUE(d.has_range);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f, d.range_m);

    // A DPT without the optional range field, with a negative (to-keel) offset.
    n = pc_nmea0183_build(buf, sizeof(buf), "SDDPT,10.5,-0.3");
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    TEST_ASSERT_TRUE(pc_nmea0183_parse_dpt(&m, &d));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.5f, d.depth_m);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -0.3f, d.offset_m);
    TEST_ASSERT_FALSE(d.has_range);

    // A GGA is not a DPT, and null args are rejected.
    pc_nmea0183_parse(GGA, strlen(GGA), &m);
    TEST_ASSERT_FALSE(pc_nmea0183_parse_dpt(&m, &d));
    TEST_ASSERT_FALSE(pc_nmea0183_parse_dpt(NULL, &d));
}

void test_decode_hdg()
{
    char buf[96];
    // Magnetic heading 98.3 deg, deviation 0.0 E, variation 12.6 W (-> -12.6 signed).
    size_t n = pc_nmea0183_build(buf, sizeof(buf), "HCHDG,98.3,0.0,E,12.6,W");
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    pc_nmea_hdg h;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_hdg(&m, &h));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 98.3f, h.heading_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, h.deviation_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -12.6f, h.variation_deg); // West -> negative

    // An easterly variation and a westerly deviation flip the signs the other way.
    n = pc_nmea0183_build(buf, sizeof(buf), "HCHDG,45.0,1.5,W,6.0,E");
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    TEST_ASSERT_TRUE(pc_nmea0183_parse_hdg(&m, &h));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.5f, h.deviation_deg); // West deviation -> negative
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, h.variation_deg);  // East variation -> positive

    // A GGA is not an HDG, and null args are rejected.
    pc_nmea0183_parse(GGA, strlen(GGA), &m);
    TEST_ASSERT_FALSE(pc_nmea0183_parse_hdg(&m, &h));
    TEST_ASSERT_FALSE(pc_nmea0183_parse_hdg(NULL, &h));
}

void test_decode_gll()
{
    char buf[96];
    // Classic GLL (pre-NMEA-2.3, no mode field): position 4916.45N / 12311.12W, 22:54:44 UTC, valid.
    size_t n = pc_nmea0183_build(buf, sizeof(buf), "GPGLL,4916.45,N,12311.12,W,225444,A");
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    pc_nmea_gll g;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_gll(&m, &g));
    TEST_ASSERT_TRUE(g.valid); // status 'A'
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 49.27417f, (float)g.lat_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, -123.18533f, (float)g.lon_deg);
    TEST_ASSERT_EQUAL_UINT8(22, g.hour);
    TEST_ASSERT_EQUAL_UINT8(54, g.minute);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 44.0f, g.second);
    TEST_ASSERT_EQUAL_HEX8('\0', g.mode); // no FAA mode field in the classic form

    // NMEA 2.3+ form: a 'V' status and an FAA mode indicator; S/W flips the coordinate signs.
    size_t n2 = pc_nmea0183_build(buf, sizeof(buf), "GPGLL,3345.678,S,15112.345,W,000000,V,N");
    TEST_ASSERT_TRUE(n2 > 0);
    Nmea0183 m2;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n2, &m2));
    pc_nmea_gll g2;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_gll(&m2, &g2));
    TEST_ASSERT_FALSE(g2.valid);        // status 'V'
    TEST_ASSERT_TRUE(g2.lat_deg < 0.0); // S -> negative
    TEST_ASSERT_TRUE(g2.lon_deg < 0.0); // W -> negative
    TEST_ASSERT_EQUAL_HEX8('N', g2.mode);

    // A GGA is not a GLL, and null args are rejected.
    pc_nmea0183_parse(GGA, strlen(GGA), &m);
    TEST_ASSERT_FALSE(pc_nmea0183_parse_gll(&m, &g));
    TEST_ASSERT_FALSE(pc_nmea0183_parse_gll(NULL, &g));
}

void test_decode_vhw()
{
    char buf[96];
    // Water speed + heading: heading 10.0 true / 8.5 magnetic, 6.5 knots / 12.0 km/h through the water.
    size_t n = pc_nmea0183_build(buf, sizeof(buf), "IIVHW,10.0,T,8.5,M,6.5,N,12.0,K");
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    pc_nmea_vhw v;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_vhw(&m, &v));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, v.heading_true_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.5f, v.heading_mag_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.5f, v.speed_knots);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f, v.speed_kmh);

    // A GGA is not a VHW, and null args are rejected.
    pc_nmea0183_parse(GGA, strlen(GGA), &m);
    TEST_ASSERT_FALSE(pc_nmea0183_parse_vhw(&m, &v));
    TEST_ASSERT_FALSE(pc_nmea0183_parse_vhw(NULL, &v));
}

void test_decode_vlw()
{
    char buf[96];
    // Distance through water: 1234.5 nm total, 12.3 nm since reset.
    size_t n = pc_nmea0183_build(buf, sizeof(buf), "VWVLW,1234.5,N,12.3,N");
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    pc_nmea_vlw v;
    TEST_ASSERT_TRUE(pc_nmea0183_parse_vlw(&m, &v));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1234.5f, v.total_water_nm);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.3f, v.trip_water_nm);

    // A GGA is not a VLW, and null args are rejected.
    pc_nmea0183_parse(GGA, strlen(GGA), &m);
    TEST_ASSERT_FALSE(pc_nmea0183_parse_vlw(&m, &v));
    TEST_ASSERT_FALSE(pc_nmea0183_parse_vlw(NULL, &v));
}

void test_decode_type_mismatch()
{
    Nmea0183 m;
    pc_nmea0183_parse(GGA, strlen(GGA), &m);
    pc_nmea_rmc r;
    TEST_ASSERT_FALSE(pc_nmea0183_parse_rmc(&m, &r)); // a GGA is not an RMC
    pc_nmea_gga g;
    pc_nmea0183_parse(RMC, strlen(RMC), &m);
    TEST_ASSERT_FALSE(pc_nmea0183_parse_gga(&m, &g)); // an RMC is not a GGA
    // Null guards.
    TEST_ASSERT_FALSE(pc_nmea0183_parse_gga(NULL, &g));
    TEST_ASSERT_FALSE(pc_nmea0183_parse_gga(&m, NULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_checksum_known_vector);
    RUN_TEST(test_build);
    RUN_TEST(test_parse_gga);
    RUN_TEST(test_field_helpers);
    RUN_TEST(test_parse_rejects_bad_checksum);
    RUN_TEST(test_parse_rejects_no_dollar);
    RUN_TEST(test_build_then_parse);
    RUN_TEST(test_nmea0183_error_paths);
    RUN_TEST(test_nmea0183_hex_val_edges);
    RUN_TEST(test_nmea0183_parse_guards);
    RUN_TEST(test_nmea0183_parse_scan_edges);
    RUN_TEST(test_nmea0183_field_overflow_and_short_address);
    RUN_TEST(test_nmea0183_field_helpers_more_guards);
    RUN_TEST(test_decode_gga);
    RUN_TEST(test_decode_rmc);
    RUN_TEST(test_decode_gsv);
    RUN_TEST(test_decode_gsv_blank_snr_and_partial);
    RUN_TEST(test_decode_zda);
    RUN_TEST(test_decode_vtg);
    RUN_TEST(test_decode_gsa);
    RUN_TEST(test_decode_mwv);
    RUN_TEST(test_decode_dpt);
    RUN_TEST(test_decode_hdg);
    RUN_TEST(test_decode_gll);
    RUN_TEST(test_decode_vhw);
    RUN_TEST(test_decode_vlw);
    RUN_TEST(test_decode_type_mismatch);
    return UNITY_END();
}
