// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Umati.ino
 * @brief umati - OPC UA for Machine Tools (OPC 40501-1) MachineTool model (PC_ENABLE_UMATI).
 *
 * Turns the board into a umati machine-tool server: it exposes the standard MachineTool information
 * model (Identification, Monitoring - MachineTool / Channel / Spindle / Axis_X..Z, Production,
 * Notification) over OPC UA on TCP/4840, served straight out of a UmatiMachineTool struct you refresh
 * in loop(). Any umati / OPC UA client (the umati dashboard, UaExpert, python asyncua) browses the
 * MachineTool and reads live values by their standard BrowseNames - the same shape across vendors.
 *
 *   pc_umati_install(&mt)                    -> registers the OPC UA Browse + Read resolvers
 *   server.listen(4840, PROTO_OPCUA)      -> the OPC UA / umati endpoint
 *
 * Builds on example OpcUa (the OPC UA Binary server); umati is the machine-tool model on top. The
 * HTTP server on :80 runs alongside on the same event loop.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_OPCUA=1 -DPC_ENABLE_UMATI=1
 * (Arduino IDE: already set for you in the build_opt.h beside this sketch.)
 */

#define PC_ENABLE_OPCUA 1
#define PC_ENABLE_UMATI 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/machine_tool/umati/umati.h"
#include <math.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// The MachineTool the umati server exposes. Own it statically and refresh its live fields each loop
// from your real machine I/O; the resolvers read straight out of it (no copy).
static UmatiMachineTool mt;

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

    // --- Identification (static): OPC 40501-1 Identification ---
    mt.name = "PROTOCORE-CNC";
    mt.ident.manufacturer = "Acme Machines";
    mt.ident.model = "AX-500";
    mt.ident.serial_number = "SN-00042";
    mt.ident.software_revision = "1.0.0";
    mt.ident.product_instance_uri = "urn:acme:AX-500:SN-00042";
    mt.ident.year_of_construction = 2026;

    // --- Initial monitoring state ---
    mt.operation_mode = UmatiOperationMode::UMATI_OP_AUTOMATIC;
    mt.channel.state = UmatiChannelState::UMATI_CH_RUNNING;
    mt.channel.feed_override = 100.0;
    mt.channel.rapid_override = 100.0;
    mt.channel.active_program = "PART_A.NC";
    mt.active_program = "PART_A.NC";
    mt.message_text = ""; // no active message
    mt.message_severity = 0;

    pc_umati_install(&mt); // bind + register the OPC UA Browse/Read resolvers
    server.on("/", HttpMethod::HTTP_GET,
              [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "umati MachineTool on :4840"); });
    server.listen(4840, ConnProto::PROTO_OPCUA); // OPC UA / umati endpoint - before begin()
    server.begin(80);
    Serial.println("umati (OPC UA for Machine Tools): opc.tcp://<ip>:4840  - browse MachineTool, read live values");
}

void loop()
{
    server.handle();

    // Simulate a running machine: refresh the live monitoring values a couple of times a second.
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last >= 500)
    {
        last = now;
        double t = now / 1000.0;
        mt.power_on_duration_s = t;
        mt.spindle.rotation_speed = 1200.0 + 50.0 * sin(t);
        mt.spindle.override_value = 90.0;
        mt.spindle.is_rotating = true;
        mt.axis_x.actual_position = 100.0 * sin(t / 2.0);
        mt.axis_y.actual_position = 100.0 * cos(t / 2.0);
        mt.axis_z.actual_position = 25.0;
        mt.produced_part_count = (uint32_t)(now / 10000); // a finished part every 10 s
    }
}
