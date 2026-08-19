// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_FTP_SESSION

PROTOCORE_BEGIN_DECLS

// PROTOCORE_FTP_SESSION_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

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

/** @brief What store takes: target, remote_path, total, ... */
typedef struct
{
    const FtpTarget *target;
    const char *remote_path;
    size_t total;
    protocore_ftp_source src;
    void *ctx;
} FtpSessionStoreArgs;

/**
 * @brief FTP client session driver: the two sockets the ftp.h codec deliberately does not own. ftp.h is pure
 * bytes-on-the-wire.
 *
 * A caller sets the members a call takes, invokes it through ::FtpSession with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   FtpSession.store_args.target = ...;
 *   FtpSession.store_args.remote_path = ...;
 *   FtpSession.store_args.total = ...;
 *   FtpSession.store_args.src = ...;
 *   FtpSession.store_args.ctx = ...;
 *   FtpSession.store(work);
 *   // FtpSession.value is what the call reports
 *
 * @var FtpSessionNs::store_args  what store takes: target, remote_path, total,
 * @var FtpSessionNs::ok  a call's true/false outcome
 * @var FtpSessionNs::value  ::PROTOCORE_FTP_READY only if the server confirmed the completed ...
 * @var FtpSessionNs::store  upload total bytes pulled from src to remote_path (RFC 959 STOR). ...
 *
 * @c work is PROTOCORE_FTP_SESSION_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    FtpSessionStoreArgs store_args;

    proto_bool ok;
    protocore_ftp_state value;

    void (*const store)(uint8_t *restrict work);
} FtpSessionNs;

/** @brief The one symbol this module exports. */
extern FtpSessionNs FtpSession;

/**
 * @brief The PROTOCORE_FTP_SESSION_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_ftp_session_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FTP_SESSION

#endif // PROTOCORE_FTP_SESSION_H
