// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_parser.c
 * @brief Standalone HTTP/1.1 request parser - implementation.
 *
 * No dependency on transport, session, or lwIP.  Consumes one byte at a
 * time via http_parser_feed(); the presentation layer is responsible for
 * pulling bytes out of whatever transport buffer it uses.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP_PARSER

#include "http_parser.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"
#include "shared/ip/ip.h" // validate a recovered proxy client IP (v4/v6)

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

HttpReq http_pool[CONN_POOL_SLOTS];

// Streaming-body hooks (OTA / file upload), owned by one instance (internal linkage): null
// unless the application installs them. One named owner, unreachable cross-TU. (The http_pool[]
// request table is the shared cross-TU substrate.) Ungated, because storing them is what the setter
// does in every build; PROTOCORE_ENABLE_STREAM_BODY decides only whether the feed path calls them.
typedef struct
{
    HttpStreamBeginCb stream_begin;
    HttpStreamDataCb stream_data;
    HttpStreamAbortCb stream_abort;
} HttpParserCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define HTTP_PARSER_OFF_CTX 0u
static_assert(HTTP_PARSER_OFF_CTX + sizeof(HttpParserCtx) <= PROTOCORE_HTTP_PARSER_BORROW,
              "PROTOCORE_HTTP_PARSER_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define HTTP_PARSER_CTX(w) ((HttpParserCtx *)(void *)((w) + HTTP_PARSER_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_HTTP_PARSER_BORROW persistent bytes, or null while the pool was short
} HttpParserOwnCtx;
static HttpParserOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_http_parser_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_HTTP_PARSER_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

// The entries this file calls before reaching their definitions.
static void http_parser_get_header(uint8_t *restrict work);

static void http_parser_set_stream_hooks(uint8_t *restrict work)
{
    HttpStreamBeginCb begin = HttpParser.set_stream_hooks_args.begin;
    HttpStreamDataCb data = HttpParser.set_stream_hooks_args.data;
    HttpStreamAbortCb abort = HttpParser.set_stream_hooks_args.abort;

    HTTP_PARSER_CTX(work)->stream_begin = begin;
    HTTP_PARSER_CTX(work)->stream_data = data;
    HTTP_PARSER_CTX(work)->stream_abort = abort;
}

// ---------------------------------------------------------------------------
// FNV-1a hash constants for HTTP version validation
// ---------------------------------------------------------------------------
// The hash of the 8-byte version token ("HTTP/1.0" or "HTTP/1.1") is
// compared against the accumulated _version_hash when CR terminates the
// version field.
//
// Both tokens are fixed by RFC 7230, so their hashes are constants and are written as
// constants. The fold is h = (h ^ byte) * PROTOCORE_FNV_PRIME over the 8 bytes, seeded with
// PROTOCORE_FNV_OFFSET, which is the same fold the accumulator below runs per byte.

#define PROTOCORE_FNV_OFFSET 2166136261u
#define PROTOCORE_FNV_PRIME 16777619u

#define PROTOCORE_HASH_HTTP10 0xF69731FBu ///< FNV-1a of "HTTP/1.0"
#define PROTOCORE_HASH_HTTP11 0xF5973068u ///< FNV-1a of "HTTP/1.1"

// ---------------------------------------------------------------------------
// RFC 7230 character-class table (hot path)
// ---------------------------------------------------------------------------
//
// The per-byte parser classifies every request byte, so the three character
// classes below are folded into one 256-entry table built at compile time (it
// lands in flash .rodata). A hot-path check is then a single table load + a mask
// bit, instead of the range compares + 15-case switch it replaces:
//   0x01 tchar       - method + header field-name (RFC 7230 §3.2.6)
//   0x02 vchar       - request-target path/query bytes (RFC 5234 VCHAR = %x21-7E)
//   0x04 field-value - header field-value bytes (RFC 7230 §3.2: VCHAR/SP/HTAB/obs-text)

#define PROTOCORE_CC_TCHAR 0x01
#define PROTOCORE_CC_VCHAR 0x02
#define PROTOCORE_CC_FIELD_VALUE 0x04

// The 256-entry class table, one const byte per input octet (lands in flash .rodata). Written out as a
// literal, so it is data the compiler places rather than code it has to fold.
// Each entry ORs the classes that octet belongs to; regenerate via tools if the character classes ever change:
//   tchar = ALPHA/DIGIT/"!#$%&'*+-.^_`|~"  vchar = %x21-7E  field-value = HTAB/%x20-7E/obs-text(%x80-FF)
static const uint8_t kCharClass[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x07, 0x06, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x06, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x07, 0x00, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
};

