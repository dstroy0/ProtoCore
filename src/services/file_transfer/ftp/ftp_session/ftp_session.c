// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ftp_session.c
 * @brief Drive a real FTP control + data connection pair with the ftp.h codec (see ftp_session.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_FTP_SESSION

#include "mmgr/membuild/membuild.h"   // protocore_sb frame builder
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"
#include "services/file_transfer/ftp/ftp_session/ftp_session.h"

static uint8_t ftp_work[16]; // the borrow an entry takes; Ftp never reads it

#include "mmgr/protoframe/protoframe.h"                  // protocore_field / protocore_fval: the log frames
#include "mmgr/protostr/protostr.h"                      // str.copy: the bounded host copy
#include "network_drivers/transport/tcp/client/client.h" // TcpClient: both connections
#include "server/clock/clock.h"                          // Clock.ms: the reply deadline
#include "services/file_transfer/ftp/ftp/ftp.h"
#include "shared/log/log.h"

PROTOCORE_BEGIN_DECLS

/** @brief Owned session state. One transfer at a time; the buffers are too big for the stack. */
typedef struct
{
    int ctrl;                           ///< control-connection id, or -1
    int data;                           ///< data-connection id, or -1
    char rx[PROTOCORE_FTP_REPLY_BUF];   ///< control-reply accumulator
    size_t rx_len;                      ///< bytes held in rx
    size_t rx_consumed;                 ///< bytes of rx the last reply occupied, shifted out on the next read
    char cmd[PROTOCORE_FTP_CMD_MAX];    ///< command being built
    uint8_t chunk[PROTOCORE_FTP_CHUNK]; ///< payload staging
    uint32_t deadline;                  ///< Clock.ms the reply in flight gives up at
    uint8_t step;                       ///< which FtpStep the sequence is resuming at
    size_t off;                         ///< payload bytes already sent
} FtpSessionCtx;

/** @brief The reply the sequence is waiting on, and so where the next call resumes. */
typedef enum PROTO_ENUM_PACKED
{
    FTP_STEP_IDLE = 0, ///< nothing in flight
    FTP_STEP_GREETING, ///< the server's 220
    FTP_STEP_USER,     ///< the answer to USER
    FTP_STEP_PASS,     ///< the answer to PASS
    FTP_STEP_TYPE,     ///< the answer to TYPE I
    FTP_STEP_EPSV,     ///< the answer to EPSV
    FTP_STEP_PASV,     ///< the answer to PASV
    FTP_STEP_STOR,     ///< the preliminary 1xx
    FTP_STEP_STREAM,   ///< the payload is going out
    FTP_STEP_DONE,     ///< the completion 226
} FtpStep;
// Designated, so a member's position in the struct does not decide what it binds to.
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define FTP_SESSION_OFF_CTX 0u
static_assert(FTP_SESSION_OFF_CTX + sizeof(FtpSessionCtx) <= PROTOCORE_FTP_SESSION_BORROW,
              "PROTOCORE_FTP_SESSION_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    FTP_SESSION_OFF_CTX % _Alignof(FtpSessionCtx) == 0,
    "FTP_SESSION_OFF_CTX is not a multiple of alignof(FtpSessionCtx) - FTP_SESSION_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define FTP_SESSION_CTX(w) ((FtpSessionCtx *)(void *)((w) + FTP_SESSION_OFF_CTX))

// Log frames. Each is the shape of one message, fixed when the code was written, so the logger
// builds it without parsing anything and a level that is compiled out costs the spec nothing.
static const protocore_field LOG_BUILD_FAILED[] = {
    {PROTOCORE_FK_LIT, 0, 18, "ftp: cannot build "}, PROTOCORE_STR, PROTOCORE_END};
static const protocore_field LOG_SENT[] = {{PROTOCORE_FK_LIT, 0, 5, "ftp> "}, PROTOCORE_STR, PROTOCORE_END};
static const protocore_field LOG_REPLY[] = {{PROTOCORE_FK_LIT, 0, 5, "ftp< "}, PROTOCORE_U32, PROTOCORE_END};
static const protocore_field LOG_REPLY_TOO_BIG[] = {
    {PROTOCORE_FK_LIT, 0, 48, "ftp: reply larger than PROTOCORE_FTP_REPLY_BUF ("},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 1, ")"},
    PROTOCORE_END};
static const protocore_field LOG_CTRL_CLOSED[] = {{PROTOCORE_FK_LIT, 0, 25, "ftp: control closed with "},
                                                  PROTOCORE_U32,
                                                  {PROTOCORE_FK_LIT, 0, 15, " bytes buffered"},
                                                  PROTOCORE_END};
static const protocore_field LOG_REPLY_TIMEOUT[] = {{PROTOCORE_FK_LIT, 0, 20, "ftp: reply timeout, "},
                                                    PROTOCORE_U32,
                                                    {PROTOCORE_FK_LIT, 0, 15, " bytes buffered"},
                                                    PROTOCORE_END};
static const protocore_field LOG_DATA_CONNECT_FAILED[] = {{PROTOCORE_FK_LIT, 0, 21, "ftp: data connect to "},
                                                          PROTOCORE_STR,
                                                          {PROTOCORE_FK_LIT, 0, 1, ":"},
                                                          PROTOCORE_U32,
                                                          {PROTOCORE_FK_LIT, 0, 7, " failed"},
                                                          PROTOCORE_END};
static const protocore_field LOG_CTRL_CONNECT_FAILED[] = {{PROTOCORE_FK_LIT, 0, 24, "ftp: control connect to "},
                                                          PROTOCORE_STR,
                                                          {PROTOCORE_FK_LIT, 0, 1, ":"},
                                                          PROTOCORE_U32,
                                                          {PROTOCORE_FK_LIT, 0, 7, " failed"},
                                                          PROTOCORE_END};

// ---------------------------------------------------------------------------
// Control channel
// ---------------------------------------------------------------------------

/** @brief Send one command line on the control connection and arm its reply deadline. */
static proto_bool ftp_send(uint8_t *restrict work, const char *verb, const char *arg)
{
    Ftp.build_command_args.buf = FTP_SESSION_CTX(work)->cmd;
    Ftp.build_command_args.cap = sizeof(FTP_SESSION_CTX(work)->cmd);
    Ftp.build_command_args.verb = verb;
    Ftp.build_command_args.arg = arg;
    Ftp.build_command(ftp_work);
    size_t n = Ftp.n;
    if (n == 0)
    {
        PROTOCORE_LOGW(LOG_BUILD_FAILED, ((const protocore_fval[]){PROTOCORE_VSTR(verb)}), 1);
        return PROTO_FALSE;
    }
    PROTOCORE_LOGD(LOG_SENT, ((const protocore_fval[]){PROTOCORE_VSTR(verb)}), 1);
    TcpClientV.cid = FTP_SESSION_CTX(work)->ctrl;
    TcpClientV.io.data = (const uint8_t *)FTP_SESSION_CTX(work)->cmd;
    TcpClientV.io.len = n;
    TcpClient.send(protocore_tcp_client_span());
    FTP_SESSION_CTX(work)->deadline = Clock.ms + PROTOCORE_FTP_TIMEOUT_MS;
    return TcpClientV.ok;
}

/**
 * @brief Read until a complete reply is buffered.
 *
 * The reply text is left at the head of rx (length in @p rlen) so a caller can hand it straight to
 * protocore_ftp_parse_pasv / _epsv; it is shifted out at the start of the next call, which keeps any
 * bytes the server pipelined behind it.
 */
static protocore_ftp_state ftp_await(uint8_t *restrict work, int *code, size_t *rlen)
{
    if (FTP_SESSION_CTX(work)->rx_consumed > 0)
    {
        mem.move(FTP_SESSION_CTX(work)->rx, FTP_SESSION_CTX(work)->rx + FTP_SESSION_CTX(work)->rx_consumed,
                 FTP_SESSION_CTX(work)->rx_len - FTP_SESSION_CTX(work)->rx_consumed);
        FTP_SESSION_CTX(work)->rx_len -= FTP_SESSION_CTX(work)->rx_consumed;
        FTP_SESSION_CTX(work)->rx_consumed = 0;
    }

    // Take every byte the control socket holds now; a zero read is what says it has no more.
    for (;;)
    {
        const size_t room = sizeof(FTP_SESSION_CTX(work)->rx) - FTP_SESSION_CTX(work)->rx_len;
        if (room == 0)
        {
            break;
        }
        TcpClientV.cid = FTP_SESSION_CTX(work)->ctrl;
        TcpClientV.io.buf = (uint8_t *)FTP_SESSION_CTX(work)->rx + FTP_SESSION_CTX(work)->rx_len;
        TcpClientV.io.cap = room;
        TcpClient.read(protocore_tcp_client_span());
        if (TcpClientV.n == 0)
        {
            break;
        }
        FTP_SESSION_CTX(work)->rx_len += TcpClientV.n;
    }

    size_t consumed = 0;
    Ftp.parse_reply_args.buf = FTP_SESSION_CTX(work)->rx;
    Ftp.parse_reply_args.len = FTP_SESSION_CTX(work)->rx_len;
    Ftp.parse_reply_args.code = code;
    Ftp.parse_reply_args.consumed = &consumed;
    Ftp.parse_reply(ftp_work);
    if (Ftp.ok)
    {
        FTP_SESSION_CTX(work)->rx_consumed = consumed;
        if (rlen)
        {
            *rlen = consumed;
        }
        PROTOCORE_LOGD(LOG_REPLY, ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)*code)}), 1);
        return PROTOCORE_FTP_READY;
    }
    if (FTP_SESSION_CTX(work)->rx_len == sizeof(FTP_SESSION_CTX(work)->rx))
    {
        PROTOCORE_LOGW(LOG_REPLY_TOO_BIG,
                       ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)sizeof(FTP_SESSION_CTX(work)->rx))}), 1);
        return PROTOCORE_FTP_FAILED; // a reply that cannot fit is malformed, not incomplete
    }
    TcpClientV.cid = FTP_SESSION_CTX(work)->ctrl;
    TcpClient.is_closed(protocore_tcp_client_span());
    if (TcpClientV.ok)
    {
        TcpClientV.cid = FTP_SESSION_CTX(work)->ctrl;
        TcpClient.available(protocore_tcp_client_span());
        if (TcpClientV.n == 0)
        {
            PROTOCORE_LOGW(LOG_CTRL_CLOSED,
                           ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)FTP_SESSION_CTX(work)->rx_len)}), 1);
            return PROTOCORE_FTP_FAILED;
        }
    }
    // Clock.ms is monotonic, so the subtraction is wrap-safe across a rollover.
    if ((int32_t)(Clock.ms - FTP_SESSION_CTX(work)->deadline) >= 0)
    {
        PROTOCORE_LOGW(LOG_REPLY_TIMEOUT,
                       ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)FTP_SESSION_CTX(work)->rx_len)}), 1);
        return PROTOCORE_FTP_FAILED;
    }
    return PROTOCORE_FTP_BUSY;
}

