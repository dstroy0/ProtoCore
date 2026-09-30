// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file utmc.c
 * @brief UTMC common-database codec (see utmc.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_UTMC

#include "memoria_operor/memoria_operor.h"
#include "services/transportation/utmc/utmc.h"
#include "verba_scribo/verba_scribo.h" // verba_*: the text and number writers the builders chain

PROTOCORE_BEGIN_DECLS

static size_t put_u(char *out, size_t cap, size_t at, uint32_t v)
{
    char tmp[11];
    int n = 0;
    do
    {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v);
    char digits[12];
    for (int i = 0; i < n; i++)
    {
        digits[i] = tmp[n - 1 - i];
    }
    digits[n] = '\0';
    return EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = digits);
}

size_t protocore_utmc_request(const char *object_id, char *out, size_t cap)
{
    size_t b = 0;
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b,
                   .text = "<?xml version=\"1.0\"?><UTMCRequest><object id=\"");
    b = EMBED_CALL(verba_textus.xml, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = object_id);
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "\"/></UTMCRequest>");
    return EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = out, .cap = cap, .at = b);
}

size_t protocore_utmc_response(const char *object_id, const char *value, uint8_t quality, const char *timestamp,
                               char *out, size_t cap)
{
    size_t b2 = 0;
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2,
                    .text = "<?xml version=\"1.0\"?><UTMCResponse><object id=\"");
    b2 = EMBED_CALL(verba_textus.xml, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = object_id);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = "\" value=\"");
    b2 = EMBED_CALL(verba_textus.xml, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = value);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = "\" quality=\"");
    b2 = put_u(out, cap, b2, quality);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = "\" timestamp=\"");
    b2 = EMBED_CALL(verba_textus.xml, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = timestamp);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = "\"/></UTMCResponse>");
    return EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = out, .cap = cap, .at = b2);
}

size_t protocore_utmc_parse_request(const char *xml, size_t len, char *out, size_t cap)
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
        if (EMBED_CALL(memor.cmp, MemoriaCfg, .src = xml + i, .other = key, .bytes = kl) != 0)
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

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_UTMC
