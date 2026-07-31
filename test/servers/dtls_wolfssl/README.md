# DTLS 1.3 interop: pc_dtls_conn ⇄ wolfSSL

Real-peer conformance test for the library's DTLS 1.3 server (`network_drivers/presentation/dtls`).
[`dtls_interop_server.cpp`](dtls_interop_server.cpp) wraps the transport-neutral `pc_dtls_conn` state
machine in a tiny UDP server; the [wolfSSL](https://github.com/wolfSSL/wolfssl) DTLS 1.3 example
client drives a full handshake **and** an application-data round trip against it.

This is the strongest evidence the wire format is correct — a from-scratch handshake against the
reference implementation, not just our own round trip. It caught three real DTLS-vs-TLS conformance
bugs the self-consistent host tests could not (see below).

## Run it

Linux only (needs a POSIX UDP socket and a native `g++`). One command builds everything and runs it:

```sh
test/servers/pc_dtls_wolfssl/run_interop.sh
```

Expected output:

```
>> running interop [HRR (default groups)] on udp/11150
HANDSHAKE OK (via HelloRetryRequest)
APPDATA RX 14 bytes: hello wolfssl!
APPDATA echoed 14 bytes; INTEROP OK
>> PASS [HRR (default groups)]
>> running interop [direct X25519 (-t)] on udp/11150
HANDSHAKE OK
APPDATA RX 14 bytes: hello wolfssl!
APPDATA echoed 14 bytes; INTEROP OK
>> PASS [direct X25519 (-t)]
>> ALL PASS
```

The client runs twice: once with wolfSSL's default groups (it leads with a non-X25519 key_share, so
the server answers with a HelloRetryRequest and renegotiates the group to X25519), and once with `-t`
(X25519 offered up front) for the one-round-trip path. The script, on first run, builds wolfSSL,
generates a throwaway Ed25519 certificate, compiles the harness, and runs the exchange. Overrides via
env: `WOLFSSL_VERSION`, `DTLS_INTEROP_PORT`, `DTLS_INTEROP_WORK`.

## Building wolfSSL with DTLS 1.3 (what the script does)

wolfSSL's GitHub releases attach only the `.tar.gz.asc` signature as an asset, so build from the git
tag (which needs autotools):

```sh
sudo apt-get install -y autoconf automake libtool   # if missing
git clone --depth 1 -b v5.9.2-stable https://github.com/wolfSSL/wolfssl.git
cd wolfssl
./autogen.sh
./configure --enable-dtls --enable-dtls13 --enable-ed25519 --enable-curve25519 --enable-aesgcm --disable-shared
make -j"$(nproc)"
# -> examples/client/client
```

The client flags used: `-u` (DTLS over UDP), `-v 4` (DTLS 1.3), `-d` (skip cert-chain verification of
the throwaway self-signed cert; the CertificateVerify signature is still checked against the cert's
public key). `-t` (offer an X25519 key_share up front) is used only for the second, one-round-trip
run; the first run omits it so wolfSSL leads with its default group and the server renegotiates to
X25519 with a HelloRetryRequest (RFC 9147 §5.1).

## What this caught

The record + handshake host KATs were pinned to an _independent Python reconstruction of the same
primitives_, which is self-consistent but shares the implementer's assumptions. Interop against a
real peer surfaced three DTLS-vs-TLS differences those KATs could not:

1. **ClientHello `legacy_cookie`** (RFC 9147 §5.3) — the DTLS ClientHello carries an extra
   `legacy_cookie` field between `legacy_session_id` and `cipher_suites` that the TLS ClientHello
   does not; the shared parser read `cipher_suites` at the wrong offset.
2. **Version codepoints** (RFC 9147 §5.3) — DTLS 1.3 advertises `0xFEFC` in `supported_versions`
   (not `0x0304`) and uses `legacy_version` `0xFEFD` in the ServerHello.
3. **HKDF-Expand-Label prefix** (RFC 9147 §5.9) — DTLS 1.3 uses the label prefix **`dtls13`**, not
   `tls13 `. This affected every derived secret **and** the record `key`/`iv`/`sn`. Diagnosed by
   comparing our handshake-traffic secret to wolfSSL's `SSLKEYLOGFILE` and bisecting the transcript
   vs. the key schedule.
