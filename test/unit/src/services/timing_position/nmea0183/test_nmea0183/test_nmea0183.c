// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NMEA 0183 sentence codec (services/timing_position/nmea0183/nmea0183.h).
//
// NMEA 0183 itself is not a public document, so the sentences below are the complete worked examples
// published in the gpsd NMEA reference (gpsd.gitlab.io/gpsd/NMEA.html), each carrying the checksum
// its own publisher computed. test_published_sentence_checksums is the load-bearing case: it accepts
// every published sentence and rejects each one with a single character changed, which pins the
// checksum rule ("the 8-bit XOR of all characters in the sentence, excluding the '$' and '*'") to
// values this code had no part in producing. Two of them are also folded by hand in the comments so
// a reader can audit the rule without trusting the source: $INDPT,2.3,0.0*46 and
// $GPZDA,160012.71,11,03,2004,-1,00*7D.

#include "services/timing_position/nmea0183/nmea0183.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The published sentences. Trailing empty fields never change the checksum, since ',' is XORed in
// and cancels in pairs.
static const char *const GGA = "$GNGGA,001043.00,4404.14036,N,12118.85961,W,1,12,0.98,1113.0,M,-21.3,M,,*47";
static const char *const RMC = "$GNRMC,001031.00,A,4404.13993,N,12118.86023,W,0.146,,100117,,,A*7B";
static const char *const GSV = "$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74";
static const char *const ZDA = "$GPZDA,160012.71,11,03,2004,-1,00*7D";
static const char *const VTG = "$GPVTG,220.86,T,,M,2.550,N,4.724,K,A*34";
static const char *const GSA = "$GNGSA,A,3,80,71,73,79,69,,,,,,,,1.83,1.09,1.47*17";
static const char *const GLL = "$GNGLL,4404.14012,N,12118.85993,W,001037.00,A,A*67";
static const char *const DPT = "$INDPT,2.3,0.0*46";

static const char *const PUBLISHED[8] = {GGA, RMC, GSV, ZDA, VTG, GSA, GLL, DPT};

static proto_bool parse(const char *s, Nmea0183 *out)
{
    return protocore_nmea0183_parse(s, strlen(s), out);
}

static double dabs(double v)
{
    return v < 0 ? -v : v;
}

// The float field helper carries coordinates, so degrees land to about a millionth.
static void deg_close(double want, double got)
{
    TEST_ASSERT_TRUE(dabs(want - got) < 2e-5);
}

static void f_close(float want, float got)
{
    float d = want - got;
    TEST_ASSERT_TRUE((d < 0 ? -d : d) <= 1e-3f);
}

// The load-bearing case: every published sentence validates, and every single-character edit of one
// breaks it. Editing a body character changes the computed XOR; editing a checksum digit changes the
// declared one.
void test_published_sentence_checksums(void)
{
    for (unsigned i = 0; i < 8; i++)
    {
        Nmea0183 m;
        TEST_ASSERT_TRUE_MESSAGE(parse(PUBLISHED[i], &m), PUBLISHED[i]);
    }

    char edit[128];
    for (unsigned i = 0; i < 8; i++)
    {
        const size_t n = strlen(PUBLISHED[i]);
        for (size_t k = 1; k < n; k++)
        {
            if (PUBLISHED[i][k] == '*')
            {
                continue; // moving the delimiter is a framing change, not a checksum one
            }
            memcpy(edit, PUBLISHED[i], n + 1);
            edit[k] = (edit[k] == '0') ? '1' : '0';
            Nmea0183 m;
            TEST_ASSERT_FALSE_MESSAGE(parse(edit, &m), edit);
        }
    }
}

