// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sep2.c
 * @brief IEEE 2030.5 resource codec (see sep2.h).
 */

#include "services/energy/sep2/sep2.h"
#include "mmgr/membuild.h" // protocore_sb frame builder

#if PROTOCORE_ENABLE_SEP2

static void put_i64(protocore_sb *b, int64_t v)
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
    protocore_sb_put(b, out);
}

static const char *NS = " xmlns=\"urn:ieee:std:2030.5:ns\"";
static const char *DECL = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";

size_t protocore_sep2_device_capability(uint32_t poll_rate, const char *edev_list_href, const char *derp_list_href, char *out,
                                 size_t cap)
{
    protocore_sb b = {out, cap, 0, out != NULL && cap > 0};
    protocore_sb_put(&b, DECL);
    protocore_sb_put(&b, "<DeviceCapability");
    protocore_sb_put(&b, NS);
    protocore_sb_put(&b, " pollRate=\"");
    put_i64(&b, poll_rate);
    protocore_sb_put(&b, "\">");
    protocore_sb_put(&b, "<EndDeviceListLink href=\"");
    protocore_sb_xml(&b, edev_list_href);
    protocore_sb_put(&b, "\"/>");
    protocore_sb_put(&b, "<DERProgramListLink href=\"");
    protocore_sb_xml(&b, derp_list_href);
    protocore_sb_put(&b, "\"/>");
    protocore_sb_put(&b, "</DeviceCapability>");
    return protocore_sb_finish(&b);
}

size_t protocore_sep2_end_device(uint64_t sfdi, const char *lfdi, const char *href, char *out, size_t cap)
{
    protocore_sb b2 = {out, cap, 0, out != NULL && cap > 0};
    protocore_sb_put(&b2, DECL);
    protocore_sb_put(&b2, "<EndDevice");
    protocore_sb_put(&b2, NS);
    protocore_sb_put(&b2, " href=\"");
    protocore_sb_xml(&b2, href);
    protocore_sb_put(&b2, "\"><sFDI>");
    put_i64(&b2, (int64_t)sfdi);
    protocore_sb_put(&b2, "</sFDI><lFDI>");
    protocore_sb_xml(&b2, lfdi);
    protocore_sb_put(&b2, "</lFDI></EndDevice>");
    return protocore_sb_finish(&b2);
}

size_t protocore_sep2_der_control(const char *mrid, uint32_t start, uint32_t duration, int32_t opmod_target_w, char *out,
                           size_t cap)
{
    protocore_sb b3 = {out, cap, 0, out != NULL && cap > 0};
    protocore_sb_put(&b3, DECL);
    protocore_sb_put(&b3, "<DERControl");
    protocore_sb_put(&b3, NS);
    protocore_sb_put(&b3, "><mRID>");
    protocore_sb_xml(&b3, mrid);
    protocore_sb_put(&b3, "</mRID><interval><start>");
    put_i64(&b3, start);
    protocore_sb_put(&b3, "</start><duration>");
    put_i64(&b3, duration);
    protocore_sb_put(&b3, "</duration></interval><DERControlBase><opModFixedW>");
    put_i64(&b3, opmod_target_w);
    protocore_sb_put(&b3, "</opModFixedW></DERControlBase></DERControl>");
    return protocore_sb_finish(&b3);
}

#endif // PROTOCORE_ENABLE_SEP2
