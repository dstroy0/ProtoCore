// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_ntrip_caster.cpp
 * @brief NTRIP caster protocol codec - request parse + response / source-table build. See pc_ntrip_caster.h.
 */

#include "services/timing_position/gnss/ntrip_caster.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_NTRIP_CASTER

#include <stdio.h>
#include <string.h>

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Case-insensitive: does the line at [s,end) begin with prefix?
static proto_bool ci_prefix(const char *s, const char *end, const char *prefix)
{
    while (*prefix)
    {
        if (s >= end || lower(*s) != lower(*prefix))
        {
            return PROTO_FALSE;
        }
        s++;
        prefix++;
    }
    return PROTO_TRUE;
}

// Skip spaces / tabs.
static const char *skip_ws(const char *s, const char *end)
{
    while (s < end && (*s == ' ' || *s == '\t'))
    {
        s++;
    }
    return s;
}

// Format a degree value to 2 decimals with integer math (newlib-nano often stubs %f).
static void fmt_deg2(char *out, size_t cap, double v)
{
    int hundredths = (int)(v * 100.0 + (v >= 0.0 ? 0.5 : -0.5));
    int whole = hundredths / 100;
    int frac = hundredths % 100;
    if (frac < 0)
    {
        frac = -frac;
    }
    const char *sign = (v < 0.0 && whole == 0) ? "-" : ""; // preserve "-0.xx"
    pc_sb sb_out = {out, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_out, sign);
    pc_sb_i64(&sb_out, (int64_t)(whole));
    pc_sb_put(&sb_out, ".");
    pc_sb_u32w(&sb_out, (uint32_t)(frac), 2);
    if (pc_sb_finish(&sb_out) == 0)
    {
        out[0] = '\0';
    }
}

