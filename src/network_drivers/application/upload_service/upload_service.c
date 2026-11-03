// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file upload_service.c
 * @brief Streaming file upload: POST body -> Arduino FS file (PROTOCORE_ENABLE_UPLOAD).
 */

#include "protocore_config.h" // the entry point: the widths

#if PROTOCORE_ENABLE_UPLOAD

#include "cellularum_laboro/cellularum_laboro.h"
#include "numeros_scribo/numeros_scribo.h" // the one frame engine
#include "server/core/worker/worker.h"     // the persistent end this module's state is taken from
#include "upload_service.h"

#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "protocore.h"
#include "server/storage/mnt/mnt.h" // the storage seam: protocore_mnt_active()
#include "shared/mime/mime.h"

static const mmgr_field UPLOAD_OK[] = {{MMGR_FK_LIT, 0, 3, "OK "}, MMGR_U32, {MMGR_FK_LIT, 0, 6, " bytes"}, MMGR_END};

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

// A region reached through a cast is only aligned if its OFFSET is: a cellblock aligns each cell to
// MMGR_CARCER_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    UPLOAD_SERVICE_OFF_CTX % _Alignof(UploadCtx) == 0,
    "UPLOAD_SERVICE_OFF_CTX is not a multiple of alignof(UploadCtx) - UPLOAD_SERVICE_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define UPLOAD_SERVICE_CTX(w) ((UploadCtx *)(void *)((w) + UPLOAD_SERVICE_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_UPLOAD_SERVICE_BORROW persistent bytes
} UploadServiceOwnCtx;
static UploadServiceOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_upload_service_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = (uint8_t *)protocore_plain_persist(PROTOCORE_UPLOAD_SERVICE_BORROW);
    }
    return s_own.span;
}

/// @brief Stream-begin hook: accept POST @p UPLOAD_SERVICE_CTX(work)->path and open the destination file.
static proto_bool upload_stream_begin(HttpReq *req)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *work = protocore_upload_service_span();

    if (!EMBED_CALL(cellul.eq, CatenaFinitaCfg, .src = req->method, .other = "POST", .cap = sizeof("POST"),
                    .ci = PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    // The `!UPLOAD_SERVICE_CTX(work)->path` half is unreachable: UPLOAD_SERVICE_CTX(work)->path is only ever set by
    // protocore_upload_begin(), which immediately hands that same pointer to on_http() -> fill_route_base(), whose
    // strncpy() would fault on a null path before any request could reach this hook. So by the
    // time this hook is installed, UPLOAD_SERVICE_CTX(work)->path is always a live C string.
    if (!UPLOAD_SERVICE_CTX(work)->path ||
        !EMBED_CALL(cellul.eq, CatenaFinitaCfg, .src = req->path, .other = UPLOAD_SERVICE_CTX(work)->path,
                    .cap = EMBED_CALL(cellul.len, CatenaFinitaCfg, .src = UPLOAD_SERVICE_CTX(work)->path,
                                      .cap = MAX_PATH_LEN - 1u) +
                           1u,
                    .ci = PROTO_FALSE))
    {
        return PROTO_FALSE;
    }

    UPLOAD_SERVICE_CTX(work)->active = PROTO_FALSE;
    UPLOAD_SERVICE_CTX(work)->error = PROTO_FALSE;
    UPLOAD_SERVICE_CTX(work)->written = 0;
    UPLOAD_SERVICE_CTX(work)->handle = -1;
    // The seam fails closed when nothing is mounted, so a cold mount answers "upload failed"
    // rather than faulting.
    Mnt.active(protocore_upload_service_span());
    const protocore_mnt_backend *mnt = MntV.backend;
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
    uint8_t *work = protocore_upload_service_span();

    (void)req; // a single upload streams at a time
    if (UPLOAD_SERVICE_CTX(work)->active && !UPLOAD_SERVICE_CTX(work)->error)
    {
        Mnt.active(protocore_upload_service_span());
        const protocore_mnt_backend *mnt = MntV.backend;
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
    uint8_t *work = protocore_upload_service_span();

    if (!req->body_streaming)
    {
        send_text(slot_id, 400, PROTOCORE_MIME_TEXT_PLAIN, "POST a file body");
        return;
    }
    if (UPLOAD_SERVICE_CTX(work)->active)
    {
        Mnt.active(protocore_upload_service_span());
        const protocore_mnt_backend *mnt = MntV.backend;
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
    EMBED_CALL(numer.build, NumerosCfg, .out = msg, .cap = sizeof(msg), .spec = UPLOAD_OK,
               .vals = (const mmgr_fval[]){MMGR_VU32((uint32_t)UPLOAD_SERVICE_CTX(work)->written)}, .nvals = 1);
    send_text(slot_id, 200, PROTOCORE_MIME_TEXT_PLAIN, msg);
}

size_t protocore_upload_service_last_size(uint8_t *work)
{
    return UPLOAD_SERVICE_CTX(work)->written;
}

void protocore_upload_service_begin(uint8_t *work, const char *path, const char *dest_path)
{

    UPLOAD_SERVICE_CTX(work)->path = path;
    UPLOAD_SERVICE_CTX(work)->dest = dest_path;
    UPLOAD_SERVICE_CTX(work)->handle = -1;

    HttpParser.set_stream_hooks(protocore_http_parser_span(), upload_stream_begin, upload_stream_data, NULL);
    on_http(path, HTTP_POST, upload_handle);
}

#endif // PROTOCORE_ENABLE_UPLOAD
