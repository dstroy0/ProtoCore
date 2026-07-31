// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Euromap77.ino
 * @brief EUROMAP 77 (OPC 40077) - OPC UA for injection moulding machines (IMM <-> MES) (PC_ENABLE_EUROMAP77).
 *
 * Turns the board into an EUROMAP 77 IMM server: it exposes the standard IMM_MES_Interface information
 * model (MachineInformation, MachineStatus, Jobs -> ActiveJob + ActiveJobValues with the UInt64
 * production counters) over OPC UA on TCP/4840, served straight out of an EmImm struct you refresh in
 * loop(). Any OPC UA / MES client (UaExpert, python asyncua, open62541) browses the IMM and reads live
 * values by their standard BrowseNames - the same shape across machine vendors.
 *
 *   pc_em77_install(&imm)                 -> registers the OPC UA Browse + Read resolvers
 *   server.listen(4840, PROTO_OPCUA)       -> the OPC UA / EUROMAP endpoint
 *
 * Builds on example OpcUa (the OPC UA Binary server); EUROMAP 77 is the injection-molding model on top -
 * the plastics-industry twin of example Umati (machine tools) and Robotics. The HTTP server on :80 runs
 * alongside on the same event loop.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_OPCUA=1 -DPC_ENABLE_EUROMAP77=1
 * (Arduino IDE: already set for you in the build_opt.h beside this sketch.)
 */

#define PC_ENABLE_OPCUA 1
#define PC_ENABLE_EUROMAP77 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/machine_tool/euromap77/euromap77.h"
#include <math.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// The IMM the EUROMAP 77 server exposes. Own it statically and refresh its live fields each loop from
// your real machine I/O; the resolvers read straight out of it (no copy).
static EmImm imm;

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

    // --- Machine identity (static): EUROMAP 77 MachineInformation ---
    imm.name = "PC-IMM";
    imm.info.manufacturer = "Acme Plastics";
    imm.info.model = "IM-200";
    imm.info.serial_number = "SN-IMM-0042";
    imm.info.product_code = "IM200-STD";
    imm.info.hardware_revision = "H1";
    imm.info.software_revision = "1.0.0";
    imm.info.device_revision = "D2";
    imm.info.manufacturer_uri = "urn:acme:plastics:IM-200";
    imm.status.is_present = true;
    imm.status.machine_mode = EmMachineMode::EM_MODE_AUTOMATIC;

    // --- Active job (static parameters) ---
    imm.active_job.job_name = "JOB-A";
    imm.active_job.job_description = "widget production run";
    imm.active_job.material = "ABS";
    imm.active_job.product_name = "Widget";
    imm.active_job.mould_id = "MLD-9";
    imm.active_job.expected_cycle_time = 12.5;
    imm.active_job.num_cavities = 4;
    imm.active_job.nominal_parts = 100000;
    imm.active_job_values.job_status = EmJobStatus::EM_JOB_IN_PRODUCTION;

    pc_em77_install(&imm); // bind + register the OPC UA Browse/Read resolvers
    server.on("/", HttpMethod::HTTP_GET,
              [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "EUROMAP 77 IMM on :4840"); });
    server.listen(4840, ConnProto::PROTO_OPCUA); // OPC UA / EUROMAP endpoint - before begin()
    server.begin(80);
    Serial.println("EUROMAP 77 (OPC UA for IMM): opc.tcp://<ip>:4840  - browse IMM_MES_Interface, read live counters");
}

void loop()
{
    server.handle();

    // Simulate a running IMM: a new cycle every ~2 s, ~4 good parts per cycle (4-cavity mould), the
    // occasional reject. The 64-bit counters climb; LastCycleTime jitters around ExpectedCycleTime.
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last >= 2000)
    {
        last = now;
        imm.active_job_values.job_cycle_counter++;
        imm.active_job_values.machine_cycle_counter++;
        uint64_t good = imm.active_job.num_cavities;
        bool reject = (imm.active_job_values.job_cycle_counter % 25) == 0; // one short-shot every 25 cycles
        if (reject)
        {
            good -= 1;
            imm.active_job_values.job_bad_parts_counter += 1;
        }
        imm.active_job_values.job_good_parts_counter += good;
        imm.active_job_values.job_parts_counter += imm.active_job.num_cavities;
        double t = now / 1000.0;
        imm.active_job_values.last_cycle_time = 12.5 + 0.5 * sin(t / 5.0);
        uint64_t cyc = imm.active_job_values.job_cycle_counter;
        imm.active_job_values.average_cycle_time =
            (imm.active_job_values.average_cycle_time * (double)(cyc - 1) + imm.active_job_values.last_cycle_time) /
            (double)cyc;
    }
}
