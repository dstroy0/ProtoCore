# SSH - a zero-heap SSH server

**Layer:** L5 Session · **Build flags:** `PC_ENABLE_SSH`

## What this example teaches

This stands up a real SSH 2.0 server (RFC 4253/4252/4254) on port 22: it loads
an RSA host key from NVS, authenticates clients by password and/or public key,
and echoes channel data back. The crypto runs on the ESP32 hardware accelerator
and no connection state touches the heap.

**Listening on a non-HTTP protocol.** The same `PC` can host other L5
protocols. You open an SSH listener with `server.listen(port, PROTO_SSH)` and
then `begin()` (no port argument needed when you have explicitly added
listeners):

```cpp
server.listen(22, ConnProto::PROTO_SSH);
int32_t result = server.begin();
pc_ssh_conn_setup();   // one-time wiring of the SSH dispatcher's outbound path (after begin)
```

**The host key lives in NVS, not in RAM.** `pc_ssh_rsa_load_pubkey()` loads only the
public half at startup; the private key is read per-signature into a stack buffer
and wiped, so it is never held in static RAM. You must provision the DER key once
per device (namespace `ssh_host_key`, key `priv_der`) - see `docs/SSH.md`:

```cpp
if (pc_ssh_rsa_load_pubkey() != 0) {
    Serial.println("No SSH host key in NVS - see docs/SSH.md (Host key provisioning)");
    return;
}
```

**Auth via callbacks.** You decide who gets in. A password callback and a
public-key callback are installed before `begin()`; the server verifies the
client's signature itself once your pubkey callback accepts the key:

```cpp
pc_ssh_auth_set_password_cb(ssh_password_auth);  // return true to accept user/pass
pc_ssh_auth_set_pubkey_cb(ssh_pubkey_auth);      // return true to accept (user, key blob)
pc_ssh_channel_set_data_cb(ssh_on_data);         // bytes from the client
```

**Channel echo.** The data callback receives the channel id the bytes arrived on
and sends them back with `pc_ssh_conn_send(slot, channel, data, len)` - the skeleton
for a remote console. With `PC_SSH_MAX_CHANNELS > 1` the client can open several
channels over one connection and each is tagged by id.

**TCP port forwarding (`ssh -L`).** Define `PC_SSH_PORT_FORWARD` (and give it
channel + client-pool room) to let the board act as a local-forward tunnel: when a
client opens a `direct-tcpip` channel, the `ssh_forward` owner opens the outbound
TCP connection through the client transport and bridges bytes both ways. It is
opt-in twice over - compiled out by default, and inert until you call
`pc_ssh_forward_begin()` - because any authenticated client could otherwise make the
board connect anywhere (an open proxy). Restrict the reachable targets with a
policy callback:

```cpp
static bool pc_ssh_forward_policy(const char *host, uint16_t port) {
    return port == 80 || port == 443;   // allow only outbound web
}
pc_ssh_forward_set_policy_cb(pc_ssh_forward_policy);
pc_ssh_forward_begin();                     // after pc_ssh_conn_setup()
```

Then `ssh -L 8080:example.com:80 admin@<ip>` and `curl localhost:8080` reaches
`example.com` through the board.

**Hardening.** Define `PC_SSH_ALLOW_PASSWORD 0` to compile password auth out
entirely (publickey-only), and failed attempts are bounded by
`SSH_MAX_AUTH_ATTEMPTS`. Use a constant-time compare and real credential storage
in production - the demo's `strcmp` is illustrative only.

## Build and run

`PC_ENABLE_SSH` must reach the library build, so pass it as a build flag:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_SSH=1" \
  --lib="." examples/L5-Session/SSH/SSH.ino
```

To build with port forwarding, add the forwarding flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_SSH=1 -DPC_SSH_PORT_FORWARD=1 -DPC_SSH_MAX_CHANNELS=4 -DPC_CLIENT_CONNS=3 -DPC_SSH_FWD_MAX=3" \
  --lib="." examples/L5-Session/SSH/SSH.ino
```

Provision a host key first (see `docs/SSH.md`), then:

```sh
ssh -p 22 admin@<ip>     # password: s3cret  (type; the server echoes it back)

# with forwarding built in:
ssh -L 8080:example.com:80 admin@<ip>   # then in another shell:
curl http://localhost:8080/             # reaches example.com:80 through the board
```

## Annotated source

The complete sketch ([SSH.ino](SSH.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

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
#include "network_drivers/presentation/ssh/auth/ssh_auth.h"    // pc_ssh_auth_set_*_cb
#include "network_drivers/presentation/ssh/connection/ssh_channel.h" // pc_ssh_channel_set_data_cb
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"    // pc_ssh_conn_send / pc_ssh_conn_setup
#include "network_drivers/presentation/ssh/connection/ssh_forward.h" // pc_ssh_forward_begin (ssh -L)
#include "network_drivers/presentation/ssh/crypto/ssh_rsa.h"     // pc_ssh_rsa_load_pubkey

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

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
    pc_ssh_conn_send(slot, channel, data, len); // echo
}

#if PC_SSH_PORT_FORWARD
// Forward policy: which ssh -L targets are allowed (else an open proxy for any
// authenticated client). Return true to permit a target.
static bool pc_ssh_forward_policy(const char *host, uint16_t port)
{
    return port == 80 || port == 443; // demo: allow only outbound web
}
#endif

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
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
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
    server.listen(22, ConnProto::PROTO_SSH);
    int32_t result = server.begin();
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }

    // One-time wiring of the SSH dispatcher's outbound path. Call after begin().
    pc_ssh_conn_setup();

#if PC_SSH_PORT_FORWARD
    // Enable ssh -L forwarding (opt-in; nothing is forwarded until this runs).
    pc_ssh_forward_set_policy_cb(pc_ssh_forward_policy);
    pc_ssh_forward_begin();
    Serial.println("SSH port forwarding enabled (ssh -L to ports 80/443)");
#endif

    Serial.println("SSH server started on port 22");
}

void loop()
{
    // Drives accept/rx for every listener, including the PROTO_SSH handshake,
    // user-auth, and channel data pumping.
    server.handle();
}
```
