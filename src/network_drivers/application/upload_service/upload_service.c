// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file upload_service.c
 * @brief Streaming file upload: POST body -> Arduino FS file (PC_ENABLE_UPLOAD).
 */

#include "upload_service.h"
#include "mmgr/frame.h" // the one frame engine

#if PC_ENABLE_UPLOAD

#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "protocore.h"
#include "server/filesystem/mnt.h" // the storage seam: pc_mnt_active()
#include "shared_primitives/mime.h"
#include <stdio.h>

static const pc_field UPLOAD_OK[] = {{PC_FK_LIT, 0, 3, "OK "}, PC_U32, {PC_FK_LIT, 0, 6, " bytes"}, PC_END};

// All upload-service state, owned by one instance (internal linkage): the server handle, the
// route path, the destination filesystem + path, and the per-upload file/flags/counter (one
// upload at a time on this single-task device). Grouped so it is one named owner, cross-TU
// unreachable.
typedef struct
{
    const char *path;
    const char *dest;
    int handle;        ///< Open destination handle from the mount; only read while `active`.
    proto_bool active; ///< Destination file opened for the current upload.
    proto_bool error;  ///< A write failed during the current upload.
    size_t written;    ///< Bytes written so far / in the last upload.
} UploadCtx;
static UploadCtx s_upl;

/// @brief Stream-begin hook: accept POST @p s_upl.path and open the destination file.
static proto_bool upload_stream_begin(HttpReq *req)
{
    if (strcmp(req->method, "POST") != 0)
    {
        return PROTO_FALSE;
    }
    // The `!s_upl.path` half is unreachable: s_upl.path is only ever set by pc_upload_begin(),
    // which immediately hands that same pointer to on_http() -> fill_route_base(), whose
    // strncpy() would fault on a null path before any request could reach this hook. So by the
    // time this hook is installed, s_upl.path is always a live C string.
    if (!s_upl.path || strcmp(req->path, s_upl.path) != 0)
    {
        return PROTO_FALSE;
    }

    s_upl.active = PROTO_FALSE;
    s_upl.error = PROTO_FALSE;
    s_upl.written = 0;
    s_upl.handle = -1;
    // The seam fails closed when nothing is mounted, so a cold mount answers "upload failed"
    // rather than faulting.
    const pc_mnt_backend *mnt = pc_mnt_active();
    if (mnt && s_upl.dest)
    {
        s_upl.handle = mnt->open(s_upl.dest, PC_MNT_WRITE);
        if (s_upl.handle >= 0)
        {
            s_upl.active = PROTO_TRUE;
        }
        else
        {
            s_upl.error = PROTO_TRUE;
        }
    }
    else
    {
        s_upl.error = PROTO_TRUE;
    }
    // Stream regardless so the body is consumed and the route handler can reply.
    return PROTO_TRUE;
}

/// @brief Stream-data hook: write one body chunk to the destination file.
static void upload_stream_data(HttpReq *req, const uint8_t *data, size_t len)
{
    (void)req; // a single upload streams at a time
    if (s_upl.active && !s_upl.error)
    {
        const pc_mnt_backend *mnt = pc_mnt_active();
        if (!mnt || mnt->write(s_upl.handle, data, len) != (int)len)
        {
            s_upl.error = PROTO_TRUE;
        }
        else
        {
            s_upl.written += len;
        }
    }
}

/// @brief HttpRoute handler (runs at PARSE_COMPLETE): close the file and reply.
static void upload_handle(uint8_t slot_id, HttpReq *req)
{
    if (!req->body_streaming)
    {
        send_text(slot_id, 400, PC_MIME_TEXT_PLAIN, "POST a file body");
        return;
    }
    if (s_upl.active)
    {
        const pc_mnt_backend *mnt = pc_mnt_active();
        if (mnt)
        {
            mnt->close(s_upl.handle);
        }
        s_upl.handle = -1;
    }
    if (!s_upl.active || s_upl.error)
    {
        send_text(slot_id, 500, PC_MIME_TEXT_PLAIN, "upload failed");
        return;
    }
    char msg[48];
    pc_frame_build(msg, sizeof(msg), UPLOAD_OK, (uint32_t)s_upl.written);
    send_text(slot_id, 200, PC_MIME_TEXT_PLAIN, msg);
}

size_t pc_upload_last_size()
{
    return s_upl.written;
}

void pc_upload_begin(const char *path, const char *dest_path)
{
    s_upl.path = path;
    s_upl.dest = dest_path;
    s_upl.handle = -1;

    http_parser_set_stream_hooks(upload_stream_begin, upload_stream_data, NULL);
    on_http(path, HTTP_POST, upload_handle);
}

#endif // PC_ENABLE_UPLOAD
