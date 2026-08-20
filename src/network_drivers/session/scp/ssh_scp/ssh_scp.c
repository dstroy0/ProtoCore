// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_scp.c
 * @brief SCP server - the rcp SINK state machine. See ssh_scp.h.
 *
 * Drives the rcp SINK protocol over an SSH `exec "scp -t <path>"` channel: send a ready ack, read the client's
 * `C<mode> <size> <name>` control line, ack it, stream <size> data bytes straight to the file, read the trailing
 * end-of-record byte, and send the final ack. One file per transfer (no -r); the SOURCE direction (`scp -f`)
 * replies "use sftp get".
 *
 * The destination reaches storage as the two pieces it already is - the `-t` target and, when that target is a
 * directory, the control line's filename - so the accessor frames the whole path once. Resolving it here would
 * put a root, a path buffer, its capacity, and a copy of the `..` guard into a protocol server.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SSH_SCP

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "network_drivers/session/scp/ssh_scp/ssh_scp.h"

static uint8_t scp_work[16]; // the borrow an entry takes; Scp never reads it

#include "mmgr/protostr/protostr.h" // str: the bounded-run walks
#include "network_drivers/presentation/ssh/connection/connection.h"
#include "network_drivers/presentation/ssh/network/network.h"
#include "network_drivers/session/scp/scp/scp.h"
#include "network_drivers/session/session.h" // scp_conns: the transfer the connection carries
#include "server/storage/filesystem/filesystem.h"

PROTOCORE_BEGIN_DECLS

// All SCP state in one owner with internal linkage, the work buffer included: a stack array is the
// one allocation the fixed-footprint accounting cannot see, and the buffer does not outlive the
// callback that fills it, so one serves every slot.
typedef struct
{
    proto_bool registered;
    // The root this server works through, bound once in begin(). A handle, not a path: this file
    // cannot name where storage begins.
    int root;
    char leaf[PROTOCORE_FILESYSTEM_PATH_MAX]; ///< one control line's filename, live only until the open
} SshScpCtx;

