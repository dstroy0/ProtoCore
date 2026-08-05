// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ModbusScan.ino
 * @brief Modbus master codec + register scanner (PC_ENABLE_MODBUS_MASTER).
 *
 * The master/client side of Modbus: build read-request ADUs and parse the
 * responses into register values. This example self-scans - it runs the requests
 * through the on-board Modbus slave (pc_modbus_process_adu) so the build/parse codec
 * is demonstrated end-to-end without an external device; against a real slave you
 * would send the ADU over a TCP client instead. GET /scan returns the discovered
 * holding registers as JSON.
 *
 * NOTE: enable both flags for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_MODBUS=1 -DPC_ENABLE_MODBUS_MASTER=1
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_MODBUS 1
#define PC_ENABLE_MODBUS_MASTER 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/fieldbus/modbus/modbus.h"
#include "services/fieldbus/modbus/modbus_master.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


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

    // Seed a few holding registers (the "slave" data model).
    pc_modbus_server_init();
    pc_modbus_set_holding_reg(0, 1234);
    pc_modbus_set_holding_reg(1, 5678);
    pc_modbus_set_holding_reg(2, 4095);
    listen(502, PROTO_MODBUS); // real Modbus TCP slave on :502

    // /scan: read holding registers 0..3 via the master codec (self-scan).
    on_http("/scan", HTTP_GET, [](uint8_t id, HttpReq *) {
        uint8_t req[16], resp[MODBUS_ADU_MAX];
        size_t rn =
            pc_modbus_build_read((uint8_t)MODBUS_FC_READ_HOLDING_REGS, 1, 1, 0, 3, req, sizeof(req));
        size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp));
        uint16_t regs[3];
        uint8_t ex = 0;
        int n = pc_modbus_parse_response(resp, pn, regs, 3, &ex);
        char b[96];
        if (n > 0)
        {
            snprintf(b, sizeof(b), "{\"regs\":[%u,%u,%u]}", regs[0], regs[1], regs[2]);
        }
        else
        {
            snprintf(b, sizeof(b), "{\"exception\":%u}", ex);
        }
        send_text(id, 200, "application/json", b);
    });
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
