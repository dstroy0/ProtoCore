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
#include "shared_primitives/mime.h"
#include <stdio.h>
#include <string.h>

static const pc_field UPLOAD_OK[] = {{PC_FK_LIT, 0, 3, "OK "}, PC_U32, {PC_FK_LIT, 0, 6, " bytes"}, PC_END};

// All upload-service state, owned by one instance (internal linkage): the server handle, the
// route path, the destination filesystem + path, and the per-upload file/flags/counter (one
// upload at a time on this single-task device). Grouped so it is one named owner, cross-TU
// unreachable.
typedef struct
{
    const char *path = NULL;
    fs::FS *fs = NULL;
    const char *dest = NULL;
    fs::File file;
    proto_bool active = PROTO_FALSE; ///< Destination file opened for the current upload.
    proto_bool error = PROTO_FALSE;  ///< A write failed during the current upload.
    size_t written = 0;              ///< Bytes written so far / in the last upload.
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
    if (!s_upl.path || strcmp(req->path, s_upl.path) != 0) // GCOVR_EXCL_BR_LINE  null path unreachable (see above)
    {
        return PROTO_FALSE;
    }

    s_upl.active = PROTO_FALSE;
    s_upl.error = PROTO_FALSE;
    s_upl.written = 0;
    // The `s_upl.fs` half is unreachable: it is always the address of the `fs::FS &fs` reference
    // parameter of pc_upload_begin(), and a reference can't be null without UB at the call site -
    // only s_upl.dest (a plain pointer) can legitimately be null here.
    if (s_upl.fs && s_upl.dest) // GCOVR_EXCL_BR_LINE  null fs unreachable (see above)
    {
        s_upl.file = s_upl.fs->open(s_upl.dest, "w");
        if (s_upl.file)
        {
            s_upl.active = PROTO_TRUE;
        }
        else
        {
            s_upl.error = PROTO_TRUE;
        }
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
        if (s_upl.file.write(data, len) != len)
        {
            s_upl.error = PROTO_TRUE;
        }
        else
        {
            s_upl.written += len;
        }
    }
}

/// @brief Route handler (runs at PARSE_COMPLETE): close the file and reply.
static void upload_handle(uint8_t slot_id, HttpReq *req)
{
    if (!req->body_streaming)
    {
        send_text(slot_id, 400, PC_MIME_TEXT_PLAIN, "POST a file body");
        return;
    }
    if (s_upl.active)
    {
        s_upl.file.close();
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

void pc_upload_begin(const char *path, fs::FS &fs, const char *dest_path)
{
    s_upl.path = path;
    s_upl.fs = &fs;
    s_upl.dest = dest_path;

    http_parser_set_stream_hooks(upload_stream_begin, upload_stream_data);
    on_http(path, HTTP_POST, upload_handle);
}

#endif // PC_ENABLE_UPLOAD
