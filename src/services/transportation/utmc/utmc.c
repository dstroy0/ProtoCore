// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file utmc.c
 * @brief UTMC common-database codec (see utmc.h).
 */

#include "services/transportation/utmc/utmc.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_UTMC

static void put_u(pc_sb *b, uint32_t v)
{
    char tmp[11];
    int n = 0;
    do
    {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v);
    char out[12];
    for (int i = 0; i < n; i++)
    {
        out[i] = tmp[n - 1 - i];
    }
    out[n] = '\0';
    pc_sb_put(b, out);
}

size_t pc_utmc_request(const char *object_id, char *out, size_t cap)
{
    pc_sb b = {out, cap, 0, out != NULL && cap > 0};
    pc_sb_put(&b, "<?xml version=\"1.0\"?><UTMCRequest><object id=\"");
    pc_sb_xml(&b, object_id);
    pc_sb_put(&b, "\"/></UTMCRequest>");
    return pc_sb_finish(&b);
}

size_t pc_utmc_response(const char *object_id, const char *value, uint8_t quality, const char *timestamp, char *out,
                        size_t cap)
{
    pc_sb b2 = {out, cap, 0, out != NULL && cap > 0};
    pc_sb_put(&b2, "<?xml version=\"1.0\"?><UTMCResponse><object id=\"");
    pc_sb_xml(&b2, object_id);
    pc_sb_put(&b2, "\" value=\"");
    pc_sb_xml(&b2, value);
    pc_sb_put(&b2, "\" quality=\"");
    put_u(&b2, quality);
    pc_sb_put(&b2, "\" timestamp=\"");
    pc_sb_xml(&b2, timestamp);
    pc_sb_put(&b2, "\"/></UTMCResponse>");
    return pc_sb_finish(&b2);
}

size_t pc_utmc_parse_request(const char *xml, size_t len, char *out, size_t cap)
{
    if (!xml || !out || cap == 0)
    {
        return 0;
    }
    // Find `id="` and copy up to the next quote.
    const char *key = "id=\"";
    size_t kl = 4;
    for (size_t i = 0; i + kl < len; i++)
    {
        if (memcmp(xml + i, key, kl) != 0)
        {
            continue;
        }
        size_t j = i + kl;
        size_t k = 0;
        while (j < len && xml[j] != '"')
        {
            if (k + 1 >= cap)
            {
                return 0;
            }
            out[k++] = xml[j++];
        }
        if (j >= len) // unterminated
        {
            return 0;
        }
        out[k] = '\0';
        return k;
    }
    return 0;
}

#endif // PC_ENABLE_UTMC
