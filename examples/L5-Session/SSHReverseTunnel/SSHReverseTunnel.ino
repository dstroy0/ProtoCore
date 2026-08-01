// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SSHReverseTunnel.ino
 * @brief Reverse-SSH tunnel: reach a device behind NAT over a public relay.
 *
 * The mirror of the SSH server. The device dials OUT to a relay with a public
 * address (any OpenSSH host), authenticates with its own ed25519 key, and asks
 * the relay to forward a port back (tcpip-forward, RFC 4254 sec 7.1 - the `ssh -R`
 * seam). A connection to the relay's forwarded port is tunnelled to a local
 * service on the device - here its own web server on :80. So the device stays
 * reachable from anywhere without a public IP or an inbound firewall hole.
 *
 * On the relay (a normal OpenSSH server), after adding the device's key below:
 *     curl http://127.0.0.1:8022/        # -> the device's web server, over the tunnel
 *
 * KEY POINT - run the tunnel in its OWN task with enough stack. The handshake's
 * field arithmetic (curve25519/ed25519, and ML-KEM if PC_ENABLE_PQC_KEX) runs
 * in the caller's task and peaks at ~10.5 KB (classical) or ~16 KB (hybrid).
 * loop()'s default 8 KB is NOT enough - create a dedicated >= 20 KB task and call
 * begin()+poll() from it (begin() also claims a private scratch arena for that
 * task, so poll() must run in the same task).
 *
 * Provision once: build, open the serial monitor, copy the printed
 * "ssh-ed25519 ..." public key into the relay's ~/.ssh/authorized_keys, and set
 * RELAY_HOST / HOST_PIN below. Get the pin (SHA-256 of the relay's host-key blob):
 *     awk '{print $2}' /etc/ssh/ssh_host_ed25519_key.pub | base64 -d | sha256sum
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/presentation/ssh/connection/ssh_client.h"
#include "shared_primitives/mime.h"

// ---- Provisioning: replace these for your network + relay ----
static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASS = "your-pass";
static const char *RELAY_HOST = "relay.example.com"; // your public OpenSSH relay
static const char *RELAY_USER = "tunnel";

// The device's ssh-ed25519 private seed (32 bytes). Generate 32 random bytes, keep
// them secret, and add the public key (printed at boot) to the relay's
// authorized_keys. The zeros below are a placeholder - the tunnel will not
// authenticate until you replace them.
static const uint8_t AUTH_SEED[32] = {0};

// The relay's host-key pin: SHA-256 of its host-key blob (see the command above).
// The handshake aborts if the relay presents any other key (no trust-on-first-use).
static const uint8_t HOST_PIN[32] = {0};


static void root(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_text(slot, 200, PC_MIME_TEXT_PLAIN, "hello from the device behind NAT, over the reverse SSH tunnel\n");
}

// Dedicated task: enough stack for the KEX, and the same task owns begin()+poll().
static void tunnel_task(void *)
{
    pc_ssh_tunnel_cfg cfg = {};
    cfg.host = RELAY_HOST;
    cfg.port = 22;
    cfg.user = RELAY_USER;
    cfg.auth_seed = AUTH_SEED;
    cfg.host_pin = HOST_PIN;
    cfg.bind_addr = "";   // relay binds the forward on localhost (GatewayPorts no)
    cfg.bind_port = 8022; // relay listens here; connections tunnel back to us
    cfg.local_port = 80;  // bridged to our own web server
    pc_ssh_tunnel_begin(&cfg);
    for (;;)
    {
        pc_ssh_tunnel_poll();
        // Reconnect with a backoff if the relay drops us or the pin/auth fails.
        if (pc_ssh_tunnel_state_get() == pc_ssh_tunnel_state::PC_TUN_FAILED)
        {
            delay(5000);
            pc_ssh_tunnel_begin(&cfg);
        }
        delay(5);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    init_wifi_physical(WIFI_SSID, WIFI_PASS);
    while (!wifi_ready())
    {
        delay(200);
    }

    // Print our public key so you can add it to the relay's authorized_keys.
    uint8_t pub[32];
    pc_ssh_tunnel_pubkey(AUTH_SEED, pub);
    Serial.print("device ssh-ed25519 raw pubkey (add to relay authorized_keys): ");
    for (int i = 0; i < 32; i++)
    {
        Serial.printf("%02x", pub[i]);
    }
    Serial.println();

    on_http("/", HttpMethod::HTTP_GET, root);
    begin_http(80);

    // 20 KB stack covers the ML-KEM hybrid handshake; pin to core 0 (off the loop).
    xTaskCreatePinnedToCore(tunnel_task, "pc_ssh_tun", 20480, nullptr, 5, nullptr, 0);
}

void loop()
{
    handle();
    if (pc_ssh_tunnel_up())
    {
        static bool announced = false;
        if (!announced)
        {
            Serial.println("reverse tunnel up - curl the relay's forwarded port to reach this device");
            announced = true;
        }
    }
    delay(2);
}
