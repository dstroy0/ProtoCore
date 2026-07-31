// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ftp_session.cpp
 * @brief Drive a real FTP control + data connection pair with the ftp.h codec (see ftp_session.h).
 */

#include "services/file_transfer/ftp/ftp_session.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder

#if PC_ENABLE_FTP_SESSION

#include "network_drivers/transport/client.h"
#include "services/file_transfer/ftp/ftp.h"
#include "services/system/clock.h" // pc_millis, pcdelay
#include "shared_primitives/log.h"
#include <stdio.h>
#include <string.h>

/** @brief Owned session state. One transfer at a time; the buffers are too big for the stack. */
struct FtpSessionCtx
{
    int ctrl;                    ///< control-connection id, or -1
    int data;                    ///< data-connection id, or -1
    char rx[PC_FTP_REPLY_BUF];   ///< control-reply accumulator
    size_t rx_len;               ///< bytes held in rx
    size_t rx_consumed;          ///< bytes of rx the last reply occupied, shifted out on the next read
    char cmd[PC_FTP_CMD_MAX];    ///< command being built
    uint8_t chunk[PC_FTP_CHUNK]; ///< payload staging
};
static FtpSessionCtx s_ftp = {-1, -1, {0}, 0, 0, {0}, {0}};

// ---------------------------------------------------------------------------
// Control channel
// ---------------------------------------------------------------------------

/** @brief Send one command line on the control connection. */
static bool ftp_send(const char *verb, const char *arg)
{
    size_t n = pc_ftp_build_command(s_ftp.cmd, sizeof(s_ftp.cmd), verb, arg);
    if (n == 0)
    {
        PC_LOGW("ftp: cannot build %s", verb);
        return false;
    }
    PC_LOGD("ftp> %s", verb);
    return pc_client_send(s_ftp.ctrl, s_ftp.cmd, n);
}

/**
 * @brief Read until a complete reply is buffered.
 *
 * The reply text is left at the head of rx (length in @p rlen) so a caller can hand it straight to
 * pc_ftp_parse_pasv / _epsv; it is shifted out at the start of the next call, which keeps any
 * bytes the server pipelined behind it.
 */
