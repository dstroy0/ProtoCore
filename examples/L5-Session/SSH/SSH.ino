// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SSH.ino
 * @brief SSH server example: host key from NVS, auth callbacks, channel echo.
 *
 * Demonstrates the SSH server stack (RFC 4253/4252/4254):
 *   - Enabling SSH (PROTOCORE_ENABLE_SSH) and listening on ProtoConn::PROTO_SSH
 *   - Loading the RSA-2048 host key from NVS (see docs/SSH.md "Host key
 *     provisioning" - you must store a DER key under namespace "ssh_host_key",
 *     key "priv_der" once per device before this runs)
 *   - Password auth (protocore_ssh_auth_set_password_cb) and publickey auth
 *     (protocore_ssh_auth_set_pubkey_cb)
 *   - A channel data callback that echoes received bytes back to the client
 *     with protocore_ssh_conn_send()
 *   - Optional TCP port forwarding via the ssh_forward owner, gated by
 *     PROTOCORE_SSH_PORT_FORWARD (off here; see the block below to enable it):
 *     local (ssh -L, outbound) AND remote (ssh -R, a listener on the device that
 *     tunnels back to the client) - SshConnection.forward_begin() enables both.
 *
 * Hardening: define PROTOCORE_SSH_ALLOW_PASSWORD 0 to compile password auth out and
 * accept publickey only. Failed attempts are bounded by SSH_MAX_AUTH_ATTEMPTS.
 *
 * Connect with:  ssh -p 22 admin@<ip>      (password below)
 * Then type; the server echoes each line back over the channel.
 * ssh -L (local):   ssh -L 8080:example.com:80 admin@<ip>   then curl localhost:8080
 * ssh -R (remote):  ssh -R 8080:localhost:9000 admin@<ip>   then a connection to
 *                   <ip>:8080 tunnels back to your local :9000.
 */

// Enable the SSH stack for this sketch (overrides the default-off config).
#define PROTOCORE_ENABLE_SSH 1

// To demonstrate TCP port forwarding (ssh -L), uncomment these: the channel pool
// must hold the shell + the tunnel(s), and the outbound client pool must cover the
// concurrent forwards (PROTOCORE_CLIENT_CONNS >= PROTOCORE_SSH_FWD_MAX).
// #define PROTOCORE_SSH_MAX_CHANNELS 4
// #define PROTOCORE_SSH_PORT_FORWARD 1
// #define PROTOCORE_CLIENT_CONNS 3
// #define PROTOCORE_SSH_FWD_MAX 3

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/presentation/ssh/auth/auth.h"
#include "network_drivers/presentation/ssh/connection/connection.h"
#include "network_drivers/presentation/ssh/transport/ssh_rsa/ssh_rsa.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// --- Authentication callbacks ----------------------------------------------

// Return true to accept the username/password. Use a constant-time compare and
// real credential storage in production; this is illustrative only.
static proto_bool ssh_password_auth(const char *user, const char *pass)
{
    return strcmp(user, "admin") == 0 && strcmp(pass, "s3cret") == 0;
}

// Return true if (user, public-key blob) is authorized. Compare the raw blob
// against your authorized_keys (the server verifies the client's signature
// itself once you accept the key here).
static proto_bool ssh_pubkey_auth(const char *user, const uint8_t *blob, size_t blob_len)
{
    (void)blob;
    (void)blob_len;
    // e.g. return user_key_matches(user, blob, blob_len);
    return strcmp(user, "admin") == 0; // accept any key for "admin" in this demo
}

// --- Channel data: echo received bytes back to the client -------------------

static void ssh_on_data(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len)
{
    SshConnectionV.chan.slot = slot;
    SshConnectionV.chan.channel = channel;
    SshConnectionV.chan.data = data;
    SshConnectionV.chan.len = len;
    SshConnection.channel_send_data(protocore_ssh_connection_span()); // echo back on the same channel
}

#if PROTOCORE_SSH_PORT_FORWARD
// --- Forward policy: which ssh -L targets are allowed --------------------------
// Any authenticated client can otherwise ask the board to connect anywhere (an
// open proxy). Return true to permit a target; restrict it to what you intend.
static proto_bool protocore_ssh_forward_policy(const char *host, uint16_t port)
{
    return port == 80 || port == 443; // demo: allow only outbound web
}
#endif

void setup()
{
    Serial.begin(115200);

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Load the RSA host key's public half from NVS (the private key is read
    // per-signature into a stack buffer and wiped; never held in static RAM).
    SshRsa.load_pubkey(protocore_ssh_rsa_span());
    if (SshRsaV.n != 0)
    {
        Serial.println("No SSH host key in NVS - see docs/SSH.md (Host key provisioning)");
        return;
    }

    // Install SSH callbacks before proto_begin().
    SshAuthV.cbs.password_cb = ssh_password_auth;
    SshAuth.set_password_cb(protocore_ssh_auth_span());
    SshAuthV.cbs.pubkey_cb = ssh_pubkey_auth;
    SshAuth.set_pubkey_cb(protocore_ssh_auth_span());
    SshConnectionV.data_cb = ssh_on_data;
    SshConnection.set_data_cb(protocore_ssh_connection_span());

    // Listen for SSH on port 22 (and, optionally, HTTP on 80 alongside it).
    listen(22, PROTO_SSH);
    int32_t result = proto_begin(NULL);
    if (result < 0)
    {
        Serial.printf("proto_begin() failed (error %d)\n", result);
        return;
    }
    // The SSH dispatcher's outbound path is wired by the builtin PROTO_SSH handler; no setup call.

#if PROTOCORE_SSH_PORT_FORWARD
    // Enable forwarding (opt-in; nothing is forwarded until this runs). This turns on
    // BOTH local (ssh -L, gated by the policy below) and remote (ssh -R, a listener the
    // client asks the device to open). For ssh -R also raise PROTOCORE_SSH_MAX_CHANNELS and,
    // if you expect concurrent tunnels, PROTOCORE_SSH_RFWD_MAX / PROTOCORE_SSH_RFWD_BRIDGE_MAX.
    SshConnectionV.forward_policy_cb = protocore_ssh_forward_policy;
    SshConnection.set_forward_policy_cb(protocore_ssh_connection_span());
    SshConnection.forward_begin(protocore_ssh_connection_span());
    Serial.println("SSH port forwarding enabled (ssh -L to 80/443; ssh -R listeners)");
#endif

    Serial.println("SSH server started on port 22");
}

void loop()
{
    // Drives accept/rx for every listener, including the ProtoConn::PROTO_SSH handshake,
    // user-auth, and channel data pumping.
    handle();
}
