// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file proto_builtins.c
 * @brief Installs the built-in protocol handlers.
 *
 * Each built-in exposes a `*_proto_handler()` accessor in its own module; this calls
 * proto_register() for each one behind the matching feature flag.
 *
 * PROTO_SSH_RFWD self-registers at runtime from pc_ssh_forward_begin().
 */

#include "network_drivers/session/proto_handler.h"

#include "network_drivers/presentation/presentation.h" // http_proto_handler()
#if PC_ENABLE_TELNET
#include "network_drivers/presentation/telnet/telnet.h"
#endif
#if PC_ENABLE_SSH
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#endif
#if PC_NEED_MODBUS
#include "services/fieldbus/modbus/modbus.h"
#endif
#if PC_ENABLE_OPCUA
#include "services/fieldbus/opcua/opcua.h"
#endif

// Registers @p h for @p proto when the module supplied one; modbus / opcua return NULL on host builds.
static inline void register_if(ConnProto proto, const ProtoHandler *h)
{
    if (h != NULL)
    {
        proto_register(proto, h);
    }
}

void proto_register_builtins(void)
{
    register_if(PROTO_HTTP, http_proto_handler()); // always present
#if PC_ENABLE_TELNET
    register_if(PROTO_TELNET, pc_telnet_proto_handler());
#endif
#if PC_ENABLE_SSH
    register_if(PROTO_SSH, ssh_proto_handler());
#endif
#if PC_NEED_MODBUS
    register_if(PROTO_MODBUS, pc_modbus_proto_handler());
#endif
#if PC_ENABLE_OPCUA
    register_if(PROTO_OPCUA, pc_opcua_proto_handler());
#endif
}
