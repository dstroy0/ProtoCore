// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sftp.h
 * @brief SFTP protocol v3 wire codec (SSH_FXP_*, draft-ietf-secsh-filexfer-02) - the pure, host-testable
 *        half of the SSH SFTP subsystem (PC_ENABLE_SSH_SFTP).
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

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SSH_SFTP

#define PC_SFTP_VERSION 3

// --- request message types (client -> server) ---
#define PC_SSH_FXP_INIT 1
#define PC_SSH_FXP_OPEN 3
#define PC_SSH_FXP_CLOSE 4
#define PC_SSH_FXP_READ 5
#define PC_SSH_FXP_WRITE 6
#define PC_SSH_FXP_LSTAT 7
#define PC_SSH_FXP_FSTAT 8
#define PC_SSH_FXP_SETSTAT 9
#define PC_SSH_FXP_FSETSTAT 10
#define PC_SSH_FXP_OPENDIR 11
#define PC_SSH_FXP_READDIR 12
#define PC_SSH_FXP_REMOVE 13
#define PC_SSH_FXP_MKDIR 14
#define PC_SSH_FXP_RMDIR 15
#define PC_SSH_FXP_REALPATH 16
#define PC_SSH_FXP_STAT 17
#define PC_SSH_FXP_RENAME 18

// --- response message types (server -> client) ---
#define PC_SSH_FXP_VERSION 2
#define PC_SSH_FXP_STATUS 101
#define PC_SSH_FXP_HANDLE 102
#define PC_SSH_FXP_DATA 103
#define PC_SSH_FXP_NAME 104
#define PC_SSH_FXP_ATTRS 105

// --- status / error codes (PC_SSH_FXP_STATUS) ---
#define PC_SSH_FX_OK 0
#define PC_SSH_FX_EOF 1
#define PC_SSH_FX_NO_SUCH_FILE 2
#define PC_SSH_FX_PERMISSION_DENIED 3
#define PC_SSH_FX_FAILURE 4
#define PC_SSH_FX_BAD_MESSAGE 5
#define PC_SSH_FX_OP_UNSUPPORTED 8

// --- PC_SSH_FXP_OPEN pflags ---
#define PC_SSH_FXF_READ 0x00000001
#define PC_SSH_FXF_WRITE 0x00000002
#define PC_SSH_FXF_APPEND 0x00000004
#define PC_SSH_FXF_CREAT 0x00000008
#define PC_SSH_FXF_TRUNC 0x00000010
#define PC_SSH_FXF_EXCL 0x00000020

// --- ATTRS flag word ---
#define PC_SSH_FILEXFER_ATTR_SIZE 0x00000001
#define PC_SSH_FILEXFER_ATTR_UIDGID 0x00000002
#define PC_SSH_FILEXFER_ATTR_PERMS 0x00000004
#define PC_SSH_FILEXFER_ATTR_ACMODTIME 0x00000008
#define PC_SSH_FILEXFER_ATTR_EXTENDED 0x80000000

// POSIX mode bits used in the permissions attr / longname (S_IFDIR / S_IFREG + rwx).
#define PC_SFTP_S_IFDIR 0040000
#define PC_SFTP_S_IFREG 0100000

/** @brief A decoded/encoded ATTRS blob (only the v3 fields the server sets/reads). */
typedef struct
{
    uint32_t flags;       ///< which fields below are present (SSH_FILEXFER_ATTR_*)
    uint64_t size;        ///< file size (ATTR_SIZE)
    uint32_t permissions; ///< POSIX mode incl. S_IFDIR/S_IFREG (ATTR_PERMISSIONS)
    uint32_t atime;       ///< access time, unix epoch (ATTR_ACMODTIME)
    uint32_t mtime;       ///< modify time, unix epoch (ATTR_ACMODTIME)
} SftpAttrs;

// --- reader: a bounds-checked cursor over a packet payload (the bytes after the 4-byte length prefix) ---
typedef struct
{
    const uint8_t *p;
    size_t len;
    size_t off;
    proto_bool ok; ///< false once any read ran past the end (all further reads are no-ops)
} SftpReader;