/** @brief Connect the data connection the server named. */
static proto_bool ftp_data_connect(uint8_t *restrict work, const char *host, uint16_t port)
{
    if (port == 0)
    {
        return PROTO_FALSE;
    }
    TcpClientV.dial.host = host;
    TcpClientV.dial.port = port;
    TcpClientV.dial.timeout_ms = PROTOCORE_FTP_TIMEOUT_MS;
    TcpClient.open(protocore_tcp_client_span());
    FTP_SESSION_CTX(work)->data = TcpClientV.i32;
    if (FTP_SESSION_CTX(work)->data < 0)
    {
        PROTOCORE_LOGW(LOG_DATA_CONNECT_FAILED,
                       ((const protocore_fval[]){PROTOCORE_VSTR(host), PROTOCORE_VU32((uint32_t)port)}), 2);
    }
    return FTP_SESSION_CTX(work)->data >= 0;
}

/** @brief The dotted quad a PASV reply names, written into @p host. */
static void ftp_pasv_host(const uint8_t ip[4], char *host, size_t cap)
{
    protocore_sb sb_host = {host, cap, 0, PROTO_TRUE};
    Sb.u32(&sb_host, (uint32_t)((unsigned)ip[0]));
    Sb.put(&sb_host, ".");
    Sb.u32(&sb_host, (uint32_t)((unsigned)ip[1]));
    Sb.put(&sb_host, ".");
    Sb.u32(&sb_host, (uint32_t)((unsigned)ip[2]));
    Sb.put(&sb_host, ".");
    Sb.u32(&sb_host, (uint32_t)((unsigned)ip[3]));
    if (Sb.finish(&sb_host) == 0)
    {
        host[0] = '\0';
    }
}

