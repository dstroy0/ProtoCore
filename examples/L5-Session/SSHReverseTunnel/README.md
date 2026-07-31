# SSHReverseTunnel - reach a device behind NAT over a public relay

**Layer:** L5 Session · **Build flags:** `PC_ENABLE_SSH`, `PC_ENABLE_SSH_CLIENT`

## What this example teaches

This is the mirror of the SSH _server_. Instead of accepting inbound SSH, the
device is the SSH **client**: it dials OUT to a relay with a public address (any
OpenSSH host), authenticates with its own ed25519 key, and asks the relay to
forward a port back to it (`tcpip-forward`, RFC 4254 §7.1 - the `ssh -R` seam).
A connection to the relay's forwarded port is tunnelled to a local service on the
device. So a device on a home LAN, behind CGNAT, or inside a firewall stays
reachable from anywhere - no public IP, no inbound port opened.

Here the local service is the device's own web server on `:80`. On the relay:

```sh
curl http://127.0.0.1:8022/      # -> the device's web server, over the tunnel
```

## Run the tunnel in its own task (important)

The handshake's field arithmetic runs in the caller's task and needs real stack:
curve25519/ed25519 peak ~10.5 KB, and the ML-KEM hybrid (`PC_ENABLE_PQC_KEX`)
~16 KB. The Arduino `loop()` task's default 8 KB is **not enough** - create a
dedicated task and drive `begin()`/`poll()` from it (a build guard enforces the
matching worker-stack floor):

```cpp
static void tunnel_task(void *) {
    pc_ssh_tunnel_cfg cfg = {};
    cfg.host = RELAY_HOST; cfg.port = 22; cfg.user = RELAY_USER;
    cfg.auth_seed = AUTH_SEED;   // the device's ed25519 seed
    cfg.host_pin  = HOST_PIN;    // SHA-256 of the relay's host-key blob (pinned)
    cfg.bind_port = 8022;        // relay listens here
    cfg.local_port = 80;         // tunnelled to our web server
    pc_ssh_tunnel_begin(&cfg);
    for (;;) { pc_ssh_tunnel_poll(); delay(5); }
}
// 20 KB stack covers the hybrid KEX; begin() and poll() must share this task.
xTaskCreatePinnedToCore(tunnel_task, "pc_ssh_tun", 20480, nullptr, 5, nullptr, 0);
```

## The relay's host key is pinned

There is no trust-on-first-use. You supply `host_pin` = the SHA-256 of the relay's
host-key blob, and the handshake aborts if the relay presents anything else. It is
type-agnostic (works whether the relay's key is ed25519, ECDSA or RSA). Get it:

```sh
awk '{print $2}' /etc/ssh/ssh_host_ed25519_key.pub | base64 -d | sha256sum
```

## Provisioning

1. Set `WIFI_SSID`/`WIFI_PASS`, `RELAY_HOST`, `RELAY_USER`, and `HOST_PIN`.
2. Generate 32 random bytes for `AUTH_SEED` (the device's private ed25519 seed).
3. Build, flash, open the serial monitor: it prints the device's public key. Add
   the matching `ssh-ed25519 ...` line to the relay's `~/.ssh/authorized_keys`.
4. `curl` the relay's forwarded port - you reach the device.

## Build

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_SSH=1 -DPC_ENABLE_SSH_CLIENT=1 -DPC_SSH_CLIENT_MAX_CHANNELS=2 -DPC_CLIENT_RX_BUF=2048 -DMAX_CONNS=4" \
  --lib="." examples/L5-Session/SSHReverseTunnel/SSHReverseTunnel.ino
```

The dialed-down channel count and client RX buffer keep this within the classic
ESP32's ~122 KB internal DRAM (the reverse client holds a relay connection plus
one bridge per channel); a PSRAM/large-SRAM board uses the roomier per-variant
defaults from its board profile, and add `-DPC_ENABLE_PQC_KEX=1` for the ML-KEM
hybrid (run the tunnel task with a >= 20 KB stack, as above).

## What it negotiates

The client offers the full modern suite and interoperates with any current SSH
server: KEX `mlkem768x25519-sha256` (when PQC is built), `curve25519-sha256`,
`ecdh-sha2-nistp256`, `diffie-hellman-group14-sha256`; host keys ed25519 / ECDSA /
RSA (all pinned); ciphers chacha20-poly1305 / aes256-gcm / aes256-ctr. Forwarded
connections are bridged through a channel pool (`PC_SSH_CLIENT_MAX_CHANNELS`), so
concurrent requests each get a channel.

HW-verified against OpenSSH 10.0 on an ESP32-S3: handshake + ed25519 auth +
`tcpip-forward` + the forwarded-tcpip bridge to `:80`, concurrent and
rapid-sequential requests returning the device's response byte-for-byte.
