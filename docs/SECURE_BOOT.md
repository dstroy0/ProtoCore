<!-- Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com> -->
<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Secure Boot & Flash Encryption (ESP32 hardening)

This guide explains how to protect a device running
ProtoCore against a **physically present attacker** - one who
can read the SPI flash, attach a debugger, or re-flash the chip - using the two
ESP32 hardware features that close those gaps: **Secure Boot v2** (only firmware
you signed will run) and **Flash Encryption** (the flash is unreadable without the
per-device key fused into the chip). The library's own crypto (TLS, SSH, SNMPv3,
JWT/OIDC, the hash-chained audit log) defends the network; Secure Boot and Flash
Encryption defend the silicon.

> ⚠️ **Everything here burns eFuses, which is irreversible.** A wrong key, a lost
> key, or the wrong mode **permanently bricks the chip or locks you out**. Practice
> on a sacrificial board, read the official docs end to end, and keep signing keys
> backed up offline. This is an ESP-IDF / `espefuse.py` / `espsecure.py` workflow;
> it is configured at the bootloader level, **not** from an Arduino sketch.

## What is at risk without it

The library keeps these secrets/assets in flash; without Flash Encryption they can
be dumped with `esptool.py read_flash`, and without Secure Boot the firmware can be
replaced with a malicious build:

| Asset                                  | Where it lives                    | Protected by                   |
| -------------------------------------- | --------------------------------- | ------------------------------ |
| Firmware image (your logic + keys)     | app partition                     | Secure Boot + Flash Encryption |
| SSH RSA host private key               | NVS (`ssh_host_key` / `priv_der`) | Flash Encryption (+NVS enc)    |
| TLS server key & certificate           | flash / NVS (your storage)        | Flash Encryption               |
| SNMPv3 USM auth/priv keys, JWT secret  | flash / NVS / build constants     | Flash Encryption               |
| WiFi credentials, config blobs         | NVS (`config_store`)              | Flash Encryption (+NVS enc)    |
| Audit-log chain (`services/audit_log`) | RAM ring (+ your sink)            | see "Audit log" below          |

The SSH private key is already loaded to the stack and wiped after use
(`ssh_rsa.h`), so it never rests in static RAM - Flash Encryption protects the
at-rest NVS copy, completing the chain.

## Secure Boot v2 - only your firmware runs

Secure Boot v2 makes the chip's ROM verify a digital signature on the bootloader,
and the bootloader verify the signature on the app, on every boot. The **public**
key's SHA-256 digest is fused into the chip; the **private** signing key stays on
your build machine.

- **Algorithm:** RSA-3072 (PSS) on all Secure-Boot-v2 chips; ECDSA (P-256/P-384)
  is also available on ESP32-S3/C-series. (Original ESP32 needs silicon revision
  v3 / ECO3 for v2; earlier revisions only have the older AES-based v1 - prefer a
  v2-capable chip.)
- **Up to two** public-key digests can be fused, so you can rotate signing keys
  once before the slots are locked.

```sh
# 1. Generate a signing key (KEEP THIS OFFLINE AND BACKED UP).
espsecure.py generate_signing_key --version 2 secure_boot_signing_key.pem

# 2. Enable in the project config and build (ESP-IDF):
idf.py menuconfig         # Security features -> Enable hardware Secure Boot in bootloader
idf.py build

# 3. First flash burns the digest eFuse; subsequent boots verify signatures.
idf.py flash
```

After Secure Boot is active the chip refuses any image not signed by your key -
including a re-flashed development build - so you must sign every future update
with the same key.

## Flash Encryption - the flash is unreadable

Flash Encryption transparently encrypts the bootloader, app, and partitions marked
`encrypted` with a key generated **on the device** and stored in a read/write
protected eFuse block (it never leaves the chip). A flash dump is then ciphertext.

- **Cipher:** AES-256-XTS (ESP32-S2/S3/C-series) or AES-256 (original ESP32).
- **Development mode:** the device can still be re-flashed and the key is usable
  by the host tool - convenient for bring-up, **not** for production.
- **Release mode:** plaintext flashing is permanently disabled and the key is
  read-protected. **One-way.** Use this only on units you are shipping.

```sh
idf.py menuconfig   # Security features -> Enable flash encryption on boot
                    #   (choose Release mode for production)
idf.py flash        # first boot encrypts in place; thereafter flash is ciphertext
```

### Encrypt NVS too (protects the stored keys)

Flash Encryption does **not** automatically encrypt the NVS partition. To protect
the SSH host key and `config_store` blobs, enable **NVS encryption**: add an
`nvs_keys` partition (itself protected by Flash Encryption) and initialize NVS with
`nvs_flash_secure_init()`. See the ESP-IDF "NVS Encryption" guide.

## Recommended production posture

1. **Secure Boot v2 + Flash Encryption (Release mode)** together - neither is
   sufficient alone (Secure Boot still leaves secrets readable; Flash Encryption
   alone still runs unsigned code).
2. **NVS encryption** for the SSH host key, TLS key, and credentials.
3. **Disable debug / recovery surfaces** via eFuse: JTAG (`espefuse.py burn_efuse
DIS_PAD_JTAG` / soft-disable), and UART download mode or Secure Download Mode
   (`ENABLE_SECURITY_DOWNLOAD`) so the bootloader cannot be used to read memory.
4. **Build-time:** ship a release build with **`PC_ENABLE_DIAG` off** (it
   exposes the compile-time config), prefer **HTTPS/WSS** (`PC_ENABLE_TLS`) and
   **mTLS** (`PC_ENABLE_MTLS`) for control planes, and gate state-changing
   routes (auth + `PC_ENABLE_CSRF`).

## Audit log under a physical attacker

The hash chain in [`services/audit_log`](FEATURES.md#audit-log) makes on-device
tampering detectable, but an attacker who holds the Flash Encryption key could in
principle recompute the chain. For **true off-device tamper-evidence**, install a
sink (`pc_audit_set_sink`) that forwards every record - with its chain hash - to
an append-only remote store (syslog / an HTTP log service / a write-once SD file)
the device cannot later rewrite. Secure Boot + Flash Encryption raise the bar to
recover that key; the remote sink is what survives if it is ever breached.

## Caveats with the Arduino core

The Arduino-ESP32 core is built on ESP-IDF, but Secure Boot and Flash Encryption
are bootloader/eFuse settings, not sketch options. To use them you either build via
ESP-IDF (with Arduino as a component) or supply a custom `sdkconfig` /
partition + bootloader to your PlatformIO build, then burn eFuses with
`espefuse.py`. The application code in this library is unchanged either way - these
features protect the image and the flash beneath it.

## References

- ESP-IDF: [Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/secure-boot-v2.html)
- ESP-IDF: [Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/flash-encryption.html)
- ESP-IDF: [NVS Encryption](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_encryption.html)
- ESP-IDF: [Security workflow / `espefuse.py`](https://docs.espressif.com/projects/esptool/en/latest/esp32/espefuse/index.html)
