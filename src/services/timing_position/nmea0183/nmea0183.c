// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nmea0183.c
 * @brief NMEA 0183 sentence codec (pure, host-tested).
 */

#include "services/timing_position/nmea0183/nmea0183.h"

#if PC_NEED_NMEA0183

#include "shared_primitives/numparse.h"
#include <string.h>

uint8_t pc_nmea0183_checksum(const char *s, size_t len)
{
    uint8_t cs = 0;
    for (size_t i = 0; i < len; i++)
    {
        cs ^= (uint8_t)s[i];
    }
    return cs;
}

static char hex_digit(uint8_t v)
{
    v &= 0x0Fu;
    return (char)(v < 10 ? ('0' + v) : ('A' + v - 10));
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return -1;
}

size_t pc_nmea0183_build(char *buf, size_t cap, const char *body)
{
    if (!buf || !body)
    {
        return 0;
    }
    size_t blen = strnlen(body, cap);
    size_t total = 1 + blen + 1 + 2 + 2; // '$' + body + '*' + HH + CRLF
    if (cap < total + 1)                 // + NUL
    {
        return 0;
    }
    uint8_t cs = pc_nmea0183_checksum(body, blen);
    size_t p = 0;
    buf[p++] = '$';
    memcpy(buf + p, body, blen);
    p += blen;
    buf[p++] = '*';
    buf[p++] = hex_digit((uint8_t)(cs >> 4));
    buf[p++] = hex_digit(cs);
    buf[p++] = '\r';
    buf[p++] = '\n';
    buf[p] = '\0';
    return p;
}

