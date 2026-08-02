// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sep2.c
 * @brief IEEE 2030.5 resource codec (see sep2.h).
 */

#include "services/energy/sep2/sep2.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_SEP2

#include <string.h>

static void put_i64(pc_sb *b, int64_t v)
{
    if (!b->ok)
    {
        return;
    }
    char tmp[21];
    int n = 0;
    proto_bool neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    do
    {
        tmp[n++] = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u);
    char out[22];
    int k = 0;
    if (neg)
    {
        out[k++] = '-';
    }
    for (int i = 0; i < n; i++)
    {
        out[k++] = tmp[n - 1 - i];
    }
    out[k] = '\0';
    pc_sb_put(b, out);
}

static const char *NS = " xmlns=\"urn:ieee:std:2030.5:ns\"";
static const char *DECL = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";

size_t pc_sep2_device_capability(uint32_t poll_rate, const char *edev_list_href, const char *derp_list_href, char *out,
                                 size_t cap)
{
    pc_sb b = {out, cap, 0, out != NULL && cap > 0};
    pc_sb_put(&b, DECL);
    pc_sb_put(&b, "<DeviceCapability");
    pc_sb_put(&b, NS);
    pc_sb_put(&b, " pollRate=\"");
    put_i64(&b, poll_rate);
    pc_sb_put(&b, "\">");
    pc_sb_put(&b, "<EndDeviceListLink href=\"");
    pc_sb_xml(&b, edev_list_href);
    pc_sb_put(&b, "\"/>");
    pc_sb_put(&b, "<DERProgramListLink href=\"");
    pc_sb_xml(&b, derp_list_href);
    pc_sb_put(&b, "\"/>");
    pc_sb_put(&b, "</DeviceCapability>");
    return pc_sb_finish(&b);
}

size_t pc_sep2_end_device(uint64_t sfdi, const char *lfdi, const char *href, char *out, size_t cap)
{
    pc_sb b2 = {out, cap, 0, out != NULL && cap > 0};
    pc_sb_put(&b2, DECL);
    pc_sb_put(&b2, "<EndDevice");
    pc_sb_put(&b2, NS);
    pc_sb_put(&b2, " href=\"");
    pc_sb_xml(&b2, href);
    pc_sb_put(&b2, "\"><sFDI>");
    put_i64(&b2, (int64_t)sfdi);
    pc_sb_put(&b2, "</sFDI><lFDI>");
    pc_sb_xml(&b2, lfdi);
    pc_sb_put(&b2, "</lFDI></EndDevice>");
    return pc_sb_finish(&b2);
}

size_t pc_sep2_der_control(const char *mrid, uint32_t start, uint32_t duration, int32_t opmod_target_w, char *out,
                           size_t cap)
{
    pc_sb b3 = {out, cap, 0, out != NULL && cap > 0};
    pc_sb_put(&b3, DECL);
    pc_sb_put(&b3, "<DERControl");
    pc_sb_put(&b3, NS);
    pc_sb_put(&b3, "><mRID>");
    pc_sb_xml(&b3, mrid);
    pc_sb_put(&b3, "</mRID><interval><start>");
    put_i64(&b3, start);
    pc_sb_put(&b3, "</start><duration>");
    put_i64(&b3, duration);
    pc_sb_put(&b3, "</duration></interval><DERControlBase><opModFixedW>");
    put_i64(&b3, opmod_target_w);
    pc_sb_put(&b3, "</opModFixedW></DERControlBase></DERControl>");
    return pc_sb_finish(&b3);
}

#endif // PC_ENABLE_SEP2
