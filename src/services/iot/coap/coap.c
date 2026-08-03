// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coap.c
 * @brief CoAP server (RFC 7252): resource table, message codec, and the UDP binding.
 */

#include "services/iot/coap/coap.h"

#if PC_ENABLE_COAP

#include "network_drivers/transport/udp.h"
#include <string.h>
#if PC_ENABLE_COAP_OBSERVE || PC_COAP_DEDUP_ENTRIES > 0
#include "server/clock/clock.h" // pc_millis(): Observe notification sequencing + dedup entry freshness
#endif

// CoAP option numbers we understand (RFC 7252 §5.10, RFC 7959). Others are skipped.
#define COAP_OPT_OBSERVE 6
#define COAP_OPT_URI_PATH 11
#define COAP_OPT_CONTENT_FORMAT 12
#define COAP_OPT_URI_QUERY 15
#define COAP_OPT_BLOCK2 23 ///< block-wise responses (RFC 7959)
#define COAP_OPT_BLOCK1 27 ///< block-wise request uploads (RFC 7959)

static const uint8_t COAP_PAYLOAD_MARKER = 0xFF;
static const size_t COAP_MAX_TOKEN = 8;

// ---------------------------------------------------------------------------
// Resource table + reconstruction scratch (all in BSS - no heap)
// ---------------------------------------------------------------------------

typedef struct
{
    const char *path;
    uint8_t methods;
    CoapHandler handler;
} CoapResource;

#if PC_COAP_DEDUP_ENTRIES > 0
// A cached response for message de-duplication (RFC 7252 §4.5). Keyed by the FULL source endpoint
// (address string + port) and the Message-ID - never a hash, so two peers cannot collide onto one
// entry and be handed each other's cached response. A retransmitted Confirmable request that hits a
// fresh entry is re-answered from `resp` without re-running its handler, so a client's CON retransmit
// cannot execute a non-idempotent request twice.
typedef struct
{
    proto_bool valid;
    char ip[16];
    uint16_t port;
    uint16_t mid;
    uint32_t stamp_ms; // pc_millis() at store; the entry expires after PC_COAP_DEDUP_LIFETIME_MS
    uint16_t len;      // cached response length
    uint8_t resp[PC_COAP_DEDUP_RESP_MAX];
} CoapDedupEntry;
#endif

#if PC_ENABLE_COAP_OBSERVE
// An Observe registration (RFC 7641): a client awaiting notifications on a resource.
typedef struct
{
    proto_bool active;
    char ip[16];
    uint16_t port;
    uint8_t token[8];
    uint8_t tkl;
    int res_idx;
    uint32_t seq; // last Observe value sent
} CoapObserver;
#endif

// All CoAP server state, owned by one instance. Grouping what were scattered file-scope
// mutables means state flows explicitly through the call graph (readers take a
// `const CoapCtx&` and cannot mutate the table), and nothing here is ambient.
typedef struct
{
    CoapResource res[PC_COAP_MAX_RESOURCES]; // resource table: set before begin, read-only during dispatch
    size_t res_count;

    char path[PC_COAP_MAX_PATH];      // scratch: reconstructed Uri-Path of the request in flight
    char query[PC_COAP_MAX_QUERY];    // scratch: reconstructed Uri-Query
    uint8_t pl[PC_COAP_MAX_PAYLOAD];  // scratch: handler response body
    uint8_t tx[PC_COAP_MSG_BUF_SIZE]; // scratch: outbound response (request buffer is transport-owned)

#if PC_COAP_DEDUP_ENTRIES > 0
    CoapDedupEntry dedup[PC_COAP_DEDUP_ENTRIES]; // message de-duplication cache (RFC 7252 §4.5)
#endif

#if PC_ENABLE_COAP_OBSERVE
    uint16_t port; // UDP port the observe transport notifies from; set by pc_coap_server_begin()
    CoapObserver obs[PC_COAP_MAX_OBSERVERS];
    // Last-request fields recorded by pc_coap_server_process_ex() for the Observe transport.
    int last_observe; // reset to -1 at the top of every request parse
    uint8_t last_method;
    uint8_t last_token[8];
    uint8_t last_tkl;
#endif

#if PC_ENABLE_COAP_BLOCK
    // Single in-flight Block1 (request upload) reassembly (RFC 7959); one transfer at a time.
    uint8_t b1[PC_COAP_BLOCK1_MAX];
    size_t b1_len;  // bytes reassembled so far (also the next expected offset)
    uint8_t b1_szx; // negotiated block-size exponent for this transfer
#endif
} CoapCtx;

static CoapCtx s_coap;

void pc_coap_server_reset()
{
    s_coap.res_count = 0;
    memset(s_coap.res, 0, sizeof(s_coap.res));
#if PC_ENABLE_COAP_BLOCK
    s_coap.b1_len = 0;
    s_coap.b1_szx = 0;
#endif
#if PC_COAP_DEDUP_ENTRIES > 0
    for (size_t i = 0; i < PC_COAP_DEDUP_ENTRIES; i++)
    {
        s_coap.dedup[i].valid = PROTO_FALSE;
    }
#endif
}