static inline proto_bool is_tchar(uint8_t b)
{
    return (kCharClass[b] & PROTOCORE_CC_TCHAR) != 0;
}
static inline proto_bool is_vchar(uint8_t b)
{
    return (kCharClass[b] & PROTOCORE_CC_VCHAR) != 0;
}
static inline proto_bool is_field_value_char(uint8_t b)
{
    return (kCharClass[b] & PROTOCORE_CC_FIELD_VALUE) != 0;
}

/**
 * @brief Split a raw query string into key=value pairs.
 *
 * Operates in-place on `req->query[]`.  Pairs are `&`-separated; key and
 * value are split on the first `=`.  Keys or values longer than their
 * respective limits are silently truncated - the path itself remains valid.
 */
static void parse_query_params(HttpReq *req)
{
    const char *qs = req->query;
    size_t len = req->query_idx;
    size_t i = 0;

    while (i < len && req->query_count < MAX_QUERY_PARAMS)
    {
        QueryParam *qp = &req->query_params[req->query_count];
        size_t key_idx = 0;
        size_t val_idx = 0;
        proto_bool in_val = PROTO_FALSE;

        while (i < len)
        {
            char c = qs[i++];
            if (c == '&')
            {
                break;
            }
            if (c == '=' && !in_val)
            {
                in_val = PROTO_TRUE;
                continue;
            }
            if (!in_val && key_idx < QUERY_KEY_LEN - 1)
            {
                qp->key[key_idx++] = c;
            }
            else if (in_val && val_idx < QUERY_VAL_LEN - 1)
            {
                qp->val[val_idx++] = c;
            }
        }

        if (key_idx > 0)
        {
            req->query_count++;
        }
    }
}

static void http_parser_reset(uint8_t *restrict work)
{
    HttpReq *req = HttpParser.reset_args.req;

    uint8_t id = req->slot_id;

#if PROTOCORE_ENABLE_STREAM_BODY
    // A streamed body that never reached PARSE_COMPLETE is being torn down (peer
    // reset / timeout / error): let the sink release its resource before we wipe
    // the state. The normal-completion reset runs while parse_state==PARSE_COMPLETE
    // (the handler already finished the sink), so this fires only on abort.
    if (req->body_streaming && req->parse_state != PARSE_COMPLETE && HTTP_PARSER_CTX(work)->stream_abort)
    {
        HTTP_PARSER_CTX(work)->stream_abort(req);
    }
#endif
    *req = (HttpReq){0}; // zero all fields
    req->slot_id = id;   // restore slot identity
    req->parse_state = PARSE_METHOD;
    req->_version_hash = PROTOCORE_FNV_OFFSET; // seed the FNV-1a accumulator
}

