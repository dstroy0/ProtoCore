// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the GNSS survey-in core (services/timing_position/gnss/pc_gnss_survey): the WGS84 geodetic->ECEF
// transform, its iterative inverse, the fixed-position averager + 3-D accuracy estimate, and the GGA->geodetic fold.
// The ECEF reference vectors below were produced by pyproj 3.7.2 transforming EPSG:4979 (WGS84 lat/lon/
// ellipsoidal height) to EPSG:4978 (WGS84 geocentric ECEF); the C transform must match to <= 0.1 mm.

#include "services/timing_position/gnss/gnss_survey.h"
#include "services/timing_position/nmea0183/nmea0183.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// One pyproj-derived case: geodetic in, ECEF (metres) out.
typedef struct
{
    double lat, lon, h;
    double x, y, z;
} EcefCase;

static const EcefCase CASES[] = {
    {0.0, 0.0, 0.0, 6378137.000000, 0.000000, 0.000000},
    {45.0, 45.0, 100.0, 3194469.145061, 3194469.145061, 4487419.119544},
    {37.7749, -122.4194, 52.0, -2706196.881923, -4261094.185420, 3885757.343188},
    {-33.8688, 151.2093, 58.0, -4646093.477288, 2553229.535817, -3534404.710910},
};

void test_geodetic_to_ecef_matches_pyproj()
{
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        GnssGeodetic g = {CASES[i].lat, CASES[i].lon, CASES[i].h};
        GnssEcef e;
        pc_gnss_geodetic_to_ecef(&g, &e);
        // pyproj prints to 1e-6 m; allow 0.2 mm for that rounding plus our own.
        TEST_ASSERT_DOUBLE_WITHIN(2e-4, CASES[i].x, e.x);
        TEST_ASSERT_DOUBLE_WITHIN(2e-4, CASES[i].y, e.y);
        TEST_ASSERT_DOUBLE_WITHIN(2e-4, CASES[i].z, e.z);
    }
}

void test_ecef_to_geodetic_roundtrip()
{
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        GnssEcef e = {CASES[i].x, CASES[i].y, CASES[i].z};
        GnssGeodetic g;
        pc_gnss_ecef_to_geodetic(&e, &g);
        TEST_ASSERT_DOUBLE_WITHIN(1e-8, CASES[i].lat, g.lat_deg); // ~1 mm in latitude
        TEST_ASSERT_DOUBLE_WITHIN(1e-8, CASES[i].lon, g.lon_deg);
        TEST_ASSERT_DOUBLE_WITHIN(1e-3, CASES[i].h, g.height_m);
    }
}

void test_m_to_01mm_rounds_half_away()
{
    TEST_ASSERT_EQUAL_INT64(63781370000LL, pc_gnss_ecef_m_to_01mm(6378137.0));
    TEST_ASSERT_EQUAL_INT64(1, pc_gnss_ecef_m_to_01mm(0.00005));   // 0.00005 m = 0.5 units -> 1
    TEST_ASSERT_EQUAL_INT64(-1, pc_gnss_ecef_m_to_01mm(-0.00005)); // -0.5 units -> -1
    TEST_ASSERT_EQUAL_INT64(0, pc_gnss_ecef_m_to_01mm(0.0));
}

// A single fix converts, averages to itself, and yields the same 0.1 mm ECEF pyproj gave.
void test_survey_single_fix_matches_reference()
{
    GnssSurvey s;
    pc_gnss_survey_reset(&s);
    GnssGeodetic g = {37.7749, -122.4194, 52.0};
    pc_gnss_survey_add_geodetic(&s, &g);
    TEST_ASSERT_EQUAL_UINT32(1, pc_gnss_survey_count(&s));

    GnssEcef mean;
    TEST_ASSERT_TRUE(pc_gnss_survey_mean(&s, &mean));
    TEST_ASSERT_EQUAL_INT64(-27061968819LL, pc_gnss_ecef_m_to_01mm(mean.x));
    TEST_ASSERT_EQUAL_INT64(-42610941854LL, pc_gnss_ecef_m_to_01mm(mean.y));
    TEST_ASSERT_EQUAL_INT64(38857573432LL, pc_gnss_ecef_m_to_01mm(mean.z));
    // One fix -> no spread, and not "complete" (needs >= 2 obs).
    TEST_ASSERT_EQUAL_DOUBLE(0.0, pc_gnss_survey_accuracy_m(&s));
    TEST_ASSERT_FALSE(pc_gnss_survey_complete(&s, 1, 10.0));
}

