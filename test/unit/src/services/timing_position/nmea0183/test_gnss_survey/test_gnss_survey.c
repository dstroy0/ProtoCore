// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the GNSS survey-in core (services/timing_position/gnss/gnss_survey.h).
//
// WGS84 publishes its defining parameters as exact numbers: semi-major axis a = 6378137.0 m and
// flattening 1/f = 298.257223563, from which the semi-minor axis b = a(1-f) = 6356752.3142 m follows.
// test_wgs84_published_axes is the load-bearing case: the geodetic-to-ECEF transform reduces at the
// equator and at the pole to exactly those two published lengths, so an ellipsoid with the wrong
// axes, the wrong eccentricity or a degree/radian slip cannot pass it. Everything else is either
// arithmetic derived from the closed form or a property (round trip, mean, spread) that must hold
// whatever the implementation.

#include "services/timing_position/gnss/gnss_survey.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// WGS84's two defining lengths (NGA TR8350.2): a is exact by definition, b follows from 1/f.
#define WGS84_A 6378137.0
#define WGS84_B 6356752.3142

static double dabs(double v)
{
    return v < 0 ? -v : v;
}

static void near_m(double want, double got, double tol)
{
    TEST_ASSERT_TRUE(dabs(want - got) <= tol);
}

// The load-bearing case. At latitude 0 the prime-vertical radius is a and the height is added along
// the equatorial radius, so a point on the equator at height 0 sits at distance a from the center;
// at latitude 90 the same closed form reduces to the semi-minor axis b.
void test_wgs84_published_axes(void)
{
    GnssGeodetic g;
    GnssEcef e;

    g.lat_deg = 0.0;
    g.lon_deg = 0.0;
    g.height_m = 0.0;
    protocore_gnss_geodetic_to_ecef(&g, &e);
    near_m(WGS84_A, e.x, 1e-3);
    near_m(0.0, e.y, 1e-6);
    near_m(0.0, e.z, 1e-6);

    g.lon_deg = 90.0;
    protocore_gnss_geodetic_to_ecef(&g, &e);
    near_m(0.0, e.x, 1e-6);
    near_m(WGS84_A, e.y, 1e-3);
    near_m(0.0, e.z, 1e-6);

    g.lon_deg = 180.0;
    protocore_gnss_geodetic_to_ecef(&g, &e);
    near_m(-WGS84_A, e.x, 1e-3);
    near_m(0.0, e.y, 1e-6);

    g.lon_deg = -90.0;
    protocore_gnss_geodetic_to_ecef(&g, &e);
    near_m(-WGS84_A, e.y, 1e-3);

    g.lat_deg = 90.0;
    g.lon_deg = 0.0;
    protocore_gnss_geodetic_to_ecef(&g, &e);
    near_m(0.0, e.x, 1e-6);
    near_m(0.0, e.y, 1e-6);
    near_m(WGS84_B, e.z, 1e-3);

    g.lat_deg = -90.0;
    protocore_gnss_geodetic_to_ecef(&g, &e);
    near_m(-WGS84_B, e.z, 1e-3);
}

// Height is measured along the ellipsoid normal, which at the equator and at the pole is the axis
// itself, so it adds straight onto a and onto b.
void test_height_adds_along_the_normal_at_the_axes(void)
{
    GnssGeodetic g;
    GnssEcef e;

    g.lat_deg = 0.0;
    g.lon_deg = 0.0;
    g.height_m = 1000.0;
    protocore_gnss_geodetic_to_ecef(&g, &e);
    near_m(WGS84_A + 1000.0, e.x, 1e-3);

    g.height_m = -500.0;
    protocore_gnss_geodetic_to_ecef(&g, &e);
    near_m(WGS84_A - 500.0, e.x, 1e-3);

    g.lat_deg = 90.0;
    g.height_m = 250.0;
    protocore_gnss_geodetic_to_ecef(&g, &e);
    near_m(WGS84_B + 250.0, e.z, 1e-3);
}

