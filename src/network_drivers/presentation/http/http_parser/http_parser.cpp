// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_parser.cpp
 * @brief Standalone HTTP/1.1 request parser - implementation.
 *
 * No dependency on transport, session, or lwIP.  Consumes one byte at a
 * time via http_parser_feed(); the presentation layer is responsible for
 * pulling bytes out of whatever transport buffer it uses.
 */

#include "http_parser.h"
#include "network_drivers/network/ip.h" // validate a recovered proxy client IP (v4/v6)
#include "shared_primitives/swar.h"     // one load, both delimiters (see next_sep)

HttpReq http_pool[CONN_POOL_SLOTS];

#if PC_ENABLE_STREAM_BODY
// Streaming-body hooks (OTA / file upload), owned by one instance (internal linkage): null
// unless the application installs them. One named owner, unreachable cross-TU. (The http_pool[]
// request table is the shared cross-TU substrate.)
struct HttpParserCtx
{
    HttpStreamBeginCb stream_begin = nullptr;
    HttpStreamDataCb stream_data = nullptr;
    HttpStreamAbortCb stream_abort = nullptr;
};
static HttpParserCtx s_hp;

void http_parser_set_stream_hooks(HttpStreamBeginCb begin, HttpStreamDataCb data, HttpStreamAbortCb abort)
{
    s_hp.stream_begin = begin;
    s_hp.stream_data = data;
    s_hp.stream_abort = abort;
}
#endif // PC_ENABLE_STREAM_BODY

// ---------------------------------------------------------------------------
// FNV-1a hash constants for HTTP version validation
// ---------------------------------------------------------------------------
// Precomputed at compile time via constexpr; zero runtime cost.
// The hash of the 8-byte version token ("HTTP/1.0" or "HTTP/1.1") is
// compared against the accumulated _version_hash when CR terminates the
// version field.

#define PC_FNV_OFFSET 2166136261u
#define PC_FNV_PRIME 16777619u

static constexpr uint32_t fnv1a(const char *s, uint32_t h = PC_FNV_OFFSET)
{
    return *s ? fnv1a(s + 1, (h ^ (uint8_t)*s) * PC_FNV_PRIME) : h;
}

#define PC_HASH_HTTP10 (fnv1a("HTTP/1.0"))
#define PC_HASH_HTTP11 (fnv1a("HTTP/1.1"))

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

#define PC_CC_TCHAR 0x01
#define PC_CC_VCHAR 0x02
#define PC_CC_FIELD_VALUE 0x04

// The 256-entry class table, one const byte per input octet (lands in flash .rodata). A plain literal so it is
// standard-independent (the arduino-esp32 build is gnu++11, where a constexpr loop-built table is ill-formed).
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

static inline bool is_tchar(uint8_t b)
{
    return (kCharClass[b] & PC_CC_TCHAR) != 0;
}
static inline bool is_vchar(uint8_t b)
{
    return (kCharClass[b] & PC_CC_VCHAR) != 0;
}
static inline bool is_field_value_char(uint8_t b)
{
    return (kCharClass[b] & PC_CC_FIELD_VALUE) != 0;
}

/**
 * @brief Split a raw query string into key=value pairs.
 *
 * Operates in-place on `req->query[]`.  Pairs are `&`-separated; key and
 * value are split on the first `=`.  Keys or values longer than their
 * respective limits are silently truncated - the path itself remains valid.
 */
// The next '&' or '=' at or after @p i, or @p len if neither is there.
//
// One load answers for both: the two lane masks are OR'd, so the first set lane is the first
// separator of either kind. Testing them in separate passes would load every word twice and then
// have to work out which hit came first.
static size_t next_sep(const char *s, size_t i, size_t len)
{
    while (i + PC_SWAR_BYTES <= len)
    {
        pc_swar_word w = pc_swar_load(s + i);
        pc_swar_word m = pc_swar_eq(w, '&') | pc_swar_eq(w, '=');
        if (m != 0)
        {
            return i + pc_swar_zero_lane(m); // the mask states the lane
        }
        i += PC_SWAR_BYTES;
    }
    // The final partial word, which cannot be loaded whole without reading past len.
    while (i < len && s[i] != '&' && s[i] != '=')
    {
        i++;
    }
    return i;
}

