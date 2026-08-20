// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_delivery.c
 * @brief HTTP delivery optimizations (see http_delivery.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP_DELIVERY

#include "mmgr/membuild/membuild.h" // protocore_sb frame builder
#include "services/file_transfer/http_delivery/http_delivery.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void http_delivery_swr(uint8_t *restrict work)
{
    (void)work;
    uint32_t age_s = HttpDelivery.swr_args.age_s;
    uint32_t max_age_s = HttpDelivery.swr_args.max_age_s;
    uint32_t swr_s = HttpDelivery.swr_args.swr_s;

    if (age_s <= max_age_s)
    {
        HttpDelivery.value = DELIVERY_FRESH;
        return;
    }
    uint64_t window = (uint64_t)max_age_s + swr_s;
    if ((uint64_t)age_s <= window)
    {
        HttpDelivery.value = DELIVERY_STALE_REVALIDATE;
        return;
    }
    HttpDelivery.value = DELIVERY_EXPIRED;
}

static void http_delivery_cache_control(uint8_t *restrict work)
{
    (void)work;
    uint32_t max_age_s = HttpDelivery.cache_control_args.max_age_s;
    uint32_t swr_s = HttpDelivery.cache_control_args.swr_s;
    char *out = HttpDelivery.cache_control_args.out;
    size_t cap = HttpDelivery.cache_control_args.cap;

    if (!out || cap == 0)
    {
        HttpDelivery.n = 0;
        return;
    }
    protocore_sb b = {out, cap, 0, PROTO_TRUE};
    Sb.put(&b, "public, max-age=");
    Sb.u32(&b, max_age_s);
    if (swr_s)
    {
        Sb.put(&b, ", stale-while-revalidate=");
        Sb.u32(&b, swr_s);
    }
    if (!b.ok)
    {
        HttpDelivery.n = 0;
        return;
    }
    out[b.len] = '\0';
    HttpDelivery.n = b.len;
}

static void http_delivery_sw_manifest(uint8_t *restrict work)
{
    (void)work;
    const char *const *paths = HttpDelivery.sw_manifest_args.paths;
    size_t n = HttpDelivery.sw_manifest_args.n;
    const char *version = HttpDelivery.sw_manifest_args.version;
    char *out = HttpDelivery.sw_manifest_args.out;
    size_t cap = HttpDelivery.sw_manifest_args.cap;

    if (!out || cap == 0 || (n && !paths))
    {
        HttpDelivery.n = 0;
        return;
    }
    protocore_sb b2 = {out, cap, 0, PROTO_TRUE};
    Sb.put(&b2, "{\"version\":");
    Sb.json(&b2, version ? version : "");
    Sb.put(&b2, ",\"precache\":[");
    for (size_t i = 0; i < n; i++)
    {
        if (i)
        {
            Sb.put(&b2, ",");
        }
        Sb.json(&b2, paths[i]);
    }
    Sb.put(&b2, "]}");
    if (!b2.ok)
    {
        HttpDelivery.n = 0;
        return;
    }
    out[b2.len] = '\0';
    HttpDelivery.n = b2.len;
}

// The route installer lives in http_delivery_routes.c, the arm that has an HTTP surface to install
// on; it is bound here so the whole surface is one initializer rather than a runtime install. Weak,
// so the three pure cores link on their own - the freshness verdict, the Cache-Control builder and
// the manifest serializer are host-tested without the server - and http_delivery_routes.c overrides
// this the moment it is in the build. Nothing is registered without it, which is what ok reports.
__attribute__((weak)) void http_delivery_serve_sw(uint8_t *restrict work)
{
    (void)work;
    HttpDelivery.ok = PROTO_FALSE;
}

HttpDeliveryNs HttpDelivery = {
    .swr = http_delivery_swr,
    .cache_control = http_delivery_cache_control,
    .sw_manifest = http_delivery_sw_manifest,
    .serve_sw = http_delivery_serve_sw,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_DELIVERY