static void http_parser_feed(uint8_t *restrict work)
{
    HttpReq *p = HttpParser.feed_args.req;
    uint8_t byte = HttpParser.feed_args.byte;

    // Terminal states (PARSE_COMPLETE / PARSE_ERROR / PARSE_ENTITY_TOO_LARGE / PARSE_URI_TOO_LONG) have no case
    // below, so they fall through to `default:` and no-op - no separate guard switch on the per-byte hot path.
    char c = (char)byte;

    switch (p->parse_state)
    {
    case PARSE_METHOD:
        if (c == ' ')
        {
            p->parse_state = PARSE_PATH;
            p->current_token_idx = 0;
        }
        else if (!is_tchar(byte))
        {
            // RFC 7230 §3.1.1: method = token; any non-tchar is malformed
            p->parse_state = PARSE_ERROR;
        }
        else if (p->current_token_idx < sizeof(p->method) - 1)
        {
            p->method[p->current_token_idx++] = c;
        }
        else
        {
            p->parse_state = PARSE_ERROR;
        }
        break;

    case PARSE_PATH:
        if (c == ' ')
        {
            p->parse_state = PARSE_VERSION;
        }
        else if (c == '?')
        {
            p->parse_state = PARSE_QUERY;
        }
        else if (!is_vchar(byte))
        {
            // RFC 3986 §3.3: path chars must be visible ASCII (or pct-encoded)
            p->parse_state = PARSE_ERROR;
        }
        else if (p->path_idx < MAX_PATH_LEN - 1)
        {
            p->path[p->path_idx++] = c;
        }
        else
        {
            p->parse_state = PARSE_URI_TOO_LONG;
        }
        break;

    case PARSE_QUERY:
        if (c == ' ')
        {
            parse_query_params(p);
            p->parse_state = PARSE_VERSION;
        }
        else if (!is_vchar(byte))
        {
            // Control chars and NUL are not valid query-string bytes
            p->parse_state = PARSE_ERROR;
        }
        else if (p->query_idx < MAX_QUERY_LEN - 1)
        {
            p->query[p->query_idx++] = c;
        }
        // Silently truncate - query overflow is a capacity limit, not a protocol error
        break;

    case PARSE_VERSION:
        if (c == '\r')
        {
            if (p->_version_hash == PROTOCORE_HASH_HTTP11)
            {
                p->version = HTTP_11;
            }
            else if (p->_version_hash == PROTOCORE_HASH_HTTP10)
            {
                p->version = HTTP_10;
            }
            else
            {
                p->version = HTTP_UNKNOWN;
            }
            p->parse_state = PARSE_EXPECT_LF;
        }
        else
        {
            p->_version_hash = (p->_version_hash ^ byte) * PROTOCORE_FNV_PRIME;
        }
        break;

    case PARSE_EXPECT_LF:
        if (c == '\n')
        {
            p->parse_state = PARSE_HEADER_KEY;
            p->current_token_idx = 0;
        }
        else
        {
            p->parse_state = PARSE_ERROR;
        }
        break;

    case PARSE_HEADER_KEY:
        if (c == '\r')
        {
            if (p->current_token_idx == 0)
            {
                // Blank line - end of headers
                p->parse_state = PARSE_EXPECT_BODY_LF;
            }
            else
            {
                // CR mid-key: malformed (RFC 7230 §3.2 requires CRLF after value)
                p->parse_state = PARSE_ERROR;
            }
        }
        else if (c == ':')
        {
            // Terminate the scratch key so Host / Content-Length detection works
            // regardless of whether this header is stored (header_count < MAX).
            size_t k = p->current_token_idx < MAX_KEY_LEN ? p->current_token_idx : MAX_KEY_LEN - 1;
            p->cur_key[k] = '\0';
            p->parse_state = PARSE_HEADER_VAL;
            p->current_token_idx = 0;
#if PROTOCORE_CAPTURE_AUTH_HEADER
            // The Authorization value (Digest / JWT bearer) exceeds MAX_VAL_LEN,
            // so capture it whole into a dedicated buffer independent of scratch.
            p->cur_is_auth = (str.eq(p->cur_key, "Authorization", MAX_KEY_LEN, PROTO_TRUE));
            if (p->cur_is_auth)
            {
                p->auth_idx = 0;
            }
#endif
        }
        else if (!is_tchar(byte))
        {
            // RFC 7230 §3.2: field-name = token; any non-tchar is malformed
            p->parse_state = PARSE_ERROR;
        }
        else
        {
            uint8_t h = p->header_count;
            if (p->current_token_idx < MAX_KEY_LEN - 1)
            {
                // Always capture into the scratch key; also store into the
                // header slot when one is still available.
                p->cur_key[p->current_token_idx] = c;
                if (h < MAX_HEADERS)
                {
                    p->headers[h].key[p->current_token_idx] = c;
                }
                p->current_token_idx++;
            }
            // An over-long key is silently capped (a capacity limit, not an
            // error): the scratch/stored key is already full and the excess is
            // ignored. A truncated key cannot match the short Host/Content-Length
            // names, and not failing the request keeps long but valid header names
            // (CORS, Sec-WebSocket-Extensions, ...) working. Mirrors the value path.
        }
        break;

    case PARSE_HEADER_VAL:
        // Strip leading OWS (SP or HTAB) after the colon - RFC 9110 §5.6.3
        if ((c == ' ' || c == '\t') && p->current_token_idx == 0)
        {
            break;
        }
        if (c == '\r')
        {
            uint8_t h = p->header_count;

            // Terminate the scratch value so detection sees a clean C string. RFC 9112 §5.1:
            // field-line = field-name ":" OWS field-value OWS, and the OWS after the last
            // non-whitespace octet is excluded from the value. The leading OWS is dropped above.
            size_t vlen = p->current_token_idx < MAX_VAL_LEN ? p->current_token_idx : MAX_VAL_LEN - 1;
            while (vlen > 0 && (p->cur_val[vlen - 1] == ' ' || p->cur_val[vlen - 1] == '\t'))
            {
                vlen--;
            }
            p->cur_val[vlen] = '\0';
            if (h < MAX_HEADERS)
            {
                p->headers[h].val[vlen] = '\0';
            }
#if PROTOCORE_CAPTURE_AUTH_HEADER
            if (p->cur_is_auth)
            {
                while (p->auth_idx > 0 &&
                       (p->authorization[p->auth_idx - 1] == ' ' || p->authorization[p->auth_idx - 1] == '\t'))
                {
                    p->auth_idx--;
                }
                p->authorization[p->auth_idx] = '\0';
                p->cur_is_auth = PROTO_FALSE;
            }
#endif

            // Host / Content-Length detection works off the scratch copies, so
            // it is correct even for headers past MAX_HEADERS (RFC 7230 §5.4,
            // §3.3.2).
            if (str.eq(p->cur_key, "Host", MAX_KEY_LEN, PROTO_TRUE))
            {
                p->host_count++;
            }

            if (str.eq(p->cur_key, "Content-Length", MAX_KEY_LEN, PROTO_TRUE))
            {
                // RFC 7230 §3.3.2: Content-Length = 1*DIGIT.
                size_t cl = 0;
                proto_bool valid = (p->cur_val[0] != '\0');
                for (const char *q = p->cur_val; *q; q++)
                {
                    if (*q < '0' || *q > '9')
                    {
                        valid = PROTO_FALSE;
                        break;
                    }
                    cl = cl * 10 + (size_t)(*q - '0');
                }
                // A non-numeric value, or a second Content-Length whose value
                // disagrees with the first, is a fatal framing error (request
                // smuggling vector) → 400.
                if (!valid || (p->content_length_count > 0 && cl != p->content_length))
                {
                    p->parse_state = PARSE_ERROR;
                    break;
                }
                p->content_length = cl;
                p->content_length_count++;
            }

            // RFC 9112 §6.1/§6.3: this server does not decode chunked request bodies,
            // and a Transfer-Encoding present with (or instead of) Content-Length is a
            // request-smuggling vector - the chunked octets would otherwise be left in
            // the buffer and reparsed as the next request. Reject any request bearing
            // Transfer-Encoding (fail closed).
            if (str.eq(p->cur_key, "Transfer-Encoding", MAX_KEY_LEN, PROTO_TRUE))
            {
                p->parse_state = PARSE_ERROR;
                break;
            }

            if (h < MAX_HEADERS)
            {
                p->header_count++;
            }

            p->parse_state = PARSE_EXPECT_LF;
            p->current_token_idx = 0;
        }
        else if (!is_field_value_char(byte))
        {
            // RFC 7230 §3.2: control chars and NUL are not valid in field values
            p->parse_state = PARSE_ERROR;
        }
        else
        {
#if PROTOCORE_CAPTURE_AUTH_HEADER
            // Capture the full Authorization value (Digest / JWT) past MAX_VAL_LEN.
            if (p->cur_is_auth && p->auth_idx < PROTOCORE_AUTH_HDR_CAP - 1)
            {
                p->authorization[p->auth_idx++] = c;
            }
#endif
            if (p->current_token_idx < MAX_VAL_LEN - 1)
            {
                // Always capture into the scratch value; also store into the
                // header slot when one is still available.
                uint8_t h = p->header_count;
                p->cur_val[p->current_token_idx] = c;
                if (h < MAX_HEADERS)
                {
                    p->headers[h].val[p->current_token_idx] = c;
                }
                p->current_token_idx++;
            }
            // Silently truncate the scratch/stored value - capacity limit, not an error.
        }
        break;

    case PARSE_EXPECT_BODY_LF:
        /*
         * Consumes the LF of the blank-line CRLF that ends the header block.
         * Decides the next state based on Content-Length:
         *   > BODY_BUF_SIZE → 413 Payload Too Large
         *   == 0            → PARSE_COMPLETE (no body)
         *   else            → PARSE_BODY
         */
        if (c == '\n')
        {
            // RFC 7230 §5.4: a request MUST NOT carry more than one Host header
            // (always enforced); an HTTP/1.1 request MUST carry exactly one Host
            // header (enforced only when PROTOCORE_ENFORCE_HOST_HEADER is set).
            proto_bool host_violation = (p->host_count > 1);
#if PROTOCORE_ENFORCE_HOST_HEADER
            if (p->version == HTTP_11 && p->host_count == 0)
            {
                host_violation = PROTO_TRUE;
            }
#endif
            if (host_violation)
            {
                p->parse_state = PARSE_ERROR;
            }
#if PROTOCORE_ENABLE_STREAM_BODY
            // Streaming sink (OTA / upload): all headers are parsed here, so the
            // hook can match method/path/Authorization and begin a sink (Update
            // or a file). If it accepts, the body streams in chunks and the size
            // cap is bypassed; the matching route handler still runs at COMPLETE.
            else if (p->content_length > 0 && HTTP_PARSER_CTX(work)->stream_begin &&
                     HTTP_PARSER_CTX(work)->stream_begin(p))
            {
                p->body_streaming = PROTO_TRUE;
                p->parse_state = PARSE_BODY;
            }
#endif
            else if (p->content_length > BODY_BUF_SIZE)
            {
                p->parse_state = PARSE_ENTITY_TOO_LARGE;
            }
            else if (p->content_length == 0)
            {
                p->body[0] = '\0';
                p->parse_state = PARSE_COMPLETE;
            }
            else
            {
                p->parse_state = PARSE_BODY;
            }
        }
        else
        {
            p->parse_state = PARSE_ERROR;
        }
        break;

    case PARSE_BODY:
        // Body is opaque data - no character validation.
#if PROTOCORE_ENABLE_STREAM_BODY
        if (p->body_streaming)
        {
            // Reuse body[] as a flush buffer: fill it, then hand whole chunks to
            // the sink. No BODY_BUF_SIZE cap on the total - the body never lives
            // in RAM all at once.
            p->body[p->body_len++] = byte;
            if (p->body_len == BODY_BUF_SIZE)
            {
                if (HTTP_PARSER_CTX(work)->stream_data)
                {
                    HTTP_PARSER_CTX(work)->stream_data(p, p->body, p->body_len);
                }
                p->body_len = 0;
            }
            p->body_bytes_read++;
            if (p->body_bytes_read >= p->content_length)
            {
                if (p->body_len && HTTP_PARSER_CTX(work)->stream_data)
                {
                    HTTP_PARSER_CTX(work)->stream_data(p, p->body, p->body_len); // flush the tail
                }
                p->body_len = 0;
                p->body[0] = '\0';
                p->parse_state = PARSE_COMPLETE;
            }
            break;
        }
#endif
        if (p->body_len < BODY_BUF_SIZE)
        {
            p->body[p->body_len++] = byte;
        }
        p->body_bytes_read++;
        if (p->body_bytes_read >= p->content_length)
        {
            p->body[p->body_len] = '\0';
            p->parse_state = PARSE_COMPLETE;
        }
        break;

    default:
        break;
    }
}

