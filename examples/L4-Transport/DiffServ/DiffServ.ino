// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file DiffServ.ino
 * @brief DiffServ QoS marking (RFC 2474): stamp outbound traffic with a DSCP.
 *
 * With PROTOCORE_ENABLE_DIFFSERV the transport writes the 6-bit DSCP into the IP DS field of outbound traffic so
 * a QoS-aware network - and the Wi-Fi WMM access-category mapping - prioritizes it over best-effort.
 *
 *   - DiffServ.set_default   -> every outbound TCP connection starts with this mark: EF (46) here
 *   - TcpListener.set_dscp   -> every connection accepted on one port takes its own mark: CS6 (48) on :8080
 *   - DiffServ.set_udp       -> outbound UDP datagrams take this mark
 *
 * A live connection keeps the mark it starts with. RFC 9293 sec 3.9.2 SHLD-23: an application does not change the
 * DS field during a connection, so the port is the finest level.
 *
 * Verify on the wire (from a machine on the same LAN):
 *   sudo tcpdump -i <iface> -v host <board-ip> and tcp
 * Responses on :80 carry tos 0xb8 (EF, DSCP 46); responses on :8080 carry tos 0xc0 (CS6, DSCP 48).
 */

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/diffserv/diffserv.h"
#include "network_drivers/transport/tcp/server/server.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";
static const uint16_t MARKED_PORT = 8080; // the listener whose connections take CS6

// GET / - the response carries the mark of the port that the request came in on.
void handle_root(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_text(slot, 200, "text/plain", "marked by its port: EF (46) on :80, CS6 (48) on :8080\n");
}

void setup()
{
    Serial.begin(115200);

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    while (!protocore_physical_wifi_up())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = protocore_physical_egress_ipv4();
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Mark every outbound TCP connection Expedited Forwarding, and outbound UDP too. A DSCP of 0 would
    // mean best-effort (no marking). Set before begin_http() so listeners pick it up.
    DiffServV.dscp = PROTOCORE_DSCP_EF;
    DiffServ.set_default(protocore_diffserv_span());
    DiffServ.set_udp(protocore_diffserv_span());

    on_http("/", HTTP_GET, handle_root);
    begin_http(80, NULL);

    // A second HTTP port, whose accepted connections take CS6 in place of the default.
    listen(MARKED_PORT, PROTO_HTTP);
    TcpListenerV.bind.port = MARKED_PORT;
    TcpListenerV.bind.dscp = PROTOCORE_DSCP_CS6;
    TcpListener.set_dscp(protocore_tcp_listener_span());
    Serial.printf("DiffServ server on :80 (EF) and :%u (CS6)\n", (unsigned)MARKED_PORT);
}

void loop()
{
    handle();
}
