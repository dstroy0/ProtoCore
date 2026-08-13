// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#endif // PROTOCORE_APP_SERVER_H
