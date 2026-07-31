// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Vxi11.ino
 * @brief VXI-11 instrument controller - drive an instrument over the legacy LXI transport
 *        (VXI-11 / ONC RPC) carrying SCPI (PC_ENABLE_VXI11).
 *
 * VXI-11 rides on ONC RPC (Sun RPC) with XDR over TCP. services/instrumentation/vxi11 is a pure codec (it builds
 * RPC calls + parses replies); the sketch owns the sockets and runs the standard session:
 *
 *   portmap GETPORT(0x0607AF/1/TCP) on port 111  -> the dynamic DEVICE_CORE port
 *   connect there, create_link("inst0")          -> a link id
 *   device_write("*IDN?\n")                       -> ask for the identity
 *   device_read()                                 -> the identity string
 *   destroy_link()
 *
 * Point INSTRUMENT_IP at a real VXI-11 / LXI instrument (or a python-vxi11 server). See the README.
 *
 * Build flags (platformio.ini):  build_flags = -DPC_ENABLE_VXI11=1
 */

#define PC_ENABLE_VXI11 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/client.h"
#include "services/instrumentation/vxi11/vxi11.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *INSTRUMENT_IP = "192.168.1.60"; // a VXI-11 instrument

static uint8_t c_req[256];
static uint8_t c_resp[512];

// Write one record-marked RPC call, read one single-fragment RPC reply (the bytes after the record
// mark) into c_resp. Returns the reply length, or 0 on timeout / overflow.
static size_t rpc_call(int cid, size_t req_len)
{
    if (req_len == 0 || !pc_client_send(cid, c_req, req_len))
    {
        return 0;
    }
    uint8_t rm[4];
    size_t got = 0;
    unsigned long deadline = millis() + 3000;
    while (got < 4 && millis() < deadline)
    {
        if (pc_client_available(cid))
        {
            got += pc_client_read(cid, rm + got, 4 - got);
        }
    }
    bool last = false;
    uint32_t frag = 0;
    if (got < 4 || !pc_rpc_parse_record_mark(rm, 4, &last, &frag) || frag > sizeof(c_resp))
    {
        return 0;
    }
    got = 0;
    while (got < frag && millis() < deadline)
    {
        if (pc_client_available(cid))
        {
            got += pc_client_read(cid, c_resp + got, frag - got);
        }
    }
    return got == frag ? got : 0;
}

static void run_session(const char *host)
{
    // 1) Ask the portmapper (TCP 111) for the DEVICE_CORE port.
    int pmap_cid = pc_client_open(host, PC_RPC_PMAP_PORT, 8000);
    if (pmap_cid < 0)
    {
        Serial.println("[vxi11] portmap connect failed");
        return;
    }
    size_t n =
        pc_vxi11_build_getport(c_req, sizeof(c_req), 1, PC_VXI11_CORE_PROG, PC_VXI11_CORE_VERS, PC_RPC_PROTO_TCP);
    uint32_t core_port = 0;
    if (!pc_vxi11_parse_getport_resp(c_resp, rpc_call(pmap_cid, n), &core_port) || core_port == 0)
    {
        Serial.println("[vxi11] GETPORT failed");
        pc_client_close(pmap_cid);
        return;
    }
    pc_client_close(pmap_cid);
    Serial.printf("[vxi11] DEVICE_CORE port = %u\n", core_port);

    // 2) Open the core channel and create a link to "inst0".
    int core_cid = pc_client_open(host, (uint16_t)core_port, 8000);
    if (core_cid < 0)
    {
        Serial.println("[vxi11] core connect failed");
        return;
    }
    n = pc_vxi11_build_create_link(c_req, sizeof(c_req), 2, 0x44575345 /* "PCE" */, false, 0, "inst0");
    Vxi11CreateLinkResp link;
    if (!pc_vxi11_parse_create_link_resp(c_resp, rpc_call(core_cid, n), &link) || link.error != PC_VXI11_ERR_NONE)
    {
        Serial.println("[vxi11] create_link failed");
        pc_client_close(core_cid);
        return;
    }
    Serial.printf("[vxi11] link=%d maxRecv=%u\n", link.lid, link.max_recv_size);

    // 3) Write "*IDN?" (END-terminated), then read the identity back.
    n = pc_vxi11_build_device_write(c_req, sizeof(c_req), 3, link.lid, 10000, 0, PC_VXI11_FLAG_END,
                                    (const uint8_t *)"*IDN?\n", 6);
    Vxi11WriteResp wr;
    pc_vxi11_parse_write_resp(c_resp, rpc_call(core_cid, n), &wr);

    n = pc_vxi11_build_device_read(c_req, sizeof(c_req), 4, link.lid, 1024, 10000, 0, 0, 0);
    Vxi11ReadResp rd;
    if (pc_vxi11_parse_read_resp(c_resp, rpc_call(core_cid, n), &rd) && rd.error == PC_VXI11_ERR_NONE)
    {
        Serial.printf("[vxi11] *IDN? -> %.*s\n", (int)rd.data_len, (const char *)rd.data);
    }
    else
    {
        Serial.println("[vxi11] device_read failed");
    }

    // 4) Close the link.
    n = pc_vxi11_build_destroy_link(c_req, sizeof(c_req), 5, link.lid);
    int32_t err = 0;
    pc_vxi11_parse_error_resp(c_resp, rpc_call(core_cid, n), &err);
    pc_client_close(core_cid);
    Serial.println("[vxi11] done");
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
}

void loop()
{
    static bool done = false;
    if (!done && millis() > 2000)
    {
        done = true;
        run_session(INSTRUMENT_IP); // pc_client_open resolves the dotted-quad host directly
    }
    delay(10);
}