// -1 until bound, not the 0 static storage would give: root 0 is a valid root, so a zeroed field
// would resolve against somebody else's storage before protocore_ssh_scp_begin() ran.
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SSH_SCP_OFF_CTX 0u
static_assert(SSH_SCP_OFF_CTX + sizeof(SshScpCtx) <= PROTOCORE_SSH_SCP_BORROW,
              "PROTOCORE_SSH_SCP_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(SSH_SCP_OFF_CTX % _Alignof(SshScpCtx) == 0,
              "SSH_SCP_OFF_CTX is not a multiple of alignof(SshScpCtx) - SSH_SCP_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define SSH_SCP_CTX(w) ((SshScpCtx *)(void *)((w) + SSH_SCP_OFF_CTX))

// An error ack is a status byte, a message, and a terminator, and all three are fixed - so each one
// is a single rodata string, sent as it stands. Staging one in a buffer would copy a constant and
// then scan the copy for a length the compiler already had.
//
// The status byte is spelled as its own literal so the concatenation boundary is explicit: written
// as one string, "\x02c" would be read as the single byte 0x2C, because a hex escape consumes every
// hex digit that follows it and 'c' is one.
static const char SCP_ERR_NO_SOURCE[] = "\x02"
                                        "scp download not supported; use sftp get\n";
static const char SCP_ERR_BAD_CMD[] = "\x02"
                                      "unsupported scp command\n";
static const char SCP_ERR_BAD_RECORD[] = "\x02"
                                         "unsupported scp record\n";
static const char SCP_ERR_CREATE[] = "\x02"
                                     "cannot create file\n";
static const char SCP_ERR_WRITE[] = "\x02"
                                    "write error\n";

static void protocore_scp_end(ScpConn *c); // called above its definition; static hides the header's

// A refused send is a peer that is gone, and a gone peer sends nothing back: end the transfer
// rather than writing further records into a channel nobody reads.
static void ack(ScpConn *c, uint8_t byte)
{
    SshConnectionV.chan.slot = c->slot;
    SshConnectionV.chan.channel = c->channel;
    SshConnectionV.chan.data = &byte;
    SshConnectionV.chan.len = 1;
    SshConnection.channel_send_data(protocore_ssh_connection_span());
    if (SshConnectionV.i32 < 0)
    {
        protocore_scp_end(c);
    }
}
/** @brief Send one complete error record. @p len is `sizeof(record) - 1`, resolved at compile time. */
static void err_ack(ScpConn *c, const char *rec, size_t len)
{
    SshConnectionV.chan.slot = c->slot;
    SshConnectionV.chan.channel = c->channel;
    SshConnectionV.chan.data = (const uint8_t *)(rec);
    SshConnectionV.chan.len = len;
    SshConnection.channel_send_data(protocore_ssh_connection_span());
    if (SshConnectionV.i32 < 0)
    {
        protocore_scp_end(c);
    }
}
static void close_file(ScpConn *c)
{
    if (c->fh >= 0)
    {
        Fs.io.handle = c->fh;
        Fs.close(protocore_filesystem_span());
        c->fh = -1;
    }
}
static void protocore_scp_end(ScpConn *c)
{
    close_file(c);
    c->active = PROTO_FALSE;
    SshConnectionV.chan.slot = c->slot;
    SshConnectionV.chan.channel = c->channel;
    SshConnection.channel_send_close(protocore_ssh_connection_span());
}

static void protocore_scp_on_open(uint8_t slot, uint32_t channel, const char *cmd, size_t cmd_len)
{
    if (slot >= MAX_SSH_CONNS)
    {
        return;
    }
    ScpConn *c = &scp_conns[slot];
    close_file(c);
    c->active = PROTO_TRUE;
    c->slot = slot;
    c->channel = channel;
    c->err = PROTO_FALSE;
    c->cl_len = 0;

    // Parsed straight into the field that keeps it. Parsing into scratch and copying would move a
    // whole path twice and then rescan the copy for a length the parse already walked past.
    Scp.parse_cmd_args.cmd = cmd;
    Scp.parse_cmd_args.cmd_len = cmd_len;
    Scp.parse_cmd_args.path_out = c->dest;
    Scp.parse_cmd_args.path_cap = sizeof(c->dest);
    Scp.parse_cmd(scp_work);
    ScpMode mode = Scp.value;
    if (mode == SCP_MODE_SINK)
    {
        // A '/' terminator is what makes the target a directory, and it is also the separator the
        // accessor's join relies on, so the flag and the string agree without either being rebuilt.
        size_t pl = str.len(c->dest, sizeof(c->dest));
        c->dest_is_dir = (pl > 0 && c->dest[pl - 1] == '/');
        c->st = WAIT_CLINE;
        ack(c, PROTOCORE_SCP_ACK_OK); // ready for the control line
    }
    else if (mode == SCP_MODE_SOURCE)
    {
        err_ack(c, SCP_ERR_NO_SOURCE, sizeof(SCP_ERR_NO_SOURCE) - 1);
        protocore_scp_end(c);
    }
    else
    {
        err_ack(c, SCP_ERR_BAD_CMD, sizeof(SCP_ERR_BAD_CMD) - 1);
        protocore_scp_end(c);
    }
}

static void protocore_scp_on_data(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_ssh_scp_span();

    if (slot >= MAX_SSH_CONNS)
    {
        return;
    }
    ScpConn *c = &scp_conns[slot];
    if (!c->active || c->channel != channel)
    {
        return;
    }

    while (len > 0)
    {
        if (c->st == WAIT_CLINE)
        {
            proto_bool complete = PROTO_FALSE;
            while (len > 0)
            {
                char ch = (char)data[0];
                data++;
                len--;
                if (ch == '\n')
                {
                    complete = PROTO_TRUE;
                    break;
                }
                if (c->cl_len < sizeof(c->cl) - 1)
                {
                    c->cl[c->cl_len++] = ch;
                }
            }
            if (!complete)
            {
                return; // the whole control line has not arrived yet
            }
            c->cl[c->cl_len] = '\0';

            uint32_t mode = 0;
            uint64_t size = 0;
            Scp.parse_cline_args.line = c->cl;
            Scp.parse_cline_args.len = c->cl_len;
            Scp.parse_cline_args.mode_out = &mode;
            Scp.parse_cline_args.size_out = &size;
            Scp.parse_cline_args.name_out = SSH_SCP_CTX(work)->leaf;
            Scp.parse_cline_args.name_cap = sizeof(SSH_SCP_CTX(work)->leaf);
            Scp.parse_cline(scp_work);
            if (!Scp.ok)
            {
                // e.g. a D/E directory record (no -r support)
                err_ack(c, SCP_ERR_BAD_RECORD, sizeof(SCP_ERR_BAD_RECORD) - 1);
                protocore_scp_end(c);
                return;
            }
            // A directory target takes the control line's filename; a file target is the whole
            // destination on its own. Either way the accessor gets the pieces and frames once.
            Fs.path.root = SSH_SCP_CTX(work)->root;
            Fs.path.dir = c->dest;
            Fs.path.name = c->dest_is_dir ? SSH_SCP_CTX(work)->leaf : "";
            Fs.io.mode = PROTOCORE_MNT_WRITE;
            Fs.open(protocore_filesystem_span());
            c->fh = Fs.i32;
            if (c->fh < 0)
            {
                err_ack(c, SCP_ERR_CREATE, sizeof(SCP_ERR_CREATE) - 1);
                protocore_scp_end(c);
                return;
            }
            c->remaining = size;
            c->st = (size == 0) ? WAIT_END : RECV;
            c->cl_len = 0;
            ack(c, PROTOCORE_SCP_ACK_OK); // proceed with the data
            continue;
        }
        if (c->st == RECV)
        {
            size_t take = (len < c->remaining) ? len : (size_t)c->remaining;
            Fs.io.handle = c->fh;
            Fs.io.wbuf = data;
            Fs.io.n = take;
            Fs.write(protocore_filesystem_span());
            if (Fs.i32 != (int)take)
            {
                c->err = PROTO_TRUE;
            }
            data += take;
            len -= take;
            c->remaining -= take;
            if (c->remaining == 0)
            {
                c->st = WAIT_END;
            }
            continue;
        }
        if (c->st == WAIT_END)
        {
            data++; // consume the end-of-record byte (0)
            len--;
            close_file(c);
            if (c->err)
            {
                err_ack(c, SCP_ERR_WRITE, sizeof(SCP_ERR_WRITE) - 1);
            }
            else
            {
                ack(c, PROTOCORE_SCP_ACK_OK);
            }
            protocore_scp_end(c);
            return;
        }
        return; // PROTOCORE_NONE / unexpected
    }
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SSH_SCP_BORROW persistent bytes
} SshScpOwnCtx;
static SshScpOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ssh_scp_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_SSH_SCP_BORROW).buf;
        // A borrow arrives zeroed, and these do not start at zero.
        SSH_SCP_CTX(s_own.span)->root = -1;
    }
    return s_own.span;
}

void protocore_ssh_scp_begin(uint8_t *restrict work)
{
    // Bind the root this server answers from. Naming a different one than SFTP is how the two end up
    // over different storage; naming the same one shares it and costs one entry.
    Fs.mount = "mnt/scp";
    Fs.begin(protocore_filesystem_span());
    SSH_SCP_CTX(work)->root = Fs.i32;

    for (int i = 0; i < MAX_SSH_CONNS; i++)
    {
        scp_conns[i].active = PROTO_FALSE;
        // BSS zeroes this table, and 0 is a valid handle, so the free marker is set rather than
        // assumed - closing handle 0 here would take a file another server had open.
        scp_conns[i].fh = -1;
    }
    if (!SSH_SCP_CTX(work)->registered)
    {
        SshConnectionV.scp_open_cb = protocore_scp_on_open;
        SshConnection.set_scp_open_cb(protocore_ssh_connection_span());
        SshConnectionV.scp_data_cb = protocore_scp_on_data;
        SshConnection.set_scp_data_cb(protocore_ssh_connection_span());
        SSH_SCP_CTX(work)->registered = PROTO_TRUE;
    }
}

/** @brief The operands and the outcome. */
SshScpVars SshScpV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_SCP
