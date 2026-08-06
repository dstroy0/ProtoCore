// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OPC UA Robotics companion model (services/machine_tool/robotics): the
// Read resolver (node id -> value over a bound RoboticsMotionDeviceSystem) and the Browse of the
// address space. Pure model-walk over an in-RAM device system; the OPC UA transport is elsewhere.
//
// Build/flash:  pio run -d performance_benching/device/robotics -t upload --upload-port COM7
#include "device_bench.h"
#include "services/fieldbus/opcua/opcua.h"
#include "services/machine_tool/robotics/robotics.h"
#include <Arduino.h>

static RoboticsMotionDeviceSystem g_mds;

static void robotics_bench_task(void *)
{
    memset(&g_mds, 0, sizeof(g_mds));
    g_mds.name = "Robot-1";
    g_mds.device.manufacturer = "Acme Robotics";
    g_mds.device.model = "AR-6";
    g_mds.device.product_code = "AR6-STD";
    g_mds.device.serial_number = "SN-R-0007";
    g_mds.device.category = RoboticsMotionDeviceCategory::ROBOTICS_CAT_ARTICULATED_ROBOT;
    g_mds.device.on_path = true;
    g_mds.device.in_control = true;
    g_mds.device.speed_override = 75.0;
    g_mds.device.axis_count = 3;
    g_mds.device.axes[0].actual_position = 10.5;
    g_mds.device.axes[0].motion_profile = RoboticsMotionProfile::ROBOTICS_PROFILE_ROTARY;
    g_mds.device.axes[1].actual_position = -20.25;
    g_mds.device.axes[1].motion_profile = RoboticsMotionProfile::ROBOTICS_PROFILE_LINEAR;
    g_mds.device.axes[2].actual_position = 33.0;
    g_mds.controller.manufacturer = "Acme Controls";
    g_mds.controller.sw_revision = "4.2.0";
    g_mds.safety.operational_mode = RoboticsOperationalMode::ROBOTICS_MODE_AUTOMATIC;
    g_mds.safety.protective_stop = true;
    pc_robotics_bind(&g_mds);

    for (;;)
    {
        Serial.printf("DB ==== robotics device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile int sink = 0;
        OpcUaVariant v;
        DBENCH_OP("pc_robotics_read manufacturer", 200000,
                  sink += pc_robotics_read(PC_ROBOTICS_NS, 6201 /*N_MD_MANUFACTURER*/, OPCUA_ATTR_VALUE, &v));
        DBENCH_OP("pc_robotics_read axis1 pos", 200000,
                  sink += pc_robotics_read(PC_ROBOTICS_NS, 6411 /*N_AXIS1_POSITION*/, OPCUA_ATTR_VALUE, &v));
        OpcUaReference refs[8];
        DBENCH_OP("pc_robotics_browse (ObjectsFolder)", 200000, sink += pc_robotics_browse(0, 85, refs, 8));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: robotics device microbench");
    xTaskCreatePinnedToCore(robotics_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