// The ellipsoid is a surface of revolution about the Z axis, so rotating the longitude rotates the
// XY pair and leaves Z and the equatorial distance untouched.
void test_longitude_only_rotates_about_the_axis(void)
{
    GnssGeodetic g;
    GnssEcef a;
    GnssEcef b;
    g.lat_deg = 37.5;
    g.height_m = 120.0;

    g.lon_deg = 0.0;
    protocore_gnss_geodetic_to_ecef(&g, &a);
    g.lon_deg = 137.0;
    protocore_gnss_geodetic_to_ecef(&g, &b);

    near_m(a.z, b.z, 1e-6);
    const double ra = a.x * a.x + a.y * a.y;
    const double rb = b.x * b.x + b.y * b.y;
    TEST_ASSERT_TRUE(dabs(ra - rb) < 1.0); // equal equatorial radius, to a square metre

    // The northern and southern hemispheres are mirror images across the equator.
    GnssEcef n;
    GnssEcef s;
    g.lon_deg = 12.0;
    g.lat_deg = 45.0;
    protocore_gnss_geodetic_to_ecef(&g, &n);
    g.lat_deg = -45.0;
    protocore_gnss_geodetic_to_ecef(&g, &s);
    near_m(n.x, s.x, 1e-6);
    near_m(n.y, s.y, 1e-6);
    near_m(-n.z, s.z, 1e-6);
}

// The inverse must undo the forward transform: a round trip returns the same position to well
// inside a millimetre, over the whole latitude range and both signs of height.
void test_geodetic_ecef_round_trip(void)
{
    static const double LAT[7] = {0.0, 1e-7, 45.0, -45.0, 60.0, 89.0, -89.9};
    static const double LON[7] = {0.0, 179.9, -179.9, 90.0, -90.0, 12.34567, -100.0};
    static const double H[7] = {0.0, 100.0, -50.0, 8848.0, 1000.0, -400.0, 20000.0};

    for (unsigned i = 0; i < 7; i++)
    {
        GnssGeodetic g = {LAT[i], LON[i], H[i]};
        GnssEcef e;
        GnssGeodetic back;
        protocore_gnss_geodetic_to_ecef(&g, &e);
        protocore_gnss_ecef_to_geodetic(&e, &back);

        TEST_ASSERT_TRUE(dabs(back.lat_deg - g.lat_deg) < 1e-9);
        TEST_ASSERT_TRUE(dabs(back.lon_deg - g.lon_deg) < 1e-9);
        near_m(g.height_m, back.height_m, 1e-4);

        // And the other way: ECEF -> geodetic -> ECEF returns the same point.
        GnssEcef again;
        protocore_gnss_geodetic_to_ecef(&back, &again);
        near_m(e.x, again.x, 1e-4);
        near_m(e.y, again.y, 1e-4);
        near_m(e.z, again.z, 1e-4);
    }
}

// RTCM carries ECEF in 0.1 mm integer units, rounded half away from zero.
void test_metres_to_tenth_millimetres(void)
{
    TEST_ASSERT_EQUAL_INT64(0ll, protocore_gnss_ecef_m_to_01mm(0.0));
    TEST_ASSERT_EQUAL_INT64(10000ll, protocore_gnss_ecef_m_to_01mm(1.0));
    TEST_ASSERT_EQUAL_INT64(-10000ll, protocore_gnss_ecef_m_to_01mm(-1.0));
    TEST_ASSERT_EQUAL_INT64(1ll, protocore_gnss_ecef_m_to_01mm(0.0001));
    TEST_ASSERT_EQUAL_INT64(63781370000ll, protocore_gnss_ecef_m_to_01mm(WGS84_A));

    // Half rounds away from zero in both directions.
    TEST_ASSERT_EQUAL_INT64(1ll, protocore_gnss_ecef_m_to_01mm(0.00005));
    TEST_ASSERT_EQUAL_INT64(-1ll, protocore_gnss_ecef_m_to_01mm(-0.00005));
    TEST_ASSERT_EQUAL_INT64(0ll, protocore_gnss_ecef_m_to_01mm(0.00004));
    TEST_ASSERT_EQUAL_INT64(0ll, protocore_gnss_ecef_m_to_01mm(-0.00004));
}

