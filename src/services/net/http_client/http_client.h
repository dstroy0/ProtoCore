// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_client.h
 * @brief Zero-heap outbound HTTP(S) client over raw lwIP (PC_ENABLE_HTTP_CLIENT).
 *
 * A blocking client for issuing requests *from* the device: webhooks, telemetry
 * push, REST calls. Split, like the other services, into a pure host-testable
 * core and an ESP32-only transport:
 *
 *  - http_client_parse_url() / http_client_build_request() /
 *    http_client_parse_response() are pure string functions, unit-tested on the
 *    host (env:native_http_client).
 *  - http_get() / http_post() resolve the host (DNS), open a raw lwIP TCP
 *    connection (https:// via client-side mbedTLS over the shared static arena),
 *    send the request, and fill the result from a fixed BSS receive buffer. No
 *    heap; one request at a time (single-task device).
 *
 * Response bodies are delimited by Content-Length or by connection close;
 * chunked transfer-decoding is applied when present. The body is returned by
 * pointer into the client's static buffer (valid until the next call).
 */

#ifndef PROTOCORE_HTTP_CLIENT_H
#define PROTOCORE_HTTP_CLIENT_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_HTTP_CLIENT

// Transport / result error codes (negative; HTTP status codes are positive).
typedef enum PROTO_ENUM_PACKED
{
    HTTP_CLIENT_ERR_URL = -1,      ///< Malformed URL.
    HTTP_CLIENT_ERR_DNS = -2,      ///< Host resolution failed.
    HTTP_CLIENT_ERR_CONNECT = -3,  ///< TCP/TLS connect failed.
    HTTP_CLIENT_ERR_TIMEOUT = -4,  ///< No (complete) response before the timeout.
    HTTP_CLIENT_ERR_SEND = -5,     ///< Failed to send the request.
    HTTP_CLIENT_ERR_RESPONSE = -6, ///< Malformed / unparseable response.
    HTTP_CLIENT_ERR_TLS = -7,      ///< HTTPS requested but TLS unavailable / handshake failed.
} HttpClientError;

/** @brief Result of an HTTP client request. */
typedef struct
{
    int status;          ///< HTTP status code (e.g. 200), or a negative HttpClientError.
    const uint8_t *body; ///< Response body (points into the client's static buffer).
    size_t body_len;     ///< Response body length in bytes.
} HttpClientResult;

// ---------------------------------------------------------------------------
// Pure helpers (host-testable; no sockets, no heap)
// ---------------------------------------------------------------------------

/**
 * @brief Parse an absolute URL into scheme/host/port/path.
 *
 * Accepts `http://host[:port][/path]` and `https://...`. Defaults: port 80
 * (http) / 443 (https), path "/".
 *
 * @return true on success; false if malformed or a field overflows its buffer.
 */
proto_bool http_client_parse_url(const char *url, proto_bool *is_https, char *host, size_t host_cap, uint16_t *port,
                                 char *path, size_t path_cap);

/**
 * @brief Build an HTTP/1.1 request line + headers (+ optional body) into @p out.
 *
 * Emits `Host`, `User-Agent`, `Connection: close`, and (when @p body) a
 * `Content-Type` + `Content-Length`.
 *
 * @return number of bytes written, or 0 if it would not fit @p cap.
 */
size_t http_client_build_request(const char *method, const char *host, uint16_t port, const char *path,
                                 const char *content_type, const uint8_t *body, size_t body_len, char *out, size_t cap);

/**
 * @brief Parse a complete HTTP response: status code + body location.
 *
 * Locates the body after the header terminator and bounds it by Content-Length,
 * decodes chunked transfer-encoding in place, or (absent both) treats the rest
 * as the body (connection-close framing).
 *
 * @param buf       full response bytes.
 * @param len       number of bytes in @p buf (mutable: chunked decode rewrites in place).
 * @param body_off  receives the body offset within @p buf.
 * @param body_len  receives the (decoded) body length.
 * @return the HTTP status code, or a negative HttpClientError on a malformed response.
 */
int http_client_parse_response(uint8_t *buf, size_t len, size_t *body_off, size_t *body_len);

// ---------------------------------------------------------------------------
// Transport (ESP32 only; returns HTTP_CLIENT_ERR_* on a host build)
// ---------------------------------------------------------------------------

/** @brief Blocking GET @p url. @return the status code (>0) or a negative HttpClientError. */
int http_get(const char *url, HttpClientResult *out);

/**
 * @brief Blocking POST @p body to @p url with @p content_type.
 * @return the status code (>0) or a negative HttpClientError.
 */
int http_post(const char *url, const char *content_type, const uint8_t *body, size_t body_len, HttpClientResult *out);

// ---------------------------------------------------------------------------
// https:// server authentication (optional; needs PC_ENABLE_HTTP_CLIENT_TLS)
// ---------------------------------------------------------------------------
// By default the client encrypts but does NOT authenticate the server (no trust
// store). Install a CA and/or a certificate pin to authenticate the peer; calls
// are no-ops on a build without client TLS. Set once before issuing requests.

/** @brief Trust anchor for https:// verification (PEM incl. NUL, or DER; nullptr clears). */
void http_client_set_ca(const uint8_t *ca, size_t ca_len);

/** @brief Pin the server certificate by its SHA-256 (32 bytes of the DER; nullptr clears). */
void http_client_set_pin(const uint8_t sha256[32]);

/** @brief Clear any installed CA / pin (back to encrypt-only). */
void http_client_clear_verify();

#endif // PC_ENABLE_HTTP_CLIENT

PROTO_END_DECLS

#endif // PROTOCORE_HTTP_CLIENT_H
