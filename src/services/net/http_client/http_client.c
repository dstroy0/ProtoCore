// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_client.c
 * @brief Outbound HTTP(S) client: URL parse + request build + response parse,
 *        and the raw-lwIP / mbedTLS transport (ESP32 only).
 */

#include "services/net/http_client/http_client.h"
#include "mmgr/membuild.h"      // pc_sb frame builder
#include "server/clock/clock.h" // pcdelay

#if PC_ENABLE_HTTP_CLIENT

#include "shared_primitives/mime.h"
#include "shared_primitives/numparse.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Pure helpers (host-testable)
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT
#include "network_drivers/transport/client.h" // shared outbound TCP client (L4)
#include <Arduino.h>                          // millis()
#endif
#if PROTOCORE_HOT && PC_ENABLE_HTTP_CLIENT_TLS
#include "network_drivers/tls/tls.h"
#include <mbedtls/ssl.h> // MBEDTLS_ERR_SSL_WANT_READ for the BIO recv callback
#endif
proto_bool http_client_parse_url(const char *url, proto_bool *is_https, char *host, size_t host_cap, uint16_t *port,
                                 char *path, size_t path_cap)
{
    if (!url || !is_https || !host || !port || !path)
    {
        return PROTO_FALSE;
    }

    const char *p = url;
    if (strncmp(p, "https://", 8) == 0)
    {
        *is_https = PROTO_TRUE;
        *port = 443;
        p += 8;
    }
    else if (strncmp(p, "http://", 7) == 0) // NOSONAR: HTTP client must accept http:// URLs
    {
        *is_https = PROTO_FALSE;
        *port = 80;
        p += 7;
    }
    else
    {
        return PROTO_FALSE;
    }

    // host (up to ':' or '/')
    const char *h = p;
    while (*p && *p != ':' && *p != '/')
    {
        p++;
    }
    size_t hlen = (size_t)(p - h);
    if (hlen == 0 || hlen >= host_cap)
    {
        return PROTO_FALSE;
    }
    memcpy(host, h, hlen);
    host[hlen] = '\0';

    // optional :port
    if (*p == ':')
    {
        p++;
        if (*p < '0' || *p > '9')
        {
            return PROTO_FALSE;
        }
        uint32_t pn = 0;
        while (*p >= '0' && *p <= '9')
        {
            pn = pn * 10 + (uint32_t)(*p++ - '0');
            if (pn > 65535)
            {
                return PROTO_FALSE;
            }
        }
        *port = (uint16_t)pn;
    }

    // path ("/" if absent)
    if (*p == '\0')
    {
        if (path_cap < 2)
        {
            return PROTO_FALSE;
        }
        path[0] = '/';
        path[1] = '\0';
    }
    else
    {
        size_t plen = strnlen(p, path_cap + 1);
        if (plen >= path_cap)
        {
            return PROTO_FALSE;
        }
        memcpy(path, p, plen + 1);
    }
    return PROTO_TRUE;
}

size_t http_client_build_request(const char *method, const char *host, uint16_t port, const char *path,
                                 const char *content_type, const uint8_t *body, size_t body_len, char *out, size_t cap)
{
    if (!method || !host || !path || !out || cap == 0)
    {
        return 0;
    }

    // Host header carries the port only when it is non-default.
    char hosthdr[80];
    if (port == 80 || port == 443)
    {
        pc_sb sb_hosthdr = {hosthdr, sizeof(hosthdr), 0, PROTO_TRUE};
        pc_sb_put(&sb_hosthdr, host);
        if (pc_sb_finish(&sb_hosthdr) == 0)
        {
            hosthdr[0] = '\0';
        }
    }
    else
    {
        pc_sb sb_hosthdr2 = {hosthdr, sizeof(hosthdr), 0, PROTO_TRUE};
        pc_sb_put(&sb_hosthdr2, host);
        pc_sb_put(&sb_hosthdr2, ":");
        pc_sb_u32(&sb_hosthdr2, (uint32_t)((unsigned)port));
        if (pc_sb_finish(&sb_hosthdr2) == 0)
        {
            hosthdr[0] = '\0';
        }
    }

    int n;
    if (body && body_len)
    {
        pc_sb sb_out = {out, cap, 0, PROTO_TRUE};
        pc_sb_put(&sb_out, method);
        pc_sb_put(&sb_out, " ");
        pc_sb_put(&sb_out, path);
        pc_sb_put(&sb_out, " HTTP/1.1\r\nHost: ");
        pc_sb_put(&sb_out, hosthdr);
        pc_sb_put(&sb_out, "\r\nUser-Agent: PC\r\nContent-Type: ");
        pc_sb_put(&sb_out, content_type ? content_type : PC_MIME_OCTET_STREAM);
        pc_sb_put(&sb_out, "\r\nContent-Length: ");
        pc_sb_u32(&sb_out, (uint32_t)((unsigned)body_len));
        pc_sb_put(&sb_out, "\r\nConnection: close\r\n\r\n");
        n = (int)pc_sb_finish(&sb_out);
        // The builder's flag is the overflow signal. pc_sb_finish reports 0 for a frame that did
        // not fit, so a length comparison cannot detect it: headers too long for cap left n == 0,
        // this guard passed, and the body was copied over an empty buffer and returned as a valid
        // request. The body still has to fit in what remains.
        if (!sb_out.ok || (size_t)n + body_len > cap)
        {
            return 0;
        }
        memcpy(out + n, body, body_len);
        return (size_t)n + body_len;
    }

    pc_sb sb_out2 = {out, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_out2, method);
    pc_sb_put(&sb_out2, " ");
    pc_sb_put(&sb_out2, path);
    pc_sb_put(&sb_out2, " HTTP/1.1\r\nHost: ");
    pc_sb_put(&sb_out2, hosthdr);
    pc_sb_put(&sb_out2, "\r\nUser-Agent: PC\r\nConnection: close\r\n\r\n");
    n = (int)pc_sb_finish(&sb_out2);
    // n < 0 is unreachable here, for the same reason as the body-request snprintf above.
    if (!sb_out2.ok)
    {
        return 0;
    }
    return (size_t)n;
}