// The checksum rule folded by hand over the shortest published sentence:
//   'I'^'N' = 49^4E = 07;  ^'D' = 43;  ^'P' = 13;  ^'T' = 47;  ^',' = 6B;  ^'2' = 59;
//   ^'.'    = 77;          ^'3' = 44;  ^',' = 68;  ^'0' = 58;  ^'.' = 76;  ^'0' = 46
// which is the *46 the sentence declares.
void test_checksum_is_the_xor_of_the_body(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x46, protocore_nmea0183_checksum("INDPT,2.3,0.0", 13));

    // $GPZDA,160012.71,11,03,2004,-1,00*7D, folded the same way, lands on 7D.
    TEST_ASSERT_EQUAL_HEX8(0x7D, protocore_nmea0183_checksum("GPZDA,160012.71,11,03,2004,-1,00", 32));

    // An empty body XORs to 0, and one character to itself.
    TEST_ASSERT_EQUAL_HEX8(0x00, protocore_nmea0183_checksum("", 0));
    TEST_ASSERT_EQUAL_HEX8(0x41, protocore_nmea0183_checksum("A", 1));
    // A character folded in twice cancels: XOR is its own inverse.
    TEST_ASSERT_EQUAL_HEX8(0x00, protocore_nmea0183_checksum("AA", 2));
    TEST_ASSERT_EQUAL_HEX8(protocore_nmea0183_checksum("AB", 2), protocore_nmea0183_checksum("BA", 2));
}

// A built sentence is "$" + body + "*HH" + CR LF, and parsing it back gives the body's fields.
void test_build_frames_the_body(void)
{
    char buf[96];
    size_t n = protocore_nmea0183_build(buf, sizeof(buf), "INDPT,2.3,0.0");
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen("$INDPT,2.3,0.0*46\r\n"), (uint32_t)n);
    TEST_ASSERT_EQUAL_STRING("$INDPT,2.3,0.0*46\r\n", buf);

    // The build of every published sentence's body reproduces that sentence.
    for (unsigned i = 0; i < 8; i++)
    {
        const char *star = strchr(PUBLISHED[i], '*');
        char body[128];
        size_t blen = (size_t)(star - PUBLISHED[i] - 1);
        memcpy(body, PUBLISHED[i] + 1, blen);
        body[blen] = '\0';

        char out[160];
        TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)protocore_nmea0183_build(out, sizeof(out), body));
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(PUBLISHED[i], out, strlen(PUBLISHED[i]), PUBLISHED[i]);
        TEST_ASSERT_EQUAL_CHAR('\r', out[strlen(PUBLISHED[i])]);
        TEST_ASSERT_EQUAL_CHAR('\n', out[strlen(PUBLISHED[i]) + 1]);
    }

    // A buffer too small for the framed sentence writes nothing rather than a truncated one.
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_nmea0183_build(buf, 8, "INDPT,2.3,0.0"));
}

// Field 0 is the 5-character address; the talker is its first two characters and the sentence type
// the last three.
void test_address_splits_into_talker_and_type(void)
{
    Nmea0183 m;
    TEST_ASSERT_TRUE(parse(GGA, &m));
    TEST_ASSERT_EQUAL_STRING("GN", m.talker);
    TEST_ASSERT_EQUAL_STRING("GGA", m.type);
    TEST_ASSERT_EQUAL_MEMORY("GNGGA", m.fields[0], 5);
    TEST_ASSERT_EQUAL_UINT8(5, m.field_len[0]);

    TEST_ASSERT_TRUE(parse(DPT, &m));
    TEST_ASSERT_EQUAL_STRING("IN", m.talker);
    TEST_ASSERT_EQUAL_STRING("DPT", m.type);
    TEST_ASSERT_EQUAL_UINT8(3, m.field_count); // address + depth + offset
}

