// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ftp_session.c
 * @brief Drive a real FTP control + data connection pair with the ftp.h codec (see ftp_session.h).
 */

#include "services/file_transfer/ftp/ftp_session.h"
#include "mmgr/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_FTP_SESSION

#include "network_drivers/transport/tcp/tcp.h"
#include "server/clock/clock.h" // protocore_millis, pcdelay
#include "services/file_transfer/ftp/ftp.h"
#include "shared/log/log.h"
#include <stdio.h>

/** @brief Owned session state. One transfer at a time; the buffers are too big for the stack. */
typedef struct
{
    int ctrl;                    ///< control-connection id, or -1
    int data;                    ///< data-connection id, or -1
    char rx[PROTOCORE_FTP_REPLY_BUF];   ///< control-reply accumulator
    size_t rx_len;               ///< bytes held in rx
    size_t rx_consumed;          ///< bytes of rx the last reply occupied, shifted out on the next read
    char cmd[PROTOCORE_FTP_CMD_MAX];    ///< command being built
    uint8_t chunk[PROTOCORE_FTP_CHUNK]; ///< payload staging
} FtpSessionCtx;
static FtpSessionCtx s_ftp = {-1, -1, {0}, 0, 0, {0}, {0}};

// Log frames. Each is the shape of one message, fixed when the code was written, so the logger
// builds it without parsing anything and a level that is compiled out costs the spec nothing.
static const protocore_field LOG_BUILD_FAILED[] = {{PROTOCORE_FK_LIT, 0, 18, "ftp: cannot build "}, PROTOCORE_STR, PROTOCORE_END};
static const protocore_field LOG_SENT[] = {{PROTOCORE_FK_LIT, 0, 5, "ftp> "}, PROTOCORE_STR, PROTOCORE_END};
static const protocore_field LOG_REPLY[] = {{PROTOCORE_FK_LIT, 0, 5, "ftp< "}, PROTOCORE_U32, PROTOCORE_END};
static const protocore_field LOG_REPLY_TOO_BIG[] = {
    {PROTOCORE_FK_LIT, 0, 41, "ftp: reply larger than PROTOCORE_FTP_REPLY_BUF ("}, PROTOCORE_U32, {PROTOCORE_FK_LIT, 0, 1, ")"}, PROTOCORE_END};
static const protocore_field LOG_CTRL_CLOSED[] = {
    {PROTOCORE_FK_LIT, 0, 25, "ftp: control closed with "}, PROTOCORE_U32, {PROTOCORE_FK_LIT, 0, 15, " bytes buffered"}, PROTOCORE_END};
static const protocore_field LOG_REPLY_TIMEOUT[] = {
    {PROTOCORE_FK_LIT, 0, 20, "ftp: reply timeout, "}, PROTOCORE_U32, {PROTOCORE_FK_LIT, 0, 15, " bytes buffered"}, PROTOCORE_END};
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

/** @brief Send one command line on the control connection. */
static proto_bool ftp_send(const char *verb, const char *arg)
{
    size_t n = protocore_ftp_build_command(s_ftp.cmd, sizeof(s_ftp.cmd), verb, arg);
    if (n == 0)
    {
        PROTOCORE_LOGW(LOG_BUILD_FAILED, ((const protocore_fval[]){PROTOCORE_VSTR(verb)}), 1);
        return PROTO_FALSE;
    }
    PROTOCORE_LOGD(LOG_SENT, ((const protocore_fval[]){PROTOCORE_VSTR(verb)}), 1);
    return Tcp.client->send(s_ftp.ctrl, s_ftp.cmd, n);
}

/**
 * @brief Read until a complete reply is buffered.
 *
 * The reply text is left at the head of rx (length in @p rlen) so a caller can hand it straight to
 * protocore_ftp_parse_pasv / _epsv; it is shifted out at the start of the next call, which keeps any
 * bytes the server pipelined behind it.
 */
