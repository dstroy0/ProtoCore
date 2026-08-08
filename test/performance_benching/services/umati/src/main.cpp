// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OPC UA umati (MachineTool) companion model
// (services/machine_tool/umati): the Read resolver (node id -> value over a bound UmatiMachineTool) and the Browse
// of the address space. Pure model-walk over an in-RAM machine-tool struct; the OPC UA transport is
// elsewhere.
//
// Build/flash:  pio run -d performance_benching/device/umati -t upload --upload-port COM7
#include "device_bench.h"
#include "services/fieldbus/opcua/opcua.h"
#include "services/machine_tool/umati/umati.h"
#include <Arduino.h>

static UmatiMachineTool g_mt;

static void umati_bench_task(void *)
{
    memset(&g_mt, 0, sizeof(g_mt));
    g_mt.name = "CNC-1";
    g_mt.ident.manufacturer = "Acme Machines";
    g_mt.ident.model = "AX-500";
    g_mt.ident.serial_number = "SN-00042";
    g_mt.ident.software_revision = "2.7.1";
    g_mt.ident.product_instance_uri = "urn:acme:AX-500:SN-00042";
    g_mt.ident.year_of_construction = 2026;
    g_mt.operation_mode = UmatiOperationMode::UMATI_OP_AUTOMATIC;
    g_mt.power_on_duration_s = 12345.0;
    g_mt.channel.state = UmatiChannelState::UMATI_CH_RUNNING;
    g_mt.channel.feed_override = 85.0;
    g_mt.channel.active_program = "PART_A.NC";
    g_mt.spindle.rotation_speed = 1200.0;
    g_mt.spindle.is_rotating = true;
    g_mt.axis_x.actual_position = 10.5;
    g_mt.axis_y.actual_position = -3.25;
    g_mt.axis_z.actual_position = 42.0;
    g_mt.active_program = "PART_A.NC";
    g_mt.produced_part_count = 7;
    pc_umati_bind(&g_mt);

    for (;;)
    {
        Serial.printf("DB ==== umati device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile int sink = 0;
        OpcUaVariant v;
        DBENCH_OP("pc_umati_read manufacturer", 200000,
                  sink += pc_umati_read(PC_UMATI_NS, 5101 /*N_ID_MANUFACTURER*/, OPCUA_ATTR_VALUE, &v));
        DBENCH_OP("pc_umati_read year", 200000,
                  sink += pc_umati_read(PC_UMATI_NS, 5104 /*N_ID_YEAR*/, OPCUA_ATTR_VALUE, &v));
        OpcUaReference refs[8];
        DBENCH_OP("pc_umati_browse (ObjectsFolder)", 200000, sink += pc_umati_browse(0, 85, refs, 8));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: umati device microbench");
    xTaskCreatePinnedToCore(umati_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
