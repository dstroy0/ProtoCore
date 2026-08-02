// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_gnss_survey.cpp
 * @brief GNSS survey-in: WGS84 geodetic <-> ECEF + fixed-position averaging. See pc_gnss_survey.h.
 */

#include "services/timing_position/gnss/gnss_survey.h"

#if PC_ENABLE_NTRIP_CASTER

#include <math.h>
#include <string.h>

#if PC_NEED_NMEA0183
#include "services/timing_position/nmea0183/nmea0183.h"
#include "shared_primitives/numparse.h"
#endif

// WGS84 ellipsoid.
static const double WGS84_A = 6378137.0;                  // semi-major axis (m)
static const double WGS84_F = 1.0 / 298.257223563;        // flattening
static const double WGS84_E2 = WGS84_F * (2.0 - WGS84_F); // first eccentricity squared
static const double DEG2RAD = 0.017453292519943295;       // pi / 180
static const double RAD2DEG = 57.29577951308232;          // 180 / pi

void pc_gnss_geodetic_to_ecef(const GnssGeodetic *g, GnssEcef *e)
{
    double lat = g->lat_deg * DEG2RAD;
    double lon = g->lon_deg * DEG2RAD;
    double slat = sin(lat);
    double clat = cos(lat);
    double slon = sin(lon);
    double clon = cos(lon);
    double n = WGS84_A / sqrt(1.0 - WGS84_E2 * slat * slat); // prime vertical radius of curvature
    e->x = (n + g->height_m) * clat * clon;
    e->y = (n + g->height_m) * clat * slon;
    e->z = (n * (1.0 - WGS84_E2) + g->height_m) * slat;
}

void pc_gnss_ecef_to_geodetic(const GnssEcef *e, GnssGeodetic *g)
{
    double p = sqrt(e->x * e->x + e->y * e->y);
    g->lon_deg = atan2(e->y, e->x) * RAD2DEG;
    if (p < 1e-9) // on the polar axis: latitude is +/-90, height is measured along z
    {
        double b = WGS84_A * (1.0 - WGS84_F);
        g->lat_deg = (e->z >= 0.0) ? 90.0 : -90.0;
        g->height_m = fabs(e->z) - b;
        return;
    }
    double lat = atan2(e->z, p * (1.0 - WGS84_E2)); // Bowring-style seed
    double n = WGS84_A;
    for (int i = 0; i < 6; i++) // fixed-point iteration; sub-mm after a few passes
    {
        double slat = sin(lat);
        n = WGS84_A / sqrt(1.0 - WGS84_E2 * slat * slat);
        double h = p / cos(lat) - n;
        lat = atan2(e->z, p * (1.0 - WGS84_E2 * n / (n + h)));
    }
    g->lat_deg = lat * RAD2DEG;
    g->height_m = p / cos(lat) - n;
}