// ---------------------------------------------------------------------------
// Data channel
// ---------------------------------------------------------------------------

/** @brief Drop both connections and reset the accumulator for the next transfer. */
static void ftp_teardown(uint8_t *restrict work)
{
    if (FTP_SESSION_CTX(work)->data >= 0)
    {
        TcpClientV.cid = FTP_SESSION_CTX(work)->data;
        TcpClient.close(protocore_tcp_client_span());
        FTP_SESSION_CTX(work)->data = -1;
    }
    if (FTP_SESSION_CTX(work)->ctrl >= 0)
    {
        TcpClientV.cid = FTP_SESSION_CTX(work)->ctrl;
        TcpClient.close(protocore_tcp_client_span());
        FTP_SESSION_CTX(work)->ctrl = -1;
    }
    FTP_SESSION_CTX(work)->rx_len = 0;
    FTP_SESSION_CTX(work)->rx_consumed = 0;
    FTP_SESSION_CTX(work)->off = 0;
    FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_IDLE;
}

// ---------------------------------------------------------------------------
// STOR
// ---------------------------------------------------------------------------

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_FTP_SESSION_BORROW persistent bytes
} FtpSessionOwnCtx;
static FtpSessionOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ftp_session_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_FTP_SESSION_BORROW).buf;
        // A borrow arrives zeroed, and these do not start at zero.
        FTP_SESSION_CTX(s_own.span)->ctrl = -1;
        FTP_SESSION_CTX(s_own.span)->data = -1;
    }
    return s_own.span;
}

