// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_delivery.cpp
 * @brief HTTP delivery optimizations (see http_delivery.h).
 */

#include "services/file_transfer/http_delivery/http_delivery.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_HTTP_DELIVERY

DeliveryVerdict pc_delivery_swr(uint32_t age_s, uint32_t max_age_s, uint32_t swr_s)
{
    if (age_s <= max_age_s)
    {
        return DeliveryVerdict::DELIVERY_FRESH;
    }
    uint64_t window = (uint64_t)max_age_s + swr_s;
    if ((uint64_t)age_s <= window)
    {
        return DeliveryVerdict::DELIVERY_STALE_REVALIDATE;
    }
    return DeliveryVerdict::DELIVERY_EXPIRED;
}

size_t pc_delivery_cache_control(uint32_t max_age_s, uint32_t swr_s, char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    pc_sb b = {out, cap, 0, true};
    pc_sb_put(&b, "public, max-age=");
    pc_sb_u32(&b, max_age_s);
    if (swr_s)
    {
        pc_sb_put(&b, ", stale-while-revalidate=");
        pc_sb_u32(&b, swr_s);
    }
    if (!b.ok)
    {
        return 0;
    }
    out[b.len] = '\0';
    return b.len;
}

size_t pc_delivery_sw_manifest(const char *const *paths, size_t n, const char *version, char *out, size_t cap)
{
    if (!out || cap == 0 || (n && !paths))
    {
        return 0;
    }
    pc_sb b2 = {out, cap, 0, true};
    pc_sb_put(&b2, "{\"version\":");
    pc_sb_json(&b2, version ? version : "");
    pc_sb_put(&b2, ",\"precache\":[");
    for (size_t i = 0; i < n; i++)
    {
        if (i)
        {
            pc_sb_put(&b2, ",");
        }
        pc_sb_json(&b2, paths[i]);
    }
    pc_sb_put(&b2, "]}");
    if (!b2.ok)
    {
        return 0;
    }
    out[b2.len] = '\0';
    return b2.len;
}

#endif // PC_ENABLE_HTTP_DELIVERY