// A survey with no fixes has no mean and no spread; the first fix becomes the mean exactly.
void test_survey_starts_empty(void)
{
    GnssSurvey s;
    protocore_gnss_survey_reset(&s);
    TEST_ASSERT_EQUAL_UINT32(0, protocore_gnss_survey_count(&s));
    GnssEcef mean = {1.0, 2.0, 3.0};
    TEST_ASSERT_FALSE(protocore_gnss_survey_mean(&s, &mean));
    TEST_ASSERT_TRUE(mean.x == 1.0); // untouched
    TEST_ASSERT_TRUE(protocore_gnss_survey_accuracy_m(&s) == 0.0);

    GnssEcef one = {1000.0, 2000.0, 3000.0};
    protocore_gnss_survey_add_ecef(&s, &one);
    TEST_ASSERT_EQUAL_UINT32(1, protocore_gnss_survey_count(&s));
    TEST_ASSERT_TRUE(protocore_gnss_survey_mean(&s, &mean));
    near_m(1000.0, mean.x, 1e-9);
    near_m(2000.0, mean.y, 1e-9);
    near_m(3000.0, mean.z, 1e-9);
    // A single fix has no spread to report.
    TEST_ASSERT_TRUE(protocore_gnss_survey_accuracy_m(&s) == 0.0);
}

// The mean is the arithmetic mean of the fixes fed in, at the ~6.4e6 m magnitude ECEF coordinates
// actually have, and the spread is the population standard deviation summed over the three axes.
// Two fixes at +/- d on one axis have mean at the midpoint and per-axis variance d^2, so the 3-D
// spread is exactly d.
void test_survey_mean_and_spread(void)
{
    GnssSurvey s;
    protocore_gnss_survey_reset(&s);
    const double base = 6378137.0;
    GnssEcef a = {base - 3.0, 100.0, -200.0};
    GnssEcef b = {base + 3.0, 100.0, -200.0};
    protocore_gnss_survey_add_ecef(&s, &a);
    protocore_gnss_survey_add_ecef(&s, &b);

    GnssEcef mean;
    TEST_ASSERT_TRUE(protocore_gnss_survey_mean(&s, &mean));
    near_m(base, mean.x, 1e-6);
    near_m(100.0, mean.y, 1e-6);
    near_m(-200.0, mean.z, 1e-6);
    near_m(3.0, protocore_gnss_survey_accuracy_m(&s), 1e-6);

    // Repeating the identical fix leaves the mean where it is and drives the spread to zero.
    GnssSurvey t;
    protocore_gnss_survey_reset(&t);
    for (unsigned i = 0; i < 64; i++)
    {
        GnssEcef same = {base, -1234567.0, 4321.5};
        protocore_gnss_survey_add_ecef(&t, &same);
    }
    TEST_ASSERT_EQUAL_UINT32(64, protocore_gnss_survey_count(&t));
    TEST_ASSERT_TRUE(protocore_gnss_survey_mean(&t, &mean));
    near_m(base, mean.x, 1e-6);
    near_m(-1234567.0, mean.y, 1e-6);
    near_m(4321.5, mean.z, 1e-6);
    near_m(0.0, protocore_gnss_survey_accuracy_m(&t), 1e-6);
}

// A symmetric scatter about a point averages back to that point however many fixes arrive, and the
// spread stays the scatter's own standard deviation rather than growing with the count.
void test_survey_averages_a_symmetric_scatter(void)
{
    GnssSurvey s;
    protocore_gnss_survey_reset(&s);
    const double cx = 6378137.0;
    const double cy = -1000000.0;
    const double cz = 4000000.0;
    for (int k = -50; k <= 50; k++)
    {
        GnssEcef e = {cx + (double)k, cy - (double)k, cz};
        protocore_gnss_survey_add_ecef(&s, &e);
    }
    TEST_ASSERT_EQUAL_UINT32(101, protocore_gnss_survey_count(&s));

    GnssEcef mean;
    TEST_ASSERT_TRUE(protocore_gnss_survey_mean(&s, &mean));
    near_m(cx, mean.x, 1e-6);
    near_m(cy, mean.y, 1e-6);
    near_m(cz, mean.z, 1e-6);

    // Per axis the population variance of -50..50 is sum(k^2)/101 = 85850/101 = 850, and two axes
    // carry it, so the 3-D spread is sqrt(1700) = 41.2310563...
    near_m(41.2310563, protocore_gnss_survey_accuracy_m(&s), 1e-4);
}

