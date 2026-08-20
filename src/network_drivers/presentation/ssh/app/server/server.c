// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file server.c
 * @brief RFC 4254 sec 6: the subsystem and exec requests that name a file-transfer service.
 */

#include "network_drivers/presentation/ssh/app/server/server.h"
#include "mmgr/bytes/bytes.h"
#include "mmgr/protomem/protomem.h"

#if PROTOCORE_ENABLE_SSH_SFTP || PROTOCORE_ENABLE_SSH_SCP
// A subsystem/exec CHANNEL_REQUEST may name a file-transfer service (SFTP or SCP). Tag @p c and fire the
// matching open callback when it does; @p off points at the request-specific arg and may be advanced. Flips
// *accept true for an accepted SFTP subsystem (exec is already in the base accept set).

void protocore_ssh_app_server_classify(uint8_t *restrict work)
{
    (void)work;
    const uint8_t i = SshAppServerV.slot;
    const uint32_t channel = SshAppServerV.channel;
    const uint8_t *rtype = SshAppServerV.req.rtype;
    const uint32_t rtype_len = SshAppServerV.req.rtype_len;
    const uint8_t *payload = SshAppServerV.req.payload;
    const size_t len = SshAppServerV.req.len;
    size_t *off = &SshAppServerV.req.off;
    proto_bool *accept = &SshAppServerV.accept;
#if !PROTOCORE_ENABLE_SSH_SFTP
    (void)accept; // only the SFTP subsystem path flips acceptance; scp exec is already accepted
#endif
#if PROTOCORE_ENABLE_SSH_SFTP
    // subsystem "sftp": not in the base accept set, so accept it here and tag the channel for the SFTP binding.
    if (rtype_len == 9 && mem.cmp(rtype, "subsystem", 9) == 0)
    {
        const uint8_t *arg = NULL;
        uint32_t arg_len = 0;
        if (bytes.rd_str(payload, len, off, &arg, &arg_len) && arg_len == 4 && mem.cmp(arg, "sftp", 4) == 0)
        {
            *accept = PROTO_TRUE;
            SshConnectionV.chan.slot = i;
            SshConnectionV.chan.channel = channel;
            SshConnectionV.chan.service = SSH_CHAN_SERVICE_SFTP;
            SshConnection.channel_bind_service(protocore_ssh_connection_span());
            SshSftpOpenCb open_cb = protocore_ssh_channel_sftp_open_cb();
            if (open_cb)
            {
                open_cb(i, channel);
            }
        }
    }
#endif
#if PROTOCORE_ENABLE_SSH_SCP
    // exec "scp …": already accepted (exec is in the base set); tag the channel + hand the command to the binding.
    if (rtype_len == 4 && mem.cmp(rtype, "exec", 4) == 0)
    {
        const uint8_t *arg = NULL;
        uint32_t arg_len = 0;
        if (bytes.rd_str(payload, len, off, &arg, &arg_len) && arg_len >= 4 && mem.cmp(arg, "scp ", 4) == 0)
        {
            SshConnectionV.chan.slot = i;
            SshConnectionV.chan.channel = channel;
            SshConnectionV.chan.service = SSH_CHAN_SERVICE_SCP;
            SshConnection.channel_bind_service(protocore_ssh_connection_span());
            SshScpOpenCb open_cb = protocore_ssh_channel_scp_open_cb();
            if (open_cb)
            {
                open_cb(i, channel, (const char *)arg, arg_len);
            }
        }
    }
#endif
}

#else

void protocore_ssh_app_server_classify(uint8_t *restrict work)
{
    (void)work;
}

#endif

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
SshAppServerVars SshAppServerV;