// Case-insensitive search for header @p name in the header block [buf, end);
// returns a pointer to the value (past "name:" and spaces) or NULL.
static const char *find_header(const uint8_t *buf, const uint8_t *end, const char *name)
{
    size_t nlen = strnlen(name, (size_t)(end - buf) + 1);
    const uint8_t *p = buf;
    while (p + nlen + 1 < end)
    {
        // at the start of a header line?
        if (strncasecmp((const char *)p, name, nlen) == 0 && p[nlen] == ':')
        {
            const uint8_t *v = p + nlen + 1;
            while (v < end && (*v == ' ' || *v == '\t'))
            {
                v++;
            }
            return (const char *)v;
        }
        // advance to the next line
        while (p < end && *p != '\n')
        {
            p++;
        }
        if (p < end)
        {
            p++;
        }
    }
    return NULL;
}

int http_client_parse_response(uint8_t *buf, size_t len, size_t *body_off, size_t *body_len)
{
    if (!buf || len < 12 || memcmp(buf, "HTTP/1.", 7) != 0)
    {
        return (int)HTTP_CLIENT_ERR_RESPONSE;
    }

    // Status code: first space, then 3 digits.
    const uint8_t *sp = (const uint8_t *)memchr(buf, ' ', len);
    if (!sp || sp + 4 > buf + len)
    {
        return (int)HTTP_CLIENT_ERR_RESPONSE;
    }
    int status = (sp[1] - '0') * 100 + (sp[2] - '0') * 10 + (sp[3] - '0');
    if (status < 100 || status > 599)
    {
        return (int)HTTP_CLIENT_ERR_RESPONSE;
    }

    // Header terminator "\r\n\r\n".
    uint8_t *hdr_end = NULL;
    for (size_t i = 0; i + 3 < len; i++)
    {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
        {
            hdr_end = buf + i;
            break;
        }
    }
    if (!hdr_end)
    {
        return (int)HTTP_CLIENT_ERR_RESPONSE;
    }

    size_t off = (size_t)(hdr_end + 4 - buf);
    size_t avail = len - off;

    const char *te = find_header(buf, hdr_end, "Transfer-Encoding");
    if (te && strncasecmp(te, "chunked", 7) == 0)
    {
        // Decode chunked transfer-encoding in place into the body region.
        size_t in = off, out = off;
        while (in < len)
        {
            // chunk size line (hex) up to CRLF
            size_t csz = 0;
            proto_bool any = PROTO_FALSE;
            while (in < len && buf[in] != '\r')
            {
                uint8_t c = buf[in++];
                int d;
                if (c >= '0' && c <= '9')
                {
                    d = c - '0';
                }
                else if (c >= 'a' && c <= 'f')
                {
                    d = c - 'a' + 10;
                }
                else if (c >= 'A' && c <= 'F')
                {
                    d = c - 'A' + 10;
                }
                else
                {
                    break; // stop at ';' (chunk ext) or junk
                }
                csz = csz * 16 + (size_t)d;
                any = PROTO_TRUE;
            }
            // skip to end of the size line
            while (in < len && buf[in] != '\n')
            {
                in++;
            }
            if (in < len)
            {
                in++; // past '\n'
            }
            if (!any)
            {
                break;
            }
            if (csz == 0)
            {
                break; // last chunk
            }
            // Wrap-safe: csz is an unbounded attacker-controlled hex size; on a 32-bit target
            // in + csz could overflow below len and skip this clamp, leaving a huge memmove.
            // Compare against the bytes remaining instead (in <= len here).
            if (csz > len - in)
            {
                csz = len - in; // truncated; take what we have
            }
            memmove(buf + out, buf + in, csz);
            out += csz;
            in += csz;
            // skip trailing CRLF after chunk data
            while (in < len && (buf[in] == '\r' || buf[in] == '\n'))
            {
                in++;
            }
        }
        *body_off = off;
        *body_len = out - off;
        return status;
    }

    const char *cl = find_header(buf, hdr_end, "Content-Length");
    if (cl)
    {
        long n = pc_strtol(cl, NULL);
        if (n < 0)
        {
            n = 0;
        }
        size_t want = (size_t)n;
        *body_off = off;
        *body_len = (want < avail) ? want : avail; // bounded by what we received
        return status;
    }

    // No framing header: connection-close delimited (rest of the buffer).
    *body_off = off;
    *body_len = avail;
    return status;
}