/**
 * Split the raw query into key=value pairs IN PLACE, as the comment always claimed: the terminator
 * is written over the separator it consumed, so each key and value is a NUL-terminated substring of
 * req->query[] and the table holds two pointers rather than two more copies.
 *
 * Done on first access rather than during parsing, which is what makes writing into query[] safe:
 * anything that wants the query whole (Digest's uri comparison, RFC 7616 3.4) reads it at dispatch,
 * before a handler can ask for a parameter. A request that never asks does no work at all.
 */
static void split_query(HttpReq *req)
{
    if (req->query_split)
    {
        return;
    }
    req->query_split = true;

    char *qs = req->query;
    size_t len = req->query_idx;
    size_t i = 0;

    while (i < len && req->query_count < MAX_QUERY_PARAMS)
    {
        size_t key_at = i;
        size_t sep = next_sep(qs, i, len);

        const char *val = ""; // a pair with no '=' has an empty value, not a missing one
        if (sep < len && qs[sep] == '=')
        {
            qs[sep] = '\0'; // terminate the key over the '='
            size_t val_at = sep + 1;
            i = next_sep(qs, val_at, len);
            val = qs + val_at;
        }
        else
        {
            i = sep;
        }
        if (i < len)
        {
            qs[i++] = '\0'; // terminate over the '&'
        }

        if (qs[key_at] != '\0') // a bare '&' contributes no pair
        {
            req->query_params[req->query_count].key = qs + key_at;
            req->query_params[req->query_count].val = val;
            req->query_count++;
        }
    }
}

void http_parser_reset(HttpReq *req)
{
    uint8_t id = req->slot_id;
#if PC_ENABLE_STREAM_BODY
    // A streamed body that never reached ParseState::PARSE_COMPLETE is being torn down (peer
    // reset / timeout / error): let the sink release its resource before we wipe
    // the state. The normal-completion reset runs while parse_state==ParseState::PARSE_COMPLETE
    // (the handler already finished the sink), so this fires only on abort.
    if (req->body_streaming && req->parse_state != ParseState::PARSE_COMPLETE && s_hp.stream_abort)
    {
        s_hp.stream_abort(req);
    }
#endif
    *req = {};         // zero all fields
    req->slot_id = id; // restore slot identity
    req->parse_state = ParseState::PARSE_METHOD;
    req->_version_hash = PC_FNV_OFFSET; // seed the FNV-1a accumulator
}

