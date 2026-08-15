// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ftp_session.h
 * @brief FTP client session driver: the two sockets the ftp.h codec deliberately does not own.
 *
 * ftp.h is pure bytes-on-the-wire. This is the other half: it drives a real control connection
 * (`protocore_client_*`) through the RFC 959 login -> TYPE I -> passive-mode -> transfer -> QUIT
 * sequence, opens the second (data) connection the server names, and streams a payload across it.
 *
 * The payload is **pulled**, not pushed: the caller supplies a `protocore_ftp_source` that fills a chunk at
 * a given offset. So the bytes can come from anywhere - a file, a sensor log, or the core-dump
 * partition (`protocore_exc_coredump_read`) - without this owner knowing about any of them, and nothing
 * ever has to fit in RAM at once.
 *
 * Non-blocking: a call advances the sequence as far as the sockets allow, then reports
 * ::PROTOCORE_FTP_BUSY. A reply is bounded by PROTOCORE_FTP_TIMEOUT_MS.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FTP_SESSION_H
#define PROTOCORE_FTP_SESSION_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_FTP_SESSION

/** @brief Where a transfer stands. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_FTP_READY = 0, ///< the server confirmed the completed transfer
    PROTOCORE_FTP_BUSY,      ///< the sockets have no more to give; ask again on the next tick
    PROTOCORE_FTP_FAILED,    ///< the sequence broke, or a reply passed its deadline
} protocore_ftp_state;

/** @brief Where to connect and who to log in as. */
typedef struct
{
    const char *host; ///< server hostname or dotted-quad
    uint16_t port;    ///< control port, or 0 for the default 21
    const char *user; ///< username, or nullptr for "anonymous"
    const char *pass; ///< password, or nullptr for "" (anonymous)
} FtpTarget;

/**
 * @brief Fill up to @p cap bytes of the payload starting at @p offset.
 *
 * Called repeatedly with ascending offsets until the declared total is sent. Returning fewer than
 * @p cap bytes ends the transfer early and fails it, so a source that cannot satisfy a chunk should
 * return 0 rather than pad.
 *
 * @return bytes written into @p buf.
 */
typedef size_t (*protocore_ftp_source)(void *ctx, size_t offset, uint8_t *buf, size_t cap);

/**
 * @brief Upload @p total bytes pulled from @p src to @p remote_path (RFC 959 STOR).
 *
 * Logs in, switches to binary (TYPE I), asks for a passive data port (EPSV, falling back to PASV
 * for servers that predate RFC 2428), connects, streams the payload, then confirms the server's
 * 226 transfer-complete before reporting success - a socket that merely accepted the bytes is not
 * treated as a stored file.
 *
 * The first call starts the session; each one after it resumes where the last stopped, so the
 * arguments must name the same transfer until it reports something other than ::PROTOCORE_FTP_BUSY.
 *
 * @return ::PROTOCORE_FTP_READY only if the server confirmed the completed transfer.
 */
protocore_ftp_state protocore_ftp_store(const FtpTarget *target, const char *remote_path, size_t total,
                                        protocore_ftp_source src, void *ctx);

#endif // PROTOCORE_ENABLE_FTP_SESSION

PROTOCORE_END_DECLS

#endif // PROTOCORE_FTP_SESSION_H
