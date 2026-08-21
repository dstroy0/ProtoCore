// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_UPLOAD_SERVICE_H
#define PROTOCORE_UPLOAD_SERVICE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file upload_service.h
 * @brief Streaming file upload to an Arduino FS (PROTOCORE_ENABLE_UPLOAD).
 *
 * Registers a POST route whose request body is streamed straight into a file on
 * a filesystem (LittleFS / SPIFFS / SD) in FILE_CHUNK_SIZE pieces - the upload
 * never has to fit in RAM. Reuses the parser's streaming-body hook (the same
 * mechanism OTA uses), so it is zero-heap and bounded.
 *
 * One upload at a time (the device runs a single loop task). Only one streaming
 * sink can be installed, so PROTOCORE_ENABLE_UPLOAD and PROTOCORE_ENABLE_OTA share the
 * parser hook - register whichever you need (not both on the same build).
 *
 * @c work is PROTOCORE_UPLOAD_SERVICE_BORROW bytes the CALLER took, at an address it knows. It is not held past the
 * call, so nothing here aliases it. How those bytes are carved is this module's and is never named here.
 */

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    void (*begin)(uint8_t *, const char *, const char *);
    size_t (*last_size)(uint8_t *);
} UploadServiceNs;
PROTOCORE_NS_LAYOUT(UploadServiceNs, begin, last_size);

/**
 * @brief Register a streaming-upload endpoint. A `POST path` request streams .
 * @param work PROTOCORE_UPLOAD_SERVICE_BORROW bytes the caller took. Not held past the call.
 * @param path the upload URL (e.g. "/upload")
 * @param dest_path destination file path (e.g. "/uploads/data.bin")
 */
void protocore_upload_service_begin(uint8_t *work, const char *path, const char *dest_path);
/**
 * @brief Bytes written by the most recent upload (for handlers / tests).
 * @param work PROTOCORE_UPLOAD_SERVICE_BORROW bytes the caller took. Not held past the call.
 * @return The size_t.
 */
size_t protocore_upload_service_last_size(uint8_t *work);

/**
 * @brief The PROTOCORE_UPLOAD_SERVICE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_upload_service_span(void);

/** @brief Module namespace. */
PROTOCORE_NS UploadServiceNs UploadService PROTOCORE_UNUSED = {.begin = protocore_upload_service_begin,
                                                               .last_size = protocore_upload_service_last_size};

PROTOCORE_END_DECLS

#endif // PROTOCORE_UPLOAD_SERVICE_H