// Symmetric scatter about a true point: the mean lands on the truth and the accuracy is the RMS spread.
void test_survey_averages_out_scatter()
{
    GnssEcef truth = {-2706196.881923, -4261094.185420, 3885757.343188};
    GnssSurvey s;
    pc_gnss_survey_reset(&s);
    const double d = 1.5; // metres of jitter on each axis
    // Four fixes: +/-d on x and +/-d on y, truth on z. Mean of the offsets is exactly zero.
    GnssEcef fixes[4] = {
        {truth.x + d, truth.y + d, truth.z},
        {truth.x - d, truth.y - d, truth.z},
        {truth.x + d, truth.y - d, truth.z},
        {truth.x - d, truth.y + d, truth.z},
    };
    for (int i = 0; i < 4; i++)
    {
        pc_gnss_survey_add_ecef(&s, &fixes[i]);
    }

    TEST_ASSERT_EQUAL_UINT32(4, pc_gnss_survey_count(&s));
    GnssEcef mean;
    TEST_ASSERT_TRUE(pc_gnss_survey_mean(&s, &mean));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, truth.x, mean.x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, truth.y, mean.y);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, truth.z, mean.z);

    // Per-axis population variance is d^2 on x and y, 0 on z -> 3-D sigma = sqrt(2) * d.
    double expect = 1.4142135623730951 * d;
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, expect, pc_gnss_survey_accuracy_m(&s));

    // Convergence gate: enough obs + spread within limit.
    TEST_ASSERT_TRUE(pc_gnss_survey_complete(&s, 4, 3.0));
    TEST_ASSERT_FALSE(pc_gnss_survey_complete(&s, 4, 2.0)); // spread ~2.12 m > 2.0 m
    TEST_ASSERT_FALSE(pc_gnss_survey_complete(&s, 5, 3.0)); // not enough observations
}

// The variance clamp in pc_gnss_survey_accuracy_m: sdx2/n - mean^2 can dip a hair below zero from
// floating-point rounding, and the guard clamps each axis to 0 before the sqrt. Real fixes essentially
// never trigger it, so drive it directly by crafting sums (via the struct's public fields, same as
// test_survey_empty_has_no_mean pokes GnssEcef directly) where every axis's "variance" is negative.
void test_survey_accuracy_clamps_negative_variance()
{
    GnssSurvey s;
    pc_gnss_survey_reset(&s);
    s.has_origin = PROTO_TRUE;
    s.count = 2;
    s.sdx = s.sdy = s.sdz = 10.0;      // mean per axis = 5.0 -> mean^2 * n = 50.0
    s.sdx2 = s.sdy2 = s.sdz2 = 49.999; // < 50.0 -> vx/vy/vz all compute negative before the clamp
    TEST_ASSERT_EQUAL_DOUBLE(0.0, pc_gnss_survey_accuracy_m(&s));
}

void test_survey_empty_has_no_mean()
{
    GnssSurvey s;
    pc_gnss_survey_reset(&s);
    GnssEcef mean;
    memset(&mean, 0x5A, sizeof(mean));
    TEST_ASSERT_FALSE(pc_gnss_survey_mean(&s, &mean));
    TEST_ASSERT_EQUAL_UINT32(0, pc_gnss_survey_count(&s));
}

// A GT-U7-style GGA: 37 deg 23.2475' N, 122 deg 02.1236' W, MSL 30.5 m, geoid -25.0 m. Build it (so the
// checksum is correct by construction) then parse it back.
void test_gga_to_geodetic()
{
    char buf[96];
    const char *body = "GPGGA,124515.00,3723.2475,N,12202.1236,W,1,08,0.9,30.5,M,-25.0,M,,";
    size_t n = pc_nmea0183_build(buf, sizeof(buf), body);
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));

    GnssGeodetic g;
    TEST_ASSERT_TRUE(pc_gnss_gga_to_geodetic(&m, &g));
    // 3723.2475 -> 37 + 23.2475/60 = 37.387458...; W/S negate.
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 37.3874583333, g.lat_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -122.0353933333, g.lon_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 5.5, g.height_m); // 30.5 + (-25.0)
}

