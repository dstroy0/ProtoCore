# SSH Support

ProtoCore includes a **complete SSH-2.0 server protocol**:
identification-string exchange, algorithm negotiation, Diffie-Hellman key
exchange, user authentication (password **and** publickey), and the
connection/channel protocol - all built on constant-memory, side-channel-aware
primitives.

The message state machine is driven by a single transport-agnostic dispatcher
([`ssh_server.cpp`](@ref ssh_server.cpp)) that
consumes decrypted message payloads and emits responses through a callback, so
it is fully unit-testable off-target. The TCP glue
([`ssh_conn.cpp`](@ref ssh_conn.cpp)) binds a
[`PROTO_SSH`](@ref PROTO_SSH) connection to a session slot, pumps ring-buffer bytes through the
banner exchange and binary-packet layer, and writes responses back to the
socket.

> **Handshake:** banner exchange → `KEXINIT` negotiation → `KEXDH` → `NEWKEYS` →
> `ssh-userauth` (publickey or password) → `ssh-connection` session channel,
> with transparent in-session re-keys.

## Feature summary

<details>
<summary><b>Expand Feature Summary List</b></summary>

- **Crypto-agnostic key exchange** - the KEX method and host-key type are negotiated, not fixed: the server advertises both suites in a runtime-selectable preference order (`ssh_kex_set_prefer_rsa()`, default RSA/DH) and picks the first mutually supported one it holds a key for. A stock `ssh` client gets modern crypto; a device that wants the ESP32-accelerated path forces RSA/DH
- **curve25519-sha256 key exchange** (RFC 8731 + RFC 7748) - X25519 ECDH; software radix-2^16 field arithmetic, with the field inversion offloaded to the ESP32 MPI/RSA hardware accelerator (the same modexp engine RSA and DH use)
- **ecdh-sha2-nistp256 key exchange** (RFC 5656 §4) - ECDH over NIST P-256 (SHA-256); the shared secret is the X coordinate of `d_S * Q_C`, hashed as an mpint with `Q_C`/`Q_S` as 65-byte point strings. ESP32 uses mbedTLS (hardware-accelerated, side-channel-hardened `mbedtls_ecdh_compute_shared`); the native test path is the same software P-256 as the host key, pinned to the RFC 5903 §8.1 shared-secret vectors
- **DH-group14-SHA256 key exchange** (RFC 3526 + RFC 8268) - 2048-bit MODP, g=2, full exchange-hash + host-key signature; the HW-accelerated path on ESP32
- **`chacha20-poly1305@openssh.com`** encryption - OpenSSH's default AEAD cipher (the negotiated preference); ChaCha20 (RFC 8439) + Poly1305 with the length field encrypted under a separate key, verified against the RFC 8439 vectors and cross-checked byte-for-byte against OpenSSL. A stock `ssh` client negotiates it; HW-verified against OpenSSH on an ESP32-S3
- **`aes256-gcm@openssh.com`** encryption (RFC 5647) - AES-256-GCM AEAD, the second negotiated preference; the 4-byte packet length is sent in the clear as additional authenticated data, the packet body is sealed, and the 96-bit nonce's invocation counter advances per packet. AES block hardware-accelerated on ESP32 via mbedTLS (GHASH in software), software AES-256 for native tests; seal/open verified byte-for-byte against the NIST/McGrew AES-256-GCM Test Case 16 vector
- **AES-256-CTR** encryption (RFC 4344) - the fallback cipher, hardware-accelerated on ESP32 via mbedTLS, software fallback for native tests
- **HMAC-SHA2-256 / HMAC-SHA2-512** integrity (RFC 6668), including the encrypt-then-MAC variants **`hmac-sha2-256-etm@openssh.com`** / **`hmac-sha2-512-etm@openssh.com`** (OpenSSH's preferred MACs for the aes cipher; the length is authenticated in the clear and the MAC is verified before any decryption) - MAC-verify-before-use invariant; the chacha20-poly1305 AEAD carries its own implicit MAC. All HW-verified against OpenSSH on an ESP32-S3
- **ssh-ed25519 host key** (RFC 8709 + RFC 8032) - Ed25519 signing over the exchange hash; installed from a 32-byte seed via `pc_ssh_hostkey_ed25519_set()` alongside or instead of the RSA host key
- **RSA-SHA2-512 / RSA-SHA2-256** host key (RFC 8332) - PKCS#1 v1.5 signing over the exchange hash; both hashes are backed by the one `ssh-rsa` key and negotiated as separate `server_host_key_algorithms` (rsa-sha2-512 preferred). Real on both platforms (ESP32 mbedTLS, native full-width modexp), HW-accelerated on ESP32
- **ECDSA-SHA2-nistp256** host key (RFC 5656) - ECDSA over NIST P-256 (SHA-256), installed from a 32-byte private scalar via `pc_ssh_hostkey_ecdsa_set()`. ESP32 uses mbedTLS (hardware-accelerated, side-channel-hardened); the native test path is a self-contained software P-256 with RFC 6979 deterministic signing, pinned byte-for-byte to the RFC 6979 A.2.5 (P-256/SHA-256) vectors
- **Publickey authentication** (RFC 4252 §7) - client `rsa-sha2-512`, `rsa-sha2-256`, `ecdsa-sha2-nistp256` **and** `ssh-ed25519` signatures are verified for real on both platforms (the RSA hash is chosen from the client's signature-algorithm name); an application callback authorizes keys per user
- **Password authentication** (RFC 4252 §8) - credentials checked via an application callback; the password is wiped from the stack after every attempt. Compile out with `PC_SSH_ALLOW_PASSWORD=0` for publickey-only hardening
- **Keyboard-interactive authentication** (RFC 4256) - opt-in via `PC_ENABLE_SSH_KEYBOARD_INTERACTIVE`; the server sends one non-echoed `Password:` prompt (`USERAUTH_INFO_REQUEST`) and verifies the `USERAUTH_INFO_RESPONSE` through the same password callback, so it is the challenge-response face of password auth (the common `ssh -o PreferredAuthentications=keyboard-interactive` / PAM-password case). The response is wiped from the stack; a response with no armed exchange is refused. HW-verified against OpenSSH 10 on an ESP32-P4 (authenticated + byte-exact channel echo)
- **Session channel** (RFC 4254) - `shell`/`exec`/`pty-req` accepted; inbound channel data surfaced to the app as a raw byte stream, with RFC 4254 §5.2 window flow control
- **TCP port forwarding** (`direct-tcpip`, the `ssh -L` local forward) - opt-in via `PC_SSH_PORT_FORWARD`; the `ssh_forward` owner opens the outbound connection through the client transport and bridges bytes both ways, with an optional target-allow policy callback
- **Remote port forwarding** (`forwarded-tcpip`, the `ssh -R` remote forward, RFC 4254 §7) - same `PC_SSH_PORT_FORWARD` gate: the client's `tcpip-forward` global request makes the device open a listener, and each inbound connection is bridged back to the client over a server-initiated `forwarded-tcpip` channel (`pc_ssh_conn_open_forwarded`), gated by the same policy callback. HW-verified on an ESP32-P4: a stock OpenSSH `ssh -R` through the device tunnels a byte-exact reverse round trip. (X11 forwarding is a deliberate non-goal.)
- **OpenSSH interoperability** - works with a stock `ssh` client (no algorithm overrides): the RX ring and KEXINIT store hold a full modern client KEXINIT, and **`ext-info` / `server-sig-algs`** (RFC 8308) is advertised (`rsa-sha2-512`, `rsa-sha2-256`, `ecdsa-sha2-nistp256` and `ssh-ed25519`, in preference order) so the client picks a key type it can offer. Depending on the server preference the client negotiates `curve25519-sha256` + `ssh-ed25519` (default modern client choice) or `diffie-hellman-group14-sha256` + `rsa-sha2-512`. HW-validated on an ESP32-S3 with a stock OpenSSH client on both suites (curve25519/ed25519 and DH/RSA), including an ed25519-only client key
- **In-session re-keying** (RFC 4253 §9) - client- or server-initiated; the session id is fixed at the first exchange hash across re-keys
- **Static-only allocation** - all SSH state pre-allocated in BSS; no heap after [`begin()`](@ref PC::begin) (except the one-per-connection mbedTLS RSA operation during KEX, freed immediately)
- **RSA private key never in static memory** - loaded from NVS → stack → sign → volatile-wipe; zero window for overflow-based key exfiltration
- **Sequence number overflow guard** - connection closed before 32-bit wrap to prevent CTR keystream reuse

</details>

## RFC / FIPS compliance

<details>
<summary><b>Expand RFC / FIPS Compliance Table</b></summary>

| Component                     | Standard                   | Status                                                                                                                                              |
| ----------------------------- | -------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| SHA-256                       | FIPS 180-4                 | Implemented; HW-accelerated on ESP32 (mbedtls streaming + one-shot), software on native                                                             |
| HMAC-SHA2-256                 | RFC 2104, RFC 6668         | Implemented; verify-before-use, constant-time MAC compare                                                                                           |
| AES-256-CTR                   | FIPS 197, RFC 4344         | Implemented (HW via mbedTLS on ESP32; software AES on native)                                                                                       |
| AES-256-GCM (`@openssh.com`)  | FIPS 197, RFC 5647, SP 800-38D | Implemented AEAD (length-in-clear as AAD, per-packet invocation counter); AES HW via mbedTLS on ESP32, GHASH in software; KAT vs NIST/McGrew Test Case 16 |
| DH group14 (modexp)           | RFC 3526, RFC 8268         | Implemented (HW `mbedtls_mpi` on ESP32; software Montgomery on native)                                                                              |
| curve25519-sha256 KEX (X25519)| RFC 8731, RFC 7748         | Implemented - software radix-2^16 field math; field inversion on the ESP32 MPI accelerator (`mbedtls_mpi_exp_mod`), software Fermat on native       |
| ecdh-sha2-nistp256 KEX        | RFC 5656 §4, RFC 5903      | Implemented - P-256 ECDH shared secret (mpint), on-curve peer-point check; ESP32 mbedTLS (HW), native software P-256. Byte-exact vs RFC 5903 §8.1 vectors |
| X25519 low-order point reject | RFC 7748 §6.1              | Implemented (rejects an all-zero shared secret)                                                                                                     |
| DH public-value validation    | RFC 4253 §8                | Implemented (rejects `e`/`f` outside `1 < v < p-1`)                                                                                                 |
| ssh-ed25519 host key + sign   | RFC 8709, RFC 8032         | Implemented - deterministic Ed25519 over the exchange hash; SHA-512 HW on ESP32 / software on native                                                |
| Ed25519 client-auth verify    | RFC 8709, RFC 8032 §5.1.7  | Implemented - `ssh-ed25519` client signatures verified for real on both platforms (KAT + tamper-reject tested)                                      |
| ecdsa-sha2-nistp256 host key  | RFC 5656, FIPS 186-4       | Implemented - ECDSA P-256 (SHA-256) over the exchange hash; ESP32 mbedTLS (HW), native software P-256. Sign byte-exact vs RFC 6979 A.2.5 vectors    |
| ECDSA P-256 client-auth verify| RFC 5656 §3.1.2, RFC 6979  | Implemented - `ecdsa-sha2-nistp256` client signatures (mpint r,s) verified on both platforms (KAT + e2e + genuine client-auth tested)               |
| Session key derivation        | RFC 4253 §7.2              | Implemented (six A–F keys from `K`, `H`, session_id)                                                                                                |
| Binary packet protocol        | RFC 4253 §6                | Implemented - framing, ≥4-byte padding to 16-byte multiple, encrypt-then-MAC over `seq‖plaintext`, CTR length-peek with snapshot/restore            |
| RSA public-key blob           | RFC 4253 §6.6, RFC 8332 §3 | Implemented - blob type string is `ssh-rsa` for both rsa-sha2-256 and rsa-sha2-512                                                                  |
| RSA-SHA2-512 / -256 signing   | RFC 8332, RFC 8017 §8.2    | **Real on both platforms** - ESP32 `mbedtls_pk_sign` (SHA-512/256 DigestInfo); native full-width `m^d mod n`. SHA-512 KAT byte-exact vs openssl; hash bound into the signature |
| RSA-SHA2-512 / -256 verification | RFC 8332, RFC 8017 §8.2 | **Real on both platforms** (small public exponent); hash chosen from the signature-algorithm name; validated against openssl-generated KATs        |
| Version / KEXINIT negotiation | RFC 4253 §4.2, §7.1        | Implemented - banner exchange + crypto-agnostic negotiation (KEX method, host-key type, cipher [chacha/aes-gcm/aes-ctr], MAC, and s2c compression all negotiated to a runtime preference)      |
| ext-info / server-sig-algs    | RFC 8308                   | Implemented - advertises accepted client-sig algorithms (`rsa-sha2-512`, `rsa-sha2-256`, `ecdsa-sha2-nistp256`, `ssh-ed25519`) after NEWKEYS         |
| Exchange hash + NEWKEYS       | RFC 4253 §7.2, §8; RFC 8731 | Implemented - H over `V_C,V_S,I_C,I_S,K_S,` then `e,f` (DH mpints) or `Q_C,Q_S` (curve strings) then `K`; keys activate on NEWKEYS                    |
| User authentication           | RFC 4252 §5, §7, §8; RFC 4256 | Implemented - `publickey` (`rsa-sha2-512`, `rsa-sha2-256`, `ecdsa-sha2-nistp256`, `ssh-ed25519`), `password`, and (opt-in `PC_ENABLE_SSH_KEYBOARD_INTERACTIVE`) `keyboard-interactive` methods                              |
| Connection / channel          | RFC 4254 §5, §6            | Implemented - single `session` channel, data stream, window flow control                                                                            |
| Re-keying                     | RFC 4253 §9                | Implemented - packet-count threshold; session id preserved across re-keys                                                                           |

</details>

## Authentication and hardening

Two methods are offered. The application installs callbacks:

```cpp
pc_ssh_auth_set_pubkey_cb([](const char *user, const uint8_t *blob, size_t len) {
    return is_authorized_key(user, blob, len); // compare against your authorized_keys
});
pc_ssh_auth_set_password_cb([](const char *user, const char *pass) {
    return check_password(user, pass);
});
```

For publickey, the server first answers the no-signature probe with
`USERAUTH_PK_OK`, then verifies the client's RSA-SHA2-256 signature over the
session id and request before granting access.

**Hardening:** define `PC_SSH_ALLOW_PASSWORD=0` to compile out password auth
entirely. The `password` method is then refused and is not advertised in the
`USERAUTH_FAILURE` method list; only `publickey` remains.

## Host key provisioning

The server reads its RSA-2048 private key from NVS (namespace `ssh_host_key`,
key `priv_der`) as a DER-encoded PKCS#1/PKCS#8 blob. Generate and store it once
per device:

1. **Generate a 2048-bit key and export DER (PKCS#8) on your workstation.** The
   bundled generator does both and prints the matching `.pub`:

   ```sh
   python3 tools/crypto/gen_ssh_host_key.py --type rsa --out-dir ./keys
   # -> keys/ssh_host_key.der (store in NVS) and keys/ssh_host_key.pub (known_hosts)
   ```

   or by hand with the same tools:

   ```sh
   openssl genrsa -out ssh_host.pem 2048
   openssl pkcs8 -topk8 -nocrypt -in ssh_host.pem -outform DER -out ssh_host.der
   ```

   (`ssh_host.der` must be ≤ [`SSH_RSA_KEY_DER_MAX`](@ref SSH_RSA_KEY_DER_MAX) bytes - ~1.2 KB for RSA-2048.)
   For a worked end-to-end walkthrough of both provisioning paths, see the
   [`SSHHostKey`](../examples/L5-Session/SSHHostKey/README.md) example.

2. **Write the DER blob into NVS** from a one-time provisioning sketch:

   ```cpp
   #include <Preferences.h>
   extern const uint8_t der[] /* = { ...contents of ssh_host.der... } */;
   extern const size_t der_len;

   Preferences p;
   p.begin("ssh_host_key", false);          // read-write
   p.putBytes("priv_der", der, der_len);     // key name MUST be "priv_der"
   p.end();
   ```

   Embed `ssh_host.der` as a byte array (e.g. `xxd -i ssh_host.der`), flash the
   provisioning sketch once, then flash your real firmware.

3. **At boot**, call [`pc_ssh_rsa_load_pubkey()`](@ref pc_ssh_rsa_load_pubkey) once (before accepting SSH) so the
   public half (n, e) is available for the host-key blob. The private half is
   parsed by the same call and kept for the server's lifetime, as an SSH host key
   normally is: on ESP32 inside a private mbedTLS context, on the software backend
   as a secure-pool borrow ([`mmgr/secure.h`](@ref secure.h)) whose reclaim wipes.
   Neither holds it in static memory, and the DER it was read from is wiped before
   the call returns.

The matching public key for clients' `known_hosts` is derived from the same DER
(`ssh-keygen -y -f ssh_host.pem`).

### Known test key (testing only)

For hardware / interop testing there is a committed, **public, insecure** key at
[`test/fixtures/ssh_test_host_key/`](../test/fixtures/ssh_test_host_key/): the DER as a
ready-to-include byte array (`ssh_test_host_key.h` -> `PC_SSH_TEST_HOST_KEY_DER`) plus
its `.pub`. Provision it in one step from a sketch:

```cpp
#include "ssh_test_host_key.h"
Preferences p;
p.begin("ssh_host_key", false);
p.putBytes("priv_der", PC_SSH_TEST_HOST_KEY_DER, PC_SSH_TEST_HOST_KEY_DER_LEN);
p.end();
```

Its private half is in the repo, so it authenticates nothing. **Never use it in a product** -
deployments must generate their own key with the steps above and keep it secret.

## Limitations

- `PC_SSH_MAX_CHANNELS` channels per connection (default 1). Port forwarding
  (`ssh -L` / `ssh -R`, `PC_SSH_PORT_FORWARD`), the **SFTP** subsystem
  (`PC_ENABLE_SSH_SFTP`) and **SCP** upload (`PC_ENABLE_SSH_SCP`) are all
  available (opt-in); X11 forwarding is not.
- SCP serves only the SINK (upload) direction; use SFTP `get` to download.
- On connection teardown the server closes the TCP without a graceful
  `SSH_MSG_DISCONNECT`, so a client may print a "broken pipe" at the very end of an
  otherwise-successful session (a plain `ssh <host> exit` shows it too). The data
  transfer is unaffected. A graceful disconnect is a planned follow-up.
- The native-build software RSA/AES/bignum paths are not constant-time; they are
  compiled out of firmware (`#ifndef ARDUINO`) and exist only for host tests. On
  ESP32 the hardware/mbedTLS paths are used. Native RSA _signing_ and
  _verification_ are both mathematically complete (validated by a sign→verify
  round-trip and an openssl KAT).

## Memory footprint

All numbers use default configuration values.

<details>
<summary><b>Expand Memory Footprint Details Table</b></summary>

| Symbol                    | Type | Notes                                            |
| ------------------------- | ---- | ------------------------------------------------ |
| `ssh_pkt[MAX_SSH_CONNS]`  | BSS  | Packet state + RX reassembly buffers             |
| `ssh_keys[MAX_SSH_CONNS]` | BSS  | 2 × 32-byte AES keys + 2 × 16-byte IV/counters + 2 × 64-byte MAC keys (raw bytes; the key schedule is rebuilt per packet in `crypto_work`, never stored here) |
| `ssh_dh[MAX_SSH_CONNS]`   | BSS  | DH scalars y, f, K (wiped post-KEX) + H          |
| `ssh_sess[MAX_SSH_CONNS]` | BSS  | Handshake phase, V_C/I_C/I_S, session id         |
| `crypto_work[2144]`       | BSS  | Shared crypto scratch (bignum / AES / HMAC / cipher state); wiped after each use |
| `group14_p`, `group14_g`  | BSS  | RFC 3526 prime + generator (constants)           |
| `ssh_host_pubkey`         | BSS  | RSA-2048 public key (n + e); no secret material  |

</details>

**Key material is excluded by design.** AES-256 keys, HMAC keys, and the DH
shared secret K live in `ssh_keys[]` / `ssh_dh[]`. The RSA private key is never
in static memory - it exists only on the stack during [`ssh_rsa_sign()`](@ref ssh_rsa_sign) and is
volatile-wiped before the function returns.

The BSS symbols (`ssh_pkt`, `ssh_keys`, `ssh_dh`) are separate linker symbols, so
a linear buffer overflow from `ssh_pkt[i].rx_buf` cannot reach `ssh_keys` in a
single stride.

See [SECURITY.md](SECURITY.md) for the full security treatment.
