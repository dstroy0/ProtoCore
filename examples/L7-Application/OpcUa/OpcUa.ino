// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file OpcUa.ino
 * @brief OPC UA Binary server - SecureChannel + Session + Read/Write + Browse (PC_ENABLE_OPCUA).
 *
 * Opens an OPC UA endpoint on TCP/4840. Implements the OPC UA Binary type codec,
 * UA-TCP (UACP) framing, the Hello/Acknowledge handshake, the SecureChannel
 * (OpenSecureChannel, SecurityPolicy None), the Session (CreateSession +
 * ActivateSession), the Read service, the Browse service, and CloseSession /
 * CloseSecureChannel - so a client (UaExpert, open62541, Python asyncua, ...)
 * completes the handshake, opens a secure channel, activates a session, browses the
 * Objects folder, and reads node values, all via registered resolvers.
 *
 *   listen(4840, ConnProto::PROTO_OPCUA)  -> HEL/ACK, OPN, CreateSession, ActivateSession, Read, Browse, Close
 *
 * The HTTP server on :80 runs alongside, sharing the same connection pool and
 * event loop - OPC UA is just another protocol on its own port.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_OPCUA=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_OPCUA 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/fieldbus/opcua/opcua.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

static uint32_t setpoint = 100; // a writable variable, exposed at ns=1;i=10

// Read resolver: map a tiny address space (namespace 1) onto live values. A client
// Reads node ns=1;i=1 (uptime), i=2 (free heap), i=3 (a Double) or i=10 (the writable
// setpoint). Return false for anything else and the server answers BadNodeIdUnknown.
static bool pc_opcua_read(uint16_t ns, uint32_t id, uint32_t attribute, OpcUaVariant *out)
{
    if (ns != 1 || attribute != OPCUA_ATTR_VALUE)
    {
        return false;
    }
    switch (id)
    {
    case 1:
        out->type = OpcUaVariantType::OPCUA_VAR_UINT32;
        out->u32 = millis() / 1000;
        return true;
    case 2:
        out->type = OpcUaVariantType::OPCUA_VAR_UINT32;
        out->u32 = ESP.getFreeHeap();
        return true;
    case 3:
        out->type = OpcUaVariantType::OPCUA_VAR_DOUBLE;
        out->f64 = 23.5;
        return true;
    case 10:
        out->type = OpcUaVariantType::OPCUA_VAR_UINT32;
        out->u32 = setpoint;
        return true;
    default:
        return false;
    }
}

// Write resolver: only ns=1;i=10 (the setpoint) is writable. Returns a StatusCode -
// Good (0) on success, or a Bad code the client surfaces as the write result.
static uint32_t pc_opcua_write(uint16_t ns, uint32_t id, uint32_t attribute, const OpcUaVariant *value)
{
    if (ns == 1 && id == 10 && attribute == OPCUA_ATTR_VALUE && value->type == OpcUaVariantType::OPCUA_VAR_UINT32)
    {
        setpoint = value->u32;
        return 0; // Good
    }
    return (ns == 1 && id == 10) ? OPCUA_STATUS_BAD_NOT_WRITABLE : OPCUA_STATUS_BAD_NODE_ID_UNKNOWN;
}

// Browse resolver: the standard Objects folder (ns=0;i=85) organizes our three
// variables (ns=1;i=1..3). Browsing anything else is BadNodeIdUnknown.
static int32_t pc_opcua_browse(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max)
{
    if (ns != 0 || id != 85) // i=85 == Objects folder
    {
        return -1;
    }
    static const char *names[3] = {"Uptime", "FreeHeap", "Temperature"};
    int32_t n = 0;
    for (uint32_t i = 0; i < 3 && (uint32_t)n < max; i++, n++)
    {
        out[n].ref_type_id = OPCUA_REFTYPE_ORGANIZES;
        out[n].is_forward = true;
        out[n].target_ns = 1;
        out[n].target_id = i + 1;
        out[n].browse_name_ns = 1;
        out[n].browse_name = names[i];
        out[n].display_name = names[i];
        out[n].node_class = OPCUA_NODECLASS_VARIABLE;
        out[n].type_def_id = OPCUA_TYPEDEF_BASE_DATA_VARIABLE;
    }
    return n;
}

void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/", HttpMethod::HTTP_GET,
              [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "OPC UA on :4840"); });
    pc_opcua_set_read_handler(pc_opcua_read);     // serve Reads for ns=1;i=1..3,10
    pc_opcua_set_write_handler(pc_opcua_write);   // accept Writes to ns=1;i=10 (the setpoint)
    pc_opcua_set_browse_handler(pc_opcua_browse); // list those under the Objects folder
    server.listen(4840, ConnProto::PROTO_OPCUA);  // OPC UA Binary endpoint - before begin() (it activates listeners)
    server.begin(80);
    Serial.println("OPC UA endpoint: opc.tcp://<ip>:4840 (handshake + SecureChannel + Session + Read/Write + Browse)");
}

void loop()
{
    server.handle();
}
