// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file AdsClient.ino
 * @brief Beckhoff ADS client - read a TwinCAT PLC over AMS/TCP (PROTOCORE_ENABLE_ADS).
 *
 * services/fieldbus/ads builds ADS/AMS requests and parses the responses; it is transport-
 * agnostic, so the app owns the socket. This sketch opens the library's outbound TCP client
 * (protocore_client) to a TwinCAT router on TCP 48898 and runs a small ADS sequence against it:
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
 * Build flags (platformio.ini):  build_flags = -DPROTOCORE_ENABLE_ADS=1 -DPROTOCORE_ENABLE_TCP_CLIENT=1 -DPROTOCORE_ENABLE_DNS_RESOLVER=1
 */

#define PROTOCORE_ENABLE_ADS 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/tcp/client/client.h"
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
static uint8_t ads_work[16]; // the borrow an Ads entry takes; the codec carries no state, so it never reads it

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

// Read up to cap wire bytes from the client slot cid into buf, if any are buffered. Returns the count read.
static size_t client_read(int cid, uint8_t *buf, size_t cap)
{
    TcpClientV.cid = cid;
    TcpClient.available(protocore_tcp_client_span());
    if (!TcpClientV.n)
    {
        return 0;
    }
    TcpClientV.cid = cid;
    TcpClientV.io.buf = buf;
    TcpClientV.io.cap = cap;
    TcpClient.read(protocore_tcp_client_span());
    return TcpClientV.n;
}

// Parse one AMS/TCP-framed reply of n octets from c_resp into h. False on a malformed frame.
static bool parse_reply(size_t n, AdsAmsHeader *h)
{
    AdsV.parse_ams_header_args.buf = c_resp;
    AdsV.parse_ams_header_args.len = n;
    AdsV.parse_ams_header_args.out = h;
    Ads.parse_ams_header(ads_work);
    return AdsV.ok;
}

// Send one framed request, read one AMS/TCP-framed reply. Returns the total reply length.
static size_t exchange(int cid, size_t reqlen)
{
    if (reqlen == 0)
    {
        return 0;
    }
    TcpClientV.cid = cid;
    TcpClientV.io.data = c_req;
    TcpClientV.io.len = reqlen;
    TcpClient.send(protocore_tcp_client_span());
    if (!TcpClientV.ok)
    {
        return 0;
    }
    size_t got = 0;
    uint32_t deadline = millis() + 3000;
    // Read the 6-octet AMS/TCP header first (reserved(2) + length(4)).
    while (got < ADS_AMSTCP_HDR_LEN && millis() < deadline)
    {
        got += client_read(cid, c_resp + got, ADS_AMSTCP_HDR_LEN - got);
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
        got += client_read(cid, c_resp + got, total - got);
    }
    return got == total ? total : 0;
}

