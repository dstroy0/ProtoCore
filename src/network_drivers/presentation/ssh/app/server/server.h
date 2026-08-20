// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file server.h
 * @brief The file-transfer services RFC 4254 sec 6.5 carries, server side.
 *
 * SFTP (draft-ietf-secsh-filexfer) and SCP (rcp) are not components of SSH; they are programs the
 * connection protocol starts through a "subsystem" or "exec" request. The classifier that decides a
 * request names one of them is declared by the connection layer it hooks into
 * (ssh_classify_file_transfer_request, connection.h) and implemented here, so the dependency runs
 * upward only.
 */

#ifndef PROTOCORE_APP_SERVER_H
#define PROTOCORE_APP_SERVER_H

#include "network_drivers/presentation/ssh/connection/connection.h"

PROTOCORE_BEGIN_DECLS

/** @brief RFC 4254 sec 5.4 CHANNEL_REQUEST: the type it names, and the body that follows. */
typedef struct
{
    const uint8_t *rtype;   ///< the request type
    uint32_t rtype_len;     ///< its length
    const uint8_t *payload; ///< the request body
    size_t len;             ///< how many bytes it has
    size_t off;             ///< in/out: the request-specific argument, advanced past what was read
} SshChanReqArgs;

/**
 * @brief The file-transfer classifier: whether a channel request names SFTP or SCP.
 *
 * A caller sets the members the call takes, invokes it through ::SshAppServer, and reads the outcome
 * off the same handle.
 *
 * @var SshAppServerNs::slot       the SSH slot the request arrived on
 * @var SshAppServerNs::channel    the channel it names
 * @var SshAppServerNs::req        what one channel request names and carries (RFC 4254 sec 5.4)
 * @var SshAppServerNs::accept     in/out: set when an accepted SFTP subsystem flips acceptance
 * @var SshAppServerNs::classify   tag the channel and fire the matching open handler
 */
typedef struct
{
    uint8_t slot;      ///< the SSH slot the request arrived on
    uint32_t channel;  ///< the channel it names
    proto_bool accept; ///< in/out: set when an accepted SFTP subsystem flips acceptance

    SshChanReqArgs req; ///< what one channel request names and carries

    void (*const classify)(uint8_t *restrict work);
} SshAppServerNs;

/** @brief The one symbol this module exports. */
extern SshAppServerNs SshAppServer;

PROTOCORE_END_DECLS

#endif // PROTOCORE_APP_SERVER_H
