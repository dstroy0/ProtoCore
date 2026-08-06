// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file OpcUaClient.ino
 * @brief OPC UA Binary client - drive a server over TCP (PC_ENABLE_OPCUA_CLIENT).
 *
 * services/pc_opcua_client builds OPC UA requests and parses responses; it is
 * transport-agnostic, so the app owns the socket. This sketch runs the OPC UA
 * server (services/fieldbus/opcua) on :4840 AND, once connected, opens the library's
 * outbound TCP client (pc_client) to its own address and walks the full client
 * sequence against it:
 *
 *   Hello/Ack -> OpenSecureChannel -> GetEndpoints -> CreateSession -> ActivateSession
 *             -> Browse(Objects) -> Read(values) -> Write(setpoint)+read-back
 *             -> CloseSession -> CloseSecureChannel
 *
 * The results are printed over Serial. Point SERVER_HOST at any OPC UA server to use
 * the client on its own; the bundled server just makes the example self-contained.
 *
 * Enable both flags for the build (platformio.ini):
 *     build_flags = -DPC_ENABLE_OPCUA=1 -DPC_ENABLE_OPCUA_CLIENT=1
 */

#define PC_ENABLE_OPCUA 1
#define PC_ENABLE_OPCUA_CLIENT 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/tcp.h"
#include "services/fieldbus/opcua/opcua.h"
#include "services/fieldbus/opcua_client/opcua_client.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// --- server side: a tiny address space (ns=1;i=1,2 read-only, i=3 writable) ---
static uint32_t setpoint = 100;

static bool srv_read(uint16_t ns, uint32_t id, uint32_t attr, OpcUaVariant *out)
{
    if (ns != 1 || attr != OPCUA_ATTR_VALUE)
    {
        return false;
    }
    if (id == 1)
    {
        out->type = OPCUA_VAR_UINT32;
        out->u32 = millis() / 1000;
        return true;
    }
    if (id == 2)
    {
        out->type = OPCUA_VAR_DOUBLE;
        out->f64 = 23.5;
        return true;
    }
    if (id == 3)
    {
        out->type = OPCUA_VAR_UINT32;
        out->u32 = setpoint;
        return true;
    }
    return false;
}

static uint32_t srv_write(uint16_t ns, uint32_t id, uint32_t attr, const OpcUaVariant *value)
{
    if (ns == 1 && id == 3 && attr == OPCUA_ATTR_VALUE && value->type == OPCUA_VAR_UINT32)
    {
        setpoint = value->u32;
        return 0; // Good
    }
    return (ns == 1 && id == 3) ? OPCUA_STATUS_BAD_NOT_WRITABLE : OPCUA_STATUS_BAD_NODE_ID_UNKNOWN;
}