static bool ftp_await(int *code, size_t *rlen)
{
    if (s_ftp.rx_consumed > 0)
    {
        memmove(s_ftp.rx, s_ftp.rx + s_ftp.rx_consumed, s_ftp.rx_len - s_ftp.rx_consumed);
        s_ftp.rx_len -= s_ftp.rx_consumed;
        s_ftp.rx_consumed = 0;
    }

    uint32_t deadline = pc_millis() + PC_FTP_TIMEOUT_MS;
    for (;;)
    {
        size_t consumed = 0;
        if (pc_ftp_parse_reply(s_ftp.rx, s_ftp.rx_len, code, &consumed))
        {
            s_ftp.rx_consumed = consumed;
            if (rlen)
            {
                *rlen = consumed;
            }
            PC_LOGD("ftp< %d", *code);
            return true;
        }
        if (s_ftp.rx_len == sizeof(s_ftp.rx))
        {
            PC_LOGW("ftp: reply larger than PC_FTP_REPLY_BUF (%u)", (unsigned)sizeof(s_ftp.rx));
            return false; // a reply that cannot fit is malformed, not incomplete
        }
        if (pc_client_is_closed(s_ftp.ctrl) && pc_client_available(s_ftp.ctrl) == 0)
        {
            PC_LOGW("ftp: control closed with %u bytes buffered", (unsigned)s_ftp.rx_len);
            return false;
        }
        // pc_millis is monotonic, so the subtraction is wrap-safe across a rollover.
        if ((int32_t)(pc_millis() - deadline) >= 0)
        {
            PC_LOGW("ftp: reply timeout, %u bytes buffered", (unsigned)s_ftp.rx_len);
            return false;
        }

        size_t got = pc_client_read(s_ftp.ctrl, (uint8_t *)s_ftp.rx + s_ftp.rx_len, sizeof(s_ftp.rx) - s_ftp.rx_len);
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
static bool ftp_cmd_ok(const char *verb, const char *arg)
{
    int code = 0;
    return ftp_send(verb, arg) && ftp_await(&code, nullptr) && pc_ftp_reply_ok(code);
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
static bool ftp_open_data(const FtpTarget *target)
{
    int code = 0;
    size_t rlen = 0;
    uint16_t port = 0;
    char host[48];

    if (ftp_send("EPSV", nullptr) && ftp_await(&code, &rlen) && code == 229 && pc_ftp_parse_epsv(s_ftp.rx, rlen, &port))
    {
        // Extended passive mode reuses the control connection's host.
        strncpy(host, target->host, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }
    else
    {
        uint8_t ip[4] = {0, 0, 0, 0};
        if (!ftp_send("PASV", nullptr) || !ftp_await(&code, &rlen) || code != 227 ||
            !pc_ftp_parse_pasv(s_ftp.rx, rlen, ip, &port))
        {
            return false;
        }
        pc_sb sb_host = {host, sizeof(host), 0, true};
        pc_sb_u32(&sb_host, (uint32_t)((unsigned)ip[0]));
        pc_sb_put(&sb_host, ".");
        pc_sb_u32(&sb_host, (uint32_t)((unsigned)ip[1]));
        pc_sb_put(&sb_host, ".");
        pc_sb_u32(&sb_host, (uint32_t)((unsigned)ip[2]));
        pc_sb_put(&sb_host, ".");
        pc_sb_u32(&sb_host, (uint32_t)((unsigned)ip[3]));
        if (pc_sb_finish(&sb_host) == 0)
        {
            host[0] = '\0';
        }
    }

    if (port == 0)
    {
        return false;
    }
    s_ftp.data = pc_client_open(host, port, PC_FTP_TIMEOUT_MS);
    if (s_ftp.data < 0)
    {
        PC_LOGW("ftp: data connect to %s:%u failed", host, (unsigned)port);
    }
    return s_ftp.data >= 0;
}

/** @brief Drop both connections and reset the accumulator for the next transfer. */
static void ftp_teardown(void)
{
    if (s_ftp.data >= 0)
    {
        pc_client_close(s_ftp.data);
        s_ftp.data = -1;
    }
    if (s_ftp.ctrl >= 0)
    {
        pc_client_close(s_ftp.ctrl);
        s_ftp.ctrl = -1;
    }
    s_ftp.rx_len = 0;
    s_ftp.rx_consumed = 0;
}

// ---------------------------------------------------------------------------
// STOR
// ---------------------------------------------------------------------------

bool pc_ftp_store(const FtpTarget *target, const char *remote_path, size_t total, pc_ftp_source src, void *ctx)
{
    if (!target || !target->host || !remote_path || remote_path[0] == '\0' || !src)
    {
        return false;
    }
    if (s_ftp.ctrl >= 0)
    {
        return false; // one transfer at a time
    }

    uint16_t ctrl_port = target->port ? target->port : 21;
    s_ftp.rx_len = 0;
    s_ftp.rx_consumed = 0;
    s_ftp.ctrl = pc_client_open(target->host, ctrl_port, PC_FTP_TIMEOUT_MS);
    if (s_ftp.ctrl < 0)
    {
        PC_LOGW("ftp: control connect to %s:%u failed", target->host, (unsigned)ctrl_port);
        return false;
    }

    int code = 0;
    if (!ftp_await(&code, nullptr) || code != 220) // server greeting
    {
        ftp_teardown();
        return false;
    }

    // USER answers 331 (password wanted) or 230 (already logged in, e.g. anonymous).
    if (!ftp_send("USER", target->user ? target->user : "anonymous") || !ftp_await(&code, nullptr))
    {
        ftp_teardown();
        return false;
    }
    if (code == 331)
    {
        if (!ftp_send("PASS", target->pass ? target->pass : "") || !ftp_await(&code, nullptr))
        {
            ftp_teardown();
            return false;
        }
    }
    if (!pc_ftp_reply_ok(code))
    {
        ftp_teardown();
        return false;
    }

    // Binary: ASCII mode would rewrite CRLF and corrupt a core dump or any other blob.
    if (!ftp_cmd_ok("TYPE", "I") || !ftp_open_data(target))
    {
        ftp_teardown();
        return false;
    }

    // The preliminary 1xx must arrive before any payload; a 5xx here means the path was rejected.
    if (!ftp_send("STOR", remote_path) || !ftp_await(&code, nullptr) || pc_ftp_reply_class(code) != 1)
    {
        ftp_teardown();
        return false;
    }

    bool ok = true;
    size_t off = 0;
    while (off < total)
    {
        size_t want = (total - off < sizeof(s_ftp.chunk)) ? total - off : sizeof(s_ftp.chunk);
        size_t got = src(ctx, off, s_ftp.chunk, want);
        if (got != want || !pc_client_send(s_ftp.data, s_ftp.chunk, got))
        {
            ok = false;
            break;
        }
        off += got;
    }

    // Closing the data connection is what marks end-of-file for a STOR, so it happens before the
    // completion reply is read - and it happens even on failure, so the server stops waiting.
    pc_client_close(s_ftp.data);
    s_ftp.data = -1;

    if (ok)
    {
        ok = ftp_await(&code, nullptr) && code == 226;
    }

    ftp_send("QUIT", nullptr); // best effort; the transfer is already decided
    ftp_teardown();
    return ok;
}

#endif // PC_ENABLE_FTP_SESSION