static void http_parser_get_header(uint8_t *restrict work)
{
    (void)work;
    const HttpReq *req = HttpParser.get_header_args.req;
    const char *key = HttpParser.get_header_args.key;

    for (uint8_t i = 0; i < req->header_count; i++)
    {
        if (str.eq(req->headers[i].key, key, MAX_KEY_LEN, PROTO_TRUE))
        {
            HttpParser.text = req->headers[i].val;
            return;
        }
    }
    HttpParser.text = NULL;
}

static void http_parser_get_cookie(uint8_t *restrict work)
{
    const HttpReq *req = HttpParser.get_cookie_args.req;
    const char *name = HttpParser.get_cookie_args.name;
    char *out = HttpParser.get_cookie_args.out;
    size_t out_size = HttpParser.get_cookie_args.out_size;

    if (out == NULL || out_size == 0)
    {
        HttpParser.ok = PROTO_FALSE;
        return;
    }
    out[0] = '\0';
    if (req == NULL || name == NULL || name[0] == '\0')
    {
        HttpParser.ok = PROTO_FALSE;
        return;
    }

    // RFC 6265 4.2.1: the request "Cookie" header is "name1=value1; name2=value2".
    // Names are case-sensitive; a value may be DQUOTE-wrapped.
    HttpParser.get_header_args.req = req;
    HttpParser.get_header_args.key = "Cookie";
    http_parser_get_header(work);
    const char *c = HttpParser.text;
    if (c == NULL)
    {
        HttpParser.ok = PROTO_FALSE;
        return;
    }
    const size_t clen = str.len(c, MAX_VAL_LEN);
    const size_t nlen = str.len(name, MAX_VAL_LEN); // a matchable cookie-name span cannot exceed a header value

    size_t at = 0;
    for (size_t g = 0; g < clen && at < clen; ++g) // each pair consumes at least its ';', so clen bounds the trips
    {
        const char *semi = str.find(c + at, clen - at, ";", sizeof(";"), PROTO_FALSE);
        const size_t stop = semi ? (size_t)(semi - c) : clen;

        size_t a = at;
        while (a < stop && (c[a] == ' ' || c[a] == '\t')) // the OWS this pair opens with
        {
            a++;
        }

        const char *eqp = str.find(c + a, stop - a, "=", sizeof("="), PROTO_FALSE);
        if (eqp)
        {
            const size_t eq = (size_t)(eqp - c);
            if (eq - a == nlen && str.diff(c + a, name, nlen, PROTO_FALSE) == nlen)
            {
                size_t v = eq + 1u;
                size_t end = stop;
                while (end > v && (c[end - 1] == ' ' || c[end - 1] == '\t')) // trailing OWS
                {
                    end--;
                }
                size_t vlen = end - v;
                if (vlen >= 2u && c[v] == '"' && c[end - 1u] == '"') // a DQUOTE-wrapped cookie-value
                {
                    v++;
                    vlen -= 2u;
                }
                if (vlen >= out_size)
                {
                    vlen = out_size - 1u;
                }
                mem.cpy(out, c + v, vlen);
                out[vlen] = '\0';
                HttpParser.ok = PROTO_TRUE;
                return;
            }
        }
        at = stop + 1u;
    }
    HttpParser.ok = PROTO_FALSE;
}

