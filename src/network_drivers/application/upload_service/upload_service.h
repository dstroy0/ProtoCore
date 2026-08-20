// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

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
 */

#ifndef PROTOCORE_UPLOAD_SERVICE_H
#define PROTOCORE_UPLOAD_SERVICE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_UPLOAD

PROTOCORE_BEGIN_DECLS

// PROTOCORE_UPLOAD_SERVICE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief What begin takes: path, dest_path. */
typedef struct
{
    const char *path;      ///< the upload URL (e.g. "/upload")
    const char *dest_path; ///< destination file path (e.g. "/uploads/data.bin")
} UploadServiceBeginArgs;
/**
 * @brief Streaming file upload to an Arduino FS (PROTOCORE_ENABLE_UPLOAD).
 *
 * A caller sets the members a call takes, invokes it through ::UploadService with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   UploadService.begin_args.path = ...;
 *   UploadService.begin_args.dest_path = ...;
 *   UploadService.begin(work);
 *
 * @var UploadServiceNs::begin_args  what begin takes: path, dest_path
 * @var UploadServiceNs::ok  a call's true/false outcome
 * @var UploadServiceNs::n  the count a call reports
 * @var UploadServiceNs::begin  register a streaming-upload endpoint. A `POST path` request streams ...
 * @var UploadServiceNs::last_size  bytes written by the most recent upload (for handlers / tests)
 *
 * @c work is PROTOCORE_UPLOAD_SERVICE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    UploadServiceBeginArgs begin_args;
    proto_bool ok;
    size_t n;
} UploadServiceVars;

/** @brief The operands and the outcome. */
extern UploadServiceVars UploadServiceV;

/** @brief The entries. */
typedef struct
{
    void (*const begin)(uint8_t *restrict work);
    void (*const last_size)(uint8_t *restrict work);
} UploadServiceNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in UploadServiceV or a region of the borrow at a fixed offset.
void protocore_upload_service_begin(uint8_t *restrict work);
void protocore_upload_service_last_size(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `UploadService.begin(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const UploadServiceNs UploadService __attribute__((unused)) = {
    .begin = protocore_upload_service_begin,
    .last_size = protocore_upload_service_last_size,
};

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

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_UPLOAD

#endif // PROTOCORE_UPLOAD_SERVICE_H