// Commas separate fields and an empty field is a zero-length one, not a missing one: the GSA
// sentence has eight blank satellite slots between its PRNs and its DOPs.
void test_empty_fields_are_counted(void)
{
    Nmea0183 m;
    TEST_ASSERT_TRUE(parse(GSA, &m));
    TEST_ASSERT_EQUAL_UINT8(18, m.field_count); // address + 17 data fields
    TEST_ASSERT_EQUAL_UINT8(0, m.field_len[8]); // the first blank PRN slot
    TEST_ASSERT_EQUAL_UINT8(4, m.field_len[15]);
    TEST_ASSERT_EQUAL_MEMORY("1.83", m.fields[15], 4);

    long v = 0;
    TEST_ASSERT_FALSE(protocore_nmea0183_field_int(&m, 8, &v)); // empty is not a number
    TEST_ASSERT_TRUE(protocore_nmea0183_field_int(&m, 2, &v));
    TEST_ASSERT_EQUAL_INT32(3, (int32_t)v); // 3D fix
    float f = 0.0f;
    TEST_ASSERT_TRUE(protocore_nmea0183_field_float(&m, 16, &f));
    f_close(1.09f, f);
    TEST_ASSERT_FALSE(protocore_nmea0183_field_float(&m, m.field_count, &f)); // past the end
}

// A sentence must start with '$' or '!', must carry a '*HH' checksum, and must have two hex digits
// there. Anything else is not a sentence.
void test_framing_is_enforced(void)
{
    Nmea0183 m;
    TEST_ASSERT_FALSE(parse("GNGGA,001043.00*47", &m)); // no start delimiter
    TEST_ASSERT_FALSE(parse("$INDPT,2.3,0.0", &m));     // no checksum
    TEST_ASSERT_FALSE(parse("$INDPT,2.3,0.0*4", &m));   // one checksum digit
    TEST_ASSERT_FALSE(parse("$INDPT,2.3,0.0*4G", &m));  // not hex
    TEST_ASSERT_FALSE(parse("$", &m));
    TEST_ASSERT_FALSE(parse("", &m));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse(DPT, strlen(DPT), NULL));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse(NULL, 8, &m));

    // '!' is the AIS-encapsulation delimiter and is accepted; the checksum rule is unchanged.
    char ais[32];
    size_t n = protocore_nmea0183_build(ais, sizeof(ais), "INDPT,2.3,0.0");
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
    ais[0] = '!';
    TEST_ASSERT_TRUE(protocore_nmea0183_parse(ais, n, &m));
    TEST_ASSERT_EQUAL_STRING("DPT", m.type);

    // Lowercase hex in the checksum is the same value.
    TEST_ASSERT_TRUE(parse("$INDPT,2.3,0.0*46", &m));
    TEST_ASSERT_TRUE(parse("$GNGLL,4404.14012,N,12118.85993,W,001037.00,A,A*67", &m));
}

// GGA: hhmmss.ss time, ddmm.mmmm coordinates hemisphere-adjusted into signed decimal degrees, fix
// quality, satellite count, HDOP and altitude.
//   4404.14036 N -> 44 + 4.14036/60   =  44.0690060
//   12118.85961 W -> -(121 + 18.85961/60) = -121.3143268
void test_gga_decodes_the_published_fix(void)
{
    Nmea0183 m;
    protocore_nmea_gga g;
    TEST_ASSERT_TRUE(parse(GGA, &m));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_gga(&m, &g));
    TEST_ASSERT_EQUAL_UINT8(0, g.hour);
    TEST_ASSERT_EQUAL_UINT8(10, g.minute);
    f_close(43.0f, g.second);
    deg_close(44.0690060, g.lat_deg);
    deg_close(-121.3143268, g.lon_deg);
    TEST_ASSERT_EQUAL_UINT8(1, g.fix_quality);
    TEST_ASSERT_EQUAL_UINT8(12, g.num_sats);
    f_close(0.98f, g.hdop);
    f_close(1113.0f, g.alt_m);

    // A GGA decoder refuses a sentence that is not a GGA.
    TEST_ASSERT_TRUE(parse(RMC, &m));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_gga(&m, &g));
}

