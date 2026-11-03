// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SmbFileClient.ino
 * @brief Read a file off a Windows / Samba share with the SMB2 client (PROTOCORE_ENABLE_SMB).
 *
 * At boot the board joins WiFi, connects to an SMB server on port 445, authenticates with
 * NTLMv2, opens a file on a share, prints the first chunk to Serial, and closes it. This is
 * the CNC use case: pull a `.nc` program from the shop's file server. The README beside this
 * sketch walks you - from scratch - through standing up a Samba share on a Raspberry Pi to
 * serve the file (no prior Windows-networking experience needed).
 *
 * `smb_client` is written against a send/recv seam, so this sketch shows the one piece of glue
 * a real device needs: `cl_send` / `cl_recv` that move bytes over the shared outbound TCP
 * transport (`protocore_client`). Any transport works the same way.
 *
 * Edit the lines marked "CHANGE ME" below, flash, and open Serial @ 115200.
 *
 * NOTE (PlatformIO): SMB is compiled into the *library*, so the flag must reach the whole
 * build: `build_flags = -DPROTOCORE_ENABLE_SMB=1`. In the Arduino IDE it is already set for you in
 * build_opt.h beside this sketch.
 */

#define PROTOCORE_ENABLE_SMB 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "network_drivers/application/smb/smb2/smb2.h" // SMB2_FILE_GENERIC_READ / SMB2_FILE_OPEN
#include "network_drivers/application/smb/smb_client/smb_client.h" // smb_open / smb_read / smb_close


// --- CHANGE ME: your WiFi ---
static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// --- CHANGE ME: your SMB server + credentials (see the README to set up Samba on a Raspberry Pi) ---
static const char *SMB_HOST = "192.168.1.50"; // the file server's IP address
static const uint16_t SMB_PORT = 445;         // SMB over Direct TCP
static const char *SMB_USER = "cnc";
static const char *SMB_PASS = "secretpassword";
static const char *SMB_DOMAIN = "";                          // empty for a local Samba account
static const char *SMB_SHARE = "\\\\192.168.1.50\\programs"; // the UNC path to the share
static const char *SMB_PATH = "PART001.NC";                  // the file on the share to read

// The SMB engine's transport seam, bound to protocore_client. `deadline` bounds each recv wait.
struct SmbXport
{
    int cid;
    uint32_t deadline;
};

static int cl_send(void *ctx, const uint8_t *data, size_t len)
{
    SmbXport *x = (SmbXport *)ctx;
    size_t sent = 0;
    while (sent < len)
    {
        size_t chunk = len - sent;
        if (chunk > 0xFFFF)
        {
            chunk = 0xFFFF;
        }
        TcpClientV.cid = x->cid;
        TcpClientV.io.data = data + sent;
        TcpClientV.io.len = chunk;
        TcpClient.send(protocore_tcp_client_span());
        if (!TcpClientV.ok)
        {
            return -1;
        }
        sent += chunk;
    }
    return (int)len;
}

static int cl_recv(void *ctx, uint8_t *buf, size_t cap)
{
    SmbXport *x = (SmbXport *)ctx;
    while ((int32_t)(x->deadline - millis()) > 0)
    {
        TcpClientV.cid = x->cid;
        TcpClientV.io.buf = buf;
        TcpClientV.io.cap = cap;
        TcpClient.read(protocore_tcp_client_span());
        size_t n = TcpClientV.n;
        if (n > 0)
        {
            return (int)n;
        }
        TcpClientV.cid = x->cid;
        TcpClient.is_closed(protocore_tcp_client_span());
        const bool closed = TcpClientV.ok;
        TcpClient.available(protocore_tcp_client_span());
        if (closed && TcpClientV.n == 0)
        {
            return -1;
        }
        delay(5);
    }
    return -1; // timeout
}

void read_program()
{
    TcpClientV.dial.host = SMB_HOST;
    TcpClientV.dial.port = SMB_PORT;
    TcpClientV.dial.timeout_ms = 8000;
    TcpClient.open(protocore_tcp_client_span());
    int cid = TcpClientV.i32;
    if (cid < 0)
    {
        Serial.println("connect failed - is the server reachable on port 445?");
        return;
    }
    SmbXport x = {cid, 0};

    SmbConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.user = SMB_USER;
    cfg.pass = SMB_PASS;
    cfg.domain = SMB_DOMAIN;
    cfg.workstation = "esp32";
    cfg.share = SMB_SHARE;
    cfg.path = SMB_PATH;
    cfg.desired_access = SMB2_FILE_GENERIC_READ;
    cfg.disposition = SMB2_FILE_OPEN; // open an existing file, fail if absent

    SmbHandle h;
    x.deadline = millis() + 8000;
    SmbResult rc = smb_open(&cfg, &h, cl_send, cl_recv, &x);
    if (rc != SMB_OK)
    {
        Serial.printf("smb_open failed (SmbResult %d) - see the README troubleshooting table\n", (int)rc);
        TcpClientV.cid = cid;
        TcpClient.close(protocore_tcp_client_span());
        return;
    }
    Serial.printf("opened %s (%llu bytes)\n", SMB_PATH, (unsigned long long)h.file_size);

    uint8_t buf[1024];
    size_t got = 0;
    x.deadline = millis() + 8000;
    rc = smb_read(&h, 0, buf, sizeof(buf), &got, cl_send, cl_recv, &x);
    if (rc == SMB_OK)
    {
        Serial.printf("--- first %u bytes ---\n", (unsigned)got);
        Serial.write(buf, got);
        Serial.println("\n--- end ---");
        // To read a larger file, loop smb_read with a growing offset until got == 0.
        // To upload instead, open with SMB2_FILE_GENERIC_WRITE + SMB2_FILE_OVERWRITE_IF
        // and call smb_write(&h, 0, data, len, &wrote, cl_send, cl_recv, &x).
    }
    else
    {
        Serial.printf("smb_read failed (SmbResult %d)\n", (int)rc);
    }

    x.deadline = millis() + 8000;
    smb_close(&h, cl_send, cl_recv, &x);
    TcpClientV.cid = cid;
    TcpClient.close(protocore_tcp_client_span());
}

void setup()
{
    Serial.begin(115200);

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    while (!protocore_physical_wifi_up())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = protocore_physical_egress_ipv4(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    read_program();
}

void loop()
{
    delay(1000);
}