// Extract and validate a Forwarded / X-Forwarded-For client-address token from
// [s, s+n) into out (canonical text). Accepts IPv4 with an optional ":port", a
// bracketed IPv6 "[2001:db8::1]:port" (RFC 7239 §6), and a bare IPv6 (the de-facto
// X-Forwarded-For form). The candidate is confirmed with protocore_ip_parse, so "unknown",
// an obfuscated "_id" identifier (RFC 7239 §6.3), or any malformed token returns
// false. Returns true and writes the RFC 5952 canonical address on success.
static proto_bool fwd_extract_client(const char *s, size_t n, char *out, size_t cap)
{
    // Trim leading/trailing OWS and a wrapping DQUOTE (RFC 7239 quotes the v6+port form).
    while (n > 0 && (*s == ' ' || *s == '\t'))
    {
        s++;
        n--;
    }
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
    {
        n--;
    }
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"')
    {
        s++;
        n -= 2;
    }
    if (n == 0)
    {
        return PROTO_FALSE;
    }

    char tok[PROTOCORE_IP_STR_MAX];
    size_t tlen = 0;
    if (s[0] == '[')
    {
        // Bracketed IPv6: take the text between '[' and ']'; a trailing ":port" is ignored.
        // Reachable: n is bounded by MAX_VAL_LEN-1 (47) only when this is the whole stored
        // header value. Via "Forwarded: for=[...]" the "for=" prefix consumes 4 of those 47
        // bytes before n is even measured, capping bracket content at 43 - short of the 46
        // needed to trip this guard. But fwd_extract_client() is also called directly on
        // X-Forwarded-For (no "for=" prefix), so the full 47 bytes are available there:
        // '[' + 46 non-']' content bytes drives tlen to sizeof(tok)-1 (45) and trips the guard
        // on the 46th byte.
        size_t i = 1;
        for (; i < n && s[i] != ']'; i++)
        {
            if (tlen + 1 >= sizeof(tok))
            {
                return PROTO_FALSE;
            }
            tok[tlen++] = s[i];
        }
        if (i >= n) // unterminated bracket
        {
            return PROTO_FALSE;
        }
    }
    else
    {
        // A single colon means "IPv4:port" (address up to the colon); two or more colons
        // mean a bare IPv6 literal (kept whole - no port stripping).
        int colons = 0;
        for (size_t i = 0; i < n; i++)
        {
            if (s[i] == ':')
            {
                colons++;
            }
        }
        size_t take = n;
        if (colons <= 1)
        {
            for (size_t i = 0; i < n; i++)
            {
                if (s[i] == ':')
                {
                    take = i;
                    break;
                }
            }
        }
        if (take == 0 || take + 1 > sizeof(tok))
        {
            return PROTO_FALSE;
        }
        mem.cpy(tok, s, take);
        tlen = take;
    }
    tok[tlen] = '\0';

    protocore_ip ip;
    Ip.args.text = tok;
    Ip.args.out = &ip;
    Ip.parse(ip_work); // rejects "unknown" / "_obf" / malformed
    if (!Ip.ok)
    {
        return PROTO_FALSE;
    }
    Ip.args.ip = &ip;
    Ip.args.buf = out;
    Ip.args.cap = cap;
    Ip.format(ip_work);
    return Ip.n > 0; // false if out is too small for the canonical text
}