void http_parser_feed(HttpReq *p, uint8_t byte)
{
    // Terminal states (PARSE_COMPLETE / PARSE_ERROR / PARSE_ENTITY_TOO_LARGE / PARSE_URI_TOO_LONG) have no case
    // below, so they fall through to `default:` and no-op - no separate guard switch on the per-byte hot path.
    char c = (char)byte;

    switch (p->parse_state)
    {

    case ParseState::PARSE_METHOD:
        if (c == ' ')
        {
            p->parse_state = ParseState::PARSE_PATH;
            p->current_token_idx = 0;
        }
        else if (!is_tchar(byte))
        {
            // RFC 7230 §3.1.1: method = token; any non-tchar is malformed
            p->parse_state = ParseState::PARSE_ERROR;
        }
        else if (p->current_token_idx < sizeof(p->method) - 1)
        {
            p->method[p->current_token_idx++] = c;
        }
        else
        {
            p->parse_state = ParseState::PARSE_ERROR;
        }
        break;

    case ParseState::PARSE_PATH:
        if (c == ' ')
        {
            p->parse_state = ParseState::PARSE_VERSION;
        }
        else if (c == '?')
        {
            p->parse_state = ParseState::PARSE_QUERY;
        }
        else if (!is_vchar(byte))
        {
            // RFC 3986 §3.3: path chars must be visible ASCII (or pct-encoded)
            p->parse_state = ParseState::PARSE_ERROR;
        }
        else if (p->path_idx < MAX_PATH_LEN - 1)
        {
            p->path[p->path_idx++] = c;
        }
        else
        {
            p->parse_state = ParseState::PARSE_URI_TOO_LONG;
        }
        break;

    case ParseState::PARSE_QUERY:
        if (c == ' ')
        {
            // Not split here: the query is tokenized on first access (see split_query), so anything
            // that needs it whole still gets it whole.
            p->parse_state = ParseState::PARSE_VERSION;
        }
        else if (!is_vchar(byte))
        {
            // Control chars and NUL are not valid query-string bytes
            p->parse_state = ParseState::PARSE_ERROR;
        }
        else if (p->query_idx < MAX_QUERY_LEN - 1)
        {
            p->query[p->query_idx++] = c;
        }
        // Silently truncate - query overflow is a capacity limit, not a protocol error
        break;

    case ParseState::PARSE_VERSION:
        if (c == '\r')
        {
            if (p->_version_hash == PC_HASH_HTTP11)
            {
                p->version = HttpVersion::HTTP_11;
            }
            else if (p->_version_hash == PC_HASH_HTTP10)
            {
                p->version = HttpVersion::HTTP_10;
            }
            else
            {
                p->version = HttpVersion::HTTP_UNKNOWN;
            }
            p->parse_state = ParseState::PARSE_EXPECT_LF;
        }
        else
        {
            p->_version_hash = (p->_version_hash ^ byte) * PC_FNV_PRIME;
        }
        break;

    case ParseState::PARSE_EXPECT_LF:
        if (c == '\n')
        {
            p->parse_state = ParseState::PARSE_HEADER_KEY;
            p->current_token_idx = 0;
        }
        else
        {
            p->parse_state = ParseState::PARSE_ERROR;
        }
        break;

    case ParseState::PARSE_HEADER_KEY:
        if (c == '\r')
        {
            if (p->current_token_idx == 0)
            {
                // Blank line - end of headers
                p->parse_state = ParseState::PARSE_EXPECT_BODY_LF;
            }
            else
            {
                // CR mid-key: malformed (RFC 7230 §3.2 requires CRLF after value)
                p->parse_state = ParseState::PARSE_ERROR;
            }
        }
        else if (c == ':')
        {
            // Terminate the scratch key so Host / Content-Length detection works
            // regardless of whether this header is stored (header_count < MAX).
            size_t k = p->current_token_idx < MAX_KEY_LEN ? p->current_token_idx : MAX_KEY_LEN - 1;
            p->cur_key[k] = '\0';
            p->parse_state = ParseState::PARSE_HEADER_VAL;
            p->current_token_idx = 0;
#if PC_CAPTURE_AUTH_HEADER
            // The Authorization value (Digest / JWT bearer) exceeds MAX_VAL_LEN,
            // so capture it whole into a dedicated buffer independent of scratch.
            p->cur_is_auth = (strcasecmp(p->cur_key, "Authorization") == 0);
            if (p->cur_is_auth)
            {
                p->auth_idx = 0;
            }
#endif
        }
        else if (!is_tchar(byte))
        {
            // RFC 7230 §3.2: field-name = token; any non-tchar is malformed
            p->parse_state = ParseState::PARSE_ERROR;
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

    case ParseState::PARSE_HEADER_VAL:
        // Strip leading OWS (SP or HTAB) after the colon - RFC 9110 §5.6.3
        if ((c == ' ' || c == '\t') && p->current_token_idx == 0)
        {
            break;
        }
        if (c == '\r')
        {
            uint8_t h = p->header_count;

            // Terminate the scratch value so detection sees a clean C string.
            size_t vlen = p->current_token_idx < MAX_VAL_LEN ? p->current_token_idx : MAX_VAL_LEN - 1;
            p->cur_val[vlen] = '\0';
#if PC_CAPTURE_AUTH_HEADER
            if (p->cur_is_auth)
            {
                p->authorization[p->auth_idx] = '\0';
                p->cur_is_auth = false;
            }
#endif

            // Host / Content-Length detection works off the scratch copies, so
            // it is correct even for headers past MAX_HEADERS (RFC 7230 §5.4,
            // §3.3.2).
            if (strcasecmp(p->cur_key, "Host") == 0)
            {
                p->host_count++;
            }

            if (strcasecmp(p->cur_key, "Content-Length") == 0)
            {
                // RFC 7230 §3.3.2: Content-Length = 1*DIGIT.
                size_t cl = 0;
                bool valid = (p->cur_val[0] != '\0');
                for (const char *q = p->cur_val; *q; q++)
                {
                    if (*q < '0' || *q > '9')
                    {
                        valid = false;
                        break;
                    }
                    cl = cl * 10 + (size_t)(*q - '0');
                }
                // A non-numeric value, or a second Content-Length whose value
                // disagrees with the first, is a fatal framing error (request
                // smuggling vector) → 400.
                if (!valid || (p->content_length_count > 0 && cl != p->content_length))
                {
                    p->parse_state = ParseState::PARSE_ERROR;
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
            if (strcasecmp(p->cur_key, "Transfer-Encoding") == 0)
            {
                p->parse_state = ParseState::PARSE_ERROR;
                break;
            }

            if (h < MAX_HEADERS)
            {
                p->header_count++;
            }

            p->parse_state = ParseState::PARSE_EXPECT_LF;
            p->current_token_idx = 0;
        }
        else if (!is_field_value_char(byte))
        {
            // RFC 7230 §3.2: control chars and NUL are not valid in field values
            p->parse_state = ParseState::PARSE_ERROR;
        }
        else
        {
#if PC_CAPTURE_AUTH_HEADER
            // Capture the full Authorization value (Digest / JWT) past MAX_VAL_LEN.
            if (p->cur_is_auth && p->auth_idx < PC_AUTH_HDR_CAP - 1)
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

    case ParseState::PARSE_EXPECT_BODY_LF:
        /*
         * Consumes the LF of the blank-line CRLF that ends the header block.
         * Decides the next state based on Content-Length:
         *   > BODY_BUF_SIZE → 413 Payload Too Large
         *   == 0            → ParseState::PARSE_COMPLETE (no body)
         *   else            → ParseState::PARSE_BODY
         */
        if (c == '\n')
        {
            // RFC 7230 §5.4: a request MUST NOT carry more than one Host header
            // (always enforced); an HTTP/1.1 request MUST carry exactly one Host
            // header (enforced only when PC_ENFORCE_HOST_HEADER is set).
            bool host_violation = (p->host_count > 1);
#if PC_ENFORCE_HOST_HEADER
            if (p->version == HttpVersion::HTTP_11 && p->host_count == 0)
            {
                host_violation = true;
            }
#endif
            if (host_violation)
            {
                p->parse_state = ParseState::PARSE_ERROR;
            }
#if PC_ENABLE_STREAM_BODY
            // Streaming sink (OTA / upload): all headers are parsed here, so the
            // hook can match method/path/Authorization and begin a sink (Update
            // or a file). If it accepts, the body streams in chunks and the size
            // cap is bypassed; the matching route handler still runs at COMPLETE.
            else if (p->content_length > 0 && s_hp.stream_begin && s_hp.stream_begin(p))
            {
                p->body_streaming = true;
                p->parse_state = ParseState::PARSE_BODY;
            }
#endif
            else if (p->content_length > BODY_BUF_SIZE)
            {
                p->parse_state = ParseState::PARSE_ENTITY_TOO_LARGE;
            }
            else if (p->content_length == 0)
            {
                p->body[0] = '\0';
                p->parse_state = ParseState::PARSE_COMPLETE;
            }
            else
            {
                p->parse_state = ParseState::PARSE_BODY;
            }
        }
        else
        {
            p->parse_state = ParseState::PARSE_ERROR;
        }
        break;

    case ParseState::PARSE_BODY:
        // Body is opaque data - no character validation.
#if PC_ENABLE_STREAM_BODY
        if (p->body_streaming)
        {
            // Reuse body[] as a flush buffer: fill it, then hand whole chunks to
            // the sink. No BODY_BUF_SIZE cap on the total - the body never lives
            // in RAM all at once.
            p->body[p->body_len++] = byte;
            if (p->body_len == BODY_BUF_SIZE)
            {
                if (s_hp.stream_data)
                {
                    s_hp.stream_data(p, p->body, p->body_len);
                }
                p->body_len = 0;
            }
            p->body_bytes_read++;
            if (p->body_bytes_read >= p->content_length)
            {
                if (p->body_len && s_hp.stream_data)
                {
                    s_hp.stream_data(p, p->body, p->body_len); // flush the tail
                }
                p->body_len = 0;
                p->body[0] = '\0';
                p->parse_state = ParseState::PARSE_COMPLETE;
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
            p->parse_state = ParseState::PARSE_COMPLETE;
        }
        break;

    default:
        break;
    }
}

const char *http_get_header(const HttpReq *req, const char *key)
{
    for (uint8_t i = 0; i < req->header_count; i++)
    {
        if (strcasecmp(req->headers[i].key, key) == 0)
        {
            return req->headers[i].val;
        }
    }
    return nullptr;
}

bool http_get_cookie(const HttpReq *req, const char *name, char *out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return false;
    }
    out[0] = '\0';
    if (req == nullptr || name == nullptr || name[0] == '\0')
    {
        return false;
    }

    // RFC 6265 4.2.1: the request "Cookie" header is "name1=value1; name2=value2".
    // Names are case-sensitive; a value may be DQUOTE-wrapped.
    const char *c = http_get_header(req, "Cookie");
    if (c == nullptr)
    {
        return false;
    }
    size_t nlen = strnlen(name, MAX_VAL_LEN); // a matchable cookie-name span cannot exceed a header value

    const char *p = c;
    while (*p != '\0')
    {
        while (*p == ' ' || *p == '\t' || *p == ';') // skip inter-pair separators/spaces
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }
        const char *eq = p;
        while (*eq != '\0' && *eq != '=' && *eq != ';') // cookie-name runs up to '='
        {
            eq++;
        }
        if (*eq == '=' && (size_t)(eq - p) == nlen && strncmp(p, name, nlen) == 0)
        {
            const char *v = eq + 1;
            const char *end = v;
            while (*end != '\0' && *end != ';') // value runs up to the next ';'
            {
                end++;
            }
            while (end > v && (end[-1] == ' ' || end[-1] == '\t')) // trim trailing OWS
            {
                end--;
            }
            size_t vlen = (size_t)(end - v);
            if (vlen >= 2 && v[0] == '"' && v[vlen - 1] == '"') // strip a quoted cookie-value
            {
                v++;
                vlen -= 2;
            }
            if (vlen >= out_size)
            {
                vlen = out_size - 1;
            }
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return true;
        }
        p = eq;
        while (*p != '\0' && *p != ';') // advance past this pair
        {
            p++;
        }
    }
    return false;
}

// Extract and validate a Forwarded / X-Forwarded-For client-address token from
// [s, s+n) into out (canonical text). Accepts IPv4 with an optional ":port", a
// bracketed IPv6 "[2001:db8::1]:port" (RFC 7239 §6), and a bare IPv6 (the de-facto
// X-Forwarded-For form). The candidate is confirmed with pc_ip_parse, so "unknown",
// an obfuscated "_id" identifier (RFC 7239 §6.3), or any malformed token returns
// false. Returns true and writes the RFC 5952 canonical address on success.
static bool fwd_extract_client(const char *s, size_t n, char *out, size_t cap)
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
        return false;
    }

    char tok[PC_IP_STR_MAX];
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
                return false;
            }
            tok[tlen++] = s[i];
        }
        if (i >= n) // unterminated bracket
        {
            return false;
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
            return false;
        }
        memcpy(tok, s, take);
        tlen = take;
    }
    tok[tlen] = '\0';

    pc_ip ip;
    if (!pc_ip_parse(tok, &ip)) // rejects "unknown" / "_obf" / malformed
    {
        return false;
    }
    return pc_ip_format(&ip, out, cap) > 0; // false if out is too small for the canonical text
}