int64_t pc_gnss_ecef_m_to_01mm(double metres)
{
    double v = metres * 10000.0; // metres -> 0.1 mm
    return (int64_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
}

// ---------------------------------------------------------------------------------------------
// Survey-in accumulator.
// ---------------------------------------------------------------------------------------------

void pc_gnss_survey_reset(GnssSurvey *s)
{
    memset(s, 0, sizeof(*s));
}

void pc_gnss_survey_add_ecef(GnssSurvey *s, const GnssEcef *e)
{
    if (!s->has_origin)
    {
        s->has_origin = PROTO_TRUE;
        s->ox = e->x;
        s->oy = e->y;
        s->oz = e->z;
    }
    double dx = e->x - s->ox;
    double dy = e->y - s->oy;
    double dz = e->z - s->oz;
    s->sdx += dx;
    s->sdy += dy;
    s->sdz += dz;
    s->sdx2 += dx * dx;
    s->sdy2 += dy * dy;
    s->sdz2 += dz * dz;
    s->count++;
}

void pc_gnss_survey_add_geodetic(GnssSurvey *s, const GnssGeodetic *g)
{
    GnssEcef e;
    pc_gnss_geodetic_to_ecef(g, &e);
    pc_gnss_survey_add_ecef(s, &e);
}

uint32_t pc_gnss_survey_count(const GnssSurvey *s)
{
    return s->count;
}

proto_bool pc_gnss_survey_mean(const GnssSurvey *s, GnssEcef *out)
{
    if (s->count == 0)
    {
        return PROTO_FALSE;
    }
    double n = (double)s->count;
    out->x = s->ox + s->sdx / n;
    out->y = s->oy + s->sdy / n;
    out->z = s->oz + s->sdz / n;
    return PROTO_TRUE;
}

double pc_gnss_survey_accuracy_m(const GnssSurvey *s)
{
    if (s->count < 2)
    {
        return 0.0;
    }
    double n = (double)s->count;
    double mx = s->sdx / n;
    double my = s->sdy / n;
    double mz = s->sdz / n;
    double vx = s->sdx2 / n - mx * mx; // population variance of the shifted coordinates
    double vy = s->sdy2 / n - my * my;
    double vz = s->sdz2 / n - mz * mz;
    if (vx < 0.0) // clamp tiny negatives from floating-point rounding
    {
        vx = 0.0;
    }
    if (vy < 0.0)
    {
        vy = 0.0;
    }
    if (vz < 0.0)
    {
        vz = 0.0;
    }
    return sqrt(vx + vy + vz);
}

proto_bool pc_gnss_survey_complete(const GnssSurvey *s, uint32_t min_obs, double acc_limit_m)
{
    return s->count >= min_obs && s->count >= 2 && pc_gnss_survey_accuracy_m(s) <= acc_limit_m;
}

// ---------------------------------------------------------------------------------------------
// GGA -> geodetic (only when the NMEA 0183 codec is available).
// ---------------------------------------------------------------------------------------------

#if PC_NEED_NMEA0183

// A GGA lat/lon field is ddmm.mmmm / dddmm.mmmm; split into whole degrees + decimal minutes.
static proto_bool dm_to_deg(const char *field, uint8_t len, double *out)
{
    if (!field || len == 0)
    {
        return PROTO_FALSE;
    }
    const char *end = field;
    double dm = pc_strtod(field, &end);
    if (end == field)
    {
        return PROTO_FALSE;
    }
    double deg = (double)(long)(dm / 100.0); // whole degrees (field is non-negative; sign is N/S,E/W)
    double minutes = dm - deg * 100.0;
    *out = deg + minutes / 60.0;
    return PROTO_TRUE;
}

proto_bool pc_gnss_gga_to_geodetic(const Nmea0183 *m, GnssGeodetic *out)
{
    if (!m || !out || m->type[0] != 'G' || m->type[1] != 'G' || m->type[2] != 'A')
    {
        return PROTO_FALSE;
    }
    if (m->field_count < 10) // need through the altitude field (index 9)
    {
        return PROTO_FALSE;
    }

    long quality = 0;
    if (!pc_nmea0183_field_int(m, 6, &quality) || quality <= 0) // 0 = no fix
    {
        return PROTO_FALSE;
    }

    double lat = 0.0;
    double lon = 0.0;
    if (!dm_to_deg(m->fields[2], m->field_len[2], &lat))
    {
        return PROTO_FALSE;
    }
    if (!dm_to_deg(m->fields[4], m->field_len[4], &lon))
    {
        return PROTO_FALSE;
    }
    if (m->field_len[3] == 1 && (m->fields[3][0] == 'S' || m->fields[3][0] == 's'))
    {
        lat = -lat;
    }
    if (m->field_len[5] == 1 && (m->fields[5][0] == 'W' || m->fields[5][0] == 'w'))
    {
        lon = -lon;
    }

    float msl = 0.0f;
    if (!pc_nmea0183_field_float(m, 9, &msl)) // orthometric (mean-sea-level) altitude
    {
        return PROTO_FALSE;
    }
    float geoid = 0.0f;
    pc_nmea0183_field_float(m, 11, &geoid); // geoid separation; absent -> 0 (ellipsoidal height = MSL)

    out->lat_deg = lat;
    out->lon_deg = lon;
    out->height_m = (double)msl + (double)geoid; // ellipsoidal height above WGS84
    return PROTO_TRUE;
}

proto_bool pc_gnss_survey_add_gga(GnssSurvey *s, const Nmea0183 *m)
{
    GnssGeodetic g;
    if (!pc_gnss_gga_to_geodetic(m, &g))
    {
        return PROTO_FALSE;
    }
    pc_gnss_survey_add_geodetic(s, &g);
    return PROTO_TRUE;
}

#endif // PC_NEED_NMEA0183

#endif // PC_ENABLE_NTRIP_CASTER
