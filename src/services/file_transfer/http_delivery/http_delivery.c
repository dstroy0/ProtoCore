// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_delivery.c
 * @brief HTTP delivery optimizations (see http_delivery.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP_DELIVERY

#include "services/file_transfer/http_delivery/http_delivery.h"
#include "verba_scribo/verba_scribo.h" // verba_*: the text and number writers the builders chain

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_http_delivery_swr(uint8_t *work)
{
    (void)work;
    uint32_t age_s = HttpDeliveryV.swr_args.age_s;
    uint32_t max_age_s = HttpDeliveryV.swr_args.max_age_s;
    uint32_t swr_s = HttpDeliveryV.swr_args.swr_s;

    if (age_s <= max_age_s)
    {
        HttpDeliveryV.value = DELIVERY_FRESH;
        return;
    }
    uint64_t window = (uint64_t)max_age_s + swr_s;
    if ((uint64_t)age_s <= window)
    {
        HttpDeliveryV.value = DELIVERY_STALE_REVALIDATE;
        return;
    }
    HttpDeliveryV.value = DELIVERY_EXPIRED;
}

void protocore_http_delivery_cache_control(uint8_t *work)
{
    (void)work;
    uint32_t max_age_s = HttpDeliveryV.cache_control_args.max_age_s;
    uint32_t swr_s = HttpDeliveryV.cache_control_args.swr_s;
    char *out = HttpDeliveryV.cache_control_args.out;
    size_t cap = HttpDeliveryV.cache_control_args.cap;

    if (!out || cap == 0)
    {
        HttpDeliveryV.n = 0;
        return;
    }
    size_t b = 0;
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "public, max-age=");
    b = EMBED_CALL(verba_numerus.u32, VerbaNumerusCfg, .out = out, .cap = cap, .at = b, .val = max_age_s);
    if (swr_s)
    {
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b,
                       .text = ", stale-while-revalidate=");
        b = EMBED_CALL(verba_numerus.u32, VerbaNumerusCfg, .out = out, .cap = cap, .at = b, .val = swr_s);
    }
    if (!EMBED_CALL(verba_finis.ok, VerbaFinisCfg, .cap = cap, .at = b))
    {
        HttpDeliveryV.n = 0;
        return;
    }
    out[b] = '\0';
    HttpDeliveryV.n = b;
}

void protocore_http_delivery_sw_manifest(uint8_t *work)
{
    (void)work;
    const char *const *paths = HttpDeliveryV.sw_manifest_args.paths;
    size_t n = HttpDeliveryV.sw_manifest_args.n;
    const char *version = HttpDeliveryV.sw_manifest_args.version;
    char *out = HttpDeliveryV.sw_manifest_args.out;
    size_t cap = HttpDeliveryV.sw_manifest_args.cap;

    if (!out || cap == 0 || (n && !paths))
    {
        HttpDeliveryV.n = 0;
        return;
    }
    size_t b2 = 0;
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = "{\"version\":");
    b2 =
        EMBED_CALL(verba_textus.json, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = version ? version : "");
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = ",\"precache\":[");
    for (size_t i = 0; i < n; i++)
    {
        if (i)
        {
            b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = ",");
        }
        b2 = EMBED_CALL(verba_textus.json, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = paths[i]);
    }
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = "]}");
    if (!EMBED_CALL(verba_finis.ok, VerbaFinisCfg, .cap = cap, .at = b2))
    {
        HttpDeliveryV.n = 0;
        return;
    }
    out[b2] = '\0';
    HttpDeliveryV.n = b2;
}

// The route installer lives in http_delivery_routes.c, the arm that has an HTTP surface to install
// on; it is bound here so the whole surface is one initializer rather than a runtime install. Weak,
// so the three pure cores link on their own - the freshness verdict, the Cache-Control builder and
// the manifest serializer are host-tested without the server - and http_delivery_routes.c overrides
// this the moment it is in the build. Nothing is registered without it, which is what ok reports.
__attribute__((weak)) void protocore_http_delivery_serve_sw(uint8_t *work)
{
    (void)work;
    HttpDeliveryV.ok = PROTO_FALSE;
}

/** @brief The operands and the outcome. */
HttpDeliveryVars HttpDeliveryV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_DELIVERY