// Find the request header block terminator (CRLFCRLF, or bare LFLF fallback). On success sets *hend
// just past the blank line and returns true; returns false if the block is incomplete (need more bytes).
static proto_bool find_header_end(const char *buf, size_t len, size_t *hend)
{
    for (size_t i = 0; i < len; i++)
    {
        if (i + 3 < len && buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
        {
            *hend = i + 4;
            return PROTO_TRUE;
        }
        if (i + 1 < len && buf[i] == '\n' && buf[i + 1] == '\n')
        {
            *hend = i + 2;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

// Scan the header lines in [buf,end) for Ntrip-Version and Authorization: Basic, filling out.
static void scan_headers(const char *buf, const char *end, NtripRequest *out)
{
    const char *line = buf;
    while (line < end)
    {
        const char *le = line;
        // find_header_end only ever reports a block that ends ON the terminating '\n' (hend = i+4 past
        // CRLFCRLF, or i+2 past LFLF), so end[-1] == '\n' and every line in [buf,end) is LF-terminated:
        // the scan always stops on a newline and the `le < end` bound has no true exit to reach.
        while (le < end && *le != '\n') // GCOVR_EXCL_LINE  le < end cannot go false, see above
        {
            le++;
        }
        const char *lend = le; // exclusive; trim a trailing '\r'
        if (lend > line && *(lend - 1) == '\r')
        {
            lend--;
        }

        if (ci_prefix(line, lend, "ntrip-version:"))
        {
            const char *v = line + 14;
            while (v + 2 < lend && !(v[0] == '2' && v[1] == '.' && v[2] == '0'))
            {
                v++;
            }
            if (v + 2 < lend) // found "Ntrip/2.0"
            {
                out->version = NTRIP_V2;
            }
        }
        else if (ci_prefix(line, lend, "authorization:"))
        {
            const char *v = skip_ws(line + 14, lend);
            if (ci_prefix(v, lend, "basic "))
            {
                v = skip_ws(v + 6, lend);
                out->auth_b64 = v;
                out->auth_b64_len = (uint16_t)(lend - v);
            }
        }
        line = le + 1;
    }
}

proto_bool pc_ntrip_request_parse(const char *buf, size_t len, NtripRequest *out)
{
    memset(out, 0, sizeof(*out));
    out->version = NTRIP_V1;

    // Find the end of the request header block (blank line): CRLFCRLF, or bare LFLF as a fallback.
    size_t hend = 0;
    if (!find_header_end(buf, len, &hend))
    {
        return PROTO_FALSE; // need more bytes
    }
    out->complete = PROTO_TRUE;

    const char *end = buf + hend;

    // Request line: "GET <target> HTTP/1.x".
    const char *p = buf;
    if (!ci_prefix(p, end, "GET "))
    {
        out->is_get = PROTO_FALSE; // malformed / unsupported method
        return PROTO_TRUE;
    }
    out->is_get = PROTO_TRUE;
    p = skip_ws(p + 4, end);
    const char *target = p;
    // Same invariant as scan_headers: end[-1] is the '\n' that closed the header block, so the target
    // scan always stops on that newline at the latest and the `p < end` bound never fires.
    while (p < end && *p != ' ' && *p != '\r' && *p != '\n' && *p != '?') // GCOVR_EXCL_LINE  see above
    {
        p++;
    }
    size_t tlen = (size_t)(p - target);

    if (tlen == 0 || (tlen == 1 && target[0] == '/'))
    {
        out->want_sourcetable = PROTO_TRUE; // "GET /" -> list the source table
    }
    else
    {
        const char *mp = target;
        size_t mlen = tlen;
        if (mp[0] == '/') // strip the leading slash
        {
            mp++;
            mlen--;
        }
        if (mlen >= sizeof(out->mountpoint))
        {
            mlen = sizeof(out->mountpoint) - 1;
        }
        memcpy(out->mountpoint, mp, mlen);
        out->mountpoint[mlen] = '\0';
    }

    // Scan the header lines for Ntrip-Version and Authorization.
    scan_headers(buf, end, out);
    return PROTO_TRUE;
}

size_t pc_ntrip_build_stream_response(char *out, size_t cap, NtripVersion version)
{
    // One builder, branching only on which response line goes in it: the version picks the text,
    // not a separate copy of the build-and-check.
    pc_sb sb_out = {out, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_out, (version == NTRIP_V2)
                           ? "HTTP/1.1 200 OK\r\nNtrip-Version: Ntrip/2.0\r\nServer: PC\r\nContent-Type: "
                             "gnss/data\r\nConnection: close\r\n\r\n"
                           : "ICY 200 OK\r\n\r\n");
    size_t n = pc_sb_finish(&sb_out);
    if (!sb_out.ok)
    {
        return 0;
    }
    return n;
}

size_t pc_ntrip_build_error_response(char *out, size_t cap, NtripVersion version)
{
    int n;
    if (version == NTRIP_V2)
    {
        pc_sb sb_out4 = {out, cap, 0, PROTO_TRUE};
        pc_sb_put(&sb_out4, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        n = (int)pc_sb_finish(&sb_out4);
    }
    else
    {
        pc_sb sb_out5 = {out, cap, 0, PROTO_TRUE};
        pc_sb_put(&sb_out5, "ERROR - Bad Request\r\n");
        n = (int)pc_sb_finish(&sb_out5);
    }
    if (n == 0) // the frame is a fixed literal, so a zero length can only mean it did not fit
    {
        return 0;
    }
    return (size_t)n;
}

size_t pc_ntrip_build_unauthorized_response(char *out, size_t cap, NtripVersion version)
{
    int n;
    if (version == NTRIP_V2)
    {
        pc_sb sb_out6 = {out, cap, 0, PROTO_TRUE};
        pc_sb_put(&sb_out6, "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"NTRIP\"\r\nContent-Length: "
                            "0\r\nConnection: close\r\n\r\n");
        n = (int)pc_sb_finish(&sb_out6);
    }
    else
    {
        pc_sb sb_out7 = {out, cap, 0, PROTO_TRUE};
        pc_sb_put(&sb_out7, "ERROR - Bad Password\r\n");
        n = (int)pc_sb_finish(&sb_out7);
    }
    if (n == 0) // the frame is a fixed literal, so a zero length can only mean it did not fit
    {
        return 0;
    }
    return (size_t)n;
}

size_t pc_ntrip_build_str_record(char *out, size_t cap, const NtripMount *m)
{
    if (!m || !m->mountpoint)
    {
        return 0;
    }
    char lat[16];
    char lon[16];
    fmt_deg2(lat, sizeof(lat), m->lat_deg);
    fmt_deg2(lon, sizeof(lon), m->lon_deg);
    const char *ident = m->identifier ? m->identifier : "";
    const char *fmtd = m->format_details ? m->format_details : "1005(1)";
    const char *nav = m->nav_system ? m->nav_system : "GPS";
    const char *ctry = m->country ? m->country : "";
    const char *gen = m->generator ? m->generator : "PC";
    // STR;mount;identifier;format;format-details;carrier;nav;network;country;lat;lon;nmea;solution;
    //     generator;compr;auth;fee;bitrate;misc   (carrier 0 = station reference only, no observations)
    pc_sb sb_out8 = {out, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_out8, "STR;");
    pc_sb_put(&sb_out8, m->mountpoint);
    pc_sb_put(&sb_out8, ";");
    pc_sb_put(&sb_out8, ident);
    pc_sb_put(&sb_out8, ";RTCM 3.3;");
    pc_sb_put(&sb_out8, fmtd);
    pc_sb_put(&sb_out8, ";0;");
    pc_sb_put(&sb_out8, nav);
    pc_sb_put(&sb_out8, ";none;");
    pc_sb_put(&sb_out8, ctry);
    pc_sb_put(&sb_out8, ";");
    pc_sb_put(&sb_out8, lat);
    pc_sb_put(&sb_out8, ";");
    pc_sb_put(&sb_out8, lon);
    pc_sb_put(&sb_out8, ";");
    pc_sb_i64(&sb_out8, (int64_t)(m->nmea_required ? 1 : 0));
    pc_sb_put(&sb_out8, ";0;");
    pc_sb_put(&sb_out8, gen);
    pc_sb_put(&sb_out8, ";none;N;N;9600;");
    int n = (int)pc_sb_finish(&sb_out8);
    if (!sb_out8.ok) // GCOVR_EXCL_LINE  n<0 needs an snprintf encoding failure, see above
    {
        return 0;
    }
    return (size_t)n;
}

size_t pc_ntrip_build_sourcetable(char *out, size_t cap, NtripVersion version, const NtripMount *mounts,
                                  size_t mount_count)
{
    static const char ENDLINE[] = "ENDSOURCETABLE\r\n";

    // Pass 1: compute the body length (records + CRLFs + ENDSOURCETABLE) with a scratch record buffer.
    size_t body_len = 0;
    char rec[192];
    for (size_t i = 0; i < mount_count; i++)
    {
        size_t rn = pc_ntrip_build_str_record(rec, sizeof(rec), &mounts[i]);
        if (rn == 0)
        {
            return 0;
        }
        body_len += rn + 2; // + CRLF
    }
    body_len += sizeof(ENDLINE) - 1;

    // Pass 2: header with the computed length, then the records, then ENDSOURCETABLE.
    int hn;
    if (version == NTRIP_V2)
    {
        pc_sb sb_out9 = {out, cap, 0, PROTO_TRUE};
        pc_sb_put(&sb_out9, "HTTP/1.1 200 OK\r\nNtrip-Version: Ntrip/2.0\r\nServer: PC\r\nContent-Type: "
                            "gnss/sourcetable\r\nContent-Length: ");
        pc_sb_u32(&sb_out9, (uint32_t)((unsigned)body_len));
        pc_sb_put(&sb_out9, "\r\nConnection: close\r\n\r\n");
        hn = (int)pc_sb_finish(&sb_out9);
    }
    else
    {
        pc_sb sb_out10 = {out, cap, 0, PROTO_TRUE};
        pc_sb_put(&sb_out10, "SOURCETABLE 200 OK\r\nServer: PC\r\nContent-Type: text/plain\r\nContent-Length: ");
        pc_sb_u32(&sb_out10, (uint32_t)((unsigned)body_len));
        pc_sb_put(&sb_out10, "\r\n\r\n");
        hn = (int)pc_sb_finish(&sb_out10);
    }
    if (hn == 0) // the frame always has a literal prefix, so a zero length means it did not fit
    {
        return 0;
    }

    size_t pos = (size_t)hn;
    for (size_t i = 0; i < mount_count; i++)
    {
        size_t rn = pc_ntrip_build_str_record(out + pos, cap - pos, &mounts[i]);
        if (rn == 0 || pos + rn + 2 >= cap)
        {
            return 0;
        }
        pos += rn;
        out[pos++] = '\r';
        out[pos++] = '\n';
    }
    if (pos + (sizeof(ENDLINE) - 1) >= cap)
    {
        return 0;
    }
    memcpy(out + pos, ENDLINE, sizeof(ENDLINE) - 1);
    pos += sizeof(ENDLINE) - 1;
    out[pos] = '\0';
    return pos;
}

#endif // PC_ENABLE_NTRIP_CASTER