void test_gga_no_fix_rejected()
{
    // Fix quality field (index 6) = 0 -> no fix -> reject.
    char buf[96];
    const char *body = "GPGGA,124515.00,3723.2475,N,12202.1236,W,0,00,99.9,,M,,M,,";
    size_t n = pc_nmea0183_build(buf, sizeof(buf), body);
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));
    GnssGeodetic g;
    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(&m, &g));
}

void test_survey_add_gga_folds_fix()
{
    char buf[96];
    const char *body = "GPGGA,124515.00,3723.2475,N,12202.1236,W,1,08,0.9,30.5,M,-25.0,M,,";
    size_t n = pc_nmea0183_build(buf, sizeof(buf), body);
    TEST_ASSERT_TRUE(n > 0);
    Nmea0183 m;
    TEST_ASSERT_TRUE(pc_nmea0183_parse(buf, n, &m));

    GnssSurvey s;
    pc_gnss_survey_reset(&s);
    TEST_ASSERT_TRUE(pc_gnss_survey_add_gga(&s, &m));
    TEST_ASSERT_EQUAL_UINT32(1, pc_gnss_survey_count(&s));

    // The folded fix matches a direct geodetic->ECEF of the same position.
    GnssGeodetic g = {37.3874583333, -122.0353933333, 5.5};
    GnssEcef direct;
    pc_gnss_geodetic_to_ecef(&g, &direct);
    GnssEcef mean;
    TEST_ASSERT_TRUE(pc_gnss_survey_mean(&s, &mean));
    TEST_ASSERT_DOUBLE_WITHIN(1e-2, direct.x, mean.x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-2, direct.y, mean.y);
    TEST_ASSERT_DOUBLE_WITHIN(1e-2, direct.z, mean.z);
}

// The polar-axis inverse branch: on the z axis (x == y == 0) latitude is +/-90 and height is
// measured straight along z. WGS84 semi-minor axis b = a(1 - f).
void test_ecef_to_geodetic_north_pole()
{
    const double b = 6378137.0 * (1.0 - 1.0 / 298.257223563);
    GnssEcef e = {0.0, 0.0, b + 100.0}; // 100 m above the north pole on the ellipsoid
    GnssGeodetic g;
    pc_gnss_ecef_to_geodetic(&e, &g);
    TEST_ASSERT_EQUAL_DOUBLE(90.0, g.lat_deg);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, g.lon_deg); // atan2(0, 0) == 0
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 100.0, g.height_m);
}

void test_ecef_to_geodetic_south_pole()
{
    const double b = 6378137.0 * (1.0 - 1.0 / 298.257223563);
    GnssEcef e = {0.0, 0.0, -(b + 250.0)}; // 250 m above the south pole
    GnssGeodetic g;
    pc_gnss_ecef_to_geodetic(&e, &g);
    TEST_ASSERT_EQUAL_DOUBLE(-90.0, g.lat_deg); // z < 0 -> the other ternary arm
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 250.0, g.height_m);
}

// Build a GGA body (checksum added by construction) and parse it back into @p m.
static proto_bool build_parse_gga(const char *body, char *buf, size_t cap, Nmea0183 *m)
{
    size_t n = pc_nmea0183_build(buf, cap, body);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    return pc_nmea0183_parse(buf, n, m);
}

// A GGA whose latitude field is empty -> dm_to_deg's zero-length guard rejects.
void test_gga_empty_lat_rejected()
{
    char buf[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(
        build_parse_gga("GPGGA,124515.00,,N,12202.1236,W,1,08,0.9,30.5,M,-25.0,M,,", buf, sizeof(buf), &m));
    GnssGeodetic g;
    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(&m, &g));
}

// A GGA whose latitude field is non-numeric -> dm_to_deg's "no digits converted" guard rejects.
void test_gga_nonnumeric_lat_rejected()
{
    char buf[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(
        build_parse_gga("GPGGA,124515.00,XX,N,12202.1236,W,1,08,0.9,30.5,M,-25.0,M,,", buf, sizeof(buf), &m));
    GnssGeodetic g;
    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(&m, &g));
}

