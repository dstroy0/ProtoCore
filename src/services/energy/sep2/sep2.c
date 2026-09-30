// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sep2.c
 * @brief IEEE 2030.5 resource codec (see sep2.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SEP2

#include "services/energy/sep2/sep2.h"
#include "verba_scribo/verba_scribo.h" // verba_*: the text and number writers the builders chain

PROTOCORE_BEGIN_DECLS

static size_t put_i64(char *out, size_t cap, size_t at, int64_t v)
{
    if (!EMBED_CALL(verba_finis.ok, VerbaFinisCfg, .cap = cap, .at = at))
    {
        return at;
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
    char digits[22];
    int k = 0;
    if (neg)
    {
        digits[k++] = '-';
    }
    for (int i = 0; i < n; i++)
    {
        digits[k++] = tmp[n - 1 - i];
    }
    digits[k] = '\0';
    return EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = digits);
}

static const char *NS = " xmlns=\"urn:ieee:std:2030.5:ns\"";
static const char *DECL = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";

size_t protocore_sep2_device_capability(uint32_t poll_rate, const char *edev_list_href, const char *derp_list_href,
                                        char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    size_t b = 0;
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = DECL);
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "<DeviceCapability");
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = NS);
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = " pollRate=\"");
    b = put_i64(out, cap, b, poll_rate);
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "\">");
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b,
                   .text = "<EndDeviceListLink href=\"");
    b = EMBED_CALL(verba_textus.xml, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = edev_list_href);
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "\"/>");
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b,
                   .text = "<DERProgramListLink href=\"");
    b = EMBED_CALL(verba_textus.xml, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = derp_list_href);
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "\"/>");
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "</DeviceCapability>");
    return EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = out, .cap = cap, .at = b);
}

size_t protocore_sep2_end_device(uint64_t sfdi, const char *lfdi, const char *href, char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    size_t b2 = 0;
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = DECL);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = "<EndDevice");
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = NS);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = " href=\"");
    b2 = EMBED_CALL(verba_textus.xml, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = href);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = "\"><sFDI>");
    b2 = put_i64(out, cap, b2, (int64_t)sfdi);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = "</sFDI><lFDI>");
    b2 = EMBED_CALL(verba_textus.xml, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = lfdi);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = "</lFDI></EndDevice>");
    return EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = out, .cap = cap, .at = b2);
}

size_t protocore_sep2_der_control(const char *mrid, uint32_t start, uint32_t duration, int32_t opmod_target_w,
                                  char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    size_t b3 = 0;
    b3 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b3, .text = DECL);
    b3 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b3, .text = "<DERControl");
    b3 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b3, .text = NS);
    b3 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b3, .text = "><mRID>");
    b3 = EMBED_CALL(verba_textus.xml, VerbaTextusCfg, .out = out, .cap = cap, .at = b3, .text = mrid);
    b3 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b3,
                    .text = "</mRID><interval><start>");
    b3 = put_i64(out, cap, b3, start);
    b3 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b3, .text = "</start><duration>");
    b3 = put_i64(out, cap, b3, duration);
    b3 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b3,
                    .text = "</duration></interval><DERControlBase><opModFixedW>");
    b3 = put_i64(out, cap, b3, opmod_target_w);
    b3 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b3,
                    .text = "</opModFixedW></DERControlBase></DERControl>");
    return EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = out, .cap = cap, .at = b3);
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SEP2