// RMC: status, position, speed, course and the ddmmyy date.
void test_rmc_decodes_the_published_fix(void)
{
    Nmea0183 m;
    protocore_nmea_rmc r;
    TEST_ASSERT_TRUE(parse(RMC, &m));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_rmc(&m, &r));
    TEST_ASSERT_TRUE(r.valid); // status 'A'
    TEST_ASSERT_EQUAL_UINT8(0, r.hour);
    TEST_ASSERT_EQUAL_UINT8(10, r.minute);
    f_close(31.0f, r.second);
    deg_close(44.0689988, r.lat_deg);   // 44 + 4.13993/60
    deg_close(-121.3143372, r.lon_deg); // -(121 + 18.86023/60)
    f_close(0.146f, r.speed_knots);
    TEST_ASSERT_EQUAL_UINT8(10, r.day); // 100117 = 10 Jan 2017
    TEST_ASSERT_EQUAL_UINT8(1, r.month);
    TEST_ASSERT_EQUAL_UINT8(17, r.year);
}

// GSV: the cycle header plus up to four satellite records, each PRN / elevation / azimuth / SNR.
void test_gsv_decodes_the_published_sky_view(void)
{
    Nmea0183 m;
    protocore_nmea_gsv g;
    TEST_ASSERT_TRUE(parse(GSV, &m));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_gsv(&m, &g));
    TEST_ASSERT_EQUAL_UINT8(3, g.total_msgs);
    TEST_ASSERT_EQUAL_UINT8(1, g.msg_num);
    TEST_ASSERT_EQUAL_UINT8(11, g.sats_in_view);
    TEST_ASSERT_EQUAL_UINT8(4, g.sat_count);

    TEST_ASSERT_EQUAL_UINT8(3, g.sats[0].prn);
    TEST_ASSERT_EQUAL_INT16(3, g.sats[0].elev_deg);
    TEST_ASSERT_EQUAL_INT16(111, g.sats[0].azim_deg);
    TEST_ASSERT_EQUAL_UINT8(4, g.sats[1].prn);
    TEST_ASSERT_EQUAL_INT16(15, g.sats[1].elev_deg);
    TEST_ASSERT_EQUAL_INT16(270, g.sats[1].azim_deg);
    TEST_ASSERT_EQUAL_UINT8(13, g.sats[3].prn);
    TEST_ASSERT_EQUAL_INT16(292, g.sats[3].azim_deg);
}

// ZDA carries the four-digit year and a signed local-zone offset, which is why it is the sentence a
// clock synchronizes from.
void test_zda_decodes_the_published_time(void)
{
    Nmea0183 m;
    protocore_nmea_zda z;
    TEST_ASSERT_TRUE(parse(ZDA, &m));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_zda(&m, &z));
    TEST_ASSERT_EQUAL_UINT8(16, z.hour);
    TEST_ASSERT_EQUAL_UINT8(0, z.minute);
    f_close(12.71f, z.second);
    TEST_ASSERT_EQUAL_UINT8(11, z.day);
    TEST_ASSERT_EQUAL_UINT8(3, z.month);
    TEST_ASSERT_EQUAL_UINT16(2004, z.year);
    TEST_ASSERT_EQUAL_INT8(-1, z.zone_hours);
    TEST_ASSERT_EQUAL_UINT8(0, z.zone_minutes);
}

// VTG: course true and magnetic, then speed in knots and km/h with their unit letters. The magnetic
// course field is blank here, which must read back as 0 rather than failing the decode.
void test_vtg_decodes_the_published_course(void)
{
    Nmea0183 m;
    protocore_nmea_vtg v;
    TEST_ASSERT_TRUE(parse(VTG, &m));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_vtg(&m, &v));
    f_close(220.86f, v.course_true_deg);
    f_close(0.0f, v.course_mag_deg);
    f_close(2.550f, v.speed_knots);
    f_close(4.724f, v.speed_kmh);
    TEST_ASSERT_EQUAL_CHAR('A', v.mode);
}