proto_bool pc_nmea0183_parse(const char *s, size_t len, Nmea0183 *out)
{
    if (!s || !out || len < 4 || (s[0] != '$' && s[0] != '!'))
    {
        return PROTO_FALSE;
    }

    // Find the '*' that introduces the checksum, stopping at any CR/LF.
    size_t star = 0;
    proto_bool found = PROTO_FALSE;
    for (size_t i = 1; i < len; i++)
    {
        if (s[i] == '*')
        {
            star = i;
            found = PROTO_TRUE;
            break;
        }
        if (s[i] == '\r' || s[i] == '\n')
        {
            break;
        }
    }
    if (!found || star + 2 >= len) // need two checksum hex digits after '*'
    {
        return PROTO_FALSE;
    }

    int hi = hex_val(s[star + 1]);
    int lo = hex_val(s[star + 2]);
    if (hi < 0 || lo < 0)
    {
        return PROTO_FALSE;
    }
    uint8_t expect = (uint8_t)((hi << 4) | lo);
    if (pc_nmea0183_checksum(s + 1, star - 1) != expect)
    {
        return PROTO_FALSE;
    }

    // Split the payload s[1..star-1] on commas (field 0 is the address).
    uint8_t fc = 0;
    size_t fstart = 1;
    for (size_t i = 1; i <= star; i++)
    {
        if (i == star || s[i] == ',')
        {
            if (fc < PC_NMEA0183_MAX_FIELDS)
            {
                out->fields[fc] = s + fstart;
                out->field_len[fc] = (uint8_t)(i - fstart);
                fc++;
            }
            fstart = i + 1;
        }
    }
    out->field_count = fc;

    // Derive talker / type from the address field (field 0).
    memset(out->talker, 0, sizeof(out->talker));
    memset(out->type, 0, sizeof(out->type));
    if (fc > 0)
    // point) guarantees star >= 1 (it is only ever set inside the search loop, which
    // starts at i = 1), so the split loop's `i <= star` bound guarantees an i == star
    // iteration, whose `i == star || ...` test is true unconditionally. PC_NMEA0183_
    // MAX_FIELDS is a positive compile-time constant (26, not overridden by any build
    // in this tree), so the *first* field-boundary event the split loop reaches --
    // whether an earlier comma or i == star itself -- always satisfies
    // `fc < PC_NMEA0183_MAX_FIELDS` and increments fc from 0. Hence fc >= 1 whenever
    // this line runs.
    {
        uint8_t al = out->field_len[0];
        const char *a = out->fields[0];
        for (uint8_t i = 0; i < 2 && i < al; i++)
        {
            out->talker[i] = a[i];
        }
        for (uint8_t i = 0; i < 3 && (uint8_t)(2 + i) < al; i++)
        {
            out->type[i] = a[2 + i];
        }
    }
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_field_float(const Nmea0183 *m, uint8_t idx, float *out)
{
    if (!m || !out || idx >= m->field_count || m->field_len[idx] == 0)
    {
        return PROTO_FALSE;
    }
    const char *end = m->fields[idx];
    // The field is delimited by a ',' or '*' in the source, so pc_strtof stops at the field end.
    float v = pc_strtof(m->fields[idx], &end);
    if (end == m->fields[idx])
    {
        return PROTO_FALSE;
    }
    *out = v;
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_field_int(const Nmea0183 *m, uint8_t idx, long *out)
{
    if (!m || !out || idx >= m->field_count || m->field_len[idx] == 0)
    {
        return PROTO_FALSE;
    }
    const char *end = m->fields[idx];
    long v = pc_strtol(m->fields[idx], &end);
    if (end == m->fields[idx])
    {
        return PROTO_FALSE;
    }
    *out = v;
    return PROTO_TRUE;
}

// ddmm.mmmm + a hemisphere char -> signed decimal degrees (float precision on the raw field).
static double nmea_coord(float ddmm, char hemi)
{
    int deg = (int)(ddmm / 100.0f);
    double dec = (double)deg + ((double)ddmm - (double)deg * 100.0) / 60.0;
    if (hemi == 'S' || hemi == 's' || hemi == 'W' || hemi == 'w')
    {
        dec = -dec;
    }
    return dec;
}

// Split an hhmmss.ss time field into hour / minute / second (all 0 on a too-short field).
static void nmea_time(const Nmea0183 *m, uint8_t idx, uint8_t *h, uint8_t *mi, float *s)
{
    *h = 0;
    *mi = 0;
    *s = 0.0f;
    if (idx >= m->field_count || m->field_len[idx] < 6)
    {
        return;
    }
    const char *f = m->fields[idx];
    *h = (uint8_t)((f[0] - '0') * 10 + (f[1] - '0'));
    *mi = (uint8_t)((f[2] - '0') * 10 + (f[3] - '0'));
    const char *end = f + 4;
    *s = pc_strtof(f + 4, &end); // ss.ss, stopped at the ',' / '*' delimiter
}

// Split a ddmmyy date field into day / month / (2-digit) year.
static void nmea_date(const Nmea0183 *m, uint8_t idx, uint8_t *d, uint8_t *mo, uint8_t *y)
{
    *d = 0;
    *mo = 0;
    *y = 0;
    if (idx >= m->field_count || m->field_len[idx] < 6)
    {
        return;
    }
    const char *f = m->fields[idx];
    *d = (uint8_t)((f[0] - '0') * 10 + (f[1] - '0'));
    *mo = (uint8_t)((f[2] - '0') * 10 + (f[3] - '0'));
    *y = (uint8_t)((f[4] - '0') * 10 + (f[5] - '0'));
}

proto_bool pc_nmea0183_parse_gga(const Nmea0183 *m, pc_nmea_gga *out)
{
    if (!m || !out || strcmp(m->type, "GGA") != 0 || m->field_count < 10) // need through altitude (field 9)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    nmea_time(m, 1, &out->hour, &out->minute, &out->second);
    float lat = 0.0f, lon = 0.0f;
    if (pc_nmea0183_field_float(m, 2, &lat) && m->field_len[3] >= 1)
    {
        out->lat_deg = nmea_coord(lat, m->fields[3][0]);
    }
    if (pc_nmea0183_field_float(m, 4, &lon) && m->field_len[5] >= 1)
    {
        out->lon_deg = nmea_coord(lon, m->fields[5][0]);
    }
    long q = 0;
    if (pc_nmea0183_field_int(m, 6, &q))
    {
        out->fix_quality = (uint8_t)q;
    }
    long ns = 0;
    if (pc_nmea0183_field_int(m, 7, &ns))
    {
        out->num_sats = (uint8_t)ns;
    }
    pc_nmea0183_field_float(m, 8, &out->hdop);  // stays 0 if empty (out was zeroed)
    pc_nmea0183_field_float(m, 9, &out->alt_m); // altitude MSL, field 10 is the 'M' unit
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_parse_rmc(const Nmea0183 *m, pc_nmea_rmc *out)
{
    if (!m || !out || strcmp(m->type, "RMC") != 0 || m->field_count < 10) // need through date (field 9)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    nmea_time(m, 1, &out->hour, &out->minute, &out->second);
    out->valid = (m->field_len[2] >= 1 && (m->fields[2][0] == 'A' || m->fields[2][0] == 'a'));
    float lat = 0.0f, lon = 0.0f;
    if (pc_nmea0183_field_float(m, 3, &lat) && m->field_len[4] >= 1)
    {
        out->lat_deg = nmea_coord(lat, m->fields[4][0]);
    }
    if (pc_nmea0183_field_float(m, 5, &lon) && m->field_len[6] >= 1)
    {
        out->lon_deg = nmea_coord(lon, m->fields[6][0]);
    }
    pc_nmea0183_field_float(m, 7, &out->speed_knots);
    pc_nmea0183_field_float(m, 8, &out->course_deg);
    nmea_date(m, 9, &out->day, &out->month, &out->year);
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_parse_gsv(const Nmea0183 *m, pc_nmea_gsv *out)
{
    if (!m || !out || strcmp(m->type, "GSV") != 0 || m->field_count < 4) // header: totalMsgs, msgNum, satsInView
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    long v = 0;
    if (pc_nmea0183_field_int(m, 1, &v))
    {
        out->total_msgs = (uint8_t)v;
    }
    if (pc_nmea0183_field_int(m, 2, &v))
    {
        out->msg_num = (uint8_t)v;
    }
    if (pc_nmea0183_field_int(m, 3, &v))
    {
        out->sats_in_view = (uint8_t)v;
    }

    // Satellite records begin at field 4, four fields each (PRN, elevation, azimuth, SNR).
    uint8_t records = (uint8_t)((m->field_count - 4) / 4);
    if (records > 4)
    {
        records = 4;
    }
    out->sat_count = records;
    for (uint8_t i = 0; i < records; i++)
    {
        uint8_t base = (uint8_t)(4 + i * 4);
        pc_nmea_gsv_sat *s = &out->sats[i];
        if (pc_nmea0183_field_int(m, base, &v))
        {
            s->prn = (uint8_t)v;
        }
        if (pc_nmea0183_field_int(m, (uint8_t)(base + 1), &v))
        {
            s->elev_deg = (int16_t)v;
        }
        if (pc_nmea0183_field_int(m, (uint8_t)(base + 2), &v))
        {
            s->azim_deg = (int16_t)v;
        }
        // SNR is blank for a satellite in view but not tracked.
        s->snr_valid = pc_nmea0183_field_int(m, (uint8_t)(base + 3), &v);
        if (s->snr_valid)
        {
            s->snr_db = (uint8_t)v;
        }
    }
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_parse_zda(const Nmea0183 *m, pc_nmea_zda *out)
{
    if (!m || !out || strcmp(m->type, "ZDA") != 0 || m->field_count < 5) // need through year (field 4)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    nmea_time(m, 1, &out->hour, &out->minute, &out->second);
    long v = 0;
    if (pc_nmea0183_field_int(m, 2, &v))
    {
        out->day = (uint8_t)v;
    }
    if (pc_nmea0183_field_int(m, 3, &v))
    {
        out->month = (uint8_t)v;
    }
    if (pc_nmea0183_field_int(m, 4, &v))
    {
        out->year = (uint16_t)v;
    }
    // The local-zone offset is optional; a receiver commonly leaves both fields blank.
    if (pc_nmea0183_field_int(m, 5, &v))
    {
        out->zone_hours = (int8_t)v;
    }
    if (pc_nmea0183_field_int(m, 6, &v))
    {
        out->zone_minutes = (uint8_t)v;
    }
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_parse_vtg(const Nmea0183 *m, pc_nmea_vtg *out)
{
    if (!m || !out || strcmp(m->type, "VTG") != 0 || m->field_count < 9) // need through the km/h unit (field 8)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    pc_nmea0183_field_float(m, 1, &out->course_true_deg); // stays 0 if empty (out was zeroed)
    pc_nmea0183_field_float(m, 3, &out->course_mag_deg);
    pc_nmea0183_field_float(m, 5, &out->speed_knots);
    pc_nmea0183_field_float(m, 7, &out->speed_kmh);
    // The mode indicator (field 9) is NMEA 2.3+; older receivers omit it.
    if (m->field_count > 9 && m->field_len[9] >= 1)
    {
        out->mode = m->fields[9][0];
    }
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_parse_gsa(const Nmea0183 *m, pc_nmea_gsa *out)
{
    if (!m || !out || strcmp(m->type, "GSA") != 0 || m->field_count < 18) // need through VDOP (field 17)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    if (m->field_len[1] >= 1)
    {
        out->mode = m->fields[1][0];
    }
    long v = 0;
    if (pc_nmea0183_field_int(m, 2, &v))
    {
        out->fix_type = (uint8_t)v;
    }
    // Fields 3..14 are the (up to 12) PRNs used; unused slots are blank and skipped.
    uint8_t cnt = 0;
    for (uint8_t f = 3; f <= 14; f++)
    {
        if (pc_nmea0183_field_int(m, f, &v))
        {
            out->sats[cnt++] = (uint8_t)v;
        }
    }
    out->sat_count = cnt;
    pc_nmea0183_field_float(m, 15, &out->pdop);
    pc_nmea0183_field_float(m, 16, &out->hdop);
    pc_nmea0183_field_float(m, 17, &out->vdop);
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_parse_mwv(const Nmea0183 *m, pc_nmea_mwv *out)
{
    if (!m || !out || strcmp(m->type, "MWV") != 0 || m->field_count < 6) // need through status (field 5)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    pc_nmea0183_field_float(m, 1, &out->wind_angle_deg); // stays 0 if empty (out was zeroed)
    if (m->field_len[2] >= 1)
    {
        out->reference = m->fields[2][0];
    }
    pc_nmea0183_field_float(m, 3, &out->wind_speed);
    if (m->field_len[4] >= 1)
    {
        out->speed_units = m->fields[4][0];
    }
    out->valid = (m->field_len[5] >= 1 && (m->fields[5][0] == 'A' || m->fields[5][0] == 'a'));
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_parse_dpt(const Nmea0183 *m, pc_nmea_dpt *out)
{
    if (!m || !out || strcmp(m->type, "DPT") != 0 || m->field_count < 3) // need depth (field 1) + offset (field 2)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    pc_nmea0183_field_float(m, 1, &out->depth_m); // stays 0 if empty (out was zeroed)
    pc_nmea0183_field_float(m, 2, &out->offset_m);
    out->has_range = pc_nmea0183_field_float(m, 3, &out->range_m); // the range scale is optional
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_parse_hdg(const Nmea0183 *m, pc_nmea_hdg *out)
{
    if (!m || !out || strcmp(m->type, "HDG") != 0 || m->field_count < 6) // heading + dev + dir + var + dir
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    pc_nmea0183_field_float(m, 1, &out->heading_deg);
    float dev = 0.0f;
    if (pc_nmea0183_field_float(m, 2, &dev)) // field 3 is the E/W direction (West -> negative)
    {
        out->deviation_deg = (m->field_len[3] >= 1 && (m->fields[3][0] == 'W' || m->fields[3][0] == 'w')) ? -dev : dev;
    }
    float var = 0.0f;
    if (pc_nmea0183_field_float(m, 4, &var)) // field 5 is the E/W direction
    {
        out->variation_deg = (m->field_len[5] >= 1 && (m->fields[5][0] == 'W' || m->fields[5][0] == 'w')) ? -var : var;
    }
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_parse_gll(const Nmea0183 *m, pc_nmea_gll *out)
{
    if (!m || !out || strcmp(m->type, "GLL") != 0 || m->field_count < 7) // need through status (field 6)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    float lat = 0.0f, lon = 0.0f;
    if (pc_nmea0183_field_float(m, 1, &lat) && m->field_len[2] >= 1)
    {
        out->lat_deg = nmea_coord(lat, m->fields[2][0]);
    }
    if (pc_nmea0183_field_float(m, 3, &lon) && m->field_len[4] >= 1)
    {
        out->lon_deg = nmea_coord(lon, m->fields[4][0]);
    }
    nmea_time(m, 5, &out->hour, &out->minute, &out->second);
    out->valid = (m->field_len[6] >= 1 && (m->fields[6][0] == 'A' || m->fields[6][0] == 'a'));
    // The FAA mode indicator (field 7) is NMEA 2.3+; older receivers omit it.
    if (m->field_count > 7 && m->field_len[7] >= 1)
    {
        out->mode = m->fields[7][0];
    }
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_parse_vhw(const Nmea0183 *m, pc_nmea_vhw *out)
{
    if (!m || !out || strcmp(m->type, "VHW") != 0 || m->field_count < 8) // need through speed km/h (field 7)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    pc_nmea0183_field_float(m, 1, &out->heading_true_deg); // heading true; stays 0 if empty (out was zeroed)
    pc_nmea0183_field_float(m, 3, &out->heading_mag_deg);  // heading magnetic
    pc_nmea0183_field_float(m, 5, &out->speed_knots);      // speed through water, knots
    pc_nmea0183_field_float(m, 7, &out->speed_kmh);        // speed through water, km/h
    return PROTO_TRUE;
}

proto_bool pc_nmea0183_parse_vlw(const Nmea0183 *m, pc_nmea_vlw *out)
{
    if (!m || !out || strcmp(m->type, "VLW") != 0 || m->field_count < 4) // need through the trip distance (field 3)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    pc_nmea0183_field_float(m, 1, &out->total_water_nm); // total cumulative; stays 0 if empty (out was zeroed)
    pc_nmea0183_field_float(m, 3, &out->trip_water_nm);  // since the last reset
    return PROTO_TRUE;
}

#endif // PC_NEED_NMEA0183