// Index just past the DQUOTE that closes the one at @p i. RFC 7230 §3.2.6:
// quoted-string = DQUOTE *( qdtext / quoted-pair ) DQUOTE and quoted-pair = "\" octet, so the
// closing DQUOTE is the first one no backslash reached.
static size_t fwd_skip_quoted(const char *s, size_t n, size_t i)
{
    size_t at = i + 1u;
    for (size_t g = 0; g < n && at < n; ++g)
    {
        const char *dq = str.find(s + at, n - at, "\"", sizeof("\""), PROTO_FALSE);
        if (!dq)
        {
            return n;
        }
        const size_t span = (size_t)(dq - (s + at));
        const char *bs = span ? str.find(s + at, span, "\\", sizeof("\\"), PROTO_FALSE) : NULL;
        if (!bs)
        {
            return (size_t)(dq - s) + 1u;
        }
        at = (size_t)(bs - s) + 2u;
    }
    return n;
}

// Index of the first @p stop octet outside a quoted-string, or @p n. @p stop is a one-octet
// literal, so its needle capacity is the octet and its NUL.
static size_t fwd_split(const char *s, size_t n, const char *stop)
{
    size_t at = 0;
    for (size_t g = 0; g < n && at < n; ++g)
    {
        const char *hit = str.find(s + at, n - at, stop, 2u, PROTO_FALSE);
        if (!hit)
        {
            return n;
        }
        const size_t span = (size_t)(hit - (s + at));
        const char *dq = span ? str.find(s + at, span, "\"", sizeof("\""), PROTO_FALSE) : NULL;
        if (!dq)
        {
            return (size_t)(hit - s);
        }
        at = fwd_skip_quoted(s, n, (size_t)(dq - s));
    }
    return n;
}