bool http_forwarded_client(const HttpReq *req, char *ip_out, size_t ip_cap, bool *is_https)
{
    if (is_https)
    {
        *is_https = false;
    }
    if (!ip_out || ip_cap == 0 || !req)
    {
        return false;
    }
    ip_out[0] = '\0';

    // Prefer RFC 7239 "Forwarded" (the leftmost element is the original client):
    //   Forwarded: for=192.0.2.60;proto=https, for=198.51.100.1
    const char *fwd = http_get_header(req, "Forwarded");
    if (fwd)
    {
        // First element = up to the first ','. Within it, find for= and proto=.
        const char *elem_end = strchr(fwd, ',');
        size_t elen = elem_end ? (size_t)(elem_end - fwd) : strnlen(fwd, MAX_VAL_LEN);
        // proto=
        if (is_https)
        {
            // Only the first element's proto= matters, so this is a single check.
            const char *hit = strstr(fwd, "proto=");
            if (hit && (size_t)(hit - fwd) < elen)
            {
                *is_https = (strncasecmp(hit + 6, "https", 5) == 0);
            }
        }
        // for=
        const char *f = strstr(fwd, "for=");
        if (f && (size_t)(f - fwd) < elen)
        {
            const char *fv = f + 4;
            const char *fend = fv;
            size_t lim = elen - (size_t)(fv - fwd);
            size_t k = 0;
            // fend[k]==',' is unreachable here: lim is derived from elen, which is itself the
            // offset of the first ',' (or end of string when there is none) - so k can never
            // walk far enough to land on a comma without k<lim failing first.
            while (k < lim && fend[k] != ';' && fend[k] != ',') // GCOVR_EXCL_BR_LINE  see above: the ',' arm can't fire
            {
                k++;
            }
            if (fwd_extract_client(fv, k, ip_out, ip_cap))
            {
                return true;
            }
        }
    }

    // De-facto X-Forwarded-For (comma list; leftmost = original client) + X-Forwarded-Proto.
    if (is_https)
    {
        const char *xfp = http_get_header(req, "X-Forwarded-Proto");
        if (xfp && strncasecmp(xfp, "https", 5) == 0)
        {
            *is_https = true;
        }
    }
    const char *xff = http_get_header(req, "X-Forwarded-For");
    if (xff)
    {
        const char *end = strchr(xff, ',');
        size_t len = end ? (size_t)(end - xff) : strnlen(xff, MAX_VAL_LEN);
        if (fwd_extract_client(xff, len, ip_out, ip_cap))
        {
            return true;
        }
    }
    return false;
}