// GSA: selection mode, fix type, the PRNs actually used, and the three DOPs. The blank PRN slots are
// skipped rather than counted as satellites.
void test_gsa_decodes_the_published_fix_set(void)
{
    Nmea0183 m;
    protocore_nmea_gsa g;
    TEST_ASSERT_TRUE(parse(GSA, &m));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_gsa(&m, &g));
    TEST_ASSERT_EQUAL_CHAR('A', g.mode);
    TEST_ASSERT_EQUAL_UINT8(3, g.fix_type); // 3D
    TEST_ASSERT_EQUAL_UINT8(5, g.sat_count);
    TEST_ASSERT_EQUAL_UINT8(80, g.sats[0]);
    TEST_ASSERT_EQUAL_UINT8(71, g.sats[1]);
    TEST_ASSERT_EQUAL_UINT8(73, g.sats[2]);
    TEST_ASSERT_EQUAL_UINT8(79, g.sats[3]);
    TEST_ASSERT_EQUAL_UINT8(69, g.sats[4]);
    f_close(1.83f, g.pdop);
    f_close(1.09f, g.hdop);
    f_close(1.47f, g.vdop);
}

// GLL is the minimal position report: coordinates, time, status, and the NMEA 2.3 mode indicator.
void test_gll_decodes_the_published_position(void)
{
    Nmea0183 m;
    protocore_nmea_gll g;
    TEST_ASSERT_TRUE(parse(GLL, &m));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_gll(&m, &g));
    deg_close(44.0690020, g.lat_deg);   // 44 + 4.14012/60
    deg_close(-121.3143322, g.lon_deg); // -(121 + 18.85993/60)
    TEST_ASSERT_EQUAL_UINT8(0, g.hour);
    TEST_ASSERT_EQUAL_UINT8(10, g.minute);
    f_close(37.0f, g.second);
    TEST_ASSERT_TRUE(g.valid);
    TEST_ASSERT_EQUAL_CHAR('A', g.mode);
}

// DPT: depth below the transducer and the transducer offset, with the range scale optional.
void test_dpt_decodes_the_published_depth(void)
{
    Nmea0183 m;
    protocore_nmea_dpt d;
    TEST_ASSERT_TRUE(parse(DPT, &m));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_dpt(&m, &d));
    f_close(2.3f, d.depth_m);
    f_close(0.0f, d.offset_m);
    TEST_ASSERT_FALSE(d.has_range);

    char with_range[64];
    TEST_ASSERT_GREATER_THAN_UINT32(
        0, (uint32_t)protocore_nmea0183_build(with_range, sizeof(with_range), "INDPT,12.5,-0.5,200.0"));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse(with_range, strlen(with_range), &m));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_dpt(&m, &d));
    f_close(12.5f, d.depth_m);
    f_close(-0.5f, d.offset_m);
    TEST_ASSERT_TRUE(d.has_range);
    f_close(200.0f, d.range_m);
}

// The instrument sentences the standard templates but gpsd prints no complete example for: built
// through this codec's own framing and read back, so the field order and the sign folding of the
// E/W direction letters are still checked end to end.
void test_instrument_sentences_round_trip(void)
{
    char buf[96];
    Nmea0183 m;

    // MWV: angle, reference, speed, units, status.
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)protocore_nmea0183_build(buf, sizeof(buf), "WIMWV,214.8,R,0.1,K,A"));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse(buf, strlen(buf), &m));
    protocore_nmea_mwv w;
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_mwv(&m, &w));
    f_close(214.8f, w.wind_angle_deg);
    TEST_ASSERT_EQUAL_CHAR('R', w.reference);
    f_close(0.1f, w.wind_speed);
    TEST_ASSERT_EQUAL_CHAR('K', w.speed_units);
    TEST_ASSERT_TRUE(w.valid);

    // HDG: heading, deviation E/W, variation E/W. East is positive, West negative, so a westerly
    // variation of 3.5 reads back as -3.5.
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)protocore_nmea0183_build(buf, sizeof(buf), "HCHDG,98.3,2.0,E,3.5,W"));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse(buf, strlen(buf), &m));
    protocore_nmea_hdg h;
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_hdg(&m, &h));
    f_close(98.3f, h.heading_deg);
    f_close(2.0f, h.deviation_deg);
    f_close(-3.5f, h.variation_deg);

    // VHW: heading true / magnetic, then speed in knots and km/h.
    TEST_ASSERT_GREATER_THAN_UINT32(
        0, (uint32_t)protocore_nmea0183_build(buf, sizeof(buf), "VWVHW,100.0,T,102.0,M,5.5,N,10.2,K"));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse(buf, strlen(buf), &m));
    protocore_nmea_vhw v;
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_vhw(&m, &v));
    f_close(100.0f, v.heading_true_deg);
    f_close(102.0f, v.heading_mag_deg);
    f_close(5.5f, v.speed_knots);
    f_close(10.2f, v.speed_kmh);

    // VLW: cumulative and trip water distance.
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)protocore_nmea0183_build(buf, sizeof(buf), "VWVLW,1234.5,N,12.3,N"));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse(buf, strlen(buf), &m));
    protocore_nmea_vlw l;
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_vlw(&m, &l));
    f_close(1234.5f, l.total_water_nm);
    f_close(12.3f, l.trip_water_nm);
}