// ---------------------------------------------------------------------------
// Transport (ESP32 only): raw-lwIP TCP client + DNS, optional client mbedTLS.
// ---------------------------------------------------------------------------
#if PROTOCORE_HOT

// Optional stage tracing: build with -DPC_HTTP_CLIENT_DEBUG to print where a
// request stalls (DNS / connect / send / receive). Goes to the console UART.
#ifdef PC_HTTP_CLIENT_DEBUG
#define CL_DBG(...) printf(__VA_ARGS__)
#else
#define CL_DBG(...) ((void)0)
#endif

// All HTTP-client state, owned by one instance (internal linkage): a single in-flight request
// (one loop task). rx holds the *response to parse*: the raw wire bytes for http, or (for
// https) the plaintext decrypted by the TLS engine; the TCP connection lives in the shared
// client pool (pc_client). One named owner, unreachable from any other translation unit.
typedef struct
{
    uint8_t rx[PC_HTTP_CLIENT_BUF_SIZE];
    int cid; // active outbound connection id (pc_client pool)
} HttpClientCtx;
static HttpClientCtx s_http = {.cid = -1};

#if PC_ENABLE_HTTP_CLIENT_TLS
// mbedTLS BIO over the shared client transport: send wire bytes through the pool,
// recv by draining its wire ring (which carries ciphertext for https).
static int cl_tls_send(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    size_t cap = len > 0xFFFF ? 0xFFFF : len;
    return pc_client_send(s_http.cid, buf, cap) ? (int)cap : -1;
}
static int cl_tls_recv(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    size_t n = pc_client_read(s_http.cid, buf, len);
    if (n == 0)
    {
        return pc_client_is_closed(s_http.cid) ? 0 : MBEDTLS_ERR_SSL_WANT_READ;
    }
    return (int)n;
}
#endif // PC_ENABLE_HTTP_CLIENT_TLS