// RFC 7239 §4: forwarded-element = [ forwarded-pair ] *( ";" [ forwarded-pair ] ),
// forwarded-pair = token "=" value, and "the parameter names are case-insensitive". The ABNF puts
// no OWS around "=" or ";", so the name is the whole span before "=" and a longer token ending in
// it never answers. The value span lands in @p vlen.
static const char *fwd_param(const char *el, size_t n, const char *name, size_t nlen, size_t *vlen)
{
    size_t at = 0;
    for (size_t g = 0; g < n && at < n; ++g)
    {
        const size_t stop = at + fwd_split(el + at, n - at, ";");
        const size_t eq = at + fwd_split(el + at, stop - at, "=");
        if (eq < stop && eq - at == nlen && str.diff(el + at, name, nlen, PROTO_TRUE) == nlen)
        {
            *vlen = stop - (eq + 1u);
            return el + eq + 1u;
        }
        at = stop + 1u;
    }
    return NULL;
}

// RFC 7239 §4: value = token / quoted-string, so a parameter value can arrive quoted.
static proto_bool fwd_value_is(const char *v, size_t n, const char *lit, size_t litlen)
{
    if (n >= 2u && v[0] == '"' && v[n - 1u] == '"')
    {
        v++;
        n -= 2u;
    }
    return n == litlen && str.diff(v, lit, litlen, PROTO_TRUE) == litlen;
}

static void http_parser_forwarded_client(uint8_t *restrict work)
{
    const HttpReq *req = HttpParser.forwarded_client_args.req;
    char *ip_out = HttpParser.forwarded_client_args.ip_out;
    size_t ip_cap = HttpParser.forwarded_client_args.ip_cap;
    proto_bool *is_https = HttpParser.forwarded_client_args.is_https;

    if (is_https)
    {
        *is_https = PROTO_FALSE;
    }
    if (!ip_out || ip_cap == 0 || !req)
    {
        HttpParser.ok = PROTO_FALSE;
        return;
    }
    ip_out[0] = '\0';

    // Prefer RFC 7239 "Forwarded" (the leftmost element is the original client):
    //   Forwarded: for=192.0.2.60;proto=https, for=198.51.100.1
    HttpParser.get_header_args.req = req;
    HttpParser.get_header_args.key = "Forwarded";
    http_parser_get_header(work);
    const char *fwd = HttpParser.text;
    if (fwd)
    {
        // RFC 7239 §4: Forwarded = 1#forwarded-element, the leftmost holding what the first proxy
        // added. A ',' inside a quoted-string does not end it.
        const size_t hlen = str.len(fwd, MAX_VAL_LEN);
        const size_t elen = fwd_split(fwd, hlen, ",");
        if (is_https)
        {
            size_t plen = 0;
            const char *proto = fwd_param(fwd, elen, "proto", 5u, &plen);
            if (proto)
            {
                *is_https = fwd_value_is(proto, plen, "https", 5u);
            }
        }
        size_t flen = 0;
        const char *f = fwd_param(fwd, elen, "for", 3u, &flen);
        if (f && fwd_extract_client(f, flen, ip_out, ip_cap))
        {
            HttpParser.ok = PROTO_TRUE;
            return;
        }
    }

    // De-facto X-Forwarded-For (comma list; leftmost = original client) + X-Forwarded-Proto.
    if (is_https)
    {
        HttpParser.get_header_args.req = req;
        HttpParser.get_header_args.key = "X-Forwarded-Proto";
        http_parser_get_header(work);
        const char *xfp = HttpParser.text;
        if (xfp && str.starts(xfp, "https", 5, PROTO_TRUE))
        {
            *is_https = PROTO_TRUE;
        }
    }
    HttpParser.get_header_args.req = req;
    HttpParser.get_header_args.key = "X-Forwarded-For";
    http_parser_get_header(work);
    const char *xff = HttpParser.text;
    if (xff)
    {
        const char *end = str.find(xff, MAX_VAL_LEN, ",", sizeof(","), PROTO_FALSE);
        size_t len = end ? (size_t)(end - xff) : str.len(xff, MAX_VAL_LEN);
        if (fwd_extract_client(xff, len, ip_out, ip_cap))
        {
            HttpParser.ok = PROTO_TRUE;
            return;
        }
    }
    HttpParser.ok = PROTO_FALSE;
}