static int32_t srv_browse(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max)
{
    if (ns != 0 || id != 85)
    {
        return -1;
    }
    static const char *names[2] = {"Uptime", "Temperature"};
    int32_t n = 0;
    for (uint32_t i = 0; i < 2 && (uint32_t)n < max; i++, n++)
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

// --- client side: send one framed message, read one framed reply over pc_client ---
static uint8_t c_req[512];
static uint8_t c_resp[2048];

static size_t exchange(int cid, size_t reqlen)
{
    if (reqlen == 0 || !Tcp.client->send(cid, c_req, reqlen))
    {
        return 0;
    }
    // Read the 8-byte UACP header, then the rest of MessageSize.
    size_t got = 0;
    uint32_t deadline = millis() + 3000;
    while (got < 8 && millis() < deadline)
    {
        if (Tcp.client->available(cid))
        {
            got += Tcp.client->read(cid, c_resp + got, 8 - got);
        }
    }
    if (got < 8)
    {
        return 0;
    }
    uint32_t size =
        (uint32_t)c_resp[4] | ((uint32_t)c_resp[5] << 8) | ((uint32_t)c_resp[6] << 16) | ((uint32_t)c_resp[7] << 24);
    if (size < 8 || size > sizeof(c_resp))
    {
        return 0;
    }
    while (got < size && millis() < deadline)
    {
        if (Tcp.client->available(cid))
        {
            got += Tcp.client->read(cid, c_resp + got, size - got);
        }
    }
    return got == size ? size : 0;
}

static void run_client(uint32_t ip)
{
    char host[16];
    snprintf(host, sizeof(host), "%u.%u.%u.%u", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
             (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
    int cid = Tcp.client->open(host, 4840, 8000);
    if (cid < 0)
    {
        Serial.println("[opcua-client] connect failed");
        return;
    }
    OpcUaClient c;
    pc_opcua_client_init(&c);
    OpcUaAckInfo ack;
    size_t n;

    n = exchange(cid, pc_opcua_client_hello("opc.tcp://self:4840", c_req, sizeof(c_req)));
    if (!n || !pc_opcua_client_on_ack(c_resp, n, &ack))
    {
        Serial.println("[opcua-client] HELLO/ACK failed");
        return;
    }
    n = exchange(cid, pc_opcua_client_open(&c, c_req, sizeof(c_req)));
    if (!n || !pc_opcua_client_on_open(&c, c_resp, n))
    {
        Serial.println("[opcua-client] OpenSecureChannel failed");
        return;
    }
    n = exchange(cid, pc_opcua_client_get_endpoints(&c, "opc.tcp://self:4840", c_req, sizeof(c_req)));
    Serial.printf("[opcua-client] GetEndpoints -> %d endpoint(s)\n",
                  n ? (int)pc_opcua_client_on_get_endpoints(c_resp, n) : -1);
    n = exchange(cid, pc_opcua_client_create_session(&c, "esp32", "opc.tcp://self:4840", c_req, sizeof(c_req)));
    if (!n || !pc_opcua_client_on_create_session(&c, c_resp, n))
    {
        Serial.println("[opcua-client] CreateSession failed");
        return;
    }
    n = exchange(cid, pc_opcua_client_activate_session(&c, c_req, sizeof(c_req)));
    if (!n || !pc_opcua_client_on_activate_session(c_resp, n))
    {
        Serial.println("[opcua-client] ActivateSession failed");
        return;
    }
    Serial.printf("[opcua-client] session active (channel=%u token=%u)\n", c.channel_id, c.token_id);

    // Browse the Objects folder.
    n = exchange(cid, pc_opcua_client_browse(&c, 0, 85, c_req, sizeof(c_req)));
    OpcUaClientRef refs[4];
    int32_t nrefs = n ? pc_opcua_client_on_browse(c_resp, n, refs, 4) : -1;
    for (int32_t i = 0; i < nrefs; i++)
    {
        Serial.printf("[opcua-client] browse: %s -> ns%u;i=%u\n", refs[i].browse_name, refs[i].target_ns,
                      refs[i].target_id);
    }

    // Read the two variables.
    OpcUaReadItem items[2] = {{1, 1, true, OPCUA_ATTR_VALUE}, {1, 2, true, OPCUA_ATTR_VALUE}};
    n = exchange(cid, pc_opcua_client_read(&c, items, 2, c_req, sizeof(c_req)));
    OpcUaVariant vals[2];
    uint32_t sts[2];
    int32_t nv = n ? pc_opcua_client_on_read(c_resp, n, vals, sts, 2) : -1;
    if (nv == 2)
    {
        Serial.printf("[opcua-client] read: Uptime=%u Temperature=%.1f\n", vals[0].u32, vals[1].f64);
    }

    // Write the setpoint (ns=1;i=3) then read it back.
    OpcUaWriteItem wi[1];
    memset(wi, 0, sizeof(wi));
    wi[0].ns = 1;
    wi[0].id = 3;
    wi[0].numeric = true;
    wi[0].attribute = OPCUA_ATTR_VALUE;
    wi[0].value.type = OPCUA_VAR_UINT32;
    wi[0].value.u32 = 555;
    n = exchange(cid, pc_opcua_client_write(&c, wi, 1, c_req, sizeof(c_req)));
    uint32_t wres[1];
    int32_t nw = n ? pc_opcua_client_on_write(c_resp, n, wres, 1) : -1;
    OpcUaReadItem rb[1] = {{1, 3, true, OPCUA_ATTR_VALUE}};
    n = exchange(cid, pc_opcua_client_read(&c, rb, 1, c_req, sizeof(c_req)));
    OpcUaVariant rbv[1];
    uint32_t rbs[1];
    int32_t nrb = n ? pc_opcua_client_on_read(c_resp, n, rbv, rbs, 1) : -1;
    if (nw == 1 && nrb == 1)
    {
        Serial.printf("[opcua-client] write setpoint=555 (status 0x%08X), read-back=%u\n", wres[0], rbv[0].u32);
    }

    exchange(cid, pc_opcua_client_close_session(&c, c_req, sizeof(c_req)));
    Tcp.client->send(cid, c_req, pc_opcua_client_close_channel(c_req, sizeof(c_req)));
    Tcp.client->close(cid);
    Serial.println("[opcua-client] done");
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

    static char url[48];
    snprintf(url, sizeof(url), "opc.tcp://%u.%u.%u.%u:4840", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
             (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
    pc_opcua_set_endpoint_url(url); // advertised in GetEndpoints / CreateSession
    pc_opcua_set_read_handler(srv_read);
    pc_opcua_set_write_handler(srv_write);
    pc_opcua_set_browse_handler(srv_browse);
    on_http("/", HTTP_GET,
              [](uint8_t id, HttpReq *) { send_text(id, 200, "text/plain", "OPC UA client demo"); });
    listen(4840, PROTO_OPCUA); // server endpoint, before begin()
    begin_http(80, NULL);
}

void loop()
{
    handle();

    // Run the client exchange once, a few seconds after boot (server is up by then).
    static bool done = false;
    if (!done && millis() > 4000)
    {
        done = true;
        run_client(pc_net_egress_ip()); // Tcp.client->open resolves the dotted-quad host directly
    }
}
