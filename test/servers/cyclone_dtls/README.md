# DTLS 1.3 interop: pc_dtls_conn ⇄ CycloneSSL

Real-peer conformance test for the library's DTLS 1.3 server (`network_drivers/presentation/dtls`)
against a **second, independent** reference stack: [Oryx Embedded **CycloneSSL**](https://github.com/Oryx-Embedded/CycloneSSL).
It complements the [wolfSSL harness](../dtls_wolfssl/README.md) - two unrelated from-scratch DTLS 1.3
implementations both completing a handshake against our server is far stronger evidence the wire
format is correct than either alone (and stronger than any self-referential KAT).

The same `dtls_interop_server` harness the wolfSSL test uses (it wraps the transport-neutral
`pc_dtls_conn` state machine in a tiny UDP server) is driven here by a
[CycloneSSL DTLS 1.3 client](cyclone_dtls_client.c) built from the Oryx sources.

## Run it

Linux only (POSIX UDP socket + native `gcc`/`g++`). One command clones Cyclone on first run, builds
everything, and runs the exchange twice:

```sh
test/servers/cyclone_dtls/run_interop.sh
```

Expected output:

```
>> running interop [plain X.509] on udp/11160
HANDSHAKE OK
APPDATA RX 15 bytes: hello cyclone!
APPDATA echoed 15 bytes; INTEROP OK
[client] DTLS 1.3 handshake OK
[client] negotiated peer cert format = X.509
ECHO(15): hello cyclone!
>> PASS [plain X.509]
>> running interop [Raw Public Keys (--rpk)] on udp/11160
HANDSHAKE OK
APPDATA RX 15 bytes: hello cyclone!
APPDATA echoed 15 bytes; INTEROP OK
[client] server presented a RawPublicKey (RFC 7250), 44 bytes SPKI
[client] negotiated peer cert format = RawPublicKey (RFC 7250)
ECHO(15): hello cyclone!
>> PASS [Raw Public Keys (--rpk)]
>> ALL PASS
```

Two runs:

- **plain X.509** - the one-round-trip DTLS 1.3 full handshake (`TLS_AES_128_GCM_SHA256` / X25519 /
  Ed25519); the server presents its self-signed Ed25519 certificate.
- **Raw Public Keys (`--rpk`)** - the client registers a RawPublicKey verify callback, so CycloneSSL
  advertises `server_certificate_type = RawPublicKey` (RFC 7250) in its ClientHello. Its callback fires
  **only** when the peer actually sends a bare `SubjectPublicKeyInfo`, so this run proves our
  `PC_ENABLE_TLS_RPK` server credential (the 44-byte Ed25519 SPKI) interoperates with CycloneSSL.

Overrides via env: `CYCLONE_SSL_VERSION` (default `v2.6.4`), `CYCLONE_INTEROP_PORT`,
`CYCLONE_INTEROP_WORK`.

## Layout

- [`cyclone_dtls_client.c`](cyclone_dtls_client.c) - the DTLS 1.3 client: a connected UDP socket wired
  to CycloneSSL via `tlsSetSocketCallbacks`, `tlsSetTransportProtocol(DATAGRAM)`, DTLS 1.3 pinned, an
  HMAC-DRBG PRNG, cert-verify + RPK-verify callbacks, then `tlsConnect` / `tlsWrite` / `tlsRead`.
- `tls_config.h`, `crypto_config.h`, `os_port_config.h` - the lean Cyclone build config (DTLS 1.3
  client, X25519, Ed25519 sign, AES-128-GCM/SHA-256, HKDF, HMAC-DRBG, and `TLS_RAW_PUBLIC_KEY_SUPPORT`).
- `run_interop.sh` - clones Cyclone (GPL, into `.work/`, never committed), builds the client + the
  `pc_dtls_conn` harness, and runs both variants.

The Oryx Cyclone sources are **not vendored** in this repo; `run_interop.sh` fetches them at run time
(GPLv2). Only our client, config, and script are committed.
