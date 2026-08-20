// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coap.c
 * @brief The CoAP server: the RFC 7252 sec 3 codec, the resource table, and the UDP binding.
 *
 * process() parses one request datagram, rejoins the Uri-Path and Uri-Query, dispatches against the
 * resource table, and writes a piggybacked response into the caller's buffer. It touches no socket.
 * begin() binds the receive port and feeds every datagram it delivers through the same path.
 */

#include "services/iot/coap/coap/coap.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from

#if PROTOCORE_ENABLE_COAP

#include "mmgr/protomem/protomem.h"                      // mem.cpy / mem.set / mem.cmp: the spans a message moves
#include "mmgr/protostr/protostr.h"                      // str.len / str.eq / str.copy: the bounded text moves
#include "network_drivers/transport/udp/server/server.h" // UdpListener: the receive port and its replies
#include "server/clock/clock.h"                          // Clock.millis: entry freshness and notification sequencing
#include "shared/ip/ip.h"                                // Ip.parse: an observer's address, per notification

PROTOCORE_BEGIN_DECLS

// The option numbers this server reads (RFC 7252 sec 5.10, RFC 7641 sec 2, RFC 7959 sec 2.1). Every
// other option number falls to the critical/elective rule in sec 5.4.1.
#define COAP_OPT_OBSERVE 6         ///< Observe (RFC 7641 sec 2)
#define COAP_OPT_URI_PATH 11       ///< Uri-Path (RFC 7252 sec 5.10.1)
#define COAP_OPT_CONTENT_FORMAT 12 ///< Content-Format (sec 5.10.3)
#define COAP_OPT_URI_QUERY 15      ///< Uri-Query (sec 5.10.1)
#define COAP_OPT_BLOCK2 23         ///< Block2 (RFC 7959 sec 2.1)
#define COAP_OPT_BLOCK1 27         ///< Block1 (RFC 7959 sec 2.1)

#define COAP_PAYLOAD_MARKER 0xFFu ///< RFC 7252 sec 3: the byte between the options and the payload
#define COAP_MAX_TOKEN 8u         ///< RFC 7252 sec 3: TKL 0..8, 9..15 reserved
#define COAP_HDR_LEN 4u           ///< RFC 7252 sec 3: Ver, T, TKL, Code and Message ID
#define COAP_SZX_RESERVED 7u      ///< RFC 7959 sec 2.2: the reserved block-size exponent
#define COAP_ENDPOINT_TEXT 16u    ///< bytes an endpoint's address text occupies, dotted-quad sized

/** @brief RFC 6690 sec 4: the well-known resource a discovery GET names. */
#define COAP_WELL_KNOWN_CORE "/.well-known/core"

/** @brief One resource-table row. */
typedef struct
{
    const char *path;
    uint8_t methods;
    CoapHandler handler;
} CoapResource;

#if PROTOCORE_COAP_DEDUP_ENTRIES > 0
/**
 * @brief One cached exchange (RFC 7252 sec 4.5).
 *
 * Keyed by the whole source endpoint and the Message ID, never a hash, so two endpoints cannot land
 * on one row and read each other's response. A repeated Confirmable message that hits a fresh row is
 * answered from @c resp without running the handler again.
 */
typedef struct
{
    proto_bool valid;
    char ip[COAP_ENDPOINT_TEXT];
    uint16_t port;
    uint16_t mid;
    uint32_t stamp_ms; ///< the clock at store; the row expires PROTOCORE_COAP_DEDUP_LIFETIME_MS later
    uint16_t len;      ///< octets of @c resp in use
    uint8_t resp[PROTOCORE_COAP_DEDUP_RESP_MAX];
} CoapDedupEntry;
#endif

#if PROTOCORE_ENABLE_COAP_OBSERVE
/** @brief One entry in the list of observers (RFC 7641 sec 4.1): a client endpoint and its Token. */
typedef struct
{
    proto_bool active;
    char ip[COAP_ENDPOINT_TEXT];
    uint16_t port;
    uint8_t token[COAP_MAX_TOKEN];
    uint8_t tkl;
    int32_t res_idx;
    uint32_t seq; ///< the sequence number the last notification carried (sec 4.4)
} CoapObserver;
#endif

/**
 * @brief The server's compile-time storage: the resource table, the codec scratch, and the caches.
 *
 * All of it BSS, so answering a request costs no heap and nothing lands on a task stack.
 */
struct CoapStorage
{
    CoapResource res[PROTOCORE_COAP_MAX_RESOURCES]; ///< the routing table, written before begin and read after
    size_t res_count;                               ///< rows in use

    char path[PROTOCORE_COAP_MAX_PATH];      ///< the Uri-Path of the request in flight, rejoined
    char query[PROTOCORE_COAP_MAX_QUERY];    ///< its Uri-Query, rejoined
    uint8_t pl[PROTOCORE_COAP_MAX_PAYLOAD];  ///< the body a handler writes
    uint8_t tx[PROTOCORE_COAP_MSG_BUF_SIZE]; ///< the outbound datagram; the inbound one is the transport's

#if PROTOCORE_COAP_DEDUP_ENTRIES > 0
    CoapDedupEntry dedup[PROTOCORE_COAP_DEDUP_ENTRIES]; ///< the deduplication cache (RFC 7252 sec 4.5)
#endif

#if PROTOCORE_ENABLE_COAP_OBSERVE
    uint16_t port;                                  ///< the bound port a notification leaves from
    CoapObserver obs[PROTOCORE_COAP_MAX_OBSERVERS]; ///< the list of observers (RFC 7641 sec 4.1)
    int32_t last_observe;                           ///< the Observe value of the request in flight, -1 when absent
    uint8_t last_code;                              ///< its Code byte
    uint8_t last_token[COAP_MAX_TOKEN];             ///< its Token
    uint8_t last_tkl;                               ///< that Token's length
#endif

#if PROTOCORE_ENABLE_COAP_BLOCK
    uint8_t b1[PROTOCORE_COAP_BLOCK1_MAX]; ///< the one Block1 body being reassembled (RFC 7959 sec 2.5)
    size_t b1_len;                         ///< octets reassembled, which is also the next expected offset
    uint8_t b1_szx;                        ///< the block-size exponent this transfer fixed
#endif
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define COAP_OFF_CTX 0u
static_assert(COAP_OFF_CTX + sizeof(struct CoapStorage) <= PROTOCORE_COAP_BORROW,
              "PROTOCORE_COAP_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define COAP_CTX(w) ((struct CoapStorage *)(void *)((w) + COAP_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_COAP_BORROW persistent bytes
} CoapOwnCtx;
static CoapOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_coap_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_COAP_BORROW).buf;
    }
    return s_own.span;
}