static proto_bool ftp_await(int *code, size_t *rlen)
{
    if (s_ftp.rx_consumed > 0)
    {
        mem.move(s_ftp.rx, s_ftp.rx + s_ftp.rx_consumed, s_ftp.rx_len - s_ftp.rx_consumed);
        s_ftp.rx_len -= s_ftp.rx_consumed;
        s_ftp.rx_consumed = 0;
    }

    uint32_t deadline = protocore_millis() + PROTOCORE_FTP_TIMEOUT_MS;
    for (;;)
    {
        size_t consumed = 0;
        if (protocore_ftp_parse_reply(s_ftp.rx, s_ftp.rx_len, code, &consumed))
        {
            s_ftp.rx_consumed = consumed;
            if (rlen)
            {
                *rlen = consumed;
            }
            PROTOCORE_LOGD(LOG_REPLY, ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)*code)}), 1);
            return PROTO_TRUE;
        }
        if (s_ftp.rx_len == sizeof(s_ftp.rx))
        {
            PROTOCORE_LOGW(LOG_REPLY_TOO_BIG, ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)sizeof(s_ftp.rx))}), 1);
            return PROTO_FALSE; // a reply that cannot fit is malformed, not incomplete
        }
        if (Tcp.client->is_closed(s_ftp.ctrl) && Tcp.client->available(s_ftp.ctrl) == 0)
        {
            PROTOCORE_LOGW(LOG_CTRL_CLOSED, ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)s_ftp.rx_len)}), 1);
            return PROTO_FALSE;
        }
        // protocore_millis is monotonic, so the subtraction is wrap-safe across a rollover.
        if ((int32_t)(protocore_millis() - deadline) >= 0)
        {
            PROTOCORE_LOGW(LOG_REPLY_TIMEOUT, ((const protocore_fval[]){PROTOCORE_VU32((uint32_t)s_ftp.rx_len)}), 1);
            return PROTO_FALSE;
        }

        size_t got = Tcp.client->read(s_ftp.ctrl, (uint8_t *)s_ftp.rx + s_ftp.rx_len, sizeof(s_ftp.rx) - s_ftp.rx_len);
        if (got == 0)
        {
            pcdelay(5);
        }
        else
        {
            s_ftp.rx_len += got;
        }
    }
}

/** @brief Send a command and require a 2xx completion. */
static proto_bool ftp_cmd_ok(const char *verb, const char *arg)
{
    int code = 0;
    return ftp_send(verb, arg) && ftp_await(&code, NULL) && protocore_ftp_reply_ok(code);
}

// ---------------------------------------------------------------------------
// Data channel
// ---------------------------------------------------------------------------

/**
 * @brief Ask for a passive data port and connect to it.
 *
 * EPSV first (RFC 2428): it carries only a port, so it survives the NAT that makes PASV's
 * advertised address wrong. PASV is the fallback for servers that answer 500 to EPSV.
 */
