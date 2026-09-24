// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file DiffServ.ino
 * @brief DiffServ QoS marking (RFC 2474): stamp outbound traffic with a DSCP.
 *
 * With PROTOCORE_ENABLE_DIFFSERV the transport writes the 6-bit DSCP into the IP DS field of outbound traffic so
 * a QoS-aware network - and the Wi-Fi WMM access-category mapping - prioritizes it over best-effort.
 *
 *   - DiffServ.set_default (EF)  -> every accepted connection's data is marked Expedited Forwarding (46)
 *   - TcpListener.set_dscp (X)   -> override for every connection accepted on ONE port, with any DSCP (real
 *                                   per-flow QoS, or arbitrary tagging for network testing). A live connection
 *                                   is not re-tagged: RFC 9293 sec 3.9.2 SHLD-23, so the port is the finest level.
 *   - DiffServ.set_udp (X)       -> mark outbound UDP datagrams
 *
 * Verify on the wire (from a machine on the same LAN):
 *   sudo tcpdump -i <iface> -v host <board-ip> and tcp portrange 80-8080
 * GET :80/ responses carry tos 0xb8 (EF, DSCP 46); GET :8080/tag responses carry tos 0xc0 (CS6, DSCP 48).
 */

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/diffserv/diffserv.h"
#include "network_drivers/transport/tcp/tcp.h" // TcpListener: the per-port DSCP override

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// GET / - the response inherits the server-wide default DSCP (EF) set in setup().
void handle_root(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_text(slot, 200, "text/plain", "marked EF (DSCP 46)\n");
}

// GET :8080/tag - every connection accepted on :8080 is tagged CS6 (48), overriding the default (see
// setup()). This is the per-flow lever: tag a port's connections with any class - useful for real QoS or
// network testing.
void handle_tag(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_text(slot, 200, "text/plain", "tagged CS6 (DSCP 48) on :8080\n");
}

void setup()
{
    Serial.begin(115200);

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32;
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Mark every outbound TCP connection Expedited Forwarding, and outbound UDP too. A DSCP of 0 would
    // mean best-effort (no marking). Set before begin() so listeners pick it up.
    DiffServV.dscp = PROTOCORE_DSCP_EF;
    DiffServ.set_default(protocore_diffserv_span());
    DiffServ.set_udp(protocore_diffserv_span());

    on_http("/", HTTP_GET, handle_root);
    on_http("/tag", HTTP_GET, handle_tag);
    listen(80, ProtoConn::PROTO_HTTP);
    listen(8080, ProtoConn::PROTO_HTTP);
    proto_begin(NULL);

    // Per-port override: connections accepted on :8080 from now on are marked CS6 instead of EF.
    TcpListenerV.bind.port = 8080;
    TcpListenerV.bind.dscp = PROTOCORE_DSCP_CS6;
    TcpListener.set_dscp(protocore_tcp_listener_span());
    Serial.println("DiffServ server on :80 (default EF) and :8080 (CS6)");
}

void loop()
{
    handle();
}
