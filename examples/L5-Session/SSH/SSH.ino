// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SSH.ino
 * @brief SSH server example: host key from NVS, auth callbacks, channel echo.
 *
 * Demonstrates the SSH server stack (RFC 4253/4252/4254):
 *   - Enabling SSH (PC_ENABLE_SSH) and listening on ConnProto::PROTO_SSH
 *   - Loading the RSA-2048 host key from NVS (see docs/SSH.md "Host key
 *     provisioning" - you must store a DER key under namespace "ssh_host_key",
 *     key "priv_der" once per device before this runs)
 *   - Password auth (pc_ssh_auth_set_password_cb) and publickey auth
 *     (pc_ssh_auth_set_pubkey_cb)
 *   - A channel data callback that echoes received bytes back to the client
 *     with pc_ssh_conn_send()
 *   - Optional TCP port forwarding via the ssh_forward owner, gated by
 *     PC_SSH_PORT_FORWARD (off here; see the block below to enable it):
 *     local (ssh -L, outbound) AND remote (ssh -R, a listener on the device that
 *     tunnels back to the client) - pc_ssh_forward_begin() enables both.
 *
 * Hardening: define PC_SSH_ALLOW_PASSWORD 0 to compile password auth out and
 * accept publickey only. Failed attempts are bounded by SSH_MAX_AUTH_ATTEMPTS.
 *
 * Connect with:  ssh -p 22 admin@<ip>      (password below)
 * Then type; the server echoes each line back over the channel.
 * ssh -L (local):   ssh -L 8080:example.com:80 admin@<ip>   then curl localhost:8080
 * ssh -R (remote):  ssh -R 8080:localhost:9000 admin@<ip>   then a connection to
 *                   <ip>:8080 tunnels back to your local :9000.
 */

// Enable the SSH stack for this sketch (overrides the default-off config).
#define PC_ENABLE_SSH 1

// To demonstrate TCP port forwarding (ssh -L), uncomment these: the channel pool
// must hold the shell + the tunnel(s), and the outbound client pool must cover the
// concurrent forwards (PC_CLIENT_CONNS >= PC_SSH_FWD_MAX).
// #define PC_SSH_MAX_CHANNELS 4
// #define PC_SSH_PORT_FORWARD 1
// #define PC_CLIENT_CONNS 3
// #define PC_SSH_FWD_MAX 3

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/presentation/ssh/auth/ssh_auth.h"
#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "network_drivers/presentation/ssh/connection/ssh_forward.h"
#include "network_drivers/tls/ssh_rsa.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// --- Authentication callbacks ----------------------------------------------

// Return true to accept the username/password. Use a constant-time compare and
// real credential storage in production; this is illustrative only.
static bool ssh_password_auth(const char *user, const char *pass)
{
    return strcmp(user, "admin") == 0 && strcmp(pass, "s3cret") == 0;
}

// Return true if (user, public-key blob) is authorized. Compare the raw blob
// against your authorized_keys (the server verifies the client's signature
// itself once you accept the key here).
static bool ssh_pubkey_auth(const char *user, const uint8_t *blob, size_t blob_len)
{
    (void)blob;
    (void)blob_len;
    // e.g. return user_key_matches(user, blob, blob_len);
    return strcmp(user, "admin") == 0; // accept any key for "admin" in this demo
}

// --- Channel data: echo received bytes back to the client -------------------

static void ssh_on_data(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len)
{
    pc_ssh_conn_send(slot, channel, data, len); // echo back on the same channel
}

#if PC_SSH_PORT_FORWARD
// --- Forward policy: which ssh -L targets are allowed --------------------------
// Any authenticated client can otherwise ask the board to connect anywhere (an
// open proxy). Return true to permit a target; restrict it to what you intend.
static bool pc_ssh_forward_policy(const char *host, uint16_t port)
{
    return port == 80 || port == 443; // demo: allow only outbound web
}
#endif

void setup()
{
    Serial.begin(115200);

    Physical.wifi->init(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Load the RSA host key's public half from NVS (the private key is read
    // per-signature into a stack buffer and wiped; never held in static RAM).
    if (pc_ssh_rsa_load_pubkey() != 0)
    {
        Serial.println("No SSH host key in NVS - see docs/SSH.md (Host key provisioning)");
        return;
    }

    // Install SSH callbacks before begin().
    pc_ssh_auth_set_password_cb(ssh_password_auth);
    pc_ssh_auth_set_pubkey_cb(ssh_pubkey_auth);
    pc_ssh_channel_set_data_cb(ssh_on_data);

    // Listen for SSH on port 22 (and, optionally, HTTP on 80 alongside it).
    listen(22, PROTO_SSH);
    int32_t result = begin();
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }

    // One-time wiring of the SSH dispatcher's outbound path. Call after begin().
    pc_ssh_conn_setup();

#if PC_SSH_PORT_FORWARD
    // Enable forwarding (opt-in; nothing is forwarded until this runs). This turns on
    // BOTH local (ssh -L, gated by the policy below) and remote (ssh -R, a listener the
    // client asks the device to open). For ssh -R also raise PC_SSH_MAX_CHANNELS and,
    // if you expect concurrent tunnels, PC_SSH_RFWD_MAX / PC_SSH_RFWD_BRIDGE_MAX.
    pc_ssh_forward_set_policy_cb(pc_ssh_forward_policy);
    pc_ssh_forward_begin();
    Serial.println("SSH port forwarding enabled (ssh -L to 80/443; ssh -R listeners)");
#endif

    Serial.println("SSH server started on port 22");
}

void loop()
{
    // Drives accept/rx for every listener, including the ConnProto::PROTO_SSH handshake,
    // user-auth, and channel data pumping.
    handle();
}
