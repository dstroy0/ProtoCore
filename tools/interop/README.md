# HTTP/3 real-client interop harness

`h3_server.cpp` runs the library's `quic_server` as a real networked HTTP/3 server on Linux, so a
third-party client (`curl --http3`, ngtcp2, quiche) can complete a full QUIC + TLS 1.3 + HTTP/3
exchange against the actual code. It drives `quic_server` through the same host seam the unit tests
use (`quic_server_ingest` + the output sink), wired to a POSIX UDP socket. On an ESP32 the same
`quic_server` binds UDP through `pc_udp`; this is the desktop stand-in that validates the wire
without flashing a board.

This is the faithful third-party proof required by the HTTP/3 profile: our server is
TLS_AES_128_GCM_SHA256 + X25519 + an **Ed25519** certificate, which curl / OpenSSL / nghttp3 accept.

## Generate a certificate + key

The server sends an Ed25519 certificate and signs the TLS 1.3 CertificateVerify with the matching
seed, so the two must come from the same key:

```sh
openssl genpkey -algorithm ed25519 -out key.pem
openssl req -x509 -key key.pem -out cert.pem -days 30 \
    -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
openssl x509 -in cert.pem -outform DER -out cert.der   # the DER leaf the server sends
openssl pkey -in key.pem -outform DER | tail -c 32 > seed.bin   # the raw 32-byte Ed25519 seed
```

## Build + run

```sh
R=../../src ; H=$R/network_drivers/presentation/http3 ; S=$R/network_drivers/presentation/ssh/crypto
g++ -std=c++17 -O2 -I "$R" -DPC_ENABLE_HTTP3=1 h3_server.cpp \
  $H/quic_server.cpp $H/quic_conn.cpp $H/quic_tls.cpp $H/tls13_kdf.cpp $H/tls13_msg.cpp \
  $H/quic_tp.cpp $H/quic_crypto.cpp $H/quic_aead.cpp $H/quic_hkdf.cpp $H/quic_packet.cpp \
  $H/quic_frame.cpp $H/quic_varint.cpp $H/h3_conn.cpp $H/h3_frame.cpp $H/qpack.cpp \
  $R/network_drivers/presentation/hpack_prim/hpack_prim.cpp \
  $S/ssh_sha256.cpp $S/ssh_hmac_sha256.cpp $S/ssh_sha512.cpp $S/ssh_curve25519.cpp $S/ssh_ed25519.cpp \
  -o h3_server
./h3_server 4433 cert.der seed.bin
```

## Interop with curl

```sh
curl --http3-only -k -v https://127.0.0.1:4433/hello
```

Expected: `using HTTP/3`, `Certificate level 0: ... ED25519, signed using ED25519`, `< HTTP/3 200`,
and the response body. (`-k` skips chain / hostname checks, but curl still verifies the Ed25519
CertificateVerify, so a mismatched `cert.der` / `seed.bin` pair fails the handshake.)

Verified: curl 8.14.1 (OpenSSL 3.5.6 QUIC + nghttp3 1.8.0) completes the handshake and GET/POST at
~14 ms per request. The mbedTLS hardware-crypto path on the ESP32-S3 produces byte-identical results
(see the on-device self-test), so this software-path interop plus the on-device agreement cover both.