void pc_sftp_rd_init(SftpReader *r, const uint8_t *payload, size_t len);
uint8_t pc_sftp_rd_u8(SftpReader *r);
uint32_t pc_sftp_rd_u32(SftpReader *r);
uint64_t pc_sftp_rd_u64(SftpReader *r);
/** @brief Read a `uint32 len || bytes` string as a pointer into the payload (no copy). @return r->ok. */
proto_bool pc_sftp_rd_string(SftpReader *r, const uint8_t **out, uint32_t *out_len);
/** @brief Parse an ATTRS blob (only known fields kept; unknown/extended fields skipped). @return r->ok. */
proto_bool pc_sftp_rd_attrs(SftpReader *r, SftpAttrs *a);

// --- writer: build a packet into a caller buffer; reserves the 4-byte length prefix, backfilled by finish ---
typedef struct
{
    uint8_t *p;
    size_t cap;
    size_t off;     ///< current write position (starts at 4, past the reserved length prefix)
    proto_bool ovf; ///< set once a write would exceed cap
} SftpWriter;

void pc_sftp_wr_init(SftpWriter *w, uint8_t *out, size_t cap);
void pc_sftp_wr_u8(SftpWriter *w, uint8_t v);
void pc_sftp_wr_u32(SftpWriter *w, uint32_t v);
void pc_sftp_wr_u64(SftpWriter *w, uint64_t v);
void pc_sftp_wr_bytes(SftpWriter *w, const void *b, size_t n);
void pc_sftp_wr_string(SftpWriter *w, const void *s, uint32_t n); ///< uint32 len + bytes
void pc_sftp_wr_attrs(SftpWriter *w, const SftpAttrs *a);
/** @brief Backfill the length prefix (= off-4). @return the total packet length, or 0 on overflow. */
size_t pc_sftp_wr_finish(SftpWriter *w);
/** @brief Position where the next byte will be written (used to remember a patch point, e.g. a NAME count). */
size_t pc_sftp_wr_pos(const SftpWriter *w);
/** @brief Overwrite a big-endian uint32 already written at @p at (for backfilling a count). */
void pc_sftp_wr_patch_u32(SftpWriter *w, size_t at, uint32_t v);

// --- framing ---
/**
 * @brief The full length of the leading packet in @p buf (4-byte prefix + payload), or 0 if fewer than 4 bytes
 *        are present (need more) or the declared length exceeds @p max (caller drops the connection).
 */
size_t pc_sftp_frame_len(const uint8_t *buf, size_t have, size_t max);

// --- response builders (return the total packet length written, or 0 on overflow) ---
size_t pc_sftp_build_version(uint8_t *out, size_t cap);
size_t pc_sftp_build_status(uint32_t id, uint32_t code, const char *msg, uint8_t *out, size_t cap);
size_t pc_sftp_build_handle(uint32_t id, const void *handle, uint32_t hlen, uint8_t *out, size_t cap);
size_t pc_sftp_build_attrs(uint32_t id, const SftpAttrs *a, uint8_t *out, size_t cap);
/** @brief PC_SSH_FXP_DATA carrying @p data[0..dlen). */
size_t pc_sftp_build_data(uint32_t id, const void *data, uint32_t dlen, uint8_t *out, size_t cap);
/** @brief PC_SSH_FXP_NAME with one entry (filename + longname + attrs) - used by REALPATH. */
size_t pc_sftp_build_name1(uint32_t id, const char *name, const char *longname, const SftpAttrs *a, uint8_t *out,
                           size_t cap);

/**
 * @brief Format a Unix `ls -l`-style longname for a NAME entry, e.g. "-rw-r--r-- 1 0 0 1234 Jan  1 2026 name".
 *        @return the string length written (excluding NUL), clamped to @p cap-1.
 */
size_t pc_sftp_format_longname(proto_bool is_dir, uint32_t perms, uint64_t size, uint32_t mtime, const char *name,
                               char *out, size_t cap);

#endif // PC_ENABLE_SSH_SFTP

PROTO_END_DECLS

#endif // PROTOCORE_SFTP_H
