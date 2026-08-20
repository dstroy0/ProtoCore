// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_builtins.c
 * @brief Installs the built-in protocol handlers.
 *
 * Each built-in exposes a `*_protocore_handler()` accessor in its own module; this calls
 * Protocols.add() for each one behind the matching feature flag.
 */

#include "server/core/proto_handler.h"

#include "network_drivers/presentation/presentation.h" // HttpConn: the HTTP handler this installs
#if PROTOCORE_ENABLE_TELNET
#include "network_drivers/presentation/telnet/telnet.h"
#endif
#if PROTOCORE_ENABLE_SSH
#include "network_drivers/presentation/ssh/server/server.h"
#endif
#if PROTOCORE_NEED_MODBUS
#include "services/fieldbus/modbus/modbus/modbus.h"
#endif
#if PROTOCORE_ENABLE_OPCUA
#include "services/opcua/opcua.h"
#endif

// Registers @p h for @p proto when the module supplied one; modbus / opcua return NULL on host builds.
static inline void register_if(ProtoConn proto, const ProtoHandler *h)
{
    if (h != NULL)
    {
        Protocols.proto = proto;
        Protocols.h = h;
        Protocols.add(protocore_session_span());
    }
}

void protocore_register_builtins(void)
{
    HttpConn.proto_handler(protocore_http_conn_span()); // always present
    register_if(PROTO_HTTP, HttpConn.handler);
#if PROTOCORE_ENABLE_TELNET
    Telnet.proto_handler(protocore_telnet_span());
    register_if(PROTO_TELNET, Telnet.handler);
#endif
#if PROTOCORE_ENABLE_SSH
    SshServer.proto_handler(protocore_ssh_server_span());
    register_if(PROTO_SSH, SshServer.handler);
#if PROTOCORE_SSH_PORT_FORWARD
    SshServer.rfwd_proto_handler(protocore_ssh_server_span());
    register_if(PROTO_SSH_RFWD, SshServer.handler);
#endif
#endif
#if PROTOCORE_NEED_MODBUS
    Modbus.handler(protocore_modbus_span());
    register_if(PROTO_MODBUS, Modbus.ptr);
#endif
#if PROTOCORE_ENABLE_OPCUA
    register_if(PROTO_OPCUA, protocore_opcua_protocore_handler());
#endif
}