static void run_client(const char *host)
{
    TcpClientV.dial.host = host;
    TcpClientV.dial.port = ADS_TCP_PORT;
    TcpClientV.dial.timeout_ms = 8000;
    TcpClient.open(protocore_tcp_client_span());
    int cid = TcpClientV.i32;
    if (cid < 0)
    {
        Serial.println("[ads] connect failed");
        return;
    }
    // open() returns before the connection exists: step it until the handshake completes or it fails.
    for (;;)
    {
        TcpClientV.cid = cid;
        TcpClient.connected(protocore_tcp_client_span());
        if (TcpClientV.ok)
        {
            break;
        }
        TcpClientV.cid = cid;
        TcpClient.is_closed(protocore_tcp_client_span());
        if (TcpClientV.ok)
        {
            Serial.println("[ads] connect failed");
            TcpClientV.cid = cid;
            TcpClient.close(protocore_tcp_client_span());
            return;
        }
        delay(10);
    }

    AdsRequest r;
    AdsAmsHeader h;
    size_t n;

    // 1) ReadDeviceInfo.
    r = next_request();
    AdsV.build_read_device_info_args.buf = c_req;
    AdsV.build_read_device_info_args.cap = sizeof(c_req);
    AdsV.build_read_device_info_args.r = &r;
    Ads.build_read_device_info(ads_work);
    n = exchange(cid, AdsV.n);
    AdsDeviceInfo di;
    bool ok = n && parse_reply(n, &h);
    if (ok)
    {
        AdsV.parse_read_device_info_args.data = h.data;
        AdsV.parse_read_device_info_args.data_len = h.data_len;
        AdsV.parse_read_device_info_args.out = &di;
        Ads.parse_read_device_info(ads_work);
        ok = AdsV.ok;
    }
    if (ok && di.result == 0)
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
    AdsV.build_read_state_args.buf = c_req;
    AdsV.build_read_state_args.cap = sizeof(c_req);
    AdsV.build_read_state_args.r = &r;
    Ads.build_read_state(ads_work);
    n = exchange(cid, AdsV.n);
    AdsReadStateResult st;
    ok = n && parse_reply(n, &h);
    if (ok)
    {
        AdsV.parse_read_state_args.data = h.data;
        AdsV.parse_read_state_args.data_len = h.data_len;
        AdsV.parse_read_state_args.out = &st;
        Ads.parse_read_state(ads_work);
        ok = AdsV.ok;
    }
    if (ok && st.result == 0)
    {
        const char *name = st.protocore_ads_state == (uint16_t)ADS_STATE_RUN      ? "RUN"
                           : st.protocore_ads_state == (uint16_t)ADS_STATE_STOP   ? "STOP"
                           : st.protocore_ads_state == (uint16_t)ADS_STATE_CONFIG ? "CONFIG"
                                                                                  : "?";
        Serial.printf("[ads] state: %s (%u)\n", name, st.protocore_ads_state);
    }
    else
    {
        Serial.println("[ads] ReadState failed");
    }

    // 3) ReadWrite: resolve the symbol name to a handle.
    r = next_request();
    AdsV.build_read_write_args.buf = c_req;
    AdsV.build_read_write_args.cap = sizeof(c_req);
    AdsV.build_read_write_args.r = &r;
    AdsV.build_read_write_args.index_group = ADS_IGRP_SYM_HND_BY_NAME;
    AdsV.build_read_write_args.index_offset = 0;
    AdsV.build_read_write_args.read_len = 4;
    AdsV.build_read_write_args.write_data = (const uint8_t *)SYMBOL;
    AdsV.build_read_write_args.write_len = (uint32_t)strlen(SYMBOL);
    Ads.build_read_write(ads_work);
    n = exchange(cid, AdsV.n);
    AdsReadResult rr;
    ok = n && parse_reply(n, &h);
    if (ok)
    {
        AdsV.parse_read_args.data = h.data;
        AdsV.parse_read_args.data_len = h.data_len;
        AdsV.parse_read_args.out = &rr;
        Ads.parse_read(ads_work);
        ok = AdsV.ok;
    }
    if (!ok || rr.result != 0 || rr.len < 4)
    {
        Serial.printf("[ads] handle for '%s' failed\n", SYMBOL);
        TcpClientV.cid = cid;
        TcpClient.close(protocore_tcp_client_span());
        return;
    }
    uint32_t handle = (uint32_t)rr.data[0] | ((uint32_t)rr.data[1] << 8) | ((uint32_t)rr.data[2] << 16) |
                      ((uint32_t)rr.data[3] << 24);

    // 4) Read the symbol value (INT32) by handle.
    r = next_request();
    AdsV.build_read_args.buf = c_req;
    AdsV.build_read_args.cap = sizeof(c_req);
    AdsV.build_read_args.r = &r;
    AdsV.build_read_args.index_group = ADS_IGRP_SYM_VAL_BY_HANDLE;
    AdsV.build_read_args.index_offset = handle;
    AdsV.build_read_args.read_len = 4;
    Ads.build_read(ads_work);
    n = exchange(cid, AdsV.n);
    ok = n && parse_reply(n, &h);
    if (ok)
    {
        AdsV.parse_read_args.data = h.data;
        AdsV.parse_read_args.data_len = h.data_len;
        AdsV.parse_read_args.out = &rr;
        Ads.parse_read(ads_work);
        ok = AdsV.ok;
    }
    if (ok && rr.result == 0 && rr.len >= 4)
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
    AdsV.build_write_args.buf = c_req;
    AdsV.build_write_args.cap = sizeof(c_req);
    AdsV.build_write_args.r = &r;
    AdsV.build_write_args.index_group = ADS_IGRP_SYM_RELEASE_HANDLE;
    AdsV.build_write_args.index_offset = 0;
    AdsV.build_write_args.data = hb;
    AdsV.build_write_args.len = 4;
    Ads.build_write(ads_work);
    exchange(cid, AdsV.n);

    TcpClientV.cid = cid;
    TcpClient.close(protocore_tcp_client_span());
    Serial.println("[ads] done");
}

void setup()
{
    Serial.begin(115200);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
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
        run_client(PLC_IP); // TcpClient.open resolves the dotted-quad host directly
    }
    delay(10);
}