proto_bool pc_coap_server_add_resource(const char *path, uint8_t methods, CoapHandler handler)
{
    if (s_coap.res_count >= PC_COAP_MAX_RESOURCES || !path || !handler)
    {
        return PROTO_FALSE;
    }
    s_coap.res[s_coap.res_count].path = path;
    s_coap.res[s_coap.res_count].methods = methods;
    s_coap.res[s_coap.res_count].handler = handler;
    s_coap.res_count++;
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Minimal big-endian unsigned decode of an option value (used for Content-Format).
static uint32_t opt_uint(const uint8_t *v, size_t n)
{
    uint32_t r = 0;
    for (size_t i = 0; i < n; i++)
    {
        r = (r << 8) | v[i];
    }
    return r;
}

// Append one path/query segment to a reconstruction buffer, prefixed by @p sep
// (pass '\0' for no separator, e.g. the first Uri-Query segment). Returns false
// (and leaves the buffer NUL-terminated) if it would overflow.
static proto_bool seg_append(char *buf, size_t cap, size_t *len, char sep, const uint8_t *seg, size_t seglen)
{
    size_t need = (sep ? 1u : 0u) + seglen + 1u; // optional separator + segment + NUL
    if (*len + need > cap)
    {
        return PROTO_FALSE;
    }
    if (sep)
    {
        buf[(*len)++] = sep;
    }
    memcpy(buf + *len, seg, seglen);
    *len += seglen;
    buf[*len] = '\0';
    return PROTO_TRUE;
}

static const CoapResource *find_resource(const CoapCtx *c, const char *path)
{
    for (size_t i = 0; i < c->res_count; i++)
    {
        if (strcmp(c->res[i].path, path) == 0)
        {
            return &c->res[i];
        }
    }
    return NULL;
}

// Emit a bare message: 4-byte header + token, no options/payload. Used for RST and
// for error responses (4.04 / 4.05 / 5.01) that carry no diagnostic payload.
static size_t emit_header(uint8_t *out, size_t cap, CoapType type, uint8_t code, uint16_t mid, const uint8_t *token,
                          uint8_t tkl)
{
    if (cap < (size_t)(4 + tkl))
    {
        return 0;
    }
    out[0] = (uint8_t)((1 << 6) | ((uint8_t)type << 4) | tkl); // Ver=1
    out[1] = code;
    out[2] = (uint8_t)(mid >> 8);
    out[3] = (uint8_t)(mid & 0xFF);
    if (tkl)
    {
        memcpy(out + 4, token, tkl);
    }
    return (size_t)(4 + tkl);
}

// Encode an unsigned value into 0..3 big-endian bytes, dropping leading zeros
// (CoAP's minimal uint option encoding). Returns the byte count (0 when v == 0).
static uint8_t enc_uint_minimal(uint32_t v, uint8_t out[3])
{
    uint8_t k = 0;
    if (v & 0xFF0000)
    {
        out[k++] = (uint8_t)(v >> 16);
    }
    if (v & 0xFFFF00)
    {
        out[k++] = (uint8_t)(v >> 8);
    }
    if (v)
    {
        out[k++] = (uint8_t)v;
    }
    return k;
}

// Append one option whose number is @p opt_num (>= @p *last_opt) and whose value
// is @p vlen bytes. Handles an extended option delta (13..268, one extension
// byte); the value length is always < 13 for the options this server emits, so no
// extended length is needed. Stops without writing if it would overflow @p cap.
// Returns the new length.
static size_t append_opt(uint8_t *resp, size_t cap, size_t n, uint32_t *last_opt, uint32_t opt_num, const uint8_t *val,
                         uint8_t vlen)
{
    uint32_t delta = opt_num - *last_opt;
    uint8_t dn = (uint8_t)(delta < 13 ? delta : 13);
    proto_bool ext = delta >= 13;
    if (n + 1u + (ext ? 1u : 0u) + vlen > cap)
    {
        return n;
    }
    resp[n++] = (uint8_t)((dn << 4) | vlen);
    if (ext)
    {
        resp[n++] = (uint8_t)(delta - 13);
    }
    for (uint8_t i = 0; i < vlen; i++)
    {
        resp[n++] = val[i];
    }
    *last_opt = opt_num;
    return n;
}

// Append options (Observe, Content-Format, Block2, Block1 - ascending order) and
// the payload to a message already holding @p n header+token bytes. @p observe_seq
// < 0 omits the Observe option (also omitted unless @p code is a 2.xx success);
// @p block2_val / @p block1_val < 0 omit the respective Block option (their value
// is the raw RFC 7959 option uint, (NUM<<4)|(M<<3)|SZX). Stops (shipping what fits)
// rather than overflowing @p cap. Returns the new length.
static size_t emit_options_payload(uint8_t *resp, size_t cap, size_t n, uint8_t code, int32_t observe_seq,
                                   CoapContentFormat content_format, int32_t block2_val, int32_t block1_val,
                                   const uint8_t *payload, size_t payload_len)
{
    uint32_t last_opt = 0;
    uint8_t v[3];

    if (observe_seq >= 0 && (code >> 5) == 2)
    {
        n = append_opt(resp, cap, n, &last_opt, COAP_OPT_OBSERVE, v,
                       enc_uint_minimal((uint32_t)observe_seq & 0xFFFFFF, v));
    }

    if (content_format != COAP_CF_NONE)
    {
        n = append_opt(resp, cap, n, &last_opt, COAP_OPT_CONTENT_FORMAT, v,
                       enc_uint_minimal((uint16_t)content_format, v));
    }

#if PC_ENABLE_COAP_BLOCK
    if (block2_val >= 0)
    {
        n = append_opt(resp, cap, n, &last_opt, COAP_OPT_BLOCK2, v, enc_uint_minimal((uint32_t)block2_val, v));
    }
    if (block1_val >= 0)
    {
        n = append_opt(resp, cap, n, &last_opt, COAP_OPT_BLOCK1, v, enc_uint_minimal((uint32_t)block1_val, v));
    }
#else
    (void)block2_val;
    (void)block1_val;
#endif

    if (payload_len)
    {
        if (n + 1 + payload_len > cap)
        {
            return n;
        }
        resp[n++] = COAP_PAYLOAD_MARKER;
        memcpy(resp + n, payload, payload_len);
        n += payload_len;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Core processing (no sockets, no heap)
// ---------------------------------------------------------------------------

size_t pc_coap_server_process_ex(const uint8_t *req, size_t req_len, uint8_t *resp, size_t pc_resp_cap,
                                 int32_t observe_seq)
{
    if (req_len < 4)
    {
        return 0; // too short for a header
    }

    uint8_t ver = (req[0] >> 6) & 0x03;
    CoapType type = (CoapType)((req[0] >> 4) & 0x03);
    uint8_t tkl = req[0] & 0x0F;
    uint8_t code = req[1];
    uint16_t mid = (uint16_t)((req[2] << 8) | req[3]);

    // Reply type for a request: piggybacked ACK for CON, NON for NON. ACK/RST we
    // receive are not requests - ignore them.
    if (type != COAP_TYPE_CON && type != COAP_TYPE_NON)
    {
        return 0;
    }
    CoapType rsp_type = (type == COAP_TYPE_CON) ? COAP_TYPE_ACK : COAP_TYPE_NON;

    // A malformed message: bad version or reserved TKL (9..15). RFC 7252 §4.2:
    // reject a CON with an RST; stay silent for a NON.
    if (ver != 1 || tkl > COAP_MAX_TOKEN)
    {
        return (type == COAP_TYPE_CON) ? emit_header(resp, pc_resp_cap, COAP_TYPE_RST, 0, mid, NULL, 0) : 0;
    }

    const uint8_t *p = req + 4;
    const uint8_t *end = req + req_len;
    if (p + tkl > end)
    {
        return (type == COAP_TYPE_CON) ? emit_header(resp, pc_resp_cap, COAP_TYPE_RST, 0, mid, NULL, 0) : 0;
    }
    const uint8_t *token = p;
    p += tkl;

    // An empty message (Code 0.00): CON is a ping -> RST; anything else -> ignore.
    if (code == 0)
    {
        return (type == COAP_TYPE_CON) ? emit_header(resp, pc_resp_cap, COAP_TYPE_RST, 0, mid, NULL, 0) : 0;
    }

#if PC_ENABLE_COAP_OBSERVE
    s_coap.last_observe = -1;
    s_coap.last_method = code;
    s_coap.last_tkl = tkl;
    if (tkl)
    {
        memcpy(s_coap.last_token, token, tkl);
    }
#endif

    // Decode options, reconstructing Uri-Path / Uri-Query and reading Content-Format.
    size_t path_len = 0;
    size_t query_len = 0;
    s_coap.path[0] = '\0';
    s_coap.query[0] = '\0';
    CoapContentFormat req_cf = COAP_CF_NONE;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint32_t opt_num = 0;
    proto_bool bad = PROTO_FALSE;
    proto_bool bad_option = PROTO_FALSE; // unrecognized critical (odd-numbered) option seen (RFC 7252 5.4.1)
#if PC_ENABLE_COAP_BLOCK
    int32_t req_block1 = -1; // request Block1 option value (RFC 7959), or -1 if absent
    int32_t req_block2 = -1; // request Block2 option value, or -1 if absent
#endif

    while (p < end)
    {
        uint8_t b = *p++;
        if (b == COAP_PAYLOAD_MARKER)
        {
            payload = p;
            payload_len = (size_t)(end - p);
            break;
        }
        uint32_t delta = b >> 4;
        uint32_t olen = b & 0x0F;
        if (delta == 15 || olen == 15) // 15 is reserved outside the payload marker
        {
            bad = PROTO_TRUE;
            break;
        }
        // Extended delta / length (13 -> +1 byte; 14 -> +2 bytes).
        if (delta == 13)
        {
            if (p >= end)
            {
                bad = PROTO_TRUE;
                break;
            }
            delta = (uint32_t)(*p++) + 13;
        }
        else if (delta == 14)
        {
            if (p + 2 > end)
            {
                bad = PROTO_TRUE;
                break;
            }
            delta = (uint32_t)((p[0] << 8) | p[1]) + 269;
            p += 2;
        }
        if (olen == 13)
        {
            if (p >= end)
            {
                bad = PROTO_TRUE;
                break;
            }
            olen = (uint32_t)(*p++) + 13;
        }
        else if (olen == 14)
        {
            if (p + 2 > end)
            {
                bad = PROTO_TRUE;
                break;
            }
            olen = (uint32_t)((p[0] << 8) | p[1]) + 269;
            p += 2;
        }
        if (p + olen > end)
        {
            bad = PROTO_TRUE;
            break;
        }
        opt_num += delta;
        const uint8_t *val = p;
        p += olen;

        switch (opt_num)
        {
        case COAP_OPT_URI_PATH:
            if (!seg_append(s_coap.path, sizeof(s_coap.path), &path_len, '/', val, olen))
            {
                bad = PROTO_TRUE;
            }
            break;
        case COAP_OPT_URI_QUERY:
            if (!seg_append(s_coap.query, sizeof(s_coap.query), &query_len, query_len ? '&' : '\0', val, olen))
            {
                bad = PROTO_TRUE;
            }
            break;
        case COAP_OPT_CONTENT_FORMAT:
            req_cf = (CoapContentFormat)opt_uint(val, olen);
            break;
#if PC_ENABLE_COAP_OBSERVE
        case COAP_OPT_OBSERVE:
            s_coap.last_observe = (int)opt_uint(val, olen); // 0 = register, 1 = deregister
            break;
#endif
#if PC_ENABLE_COAP_BLOCK
        case COAP_OPT_BLOCK1: // block-wise request uploads (RFC 7959)
            if (olen > 3)
            {
                bad = PROTO_TRUE;
            }
            else
            {
                req_block1 = (int32_t)opt_uint(val, olen);
            }
            break;
        case COAP_OPT_BLOCK2: // block-wise responses (RFC 7959)
            if (olen > 3)
            {
                bad = PROTO_TRUE;
            }
            else
            {
                req_block2 = (int32_t)opt_uint(val, olen);
            }
            break;
#endif
        default:
            // RFC 7252 5.4.1: an unrecognized option of class "critical" (odd option
            // number) in a request must cause a 4.02 (Bad Option). Elective (even)
            // unknown options are silently ignored. Block1/Block2 are critical, so a
            // build without COAP_BLOCK correctly rejects them here.
            if (opt_num & 1)
            {
                bad_option = PROTO_TRUE;
            }
            break;
        }
        if (bad)
        {
            break;
        }
    }

    if (bad)
    {
        return emit_header(resp, pc_resp_cap, rsp_type, (uint8_t)COAP_RSP_BAD_REQUEST, mid, token, tkl);
    }
    if (bad_option)
    {
        return emit_header(resp, pc_resp_cap, rsp_type, (uint8_t)COAP_RSP_BAD_OPTION, mid, token, tkl);
    }

    if (path_len == 0)
    {
        s_coap.path[0] = '/';
        s_coap.path[1] = '\0';
    }

    // Only class-0 GET/POST/PUT/DELETE are supported request methods. RFC 7252 5.8:
    // "A request with an unrecognized or unsupported Method Code MUST generate a 4.05
    // (Method Not Allowed) piggybacked response."
    // GCOVR_EXCL_START  the `code < COAP_GET` operand cannot be taken: code is unsigned and code == 0
    // (the empty message) already returned above, so nothing below COAP_GET reaches this line.
    if ((code >> 5) != 0 || code < (uint8_t)COAP_GET || code > (uint8_t)COAP_DELETE)
    {
        return emit_header(resp, pc_resp_cap, rsp_type, (uint8_t)COAP_RSP_METHOD_NOT_ALLOWED, mid, token, tkl);
    }
    // GCOVR_EXCL_STOP

    // The response the emit path below serializes (block-wise if large). Filled
    // either by the .well-known/core discovery listing or by a resource handler.
    CoapResponse cresp;
    cresp.code = (uint8_t)COAP_RSP_CONTENT;
    cresp.content_format = COAP_CF_NONE;
    cresp.payload = s_coap.pl;
    cresp.payload_cap = sizeof(s_coap.pl);
    cresp.payload_len = 0;
    int32_t block1_echo = -1; // Block1 option to echo on the final-block response

    // RFC 6690: GET /.well-known/core returns the CoRE Link Format listing of the
    // registered resources, e.g. "</info>,</led>". Block2 (below) pages it if large.
    if (strcmp(s_coap.path, "/.well-known/core") == 0)
    {
        if (code != (uint8_t)COAP_GET)
        {
            return emit_header(resp, pc_resp_cap, rsp_type, (uint8_t)COAP_RSP_METHOD_NOT_ALLOWED, mid, token, tkl);
        }
        size_t pl = 0;
        for (size_t i = 0; i < s_coap.res_count; i++)
        {
            const char *rpath = s_coap.res[i].path;
            size_t plen = strnlen(rpath, sizeof(s_coap.pl));
            size_t need = (pl ? 1u : 0u) + 2u + plen; // optional ',' + '<' + path + '>'
            if (pl + need > sizeof(s_coap.pl))
            {
                break; // listing exceeds the payload buffer; truncate at a resource boundary
            }
            if (pl)
            {
                s_coap.pl[pl++] = ',';
            }
            s_coap.pl[pl++] = '<';
            memcpy(s_coap.pl + pl, rpath, plen);
            pl += plen;
            s_coap.pl[pl++] = '>';
        }
        cresp.content_format = COAP_CF_LINK;
        cresp.payload_len = pl;
    }
    else
    {
        const CoapResource *r = find_resource(&s_coap, s_coap.path);
        if (!r)
        {
            return emit_header(resp, pc_resp_cap, rsp_type, (uint8_t)COAP_RSP_NOT_FOUND, mid, token, tkl);
        }
        if (!(r->methods & (1u << code)))
        {
            return emit_header(resp, pc_resp_cap, rsp_type, (uint8_t)COAP_RSP_METHOD_NOT_ALLOWED, mid, token, tkl);
        }

        const uint8_t *eff_payload = payload;
        size_t eff_payload_len = payload_len;

#if PC_ENABLE_COAP_BLOCK
        // --- Block1: reassemble a chunked POST/PUT payload (RFC 7959 §2.5) ---
        if (req_block1 >= 0 && (code == (uint8_t)COAP_POST || code == (uint8_t)COAP_PUT))
        {
            uint32_t b = (uint32_t)req_block1;
            uint32_t num = b >> 4;
            uint8_t more = (uint8_t)((b >> 3) & 1);
            uint8_t szx = (uint8_t)(b & 7);
            if (szx == 7) // reserved block size
            {
                return emit_header(resp, pc_resp_cap, rsp_type, (uint8_t)COAP_RSP_BAD_OPTION, mid, token, tkl);
            }
            uint32_t bsize = 1u << (szx + 4);
            if (num == 0) // first block starts a fresh transfer
            {
                s_coap.b1_len = 0;
                s_coap.b1_szx = szx;
            }
            // The block size is fixed for a transfer; an offset gap means a lost or
            // reordered block. Either way the reassembly cannot continue: 4.08.
            if (szx != s_coap.b1_szx || (size_t)num * bsize != s_coap.b1_len)
            {
                s_coap.b1_len = 0;
                return emit_header(resp, pc_resp_cap, rsp_type, (uint8_t)COAP_RSP_REQUEST_INCOMPLETE, mid, token, tkl);
            }
            if (s_coap.b1_len + payload_len > sizeof(s_coap.b1))
            {
                s_coap.b1_len = 0;
                return emit_header(resp, pc_resp_cap, rsp_type, (uint8_t)COAP_RSP_REQUEST_TOO_LARGE, mid, token, tkl);
            }
            if (payload_len)
            {
                memcpy(s_coap.b1 + s_coap.b1_len, payload, payload_len);
            }
            s_coap.b1_len += payload_len;

            if (more)
            {
                // More blocks coming: acknowledge with 2.31 Continue + Block1 echo
                // (no representation yet; the handler runs only on the final block).
                size_t cn = emit_header(resp, pc_resp_cap, rsp_type, (uint8_t)COAP_RSP_CONTINUE, mid, token, tkl);
                if (cn == 0)
                {
                    return 0;
                }
                return emit_options_payload(resp, pc_resp_cap, cn, (uint8_t)COAP_RSP_CONTINUE, -1, COAP_CF_NONE, -1,
                                            (int32_t)((num << 4) | (1u << 3) | szx), NULL, 0);
            }
            // Final block: hand the whole reassembled payload to the handler.
            eff_payload = s_coap.b1;
            eff_payload_len = s_coap.b1_len;
            block1_echo = (int32_t)((num << 4) | szx); // More = 0
        }
#endif

        CoapRequest creq;
        creq.method = (CoapMethod)code;
        creq.path = s_coap.path;
        creq.query = s_coap.query;
        creq.payload = eff_payload;
        creq.payload_len = eff_payload_len;
        creq.content_format = req_cf;

        r->handler(&creq, &cresp);
        if (cresp.payload_len > sizeof(s_coap.pl))
        {
            cresp.payload_len = sizeof(s_coap.pl); // defensive clamp
        }
    }

    int32_t block2_echo = -1;

#if PC_ENABLE_COAP_BLOCK
    if (block1_echo >= 0)
    {
        s_coap.b1_len = 0; // the reassembled upload has been handed to the handler; clear it
    }

    // --- Block2: serve a (large or explicitly requested) representation one
    // block at a time (RFC 7959 §2.4). Applies only to a successful body. ---
    if ((cresp.code >> 5) == 2)
    {
        uint8_t szx = PC_COAP_BLOCK_SZX_MAX;
        uint32_t num = 0;
        proto_bool block_wise = PROTO_FALSE;
        if (req_block2 >= 0)
        {
            uint32_t b = (uint32_t)req_block2;
            num = b >> 4;
            szx = (uint8_t)(b & 7);
            if (szx == 7)
            {
                return emit_header(resp, pc_resp_cap, rsp_type, (uint8_t)COAP_RSP_BAD_OPTION, mid, token, tkl);
            }
            if (szx > PC_COAP_BLOCK_SZX_MAX)
            {
                szx = PC_COAP_BLOCK_SZX_MAX;
            }
            block_wise = PROTO_TRUE; // the client asked for block-wise transfer
        }
        else if (cresp.payload_len > (size_t)(1u << (szx + 4)))
        {
            block_wise = PROTO_TRUE; // body too large for one block -> start at block 0
        }
        if (block_wise)
        {
            uint32_t bsize = 1u << (szx + 4);
            size_t off = (size_t)num * bsize;
            // A block number past the end of the representation is a bad request.
            if (off > cresp.payload_len || (off == cresp.payload_len && num > 0))
            {
                return emit_header(resp, pc_resp_cap, rsp_type, (uint8_t)COAP_RSP_BAD_REQUEST, mid, token, tkl);
            }
            size_t this_len = cresp.payload_len - off;
            uint8_t more = 0;
            if (this_len > bsize)
            {
                this_len = bsize;
                more = 1;
            }
            block2_echo = (int32_t)((num << 4) | ((uint32_t)more << 3) | szx);
            cresp.payload += off;
            cresp.payload_len = this_len;
        }
    }
#endif

    // Build the response: header + token + options (ascending order) + payload.
    size_t n = emit_header(resp, pc_resp_cap, rsp_type, cresp.code, mid, token, tkl);
    if (n == 0)
    {
        return 0;
    }
    return emit_options_payload(resp, pc_resp_cap, n, cresp.code, observe_seq, cresp.content_format, block2_echo,
                                block1_echo, cresp.payload, cresp.payload_len);
}

size_t pc_coap_server_process(const uint8_t *req, size_t req_len, uint8_t *resp, size_t pc_resp_cap)
{
    return pc_coap_server_process_ex(req, req_len, resp, pc_resp_cap, -1); // no Observe option
}

#if PC_COAP_DEDUP_ENTRIES > 0
// ---------------------------------------------------------------------------
// Message de-duplication cache (RFC 7252 §4.5) - host-testable core
// ---------------------------------------------------------------------------

proto_bool pc_coap_dedup_lookup(const char *src_ip, uint16_t src_port, uint16_t mid, const uint8_t **out,
                                size_t *out_len)
{
    if (!src_ip)
    {
        return PROTO_FALSE;
    }
    uint32_t now = pc_millis();
    for (size_t i = 0; i < PC_COAP_DEDUP_ENTRIES; i++)
    {
        const CoapDedupEntry *e = &s_coap.dedup[i];
        if (e->valid && e->mid == mid && e->port == src_port && (now - e->stamp_ms) < PC_COAP_DEDUP_LIFETIME_MS &&
            strncmp(e->ip, src_ip, sizeof(e->ip)) == 0)
        {
            if (out)
            {
                *out = e->resp;
            }
            if (out_len)
            {
                *out_len = e->len;
            }
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

void pc_coap_dedup_store(const char *src_ip, uint16_t src_port, uint16_t mid, const uint8_t *resp, size_t len)
{
    if (!src_ip || !resp || len == 0 || len > PC_COAP_DEDUP_RESP_MAX)
    {
        return; // an over-large response is not cached - a retransmission simply re-processes it
    }
    uint32_t now = pc_millis();
    // Prefer the entry already holding this key, then a free / expired slot, else evict the oldest.
    size_t victim = 0;
    uint32_t oldest = 0;
    for (size_t i = 0; i < PC_COAP_DEDUP_ENTRIES; i++)
    {
        const CoapDedupEntry *e = &s_coap.dedup[i];
        if (e->valid && e->mid == mid && e->port == src_port && strncmp(e->ip, src_ip, sizeof(e->ip)) == 0)
        {
            victim = i;
            break;
        }
        if (!e->valid || (now - e->stamp_ms) >= PC_COAP_DEDUP_LIFETIME_MS)
        {
            victim = i;
            break;
        }
        uint32_t age = now - e->stamp_ms;
        if (age >= oldest)
        {
            oldest = age;
            victim = i;
        }
    }
    CoapDedupEntry *e = &s_coap.dedup[victim];
    size_t iplen = strnlen(src_ip, sizeof(e->ip) - 1);
    memcpy(e->ip, src_ip, iplen);
    e->ip[iplen] = 0;
    e->port = src_port;
    e->mid = mid;
    e->stamp_ms = now;
    e->len = (uint16_t)len;
    memcpy(e->resp, resp, len);
    e->valid = PROTO_TRUE;
}
#endif // PC_COAP_DEDUP_ENTRIES > 0

// ---------------------------------------------------------------------------
// UDP transport (pc_udp_listen is a host stub on non-Arduino builds)
// ---------------------------------------------------------------------------

#if PC_COAP_DEDUP_ENTRIES > 0
// If @p data is a Confirmable request already answered for this peer (RFC 7252 §4.5), resend the cached
// response and return true so the caller stops (the handler is not re-run). Else return false.
static proto_bool coap_dedup_replay(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, const char *ip,
                                    uint16_t port, proto_bool have_peer)
{
    if (!have_peer || len < 4 || ((data[0] >> 4) & 0x03) != (uint8_t)COAP_TYPE_CON)
    {
        return PROTO_FALSE;
    }
    uint16_t mid = (uint16_t)((data[2] << 8) | data[3]);
    const uint8_t *cached = NULL;
    size_t clen = 0;
    if (!pc_coap_dedup_lookup(ip, port, mid, &cached, &clen))
    {
        return PROTO_FALSE;
    }
    pc_udp_send(peer, cached, clen);
    return PROTO_TRUE;
}

// Cache the response just sent for a Confirmable request so its retransmission is deduplicated.
static void coap_dedup_remember(const uint8_t *data, size_t len, const char *ip, uint16_t port, proto_bool have_peer,
                                const uint8_t *resp, size_t resp_len)
{
    if (!have_peer || len < 4 || ((data[0] >> 4) & 0x03) != (uint8_t)COAP_TYPE_CON)
    {
        return;
    }
    uint16_t mid = (uint16_t)((data[2] << 8) | data[3]);
    pc_coap_dedup_store(ip, port, mid, resp, resp_len);
}
#endif // PC_COAP_DEDUP_ENTRIES > 0

#if PC_ENABLE_COAP_OBSERVE
// ---------------------------------------------------------------------------
// Observe registry + notifications (RFC 7641)
// ---------------------------------------------------------------------------

static int find_resource_index(const CoapCtx *c, const char *path)
{
    for (size_t i = 0; i < c->res_count; i++)
    {
        if (strcmp(c->res[i].path, path) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

static proto_bool same_token(const CoapObserver *o, const uint8_t *token, uint8_t tkl)
{
    return o->tkl == tkl && (tkl == 0 || memcmp(o->token, token, tkl) == 0);
}

// Find/refresh an observer; create one on first registration. Returns its slot or -1.
static int obs_register(const char *ip, uint16_t port, const uint8_t *token, uint8_t tkl, int res_idx)
{
    for (int i = 0; i < PC_COAP_MAX_OBSERVERS; i++)
    {
        if (s_coap.obs[i].active && s_coap.obs[i].res_idx == res_idx && s_coap.obs[i].port == port &&
            same_token(&s_coap.obs[i], token, tkl) && strcmp(s_coap.obs[i].ip, ip) == 0)
        {
            return i; // already observing
        }
    }
    for (int i = 0; i < PC_COAP_MAX_OBSERVERS; i++)
    {
        if (!s_coap.obs[i].active)
        {
            s_coap.obs[i].active = PROTO_TRUE;
            strncpy(s_coap.obs[i].ip, ip, sizeof(s_coap.obs[i].ip) - 1);
            s_coap.obs[i].ip[sizeof(s_coap.obs[i].ip) - 1] = '\0';
            s_coap.obs[i].port = port;
            s_coap.obs[i].tkl = tkl;
            if (tkl)
            {
                memcpy(s_coap.obs[i].token, token, tkl);
            }
            s_coap.obs[i].res_idx = res_idx;
            s_coap.obs[i].seq = 1;
            return i;
        }
    }
    return -1; // registry full -> observation declined (resource still returned)
}

// Remove a specific observation (deregister GET) or every observation from a peer
// (a Reset). Pass token=nullptr to drop all of @p ip:@p port.
static void obs_remove(const char *ip, uint16_t port, const uint8_t *token, uint8_t tkl)
{
    for (int i = 0; i < PC_COAP_MAX_OBSERVERS; i++)
    {
        if (s_coap.obs[i].active && s_coap.obs[i].port == port && strcmp(s_coap.obs[i].ip, ip) == 0 &&
            (token == NULL || same_token(&s_coap.obs[i], token, tkl)))
        {
            s_coap.obs[i].active = PROTO_FALSE;
        }
    }
}

void pc_coap_notify(const char *path)
{
    int ridx = find_resource_index(&s_coap, path);
    if (ridx < 0)
    {
        return;
    }
    for (int i = 0; i < PC_COAP_MAX_OBSERVERS; i++)
    {
        if (!s_coap.obs[i].active || s_coap.obs[i].res_idx != ridx)
        {
            continue;
        }
        // Re-render the resource via its GET handler.
        CoapRequest creq;
        creq.method = COAP_GET;
        creq.path = s_coap.res[ridx].path;
        creq.query = "";
        creq.payload = NULL;
        creq.payload_len = 0;
        creq.content_format = COAP_CF_NONE;
        CoapResponse cresp;
        cresp.code = (uint8_t)COAP_RSP_CONTENT;
        cresp.content_format = COAP_CF_NONE;
        cresp.payload = s_coap.pl;
        cresp.payload_cap = sizeof(s_coap.pl);
        cresp.payload_len = 0;
        s_coap.res[ridx].handler(&creq, &cresp);
        if (cresp.payload_len > sizeof(s_coap.pl))
        {
            cresp.payload_len = sizeof(s_coap.pl);
        }

        // Build a NON notification: header + token + Observe(seq) + body.
        uint16_t mid = (uint16_t)pc_millis();
        s_coap.obs[i].seq = (s_coap.obs[i].seq + 1) & 0xFFFFFF;
        size_t n = emit_header(s_coap.tx, sizeof(s_coap.tx), COAP_TYPE_NON, cresp.code, mid, s_coap.obs[i].token,
                               s_coap.obs[i].tkl);
        // GCOVR_EXCL_START  the n == 0 operand of both tests is unreachable: protocore_config.h enforces
        // PC_COAP_MSG_BUF_SIZE >= PC_COAP_MAX_PAYLOAD + 16 (>= 17), so emit_header() above always
        // has room for the 4-byte header plus a token of at most 8 bytes and never returns 0.
        if (n)
        {
            n = emit_options_payload(s_coap.tx, sizeof(s_coap.tx), n, cresp.code, (int32_t)s_coap.obs[i].seq,
                                     cresp.content_format, -1, -1, cresp.payload, cresp.payload_len);
        }
        if (!n || !pc_udp_listener_sendto(s_coap.port, s_coap.obs[i].ip, s_coap.obs[i].port, s_coap.tx, n))
        {
            s_coap.obs[i].active = PROTO_FALSE; // unreachable -> drop the observer
        }
        // GCOVR_EXCL_STOP
    }
}

static void coap_udp_handler(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)ctx;
    char ip[16];
    uint16_t pport = 0;
    proto_bool have_peer = pc_udp_peer_addr(peer, ip, sizeof(ip), &pport);

    // A Reset from a client rejects our notification -> drop its observations.
    if (len >= 1 && ((data[0] >> 4) & 0x03) == (uint8_t)COAP_TYPE_RST)
    {
        if (have_peer) // GCOVR_EXCL_LINE  host mock always supplies a peer; a null addr is ESP32/lwIP only
        {
            obs_remove(ip, pport, NULL, 0);
        }
        return;
    }

#if PC_COAP_DEDUP_ENTRIES > 0
    // A retransmitted CON we already answered is re-answered from the cache without re-dispatching.
    if (coap_dedup_replay(data, len, peer, ip, pport, have_peer))
    {
        return;
    }
#endif

    size_t rn = pc_coap_server_process_ex(data, len, s_coap.tx, sizeof(s_coap.tx), -1);
    if (!rn)
    {
        return;
    }

    // The have_peer operand of this test and of the `else if` below can only be false on an ESP32
    // lwIP receive with a null source address; the host UDP mock always injects a peer.
    if (s_coap.last_method == (uint8_t)COAP_GET && s_coap.last_observe == 0 && have_peer) // GCOVR_EXCL_LINE
    {
        int ridx = find_resource_index(&s_coap, s_coap.path);
        if (ridx >= 0)
        {
            int slot = obs_register(ip, pport, s_coap.last_token, s_coap.last_tkl, ridx);
            if (slot >= 0)
            {
                // Re-encode the response carrying the Observe option (registration ack).
                size_t rn2 =
                    pc_coap_server_process_ex(data, len, s_coap.tx, sizeof(s_coap.tx), (int32_t)s_coap.obs[slot].seq);
                // rn2 == 0 is unreachable: the re-encode differs from the call above - which already
                // returned rn > 0 - only by the Observe option, which append_opt() drops rather than
                // failing on, so the same request into the same buffer cannot now produce 0 bytes.
                if (rn2) // GCOVR_EXCL_LINE
                {
                    rn = rn2;
                }
            }
        }
    }
    else if (s_coap.last_observe == 1 && have_peer) // GCOVR_EXCL_LINE  have_peer is always true on the host
    {
        obs_remove(ip, pport, s_coap.last_token, s_coap.last_tkl);
    }

#if PC_COAP_DEDUP_ENTRIES > 0
    coap_dedup_remember(data, len, ip, pport, have_peer, s_coap.tx, rn); // cache for a future retransmission
#endif
    pc_udp_send(peer, s_coap.tx, rn);
}

void pc_coap_server_begin(uint16_t port)
{
    s_coap.port = port;
    for (int i = 0; i < PC_COAP_MAX_OBSERVERS; i++)
    {
        s_coap.obs[i].active = PROTO_FALSE;
    }
    pc_udp_listen(port, coap_udp_handler, NULL);
}

#else // Observe disabled: the basic request/response handler

static void coap_udp_handler(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)ctx;
#if PC_COAP_DEDUP_ENTRIES > 0
    char ip[16];
    uint16_t pport = 0;
    proto_bool have_peer = pc_udp_peer_addr(peer, ip, sizeof(ip), &pport);
    if (coap_dedup_replay(data, len, peer, ip, pport, have_peer))
    {
        return;
    }
#endif
    size_t rn = pc_coap_server_process(data, len, s_coap.tx, sizeof(s_coap.tx));
    if (rn)
    {
#if PC_COAP_DEDUP_ENTRIES > 0
        coap_dedup_remember(data, len, ip, pport, have_peer, s_coap.tx, rn);
#endif
        pc_udp_send(peer, s_coap.tx, rn);
    }
}

void pc_coap_server_begin(uint16_t port)
{
    pc_udp_listen(port, coap_udp_handler, NULL);
}

#endif // PC_ENABLE_COAP_OBSERVE

#endif // PC_ENABLE_COAP
