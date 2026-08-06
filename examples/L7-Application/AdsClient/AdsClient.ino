// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file AdsClient.ino
 * @brief Beckhoff ADS client - read a TwinCAT PLC over AMS/TCP (PC_ENABLE_ADS).
 *
 * services/fieldbus/ads builds ADS/AMS requests and parses the responses; it is transport-
 * agnostic, so the app owns the socket. This sketch opens the library's outbound TCP client
 * (pc_client) to a TwinCAT router on TCP 48898 and runs a small ADS sequence against it:
 *
 *   ReadDeviceInfo  -> the runtime name + version
 *   ReadState       -> RUN / STOP / CONFIG
 *   ReadWrite(0xF003, name) -> a handle for a PLC symbol by name
 *   Read(0xF005, handle)    -> the symbol's current value (an INT32 here)
 *   Write(0xF006, handle)   -> release the handle
 *
 * results are printed over Serial. Unlike an OPC UA server, an ADS target cannot be
 * self-hosted here, so point PLC_IP / PLC_NET_ID at a real TwinCAT router. First add
 * an AMS route on the PLC back to this device's AMSNetId (below) or the router will
 * reject the connection - see the README.
 *
 * Build flag (platformio.ini):  build_flags = -DPC_ENABLE_ADS=1
 */

#define PC_ENABLE_ADS 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/tcp.h"
#include "services/fieldbus/ads/ads.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// --- the target TwinCAT router ---
static const char *PLC_IP = "192.168.1.50";       // the router's IP
static const char *PLC_NET_ID = "5.18.30.40.1.1"; // the PLC's AMSNetId
static const uint16_t PLC_PORT = 851;             // 851 = first TC3 PLC runtime (801 for TC2)
static const char *SYMBOL = "MAIN.nCounter";      // an INT32 in the PLC to read

// This device's AMSNetId: by convention the WiFi IP with ".1.1" appended. Register this
// exact id as a route on the PLC. The source AMS port is caller-chosen.
static AdsAmsAddr g_source;
static uint16_t g_invoke = 1;
static uint8_t c_req[256];
static uint8_t c_resp[512];

