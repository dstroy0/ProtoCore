// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ftp_session.c
 * @brief Drive a real FTP control + data connection pair with the ftp.h codec (see ftp_session.h).
 */

#include "services/file_transfer/ftp/ftp_session.h"
#include "mmgr/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_FTP_SESSION

#include "mmgr/protoframe.h"                             // protocore_field / protocore_fval: the log frames
#include "mmgr/protostr.h"                               // str.copy: the bounded host copy
#include "network_drivers/transport/tcp/client/client.h" // TcpClient: both connections
#include "server/clock/clock.h"                          // Clock.ms: the reply deadline
#include "services/file_transfer/ftp/ftp.h"
#include "shared/log/log.h"

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
static FtpSessionCtx s_ftp = {.ctrl = -1, .data = -1};

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
static proto_bool ftp_send(const char *verb, const char *arg)
{
    size_t n = protocore_ftp_build_command(s_ftp.cmd, sizeof(s_ftp.cmd), verb, arg);
    if (n == 0)
    {
        PROTOCORE_LOGW(LOG_BUILD_FAILED, ((const protocore_fval[]){PROTOCORE_VSTR(verb)}), 1);
        return PROTO_FALSE;
    }
    PROTOCORE_LOGD(LOG_SENT, ((const protocore_fval[]){PROTOCORE_VSTR(verb)}), 1);
    TcpClient.cid = s_ftp.ctrl;
    TcpClient.io.data = (const uint8_t *)s_ftp.cmd;
    TcpClient.io.len = n;
    TcpClient.send(TcpClient.internal);
    s_ftp.deadline = Clock.ms + PROTOCORE_FTP_TIMEOUT_MS;
    return TcpClient.ok;
}

/**
 * @brief Read until a complete reply is buffered.
 *
 * The reply text is left at the head of rx (length in @p rlen) so a caller can hand it straight to
 * protocore_ftp_parse_pasv / _epsv; it is shifted out at the start of the next call, which keeps any
 * bytes the server pipelined behind it.
 */
static protocore_ftp_state ftp_await(int *code, size_t *rlen)
{
    if (s_ftp.rx_consumed > 0)
    {
        mem.move(s_ftp.rx, s_ftp.rx + s_ftp.rx_consumed, s_ftp.rx_len - s_ftp.rx_consumed);
        s_ftp.rx_len -= s_ftp.rx_consumed;
        s_ftp.rx_consumed = 0;
    }

    // Take every byte the control socket holds now; a zero read is what says it has no more.
    for (;;)
    {
        const size_t room = sizeof(s_ftp.rx) - s_ftp.rx_len;
        if (room == 0)
        {
            break;
        }
        TcpClient.cid = s_ftp.ctrl;
        TcpClient.io.buf = (uint8_t *)s_ftp.rx + s_ftp.rx_len;
        TcpClient.io.cap = room;
        TcpClient.read(TcpClient.internal);
        if (TcpClient.n == 0)
        {
            break;
        }
        s_ftp.rx_len += TcpClient.n;
    }

    size_t consumed = 0;
    if (protocore_ftp_parse_reply(s_ftp.rx, s_ftp.rx_len, code, &consumed))
    {
        s_ftp.rx_consumed = consumed;
        if (rlen)
        {
            *rlen = consumed;
        }
        PROTOCORE_LOGD(LOG_REPLY, ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)*code)}), 1);
        return PROTOCORE_FTP_READY;
    }
    if (s_ftp.rx_len == sizeof(s_ftp.rx))
    {
        PROTOCORE_LOGW(LOG_REPLY_TOO_BIG, ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)sizeof(s_ftp.rx))}), 1);
        return PROTOCORE_FTP_FAILED; // a reply that cannot fit is malformed, not incomplete
    }
    TcpClient.cid = s_ftp.ctrl;
    TcpClient.is_closed(TcpClient.internal);
    if (TcpClient.ok)
    {
        TcpClient.cid = s_ftp.ctrl;
        TcpClient.available(TcpClient.internal);
        if (TcpClient.n == 0)
        {
            PROTOCORE_LOGW(LOG_CTRL_CLOSED, ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)s_ftp.rx_len)}), 1);
            return PROTOCORE_FTP_FAILED;
        }
    }
    // Clock.ms is monotonic, so the subtraction is wrap-safe across a rollover.
    if ((int32_t)(Clock.ms - s_ftp.deadline) >= 0)
    {
        PROTOCORE_LOGW(LOG_REPLY_TIMEOUT, ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)s_ftp.rx_len)}), 1);
        return PROTOCORE_FTP_FAILED;
    }
    return PROTOCORE_FTP_BUSY;
}

