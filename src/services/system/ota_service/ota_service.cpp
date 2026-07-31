// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ota_service.cpp
 * @brief Authenticated streaming OTA firmware update (PC_ENABLE_OTA).
 */

#include "ota_service.h"
#include "services/system/clock.h" // pcdelay

#if PC_ENABLE_OTA && defined(ARDUINO)

#include "network_drivers/presentation/codec/base64/base64.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "protocore.h"
#include "shared_primitives/mime.h"
#include <Arduino.h>
#include <Update.h>
#include <string.h>

// All OTA-service state, owned by one instance (internal linkage): the server handle, the
// route path, the Basic-auth credentials, and the per-upload flags (one upload at a time on
// this single-task device). Grouped so it is one named owner, unreachable cross-TU.
struct OtaCtx
{
    PC *server = nullptr;
    const char *path = nullptr;
    char user[MAX_AUTH_LEN] = {0};
    char pass[MAX_AUTH_LEN] = {0};
    bool authed = false; ///< Credentials validated for the current upload.
    bool active = false; ///< Update.begin() succeeded for the current upload.
    bool error = false;  ///< A write failed during the current upload.
};
static OtaCtx s_ota;

/// @brief Validate the request's HTTP Basic credentials against s_ota.user/s_ota.pass.
static bool ota_check_auth(HttpReq *req)
{
    const char *h = http_get_header(req, "Authorization");
    if (!h || strncmp(h, "Basic ", 6) != 0)
    {
        return false;
    }

    uint8_t decoded[MAX_AUTH_LEN * 2 + 2];
    size_t n = pc_base64_decode(h + 6, decoded, sizeof(decoded) - 1);
    if (n == 0)
    {
        return false;
    }
    decoded[n] = '\0';

    const char *colon = (const char *)memchr(decoded, ':', n);
    if (!colon)
    {
        return false;
    }
    size_t ulen = (size_t)(colon - (const char *)decoded);
    const char *pass = colon + 1;
    return (ulen == strnlen(s_ota.user, sizeof(s_ota.user))) && (memcmp(decoded, s_ota.user, ulen) == 0) &&
           (strcmp(pass, s_ota.pass) == 0);
}

/// @brief Stream-begin hook: accept POST @p s_ota.path; begin Update if authorized.
static bool ota_stream_begin(HttpReq *req)
{
    if (strcmp(req->method, "POST") != 0)
    {
        return false;
    }
    if (!s_ota.path || strcmp(req->path, s_ota.path) != 0)
    {
        return false;
    }

    s_ota.authed = ota_check_auth(req);
    s_ota.active = false;
    s_ota.error = false;
    if (s_ota.authed)
    {
        if (Update.begin(UPDATE_SIZE_UNKNOWN))
        {
            s_ota.active = true;
        }
        else
        {
            s_ota.error = true;
        }
    }
    // Stream regardless so the body is consumed and the route handler can reply;
    // when unauthorized/!active the data hook simply discards.
    return true;
}

/// @brief Stream-data hook: write one chunk of the image to Update.
static void ota_stream_data(HttpReq *req, const uint8_t *data, size_t len)
{
    (void)req; // a single OTA image streams at a time
    if (s_ota.authed && s_ota.active && !s_ota.error)
    {
        if (Update.write((uint8_t *)data, len) != len)
        {
            s_ota.error = true;
        }
    }
}

/// @brief Route handler (runs at ParseState::PARSE_COMPLETE): finalize and reply, then reboot.
static void ota_handle(uint8_t slot_id, HttpReq *req)
{
    if (!req->body_streaming)
    {
        s_ota.server->send(slot_id, 400, PC_MIME_TEXT_PLAIN, "POST a raw firmware image");
        return;
    }
    if (!s_ota.authed)
    {
        s_ota.server->send(slot_id, 401, PC_MIME_TEXT_PLAIN, "Unauthorized");
        return;
    }
    bool ok = s_ota.active && !s_ota.error && Update.end(true);
    if (!ok)
    {
        if (s_ota.active)
        {
            Update.abort();
        }
        s_ota.server->send(slot_id, 400, PC_MIME_TEXT_PLAIN, "Update failed");
        return;
    }
    s_ota.server->send(slot_id, 200, PC_MIME_TEXT_PLAIN, "OK - rebooting");
    pcdelay(150); // let the response flush before the reboot
    ESP.restart();
}

void pc_ota_begin(PC &server, const char *path, const char *user, const char *pass)
{
    s_ota.server = &server;
    s_ota.path = path;
    strncpy(s_ota.user, user ? user : "", sizeof(s_ota.user) - 1);
    s_ota.user[sizeof(s_ota.user) - 1] = '\0';
    strncpy(s_ota.pass, pass ? pass : "", sizeof(s_ota.pass) - 1);
    s_ota.pass[sizeof(s_ota.pass) - 1] = '\0';

    http_parser_set_stream_hooks(ota_stream_begin, ota_stream_data);
    server.on(path, HttpMethod::HTTP_POST, ota_handle);
}

#else

void pc_ota_begin(PC &server, const char *path, const char *user, const char *pass)
{
    (void)server;
    (void)path;
    (void)user;
    (void)pass;
}

#endif // PC_ENABLE_OTA && ARDUINO
