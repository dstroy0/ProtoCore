// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SSHHostKey.ino
 * @brief Provision an SSH host key two ways: embedded at compile time, or flashed
 *        to NVS as a runtime service.
 *
 * A server's host key is what lets a client trust it is really talking to this
 * device (it signs the key exchange). This example shows the two ways to give the
 * server one, both starting from a key pair you generate on your workstation with:
 *
 *     python3 tools/gen_ssh_host_key.py --type ed25519 \
 *         --header examples/L5-Session/SSHHostKey/host_key.h --symbol HOST_KEY_SEED
 *     python3 tools/gen_ssh_host_key.py --type rsa            # for the NVS path
 *
 * The script prints a .pub line - add it to your client's ~/.ssh/known_hosts.
 *
 *   HOST_KEY_PROVISION 1  (default) - COMPILE TIME. The 32-byte Ed25519 seed in
 *       host_key.h is compiled into the firmware and installed with
 *       SshHostkey.ed25519_set(). Simplest; the key ships inside the image.
 *
 *   HOST_KEY_PROVISION 2  - RUNTIME SERVICE. The RSA-2048 private key lives in NVS
 *       (namespace "ssh_host_key", key "priv_der"); pc_ssh_rsa_load_pubkey() loads its
 *       public half at boot and each signature reads the private key into a stack
 *       buffer and wipes it (never held in RAM). Write it once (see the block below,
 *       or docs/SSH.md "Host key provisioning"), then the firmware just loads it.
 *
 * NOTE: the sketch has an inline PUBLIC DEMO seed so it builds out of the box. It
 * authenticates nothing. The command above writes host_key.h, which this sketch
 * picks up automatically (__has_include) and uses instead - generate your own and
 * keep the private key secret before using this anywhere real.
 *
 * Connect:  ssh -p 22 admin@<ip>     (password: s3cret; the server echoes input)
 */

#define PC_ENABLE_SSH 1

// 1 = embed an Ed25519 key at compile time; 2 = load an RSA key from NVS at boot.
#define HOST_KEY_PROVISION 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/presentation/ssh/auth/ssh_auth.h"
#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "network_drivers/tls/ssh_rsa.h"          // pc_ssh_rsa_load_pubkey (NVS path)
#include "network_drivers/presentation/ssh/transport/ssh_transport.h" // SshHostkey.ed25519_set (embed path)

#if HOST_KEY_PROVISION == 1
// Prefer a generated key if you made one (gen_ssh_host_key.py ... --header host_key.h);
// otherwise fall back to an inline PUBLIC DEMO seed so the example always builds.
#if defined(__has_include) && __has_include("host_key.h")
#include "host_key.h"
#else
static const uint8_t HOST_KEY_SEED[] = {0x96, 0x77, 0x0d, 0xf4, 0x8f, 0x2c, 0x40, 0xb9, 0xf8, 0xb1, 0xb4,
                                        0x60, 0xa0, 0xc0, 0x85, 0xb8, 0x16, 0x2d, 0xbe, 0xbb, 0x6d, 0xe1,
                                        0xd5, 0x86, 0x41, 0xc3, 0xb7, 0xc5, 0x8b, 0x07, 0x65, 0xeb};
#endif
#endif

// Mode 2 one-time NVS write (opt-in). Headers stay at file scope, not in a function.
#if HOST_KEY_PROVISION == 2 && defined(PROVISION_WRITE_ONCE)
#include "host_key_der.h" // gen_ssh_host_key.py --type rsa --header host_key_der.h --symbol HOST_KEY_DER
#include <Preferences.h>
#endif

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static bool ssh_password_auth(const char *user, const char *pass)
{
    return strcmp(user, "admin") == 0 && strcmp(pass, "s3cret") == 0; // demo only
}

static void ssh_on_data(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len)
{
    SshProto.send(slot, channel, data, len); // echo
}

// Install the host key. Returns true on success. The two provisioning paths differ
// only here; everything downstream (auth, channels) is identical.
static bool install_host_key()
{
#if HOST_KEY_PROVISION == 1
    // Compile-time: the seed is embedded in the firmware image.
    SshHostkey.ed25519_set(HOST_KEY_SEED);
    Serial.println("Host key: Ed25519, embedded at compile time");
    return true;
#else
    // Runtime service: the RSA key is stored in NVS. To write it ONCE, generate an
    // RSA header (see the include above) and build once with -DPROVISION_WRITE_ONCE:
#ifdef PROVISION_WRITE_ONCE
    Preferences p;
    p.begin("ssh_host_key", false);
    p.putBytes("priv_der", HOST_KEY_DER, HOST_KEY_DER_LEN); // key name MUST be "priv_der"
    p.end();
    Serial.println("Host key: wrote RSA DER to NVS (remove PROVISION_WRITE_ONCE and re-flash)");
#endif
    // Normal boot: load the public half from NVS (private key stays out of RAM).
    if (pc_ssh_rsa_load_pubkey() != 0)
    {
        Serial.println("Host key: none in NVS - flash once with PROVISION_WRITE_ONCE (see docs/SSH.md)");
        return false;
    }
    Serial.println("Host key: RSA, loaded from NVS");
    return true;
#endif
}

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

    if (!install_host_key())
    {
        return;
    }

    SshAuth.set_password_cb(ssh_password_auth);
    SshChannels.set_data_cb(ssh_on_data);

    listen(22, PROTO_SSH);
    int32_t result = begin();
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    SshProto.setup();
    Serial.println("SSH server started on port 22");
}

void loop()
{
    handle();
}
