// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file upload_service.c
 * @brief Streaming file upload: POST body -> Arduino FS file (PROTOCORE_ENABLE_UPLOAD).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_UPLOAD

#include "mmgr/plaintext/plaintext.h"   // the persistent end this module's state is taken from
#include "mmgr/protoframe/protoframe.h" // the one frame engine
#include "mmgr/protostr/protostr.h"
#include "upload_service.h"

static uint8_t mnt_work[16]; // the borrow an entry takes; Mnt never reads it

#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "protocore.h"
#include "server/storage/mnt/mnt.h" // the storage seam: protocore_mnt_active()
#include "shared/mime/mime.h"

PROTOCORE_BEGIN_DECLS

static const protocore_field UPLOAD_OK[] = {
    {PROTOCORE_FK_LIT, 0, 3, "OK "}, PROTOCORE_U32, {PROTOCORE_FK_LIT, 0, 6, " bytes"}, PROTOCORE_END};

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
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define UPLOAD_SERVICE_OFF_CTX 0u
static_assert(UPLOAD_SERVICE_OFF_CTX + sizeof(UploadCtx) <= PROTOCORE_UPLOAD_SERVICE_BORROW,
              "PROTOCORE_UPLOAD_SERVICE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define UPLOAD_SERVICE_CTX(w) ((UploadCtx *)(void *)((w) + UPLOAD_SERVICE_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_UPLOAD_SERVICE_BORROW persistent bytes, or null while the pool was short
} UploadServiceOwnCtx;
static UploadServiceOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_upload_service_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_UPLOAD_SERVICE_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

/// @brief Stream-begin hook: accept POST @p UPLOAD_SERVICE_CTX(work)->path and open the destination file.
static proto_bool upload_stream_begin(HttpReq *req)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_upload_service_span();

    if (!str.eq(req->method, "POST", sizeof("POST"), PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    // The `!UPLOAD_SERVICE_CTX(work)->path` half is unreachable: UPLOAD_SERVICE_CTX(work)->path is only ever set by
    // protocore_upload_begin(), which immediately hands that same pointer to on_http() -> fill_route_base(), whose
    // strncpy() would fault on a null path before any request could reach this hook. So by the
    // time this hook is installed, UPLOAD_SERVICE_CTX(work)->path is always a live C string.
    if (!UPLOAD_SERVICE_CTX(work)->path ||
        !str.eq(req->path, UPLOAD_SERVICE_CTX(work)->path,
                str.len(UPLOAD_SERVICE_CTX(work)->path, MAX_PATH_LEN - 1u) + 1u, PROTO_FALSE))
    {
        return PROTO_FALSE;
    }

    UPLOAD_SERVICE_CTX(work)->active = PROTO_FALSE;
    UPLOAD_SERVICE_CTX(work)->error = PROTO_FALSE;
    UPLOAD_SERVICE_CTX(work)->written = 0;
    UPLOAD_SERVICE_CTX(work)->handle = -1;
    // The seam fails closed when nothing is mounted, so a cold mount answers "upload failed"
    // rather than faulting.
    Mnt.active(mnt_work);
    const protocore_mnt_backend *mnt = Mnt.backend;
    if (mnt && UPLOAD_SERVICE_CTX(work)->dest)
    {
        UPLOAD_SERVICE_CTX(work)->handle = mnt->open(UPLOAD_SERVICE_CTX(work)->dest, PROTOCORE_MNT_WRITE);
        if (UPLOAD_SERVICE_CTX(work)->handle >= 0)
        {
            UPLOAD_SERVICE_CTX(work)->active = PROTO_TRUE;
        }
        else
        {
            UPLOAD_SERVICE_CTX(work)->error = PROTO_TRUE;
        }
    }
    else
    {
        UPLOAD_SERVICE_CTX(work)->error = PROTO_TRUE;
    }
    // Stream regardless so the body is consumed and the route handler can reply.
    return PROTO_TRUE;
}

/// @brief Stream-data hook: write one body chunk to the destination file.
static void upload_stream_data(HttpReq *req, const uint8_t *data, size_t len)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_upload_service_span();

    (void)req; // a single upload streams at a time
    if (UPLOAD_SERVICE_CTX(work)->active && !UPLOAD_SERVICE_CTX(work)->error)
    {
        Mnt.active(mnt_work);
        const protocore_mnt_backend *mnt = Mnt.backend;
        if (!mnt || mnt->write(UPLOAD_SERVICE_CTX(work)->handle, data, len) != (int)len)
        {
            UPLOAD_SERVICE_CTX(work)->error = PROTO_TRUE;
        }
        else
        {
            UPLOAD_SERVICE_CTX(work)->written += len;
        }
    }
}

/// @brief HttpRoute handler (runs at PARSE_COMPLETE): close the file and reply.
static void upload_handle(uint8_t slot_id, HttpReq *req)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_upload_service_span();

    if (!req->body_streaming)
    {
        send_text(slot_id, 400, PROTOCORE_MIME_TEXT_PLAIN, "POST a file body");
        return;
    }
    if (UPLOAD_SERVICE_CTX(work)->active)
    {
        Mnt.active(mnt_work);
        const protocore_mnt_backend *mnt = Mnt.backend;
        if (mnt)
        {
            mnt->close(UPLOAD_SERVICE_CTX(work)->handle);
        }
        UPLOAD_SERVICE_CTX(work)->handle = -1;
    }
    if (!UPLOAD_SERVICE_CTX(work)->active || UPLOAD_SERVICE_CTX(work)->error)
    {
        send_text(slot_id, 500, PROTOCORE_MIME_TEXT_PLAIN, "upload failed");
        return;
    }
    char msg[48];
    frame.build(msg, sizeof(msg), UPLOAD_OK,
                (const protocore_fval[]){PROTOCORE_VU32((uint32_t)UPLOAD_SERVICE_CTX(work)->written)}, 1);
    send_text(slot_id, 200, PROTOCORE_MIME_TEXT_PLAIN, msg);
}

static void upload_service_last_size(uint8_t *restrict work)
{
    UploadService.n = UPLOAD_SERVICE_CTX(work)->written;
}

static void upload_service_begin(uint8_t *restrict work)
{
    const char *path = UploadService.begin_args.path;
    const char *dest_path = UploadService.begin_args.dest_path;

    UPLOAD_SERVICE_CTX(work)->path = path;
    UPLOAD_SERVICE_CTX(work)->dest = dest_path;
    UPLOAD_SERVICE_CTX(work)->handle = -1;

    HttpParser.set_stream_hooks_args.begin = upload_stream_begin;
    HttpParser.set_stream_hooks_args.data = upload_stream_data;
    HttpParser.set_stream_hooks_args.abort = NULL;
    HttpParser.set_stream_hooks(protocore_http_parser_span());
    on_http(path, HTTP_POST, upload_handle);
}

UploadServiceNs UploadService = {
    .begin = upload_service_begin,
    .last_size = upload_service_last_size,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_UPLOAD
