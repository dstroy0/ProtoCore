// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SSHSftp.ino
 * @brief SFTP (and SCP) file server over SSH (PC_ENABLE_SSH_SFTP / _SCP).
 *
 * The board serves files from a LittleFS partition over the one authenticated SSH port: a client's
 * `sftp` (or `scp`) session reads/writes/lists files under a mount root. This is the standards-track
 * southbound path for dropping files (e.g. NC / G-code programs) onto the device securely.
 *
 * It is the SSH server example (SSH) plus two lines: mount a filesystem and call
 * pc_ssh_sftp_begin(fs, root). The SFTP subsystem + SCP exec attach to the existing SSH channel layer.
 *
 * Provision an RSA host key in NVS first (see docs/SSH.md "Host key provisioning"), then connect:
 *   sftp -P 22 admin@<ip>            # then: put file / get file / ls / mkdir / rm / rename
 *   scp -P 22 localfile admin@<ip>:/f
 *   scp -P 22 admin@<ip>:/f out
 *
 * NOTE (PlatformIO): the SFTP server is compiled into the *library*, so the flags must reach the whole
 * build: -DPC_ENABLE_SSH=1 -DPC_ENABLE_FILE_SERVING=1 -DPC_ENABLE_SSH_SFTP=1 (+ _SCP for scp).
 * In the Arduino IDE they are set for you in build_opt.h.
 */

#define PC_ENABLE_SSH 1
#define PC_ENABLE_FILE_SERVING 1
#define PC_ENABLE_SSH_SFTP 1
#define PC_ENABLE_SSH_SCP 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/presentation/ssh/auth/ssh_auth.h"
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "network_drivers/presentation/ssh/crypto/ssh_rsa.h"
#include "server/ssh_scp.h"
#include "server/ssh_sftp.h"
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

static bool ssh_password_auth(const char *user, const char *pass)
{
    return strcmp(user, "admin") == 0 && strcmp(pass, "s3cret") == 0; // illustrative only
}

void setup()
{
    Serial.begin(115200);

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Mount the filesystem SFTP serves (format on first boot). Any fs::FS works (SD, LittleFS, SPIFFS).
    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS mount failed");
        return;
    }

    if (pc_ssh_rsa_load_pubkey() != 0)
    {
        Serial.println("No SSH host key in NVS - see docs/SSH.md (Host key provisioning)");
        return;
    }
    pc_ssh_auth_set_password_cb(ssh_password_auth);

    server.listen(22, ConnProto::PROTO_SSH);
    if (server.begin() < 0)
    {
        Serial.println("begin() failed");
        return;
    }
    pc_ssh_conn_setup();

    // Serve SFTP + SCP from the whole LittleFS volume. A "subsystem sftp" request opens an SFTP session;
    // `scp localfile admin@<ip>:/path` drops a file onto the volume.
    pc_ssh_sftp_begin(LittleFS, "/");
    pc_ssh_scp_begin(LittleFS, "/");

    Serial.println("SFTP/SCP server started: sftp -P 22 admin@<ip> ; scp file admin@<ip>:/path");
}

void loop()
{
    server.handle();
}