/** @brief Connect the data connection the server named. */
static proto_bool ftp_data_connect(const char *host, uint16_t port)
{
    if (port == 0)
    {
        return PROTO_FALSE;
    }
    TcpClient.dial.host = host;
    TcpClient.dial.port = port;
    TcpClient.dial.timeout_ms = PROTOCORE_FTP_TIMEOUT_MS;
    TcpClient.open(TcpClient.internal);
    s_ftp.data = TcpClient.i32;
    if (s_ftp.data < 0)
    {
        PROTOCORE_LOGW(LOG_DATA_CONNECT_FAILED,
                       ((const protocore_fval[]){PROTOCORE_VSTR(host), PROTOCORE_VU32((uint32_t)port)}), 2);
    }
    return s_ftp.data >= 0;
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
static void ftp_teardown(void)
{
    if (s_ftp.data >= 0)
    {
        TcpClient.cid = s_ftp.data;
        TcpClient.close(TcpClient.internal);
        s_ftp.data = -1;
    }
    if (s_ftp.ctrl >= 0)
    {
        TcpClient.cid = s_ftp.ctrl;
        TcpClient.close(TcpClient.internal);
        s_ftp.ctrl = -1;
    }
    s_ftp.rx_len = 0;
    s_ftp.rx_consumed = 0;
    s_ftp.off = 0;
    s_ftp.step = (uint8_t)FTP_STEP_IDLE;
}

// ---------------------------------------------------------------------------
// STOR
// ---------------------------------------------------------------------------

protocore_ftp_state protocore_ftp_store(const FtpTarget *target, const char *remote_path, size_t total,
                                        protocore_ftp_source src, void *ctx)
{
    if (!target || !target->host || !remote_path || remote_path[0] == '\0' || !src)
    {
        return PROTOCORE_FTP_FAILED;
    }

    if ((FtpStep)s_ftp.step == FTP_STEP_IDLE)
    {
        const uint16_t ctrl_port = target->port ? target->port : 21;
        s_ftp.rx_len = 0;
        s_ftp.rx_consumed = 0;
        s_ftp.off = 0;
        TcpClient.dial.host = target->host;
        TcpClient.dial.port = ctrl_port;
        TcpClient.dial.timeout_ms = PROTOCORE_FTP_TIMEOUT_MS;
        TcpClient.open(TcpClient.internal);
        s_ftp.ctrl = TcpClient.i32;
        if (s_ftp.ctrl < 0)
        {
            PROTOCORE_LOGW(
                LOG_CTRL_CONNECT_FAILED,
                ((const protocore_fval[]){PROTOCORE_VSTR(target->host), PROTOCORE_VU32((uint32_t)ctrl_port)}), 2);
            return PROTOCORE_FTP_FAILED;
        }
        s_ftp.deadline = Clock.ms + PROTOCORE_FTP_TIMEOUT_MS;
        s_ftp.step = (uint8_t)FTP_STEP_GREETING;
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

        switch ((FtpStep)s_ftp.step)
        {
        case FTP_STEP_GREETING:
            st = ftp_await(&code, NULL);
            if (st == PROTOCORE_FTP_BUSY)
            {
                return PROTOCORE_FTP_BUSY;
            }
            if (st == PROTOCORE_FTP_FAILED || code != 220)
            {
                break;
            }
            // USER answers 331 (password wanted) or 230 (already logged in, e.g. anonymous).
            if (!ftp_send("USER", target->user ? target->user : "anonymous"))
            {
                break;
            }
            s_ftp.step = (uint8_t)FTP_STEP_USER;
            continue;

        case FTP_STEP_USER:
            st = ftp_await(&code, NULL);
            if (st == PROTOCORE_FTP_BUSY)
            {
                return PROTOCORE_FTP_BUSY;
            }
            if (st == PROTOCORE_FTP_FAILED)
            {
                break;
            }
            if (code == 331)
            {
                if (!ftp_send("PASS", target->pass ? target->pass : ""))
                {
                    break;
                }
                s_ftp.step = (uint8_t)FTP_STEP_PASS;
                continue;
            }
            if (!protocore_ftp_reply_ok(code))
            {
                break;
            }
            // Binary: ASCII mode would rewrite CRLF and corrupt a core dump or any other blob.
            if (!ftp_send("TYPE", "I"))
            {
                break;
            }
            s_ftp.step = (uint8_t)FTP_STEP_TYPE;
            continue;

        case FTP_STEP_PASS:
            st = ftp_await(&code, NULL);
            if (st == PROTOCORE_FTP_BUSY)
            {
                return PROTOCORE_FTP_BUSY;
            }
            if (st == PROTOCORE_FTP_FAILED || !protocore_ftp_reply_ok(code) || !ftp_send("TYPE", "I"))
            {
                break;
            }
            s_ftp.step = (uint8_t)FTP_STEP_TYPE;
            continue;

        case FTP_STEP_TYPE:
            st = ftp_await(&code, NULL);
            if (st == PROTOCORE_FTP_BUSY)
            {
                return PROTOCORE_FTP_BUSY;
            }
            // EPSV first (RFC 2428): it carries only a port, so it survives the NAT that makes
            // PASV's advertised address wrong.
            if (st == PROTOCORE_FTP_FAILED || !protocore_ftp_reply_ok(code) || !ftp_send("EPSV", NULL))
            {
                break;
            }
            s_ftp.step = (uint8_t)FTP_STEP_EPSV;
            continue;

        case FTP_STEP_EPSV:
            st = ftp_await(&code, &rlen);
            if (st == PROTOCORE_FTP_BUSY)
            {
                return PROTOCORE_FTP_BUSY;
            }
            if (st == PROTOCORE_FTP_FAILED)
            {
                break;
            }
            if (code == 229 && protocore_ftp_parse_epsv(s_ftp.rx, rlen, &port))
            {
                // Extended passive mode reuses the control connection's host.
                str.copy(host, target->host, sizeof(host));
                if (!ftp_data_connect(host, port) || !ftp_send("STOR", remote_path))
                {
                    break;
                }
                s_ftp.step = (uint8_t)FTP_STEP_STOR;
                continue;
            }
            // PASV is the fallback for servers that answer 500 to EPSV.
            if (!ftp_send("PASV", NULL))
            {
                break;
            }
            s_ftp.step = (uint8_t)FTP_STEP_PASV;
            continue;

        case FTP_STEP_PASV: {
            uint8_t ip[4] = {0, 0, 0, 0};
            st = ftp_await(&code, &rlen);
            if (st == PROTOCORE_FTP_BUSY)
            {
                return PROTOCORE_FTP_BUSY;
            }
            if (st == PROTOCORE_FTP_FAILED || code != 227 || !protocore_ftp_parse_pasv(s_ftp.rx, rlen, ip, &port))
            {
                break;
            }
            ftp_pasv_host(ip, host, sizeof(host));
            if (!ftp_data_connect(host, port) || !ftp_send("STOR", remote_path))
            {
                break;
            }
            s_ftp.step = (uint8_t)FTP_STEP_STOR;
            continue;
        }

        case FTP_STEP_STOR:
            // The preliminary 1xx must arrive before any payload; a 5xx means the path was rejected.
            st = ftp_await(&code, NULL);
            if (st == PROTOCORE_FTP_BUSY)
            {
                return PROTOCORE_FTP_BUSY;
            }
            if (st == PROTOCORE_FTP_FAILED || protocore_ftp_reply_class(code) != 1)
            {
                break;
            }
            s_ftp.step = (uint8_t)FTP_STEP_STREAM;
            continue;

        case FTP_STEP_STREAM: {
            proto_bool sent = PROTO_TRUE;
            while (s_ftp.off < total)
            {
                const size_t want = (total - s_ftp.off < sizeof(s_ftp.chunk)) ? total - s_ftp.off : sizeof(s_ftp.chunk);
                const size_t got = src(ctx, s_ftp.off, s_ftp.chunk, want);
                TcpClient.cid = s_ftp.data;
                TcpClient.io.data = s_ftp.chunk;
                TcpClient.io.len = got;
                TcpClient.send(TcpClient.internal);
                if (got != want || !TcpClient.ok)
                {
                    sent = PROTO_FALSE;
                    break;
                }
                s_ftp.off += got;
            }
            // Closing the data connection is what marks end-of-file for a STOR, so it happens
            // before the completion reply is read - and on failure too, so the server stops waiting.
            TcpClient.cid = s_ftp.data;
            TcpClient.close(TcpClient.internal);
            s_ftp.data = -1;
            if (!sent)
            {
                break;
            }
            s_ftp.deadline = Clock.ms + PROTOCORE_FTP_TIMEOUT_MS;
            s_ftp.step = (uint8_t)FTP_STEP_DONE;
            continue;
        }

        case FTP_STEP_DONE:
            st = ftp_await(&code, NULL);
            if (st == PROTOCORE_FTP_BUSY)
            {
                return PROTOCORE_FTP_BUSY;
            }
            if (st == PROTOCORE_FTP_FAILED || code != 226)
            {
                break;
            }
            ftp_send("QUIT", NULL); // best effort; the transfer is already decided
            ftp_teardown();
            return PROTOCORE_FTP_READY;

        case FTP_STEP_IDLE:
        default:
            break;
        }

        ftp_send("QUIT", NULL); // best effort; the transfer is already decided
        ftp_teardown();
        return PROTOCORE_FTP_FAILED;
    }
}

#endif // PROTOCORE_ENABLE_FTP_SESSION
