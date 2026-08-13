// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file server.c
 * @brief RFC 4254 sec 6: the subsystem and exec requests that name a file-transfer service.
 */

#include "network_drivers/presentation/ssh/app/server.h"
#include "mmgr/bytes.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_SSH_SFTP || PROTOCORE_ENABLE_SSH_SCP
// A subsystem/exec CHANNEL_REQUEST may name a file-transfer service (SFTP or SCP). Tag @p c and fire the
// matching open callback when it does; @p off points at the request-specific arg and may be advanced. Flips
// *accept true for an accepted SFTP subsystem (exec is already in the base accept set).
void ssh_classify_file_transfer_request(uint8_t i, uint32_t channel, const uint8_t *rtype, uint32_t rtype_len,
                                           const uint8_t *payload, size_t len, size_t *off, proto_bool *accept)
{
#if !PROTOCORE_ENABLE_SSH_SFTP
    (void)accept; // only the SFTP subsystem path flips acceptance; scp exec is already accepted
#endif
#if PROTOCORE_ENABLE_SSH_SFTP
    // subsystem "sftp": not in the base accept set, so accept it here and tag the channel for the SFTP binding.
    if (rtype_len == 9 && mem.cmp(rtype, "subsystem", 9) == 0)
    {
        const uint8_t *arg = NULL;
        uint32_t arg_len = 0;
        if (protocore_rd_str(payload, len, off, &arg, &arg_len) && arg_len == 4 && mem.cmp(arg, "sftp", 4) == 0)
        {
            *accept = PROTO_TRUE;
            protocore_ssh_channel_bind_service(i, channel, SSH_CHAN_SERVICE_SFTP);
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
        if (protocore_rd_str(payload, len, off, &arg, &arg_len) && arg_len >= 4 && mem.cmp(arg, "scp ", 4) == 0)
        {
            protocore_ssh_channel_bind_service(i, channel, SSH_CHAN_SERVICE_SCP);
            SshScpOpenCb open_cb = protocore_ssh_channel_scp_open_cb();
            if (open_cb)
            {
                open_cb(i, channel, (const char *)arg, arg_len);
            }
        }
    }
#endif
}
#endif