// ---------------------------------------------------------------------------
// Pure helpers: they read what they are given and hold nothing
// ---------------------------------------------------------------------------

// Two NUL-terminated runs are the same string within nul_cap bytes. The compare is bounded by the
// measured length, so neither side is read past its own terminator. An absent run matches nothing.
static proto_bool text_eq(const char *a, const char *b, size_t nul_cap)
{
    if (!a || !b)
    {
        return PROTO_FALSE;
    }
    size_t la = str.len(a, nul_cap);
    return la == str.len(b, nul_cap) && str.eq(a, b, la + 1u, PROTO_FALSE);
}

// An option value as a big-endian unsigned (RFC 7252 sec 3.2, the 'uint' option format).
static uint32_t opt_uint(const uint8_t *v, size_t n)
{
    uint32_t r = 0;
    for (size_t i = 0; i < n; i++)
    {
        r = (r << 8) | v[i];
    }
    return r;
}

// Append one Uri-Path or Uri-Query segment behind sep ('\0' for none), keeping the buffer terminated.
// False when the segment would not fit.
static proto_bool seg_append(char *buf, size_t cap, size_t *len, char sep, const uint8_t *seg, size_t seglen)
{
    size_t need = (sep ? 1u : 0u) + seglen + 1u; // separator, segment, terminator
    if (*len + need > cap)
    {
        return PROTO_FALSE;
    }
    if (sep)
    {
        buf[(*len)++] = sep;
    }
    mem.cpy(buf + *len, seg, seglen);
    *len += seglen;
    buf[*len] = '\0';
    return PROTO_TRUE;
}

// Write the 4-byte header and the Token (RFC 7252 sec 3), and nothing else. Returns the length, or 0
// when the buffer cannot hold that much.
static size_t emit_header(uint8_t *out, size_t cap, CoapType type, uint8_t code, uint16_t mid, const uint8_t *token,
                          uint8_t tkl)
{
    if (cap < (size_t)(COAP_HDR_LEN + tkl))
    {
        return 0;
    }
    out[0] = (uint8_t)((1 << 6) | ((uint8_t)type << 4) | tkl); // Ver = 1
    out[1] = code;
    out[2] = (uint8_t)(mid >> 8);
    out[3] = (uint8_t)(mid & 0xFF);
    if (tkl)
    {
        mem.cpy(out + COAP_HDR_LEN, token, tkl);
    }
    return (size_t)(COAP_HDR_LEN + tkl);
}

// Encode an unsigned into 0..3 big-endian bytes with the leading zeros dropped (RFC 7252 sec 3.2).
// Returns the byte count, 0 when v is 0.
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

// Append one option of number opt_num (at or above *last_opt) carrying vlen bytes, as the Option
// Delta / Option Length pair of RFC 7252 sec 3.1 with one extension byte when the delta reaches 13.
// Every value this server emits is shorter than 13 bytes, so no extended length is written. Writes
// nothing when it would not fit. Returns the new length.
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

// Append the options in ascending Option Number order (Observe, Content-Format, Block2, Block1) and
// then the Payload Marker and the payload, to a message already holding n header and Token bytes.
// observe_seq below 0 omits Observe, which is also omitted unless code is a 2.xx (RFC 7641 sec 4.2);
// block2_val / block1_val below 0 omit their option, whose value is the (NUM<<4)|(M<<3)|SZX of
// RFC 7959 sec 2.2. Stops with what fits rather than overrunning cap. Returns the new length.
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

#if PROTOCORE_ENABLE_COAP_BLOCK
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
        mem.cpy(resp + n, payload, payload_len);
        n += payload_len;
    }
    return n;
}