void protocore_ftp_session_store(uint8_t *restrict work)
{
    const FtpTarget *target = FtpSessionV.store_args.target;
    const char *remote_path = FtpSessionV.store_args.remote_path;
    size_t total = FtpSessionV.store_args.total;
    protocore_ftp_source src = FtpSessionV.store_args.src;
    void *ctx = FtpSessionV.store_args.ctx;

    if (!target || !target->host || !remote_path || remote_path[0] == '\0' || !src)
    {
        FtpSessionV.value = PROTOCORE_FTP_FAILED;
        return;
    }

    if ((FtpStep)FTP_SESSION_CTX(work)->step == FTP_STEP_IDLE)
    {
        const uint16_t ctrl_port = target->port ? target->port : 21;
        FTP_SESSION_CTX(work)->rx_len = 0;
        FTP_SESSION_CTX(work)->rx_consumed = 0;
        FTP_SESSION_CTX(work)->off = 0;
        TcpClientV.dial.host = target->host;
        TcpClientV.dial.port = ctrl_port;
        TcpClientV.dial.timeout_ms = PROTOCORE_FTP_TIMEOUT_MS;
        TcpClient.open(protocore_tcp_client_span());
        FTP_SESSION_CTX(work)->ctrl = TcpClientV.i32;
        if (FTP_SESSION_CTX(work)->ctrl < 0)
        {
            PROTOCORE_LOGW(
                LOG_CTRL_CONNECT_FAILED,
                ((const protocore_fval[]){PROTOCORE_VSTR(target->host), PROTOCORE_VU32((uint32_t)ctrl_port)}), 2);
            FtpSessionV.value = PROTOCORE_FTP_FAILED;
            return;
        }
        FTP_SESSION_CTX(work)->deadline = Clock.ms + PROTOCORE_FTP_TIMEOUT_MS;
        FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_GREETING;
    }

    // Every arm either advances the sequence and continues, or reports what it is still waiting on.
    // A break leaves the switch for the failure tail.
    for (;;)
    {
        int code = 0;
        size_t rlen = 0;
        uint16_t port = 0;
        char host[48];
        protocore_ftp_state st;

        switch ((FtpStep)FTP_SESSION_CTX(work)->step)
        {
        case FTP_STEP_GREETING:
            st = ftp_await(work, &code, NULL);
            if (st == PROTOCORE_FTP_BUSY)
            {
                FtpSessionV.value = PROTOCORE_FTP_BUSY;
                return;
            }
            if (st == PROTOCORE_FTP_FAILED || code != 220)
            {
                break;
            }
            // USER answers 331 (password wanted) or 230 (already logged in, e.g. anonymous).
            if (!ftp_send(work, "USER", target->user ? target->user : "anonymous"))
            {
                break;
            }
            FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_USER;
            continue;

        case FTP_STEP_USER:
            st = ftp_await(work, &code, NULL);
            if (st == PROTOCORE_FTP_BUSY)
            {
                FtpSessionV.value = PROTOCORE_FTP_BUSY;
                return;
            }
            if (st == PROTOCORE_FTP_FAILED)
            {
                break;
            }
            if (code == 331)
            {
                if (!ftp_send(work, "PASS", target->pass ? target->pass : ""))
                {
                    break;
                }
                FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_PASS;
                continue;
            }
            if (!protocore_ftp_reply_ok(code))
            {
                break;
            }
            // Binary: ASCII mode would rewrite CRLF and corrupt a core dump or any other blob.
            if (!ftp_send(work, "TYPE", "I"))
            {
                break;
            }
            FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_TYPE;
            continue;

        case FTP_STEP_PASS:
            st = ftp_await(work, &code, NULL);
            if (st == PROTOCORE_FTP_BUSY)
            {
                FtpSessionV.value = PROTOCORE_FTP_BUSY;
                return;
            }
            if (st == PROTOCORE_FTP_FAILED || !protocore_ftp_reply_ok(code) || !ftp_send(work, "TYPE", "I"))
            {
                break;
            }
            FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_TYPE;
            continue;

        case FTP_STEP_TYPE:
            st = ftp_await(work, &code, NULL);
            if (st == PROTOCORE_FTP_BUSY)
            {
                FtpSessionV.value = PROTOCORE_FTP_BUSY;
                return;
            }
            // EPSV first (RFC 2428): it carries only a port, so it survives the NAT that makes
            // PASV's advertised address wrong.
            if (st == PROTOCORE_FTP_FAILED || !protocore_ftp_reply_ok(code) || !ftp_send(work, "EPSV", NULL))
            {
                break;
            }
            FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_EPSV;
            continue;

        case FTP_STEP_EPSV:
            st = ftp_await(work, &code, &rlen);
            if (st == PROTOCORE_FTP_BUSY)
            {
                FtpSessionV.value = PROTOCORE_FTP_BUSY;
                return;
            }
            if (st == PROTOCORE_FTP_FAILED)
            {
                break;
            }
            Ftp.ok = PROTO_FALSE;
            if (code == 229)
            {
                Ftp.parse_epsv_args.buf = FTP_SESSION_CTX(work)->rx;
                Ftp.parse_epsv_args.len = rlen;
                Ftp.parse_epsv_args.port = &port;
                Ftp.parse_epsv(ftp_work);
            }
            if (Ftp.ok)
            {
                // Extended passive mode reuses the control connection's host.
                str.copy(host, target->host, sizeof(host));
                if (!ftp_data_connect(work, host, port) || !ftp_send(work, "STOR", remote_path))
                {
                    break;
                }
                FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_STOR;
                continue;
            }
            // PASV is the fallback for servers that answer 500 to EPSV.
            if (!ftp_send(work, "PASV", NULL))
            {
                break;
            }
            FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_PASV;
            continue;

        case FTP_STEP_PASV: {
            uint8_t ip[4] = {0, 0, 0, 0};
            st = ftp_await(work, &code, &rlen);
            if (st == PROTOCORE_FTP_BUSY)
            {
                FtpSessionV.value = PROTOCORE_FTP_BUSY;
                return;
            }
            Ftp.ok = PROTO_FALSE;
            if (st != PROTOCORE_FTP_FAILED && code == 227)
            {
                Ftp.parse_pasv_args.buf = FTP_SESSION_CTX(work)->rx;
                Ftp.parse_pasv_args.len = rlen;
                Ftp.parse_pasv_args.ip = ip;
                Ftp.parse_pasv_args.port = &port;
                Ftp.parse_pasv(ftp_work);
            }
            if (!Ftp.ok)
            {
                break;
            }
            ftp_pasv_host(ip, host, sizeof(host));
            if (!ftp_data_connect(work, host, port) || !ftp_send(work, "STOR", remote_path))
            {
                break;
            }
            FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_STOR;
            continue;
        }

        case FTP_STEP_STOR:
            // The preliminary 1xx must arrive before any payload; a 5xx means the path was rejected.
            st = ftp_await(work, &code, NULL);
            if (st == PROTOCORE_FTP_BUSY)
            {
                FtpSessionV.value = PROTOCORE_FTP_BUSY;
                return;
            }
            if (st == PROTOCORE_FTP_FAILED || protocore_ftp_reply_class(code) != 1)
            {
                break;
            }
            FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_STREAM;
            continue;

        case FTP_STEP_STREAM: {
            proto_bool sent = PROTO_TRUE;
            while (FTP_SESSION_CTX(work)->off < total)
            {
                const size_t want = (total - FTP_SESSION_CTX(work)->off < sizeof(FTP_SESSION_CTX(work)->chunk))
                                        ? total - FTP_SESSION_CTX(work)->off
                                        : sizeof(FTP_SESSION_CTX(work)->chunk);
                const size_t got = src(ctx, FTP_SESSION_CTX(work)->off, FTP_SESSION_CTX(work)->chunk, want);
                TcpClientV.cid = FTP_SESSION_CTX(work)->data;
                TcpClientV.io.data = FTP_SESSION_CTX(work)->chunk;
                TcpClientV.io.len = got;
                TcpClient.send(protocore_tcp_client_span());
                if (got != want || !TcpClientV.ok)
                {
                    sent = PROTO_FALSE;
                    break;
                }
                FTP_SESSION_CTX(work)->off += got;
            }
            // Closing the data connection is what marks end-of-file for a STOR, so it happens
            // before the completion reply is read - and on failure too, so the server stops waiting.
            TcpClientV.cid = FTP_SESSION_CTX(work)->data;
            TcpClient.close(protocore_tcp_client_span());
            FTP_SESSION_CTX(work)->data = -1;
            if (!sent)
            {
                break;
            }
            FTP_SESSION_CTX(work)->deadline = Clock.ms + PROTOCORE_FTP_TIMEOUT_MS;
            FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_DONE;
            continue;
        }

        case FTP_STEP_DONE:
            st = ftp_await(work, &code, NULL);
            if (st == PROTOCORE_FTP_BUSY)
            {
                FtpSessionV.value = PROTOCORE_FTP_BUSY;
                return;
            }
            if (st == PROTOCORE_FTP_FAILED || code != 226)
            {
                break;
            }
            ftp_send(work, "QUIT", NULL); // best effort; the transfer is already decided
            ftp_teardown(work);
            FtpSessionV.value = PROTOCORE_FTP_READY;
            return;

        case FTP_STEP_IDLE:
        default:
            break;
        }

        ftp_send(work, "QUIT", NULL); // best effort; the transfer is already decided
        ftp_teardown(work);
        FtpSessionV.value = PROTOCORE_FTP_FAILED;
        return;
    }
}

/** @brief The operands and the outcome. */
FtpSessionVars FtpSessionV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FTP_SESSION