const char *http_get_query(HttpReq *req, const char *key)
{
    split_query(req); // idempotent; the first caller pays for it, a request with no reader pays none
    for (uint8_t i = 0; i < req->query_count; i++)
    {
        if (strcmp(req->query_params[i].key, key) == 0)
        {
            return req->query_params[i].val;
        }
    }
    return nullptr;
}

bool http_get_form(const HttpReq *req, const char *key, char *out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return false;
    }
    out[0] = '\0';
    if (req == nullptr || key == nullptr)
    {
        return false;
    }

    // Only urlencoded bodies (allow a trailing "; charset=..." suffix).
    const char *ct = http_get_header(req, "Content-Type");
    if (ct == nullptr || strncasecmp(ct, "application/x-www-form-urlencoded", 33) != 0)
    {
        return false;
    }

    const char *body = (const char *)req->body;
    size_t len = req->body_len;
    size_t key_len = strnlen(key, len + 1); // a matchable body key cannot exceed the body length
    size_t i = 0;

    while (i < len)
    {
        size_t ks = i;
        while (i < len && body[i] != '=' && body[i] != '&')
        {
            i++;
        }
        bool key_matches = (i - ks == key_len) && (strncmp(body + ks, key, key_len) == 0);

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
        if (i < len && body[i] == '&') // GCOVR_EXCL_BR_LINE  see above: the not-'&' arm can't fire
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
            memcpy(out, body + vs, vlen);
            out[vlen] = '\0';
            return true;
        }
    }
    return false;
}

const char *http_get_param(const HttpReq *req, const char *key)
{
    if (req == nullptr || key == nullptr)
    {
        return nullptr;
    }
    for (uint8_t i = 0; i < req->path_param_count; i++)
    {
        if (strcmp(req->path_params[i].key, key) == 0)
        {
            return req->path_params[i].val;
        }
    }
    return nullptr;
}