// The row whose path is @p path, or -1. The table is small and scanned end to end.
static int32_t find_resource_index(const uint8_t *work, const char *path)
{
    for (size_t i = 0; i < COAP_CTX(work)->res_count; i++)
    {
        if (text_eq(COAP_CTX(work)->res[i].path, path, PROTOCORE_COAP_MAX_PATH))
        {
            return (int32_t)i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// The resource table
// ---------------------------------------------------------------------------

// Empty the table and every cache.
void protocore_coap_reset(uint8_t *restrict work)
{
    COAP_CTX(work)->res_count = 0;
    mem.set(COAP_CTX(work)->res, 0, sizeof(COAP_CTX(work)->res));
#if PROTOCORE_ENABLE_COAP_BLOCK
    COAP_CTX(work)->b1_len = 0;
    COAP_CTX(work)->b1_szx = 0;
#endif
#if PROTOCORE_COAP_DEDUP_ENTRIES > 0
    for (size_t i = 0; i < PROTOCORE_COAP_DEDUP_ENTRIES; i++)
    {
        COAP_CTX(work)->dedup[i].valid = PROTO_FALSE;
    }
#endif
    CoapV.ok = PROTO_TRUE;
}

// Take one more row of the table for ns->resource.
void protocore_coap_add_resource(uint8_t *restrict work)
{
    if (COAP_CTX(work)->res_count >= PROTOCORE_COAP_MAX_RESOURCES || !CoapV.resource.path || !CoapV.resource.handler)
    {
        CoapV.ok = PROTO_FALSE;
        return;
    }
    COAP_CTX(work)->res[COAP_CTX(work)->res_count].path = CoapV.resource.path;
    COAP_CTX(work)->res[COAP_CTX(work)->res_count].methods = CoapV.resource.methods;
    COAP_CTX(work)->res[COAP_CTX(work)->res_count].handler = CoapV.resource.handler;
    COAP_CTX(work)->res_count++;
    CoapV.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// The codec: one request datagram in, one response datagram out
// ---------------------------------------------------------------------------

// Answer ns->msg.req into ns->msg.resp, reporting the response length in ns->n. A 2.xx response
// carries the Observe option when ns->observe.seq is at or above 0 (RFC 7641 sec 4.2).
void protocore_coap_process_observe(uint8_t *restrict work)
{
    const uint8_t *req = CoapV.msg.req;
    const size_t req_len = CoapV.msg.req_len;
    uint8_t *resp = CoapV.msg.resp;
    const size_t resp_cap = CoapV.msg.resp_cap;
    const int32_t observe_seq = CoapV.observe.seq;

    CoapV.n = 0;
#if PROTOCORE_ENABLE_COAP_OBSERVE
    // Cleared before any early return, so a request that never reaches the option walk cannot leave
    // the previous one's Code, Token and Observe value standing for it.
    COAP_CTX(work)->last_observe = -1;
    COAP_CTX(work)->last_code = 0;
    COAP_CTX(work)->last_tkl = 0;
#endif
    if (!req || !resp || req_len < COAP_HDR_LEN)
    {
        return; // shorter than the header
    }

    uint8_t ver = (req[0] >> 6) & 0x03;
    CoapType type = (CoapType)((req[0] >> 4) & 0x03);
    uint8_t tkl = req[0] & 0x0F;
    uint8_t code = req[1];
    uint16_t mid = (uint16_t)((req[2] << 8) | req[3]);

    // A Confirmable request takes a piggybacked Acknowledgement, a Non-confirmable one a
    // Non-confirmable response (RFC 7252 sec 5.2.1, sec 5.2.3). An Acknowledgement or Reset we
    // receive is not a request.
    if (type != COAP_TYPE_CON && type != COAP_TYPE_NON)
    {
        return;
    }
    CoapType rsp_type = (type == COAP_TYPE_CON) ? COAP_TYPE_ACK : COAP_TYPE_NON;

    // An unknown version is silently ignored and a reserved TKL is a message format error (RFC 7252
    // sec 3). Rejecting a Confirmable message is a matching Reset, which is Empty (sec 4.2); a
    // Non-confirmable one is dropped.
    if (ver != 1 || tkl > COAP_MAX_TOKEN)
    {
        if (type == COAP_TYPE_CON)
        {
            CoapV.n = emit_header(resp, resp_cap, COAP_TYPE_RST, 0, mid, NULL, 0);
        }
        return;
    }

    const uint8_t *p = req + COAP_HDR_LEN;
    const uint8_t *end = req + req_len;
    if (p + tkl > end)
    {
        if (type == COAP_TYPE_CON)
        {
            CoapV.n = emit_header(resp, resp_cap, COAP_TYPE_RST, 0, mid, NULL, 0);
        }
        return;
    }
    const uint8_t *token = p;
    p += tkl;

    // Code 0.00 is an Empty message (RFC 7252 sec 3). Confirmable, it elicits a Reset (sec 4.2).
    if (code == 0)
    {
        if (type == COAP_TYPE_CON)
        {
            CoapV.n = emit_header(resp, resp_cap, COAP_TYPE_RST, 0, mid, NULL, 0);
        }
        return;
    }

#if PROTOCORE_ENABLE_COAP_OBSERVE
    COAP_CTX(work)->last_code = code;
    COAP_CTX(work)->last_tkl = tkl;
    if (tkl)
    {
        mem.cpy(COAP_CTX(work)->last_token, token, tkl);
    }
#endif

    // Walk the options, rejoining Uri-Path and Uri-Query and reading Content-Format.
    size_t path_len = 0;
    size_t query_len = 0;
    COAP_CTX(work)->path[0] = '\0';
    COAP_CTX(work)->query[0] = '\0';
    CoapContentFormat req_cf = COAP_CF_NONE;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint32_t opt_num = 0;
    proto_bool format_error = PROTO_FALSE;     // RFC 7252 sec 3.1: a message format error
    proto_bool unknown_critical = PROTO_FALSE; // an unrecognized option of class critical (sec 5.4.1)
#if PROTOCORE_ENABLE_COAP_BLOCK
    int32_t req_block1 = -1; // the request's Block1 value (RFC 7959 sec 2.2), or -1 when absent
    int32_t req_block2 = -1; // its Block2 value, or -1 when absent
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
        if (delta == 15 || olen == 15) // 15 belongs to the Payload Marker (RFC 7252 sec 3.1)
        {
            format_error = PROTO_TRUE;
            break;
        }
        // The extended Option Delta and Option Length: 13 adds one byte, 14 adds two (sec 3.1).
        if (delta == 13)
        {
            if (p >= end)
            {
                format_error = PROTO_TRUE;
                break;
            }
            delta = (uint32_t)(*p++) + 13;
        }
        else if (delta == 14)
        {
            if (p + 2 > end)
            {
                format_error = PROTO_TRUE;
                break;
            }
            delta = (uint32_t)((p[0] << 8) | p[1]) + 269;
            p += 2;
        }
        if (olen == 13)
        {
            if (p >= end)
            {
                format_error = PROTO_TRUE;
                break;
            }
            olen = (uint32_t)(*p++) + 13;
        }
        else if (olen == 14)
        {
            if (p + 2 > end)
            {
                format_error = PROTO_TRUE;
                break;
            }
            olen = (uint32_t)((p[0] << 8) | p[1]) + 269;
            p += 2;
        }
        if (p + olen > end)
        {
            format_error = PROTO_TRUE;
            break;
        }
        opt_num += delta;
        const uint8_t *val = p;
        p += olen;

        switch (opt_num)
        {
        case COAP_OPT_URI_PATH:
            if (!seg_append(COAP_CTX(work)->path, sizeof(COAP_CTX(work)->path), &path_len, '/', val, olen))
            {
                format_error = PROTO_TRUE;
            }
            break;
        case COAP_OPT_URI_QUERY:
            if (!seg_append(COAP_CTX(work)->query, sizeof(COAP_CTX(work)->query), &query_len, query_len ? '&' : '\0',
                            val, olen))
            {
                format_error = PROTO_TRUE;
            }
            break;
        case COAP_OPT_CONTENT_FORMAT:
            req_cf = (CoapContentFormat)opt_uint(val, olen);
            break;
#if PROTOCORE_ENABLE_COAP_OBSERVE
        case COAP_OPT_OBSERVE:
            COAP_CTX(work)->last_observe = (int32_t)opt_uint(val, olen); // 0 registers, 1 deregisters
            break;
#endif
#if PROTOCORE_ENABLE_COAP_BLOCK
        case COAP_OPT_BLOCK1:
            if (olen > 3) // RFC 7959 sec 2.2: the value is a 0..3 byte uint
            {
                format_error = PROTO_TRUE;
            }
            else
            {
                req_block1 = (int32_t)opt_uint(val, olen);
            }
            break;
        case COAP_OPT_BLOCK2:
            if (olen > 3)
            {
                format_error = PROTO_TRUE;
            }
            else
            {
                req_block2 = (int32_t)opt_uint(val, olen);
            }
            break;
#endif
        default:
            // RFC 7252 sec 5.4.1: an unrecognized option of class critical, which is an odd Option
            // Number, answers 4.02 Bad Option; an elective one is silently ignored. Block1 and Block2
            // are critical, so a build without them rejects them here.
            if (opt_num & 1)
            {
                unknown_critical = PROTO_TRUE;
            }
            break;
        }
        if (format_error)
        {
            break;
        }
    }

    if (format_error)
    {
        CoapV.n = emit_header(resp, resp_cap, rsp_type, (uint8_t)COAP_RSP_BAD_REQUEST, mid, token, tkl);
        return;
    }
    if (unknown_critical)
    {
        CoapV.n = emit_header(resp, resp_cap, rsp_type, (uint8_t)COAP_RSP_BAD_OPTION, mid, token, tkl);
        return;
    }

    if (path_len == 0)
    {
        COAP_CTX(work)->path[0] = '/';
        COAP_CTX(work)->path[1] = '\0';
    }

    // RFC 7252 sec 5.8: "A request with an unrecognized or unsupported Method Code MUST generate a
    // 4.05 (Method Not Allowed) piggybacked response."
    if ((code >> 5) != 0 || code < (uint8_t)COAP_GET || code > (uint8_t)COAP_DELETE)
    {
        CoapV.n = emit_header(resp, resp_cap, rsp_type, (uint8_t)COAP_RSP_METHOD_NOT_ALLOWED, mid, token, tkl);
        return;
    }

    // What the emit path below serializes, filled either by the discovery listing or by a handler.
    CoapResponse cresp;
    cresp.code = (uint8_t)COAP_RSP_CONTENT;
    cresp.content_format = COAP_CF_NONE;
    cresp.payload = COAP_CTX(work)->pl;
    cresp.payload_cap = sizeof(COAP_CTX(work)->pl);
    cresp.payload_len = 0;
    int32_t block1_echo = -1; // the Block1 option the final block's response echoes

    // RFC 6690 sec 4: a GET on "/.well-known/core" returns the registered resources in the CoRE Link
    // Format of sec 2, `link-value-list` of `"<" URI-Reference ">"` separated by commas.
    if (text_eq(COAP_CTX(work)->path, COAP_WELL_KNOWN_CORE, PROTOCORE_COAP_MAX_PATH))
    {
        if (code != (uint8_t)COAP_GET)
        {
            CoapV.n = emit_header(resp, resp_cap, rsp_type, (uint8_t)COAP_RSP_METHOD_NOT_ALLOWED, mid, token, tkl);
            return;
        }
        size_t pl = 0;
        for (size_t i = 0; i < COAP_CTX(work)->res_count; i++)
        {
            const char *rpath = COAP_CTX(work)->res[i].path;
            size_t plen = str.len(rpath, sizeof(COAP_CTX(work)->pl));
            size_t need = (pl ? 1u : 0u) + 2u + plen; // ',' then '<' then the path then '>'
            if (pl + need > sizeof(COAP_CTX(work)->pl))
            {
                break; // the listing outgrew the body buffer; it ends on a whole link-value
            }
            if (pl)
            {
                COAP_CTX(work)->pl[pl++] = ',';
            }
            COAP_CTX(work)->pl[pl++] = '<';
            mem.cpy(COAP_CTX(work)->pl + pl, rpath, plen);
            pl += plen;
            COAP_CTX(work)->pl[pl++] = '>';
        }
        cresp.content_format = COAP_CF_LINK;
        cresp.payload_len = pl;
    }
    else
    {
        int32_t ridx = find_resource_index(work, COAP_CTX(work)->path);
        if (ridx < 0)
        {
            CoapV.n = emit_header(resp, resp_cap, rsp_type, (uint8_t)COAP_RSP_NOT_FOUND, mid, token, tkl);
            return;
        }
        const CoapResource *r = &COAP_CTX(work)->res[ridx];
        if (!(r->methods & (1u << code)))
        {
            CoapV.n = emit_header(resp, resp_cap, rsp_type, (uint8_t)COAP_RSP_METHOD_NOT_ALLOWED, mid, token, tkl);
            return;
        }

        const uint8_t *eff_payload = payload;
        size_t eff_payload_len = payload_len;

#if PROTOCORE_ENABLE_COAP_BLOCK
        // Block1 (RFC 7959 sec 2.5): reassemble a chunked POST or PUT body.
        if (req_block1 >= 0 && (code == (uint8_t)COAP_POST || code == (uint8_t)COAP_PUT))
        {
            uint32_t b = (uint32_t)req_block1;
            uint32_t num = b >> 4;                  // NUM, the block number
            uint8_t more = (uint8_t)((b >> 3) & 1); // M, the more flag
            uint8_t szx = (uint8_t)(b & 7);         // SZX, the size exponent
            if (szx == COAP_SZX_RESERVED)
            {
                // RFC 7959 sec 2.2: SZX 7 "MUST NOT be sent and MUST lead to a 4.00 Bad Request
                // response code upon reception in a request".
                CoapV.n = emit_header(resp, resp_cap, rsp_type, (uint8_t)COAP_RSP_BAD_REQUEST, mid, token, tkl);
                return;
            }
            uint32_t bsize = 1u << (szx + 4); // block size = 2**(SZX + 4)
            if (num == 0)                     // block number 0 starts a fresh body
            {
                COAP_CTX(work)->b1_len = 0;
                COAP_CTX(work)->b1_szx = szx;
            }
            // The size is fixed for one transfer and each block starts at NUM << (SZX + 4). A gap
            // means a block was lost or reordered, which sec 2.5 answers 4.08.
            if (szx != COAP_CTX(work)->b1_szx || (size_t)num * bsize != COAP_CTX(work)->b1_len)
            {
                COAP_CTX(work)->b1_len = 0;
                CoapV.n =
                    emit_header(resp, resp_cap, rsp_type, (uint8_t)COAP_RSP_REQUEST_ENTITY_INCOMPLETE, mid, token, tkl);
                return;
            }
            if (COAP_CTX(work)->b1_len + payload_len > sizeof(COAP_CTX(work)->b1))
            {
                COAP_CTX(work)->b1_len = 0;
                CoapV.n =
                    emit_header(resp, resp_cap, rsp_type, (uint8_t)COAP_RSP_REQUEST_ENTITY_TOO_LARGE, mid, token, tkl);
                return;
            }
            if (payload_len)
            {
                mem.cpy(COAP_CTX(work)->b1 + COAP_CTX(work)->b1_len, payload, payload_len);
            }
            COAP_CTX(work)->b1_len += payload_len;

            if (more)
            {
                // RFC 7959 sec 2.5: every success response to a non-final block is 2.31 Continue,
                // echoing Block1. The handler runs only on the final block.
                size_t cn = emit_header(resp, resp_cap, rsp_type, (uint8_t)COAP_RSP_CONTINUE, mid, token, tkl);
                if (cn == 0)
                {
                    return;
                }
                CoapV.n = emit_options_payload(resp, resp_cap, cn, (uint8_t)COAP_RSP_CONTINUE, -1, COAP_CF_NONE, -1,
                                               (int32_t)((num << 4) | (1u << 3) | szx), NULL, 0);
                return;
            }
            // The final block: the whole reassembled body goes to the handler.
            eff_payload = COAP_CTX(work)->b1;
            eff_payload_len = COAP_CTX(work)->b1_len;
            block1_echo = (int32_t)((num << 4) | szx); // M unset
        }
#endif

        CoapRequest creq;
        creq.method = (CoapMethod)code;
        creq.path = COAP_CTX(work)->path;
        creq.query = COAP_CTX(work)->query;
        creq.payload = eff_payload;
        creq.payload_len = eff_payload_len;
        creq.content_format = req_cf;

        r->handler(&creq, &cresp);
        if (cresp.payload_len > sizeof(COAP_CTX(work)->pl))
        {
            cresp.payload_len = sizeof(COAP_CTX(work)->pl); // a handler that overstated what it wrote
        }
    }

    int32_t block2_echo = -1;

#if PROTOCORE_ENABLE_COAP_BLOCK
    if (block1_echo >= 0)
    {
        COAP_CTX(work)->b1_len = 0; // the reassembled body reached the handler
    }

    // Block2 (RFC 7959 sec 2.4): serve a representation one block at a time, either because the
    // client asked with a Block2 option or because the body outgrew one block. Success bodies only.
    if ((cresp.code >> 5) == 2)
    {
        uint8_t szx = PROTOCORE_COAP_BLOCK_SZX_MAX;
        uint32_t num = 0;
        proto_bool block_wise = PROTO_FALSE;
        if (req_block2 >= 0)
        {
            uint32_t b = (uint32_t)req_block2;
            num = b >> 4;
            szx = (uint8_t)(b & 7);
            if (szx == COAP_SZX_RESERVED)
            {
                // RFC 7959 sec 2.2: the reserved SZX leads to 4.00 Bad Request.
                CoapV.n = emit_header(resp, resp_cap, rsp_type, (uint8_t)COAP_RSP_BAD_REQUEST, mid, token, tkl);
                return;
            }
            if (szx > PROTOCORE_COAP_BLOCK_SZX_MAX)
            {
                szx = PROTOCORE_COAP_BLOCK_SZX_MAX; // sec 2.4: the size asked for, or a smaller one
            }
            block_wise = PROTO_TRUE;
        }
        else if (cresp.payload_len > (size_t)(1u << (szx + 4)))
        {
            block_wise = PROTO_TRUE; // too large for one block, so start at block number 0
        }
        if (block_wise)
        {
            uint32_t bsize = 1u << (szx + 4);
            size_t off = (size_t)num * bsize;
            // A block number past the end of the representation names nothing.
            if (off > cresp.payload_len || (off == cresp.payload_len && num > 0))
            {
                CoapV.n = emit_header(resp, resp_cap, rsp_type, (uint8_t)COAP_RSP_BAD_REQUEST, mid, token, tkl);
                return;
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

    // The response: header, Token, the options in ascending order, the Payload Marker and the body.
    size_t n = emit_header(resp, resp_cap, rsp_type, cresp.code, mid, token, tkl);
    if (n == 0)
    {
        return;
    }
    CoapV.n = emit_options_payload(resp, resp_cap, n, cresp.code, observe_seq, cresp.content_format, block2_echo,
                                   block1_echo, cresp.payload, cresp.payload_len);
}

// Answer ns->msg.req with no Observe option in the response.
void protocore_coap_process(uint8_t *restrict work)
{
    CoapV.observe.seq = -1;
    protocore_coap_process_observe(work);
}

#if PROTOCORE_COAP_DEDUP_ENTRIES > 0
// ---------------------------------------------------------------------------
// Message deduplication (RFC 7252 sec 4.5)
// ---------------------------------------------------------------------------

// The response already sent for ns->exchange, when its row is still fresh.
void protocore_coap_dedup_lookup(uint8_t *restrict work)
{
    CoapV.ok = PROTO_FALSE;
    CoapV.bytes = NULL;
    CoapV.n = 0;
    if (!CoapV.exchange.src_ip)
    {
        return;
    }
    const uint32_t now = Clock.ms;
    for (size_t i = 0; i < PROTOCORE_COAP_DEDUP_ENTRIES; i++)
    {
        const CoapDedupEntry *e = &COAP_CTX(work)->dedup[i];
        if (e->valid && e->mid == CoapV.exchange.mid && e->port == CoapV.exchange.src_port &&
            (now - e->stamp_ms) < PROTOCORE_COAP_DEDUP_LIFETIME_MS &&
            text_eq(e->ip, CoapV.exchange.src_ip, sizeof(e->ip)))
        {
            CoapV.bytes = e->resp;
            CoapV.n = e->len;
            CoapV.ok = PROTO_TRUE;
            return;
        }
    }
}

// Keep the response sent for ns->exchange so its repeat is answered without the handler.
void protocore_coap_dedup_store(uint8_t *restrict work)
{
    CoapV.ok = PROTO_FALSE;
    if (!CoapV.exchange.src_ip || !CoapV.exchange.resp || CoapV.exchange.resp_len == 0 ||
        CoapV.exchange.resp_len > PROTOCORE_COAP_DEDUP_RESP_MAX)
    {
        return; // an over-long response is not cached, so its repeat is processed again
    }
    const uint32_t now = Clock.ms;
    // The row already holding this key, then a free or expired one, then the oldest.
    size_t victim = 0;
    uint32_t oldest = 0;
    for (size_t i = 0; i < PROTOCORE_COAP_DEDUP_ENTRIES; i++)
    {
        const CoapDedupEntry *e = &COAP_CTX(work)->dedup[i];
        if (e->valid && e->mid == CoapV.exchange.mid && e->port == CoapV.exchange.src_port &&
            text_eq(e->ip, CoapV.exchange.src_ip, sizeof(e->ip)))
        {
            victim = i;
            break;
        }
        if (!e->valid || (now - e->stamp_ms) >= PROTOCORE_COAP_DEDUP_LIFETIME_MS)
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
    CoapDedupEntry *e = &COAP_CTX(work)->dedup[victim];
    (void)str.copy(e->ip, CoapV.exchange.src_ip, sizeof(e->ip));
    e->port = CoapV.exchange.src_port;
    e->mid = CoapV.exchange.mid;
    e->stamp_ms = now;
    e->len = (uint16_t)CoapV.exchange.resp_len;
    mem.cpy(e->resp, CoapV.exchange.resp, CoapV.exchange.resp_len);
    e->valid = PROTO_TRUE;
    CoapV.ok = PROTO_TRUE;
}
#endif // PROTOCORE_COAP_DEDUP_ENTRIES > 0

// ---------------------------------------------------------------------------
// The UDP binding
// ---------------------------------------------------------------------------

// Send len octets back to the peer a handler was given.
static void peer_reply(const struct protocore_udp_peer *peer, const uint8_t *data, size_t len)
{
    UdpListenerV.peer_args.peer = peer;
    UdpListenerV.send_args.data = data;
    UdpListenerV.send_args.len = len;
    UdpListener.reply(protocore_udp_listener_span());
}

#if PROTOCORE_ENABLE_COAP_OBSERVE || PROTOCORE_COAP_DEDUP_ENTRIES > 0
// The sender's address and port, as text. False when the transport did not name one.
static proto_bool peer_text(const struct protocore_udp_peer *peer, char *ip, size_t cap, uint16_t *port)
{
    UdpListenerV.peer_args.peer = peer;
    UdpListenerV.peer_args.ip_out = ip;
    UdpListenerV.peer_args.ip_cap = cap;
    UdpListenerV.peer_args.port_out = port;
    UdpListener.peer_addr(protocore_udp_listener_span());
    return UdpListenerV.ok;
}

// The Type field of a datagram (RFC 7252 sec 3). The datagram must hold at least its first byte.
static CoapType dgram_type(const uint8_t *data)
{
    return (CoapType)((data[0] >> 4) & 0x03);
}
#endif

#if PROTOCORE_COAP_DEDUP_ENTRIES > 0
// The Message ID of a datagram (RFC 7252 sec 3). The datagram must hold the whole header.
static uint16_t dgram_mid(const uint8_t *data)
{
    return (uint16_t)((data[2] << 8) | data[3]);
}
#endif

#if PROTOCORE_ENABLE_COAP_OBSERVE
// ---------------------------------------------------------------------------
// The list of observers and its notifications (RFC 7641)
// ---------------------------------------------------------------------------

// The entry's Token is the one the request in flight carried (RFC 7641 sec 4.1).
static proto_bool same_token(const uint8_t *work, const CoapObserver *o)
{
    return o->tkl == COAP_CTX(work)->last_tkl &&
           (o->tkl == 0 || mem.cmp(o->token, COAP_CTX(work)->last_token, o->tkl) == 0);
}

// The entry is this endpoint's (RFC 7641 sec 4.1: the list is keyed by client endpoint and Token).
static proto_bool same_endpoint(const uint8_t *work, const CoapObserver *o)
{
    return o->port == CoapV.exchange.src_port && text_eq(o->ip, CoapV.exchange.src_ip, sizeof(o->ip));
}

// Add the request in flight to the list of observers of resource @p res_idx, or refresh the entry
// already there (sec 4.1: a matching endpoint/token pair updates rather than adds). Returns the slot,
// or -1 when the list is full, which leaves the GET answered without an Observe option.
static int32_t obs_register(uint8_t *restrict work, int32_t res_idx)
{
    for (int32_t i = 0; i < PROTOCORE_COAP_MAX_OBSERVERS; i++)
    {
        CoapObserver *o = &COAP_CTX(work)->obs[i];
        if (o->active && o->res_idx == res_idx && same_endpoint(work, o) && same_token(work, o))
        {
            return i;
        }
    }
    for (int32_t i = 0; i < PROTOCORE_COAP_MAX_OBSERVERS; i++)
    {
        CoapObserver *o = &COAP_CTX(work)->obs[i];
        if (!o->active)
        {
            o->active = PROTO_TRUE;
            (void)str.copy(o->ip, CoapV.exchange.src_ip, sizeof(o->ip));
            o->port = CoapV.exchange.src_port;
            o->tkl = COAP_CTX(work)->last_tkl;
            if (o->tkl)
            {
                mem.cpy(o->token, COAP_CTX(work)->last_token, o->tkl);
            }
            o->res_idx = res_idx;
            o->seq = 1;
            return i;
        }
    }
    return -1;
}

// Remove the endpoint's entry carrying the request's Token (sec 4.1, a deregister GET).
static void obs_drop_token(uint8_t *restrict work)
{
    for (int32_t i = 0; i < PROTOCORE_COAP_MAX_OBSERVERS; i++)
    {
        CoapObserver *o = &COAP_CTX(work)->obs[i];
        if (o->active && same_endpoint(work, o) && same_token(work, o))
        {
            o->active = PROTO_FALSE;
        }
    }
}

// Remove every entry of an endpoint (sec 4.5: a Reset rejecting a notification ends the observation).
static void obs_drop_endpoint(uint8_t *restrict work)
{
    for (int32_t i = 0; i < PROTOCORE_COAP_MAX_OBSERVERS; i++)
    {
        CoapObserver *o = &COAP_CTX(work)->obs[i];
        if (o->active && same_endpoint(work, o))
        {
            o->active = PROTO_FALSE;
        }
    }
}

// Send the current representation of ns->observe.path to every observer of it (RFC 7641 sec 4.2).
void protocore_coap_notify(uint8_t *restrict work)
{
    CoapV.ok = PROTO_FALSE;
    int32_t ridx = find_resource_index(work, CoapV.observe.path);
    if (ridx < 0)
    {
        return;
    }
    for (int32_t i = 0; i < PROTOCORE_COAP_MAX_OBSERVERS; i++)
    {
        CoapObserver *o = &COAP_CTX(work)->obs[i];
        if (!o->active || o->res_idx != ridx)
        {
            continue;
        }
        // Re-render the resource through its GET handler.
        CoapRequest creq;
        creq.method = COAP_GET;
        creq.path = COAP_CTX(work)->res[ridx].path;
        creq.query = "";
        creq.payload = NULL;
        creq.payload_len = 0;
        creq.content_format = COAP_CF_NONE;
        CoapResponse cresp;
        cresp.code = (uint8_t)COAP_RSP_CONTENT;
        cresp.content_format = COAP_CF_NONE;
        cresp.payload = COAP_CTX(work)->pl;
        cresp.payload_cap = sizeof(COAP_CTX(work)->pl);
        cresp.payload_len = 0;
        COAP_CTX(work)->res[ridx].handler(&creq, &cresp);
        if (cresp.payload_len > sizeof(COAP_CTX(work)->pl))
        {
            cresp.payload_len = sizeof(COAP_CTX(work)->pl);
        }

        // A Non-confirmable notification: header, Token, Observe, then the body. Sec 4.4 puts the
        // sequence number in the low 24 bits and requires it to rise for a given token and resource.
        uint16_t mid = (uint16_t)Clock.ms;
        o->seq = (o->seq + 1) & 0xFFFFFF;
        size_t n = emit_header(COAP_CTX(work)->tx, sizeof(COAP_CTX(work)->tx), COAP_TYPE_NON, cresp.code, mid, o->token,
                               o->tkl);
        if (n)
        {
            n = emit_options_payload(COAP_CTX(work)->tx, sizeof(COAP_CTX(work)->tx), n, cresp.code, (int32_t)o->seq,
                                     cresp.content_format, -1, -1, cresp.payload, cresp.payload_len);
        }
        protocore_ip dst = {PROTOCORE_IP_NONE, {0}};
        IpV.args.text = o->ip;
        IpV.args.out = &dst;
        Ip.parse(work);
        if (!n || !IpV.ok)
        {
            o->active = PROTO_FALSE; // unreachable, so the entry goes
            continue;
        }
        UdpListenerV.port = COAP_CTX(work)->port;
        UdpListenerV.send_args.dst = &dst;
        UdpListenerV.send_args.dst_port = o->port;
        UdpListenerV.send_args.data = COAP_CTX(work)->tx;
        UdpListenerV.send_args.len = n;
        UdpListener.sendto(protocore_udp_listener_span());
        if (!UdpListenerV.ok)
        {
            o->active = PROTO_FALSE;
        }
    }
    CoapV.ok = PROTO_TRUE;
}

static void coap_udp_handler(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *user)
{
    (void)user;
    uint8_t *work = protocore_coap_span();
    char ip[COAP_ENDPOINT_TEXT] = {0};
    uint16_t pport = 0;
    proto_bool have_peer = peer_text(peer, ip, sizeof(ip), &pport);
    CoapV.exchange.src_ip = ip;
    CoapV.exchange.src_port = pport;

    // A Reset rejects a notification, which ends every observation from that endpoint (sec 4.5).
    if (len >= 1 && dgram_type(data) == COAP_TYPE_RST)
    {
        if (have_peer)
        {
            obs_drop_endpoint(work);
        }
        return;
    }

    const proto_bool confirmable = have_peer && len >= COAP_HDR_LEN && dgram_type(data) == COAP_TYPE_CON;

    CoapV.msg.req = data;
    CoapV.msg.req_len = len;
    CoapV.msg.resp = COAP_CTX(work)->tx;
    CoapV.msg.resp_cap = sizeof(COAP_CTX(work)->tx);

#if PROTOCORE_COAP_DEDUP_ENTRIES > 0
    // A repeated Confirmable message is answered from the cache, without the handler (sec 4.5).
    if (confirmable)
    {
        CoapV.exchange.mid = dgram_mid(data);
        protocore_coap_dedup_lookup(work);
        if (CoapV.ok)
        {
            peer_reply(peer, CoapV.bytes, CoapV.n);
            return;
        }
    }
#else
    (void)confirmable;
#endif

    protocore_coap_process(work);
    if (CoapV.n == 0)
    {
        return;
    }
    size_t rn = CoapV.n;

    if (COAP_CTX(work)->last_code == (uint8_t)COAP_GET && COAP_CTX(work)->last_observe == 0 && have_peer)
    {
        int32_t ridx = find_resource_index(work, COAP_CTX(work)->path);
        if (ridx >= 0)
        {
            int32_t slot = obs_register(work, ridx);
            if (slot >= 0)
            {
                // Re-encode the same response carrying the Observe option, which is what tells the
                // client the entry was added (sec 3.1).
                CoapV.observe.seq = (int32_t)COAP_CTX(work)->obs[slot].seq;
                protocore_coap_process_observe(work);
                if (CoapV.n)
                {
                    rn = CoapV.n;
                }
            }
        }
    }
    else if (COAP_CTX(work)->last_observe == 1 && have_peer)
    {
        obs_drop_token(work);
    }

#if PROTOCORE_COAP_DEDUP_ENTRIES > 0
    if (confirmable)
    {
        CoapV.exchange.mid = dgram_mid(data);
        CoapV.exchange.resp = COAP_CTX(work)->tx;
        CoapV.exchange.resp_len = rn;
        protocore_coap_dedup_store(work);
    }
#endif
    peer_reply(peer, COAP_CTX(work)->tx, rn);
}

// Bind ns->bind.port and route its datagrams into the server, emptying the list of observers first.
void protocore_coap_begin(uint8_t *restrict work)
{
    COAP_CTX(work)->port = CoapV.bind.port;
    for (int32_t i = 0; i < PROTOCORE_COAP_MAX_OBSERVERS; i++)
    {
        COAP_CTX(work)->obs[i].active = PROTO_FALSE;
    }
    UdpListenerV.port = CoapV.bind.port;
    UdpListenerV.bind.handler = coap_udp_handler;
    UdpListenerV.bind.handler_ctx = NULL;
    UdpListener.listen(protocore_udp_listener_span());
    CoapV.ok = UdpListenerV.ok;
}

#else // the plain request and response path, with no list of observers

static void coap_udp_handler(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *user)
{
    (void)user;
    uint8_t *work = protocore_coap_span();
#if PROTOCORE_COAP_DEDUP_ENTRIES > 0
    char ip[COAP_ENDPOINT_TEXT] = {0};
    uint16_t pport = 0;
    proto_bool have_peer = peer_text(peer, ip, sizeof(ip), &pport);
    const proto_bool confirmable = have_peer && len >= COAP_HDR_LEN && dgram_type(data) == COAP_TYPE_CON;
    if (confirmable)
    {
        CoapV.exchange.src_ip = ip;
        CoapV.exchange.src_port = pport;
        CoapV.exchange.mid = dgram_mid(data);
        protocore_coap_dedup_lookup(work);
        if (CoapV.ok)
        {
            peer_reply(peer, CoapV.bytes, CoapV.n); // answered from the cache (RFC 7252 sec 4.5)
            return;
        }
    }
#endif
    CoapV.msg.req = data;
    CoapV.msg.req_len = len;
    CoapV.msg.resp = COAP_CTX(work)->tx;
    CoapV.msg.resp_cap = sizeof(COAP_CTX(work)->tx);
    protocore_coap_process(work);
    if (CoapV.n == 0)
    {
        return;
    }
    size_t rn = CoapV.n;
#if PROTOCORE_COAP_DEDUP_ENTRIES > 0
    if (confirmable)
    {
        CoapV.exchange.resp = COAP_CTX(work)->tx;
        CoapV.exchange.resp_len = rn;
        protocore_coap_dedup_store(work);
    }
#endif
    peer_reply(peer, COAP_CTX(work)->tx, rn);
}

// Bind ns->bind.port and route its datagrams into the server.
void protocore_coap_begin(uint8_t *restrict work)
{
    (void)work;
    UdpListenerV.port = CoapV.bind.port;
    UdpListenerV.bind.handler = coap_udp_handler;
    UdpListenerV.bind.handler_ctx = NULL;
    UdpListener.listen(protocore_udp_listener_span());
    CoapV.ok = UdpListenerV.ok;
}

#endif // PROTOCORE_ENABLE_COAP_OBSERVE

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
CoapVars CoapV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_COAP