// Every typed decoder refuses a sentence of the wrong type rather than reading another sentence's
// fields as its own.
void test_typed_decoders_check_the_sentence_type(void)
{
    Nmea0183 m;
    TEST_ASSERT_TRUE(parse(DPT, &m));

    protocore_nmea_gga gga;
    protocore_nmea_rmc rmc;
    protocore_nmea_gsv gsv;
    protocore_nmea_zda zda;
    protocore_nmea_vtg vtg;
    protocore_nmea_gsa gsa;
    protocore_nmea_gll gll;
    protocore_nmea_mwv mwv;
    protocore_nmea_hdg hdg;
    protocore_nmea_vhw vhw;
    protocore_nmea_vlw vlw;
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_gga(&m, &gga));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_rmc(&m, &rmc));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_gsv(&m, &gsv));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_zda(&m, &zda));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_vtg(&m, &vtg));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_gsa(&m, &gsa));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_gll(&m, &gll));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_mwv(&m, &mwv));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_hdg(&m, &hdg));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_vhw(&m, &vhw));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_vlw(&m, &vlw));

    // A GGA truncated before its altitude no longer has the fields the decoder needs.
    char shortgga[64];
    TEST_ASSERT_GREATER_THAN_UINT32(0,
                                    (uint32_t)protocore_nmea0183_build(shortgga, sizeof(shortgga), "GNGGA,001043.00"));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse(shortgga, strlen(shortgga), &m));
    TEST_ASSERT_FALSE(protocore_nmea0183_parse_gga(&m, &gga));
}

// The southern and eastern hemispheres take the opposite sign to the published northern / western
// example, which is what the hemisphere letter is for.
void test_hemisphere_letters_set_the_sign(void)
{
    char buf[96];
    Nmea0183 m;
    protocore_nmea_gga g;

    TEST_ASSERT_GREATER_THAN_UINT32(
        0, (uint32_t)protocore_nmea0183_build(buf, sizeof(buf),
                                              "GPGGA,123519,4807.038,S,01131.000,E,1,08,0.9,545.4,M,,M,,"));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse(buf, strlen(buf), &m));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_gga(&m, &g));
    deg_close(-48.1173000, g.lat_deg); // -(48 + 7.038/60)
    deg_close(11.5166667, g.lon_deg);  // 11 + 31.000/60
    TEST_ASSERT_EQUAL_UINT8(12, g.hour);
    TEST_ASSERT_EQUAL_UINT8(35, g.minute);
    f_close(19.0f, g.second);

    TEST_ASSERT_GREATER_THAN_UINT32(
        0, (uint32_t)protocore_nmea0183_build(buf, sizeof(buf),
                                              "GPGGA,123519,4807.038,N,01131.000,W,1,08,0.9,545.4,M,,M,,"));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse(buf, strlen(buf), &m));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse_gga(&m, &g));
    deg_close(48.1173000, g.lat_deg);
    deg_close(-11.5166667, g.lon_deg);
}