// dm_to_deg's `!field` guard: a real parse never leaves a field pointer null, so force it directly (the
// Nmea0183 fields[] array is public, same technique as overriding GnssSurvey's fields above) to hit the
// short-circuit true arm of `!field || len == 0` that the empty/non-numeric-field tests can't reach.
void test_gga_null_lat_field_rejected()
{
    char buf[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(
        build_parse_gga("GPGGA,124515.00,3723.2475,N,12202.1236,W,1,08,0.9,30.5,M,-25.0,M,,", buf, sizeof(buf), &m));
    m.fields[2] = NULL; // simulate a null field pointer
    GnssGeodetic g;
    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(&m, &g));
}

// A GGA whose longitude field is empty -> the lon dm_to_deg call rejects (after lat parses fine).
void test_gga_empty_lon_rejected()
{
    char buf[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(build_parse_gga("GPGGA,124515.00,3723.2475,N,,W,1,08,0.9,30.5,M,-25.0,M,,", buf, sizeof(buf), &m));
    GnssGeodetic g;
    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(&m, &g));
}

// A GGA whose fix-quality field is empty -> pc_nmea0183_field_int fails -> reject (distinct from quality 0).
void test_gga_empty_quality_rejected()
{
    char buf[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(
        build_parse_gga("GPGGA,124515.00,3723.2475,N,12202.1236,W,,08,0.9,30.5,M,-25.0,M,,", buf, sizeof(buf), &m));
    GnssGeodetic g;
    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(&m, &g));
}

// A GGA whose altitude (MSL) field is empty -> pc_nmea0183_field_float(9) fails -> reject.
void test_gga_empty_altitude_rejected()
{
    char buf[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(
        build_parse_gga("GPGGA,124515.00,3723.2475,N,12202.1236,W,1,08,0.9,,M,-25.0,M,,", buf, sizeof(buf), &m));
    GnssGeodetic g;
    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(&m, &g));
}

// A GGA truncated before the altitude field (field_count < 10) -> reject.
void test_gga_too_few_fields_rejected()
{
    char buf[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(build_parse_gga("GPGGA,124515.00,3723.2475,N,12202.1236,W,1", buf, sizeof(buf), &m));
    TEST_ASSERT_TRUE(m.field_count < 10);
    GnssGeodetic g;
    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(&m, &g));
}

// Southern latitude + eastern longitude: the 'S' arm negates lat, 'E' leaves lon positive.
void test_gga_southern_eastern_hemisphere()
{
    char buf[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(
        build_parse_gga("GPGGA,124515.00,3723.2475,S,12202.1236,E,1,08,0.9,30.5,M,-25.0,M,,", buf, sizeof(buf), &m));
    GnssGeodetic g;
    TEST_ASSERT_TRUE(pc_gnss_gga_to_geodetic(&m, &g));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -37.3874583333, g.lat_deg); // S -> negative
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 122.0353933333, g.lon_deg); // E -> positive
}

// Lowercase 's'/'w' hemisphere letters are accepted too (the second arm of each OR).
void test_gga_lowercase_hemispheres()
{
    char buf[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(
        build_parse_gga("GPGGA,124515.00,3723.2475,s,12202.1236,w,1,08,0.9,30.5,M,-25.0,M,,", buf, sizeof(buf), &m));
    GnssGeodetic g;
    TEST_ASSERT_TRUE(pc_gnss_gga_to_geodetic(&m, &g));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -37.3874583333, g.lat_deg);  // 's' -> negative
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -122.0353933333, g.lon_deg); // 'w' -> negative
}

// Hemisphere fields with length != 1 take the short-circuit false arm of `field_len == 1 && (...)` (the
// letter is never even looked at); both existing tests always pass a single-char N/S and E/W field, so
// leave both hemisphere fields empty here to hit that arm on both guards.
void test_gga_empty_hemisphere_fields_not_negated()
{
    char buf[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(
        build_parse_gga("GPGGA,124515.00,3723.2475,,12202.1236,,1,08,0.9,30.5,M,-25.0,M,,", buf, sizeof(buf), &m));
    GnssGeodetic g;
    TEST_ASSERT_TRUE(pc_gnss_gga_to_geodetic(&m, &g));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 37.3874583333, g.lat_deg);  // no hemisphere letter -> stays positive
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 122.0353933333, g.lon_deg); // no hemisphere letter -> stays positive
}