// Core request: build, connect, send, receive, parse.
static int http_request(const char *method, const char *url, const char *content_type, const uint8_t *body,
                        size_t body_len, HttpClientResult *out)
{
    if (out)
    {
        out->status = 0;
        out->body = NULL;
        out->body_len = 0;
    }

    proto_bool is_https;
    char host[80], path[160];
    uint16_t port;
    if (!http_client_parse_url(url, &is_https, host, sizeof(host), &port, path, sizeof(path)))
    {
        return (int)HTTP_CLIENT_ERR_URL;
    }
    CL_DBG("[hc] url host=%s port=%u https=%d path=%s\n", host, (unsigned)port, (int)is_https, path);
#if !PC_ENABLE_HTTP_CLIENT_TLS
    if (is_https)
    {
        return (int)HTTP_CLIENT_ERR_TLS;
    }
#endif

    // Build the request line + headers (+ optional body).
    char req[768];
    size_t reqlen = http_client_build_request(method, host, port, path, content_type, body, body_len, req, sizeof(req));
    if (reqlen == 0)
    {
        return (int)HTTP_CLIENT_ERR_URL;
    }

    uint32_t deadline = millis() + PC_HTTP_CLIENT_TIMEOUT_MS;

    // Open the connection (DNS + connect) via the shared client transport.
    s_http.cid = pc_client_open(host, port, PC_HTTP_CLIENT_TIMEOUT_MS);
    CL_DBG("[hc] pc_client_open cid=%d\n", s_http.cid);
    if (s_http.cid < 0)
    {
        return (s_http.cid == -2) ? (int)HTTP_CLIENT_ERR_DNS : (int)HTTP_CLIENT_ERR_CONNECT;
    }

    // The response to parse: raw wire bytes (http) or decrypted plaintext (https),
    // both land in s_http.rx.
    size_t pc_resp_len = 0;

    if (is_https)
    {
#if PC_ENABLE_HTTP_CLIENT_TLS
        int rc = pc_tls_client_run(host, (const uint8_t *)req, reqlen, s_http.rx, sizeof(s_http.rx), &pc_resp_len,
                                   cl_tls_send, cl_tls_recv, deadline);
        CL_DBG("[hc] tls rc=%d pt_len=%u\n", rc, (unsigned)pc_resp_len);
        if (rc < 0)
        {
            pc_client_close(s_http.cid);
            s_http.cid = -1;
            return (int)HTTP_CLIENT_ERR_TLS;
        }
#else
        pc_client_close(s_http.cid);
        s_http.cid = -1;
        return (int)HTTP_CLIENT_ERR_TLS;
#endif
    }
    else
    {
        // Plaintext: send the request, then drain wire bytes into s_http.rx until the
        // peer closes (and the ring is empty), the buffer fills, or we time out.
        if (!pc_client_send(s_http.cid, req, reqlen))
        {
            pc_client_close(s_http.cid);
            s_http.cid = -1;
            return (int)HTTP_CLIENT_ERR_SEND;
        }
        while ((int32_t)(deadline - millis()) > 0)
        {
            size_t n = pc_client_read(s_http.cid, s_http.rx + pc_resp_len, sizeof(s_http.rx) - pc_resp_len);
            pc_resp_len += n;
            if (pc_resp_len >= sizeof(s_http.rx))
            {
                break;
            }
            if (pc_client_is_closed(s_http.cid) && pc_client_available(s_http.cid) == 0)
            {
                break;
            }
            if (n == 0)
            {
                pcdelay(5);
            }
        }
    }

    CL_DBG("[hc] done pc_resp_len=%u\n", (unsigned)pc_resp_len);
    pc_client_close(s_http.cid);
    s_http.cid = -1;

    if (pc_resp_len == 0)
    {
        return (int)HTTP_CLIENT_ERR_TIMEOUT;
    }

    size_t body_off = 0, blen = 0;
    int status = http_client_parse_response(s_http.rx, pc_resp_len, &body_off, &blen);
    if (status < 0)
    {
        return (int)HTTP_CLIENT_ERR_RESPONSE;
    }
    if (out)
    {
        out->status = status;
        out->body = s_http.rx + body_off;
        out->body_len = blen;
    }
    return status;
}

int http_get(const char *url, HttpClientResult *out)
{
    return http_request("GET", url, NULL, NULL, 0, out);
}

int http_post(const char *url, const char *content_type, const uint8_t *body, size_t body_len, HttpClientResult *out)
{
    return http_request("POST", url, content_type, body, body_len, out);
}

void http_client_set_ca(const uint8_t *ca, size_t ca_len)
{
#if PC_ENABLE_HTTP_CLIENT_TLS
    pc_tls_client_set_ca(ca, ca_len);
#else
    (void)ca;
    (void)ca_len;
#endif
}
void http_client_set_pin(const uint8_t sha256[32])
{
#if PC_ENABLE_HTTP_CLIENT_TLS
    pc_tls_client_set_pin(sha256);
#else
    (void)sha256;
#endif
}
void http_client_clear_verify()
{
#if PC_ENABLE_HTTP_CLIENT_TLS
    pc_tls_client_clear_verify();
#endif
}

#else // host build: transport is a stub

int http_get(const char *url, HttpClientResult *out)
{
    (void)url;
    (void)out;
    return (int)HTTP_CLIENT_ERR_CONNECT;
}
int http_post(const char *url, const char *content_type, const uint8_t *body, size_t body_len, HttpClientResult *out)
{
    (void)url;
    (void)content_type;
    (void)body;
    (void)body_len;
    (void)out;
    return (int)HTTP_CLIENT_ERR_CONNECT;
}
void http_client_set_ca(const uint8_t *ca, size_t ca_len)
{
    (void)ca;
    (void)ca_len;
}
void http_client_set_pin(const uint8_t *sha256)
{
    (void)sha256;
}
void http_client_clear_verify()
{
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_HTTP_CLIENT
