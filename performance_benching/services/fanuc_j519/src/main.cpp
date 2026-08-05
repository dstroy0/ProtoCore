// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the FANUC Stream Motion / option J519 UDP codec
// (services/machine_tool/fanuc_j519): builders and parsers for the PC->robot Motion Command and the robot->PC
// Robot Status and Ack packets - straight-line field packing at fixed offsets (integers
// little-endian via shared_primitives/endian.h, floats moved through a uint32_t with memcpy), pure
// (no sockets, no heap). Worked pattern follows performance_benching/device/modbus (a pure protocol codec with no
// hardware involved), so every call here exercises the real production code path; there is no
// hardware to stub (Stream Motion is a plain UDP payload codec - the caller owns the socket).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/fanuc_j519 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/machine_tool/fanuc_j519/fanuc_j519.h"
#include <Arduino.h>

static void fanuc_j519_bench_task(void *)
{
    // Motion Command (PC -> robot, 64 octets): realistic joint setpoints, mirrors
    // test/test_fanuc_j519's make_motion() sample data.
    static J519MotionCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.version_no = 1;
    cmd.sequence_no = 0x11223344;
    cmd.last_data = 0;
    cmd.read_io_type = (uint8_t)J519IoType::J519_IO_DI;
    cmd.read_io_index = 0x0102;
    cmd.read_io_mask = 0x0304;
    cmd.data_style = (uint8_t)J519DataStyle::J519_STYLE_JOINT;
    cmd.write_io_type = (uint8_t)J519IoType::J519_IO_DO;
    cmd.write_io_index = 0x0506;
    cmd.write_io_mask = 0x0708;
    cmd.write_io_value = 0x090A;
    for (int i = 0; i < PC_J519_AXES; i++)
    {
        cmd.joint_data[i] = (float)i;
    }
    cmd.joint_data[0] = 1.0f;
    cmd.joint_data[1] = -2.5f;

    // Robot Status (robot -> PC, 132 octets): mirrors test/test_fanuc_j519's make_status().
    static J519RobotStatus st;
    memset(&st, 0, sizeof(st));
    st.version_no = 1;
    st.sequence_no = 0x55667788;
    st.status = J519_STATUS_READY | J519_STATUS_SYSRDY;
    st.read_io_type = (uint8_t)J519IoType::J519_IO_RO;
    st.read_io_index = 0x1112;
    st.read_io_mask = 0x1314;
    st.read_io_value = 0x1516;
    st.time_stamp = 0x99AABBCC;
    for (int i = 0; i < PC_J519_AXES; i++)
    {
        st.cartesian_pose[i] = (float)(100 + i);
        st.joint_pose[i] = (float)(200 + i);
        st.motor_current[i] = (float)(300 + i);
    }

    // Ack (robot -> PC, 184 octets): mirrors test/test_fanuc_j519's make_ack() (largest packet -
    // two 20-entry float tables).
    static J519Ack ack;
    memset(&ack, 0, sizeof(ack));
    ack.version_no = 1;
    ack.axis_no = 6;
    ack.threshold_type = (uint32_t)J519ThresholdType::J519_THR_JERK;
    ack.max_cart_speed = 2000;
    ack.unknown0 = 0xDEADBEEF;
    for (int i = 0; i < PC_J519_THRESHOLDS; i++)
    {
        ack.threshold_no_load[i] = (float)i;
        ack.threshold_max_load[i] = (float)(1000 + i);
    }

    static uint8_t motion_buf[PC_J519_LEN_MOTION];
    static uint8_t status_buf[PC_J519_LEN_STATUS];
    static uint8_t ack_buf[PC_J519_LEN_ACK];
    pc_j519_build_motion(motion_buf, sizeof(motion_buf), &cmd);
    pc_j519_build_status(status_buf, sizeof(status_buf), &st);
    pc_j519_build_ack(ack_buf, sizeof(ack_buf), &ack);

    static J519MotionCommand got_cmd;
    static J519RobotStatus got_st;
    static J519Ack got_ack;

    for (;;)
    {
        Serial.printf("DB ==== fanuc_j519 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile bool ok = false;

        DBENCH_OP("pc_j519_build_motion", 100000, sink += pc_j519_build_motion(motion_buf, sizeof(motion_buf), &cmd));
        DBENCH_OP("pc_j519_parse_motion", 100000, ok = pc_j519_parse_motion(motion_buf, sizeof(motion_buf), &got_cmd));
        DBENCH_OP("pc_j519_build_status", 50000, sink += pc_j519_build_status(status_buf, sizeof(status_buf), &st));
        DBENCH_OP("pc_j519_parse_status", 50000, ok = pc_j519_parse_status(status_buf, sizeof(status_buf), &got_st));
        DBENCH_OP("pc_j519_build_ack", 20000, sink += pc_j519_build_ack(ack_buf, sizeof(ack_buf), &ack));
        DBENCH_OP("pc_j519_parse_ack", 20000, ok = pc_j519_parse_ack(ack_buf, sizeof(ack_buf), &got_ack));

        (void)sink;
        (void)ok;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: fanuc_j519 device microbench");
    xTaskCreatePinnedToCore(fanuc_j519_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