// A GGA with no geoid-separation field (field_count 11) -> geoid defaults to 0, height == MSL.
void test_gga_geoid_absent_defaults_zero()
{
    char buf[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(build_parse_gga("GPGGA,124515.00,3723.2475,N,12202.1236,W,1,08,0.9,30.5,M", buf, sizeof(buf), &m));
    GnssGeodetic g;
    TEST_ASSERT_TRUE(pc_gnss_gga_to_geodetic(&m, &g));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 30.5, g.height_m); // MSL 30.5 + geoid 0
}

// Null pointers and non-GGA sentence types are all rejected by the address guard.
void test_gga_bad_args_and_types_rejected()
{
    char buf[96];
    Nmea0183 good;
    TEST_ASSERT_TRUE(
        build_parse_gga("GPGGA,124515.00,3723.2475,N,12202.1236,W,1,08,0.9,30.5,M,-25.0,M,,", buf, sizeof(buf), &good));
    GnssGeodetic g;

    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(NULL, &g));    // !m
    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(&good, NULL)); // !out

    char b2[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(build_parse_gga("GPVTG,054.7,T,034.4,M,005.5,N,010.2,K", b2, sizeof(b2), &m));
    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(&m, &g)); // type[0] != 'G'
    TEST_ASSERT_TRUE(build_parse_gga("GPGLL,3723.2475,N,12202.1236,W,124515,A", b2, sizeof(b2), &m));
    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(&m, &g)); // type[1] != 'G'
    TEST_ASSERT_TRUE(build_parse_gga("GPGGZ,124515.00,3723.2475,N,12202.1236,W,1,08,0.9,30.5,M", b2, sizeof(b2), &m));
    TEST_ASSERT_FALSE(pc_gnss_gga_to_geodetic(&m, &g)); // type[2] != 'A'
}

// pc_gnss_survey_add_gga returns false (and adds nothing) when the GGA carries no valid fix.
void test_survey_add_gga_rejects_bad_fix()
{
    char buf[96];
    Nmea0183 m;
    TEST_ASSERT_TRUE(
        build_parse_gga("GPGGA,124515.00,3723.2475,N,12202.1236,W,0,00,99.9,,M,,M,,", buf, sizeof(buf), &m));
    GnssSurvey s;
    pc_gnss_survey_reset(&s);
    TEST_ASSERT_FALSE(pc_gnss_survey_add_gga(&s, &m));
    TEST_ASSERT_EQUAL_UINT32(0, pc_gnss_survey_count(&s));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_geodetic_to_ecef_matches_pyproj);
    RUN_TEST(test_ecef_to_geodetic_roundtrip);
    RUN_TEST(test_m_to_01mm_rounds_half_away);
    RUN_TEST(test_survey_single_fix_matches_reference);
    RUN_TEST(test_survey_averages_out_scatter);
    RUN_TEST(test_survey_accuracy_clamps_negative_variance);
    RUN_TEST(test_survey_empty_has_no_mean);
    RUN_TEST(test_gga_to_geodetic);
    RUN_TEST(test_gga_no_fix_rejected);
    RUN_TEST(test_survey_add_gga_folds_fix);
    RUN_TEST(test_ecef_to_geodetic_north_pole);
    RUN_TEST(test_ecef_to_geodetic_south_pole);
    RUN_TEST(test_gga_empty_lat_rejected);
    RUN_TEST(test_gga_nonnumeric_lat_rejected);
    RUN_TEST(test_gga_null_lat_field_rejected);
    RUN_TEST(test_gga_empty_lon_rejected);
    RUN_TEST(test_gga_empty_quality_rejected);
    RUN_TEST(test_gga_empty_altitude_rejected);
    RUN_TEST(test_gga_too_few_fields_rejected);
    RUN_TEST(test_gga_southern_eastern_hemisphere);
    RUN_TEST(test_gga_lowercase_hemispheres);
    RUN_TEST(test_gga_empty_hemisphere_fields_not_negated);
    RUN_TEST(test_gga_geoid_absent_defaults_zero);
    RUN_TEST(test_gga_bad_args_and_types_rejected);
    RUN_TEST(test_survey_add_gga_rejects_bad_fix);
    return UNITY_END();
}
