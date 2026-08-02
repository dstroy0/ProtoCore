// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_gnss_survey.h
 * @brief GNSS survey-in: WGS84 geodetic <-> ECEF + fixed-position averaging (PC_ENABLE_NTRIP_CASTER).
 *
 * An RTK / DGPS base must advertise its own antenna position precisely. A stationary receiver's
 * single-epoch fixes scatter by meters, so the base "surveys in": it averages many fixes into one mean
 * position and only trusts it once enough observations have landed and their spread is small enough. That
 * mean position, in ECEF, is what the caster puts in an RTCM3 1005/1006 message (see rtcm3.h).
 *
 * This is the pure, zero-heap, host-tested core:
 *  - the exact closed-form WGS84 geodetic (lat/lon/ellipsoidal-height) -> ECEF transform and an iterative
 *    inverse (for read-back), matched against pyproj (EPSG:4979 -> EPSG:4978);
 *  - an incremental survey accumulator that keeps a running mean ECEF and a 3-D spread (standard
 *    deviation) using a shifted-origin sum / sum-of-squares, so it is numerically stable at the
 *    ~6.4e6 m magnitude of ECEF coordinates and needs no history buffer;
 *  - a helper to fold a GGA fix straight into the survey (built only when PC_ENABLE_NMEA0183 is on).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GNSS_SURVEY_H
#define PROTOCORE_GNSS_SURVEY_H

#include "protocore_config.h"

#if PC_ENABLE_NTRIP_CASTER

/** @brief A WGS84 geodetic position: latitude/longitude in degrees, ellipsoidal height in metres. */
typedef struct
{
    double lat_deg;
    double lon_deg;
    double height_m; ///< height above the WGS84 ellipsoid (NOT mean sea level)
} GnssGeodetic;

/** @brief An Earth-Centerd, Earth-Fixed position in metres (WGS84 / EPSG:4978). */
typedef struct
{
    double x;
    double y;
    double z;
} GnssEcef;

/** @brief WGS84 geodetic -> ECEF (exact closed form). */
void pc_gnss_geodetic_to_ecef(const GnssGeodetic *g, GnssEcef *e);

/** @brief ECEF -> WGS84 geodetic (iterative; converges to sub-millimetre in a few passes). */
void pc_gnss_ecef_to_geodetic(const GnssEcef *e, GnssGeodetic *g);

/** @brief Round metres to RTCM's 0.1 mm integer units (half away from zero) for pc_rtcm3_build_1005/1006. */
int64_t pc_gnss_ecef_m_to_01mm(double metres);

/**
 * @brief Survey-in accumulator: a running mean ECEF and 3-D spread over the fixes fed to it.
 *
 * Coordinates are accumulated relative to the first fix (the origin) so the running sums stay small and
 * the variance is free of the catastrophic cancellation a raw sum-of-squares at ~6.4e6 m would suffer.
 * Zero heap; carries no per-fix history.
 */
typedef struct
{
    proto_bool has_origin; ///< the first fix has been recorded as the shift origin
    double ox;             ///< origin ECEF (metres) - the first fix
    double oy;
    double oz;
    double sdx; ///< sum of (x - origin) over all fixes
    double sdy;
    double sdz;
    double sdx2; ///< sum of (x - origin)^2 over all fixes
    double sdy2;
    double sdz2;
    uint32_t count; ///< number of fixes accumulated
} GnssSurvey;

/** @brief Reset a survey to empty (no fixes). */
void pc_gnss_survey_reset(GnssSurvey *s);

/** @brief Fold one ECEF fix into the survey. */
void pc_gnss_survey_add_ecef(GnssSurvey *s, const GnssEcef *e);

/** @brief Fold one geodetic fix into the survey (converts to ECEF first). */
void pc_gnss_survey_add_geodetic(GnssSurvey *s, const GnssGeodetic *g);

/** @brief Number of fixes accumulated so far. */
uint32_t pc_gnss_survey_count(const GnssSurvey *s);

/** @brief Mean ECEF position. @return false (and leaves @p out untouched) if no fixes yet. */
proto_bool pc_gnss_survey_mean(const GnssSurvey *s, GnssEcef *out);

/**
 * @brief 3-D standard deviation of the accumulated fixes, in metres (sqrt of the summed per-axis
 *        population variances). @return 0 with fewer than two fixes.
 */
double pc_gnss_survey_accuracy_m(const GnssSurvey *s);

/**
 * @brief Whether the survey has converged: at least @p min_obs fixes and a 3-D spread within
 *        @p acc_limit_m metres. Mirrors a u-blox TMODE3 survey-in's min-observations / accuracy-limit gate.
 */
proto_bool pc_gnss_survey_complete(const GnssSurvey *s, uint32_t min_obs, double acc_limit_m);

#if PC_NEED_NMEA0183
struct Nmea0183;

/**
 * @brief Decode a parsed GGA sentence into a geodetic fix (ellipsoidal height = MSL altitude + geoid
 *        separation). @return false if @p m is not a GGA, the fix quality is 0 (no fix), or a field is
 *        missing / malformed.
 */
proto_bool pc_gnss_gga_to_geodetic(const Nmea0183 *m, GnssGeodetic *out);

/** @brief Parse a GGA and, if it carries a valid fix, fold it into the survey. @return true if added. */
proto_bool pc_gnss_survey_add_gga(GnssSurvey *s, const Nmea0183 *m);
#endif

#endif // PC_ENABLE_NTRIP_CASTER

#endif // PROTOCORE_GNSS_SURVEY_H
