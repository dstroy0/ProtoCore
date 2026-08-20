# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/file_transfer/ftp/ftp_session
  GATE PROTOCORE_ENABLE_FTP_SESSION
  SOURCES ftp_session.c
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/plaintext
    mmgr/protoframe
    mmgr/protomem
    mmgr/protostr
    network_drivers/transport/tcp/client
    server/clock
    services/file_transfer/ftp/ftp
    shared/log
)