static void http_parser_get_query(uint8_t *restrict work)
{
    (void)work;
    const HttpReq *req = HttpParser.get_query_args.req;
    const char *key = HttpParser.get_query_args.key;

    for (uint8_t i = 0; i < req->query_count; i++)
    {
        if (str.eq(req->query_params[i].key, key, QUERY_KEY_LEN, PROTO_FALSE))
        {
            HttpParser.text = req->query_params[i].val;
            return;
        }
    }
    HttpParser.text = NULL;
}

static void http_parser_get_form(uint8_t *restrict work)
{
    const HttpReq *req = HttpParser.get_form_args.req;
    const char *key = HttpParser.get_form_args.key;
    char *out = HttpParser.get_form_args.out;
    size_t out_size = HttpParser.get_form_args.out_size;

    if (out == NULL || out_size == 0)
    {
        HttpParser.ok = PROTO_FALSE;
        return;
    }
    out[0] = '\0';
    if (req == NULL || key == NULL)
    {
        HttpParser.ok = PROTO_FALSE;
        return;
    }

    // Only urlencoded bodies (allow a trailing "; charset=..." suffix).
    HttpParser.get_header_args.req = req;
    HttpParser.get_header_args.key = "Content-Type";
    http_parser_get_header(work);
    const char *ct = HttpParser.text;
    if (ct == NULL || !str.starts(ct, "application/x-www-form-urlencoded", 33, PROTO_TRUE))
    {
        HttpParser.ok = PROTO_FALSE;
        return;
    }

    const char *body = (const char *)req->body;
    size_t len = req->body_len;
    size_t key_len = str.len(key, len + 1); // a matchable body key cannot exceed the body length
    size_t i = 0;

    while (i < len)
    {
        size_t ks = i;
        while (i < len && body[i] != '=' && body[i] != '&')
        {
            i++;
        }
        proto_bool key_matches = (i - ks == key_len) && (str.diff(body + ks, key, key_len, PROTO_FALSE) == key_len);

        size_t vs = i;
        size_t ve = i;
        if (i < len && body[i] == '=')
        {
            vs = ++i;
            while (i < len && body[i] != '&')
            {
                i++;
            }
            ve = i;
        }
        // body[i] != '&' here is unreachable: if the first scan above stopped because
        // body[i]=='&' (no '=' before it), that is already the '&' we're checking for. If it
        // instead stopped on '=', the '=' block's own scan only ever stops early on '&' - it
        // has no '=' check - so it too can only leave body[i]=='&' when i<len. The remaining
        // stop condition for both scans, i==len, is excluded by the i<len test itself. So
        // whenever i < len still holds at this point, body[i] can only be '&'.
        if (i < len && body[i] == '&')
        {
            i++;
        }

        if (key_matches)
        {
            size_t vlen = ve - vs;
            if (vlen > out_size - 1)
            {
                vlen = out_size - 1;
            }
            mem.cpy(out, body + vs, vlen);
            out[vlen] = '\0';
            HttpParser.ok = PROTO_TRUE;
            return;
        }
    }
    HttpParser.ok = PROTO_FALSE;
}

static void http_parser_get_param(uint8_t *restrict work)
{
    (void)work;
    const HttpReq *req = HttpParser.get_param_args.req;
    const char *key = HttpParser.get_param_args.key;

    if (req == NULL || key == NULL)
    {
        HttpParser.text = NULL;
        return;
    }
    for (uint8_t i = 0; i < req->path_param_count; i++)
    {
        if (str.eq(req->path_params[i].key, key, QUERY_KEY_LEN, PROTO_FALSE))
        {
            HttpParser.text = req->path_params[i].val;
            return;
        }
    }
    HttpParser.text = NULL;
}

// Designated, so a member's position in the struct does not decide what it binds to.
HttpParserNs HttpParser = {
    .set_stream_hooks = http_parser_set_stream_hooks,
    .reset = http_parser_reset,
    .feed = http_parser_feed,
    .get_header = http_parser_get_header,
    .get_cookie = http_parser_get_cookie,
    .forwarded_client = http_parser_forwarded_client,
    .get_query = http_parser_get_query,
    .get_form = http_parser_get_form,
    .get_param = http_parser_get_param,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_PARSER
