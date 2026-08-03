// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file proto_builtins.c
 * @brief The built-in protocol registry (policy, not mechanism).
 *
 * The session dispatcher is pure mechanism: it routes events to whatever ProtoHandler is
 * registered for a connection's ConnProto and names no protocol. This file is the one place
 * that knows which protocols the build includes. Each built-in's handler + callbacks live in
 * its own module and are exposed by a pure `*_proto_handler()` accessor (no session
 * dependency); here we install each behind the matching feature flag. Adding a protocol means
 * writing its module and adding one guarded line here - never editing the dispatcher.
 *
 * (The SSH remote-forward listener, PROTO_SSH_RFWD, is intentionally NOT here: it is a
 * runtime opt-in that self-registers from pc_ssh_forward_begin().)
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

// Register @p h for @p proto only if the module actually supplied one (modbus / opcua return NULL
// on host builds, where there is no TCP transport handler).
static inline void register_if(ConnProto proto, const ProtoHandler *h)
{
    if (h != NULL) // GCOVR_EXCL_BR_LINE  null half needs modbus/opcua compiled in alongside this file; no env does
    {
        proto_register(proto, h);
    }
}

void proto_register_builtins(void)
{
    register_if(PROTO_HTTP, http_proto_handler()); // always present (the core request/response protocol)
#if PC_ENABLE_TELNET
    register_if(PROTO_TELNET, pc_telnet_proto_handler());
#endif
#if PC_ENABLE_SSH
    // GCOVR_EXCL_START  no coverage env combines PC_ENABLE_SSH with a proto_register_builtins() call:
    // the SSH env tests the handler directly and the session/accept-gate envs keep SSH off (they reuse
    // the SSH proto slot). Device-reachable on any SSH firmware via session init; same gap as line 40.
    register_if(PROTO_SSH, ssh_proto_handler());
    // GCOVR_EXCL_STOP
#endif
#if PC_NEED_MODBUS
    register_if(PROTO_MODBUS, pc_modbus_proto_handler());
#endif
#if PC_ENABLE_OPCUA
    register_if(PROTO_OPCUA, pc_opcua_proto_handler());
#endif
}