// The completion gate needs both a minimum count and a spread inside the accuracy limit, exactly as
// a u-blox TMODE3 survey-in does.
void test_survey_completion_gate(void)
{
    GnssSurvey s;
    protocore_gnss_survey_reset(&s);
    TEST_ASSERT_FALSE(protocore_gnss_survey_complete(&s, 1, 100.0)); // nothing yet

    const double base = 6378137.0;
    for (unsigned i = 0; i < 10; i++)
    {
        GnssEcef e = {base, 0.0, 0.0};
        protocore_gnss_survey_add_ecef(&s, &e);
    }
    TEST_ASSERT_TRUE(protocore_gnss_survey_complete(&s, 10, 0.01));
    TEST_ASSERT_FALSE(protocore_gnss_survey_complete(&s, 11, 0.01)); // too few observations

    // One wild fix widens the spread past a tight limit without changing the count requirement.
    GnssEcef outlier = {base + 100.0, 0.0, 0.0};
    protocore_gnss_survey_add_ecef(&s, &outlier);
    TEST_ASSERT_FALSE(protocore_gnss_survey_complete(&s, 10, 0.01));
    TEST_ASSERT_TRUE(protocore_gnss_survey_complete(&s, 10, 100.0));
}

// A geodetic fix folded in must land in the same place the explicit ECEF conversion does.
void test_survey_accepts_geodetic_fixes(void)
{
    GnssGeodetic g = {44.0690060, -121.3143268, 1091.7};
    GnssEcef direct;
    protocore_gnss_geodetic_to_ecef(&g, &direct);

    GnssSurvey s;
    protocore_gnss_survey_reset(&s);
    protocore_gnss_survey_add_geodetic(&s, &g);
    GnssEcef mean;
    TEST_ASSERT_TRUE(protocore_gnss_survey_mean(&s, &mean));
    near_m(direct.x, mean.x, 1e-6);
    near_m(direct.y, mean.y, 1e-6);
    near_m(direct.z, mean.z, 1e-6);
}

// The GGA fold: the ddmm.mmmm coordinates become signed decimal degrees and the ellipsoidal height
// is the mean-sea-level altitude plus the geoid separation the sentence carries. For
// 1113.0 m MSL with a separation of -21.3 m that is 1091.7 m.
void test_gga_folds_into_a_geodetic_fix(void)
{
    static const char *const GGA = "$GNGGA,001043.00,4404.14036,N,12118.85961,W,1,12,0.98,1113.0,M,-21.3,M,,*47";
    Nmea0183 m;
    TEST_ASSERT_TRUE(protocore_nmea0183_parse(GGA, strlen(GGA), &m));

    GnssGeodetic g;
    TEST_ASSERT_TRUE(protocore_gnss_gga_to_geodetic(&m, &g));
    TEST_ASSERT_TRUE(dabs(g.lat_deg - 44.0690060) < 2e-5);     // 44 + 4.14036/60
    TEST_ASSERT_TRUE(dabs(g.lon_deg - (-121.3143268)) < 2e-5); // -(121 + 18.85961/60)
    near_m(1091.7, g.height_m, 0.01);

    GnssSurvey s;
    protocore_gnss_survey_reset(&s);
    TEST_ASSERT_TRUE(protocore_gnss_survey_add_gga(&s, &m));
    TEST_ASSERT_EQUAL_UINT32(1, protocore_gnss_survey_count(&s));
}

// A sentence with no fix carries no position to survey: fix quality 0 is refused, as is a sentence
// that is not a GGA at all.
void test_gga_without_a_fix_is_refused(void)
{
    char buf[128];
    Nmea0183 m;
    GnssGeodetic g;
    GnssSurvey s;
    protocore_gnss_survey_reset(&s);

    TEST_ASSERT_GREATER_THAN_UINT32(
        0, (uint32_t)protocore_nmea0183_build(buf, sizeof(buf),
                                              "GNGGA,001043.00,4404.14036,N,12118.85961,W,0,00,,,M,,M,,"));
    TEST_ASSERT_TRUE(protocore_nmea0183_parse(buf, strlen(buf), &m));
    TEST_ASSERT_FALSE(protocore_gnss_gga_to_geodetic(&m, &g));
    TEST_ASSERT_FALSE(protocore_gnss_survey_add_gga(&s, &m));
    TEST_ASSERT_EQUAL_UINT32(0, protocore_gnss_survey_count(&s));

    static const char *const RMC = "$GNRMC,001031.00,A,4404.13993,N,12118.86023,W,0.146,,100117,,,A*7B";
    TEST_ASSERT_TRUE(protocore_nmea0183_parse(RMC, strlen(RMC), &m));
    TEST_ASSERT_FALSE(protocore_gnss_gga_to_geodetic(&m, &g));
    TEST_ASSERT_FALSE(protocore_gnss_survey_add_gga(&s, &m));
}
