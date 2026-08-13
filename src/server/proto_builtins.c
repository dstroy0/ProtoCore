// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file proto_builtins.c
 * @brief Installs the built-in protocol handlers.
 *
 * Each built-in exposes a `*_protocore_handler()` accessor in its own module; this calls
 * Protocols.add() for each one behind the matching feature flag.
 */

#include "network_drivers/session/proto_handler.h"

#include "network_drivers/presentation/presentation.h" // http_protocore_handler()
#if PROTOCORE_ENABLE_TELNET
#include "network_drivers/presentation/telnet/telnet.h"
#endif
#if PROTOCORE_ENABLE_SSH
#include "network_drivers/presentation/ssh/server/server.h"
#endif
#if PROTOCORE_NEED_MODBUS
#include "services/fieldbus/modbus/modbus.h"
#endif
#if PROTOCORE_ENABLE_OPCUA
#include "services/fieldbus/opcua/opcua.h"
#endif

// Registers @p h for @p proto when the module supplied one; modbus / opcua return NULL on host builds.
static inline void register_if(ProtoConn proto, const ProtoHandler *h)
{
    if (h != NULL)
    {
        Protocols.add(proto, h);
    }
}

void proto_register_builtins(void)
{
    register_if(PROTO_HTTP, http_protocore_handler()); // always present
#if PROTOCORE_ENABLE_TELNET
    register_if(PROTO_TELNET, Telnet.proto_handler());
#endif
#if PROTOCORE_ENABLE_SSH
    register_if(PROTO_SSH, ssh_protocore_handler());
#if PROTOCORE_SSH_PORT_FORWARD
    register_if(PROTO_SSH_RFWD, ssh_protocore_rfwd_handler());
#endif
#endif
#if PROTOCORE_NEED_MODBUS
    register_if(PROTO_MODBUS, protocore_modbus_protocore_handler());
#endif
#if PROTOCORE_ENABLE_OPCUA
    register_if(PROTO_OPCUA, protocore_opcua_protocore_handler());
#endif
}
