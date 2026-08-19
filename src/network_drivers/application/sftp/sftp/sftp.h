// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sftp.h
 * @brief SFTP protocol v3 wire codec (SSH_FXP_*, draft-ietf-secsh-filexfer-02) - the pure, host-testable
 *        half of the SSH SFTP subsystem (PROTOCORE_ENABLE_SSH_SFTP).
 *
 * SFTP runs as an SSH "subsystem" over a session channel: length-prefixed packets, each `uint32 length ||
 * byte type || …`. This file parses request packets and builds response packets into caller buffers - no
 * filesystem, no SSH, no Arduino, zero heap. The fs::FS binding + the channel glue live in
 * network_drivers/application/sftp/ssh_sftp.
 *
 * Everything is big-endian (SSH wire order). A "string" is a `uint32 length || bytes` field (not
 * NUL-terminated). Version 3 is the de-facto standard (the OpenSSH sftp client's default).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SFTP_H
#define PROTOCORE_SFTP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SSH_SFTP

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define PROTOCORE_SFTP_VERSION 3

// --- request message types (client -> server) ---
#define PROTOCORE_SSH_FXP_INIT 1
#define PROTOCORE_SSH_FXP_OPEN 3
#define PROTOCORE_SSH_FXP_CLOSE 4
#define PROTOCORE_SSH_FXP_READ 5
#define PROTOCORE_SSH_FXP_WRITE 6
#define PROTOCORE_SSH_FXP_LSTAT 7
#define PROTOCORE_SSH_FXP_FSTAT 8
#define PROTOCORE_SSH_FXP_SETSTAT 9
#define PROTOCORE_SSH_FXP_FSETSTAT 10
#define PROTOCORE_SSH_FXP_OPENDIR 11
#define PROTOCORE_SSH_FXP_READDIR 12
#define PROTOCORE_SSH_FXP_REMOVE 13
#define PROTOCORE_SSH_FXP_MKDIR 14
#define PROTOCORE_SSH_FXP_RMDIR 15
#define PROTOCORE_SSH_FXP_REALPATH 16
#define PROTOCORE_SSH_FXP_STAT 17
#define PROTOCORE_SSH_FXP_RENAME 18

// --- response message types (server -> client) ---
#define PROTOCORE_SSH_FXP_VERSION 2
#define PROTOCORE_SSH_FXP_STATUS 101
#define PROTOCORE_SSH_FXP_HANDLE 102
#define PROTOCORE_SSH_FXP_DATA 103
#define PROTOCORE_SSH_FXP_NAME 104
#define PROTOCORE_SSH_FXP_ATTRS 105

// --- status / error codes (PROTOCORE_SSH_FXP_STATUS) ---
#define PROTOCORE_SSH_FX_OK 0
#define PROTOCORE_SSH_FX_EOF 1
#define PROTOCORE_SSH_FX_NO_SUCH_FILE 2
#define PROTOCORE_SSH_FX_PERMISSION_DENIED 3
#define PROTOCORE_SSH_FX_FAILURE 4
#define PROTOCORE_SSH_FX_BAD_MESSAGE 5
#define PROTOCORE_SSH_FX_OP_UNSUPPORTED 8

// --- PROTOCORE_SSH_FXP_OPEN pflags ---
#define PROTOCORE_SSH_FXF_READ 0x00000001
#define PROTOCORE_SSH_FXF_WRITE 0x00000002
#define PROTOCORE_SSH_FXF_APPEND 0x00000004
#define PROTOCORE_SSH_FXF_CREAT 0x00000008
#define PROTOCORE_SSH_FXF_TRUNC 0x00000010
#define PROTOCORE_SSH_FXF_EXCL 0x00000020

// --- ATTRS flag word ---
#define PROTOCORE_SSH_FILEXFER_ATTR_SIZE 0x00000001
#define PROTOCORE_SSH_FILEXFER_ATTR_UIDGID 0x00000002
#define PROTOCORE_SSH_FILEXFER_ATTR_PERMS 0x00000004
#define PROTOCORE_SSH_FILEXFER_ATTR_ACMODTIME 0x00000008
#define PROTOCORE_SSH_FILEXFER_ATTR_EXTENDED 0x80000000

// POSIX mode bits used in the permissions attr / longname (S_IFDIR / S_IFREG + rwx).
#define PROTOCORE_SFTP_S_IFDIR 0040000
#define PROTOCORE_SFTP_S_IFREG 0100000

/** @brief A decoded/encoded ATTRS blob (only the v3 fields the server sets/reads). */
typedef struct
{
    uint32_t flags;       ///< which fields below are present (SSH_FILEXFER_ATTR_*)
    uint64_t size;        ///< file size (ATTR_SIZE)
    uint32_t permissions; ///< POSIX mode incl. S_IFDIR/S_IFREG (ATTR_PERMISSIONS)
    uint32_t atime;       ///< access time, unix epoch (ATTR_ACMODTIME)
    uint32_t mtime;       ///< modify time, unix epoch (ATTR_ACMODTIME)
} SftpAttrs;

typedef struct
{
    const uint8_t *p;
    size_t len;
    size_t off;
    proto_bool ok; ///< false once any read ran past the end (all further reads are no-ops)
} SftpReader;

typedef struct
{
    uint8_t *p;
    size_t cap;
    size_t off;     ///< current write position (starts at 4, past the reserved length prefix)
    proto_bool ovf; ///< set once a write would exceed cap
} SftpWriter;

/** @brief What rd_init takes: r, payload, len. */
typedef struct
{
    SftpReader *r;
    const uint8_t *payload;
    size_t len;
} SftpRdInitArgs;

/** @brief What rd_u8 takes: r. */
typedef struct
{
    SftpReader *r;
} SftpRdU8Args;

/** @brief What rd_u32 takes: r. */
typedef struct
{
    SftpReader *r;
} SftpRdU32Args;

/** @brief What rd_u64 takes: r. */
typedef struct
{
    SftpReader *r;
} SftpRdU64Args;

/** @brief What rd_string takes: r, out, out_len. */
typedef struct
{
    SftpReader *r;
    const uint8_t **out;
    uint32_t *out_len;
} SftpRdStringArgs;

/** @brief What rd_attrs takes: r, a. */
typedef struct
{
    SftpReader *r;
    SftpAttrs *a;
} SftpRdAttrsArgs;

/** @brief What wr_init takes: w, out, cap. */
typedef struct
{
    SftpWriter *w;
    uint8_t *out;
    size_t cap;
} SftpWrInitArgs;

/** @brief What wr_u8 takes: w, v. */
typedef struct
{
    SftpWriter *w;
    uint8_t v;
} SftpWrU8Args;

/** @brief What wr_u32 takes: w, v. */
typedef struct
{
    SftpWriter *w;
    uint32_t v;
} SftpWrU32Args;

/** @brief What wr_u64 takes: w, v. */
typedef struct
{
    SftpWriter *w;
    uint64_t v;
} SftpWrU64Args;

/** @brief What wr_bytes takes: w, b, n. */
typedef struct
{
    SftpWriter *w;
    const void *b;
    size_t n;
} SftpWrBytesArgs;

/** @brief What wr_string takes: w, s, n. */
typedef struct
{
    SftpWriter *w;
    const void *s;
    uint32_t n;
} SftpWrStringArgs;

/** @brief What wr_attrs takes: w, a. */
typedef struct
{
    SftpWriter *w;
    const SftpAttrs *a;
} SftpWrAttrsArgs;

/** @brief What wr_finish takes: w. */
typedef struct
{
    SftpWriter *w;
} SftpWrFinishArgs;

/** @brief What wr_pos takes: w. */
typedef struct
{
    const SftpWriter *w;
} SftpWrPosArgs;

/** @brief What wr_patch_u32 takes: w, at, v. */
typedef struct
{
    SftpWriter *w;
    size_t at;
    uint32_t v;
} SftpWrPatchU32Args;

/** @brief What frame_len takes: buf, have, max. */
typedef struct
{
    const uint8_t *buf;
    size_t have;
    size_t max;
} SftpFrameLenArgs;

/** @brief What build_version takes: out, cap. */
typedef struct
{
    uint8_t *out;
    size_t cap;
} SftpBuildVersionArgs;

/** @brief What build_status takes: id, code, msg, out, cap. */
typedef struct
{
    uint32_t id;
    uint32_t code;
    const char *msg;
    uint8_t *out;
    size_t cap;
} SftpBuildStatusArgs;

/** @brief What build_handle takes: id, handle, hlen, out, cap. */
typedef struct
{
    uint32_t id;
    const void *handle;
    uint32_t hlen;
    uint8_t *out;
    size_t cap;
} SftpBuildHandleArgs;

/** @brief What build_attrs takes: id, a, out, cap. */
typedef struct
{
    uint32_t id;
    const SftpAttrs *a;
    uint8_t *out;
    size_t cap;
} SftpBuildAttrsArgs;

/** @brief What build_data takes: id, data, dlen, out, cap. */
typedef struct
{
    uint32_t id;
    const void *data;
    uint32_t dlen;
    uint8_t *out;
    size_t cap;
} SftpBuildDataArgs;

/** @brief What build_name1 takes: id, name, longname, a, out, cap. */
typedef struct
{
    uint32_t id;
    const char *name;
    const char *longname;
    const SftpAttrs *a;
    uint8_t *out;
    size_t cap;
} SftpBuildName1Args;

/** @brief What format_longname takes: is_dir, perms, size, mtime, ... */
typedef struct
{
    proto_bool is_dir;
    uint32_t perms;
    uint64_t size;
    uint32_t mtime;
    const char *name;
    char *out;
    size_t cap;
} SftpFormatLongnameArgs;

/**
 * @brief SFTP protocol v3 wire codec (SSH_FXP_*, draft-ietf-secsh-filexfer-02) - the pure, host-testable half of the
 * SSH SFTP subsystem (PROTOCORE_ENABLE_SSH_SFTP).
 *
 * A caller sets the members a call takes, invokes it through ::Sftp with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Sftp.rd_init_args.r = ...;
 *   Sftp.rd_init_args.payload = ...;
 *   Sftp.rd_init_args.len = ...;
 *   Sftp.rd_init(work);
 *
 * @var SftpNs::rd_init_args  what rd_init takes: r, payload, len
 * @var SftpNs::rd_u8_args  what rd_u8 takes: r
 * @var SftpNs::rd_u32_args  what rd_u32 takes: r
 * @var SftpNs::rd_u64_args  what rd_u64 takes: r
 * @var SftpNs::rd_string_args  what rd_string takes: r, out, out_len
 * @var SftpNs::rd_attrs_args  what rd_attrs takes: r, a
 * @var SftpNs::wr_init_args  what wr_init takes: w, out, cap
 * @var SftpNs::wr_u8_args  what wr_u8 takes: w, v
 * @var SftpNs::wr_u32_args  what wr_u32 takes: w, v
 * @var SftpNs::wr_u64_args  what wr_u64 takes: w, v
 * @var SftpNs::wr_bytes_args  what wr_bytes takes: w, b, n
 * @var SftpNs::wr_string_args  what wr_string takes: w, s, n
 * @var SftpNs::wr_attrs_args  what wr_attrs takes: w, a
 * @var SftpNs::wr_finish_args  what wr_finish takes: w
 * @var SftpNs::wr_pos_args  what wr_pos takes: w
 * @var SftpNs::wr_patch_u32_args  what wr_patch_u32 takes: w, at, v
 * @var SftpNs::frame_len_args  what frame_len takes: buf, have, max
 * @var SftpNs::build_version_args  what build_version takes: out, cap
 * @var SftpNs::build_status_args  what build_status takes: id, code, msg, out, cap
 * @var SftpNs::build_handle_args  what build_handle takes: id, handle, hlen, out, cap
 * @var SftpNs::build_attrs_args  what build_attrs takes: id, a, out, cap
 * @var SftpNs::build_data_args  what build_data takes: id, data, dlen, out, cap
 * @var SftpNs::build_name1_args  what build_name1 takes: id, name, longname, a, out, cap
 * @var SftpNs::format_longname_args  what format_longname takes: is_dir, perms, size, mtime,
 * @var SftpNs::ok  a call's true/false outcome
 * @var SftpNs::value  the value a call reports
 * @var SftpNs::u32  what a call reports
 * @var SftpNs::u64  what a call reports
 * @var SftpNs::n  the string length written (excluding NUL), clamped to cap-1
 * @var SftpNs::rd_init  rd_init
 * @var SftpNs::rd_u8  rd_u8
 * @var SftpNs::rd_u32  rd_u32
 * @var SftpNs::rd_u64  rd_u64
 * @var SftpNs::rd_string  read a `uint32 len || bytes` string as a pointer into the payload ...
 * @var SftpNs::rd_attrs  parse an ATTRS blob (only known fields kept; unknown/extended ...
 * @var SftpNs::wr_init  wr_init
 * @var SftpNs::wr_u8  wr_u8
 * @var SftpNs::wr_u32  wr_u32
 * @var SftpNs::wr_u64  wr_u64
 * @var SftpNs::wr_bytes  wr_bytes
 * @var SftpNs::wr_string  wr_string
 * @var SftpNs::wr_attrs  wr_attrs
 * @var SftpNs::wr_finish  backfill the length prefix (= off-4). the total packet length, or 0 ...
 * @var SftpNs::wr_pos  position where the next byte will be written (used to remember a ...
 * @var SftpNs::wr_patch_u32  overwrite a big-endian uint32 already written at at (for ...
 * @var SftpNs::frame_len  the full length of the leading packet in buf (4-byte prefix + ...
 * @var SftpNs::build_version  build_version
 * @var SftpNs::build_status  build_status
 * @var SftpNs::build_handle  build_handle
 * @var SftpNs::build_attrs  build_attrs
 * @var SftpNs::build_data  PROTOCORE_SSH_FXP_DATA carrying data[0..dlen)
 * @var SftpNs::build_name1  PROTOCORE_SSH_FXP_NAME with one entry (filename + longname + attrs) ...
 * @var SftpNs::format_longname  format a Unix `ls -l`-style longname for a NAME entry, e.g. ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    SftpRdInitArgs rd_init_args;
    SftpRdU8Args rd_u8_args;
    SftpRdU32Args rd_u32_args;
    SftpRdU64Args rd_u64_args;
    SftpRdStringArgs rd_string_args;
    SftpRdAttrsArgs rd_attrs_args;
    SftpWrInitArgs wr_init_args;
    SftpWrU8Args wr_u8_args;
    SftpWrU32Args wr_u32_args;
    SftpWrU64Args wr_u64_args;
    SftpWrBytesArgs wr_bytes_args;
    SftpWrStringArgs wr_string_args;
    SftpWrAttrsArgs wr_attrs_args;
    SftpWrFinishArgs wr_finish_args;
    SftpWrPosArgs wr_pos_args;
    SftpWrPatchU32Args wr_patch_u32_args;
    SftpFrameLenArgs frame_len_args;
    SftpBuildVersionArgs build_version_args;
    SftpBuildStatusArgs build_status_args;
    SftpBuildHandleArgs build_handle_args;
    SftpBuildAttrsArgs build_attrs_args;
    SftpBuildDataArgs build_data_args;
    SftpBuildName1Args build_name1_args;
    SftpFormatLongnameArgs format_longname_args;

    proto_bool ok;
    uint8_t value;
    uint32_t u32;
    uint64_t u64;
    size_t n;

    void (*const rd_init)(uint8_t *restrict work);
    void (*const rd_u8)(uint8_t *restrict work);
    void (*const rd_u32)(uint8_t *restrict work);
    void (*const rd_u64)(uint8_t *restrict work);
    void (*const rd_string)(uint8_t *restrict work);
    void (*const rd_attrs)(uint8_t *restrict work);
    void (*const wr_init)(uint8_t *restrict work);
    void (*const wr_u8)(uint8_t *restrict work);
    void (*const wr_u32)(uint8_t *restrict work);
    void (*const wr_u64)(uint8_t *restrict work);
    void (*const wr_bytes)(uint8_t *restrict work);
    void (*const wr_string)(uint8_t *restrict work);
    void (*const wr_attrs)(uint8_t *restrict work);
    void (*const wr_finish)(uint8_t *restrict work);
    void (*const wr_pos)(uint8_t *restrict work);
    void (*const wr_patch_u32)(uint8_t *restrict work);
    void (*const frame_len)(uint8_t *restrict work);
    void (*const build_version)(uint8_t *restrict work);
    void (*const build_status)(uint8_t *restrict work);
    void (*const build_handle)(uint8_t *restrict work);
    void (*const build_attrs)(uint8_t *restrict work);
    void (*const build_data)(uint8_t *restrict work);
    void (*const build_name1)(uint8_t *restrict work);
    void (*const format_longname)(uint8_t *restrict work);
} SftpNs;

/** @brief The one symbol this module exports. */
extern SftpNs Sftp;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_SFTP

#endif // PROTOCORE_SFTP_H
