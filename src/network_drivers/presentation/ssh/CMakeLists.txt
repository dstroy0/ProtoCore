# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/ssh
  GATE PROTOCORE_ENABLE_SSH
  SOURCES ssh.c
  PRIVATE_DEPS
    mmgr/secure
    network_drivers/presentation/ssh/common
)

add_subdirectory(app)
add_subdirectory(auth)
add_subdirectory(client)
add_subdirectory(common)
add_subdirectory(connection)
add_subdirectory(network)
add_subdirectory(server)
add_subdirectory(transport)