// Parse "a.b.c.d.e.f" into six octets. Returns false on a malformed id.
static bool parse_net_id(const char *s, uint8_t out[ADS_NET_ID_LEN])
{
    int v[ADS_NET_ID_LEN];
    if (sscanf(s, "%d.%d.%d.%d.%d.%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != ADS_NET_ID_LEN)
    {
        return false;
    }
    for (int i = 0; i < ADS_NET_ID_LEN; i++)
    {
        out[i] = (uint8_t)v[i];
    }
    return true;
}

static AdsRequest next_request()
{
    AdsRequest r;
    parse_net_id(PLC_NET_ID, r.target.net_id);
    r.target.port = PLC_PORT;
    r.source = g_source;
    r.invoke_id = g_invoke++;
    return r;
}

// Send one framed request, read one AMS/TCP-framed reply. Returns the total reply length.
static size_t exchange(int cid, size_t reqlen)
{
    if (reqlen == 0 || !Tcp.client->send(cid, c_req, reqlen))
    {
        return 0;
    }
    size_t got = 0;
    uint32_t deadline = millis() + 3000;
    // Read the 6-octet AMS/TCP header first (reserved(2) + length(4)).
    while (got < ADS_AMSTCP_HDR_LEN && millis() < deadline)
    {
        if (Tcp.client->available(cid))
        {
            got += Tcp.client->read(cid, c_resp + got, ADS_AMSTCP_HDR_LEN - got);
        }
    }
    if (got < ADS_AMSTCP_HDR_LEN)
    {
        return 0;
    }
    uint32_t frame =
        (uint32_t)c_resp[2] | ((uint32_t)c_resp[3] << 8) | ((uint32_t)c_resp[4] << 16) | ((uint32_t)c_resp[5] << 24);
    size_t total = ADS_AMSTCP_HDR_LEN + frame;
    if (frame < ADS_AMS_HDR_LEN || total > sizeof(c_resp))
    {
        return 0;
    }
    while (got < total && millis() < deadline)
    {
        if (Tcp.client->available(cid))
        {
            got += Tcp.client->read(cid, c_resp + got, total - got);
        }
    }
    return got == total ? total : 0;
}

static void run_client(const char *host)
{
    int cid = Tcp.client->open(host, ADS_TCP_PORT, 8000);
    if (cid < 0)
    {
        Serial.println("[ads] connect failed");
        return;
    }

    AdsRequest r;
    AdsAmsHeader h;
    size_t n;

    // 1) ReadDeviceInfo.
    r = next_request();
    n = exchange(cid, pc_ads_build_read_device_info(c_req, sizeof(c_req), &r));
    AdsDeviceInfo di;
    if (n && pc_ads_parse_ams_header(c_resp, n, &h) && pc_ads_parse_read_device_info(h.data, h.data_len, &di) &&
        di.result == 0)
    {
        Serial.printf("[ads] device: %s v%u.%u build %u\n", di.device_name, di.version_major, di.version_minor,
                      di.version_build);
    }
    else
    {
        Serial.println("[ads] ReadDeviceInfo failed");
    }

    // 2) ReadState.
    r = next_request();
    n = exchange(cid, pc_ads_build_read_state(c_req, sizeof(c_req), &r));
    AdsReadStateResult st;
    if (n && pc_ads_parse_ams_header(c_resp, n, &h) && pc_ads_parse_read_state(h.data, h.data_len, &st) &&
        st.result == 0)
    {
        const char *name = st.pc_ads_state == (uint16_t)AdsState::run      ? "RUN"
                           : st.pc_ads_state == (uint16_t)AdsState::stop   ? "STOP"
                           : st.pc_ads_state == (uint16_t)AdsState::config ? "CONFIG"
                                                                           : "?";
        Serial.printf("[ads] state: %s (%u)\n", name, st.pc_ads_state);
    }
    else
    {
        Serial.println("[ads] ReadState failed");
    }

    // 3) ReadWrite: resolve the symbol name to a handle.
    r = next_request();
    n = exchange(cid, pc_ads_build_read_write(c_req, sizeof(c_req), &r, ADS_IGRP_SYM_HND_BY_NAME, 0, 4,
                                              (const uint8_t *)SYMBOL, (uint32_t)strlen(SYMBOL)));
    AdsReadResult rr;
    if (!n || !pc_ads_parse_ams_header(c_resp, n, &h) || !pc_ads_parse_read(h.data, h.data_len, &rr) ||
        rr.result != 0 || rr.len < 4)
    {
        Serial.printf("[ads] handle for '%s' failed\n", SYMBOL);
        Tcp.client->close(cid);
        return;
    }
    uint32_t handle = (uint32_t)rr.data[0] | ((uint32_t)rr.data[1] << 8) | ((uint32_t)rr.data[2] << 16) |
                      ((uint32_t)rr.data[3] << 24);

    // 4) Read the symbol value (INT32) by handle.
    r = next_request();
    n = exchange(cid, pc_ads_build_read(c_req, sizeof(c_req), &r, ADS_IGRP_SYM_VAL_BY_HANDLE, handle, 4));
    if (n && pc_ads_parse_ams_header(c_resp, n, &h) && pc_ads_parse_read(h.data, h.data_len, &rr) && rr.result == 0 &&
        rr.len >= 4)
    {
        int32_t val = (int32_t)((uint32_t)rr.data[0] | ((uint32_t)rr.data[1] << 8) | ((uint32_t)rr.data[2] << 16) |
                                ((uint32_t)rr.data[3] << 24));
        Serial.printf("[ads] %s = %ld\n", SYMBOL, (long)val);
    }
    else
    {
        Serial.printf("[ads] read '%s' failed\n", SYMBOL);
    }

    // 5) Release the handle (Write the 4-octet handle to index group 0xF006).
    r = next_request();
    uint8_t hb[4] = {(uint8_t)handle, (uint8_t)(handle >> 8), (uint8_t)(handle >> 16), (uint8_t)(handle >> 24)};
    exchange(cid, pc_ads_build_write(c_req, sizeof(c_req), &r, ADS_IGRP_SYM_RELEASE_HANDLE, 0, hb, 4));

    Tcp.client->close(cid);
    Serial.println("[ads] done");
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
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Build this device's AMSNetId from its IP (add this as a route on the PLC).
    g_source.net_id[0] = (uint8_t)(ip & 0xFF);
    g_source.net_id[1] = (uint8_t)((ip >> 8) & 0xFF);
    g_source.net_id[2] = (uint8_t)((ip >> 16) & 0xFF);
    g_source.net_id[3] = (uint8_t)((ip >> 24) & 0xFF);
    g_source.net_id[4] = 1;
    g_source.net_id[5] = 1;
    g_source.port = 32905; // arbitrary caller AMS port
    Serial.printf("This device AMSNetId: %u.%u.%u.%u.1.1  (add as a route on the PLC)\n", (unsigned)(ip & 0xFF),
                  (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    static bool done = false;
    if (!done && millis() > 2000)
    {
        done = true;
        run_client(PLC_IP); // Tcp.client->open resolves the dotted-quad host directly
    }
    delay(10);
}
