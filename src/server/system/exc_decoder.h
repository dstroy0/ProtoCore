// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file exc_decoder.h
 * @brief Panic / exception decoder for a live diagnostics panel (PROTOCORE_ENABLE_EXC_DECODER).
 *
 * When the part panics it prints a dump: a cause ("LoadProhibited"), a per-core register
 * dump (PC, EXCVADDR, ...), and a backtrace of `PC:SP` frame pairs. To resolve those PCs to file:line an
 * addr2line-style panel needs the firmware ELF, which lives off-device - so the on-device job is to
 * *extract and present* the raw decode: the cause, the faulting PC + data address, and the ordered
 * backtrace PC list, served as JSON for a live "/exception" panel (the browser or a build server then
 * resolves symbols). This is that extractor.
 *
 * It parses the panic text an app captures (from the console, a saved crash line, or a text rendering of
 * the core-dump partition) into a structured ExcInfo, and serializes it. Pure, zero heap, no stdlib
 * (hand-rolled hex/decimal parsing), host-testable against a captured panic string.
 */

#ifndef PROTOCORE_EXC_DECODER_H
#define PROTOCORE_EXC_DECODER_H

#include "protocore_config.h"
#include "server/filesystem/mnt.h" // protocore_mnt_backend - the store a dump is offloaded to

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_EXC_DECODER

#ifndef PROTOCORE_EXC_MAX_FRAMES
#define PROTOCORE_EXC_MAX_FRAMES 32 ///< backtrace frames retained (a panic rarely exceeds this).
#endif

/** @brief One backtrace frame: a program counter and its stack pointer. */
typedef struct
{
    uint32_t pc;
    uint32_t sp;
} ExcFrame;

/** @brief A decoded panic. Fields not found in the input are left at their zeroed / -1 defaults. */
typedef struct
{
    int core;                                  ///< panicking core number, or -1 if not present.
    char cause[32];                            ///< exception cause text (e.g. "LoadProhibited"), "" if absent.
    uint32_t pc;                               ///< faulting PC (register-dump PC, else first backtrace frame).
    uint32_t excvaddr;                         ///< faulting data address (EXCVADDR), 0 if absent.
    proto_bool has_excvaddr;                   ///< true if an EXCVADDR field was present.
    ExcFrame frames[PROTOCORE_EXC_MAX_FRAMES]; ///< backtrace, outermost-first as printed.
    size_t frame_count;
} ExcInfo;

/** @brief The panic a call reads, and where the decoded form lands. */
typedef struct
{
    const char *text; ///< the printed panic dump a parse walks
    ExcInfo *info;    ///< where a parse or a summary lands the decoded panic
} ExcParseArgs;

/** @brief The stored crash image: the span a call names, and where its bytes land. */
typedef struct
{
    ExcCoreDump *img;                    ///< where a presence check reports the image it found
    size_t offset;                       ///< where in the image a read starts
    void *buf;                           ///< where those bytes land
    size_t len;                          ///< how many
    const protocore_mnt_backend *file_sys; ///< the filesystem a save writes through
    const char *path;                    ///< the file it writes
} ExcDumpArgs;

/** @brief Where a report is written. */
typedef struct
{
    char *out;  ///< where the JSON lands
    size_t cap; ///< how much room it has
} ExcOutArgs;

/** @brief The decoder's own calls, described only in exc_decoder.c / exc_coredump.c. */
struct ExcDecoderInternal;

/**
 * @brief The panic decoder and the stored crash image.
 *
 * A caller sets the members a call takes, invokes it through ::Exc, and reads the outcome off the
 * same handle. The decoding is pure; the image calls reach the platform seam.
 *
 * @var ExcDecoderNs::parse_args  the panic a call reads, and where the decoded form lands
 * @var ExcDecoderNs::dump        the stored crash image: the span a call names, and where it lands
 * @var ExcDecoderNs::out_args    where a report is written
 * @var ExcDecoderNs::ok          a call's true/false outcome
 * @var ExcDecoderNs::n           bytes a report wrote, or 0 when it did not fit
 * @var ExcDecoderNs::parse       decode a printed panic dump
 * @var ExcDecoderNs::json        serialize a decoded panic
 * @var ExcDecoderNs::present     a stored crash image exists and verifies
 * @var ExcDecoderNs::summary     the stored image's summary, decoded into an ExcInfo
 * @var ExcDecoderNs::read        read a span of the stored image
 * @var ExcDecoderNs::save        stream the whole image to a file
 * @var ExcDecoderNs::erase       discard the stored image
 * @var ExcDecoderNs::internal    the calls that decode and offload
 *
 * No storage member: the parse works in the caller's ExcInfo and the image lives in the part.
 */
typedef struct
{
    ExcParseArgs parse_args;
    ExcDumpArgs dump;
    ExcOutArgs out_args;

    proto_bool ok;
    size_t n;

    void (*parse)(struct ExcDecoderInternal *ctx);
    void (*json)(struct ExcDecoderInternal *ctx);
#if PROTOCORE_HAS_VENDOR_COREDUMP
    void (*present)(struct ExcDecoderInternal *ctx);
    void (*summary)(struct ExcDecoderInternal *ctx);
    void (*read)(struct ExcDecoderInternal *ctx);
    void (*save)(struct ExcDecoderInternal *ctx);
    void (*erase)(struct ExcDecoderInternal *ctx);
#endif

    struct ExcDecoderInternal *internal;
} ExcDecoderNs;

/** @brief The one symbol this module exports. */
extern ExcDecoderNs Exc;

#endif // PROTOCORE_ENABLE_EXC_DECODER

PROTOCORE_END_DECLS

#endif // PROTOCORE_EXC_DECODER_H