static proto_bool ftp_open_data(const FtpTarget *target)
{
    int code = 0;
    size_t rlen = 0;
    uint16_t port = 0;
    char host[48];

    if (ftp_send("EPSV", NULL) && ftp_await(&code, &rlen) && code == 229 && protocore_ftp_parse_epsv(s_ftp.rx, rlen, &port))
    {
        // Extended passive mode reuses the control connection's host.
        strncpy(host, target->host, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }
    else
    {
        uint8_t ip[4] = {0, 0, 0, 0};
        if (!ftp_send("PASV", NULL) || !ftp_await(&code, &rlen) || code != 227 ||
            !protocore_ftp_parse_pasv(s_ftp.rx, rlen, ip, &port))
        {
            return PROTO_FALSE;
        }
        protocore_sb sb_host = {host, sizeof(host), 0, PROTO_TRUE};
        protocore_sb_u32(&sb_host, (uint32_t)((unsigned)ip[0]));
        protocore_sb_put(&sb_host, ".");
        protocore_sb_u32(&sb_host, (uint32_t)((unsigned)ip[1]));
        protocore_sb_put(&sb_host, ".");
        protocore_sb_u32(&sb_host, (uint32_t)((unsigned)ip[2]));
        protocore_sb_put(&sb_host, ".");
        protocore_sb_u32(&sb_host, (uint32_t)((unsigned)ip[3]));
        if (protocore_sb_finish(&sb_host) == 0)
        {
            host[0] = '\0';
        }
    }

    if (port == 0)
    {
        return PROTO_FALSE;
    }
    s_ftp.data = Tcp.client->open(host, port, PROTOCORE_FTP_TIMEOUT_MS);
    if (s_ftp.data < 0)
    {
        PROTOCORE_LOGW(LOG_DATA_CONNECT_FAILED, ((const protocore_fval[]){PROTOCORE_VSTR(host), PROTOCORE_VU32((uint32_t)port)}), 2);
    }
    return s_ftp.data >= 0;
}

/** @brief Drop both connections and reset the accumulator for the next transfer. */
static void ftp_teardown(void)
{
    if (s_ftp.data >= 0)
    {
        Tcp.client->close(s_ftp.data);
        s_ftp.data = -1;
    }
    if (s_ftp.ctrl >= 0)
    {
        Tcp.client->close(s_ftp.ctrl);
        s_ftp.ctrl = -1;
    }
    s_ftp.rx_len = 0;
    s_ftp.rx_consumed = 0;
}

// ---------------------------------------------------------------------------
// STOR
// ---------------------------------------------------------------------------

proto_bool protocore_ftp_store(const FtpTarget *target, const char *remote_path, size_t total, protocore_ftp_source src, void *ctx)
{
    if (!target || !target->host || !remote_path || remote_path[0] == '\0' || !src)
    {
        return PROTO_FALSE;
    }
    if (s_ftp.ctrl >= 0)
    {
        return PROTO_FALSE; // one transfer at a time
    }

    uint16_t ctrl_port = target->port ? target->port : 21;
    s_ftp.rx_len = 0;
    s_ftp.rx_consumed = 0;
    s_ftp.ctrl = Tcp.client->open(target->host, ctrl_port, PROTOCORE_FTP_TIMEOUT_MS);
    if (s_ftp.ctrl < 0)
    {
        PROTOCORE_LOGW(LOG_CTRL_CONNECT_FAILED, ((const protocore_fval[]){PROTOCORE_VSTR(target->host), PROTOCORE_VU32((uint32_t)ctrl_port)}), 2);
        return PROTO_FALSE;
    }

    int code = 0;
    if (!ftp_await(&code, NULL) || code != 220) // server greeting
    {
        ftp_teardown();
        return PROTO_FALSE;
    }

    // USER answers 331 (password wanted) or 230 (already logged in, e.g. anonymous).
    if (!ftp_send("USER", target->user ? target->user : "anonymous") || !ftp_await(&code, NULL))
    {
        ftp_teardown();
        return PROTO_FALSE;
    }
    if (code == 331)
    {
        if (!ftp_send("PASS", target->pass ? target->pass : "") || !ftp_await(&code, NULL))
        {
            ftp_teardown();
            return PROTO_FALSE;
        }
    }
    if (!protocore_ftp_reply_ok(code))
    {
        ftp_teardown();
        return PROTO_FALSE;
    }

    // Binary: ASCII mode would rewrite CRLF and corrupt a core dump or any other blob.
    if (!ftp_cmd_ok("TYPE", "I") || !ftp_open_data(target))
    {
        ftp_teardown();
        return PROTO_FALSE;
    }

    // The preliminary 1xx must arrive before any payload; a 5xx here means the path was rejected.
    if (!ftp_send("STOR", remote_path) || !ftp_await(&code, NULL) || protocore_ftp_reply_class(code) != 1)
    {
        ftp_teardown();
        return PROTO_FALSE;
    }

    proto_bool ok = PROTO_TRUE;
    size_t off = 0;
    while (off < total)
    {
        size_t want = (total - off < sizeof(s_ftp.chunk)) ? total - off : sizeof(s_ftp.chunk);
        size_t got = src(ctx, off, s_ftp.chunk, want);
        if (got != want || !Tcp.client->send(s_ftp.data, s_ftp.chunk, got))
        {
            ok = PROTO_FALSE;
            break;
        }
        off += got;
    }

    // Closing the data connection is what marks end-of-file for a STOR, so it happens before the
    // completion reply is read - and it happens even on failure, so the server stops waiting.
    Tcp.client->close(s_ftp.data);
    s_ftp.data = -1;

    if (ok)
    {
        ok = ftp_await(&code, NULL) && code == 226;
    }

    ftp_send("QUIT", NULL); // best effort; the transfer is already decided
    ftp_teardown();
    return ok;
}

#endif // PROTOCORE_ENABLE_FTP_SESSION
