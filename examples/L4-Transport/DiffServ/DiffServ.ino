// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file DiffServ.ino
 * @brief DiffServ QoS marking (RFC 2474): stamp outbound traffic with a DSCP.
 *
 * With PC_ENABLE_DIFFSERV the transport writes the 6-bit DSCP into the IP DS field of outbound traffic so
 * a QoS-aware network - and the Wi-Fi WMM access-category mapping - prioritizes it over best-effort.
 *
 *   - pc_set_default_dscp(EF)   -> every accepted connection's data is marked Expedited Forwarding (46)
 *   - pc_conn_set_dscp(slot, X) -> re-tag ONE connection with any DSCP (real per-flow QoS, or arbitrary
 *                                   tagging for network testing)
 *   - pc_udp_set_dscp(X)        -> mark outbound UDP datagrams
 *
 * Verify on the wire (from a machine on the same LAN):
 *   sudo tcpdump -i <iface> -v host <board-ip> and tcp port 80
 * GET /    responses carry tos 0xb8 (EF, DSCP 46); GET /tag responses carry tos 0xc0 (CS6, DSCP 48).
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/diffserv.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// GET / - the response inherits the server-wide default DSCP (EF) set in setup().
void handle_root(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_text(slot, 200, "text/plain", "marked EF (DSCP 46)\n");
}

// GET /tag - re-tag THIS connection to CS6 (48) before responding, overriding the default. This is the
// per-flow lever: tag any single connection with any class - useful for real QoS or network testing.
void handle_tag(uint8_t slot, HttpReq *req)
{
    (void)req;
    pc_conn_set_dscp(slot, PC_DSCP_CS6);
    send_text(slot, 200, "text/plain", "re-tagged CS6 (DSCP 48)\n");
}

void setup()
{
    Serial.begin(115200);

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip();
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Mark every outbound TCP connection Expedited Forwarding, and outbound UDP too. A DSCP of 0 would
    // mean best-effort (no marking). Set before begin() so listeners pick it up.
    pc_set_default_dscp(PC_DSCP_EF);
    pc_udp_set_dscp(PC_DSCP_EF);

    on_http("/", HTTP_GET, handle_root);
    on_http("/tag", HTTP_GET, handle_tag);
    begin_http(80, NULL);
    Serial.println("DiffServ server on :80 (default EF; /tag -> CS6)");
}

void loop()
{
    handle();
}
