# Documentation

A vendor agnostic, multi-protocol network server with a fully deterministic memory footprint, RFC 7230 compliant request parsing, and an OSI-layered architecture. It serves HTTP/1.1 and HTTP/2 (with HTTP/3 over QUIC, host-tested), WebSocket, and Server-Sent Events, with optional HTTPS/TLS, SSH, Telnet, SNMP, CoAP, Modbus TCP, MQTT, and OPC UA.

## Features

A compile-time menu grouped by the OSI layer each feature lives at, alphabetized within each layer: each cell is an optional `PROTOCORE_ENABLE_*` subsystem (core HTTP/1.1, routing, middleware, JSON, templating, and chunked responses are always on). **Hover an entry for its summary; click through to [FEATURES.md](FEATURES.md) for the full description.** The tables are generated from [FEATURES.md](FEATURES.md) by `tools/ci_tooling/generate/gen_feature_tables.py`, so they never drift.

<!-- BEGIN GENERATED FEATURE TABLES (tools/ci_tooling/generate/gen_feature_tables.py) -->

<!-- prettier-ignore-start -->

**256 features**, every one a compile-time `PROTOCORE_ENABLE_*` flag that is off unless you ask for it. Core HTTP/1.1 parsing, routing, middleware, JSON, templating and chunked responses are always on and are not flags.

<a href="https://dstroy0.github.io/ProtoCore/features.html" title="Browse every feature">
  <img alt="Feature map: the OSI stack and the feature groups on each layer" src="diagrams/features_map.svg" width="100%">
</a>

| Layer | Features | For example |
| --- | --- | --- |
| **Foundation** | 17 | Config IO, Config Store, Device ID, DMA Peripheral Ingest, … |
| **Physical & Data Link (L1-L2)** | 31 | ADS1115, BLE GATT, Bus Capture, CC1101, … |
| **Network (L3)** | 6 | Dns Resolver, Happy Eyeballs, IPv6, Link Manager, … |
| **Transport (L4)** | 9 | Accept Throttle, IP Allowlist, Keep-Alive, MTLS, … |
| **Session (L5)** | 5 | SSH, SSH Compression, SSH SCP, SSH SFTP, … |
| **Presentation (L6)** | 18 | Auth, Auth Lockout, CBOR, CloudEvents, … |
| **Application (L7)** | 170 | AD9238, Adaptive mDNS, ADS (Beckhoff), AMQP, … |

**[Browse all of them →](https://dstroy0.github.io/ProtoCore/features.html)** - filterable, grouped by layer, one line each. Full descriptions live in [FEATURES.md](FEATURES.md); both are generated from it, so they cannot drift.

<!-- prettier-ignore-end -->

<!-- END GENERATED FEATURE TABLES -->


## New to this? Start here

If networking is new to you, the [**learn series**](learn/) is a from-scratch on-ramp
that assumes no prior knowledge: the [OSI model](learn/osi-model.md),
[TCP/IP](learn/tcp-ip.md), and [a primer on every language](learn/languages.md) in the
project - each tied back to the code below. Every protocol the library implements is
mapped to its authoritative spec in [STANDARDS.md](STANDARDS.md).

## Architecture

Each OSI layer lives in its own subdirectory under `src/network_drivers/`:

<details>
<summary><b>View Directory and OSI Layer Layout</b></summary>

```
L7  include/protocore.h  src/protocore.c         Public API, dispatch, send_text()
L6  src/network_drivers/presentation/
        presentation.c/.h                         Drains ring buffer → parser
        http/http_parser/http_parser.c/.h         RFC 7230 byte-stream state machine
        http/websocket/websocket.c/.h             WS frame parser
        http/sse/sse.c/.h                         SSE connection pool
        codec/base64/base64.c/.h                  Base64 / base64url
        codec/multipart/multipart.c/.h            Multipart form-data parser
L5  src/network_drivers/session/
        session.c/.h                              FreeRTOS event queue drain
L4  src/network_drivers/transport/
        tcp/tcp.c/.h                              lwIP callbacks, ring buffers, timeouts
        tcp/server/server.c/.h                    Per-port TCP listener, per-listener queue
L3  src/network_drivers/network/
        network.c/.h                              lwIP stub
L2  src/network_drivers/datalink/
        datalink.c/.h                             Espressif WiFi driver stub
L1  src/network_drivers/physical/
        physical.c/.h                             WiFi.begin() wrapper

    src/crypto/hash/sha1.c/.h                     SHA-1 for the WebSocket handshake
    src/network_drivers/tls/                      mbedTLS over a fixed static pool (HTTPS / wss)
    src/network_drivers/application/              Generated web assets (dashboard, terminal)
    src/web_assets/                               The editable sources those blobs are built from
    src/network_drivers/presentation/ssh/         Zero-heap SSH-2.0 server
    src/services/                                 Optional L7 subsystems, grouped by domain:
        fieldbus/{modbus, opcua, opcua_client}, iot/{mqtt, coap, graphql},
        net/snmp, security/{oidc, oauth2, totp}, radio/espnow, ...  (see FEATURES.md)
```

The conceptual layer map above is a summary; the complete file layout is generated
below from `src/` by `tools/ci_tooling/generate/gen_readme_sections.py` (single-`.h`/`.c`
service folders are collapsed to their name; generated web-asset blobs are counted,
not listed).

<details>
<summary><b>Full source tree (every library file)</b></summary>

<!-- BEGIN GENERATED SOURCE-TREE (tools/ci_tooling/generate/gen_readme_sections.py) -->

<!-- prettier-ignore-start -->

```text
src/
├── config/
│   ├── features/
│   │   ├── feature_dependency_en.h
│   │   └── feature_en_error.h
│   ├── hardware_capabilities/
│   │   ├── hw_caps_en.h
│   │   ├── hw_caps_en_error.h
│   │   └── hw_caps_prototypes.h
│   ├── memory_sizing/
│   │   └── buffer_sizing.h
│   └── platform/
│       ├── compiler_directives.h
│       ├── platform.h
│       ├── platform_defines.h
│       ├── platform_error.h
│       ├── platform_prototypes.h
│       └── types.h
├── crypto/
│   ├── aead/
│   │   ├── aes128gcm/
│   │   │   ├── aes128gcm.c
│   │   │   ├── aes128gcm.h
│   │   │   └── CMakeLists.txt
│   │   ├── aesccm/
│   │   │   ├── aesccm.c
│   │   │   ├── aesccm.h
│   │   │   └── CMakeLists.txt
│   │   ├── aesgcm/
│   │   │   ├── aesgcm.c
│   │   │   ├── aesgcm.h
│   │   │   └── CMakeLists.txt
│   │   ├── chachapoly/
│   │   │   ├── chachapoly.c
│   │   │   ├── chachapoly.h
│   │   │   └── CMakeLists.txt
│   │   └── CMakeLists.txt
│   ├── asymmetric/
│   │   ├── bignum/
│   │   │   ├── bignum.c
│   │   │   ├── bignum.h
│   │   │   └── CMakeLists.txt
│   │   ├── curve25519/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── curve25519.c
│   │   │   └── curve25519.h
│   │   ├── ecdsa/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ecdsa.c
│   │   │   └── ecdsa.h
│   │   ├── ed25519/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ed25519.c
│   │   │   └── ed25519.h
│   │   ├── fe25519/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── fe25519.c
│   │   │   └── fe25519.h
│   │   ├── rsa/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── rsa.c
│   │   │   └── rsa.h
│   │   ├── CMakeLists.txt
│   │   └── ed25519_comb_table.h
│   ├── cipher/
│   │   ├── aes256ctr/
│   │   │   ├── aes256ctr.c
│   │   │   ├── aes256ctr.h
│   │   │   └── CMakeLists.txt
│   │   ├── aes_block/
│   │   │   ├── aes_block.c
│   │   │   ├── aes_block.h
│   │   │   └── CMakeLists.txt
│   │   ├── chacha20/
│   │   │   ├── chacha20.c
│   │   │   ├── chacha20.h
│   │   │   └── CMakeLists.txt
│   │   ├── aes_sbox.h
│   │   └── CMakeLists.txt
│   ├── hash/
│   │   ├── md/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── md.c
│   │   │   └── md.h
│   │   ├── sha1/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sha1.c
│   │   │   └── sha1.h
│   │   ├── sha256/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sha256.c
│   │   │   └── sha256.h
│   │   ├── sha3/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sha3.c
│   │   │   └── sha3.h
│   │   ├── sha384/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sha384.c
│   │   │   └── sha384.h
│   │   ├── sha512/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sha512.c
│   │   │   └── sha512.h
│   │   └── CMakeLists.txt
│   ├── kdf/
│   │   ├── hkdf/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── hkdf.c
│   │   │   └── hkdf.h
│   │   ├── hkdf_sha384/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── hkdf_sha384.c
│   │   │   └── hkdf_sha384.h
│   │   ├── kdf/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── kdf.c
│   │   │   └── kdf.h
│   │   └── CMakeLists.txt
│   ├── mac/
│   │   ├── aes_cmac/
│   │   │   ├── aes_cmac.c
│   │   │   ├── aes_cmac.h
│   │   │   └── CMakeLists.txt
│   │   ├── ghash/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ghash.c
│   │   │   └── ghash.h
│   │   ├── hmac_sha256/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── hmac_sha256.c
│   │   │   └── hmac_sha256.h
│   │   ├── hmac_sha384/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── hmac_sha384.c
│   │   │   └── hmac_sha384.h
│   │   ├── hmac_sha512/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── hmac_sha512.c
│   │   │   └── hmac_sha512.h
│   │   ├── poly1305/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── poly1305.c
│   │   │   └── poly1305.h
│   │   └── CMakeLists.txt
│   ├── pqc/
│   │   ├── mlkem/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── mlkem.c
│   │   │   └── mlkem.h
│   │   ├── sntrup761/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sntrup761.c
│   │   │   └── sntrup761.h
│   │   └── CMakeLists.txt
│   ├── rng/
│   │   ├── CMakeLists.txt
│   │   ├── rng.c
│   │   └── rng.h
│   ├── x509/
│   │   ├── x509/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── x509.c
│   │   │   └── x509.h
│   │   ├── x509_verify/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── x509_verify.c
│   │   │   └── x509_verify.h
│   │   └── CMakeLists.txt
│   ├── CMakeLists.txt
│   ├── crypto_opt.h
│   ├── ct_eq.c
│   └── ct_eq.h
├── mmgr/
│   ├── arena/
│   │   ├── arena.c
│   │   ├── arena.h
│   │   └── CMakeLists.txt
│   ├── bitio/
│   │   ├── bitio.c
│   │   ├── bitio.h
│   │   └── CMakeLists.txt
│   ├── bytes/
│   │   ├── bytes.c
│   │   ├── bytes.h
│   │   └── CMakeLists.txt
│   ├── dma/
│   │   ├── CMakeLists.txt
│   │   ├── dma.c
│   │   └── dma.h
│   ├── endian/
│   │   ├── CMakeLists.txt
│   │   ├── endian.c
│   │   └── endian.h
│   ├── float_bits/
│   │   ├── CMakeLists.txt
│   │   ├── float_bits.c
│   │   └── float_bits.h
│   ├── membuild/
│   │   ├── CMakeLists.txt
│   │   ├── membuild.c
│   │   └── membuild.h
│   ├── plaintext/
│   │   ├── CMakeLists.txt
│   │   ├── plaintext.c
│   │   └── plaintext.h
│   ├── protoframe/
│   │   ├── CMakeLists.txt
│   │   ├── protoframe.c
│   │   └── protoframe.h
│   ├── protomem/
│   │   ├── CMakeLists.txt
│   │   ├── protomem.c
│   │   └── protomem.h
│   ├── protostr/
│   │   ├── CMakeLists.txt
│   │   ├── protostr.c
│   │   └── protostr.h
│   ├── psram_pool/
│   │   ├── CMakeLists.txt
│   │   ├── psram_pool.c
│   │   └── psram_pool.h
│   ├── rawmemcpy/
│   │   ├── CMakeLists.txt
│   │   ├── rawmemcpy.c
│   │   └── rawmemcpy.h
│   ├── secure/
│   │   ├── CMakeLists.txt
│   │   ├── secure.c
│   │   └── secure.h
│   ├── span/
│   │   ├── CMakeLists.txt
│   │   ├── span.c
│   │   └── span.h
│   ├── swar/
│   │   ├── CMakeLists.txt
│   │   ├── swar.c
│   │   └── swar.h
│   ├── CMakeLists.txt
│   └── ring.h
├── network_drivers/
│   ├── application/
│   │   ├── binary_asset_blobs/
│   │   │   ├── binary_asset_blobs.c
│   │   │   ├── binary_asset_blobs.h
│   │   │   └── CMakeLists.txt
│   │   ├── file_serving/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── file_serving.c
│   │   │   └── file_serving.h
│   │   ├── http_range/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── http_range.c
│   │   │   └── http_range.h
│   │   ├── mdns_adaptive/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── mdns_adaptive.c
│   │   │   └── mdns_adaptive.h
│   │   ├── mdns_service/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── mdns_service.c
│   │   │   └── mdns_service.h
│   │   ├── ntp/
│   │   │   ├── CMakeLists.txt
│   │   │   └── ntp.h
│   │   ├── ntp_server/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ntp_server.c
│   │   │   └── ntp_server.h
│   │   ├── ntp_service/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ntp_service.c
│   │   │   └── ntp_service.h
│   │   ├── nts/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── nts.c
│   │   │   └── nts.h
│   │   ├── ptp/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ptp.c
│   │   │   └── ptp.h
│   │   ├── sftp/
│   │   │   ├── sftp/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── sftp.c
│   │   │   │   └── sftp.h
│   │   │   ├── ssh_sftp/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── ssh_sftp.c
│   │   │   │   └── ssh_sftp.h
│   │   │   └── CMakeLists.txt
│   │   ├── smb/
│   │   │   ├── ntlm/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── ntlm.c
│   │   │   │   └── ntlm.h
│   │   │   ├── ntlmssp/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── ntlmssp.c
│   │   │   │   └── ntlmssp.h
│   │   │   ├── smb2/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── smb2.c
│   │   │   │   └── smb2.h
│   │   │   ├── smb_client/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── smb_client.c
│   │   │   │   └── smb_client.h
│   │   │   ├── spnego/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── spnego.c
│   │   │   │   └── spnego.h
│   │   │   └── CMakeLists.txt
│   │   ├── upload_service/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── upload_service.c
│   │   │   └── upload_service.h
│   │   ├── web_assets/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── web_assets.c
│   │   │   └── web_assets.h
│   │   ├── webdav/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── webdav.c
│   │   │   └── webdav.h
│   │   └── CMakeLists.txt
│   ├── datalink/
│   │   ├── datalink/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── datalink.c
│   │   │   └── datalink.h
│   │   ├── roaming/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── roaming.c
│   │   │   └── roaming.h
│   │   └── CMakeLists.txt
│   ├── network/
│   │   ├── dns/
│   │   │   ├── dns/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── dns.c
│   │   │   │   └── dns.h
│   │   │   ├── dns_resolver/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── dns_resolver.c
│   │   │   │   └── dns_resolver.h
│   │   │   ├── dns_server/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── dns_server.c
│   │   │   │   └── dns_server.h
│   │   │   ├── dns_wire/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── dns_wire.c
│   │   │   │   └── dns_wire.h
│   │   │   └── CMakeLists.txt
│   │   ├── forward/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── forward.c
│   │   │   └── forward.h
│   │   ├── CMakeLists.txt
│   │   ├── network.c
│   │   └── network.h
│   ├── physical/
│   │   ├── physical/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── physical.c
│   │   │   └── physical.h
│   │   ├── radio_power/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── radio_power.c
│   │   │   └── radio_power.h
│   │   └── CMakeLists.txt
│   ├── presentation/
│   │   ├── codec/
│   │   │   ├── base64/
│   │   │   │   ├── base64.c
│   │   │   │   ├── base64.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── cbor/
│   │   │   │   ├── cbor.c
│   │   │   │   ├── cbor.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── deflate/
│   │   │   │   ├── deflate/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── deflate.c
│   │   │   │   │   └── deflate.h
│   │   │   │   ├── rfc1951/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── rfc1951.c
│   │   │   │   │   └── rfc1951.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── hpack_prim/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── hpack_prim.c
│   │   │   │   └── hpack_prim.h
│   │   │   ├── inflate/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── inflate.c
│   │   │   │   └── inflate.h
│   │   │   ├── json/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── json.c
│   │   │   │   └── json.h
│   │   │   ├── msgpack/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── msgpack.c
│   │   │   │   └── msgpack.h
│   │   │   ├── multipart/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── multipart.c
│   │   │   │   └── multipart.h
│   │   │   ├── CMakeLists.txt
│   │   │   └── codec.h
│   │   ├── http/
│   │   │   ├── auth/
│   │   │   │   ├── auth.c
│   │   │   │   ├── auth.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── http2/
│   │   │   │   ├── h2_conn/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── h2_conn.c
│   │   │   │   │   └── h2_conn.h
│   │   │   │   ├── h2_frame/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── h2_frame.c
│   │   │   │   │   └── h2_frame.h
│   │   │   │   ├── h2_server/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── h2_server.c
│   │   │   │   │   └── h2_server.h
│   │   │   │   ├── hpack/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── hpack.c
│   │   │   │   │   └── hpack.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── http3/
│   │   │   │   ├── h3_conn/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── h3_conn.c
│   │   │   │   │   └── h3_conn.h
│   │   │   │   ├── h3_frame/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── h3_frame.c
│   │   │   │   │   └── h3_frame.h
│   │   │   │   ├── h3_server/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── h3_server.c
│   │   │   │   │   └── h3_server.h
│   │   │   │   ├── qpack/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── qpack.c
│   │   │   │   │   └── qpack.h
│   │   │   │   ├── quic_conn/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── quic_conn.c
│   │   │   │   │   └── quic_conn.h
│   │   │   │   ├── quic_crypto/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── quic_crypto.c
│   │   │   │   │   └── quic_crypto.h
│   │   │   │   ├── quic_frame/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── quic_frame.c
│   │   │   │   │   └── quic_frame.h
│   │   │   │   ├── quic_packet/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── quic_packet.c
│   │   │   │   │   └── quic_packet.h
│   │   │   │   ├── quic_server/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── quic_server.c
│   │   │   │   │   └── quic_server.h
│   │   │   │   ├── quic_tls/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── quic_tls.c
│   │   │   │   │   └── quic_tls.h
│   │   │   │   ├── quic_tp/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── quic_tp.c
│   │   │   │   │   └── quic_tp.h
│   │   │   │   ├── quic_varint/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── quic_varint.c
│   │   │   │   │   └── quic_varint.h
│   │   │   │   ├── tls13_msg/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── tls13_msg.c
│   │   │   │   │   └── tls13_msg.h
│   │   │   │   ├── tls13_rpk/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── tls13_rpk.c
│   │   │   │   │   └── tls13_rpk.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── http_parser/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── http_parser.c
│   │   │   │   └── http_parser.h
│   │   │   ├── httpcache/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── httpcache.c
│   │   │   │   └── httpcache.h
│   │   │   ├── route/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── http_route.c
│   │   │   │   └── http_route.h
│   │   │   ├── sse/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── sse.c
│   │   │   │   └── sse.h
│   │   │   ├── websocket/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── websocket.c
│   │   │   │   └── websocket.h
│   │   │   ├── CMakeLists.txt
│   │   │   ├── http.c
│   │   │   └── http.h
│   │   ├── security/
│   │   │   ├── dtls/
│   │   │   │   ├── dtls_conn/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── dtls_conn.c
│   │   │   │   │   └── dtls_conn.h
│   │   │   │   ├── dtls_handshake/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── dtls_handshake.c
│   │   │   │   │   └── dtls_handshake.h
│   │   │   │   ├── dtls_record/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── dtls_record.c
│   │   │   │   │   └── dtls_record.h
│   │   │   │   └── CMakeLists.txt
│   │   │   └── CMakeLists.txt
│   │   ├── ssh/
│   │   │   ├── app/
│   │   │   │   ├── client/
│   │   │   │   │   ├── client.c
│   │   │   │   │   ├── client.h
│   │   │   │   │   └── CMakeLists.txt
│   │   │   │   ├── server/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── server.c
│   │   │   │   │   └── server.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── auth/
│   │   │   │   ├── auth.c
│   │   │   │   ├── auth.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── client/
│   │   │   │   ├── client.c
│   │   │   │   ├── client.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── connection/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── connection.c
│   │   │   │   └── connection.h
│   │   │   ├── network/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── network.c
│   │   │   │   └── network.h
│   │   │   ├── server/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── server.c
│   │   │   │   └── server.h
│   │   │   ├── transport/
│   │   │   │   ├── comp/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── comp.c
│   │   │   │   │   └── comp.h
│   │   │   │   ├── extension/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── extension.c
│   │   │   │   │   └── extension.h
│   │   │   │   ├── inflate/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── inflate.c
│   │   │   │   │   └── inflate.h
│   │   │   │   ├── phase_machine/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── phase_machine.c
│   │   │   │   │   └── phase_machine.h
│   │   │   │   ├── ssh_kexhash/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── ssh_kexhash.c
│   │   │   │   │   └── ssh_kexhash.h
│   │   │   │   ├── ssh_rsa/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── ssh_rsa.c
│   │   │   │   │   └── ssh_rsa.h
│   │   │   │   ├── transport/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── transport.c
│   │   │   │   │   └── transport.h
│   │   │   │   ├── zlib/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── zlib.c
│   │   │   │   │   └── zlib.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── CMakeLists.txt
│   │   │   ├── common.h
│   │   │   ├── ssh.c
│   │   │   └── ssh.h
│   │   ├── telnet/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── telnet.c
│   │   │   └── telnet.h
│   │   ├── CMakeLists.txt
│   │   ├── presentation.c
│   │   └── presentation.h
│   ├── session/
│   │   ├── scp/
│   │   │   ├── scp/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── scp.c
│   │   │   │   └── scp.h
│   │   │   ├── ssh_scp/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── ssh_scp.c
│   │   │   │   └── ssh_scp.h
│   │   │   └── CMakeLists.txt
│   │   ├── sse/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sse.c
│   │   │   └── sse.h
│   │   ├── ws/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ws.c
│   │   │   └── ws.h
│   │   ├── CMakeLists.txt
│   │   ├── session.c
│   │   └── session.h
│   ├── tls/
│   │   ├── handshake/
│   │   │   ├── CMakeLists.txt
│   │   │   └── handshake.c
│   │   ├── key_schedule/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── key_schedule.c
│   │   │   └── key_schedule.h
│   │   ├── record/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── record.c
│   │   │   └── record.h
│   │   ├── CMakeLists.txt
│   │   ├── tls.c
│   │   └── tls.h
│   ├── transport/
│   │   ├── diffserv/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── diffserv.c
│   │   │   └── diffserv.h
│   │   ├── happy_eyeballs/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── happy_eyeballs.c
│   │   │   └── happy_eyeballs.h
│   │   ├── net_addr/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── net_addr.c
│   │   │   └── net_addr.h
│   │   ├── proxy_protocol/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── proxy_protocol.c
│   │   │   └── proxy_protocol.h
│   │   ├── tcp/
│   │   │   ├── client/
│   │   │   │   ├── client.c
│   │   │   │   ├── client.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── lower/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── lower.c
│   │   │   │   └── lower.h
│   │   │   ├── protocol/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── protocol.c
│   │   │   │   └── protocol.h
│   │   │   ├── server/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── server.c
│   │   │   │   └── server.h
│   │   │   ├── CMakeLists.txt
│   │   │   ├── common.h
│   │   │   ├── evt.h
│   │   │   ├── tcp.c
│   │   │   └── tcp.h
│   │   ├── udp/
│   │   │   ├── client/
│   │   │   │   ├── client.c
│   │   │   │   ├── client.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── server/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── server.c
│   │   │   │   └── server.h
│   │   │   ├── CMakeLists.txt
│   │   │   ├── common.h
│   │   │   ├── udp.c
│   │   │   └── udp.h
│   │   └── CMakeLists.txt
│   └── CMakeLists.txt
├── server/
│   ├── clock/
│   │   ├── clock.c
│   │   ├── clock.h
│   │   └── CMakeLists.txt
│   ├── core/
│   │   ├── exc_decoder/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── exc_decoder.c
│   │   │   └── exc_decoder.h
│   │   ├── failsafe/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── failsafe.c
│   │   │   └── failsafe.h
│   │   ├── guardrails/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── guardrails.c
│   │   │   └── guardrails.h
│   │   ├── logbuf/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── logbuf.c
│   │   │   └── logbuf.h
│   │   ├── power_mgmt/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── power_mgmt.c
│   │   │   └── power_mgmt.h
│   │   ├── preempt_queue/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── preempt_queue.c
│   │   │   └── preempt_queue.h
│   │   ├── provisioning_service/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── provisioning_service.c
│   │   │   └── provisioning_service.h
│   │   ├── sleep_sched/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sleep_sched.c
│   │   │   └── sleep_sched.h
│   │   ├── worker/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── worker.c
│   │   │   └── worker.h
│   │   ├── CMakeLists.txt
│   │   ├── exc_coredump.c
│   │   └── proto_handler.h
│   ├── io/
│   │   ├── http_clock/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── http_clock.c
│   │   │   └── http_clock.h
│   │   ├── CMakeLists.txt
│   │   ├── middleware.c
│   │   ├── response.c
│   │   ├── webdav_handler.c
│   │   ├── webdav_handler.h
│   │   └── websocket_sse.c
│   ├── net/
│   │   ├── gateway/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── gateway.c
│   │   │   └── gateway.h
│   │   ├── iface_bridge/
│   │   │   ├── iface_bridge/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── iface_bridge.c
│   │   │   │   └── iface_bridge.h
│   │   │   ├── iface_bridge_hw/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── iface_bridge_hw.c
│   │   │   │   └── iface_bridge_hw.h
│   │   │   └── CMakeLists.txt
│   │   ├── netadapt/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── netadapt.c
│   │   │   └── netadapt.h
│   │   ├── relay/
│   │   │   ├── relay/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── relay.c
│   │   │   │   └── relay.h
│   │   │   ├── relay_listener/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── relay_listener.c
│   │   │   │   └── relay_listener.h
│   │   │   └── CMakeLists.txt
│   │   ├── sockpool/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sockpool.c
│   │   │   └── sockpool.h
│   │   └── CMakeLists.txt
│   ├── peripherals/
│   │   ├── ad9238/
│   │   │   ├── ad9238.c
│   │   │   ├── ad9238.h
│   │   │   └── CMakeLists.txt
│   │   ├── ads1115/
│   │   │   ├── ads1115.c
│   │   │   ├── ads1115.h
│   │   │   └── CMakeLists.txt
│   │   ├── dmx/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── dmx.c
│   │   │   └── dmx.h
│   │   ├── dshot/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── dshot.c
│   │   │   └── dshot.h
│   │   ├── fdc2214/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── fdc2214.c
│   │   │   └── fdc2214.h
│   │   ├── hmmd/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── hmmd.c
│   │   │   └── hmmd.h
│   │   ├── ina219/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ina219.c
│   │   │   └── ina219.h
│   │   ├── ld2410/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ld2410.c
│   │   │   └── ld2410.h
│   │   ├── ldc1614/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ldc1614.c
│   │   │   └── ldc1614.h
│   │   ├── mpr121/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── mpr121.c
│   │   │   └── mpr121.h
│   │   ├── pca9685/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── pca9685.c
│   │   │   └── pca9685.h
│   │   ├── pmbus/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── pmbus.c
│   │   │   └── pmbus.h
│   │   ├── pn532/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── pn532.c
│   │   │   └── pn532.h
│   │   ├── rcwl0516/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── rcwl0516.c
│   │   │   └── rcwl0516.h
│   │   ├── rtc/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── rtc.c
│   │   │   └── rtc.h
│   │   ├── sdi12/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sdi12.c
│   │   │   └── sdi12.h
│   │   ├── sen0192/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sen0192.c
│   │   │   └── sen0192.h
│   │   ├── sht3x/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sht3x.c
│   │   │   └── sht3x.h
│   │   ├── smbus/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── smbus.c
│   │   │   └── smbus.h
│   │   ├── vl53l0x/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── vl53l0x.c
│   │   │   └── vl53l0x.h
│   │   ├── CMakeLists.txt
│   │   ├── i2c.h
│   │   ├── spi.h
│   │   └── uart.h
│   ├── security/
│   │   ├── audit_log/
│   │   │   ├── audit_log.c
│   │   │   ├── audit_log.h
│   │   │   └── CMakeLists.txt
│   │   ├── auth_lockout/
│   │   │   ├── auth_lockout.c
│   │   │   ├── auth_lockout.h
│   │   │   └── CMakeLists.txt
│   │   ├── csrf/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── csrf.c
│   │   │   └── csrf.h
│   │   ├── forwarded_trust/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── forwarded_trust.c
│   │   │   └── forwarded_trust.h
│   │   ├── tls_policy/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── tls_policy.c
│   │   │   └── tls_policy.h
│   │   └── CMakeLists.txt
│   ├── signaling/
│   │   ├── bus_capture/
│   │   │   ├── bus_capture.c
│   │   │   ├── bus_capture.h
│   │   │   └── CMakeLists.txt
│   │   ├── device_id/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── device_id.c
│   │   │   └── device_id.h
│   │   ├── gpio_map/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── gpio_map.c
│   │   │   └── gpio_map.h
│   │   ├── hw_health/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── hw_health.c
│   │   │   └── hw_health.h
│   │   ├── link_manager/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── link_manager.c
│   │   │   └── link_manager.h
│   │   ├── signaling/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── signaling.c
│   │   │   └── signaling.h
│   │   ├── trace_capture/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── trace_capture.c
│   │   │   └── trace_capture.h
│   │   ├── CMakeLists.txt
│   │   └── gpio_map_routes.c
│   ├── storage/
│   │   ├── config_io/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── config_io.c
│   │   │   └── config_io.h
│   │   ├── config_store/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── config_store.c
│   │   │   └── config_store.h
│   │   ├── filesystem/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── filesystem.c
│   │   │   └── filesystem.h
│   │   ├── hotswap/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── hotswap.c
│   │   │   └── hotswap.h
│   │   ├── mnt/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── mnt.c
│   │   │   └── mnt.h
│   │   ├── mnt_ram/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── mnt_ram.c
│   │   │   └── mnt_ram.h
│   │   ├── partition_monitor/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── partition_monitor.c
│   │   │   ├── partition_monitor.h
│   │   │   └── partition_monitor_routes.c
│   │   ├── wearlevel/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── wearlevel.c
│   │   │   └── wearlevel.h
│   │   └── CMakeLists.txt
│   ├── update/
│   │   ├── ota_rollback/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ota_rollback.c
│   │   │   └── ota_rollback.h
│   │   ├── ota_service/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ota_service.c
│   │   │   └── ota_service.h
│   │   └── CMakeLists.txt
│   ├── web/
│   │   ├── dashboard/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── dashboard.c
│   │   │   ├── dashboard.h
│   │   │   └── dashboard_routes.c
│   │   ├── edge_cache/
│   │   │   ├── edge_cache/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── edge_cache.c
│   │   │   │   └── edge_cache.h
│   │   │   ├── edge_cache_proxy/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── edge_cache_proxy.c
│   │   │   │   └── edge_cache_proxy.h
│   │   │   ├── edge_cache_sd/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── edge_cache_sd.c
│   │   │   │   └── edge_cache_sd.h
│   │   │   ├── edge_fetch/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── edge_fetch.c
│   │   │   │   └── edge_fetch.h
│   │   │   ├── edge_mesh/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── edge_mesh.c
│   │   │   │   └── edge_mesh.h
│   │   │   └── CMakeLists.txt
│   │   ├── spa_router/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── spa_router.c
│   │   │   └── spa_router.h
│   │   ├── web_terminal/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── web_terminal.c
│   │   │   └── web_terminal.h
│   │   └── CMakeLists.txt
│   ├── CMakeLists.txt
│   ├── protocore_builtins.c
│   └── regex.c
├── services/
│   ├── energy/
│   │   ├── c37118/
│   │   │   ├── c37118.c
│   │   │   ├── c37118.h
│   │   │   └── CMakeLists.txt
│   │   ├── dnp3/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── dnp3.c
│   │   │   └── dnp3.h
│   │   ├── goose/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── goose.c
│   │   │   └── goose.h
│   │   ├── iccp/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── iccp.c
│   │   │   └── iccp.h
│   │   ├── iec60870/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── iec60870.c
│   │   │   └── iec60870.h
│   │   ├── mms/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── mms.c
│   │   │   └── mms.h
│   │   ├── openadr/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── openadr.c
│   │   │   └── openadr.h
│   │   ├── sep2/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sep2.c
│   │   │   └── sep2.h
│   │   ├── sunspec/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sunspec.c
│   │   │   └── sunspec.h
│   │   └── CMakeLists.txt
│   ├── fieldbus/
│   │   ├── ads/
│   │   │   ├── ads.c
│   │   │   ├── ads.h
│   │   │   └── CMakeLists.txt
│   │   ├── bacnet/
│   │   │   ├── bacnet.c
│   │   │   ├── bacnet.h
│   │   │   └── CMakeLists.txt
│   │   ├── canopen/
│   │   │   ├── canopen.c
│   │   │   ├── canopen.h
│   │   │   └── CMakeLists.txt
│   │   ├── cclink/
│   │   │   ├── cclink.c
│   │   │   ├── cclink.h
│   │   │   └── CMakeLists.txt
│   │   ├── cia402/
│   │   │   ├── cia402.c
│   │   │   ├── cia402.h
│   │   │   └── CMakeLists.txt
│   │   ├── cip/
│   │   │   ├── cip.c
│   │   │   ├── cip.h
│   │   │   └── CMakeLists.txt
│   │   ├── cotp/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── cotp.c
│   │   │   └── cotp.h
│   │   ├── devicenet/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── devicenet.c
│   │   │   └── devicenet.h
│   │   ├── df1/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── df1.c
│   │   │   └── df1.h
│   │   ├── directnet/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── directnet.c
│   │   │   └── directnet.h
│   │   ├── enip/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── enip.c
│   │   │   └── enip.h
│   │   ├── fins/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── fins.c
│   │   │   └── fins.h
│   │   ├── hart/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── hart.c
│   │   │   └── hart.h
│   │   ├── hostlink/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── hostlink.c
│   │   │   └── hostlink.h
│   │   ├── interbus/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── interbus.c
│   │   │   └── interbus.h
│   │   ├── iolink/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── iolink.c
│   │   │   └── iolink.h
│   │   ├── j1939/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── j1939.c
│   │   │   └── j1939.h
│   │   ├── lonworks/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── lonworks.c
│   │   │   └── lonworks.h
│   │   ├── mbplus/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── mbplus.c
│   │   │   └── mbplus.h
│   │   ├── mbus/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── mbus.c
│   │   │   └── mbus.h
│   │   ├── melsec/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── melsec.c
│   │   │   └── melsec.h
│   │   ├── modbus/
│   │   │   ├── modbus/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── modbus.c
│   │   │   │   └── modbus.h
│   │   │   ├── modbus_master/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── modbus_master.c
│   │   │   │   └── modbus_master.h
│   │   │   └── CMakeLists.txt
│   │   ├── powerlink/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── powerlink.c
│   │   │   └── powerlink.h
│   │   ├── profibus/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── profibus.c
│   │   │   └── profibus.h
│   │   ├── profinet/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── profinet.c
│   │   │   └── profinet.h
│   │   ├── rawl2/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── rawl2.c
│   │   │   └── rawl2.h
│   │   ├── s7comm/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── s7comm.c
│   │   │   └── s7comm.h
│   │   ├── sercos/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sercos.c
│   │   │   └── sercos.h
│   │   ├── simatic/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── simatic.c
│   │   │   └── simatic.h
│   │   ├── snp/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── snp.c
│   │   │   └── snp.h
│   │   └── CMakeLists.txt
│   ├── file_transfer/
│   │   ├── ftp/
│   │   │   ├── ftp/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── ftp.c
│   │   │   │   └── ftp.h
│   │   │   ├── ftp_session/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── ftp_session.c
│   │   │   │   └── ftp_session.h
│   │   │   └── CMakeLists.txt
│   │   ├── http_delivery/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── http_delivery.c
│   │   │   ├── http_delivery.h
│   │   │   └── http_delivery_routes.c
│   │   └── CMakeLists.txt
│   ├── instrumentation/
│   │   ├── gpib/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── gpib.c
│   │   │   └── gpib.h
│   │   ├── hislip/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── hislip.c
│   │   │   └── hislip.h
│   │   ├── scpi/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── scpi.c
│   │   │   └── scpi.h
│   │   ├── vxi11/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── vxi11.c
│   │   │   └── vxi11.h
│   │   └── CMakeLists.txt
│   ├── iot/
│   │   ├── amqp/
│   │   │   ├── amqp.c
│   │   │   ├── amqp.h
│   │   │   └── CMakeLists.txt
│   │   ├── cloudevents/
│   │   │   ├── cloudevents.c
│   │   │   ├── cloudevents.h
│   │   │   └── CMakeLists.txt
│   │   ├── coap/
│   │   │   ├── coap/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── coap.c
│   │   │   │   └── coap.h
│   │   │   ├── coaps/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── coaps.c
│   │   │   │   └── coaps.h
│   │   │   ├── coaps_server/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── coaps_server.c
│   │   │   │   └── coaps_server.h
│   │   │   └── CMakeLists.txt
│   │   ├── dds/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── dds.c
│   │   │   └── dds.h
│   │   ├── graphql/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── graphql.c
│   │   │   └── graphql.h
│   │   ├── grpcweb/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── grpcweb.c
│   │   │   └── grpcweb.h
│   │   ├── lwm2m/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── lwm2m_tlv.c
│   │   │   └── lwm2m_tlv.h
│   │   ├── mqtt/
│   │   │   ├── mqtt/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── mqtt.c
│   │   │   │   └── mqtt.h
│   │   │   ├── mqtt_sn/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── mqtt_sn.c
│   │   │   │   └── mqtt_sn.h
│   │   │   └── CMakeLists.txt
│   │   ├── nats/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── nats.c
│   │   │   └── nats.h
│   │   ├── protobuf/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── protobuf.c
│   │   │   └── protobuf.h
│   │   ├── redis_resp/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── redis_resp.c
│   │   │   └── redis_resp.h
│   │   ├── senml/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── senml.c
│   │   │   └── senml.h
│   │   ├── sparkplug/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sparkplug.c
│   │   │   └── sparkplug.h
│   │   ├── statsd/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── statsd.c
│   │   │   └── statsd.h
│   │   ├── stomp/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── stomp.c
│   │   │   └── stomp.h
│   │   ├── telemetry/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── telemetry.c
│   │   │   └── telemetry.h
│   │   ├── udp_telemetry/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── udp_telemetry.c
│   │   │   └── udp_telemetry.h
│   │   ├── wamp/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── wamp.c
│   │   │   └── wamp.h
│   │   ├── xmpp/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── xmpp.c
│   │   │   └── xmpp.h
│   │   └── CMakeLists.txt
│   ├── machine_tool/
│   │   ├── atc/
│   │   │   ├── atc.c
│   │   │   ├── atc.h
│   │   │   └── CMakeLists.txt
│   │   ├── dnc/
│   │   │   ├── dnc/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── dnc.c
│   │   │   │   └── dnc.h
│   │   │   ├── dnc_stream/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── dnc_stream.c
│   │   │   │   └── dnc_stream.h
│   │   │   └── CMakeLists.txt
│   │   ├── fanuc_j519/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── fanuc_j519.c
│   │   │   └── fanuc_j519.h
│   │   ├── focas/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── focas.c
│   │   │   └── focas.h
│   │   ├── haas_mdc/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── haas_mdc.c
│   │   │   └── haas_mdc.h
│   │   ├── lsv2/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── lsv2.c
│   │   │   └── lsv2.h
│   │   ├── mtconnect/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── mtconnect.c
│   │   │   └── mtconnect.h
│   │   ├── packml/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── packml.c
│   │   │   └── packml.h
│   │   ├── safety_scl/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── safety_scl.c
│   │   │   └── safety_scl.h
│   │   └── CMakeLists.txt
│   ├── net/
│   │   ├── flow_export/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── flow_export.c
│   │   │   └── flow_export.h
│   │   ├── http_client/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── http_client.c
│   │   │   └── http_client.h
│   │   ├── smtp/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── smtp.c
│   │   │   └── smtp.h
│   │   ├── snmp/
│   │   │   ├── snmp_agent/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── snmp_agent.c
│   │   │   │   └── snmp_agent.h
│   │   │   ├── snmp_ber/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── snmp_ber.c
│   │   │   │   └── snmp_ber.h
│   │   │   ├── snmp_crypto/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── snmp_crypto.c
│   │   │   │   └── snmp_crypto.h
│   │   │   ├── snmp_notify/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── snmp_notify.c
│   │   │   │   └── snmp_notify.h
│   │   │   ├── snmp_v3/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── snmp_v3.c
│   │   │   │   └── snmp_v3.h
│   │   │   └── CMakeLists.txt
│   │   ├── syslog/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── syslog.c
│   │   │   └── syslog.h
│   │   ├── webhook/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── webhook.c
│   │   │   └── webhook.h
│   │   ├── ws_client/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ws_client.c
│   │   │   └── ws_client.h
│   │   └── CMakeLists.txt
│   ├── opcua/
│   │   ├── models/
│   │   │   ├── euromap77/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── euromap77.c
│   │   │   │   └── euromap77.h
│   │   │   ├── robotics/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── robotics.c
│   │   │   │   └── robotics.h
│   │   │   ├── umati/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── umati.c
│   │   │   │   └── umati.h
│   │   │   └── CMakeLists.txt
│   │   ├── opcua_client/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── opcua_client.c
│   │   │   └── opcua_client.h
│   │   ├── CMakeLists.txt
│   │   ├── opcua.c
│   │   └── opcua.h
│   ├── radio/
│   │   ├── ble_gatt/
│   │   │   ├── ble_gatt.c
│   │   │   ├── ble_gatt.h
│   │   │   └── CMakeLists.txt
│   │   ├── cc1101/
│   │   │   ├── cc1101.c
│   │   │   ├── cc1101.h
│   │   │   └── CMakeLists.txt
│   │   ├── enocean/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── enocean.c
│   │   │   └── enocean.h
│   │   ├── espnow/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── espnow.c
│   │   │   └── espnow.h
│   │   ├── lora/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── lora.c
│   │   │   └── lora.h
│   │   ├── nrf24/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── nrf24.c
│   │   │   └── nrf24.h
│   │   ├── promisc/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── promisc.c
│   │   │   └── promisc.h
│   │   ├── radio_sniff/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── radio_sniff.c
│   │   │   └── radio_sniff.h
│   │   ├── sigfox/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sigfox.c
│   │   │   └── sigfox.h
│   │   ├── thread/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── thread.c
│   │   │   └── thread.h
│   │   ├── wifi_sniffer/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── wifi_sniffer.c
│   │   │   └── wifi_sniffer.h
│   │   ├── wisun/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── wisun.c
│   │   │   └── wisun.h
│   │   ├── zigbee/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── zigbee.c
│   │   │   └── zigbee.h
│   │   ├── zwave/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── zwave.c
│   │   │   └── zwave.h
│   │   └── CMakeLists.txt
│   ├── security/
│   │   ├── ikev2/
│   │   │   ├── ikev2/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── ikev2.c
│   │   │   │   └── ikev2.h
│   │   │   ├── ikev2_natt/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── ikev2_natt.c
│   │   │   │   └── ikev2_natt.h
│   │   │   └── CMakeLists.txt
│   │   ├── jwt/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── jwt.c
│   │   │   └── jwt.h
│   │   ├── oauth2/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── oauth2.c
│   │   │   └── oauth2.h
│   │   ├── oidc/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── oidc.c
│   │   │   └── oidc.h
│   │   ├── totp/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── totp.c
│   │   │   └── totp.h
│   │   └── CMakeLists.txt
│   ├── southbound/
│   │   ├── sb_modbus/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sb_modbus.c
│   │   │   └── sb_modbus.h
│   │   ├── southbound/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── southbound.c
│   │   │   └── southbound.h
│   │   └── CMakeLists.txt
│   ├── storage/
│   │   ├── dbm/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── dbm.c
│   │   │   └── dbm.h
│   │   ├── docstore/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── docstore.c
│   │   │   └── docstore.h
│   │   ├── sqlite/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── sqlite_format.c
│   │   │   └── sqlite_format.h
│   │   ├── wal/
│   │   │   ├── wal/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── wal.c
│   │   │   │   └── wal.h
│   │   │   ├── wal_store/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── wal_store.c
│   │   │   │   └── wal_store.h
│   │   │   ├── CMakeLists.txt
│   │   │   └── wal_fs.h
│   │   └── CMakeLists.txt
│   ├── system/
│   │   ├── control/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── control.c
│   │   │   └── control.h
│   │   ├── esp/
│   │   │   ├── esp/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── esp.c
│   │   │   │   └── esp.h
│   │   │   ├── ipsec_db/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── ipsec_db.c
│   │   │   │   └── ipsec_db.h
│   │   │   └── CMakeLists.txt
│   │   └── CMakeLists.txt
│   ├── timing_position/
│   │   ├── gnss/
│   │   │   ├── gnss_survey/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── gnss_survey.c
│   │   │   │   └── gnss_survey.h
│   │   │   ├── ntrip_caster/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── ntrip_caster.c
│   │   │   │   └── ntrip_caster.h
│   │   │   ├── ntrip_caster_listener/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── ntrip_caster_listener.c
│   │   │   │   └── ntrip_caster_listener.h
│   │   │   ├── rtcm3/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── rtcm3.c
│   │   │   │   └── rtcm3.h
│   │   │   └── CMakeLists.txt
│   │   ├── nmea0183/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── nmea0183.c
│   │   │   └── nmea0183.h
│   │   ├── nmea2000/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── nmea2000.c
│   │   │   └── nmea2000.h
│   │   ├── time_source/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── time_source.c
│   │   │   └── time_source.h
│   │   ├── ubx/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ubx.c
│   │   │   └── ubx.h
│   │   └── CMakeLists.txt
│   ├── transportation/
│   │   ├── j2735/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── j2735.c
│   │   │   └── j2735.h
│   │   ├── nema_ts2/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── nema_ts2.c
│   │   │   └── nema_ts2.h
│   │   ├── ntcip/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ntcip.c
│   │   │   └── ntcip.h
│   │   ├── ocit/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ocit.c
│   │   │   └── ocit.h
│   │   ├── utmc/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── utmc.c
│   │   │   └── utmc.h
│   │   ├── wave/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── wave.c
│   │   │   └── wave.h
│   │   └── CMakeLists.txt
│   └── CMakeLists.txt
├── shared/
│   ├── can/
│   │   ├── can.h
│   │   └── CMakeLists.txt
│   ├── crc/
│   │   ├── CMakeLists.txt
│   │   ├── crc.c
│   │   └── crc.h
│   ├── der/
│   │   ├── CMakeLists.txt
│   │   ├── der.c
│   │   └── der.h
│   ├── hex/
│   │   ├── CMakeLists.txt
│   │   ├── hex.c
│   │   └── hex.h
│   ├── http_date/
│   │   ├── CMakeLists.txt
│   │   ├── http_date.c
│   │   └── http_date.h
│   ├── ip/
│   │   ├── CMakeLists.txt
│   │   ├── ip.c
│   │   └── ip.h
│   ├── log/
│   │   ├── CMakeLists.txt
│   │   ├── log.c
│   │   └── log.h
│   ├── mime/
│   │   ├── CMakeLists.txt
│   │   └── mime.h
│   ├── pcap/
│   │   ├── CMakeLists.txt
│   │   ├── pcap.c
│   │   └── pcap.h
│   ├── speed_opt/
│   │   ├── CMakeLists.txt
│   │   └── speed_opt.h
│   ├── time_compat/
│   │   ├── CMakeLists.txt
│   │   ├── time_compat.c
│   │   └── time_compat.h
│   ├── utf8/
│   │   ├── CMakeLists.txt
│   │   ├── utf8.c
│   │   └── utf8.h
│   └── CMakeLists.txt
├── web_assets/
│   ├── favicons/  (288 generated files)
│   ├── input/
│   │   ├── PROTOCORE_DASHBOARD_PAGE.html
│   │   ├── PROTOCORE_METRICS_PROM.txt
│   │   ├── PROTOCORE_PROV_FORM.html
│   │   ├── PROTOCORE_PROV_SAVED_HTML.html
│   │   ├── PROTOCORE_SERVICE_WORKER.js
│   │   ├── PROTOCORE_STATS_JSON.json
│   │   └── PROTOCORE_TERMINAL_PAGE.html
│   ├── themes/  (112 generated files)
│   ├── wizard/
│   │   ├── __init__.py
│   │   ├── build_assets.py
│   │   ├── gen_favicons.py
│   │   ├── gen_theme_blobs.py
│   │   └── gen_themes.py
│   ├── __init__.py
│   └── README.md
├── CMakeLists.txt
├── derived_sizing.h
├── protocore.c
└── protocore_config.h
```

<!-- prettier-ignore-end -->

<!-- END GENERATED SOURCE-TREE -->

</details>

### Build Footprint

Measured flash + static RAM for each optional feature, built in isolation over the
base server on `esp32dev`. Generated from `docs/footprints.json` (produced by the
RPi build matrix) by `tools/ci_tooling/generate/gen_readme_sections.py`.

<details>
<summary><b>Per-feature build footprint</b></summary>

<!-- BEGIN GENERATED BUILD-FOOTPRINT (tools/ci_tooling/generate/gen_readme_sections.py) -->

<!-- prettier-ignore-start -->

Measured on `esp32dev` from each feature's isolated example (one feature enabled over the
base server). Flash is the program image; RAM is static `.data + .bss`. Regenerated by the
Feature Tables workflow from `docs/footprints.json`.

| Feature | Example | Flash (bytes) | Static RAM (bytes) |
| :------ | :------ | ------------: | -----------------: |
| `SIGFOX` | `Drivers/SigfoxUplink` | 267,957 | 21,464 |
| `ENOCEAN+GATEWAY` | `Drivers/EnOceanGateway` | 268,745 | 21,848 |
| `ZWAVE+GATEWAY` | `Drivers/ZWaveGateway` | 268,953 | 21,848 |
| `DMA+PREEMPT_QUEUE+DMA_SIMULATE` | `Peripherals/DmaIngest` | 269,297 | 28,616 |
| `ZIGBEE+GATEWAY` | `Drivers/ZigbeeGateway` | 269,357 | 22,104 |
| `core/SSHCryptoSelfTest` | `L5-Session/SSHCryptoSelfTest` | 269,585 | 24,092 |
| `SEN0192` | `Drivers/Sen0192` | 269,717 | 21,496 |
| `DMA+PREEMPT_QUEUE+GATEWAY+DMA_SIMULATE` | `Drivers/RadioGateway` | 270,329 | 28,728 |
| `LD2410` | `Drivers/Ld2410` | 270,605 | 21,656 |
| `DMA+PREEMPT_QUEUE+FORWARD+DMA_SIMULATE` | `Foundation/InterfaceForward` | 270,993 | 29,104 |
| `THREAD+GATEWAY` | `Drivers/ThreadGateway` | 271,933 | 22,616 |
| `NMEA0183+UBX` | `Drivers/UbloxGnss` | 273,709 | 22,432 |
| `PREEMPT_QUEUE` | `Foundation/PreemptQueue` | 273,861 | 23,968 |
| `NRF24+GATEWAY` | `Drivers/Nrf24Gateway` | 275,969 | 21,688 |
| `LORA+GATEWAY` | `Drivers/LoRaGateway` | 276,189 | 21,688 |
| `PCA9685` | `Drivers/Pca9685` | 276,825 | 24,596 |
| `INA219` | `Drivers/Ina219` | 277,849 | 24,596 |
| `ADS1115` | `Drivers/Ads1115` | 277,873 | 24,612 |
| `SHT3X` | `Drivers/Sht3x` | 277,957 | 24,612 |
| `MPR121` | `Drivers/Mpr121` | 279,981 | 24,700 |
| `PN532+GATEWAY` | `Drivers/NfcGateway` | 288,189 | 21,936 |
| `core/EthernetW5500` | `Peripherals/EthernetW5500` | 469,573 | 73,672 |
| `HISLIP` | `L7-Application/HiSlip` | 725,197 | 44,068 |
| `LSV2` | `L7-Application/HeidenhainLsv2` | 725,481 | 44,068 |
| `SCPI` | `L7-Application/Scpi` | 725,493 | 43,812 |
| `DNS_SERVER` | `L7-Application/DnsServer` | 725,621 | 46,044 |
| `WIFI_SNIFFER+PROMISC` | `Peripherals/WifiSniffer` | 725,945 | 43,660 |
| `GPIB` | `L7-Application/Gpib` | 726,069 | 43,588 |
| `HAAS_MDC` | `L7-Application/HaasMdc` | 726,081 | 43,452 |
| `IKEV2` | `L5-Session/IKEv2` | 726,185 | 43,948 |
| `VXI11` | `L7-Application/Vxi11` | 726,305 | 44,196 |
| `STATSD` | `L7-Application/StatsdMetrics` | 728,069 | 45,148 |
| `PTP` | `L7-Application/Ptp` | 728,113 | 45,036 |
| `SNMP+SNMP_TRAP` | `L7-Application/SnmpTrap` | 728,193 | 45,004 |
| `COAP+COAP_BLOCK+COAP_MAX_PAYLOAD` | `L7-Application/CoapBlock` | 728,729 | 49,588 |
| `COAP+COAP_OBSERVE` | `L7-Application/CoapObserve` | 729,985 | 47,332 |
| `UDP_TELEMETRY` | `L7-Application/UdpTelemetry` | 729,993 | 45,012 |
| `ESPNOW` | `L7-Application/EspNow` | 731,513 | 43,580 |
| `DNC` | `L7-Application/EthernetDnc` | 733,649 | 61,140 |
| `SMTP` | `L7-Application/SmtpAlert` | 735,301 | 61,140 |
| `HTTP_CLIENT` | `L7-Application/HttpClient` | 736,785 | 63,188 |
| `MQTT` | `L7-Application/MqttClient` | 739,985 | 65,340 |
| `ACCEPT_THROTTLE` | `L4-Transport/AcceptThrottle` | 744,233 | 73,836 |
| `core/Basic` | `Foundation/Basic` | 744,237 | 73,828 |
| `RADIO_POWER+RADIO_WIFI_PS` | `L7-Application/RadioPower` | 744,425 | 73,828 |
| `core/CORS` | `L7-Application/CORS` | 744,489 | 73,828 |
| `core/MediaStreaming` | `L7-Application/MediaStreaming` | 744,537 | 73,828 |
| `core/RegexRoutes` | `L7-Application/RegexRoutes` | 744,581 | 73,828 |
| `DIFFSERV` | `L4-Transport/DiffServ` | 744,661 | 73,836 |
| `core/PathParams` | `L7-Application/PathParams` | 744,681 | 73,828 |
| `DEVICE_ID` | `L7-Application/DeviceUuid` | 744,697 | 73,868 |
| `PER_IP_THROTTLE` | `L4-Transport/PerIpThrottle` | 744,801 | 74,276 |
| `core/ResponseHeaders` | `L7-Application/ResponseHeaders` | 744,813 | 73,828 |
| `core/Middleware` | `L7-Application/Middleware` | 744,829 | 73,836 |
| `GUARDRAILS` | `L7-Application/Guardrails` | 745,089 | 73,836 |
| `core/ChunkedResponse` | `L7-Application/ChunkedResponse` | 745,113 | 73,844 |
| `core/NetEgress` | `L7-Application/NetEgress` | 745,137 | 73,828 |
| `core/FormParams` | `L6-Presentation/FormParams` | 745,217 | 73,828 |
| `PARTITION_MONITOR` | `L7-Application/PartitionMonitor` | 745,333 | 73,828 |
| `KEEPALIVE` | `L4-Transport/KeepAlive` | 745,345 | 73,844 |
| `OTA_ROLLBACK` | `L7-Application/OtaRollback` | 745,349 | 73,836 |
| `TOTP` | `L7-Application/Totp` | 745,601 | 73,868 |
| `IP_ALLOWLIST` | `L4-Transport/IpAllowlist` | 745,825 | 73,980 |
| `core/InterfaceFilter` | `L7-Application/InterfaceFilter` | 746,033 | 73,828 |
| `SPA_ROUTER` | `L7-Application/SpaFallback` | 746,069 | 73,828 |
| `DIAG` | `L7-Application/Diagnostics` | 746,305 | 90,220 |
| `core/Templating` | `L7-Application/Templating` | 746,417 | 73,868 |
| `TELNET` | `L5-Session/Telnet` | 746,517 | 74,364 |
| `CONTROL` | `L7-Application/PidTuning` | 746,521 | 81,908 |
| `CSRF` | `L7-Application/Csrf` | 746,593 | 76,516 |
| `GPIO_MAP` | `L7-Application/GpioMap` | 746,873 | 73,884 |
| `MODBUS+MODBUS_MASTER` | `L7-Application/ModbusScan` | 746,893 | 74,108 |
| `CBOR` | `L6-Presentation/Cbor` | 746,921 | 73,908 |
| `IPV6` | `Foundation/IPv6` | 746,949 | 73,828 |
| `AUDIT_LOG` | `L7-Application/AuditLog` | 747,025 | 76,820 |
| `JWT` | `L6-Presentation/JWTAuth` | 747,513 | 80,708 |
| `SYSLOG` | `L7-Application/Syslog` | 747,869 | 75,740 |
| `MSGPACK` | `L6-Presentation/MsgPack` | 748,109 | 73,908 |
| `LOGBUF` | `L7-Application/LogBuffer` | 748,121 | 73,076 |
| `NTP_SERVER+TIME_SOURCE+NMEA0183+NTP` | `L7-Application/NtpServer` | 748,301 | 46,724 |
| `AUTH` | `L6-Presentation/DigestAuth` | 748,585 | 78,492 |
| `MODBUS` | `L7-Application/ModbusTcp` | 748,861 | 70,228 |
| `STATS` | `L7-Application/Stats` | 748,873 | 70,036 |
| `core/Expert` | `Foundation/Expert` | 749,553 | 69,964 |
| `CONFIG_STORE+CONFIG_IO` | `L7-Application/ConfigExport` | 749,705 | 73,904 |
| `AUTH+AUTH_LOCKOUT` | `L6-Presentation/AuthLockout` | 749,829 | 79,068 |
| `DNS_RESOLVER` | `L7-Application/DnsResolver` | 750,481 | 75,116 |
| `STATS+METRICS` | `L7-Application/PrometheusMetrics` | 750,797 | 70,076 |
| `core/Json` | `L6-Presentation/Json` | 750,921 | 69,956 |
| `GRAPHQL` | `L7-Application/GraphQL` | 751,193 | 78,244 |
| `COAP` | `L7-Application/CoAP` | 751,333 | 77,548 |
| `PROVISIONING` | `L7-Application/Provisioning` | 751,917 | 75,448 |
| `AUTH+AUTH_LOCKOUT+FORWARDED_TRUST` | `L6-Presentation/ForwardedTrust` | 752,913 | 79,108 |
| `OTA` | `L7-Application/OTA` | 752,969 | 94,540 |
| `core/Advanced` | `Foundation/Advanced` | 753,277 | 73,940 |
| `ADS` | `L7-Application/AdsClient` | 753,485 | 44,204 |
| `TELEMETRY` | `L7-Application/Telemetry` | 753,525 | 74,152 |
| `SNMP` | `L7-Application/SNMP` | 755,961 | 82,404 |
| `core/BasicAuth` | `L6-Presentation/BasicAuth` | 756,441 | 81,844 |
| `core/DigestAuth` | `L6-Presentation/DigestAuth` | 756,445 | 81,844 |
| `RELAY` | `L7-Application/PortForward` | 756,761 | 104,564 |
| `core/WebSocket` | `L6-Presentation/WebSocket` | 756,825 | 81,844 |
| `HTTP_CLIENT+WEBHOOK` | `L7-Application/Webhook` | 757,109 | 93,580 |
| `OIDC` | `L7-Application/OidcAuth` | 757,189 | 103,188 |
| `core/ServerSentEvents` | `L6-Presentation/ServerSentEvents` | 757,261 | 81,852 |
| `AUTH_LOCKOUT` | `L6-Presentation/AuthLockout` | 757,345 | 82,420 |
| `core/Multipart` | `L6-Presentation/Multipart` | 757,597 | 81,844 |
| `RTC+TIME_SOURCE+NTP` | `Drivers/Rtc` | 757,685 | 48,168 |
| `core/Sysadmin` | `Foundation/Sysadmin` | 758,429 | 73,844 |
| `SIMATIC` | `L7-Application/SimaticSerial` | 758,481 | 83,316 |
| `PACKML` | `L7-Application/PackML` | 758,809 | 81,884 |
| `OAUTH2+HTTP_CLIENT` | `L7-Application/OAuth2` | 759,337 | 96,652 |
| `AUTH_LOCKOUT+FORWARDED_TRUST` | `L6-Presentation/ForwardedTrust` | 760,465 | 82,460 |
| `WS_DEFLATE` | `L6-Presentation/WebSocketCompression` | 761,197 | 90,044 |
| `OPCUA+UMATI` | `L7-Application/Umati` | 761,997 | 80,380 |
| `OPCUA+EUROMAP77` | `L7-Application/Euromap77` | 762,237 | 80,404 |
| `OPCUA+ROBOTICS` | `L7-Application/Robotics` | 762,349 | 80,596 |
| `OPCUA` | `L7-Application/OpcUa` | 762,833 | 84,124 |
| `NTRIP_CASTER` | `L7-Application/NtripCaster` | 765,277 | 72,872 |
| `SMB` | `L7-Application/SmbFileClient` | 765,545 | 70,292 |
| `NTP+TIME_SOURCE` | `L7-Application/TimeSourceFallback` | 765,849 | 75,452 |
| `PROMISC+FORWARD+ETHERNET` | `Peripherals/WifiCapture` | 766,233 | 47,592 |
| `WEB_TERMINAL` | `L6-Presentation/WebTerminal` | 766,489 | 81,924 |
| `OPCUA+OPCUA_CLIENT` | `L7-Application/OpcUaClient` | 766,701 | 86,732 |
| `MDNS` | `L7-Application/mDNS` | 769,825 | 75,736 |
| `NTP` | `L7-Application/SNTP` | 770,417 | 76,384 |
| `MDNS+PROMISC+WIFI_SNIFFER+MDNS_ADAPTIVE` | `L7-Application/MdnsAdaptive` | 771,725 | 75,816 |
| `BUS_CAPTURE+FORWARD+ETHERNET` | `Peripherals/CanCapture` | 772,177 | 45,576 |
| `COAP+DTLS` | `L7-Application/CoapSecure` | 773,197 | 106,268 |
| `IFACE_BRIDGE` | `L7-Application/InterfaceBridge` | 774,973 | 70,796 |
| `EDGE_CACHE+HTTP_CACHE+HTTP_CLIENT` | `L7-Application/EdgeCache` | 777,933 | 119,324 |
| `EDGE_CACHE+HTTP_CACHE+HTTP_CLIENT+EDGE_MESH` | `L7-Application/MeshCache` | 778,433 | 124,240 |
| `EDGE_CACHE+HTTP_CACHE+HTTP_CLIENT+EDGE_MESH+EDGE_CACHE_SLOTS+EDGE_FETCH_SLOTS+MESH_MAX_PEERS` | `L7-Application/MeshCache` | 782,565 | 115,380 |
| `DASHBOARD` | `L7-Application/Dashboard` | 782,989 | 82,212 |
| `ETHERNET` | `Peripherals/Ethernet` | 783,713 | 73,880 |
| `ETHERNET+ETH_W5500+ETH_W5500_CS+ETH_W5500_RST+ETH_W5500_INT+ETH_W5500_SCK+ETH_W5500_MISO+ETH_W5500_MOSI` | `Peripherals/EthernetW5500` | 783,745 | 73,880 |
| `MNT` | `L7-Application/Mnt` | 791,717 | 76,660 |
| `core/FileServing` | `L7-Application/FileServing` | 797,713 | 81,876 |
| `UPLOAD` | `L7-Application/FileUpload` | 798,873 | 101,988 |
| `RANGE` | `L7-Application/Range` | 798,985 | 81,876 |
| `VFS` | `L7-Application/Vfs` | 799,825 | 86,364 |
| `WEBDAV` | `L7-Application/WebDav` | 821,069 | 105,352 |
| `SSH` | `L5-Session/SSHHostKey` | 828,113 | 111,200 |
| `WEBDAV+WEBDAV_MAX_ENTRIES+WEBDAV_BUF_SIZE` | `L7-Application/WebDav` | 828,641 | 92,364 |
| `WS_CLIENT+TLS+WS_CLIENT_TLS` | `L7-Application/WebSocketClient` | 831,333 | 120,548 |
| `WS_CLIENT+TLS+WS_CLIENT_TLS+WS_CLIENT_BUF_SIZE` | `L7-Application/WebSocketClient` | 831,745 | 123,620 |
| `HOTSWAP` | `L7-Application/HotSwapStorage` | 832,013 | 74,768 |
| `ETAG` | `L7-Application/ETag` | 832,909 | 83,140 |
| `WS_CLIENT+TLS+WS_CLIENT_TLS+WS_CLIENT_BUF_SIZE+TLS_ARENA_SIZE` | `L7-Application/WebSocketClient` | 833,481 | 98,792 |
| `SSH+SSH_CLIENT+SSH_CLIENT_MAX_CHANNELS+CLIENT_RX_BUF` | `L5-Session/SSHReverseTunnel` | 837,549 | 109,400 |
| `EXC_DECODER+FTP+FTP_SESSION` | `L7-Application/CoreDump` | 844,789 | 83,476 |
| `HTTP_DELIVERY+FILE_SERVING+RANGE` | `L7-Application/HttpDelivery` | 846,333 | 82,752 |
| `TLS+TLS_ARENA_SIZE` | `L4-Transport/HTTPS` | 849,141 | 98,400 |
| `TLS+TLS_RESUMPTION+TLS_ARENA_SIZE` | `L4-Transport/TlsResumption` | 849,797 | 98,560 |
| `TLS+MTLS+TLS_ARENA_SIZE` | `L4-Transport/mTLS` | 852,593 | 98,408 |
| `TLS` | `L6-Presentation/SecureWebSocket` | 855,873 | 122,020 |
| `TLS+TLS_RESUMPTION` | `L4-Transport/TlsResumption` | 856,693 | 122,180 |
| `TLS+MTLS` | `L4-Transport/mTLS` | 856,829 | 122,356 |
| `POWER_MGMT` | `L7-Application/PowerGovernor` | 873,297 | 77,688 |
| `SSH+FILE_SERVING+SSH_SFTP+SSH_SCP` | `L5-Session/SSHSftp` | 890,397 | 121,544 |

<!-- prettier-ignore-end -->

<!-- END GENERATED BUILD-FOOTPRINT -->

</details>

## Zero Heap Allocation

Every byte of memory the library uses is accounted for at compile time:

<details>
<summary><b>View Zero Heap Allocation Storage Details</b></summary>

| Storage                                                                        | Location                       |
| ------------------------------------------------------------------------------ | ------------------------------ |
| `conn_pool[CONN_POOL_SLOTS]` - TCP connections + ring buffers                  | BSS                            |
| `http_pool[CONN_POOL_SLOTS]` - HTTP request structs                            | BSS                            |
| `ws_pool[MAX_WS_CONNS]` - WebSocket connection state                           | BSS                            |
| `protocore_sse_pool[MAX_SSE_CONNS]` - SSE connection state                     | BSS                            |
| `_queue_storage[EVT_QUEUE_DEPTH * sizeof(TcpEvt)]` - event queue backing store | BSS                            |
| `_queue_struct` - `protocore_platform_queue_ctrl`                              | BSS                            |
| HttpRoute table `HttpRouteCtx.entry[MAX_ROUTES]`                               | mmgr `secure` (persistent end) |

</details>

`proto_begin()` creates each listener's event queue through `protocore_platform_queue_create` (`xQueueCreateStatic` on FreeRTOS) - no `pvPortMalloc`, no fragmentation risk. The library makes no heap allocations.

The only post-`proto_begin()` allocation that can occur is inside the Arduino `fs::File` construction in the mount backend `serve_file()` reaches (`test/core_setup/hal/esp/esp_mnt_fs.cpp`), which is an Arduino FS implementation detail outside the library's control.

Every pool above is a fixed BSS array sized from the compile-time constants, so the memory cost is exactly what the configuration says - it never grows at runtime. For the measured flash and static-RAM cost of each optional feature, see the [Build Footprint](#build-footprint) table above.

## Feature Flags & Configuration

> [!IMPORTANT]
> **Use Build Flags (`-D...`), Not Sketch `#define`s!**
>
> Because PlatformIO (and standard Arduino IDE builds) compiles the library's source files (`.cpp`) independently from your sketch (`.ino` / `.cpp`), `#define` macros inside your sketch files **do not propagate** to the library's pre-compiled objects.
>
> Declaring configuration or feature macros like `#define PROTOCORE_ENABLE_PROVISIONING 1` inside your `.ino` sketch file before the `#include` will result in configuration mismatches, linker errors (such as undefined symbols), or unstable behavior at runtime.
>
> To enable/disable features or override configuration constants, you **must** pass them as compiler build flags. For example, in PlatformIO, define them inside `platformio.ini` under `build_flags`:
>
> ```ini
> [env:esp32dev]
> platform = espressif32
> board = esp32dev
> framework = arduino
> build_flags =
>     -DPROTOCORE_ENABLE_PROVISIONING=1
>     -DPROTOCORE_ENABLE_WEBSOCKET=0
>     -DMAX_CONNS=6
> ```

Any feature flag set to `0` strips the corresponding code and its includes from the build entirely.

### Feature Flags

The complete set of `PROTOCORE_ENABLE_*` flags and their defaults, scraped from
`src/protocore_config.h` by `tools/ci_tooling/generate/gen_readme_sections.py` (see
[FEATURES.md](FEATURES.md) for the full description of each):

<details>
<summary><b>All feature flags and their defaults</b></summary>

<!-- BEGIN GENERATED FEATURE-FLAGS (tools/ci_tooling/generate/gen_readme_sections.py) -->

<!-- prettier-ignore-start -->

| Flag | Default | Description |
| :--- | :-----: | :---------- |
| `PROTOCORE_ENABLE_ACCEPT_THROTTLE` | `0` |  |
| `PROTOCORE_ENABLE_AD9238` | `0` |  |
| `PROTOCORE_ENABLE_ADS` | `0` |  |
| `PROTOCORE_ENABLE_ADS1115` | `0` |  |
| `PROTOCORE_ENABLE_AES256CTR` | `1` |  |
| `PROTOCORE_ENABLE_AESGCM` | `1` |  |
| `PROTOCORE_ENABLE_AES_BLOCK` | `1` |  |
| `PROTOCORE_ENABLE_AES_CMAC` | `1` |  |
| `PROTOCORE_ENABLE_AES_SBOX` | `1` |  |
| `PROTOCORE_ENABLE_AMQP` | `0` |  |
| `PROTOCORE_ENABLE_ATC` | `0` |  |
| `PROTOCORE_ENABLE_AUDIT_LOG` | `0` |  |
| `PROTOCORE_ENABLE_AUTH` | `0` |  |
| `PROTOCORE_ENABLE_AUTH_LOCKOUT` | `0` |  |
| `PROTOCORE_ENABLE_BACNET` | `0` |  |
| `PROTOCORE_ENABLE_BASE64` | `1` |  |
| `PROTOCORE_ENABLE_BIGNUM` | `1` |  |
| `PROTOCORE_ENABLE_BLE_GATT` | `0` |  |
| `PROTOCORE_ENABLE_BUS_CAPTURE` | `0` |  |
| `PROTOCORE_ENABLE_C37118` | `0` |  |
| `PROTOCORE_ENABLE_CANOPEN` | `0` |  |
| `PROTOCORE_ENABLE_CBOR` | `0` |  |
| `PROTOCORE_ENABLE_CC1101` | `0` |  |
| `PROTOCORE_ENABLE_CCLINK` | `0` |  |
| `PROTOCORE_ENABLE_CHACHA20` | `1` |  |
| `PROTOCORE_ENABLE_CHACHAPOLY` | `1` |  |
| `PROTOCORE_ENABLE_CIA402` | `0` |  |
| `PROTOCORE_ENABLE_CIP` | `0` |  |
| `PROTOCORE_ENABLE_CLOUDEVENTS` | `0` |  |
| `PROTOCORE_ENABLE_COAP` | `0` |  |
| `PROTOCORE_ENABLE_COAP_BLOCK` | `0` |  |
| `PROTOCORE_ENABLE_COAP_OBSERVE` | `0` |  |
| `PROTOCORE_ENABLE_CONFIG_IO` | `0` |  |
| `PROTOCORE_ENABLE_CONFIG_STORE` | `0` |  |
| `PROTOCORE_ENABLE_CONTROL` | `0` |  |
| `PROTOCORE_ENABLE_COTP` | `0` |  |
| `PROTOCORE_ENABLE_CSRF` | `0` |  |
| `PROTOCORE_ENABLE_CT_EQ` | `1` |  |
| `PROTOCORE_ENABLE_CURVE25519` | `1` |  |
| `PROTOCORE_ENABLE_DASHBOARD` | `0` |  |
| `PROTOCORE_ENABLE_DBM` | `0` |  |
| `PROTOCORE_ENABLE_DDS` | `0` |  |
| `PROTOCORE_ENABLE_DEFLATE_RFC1951` | `0` |  |
| `PROTOCORE_ENABLE_DEVICENET` | `0` |  |
| `PROTOCORE_ENABLE_DEVICE_ID` | `0` |  |
| `PROTOCORE_ENABLE_DF1` | `0` |  |
| `PROTOCORE_ENABLE_DIAG` | `0` |  |
| `PROTOCORE_ENABLE_DIFFSERV` | `0` |  |
| `PROTOCORE_ENABLE_DIRECTNET` | `0` |  |
| `PROTOCORE_ENABLE_DMA` | `0` |  |
| `PROTOCORE_ENABLE_DMX` | `0` |  |
| `PROTOCORE_ENABLE_DNC` | `0` |  |
| `PROTOCORE_ENABLE_DNP3` | `0` |  |
| `PROTOCORE_ENABLE_DNS` | `0` |  |
| `PROTOCORE_ENABLE_DNS_RESOLVER` | `0` |  |
| `PROTOCORE_ENABLE_DNS_SERVER` | `0` |  |
| `PROTOCORE_ENABLE_DOCSTORE` | `0` |  |
| `PROTOCORE_ENABLE_DSHOT` | `0` |  |
| `PROTOCORE_ENABLE_DTLS` | `0` |  |
| `PROTOCORE_ENABLE_ECDSA` | `1` |  |
| `PROTOCORE_ENABLE_ED25519` | `1` |  |
| `PROTOCORE_ENABLE_ENIP` | `0` |  |
| `PROTOCORE_ENABLE_ENOCEAN` | `0` |  |
| `PROTOCORE_ENABLE_ESPNOW` | `0` |  |
| `PROTOCORE_ENABLE_ETAG` | `0` |  |
| `PROTOCORE_ENABLE_ETHERNET` | `0` |  |
| `PROTOCORE_ENABLE_EUROMAP77` | `0` |  |
| `PROTOCORE_ENABLE_EXC_DECODER` | `0` |  |
| `PROTOCORE_ENABLE_FAILSAFE` | `0` |  |
| `PROTOCORE_ENABLE_FANUC_J519` | `0` |  |
| `PROTOCORE_ENABLE_FDC2214` | `0` |  |
| `PROTOCORE_ENABLE_FE25519` | `1` |  |
| `PROTOCORE_ENABLE_FILE_SERVING` | `0` |  |
| `PROTOCORE_ENABLE_FINS` | `0` |  |
| `PROTOCORE_ENABLE_FLOW_EXPORT` | `0` |  |
| `PROTOCORE_ENABLE_FOCAS` | `0` |  |
| `PROTOCORE_ENABLE_FORWARD` | `0` |  |
| `PROTOCORE_ENABLE_FORWARDED_TRUST` | `0` |  |
| `PROTOCORE_ENABLE_FTP` | `0` |  |
| `PROTOCORE_ENABLE_FTP_SESSION` | `0` |  |
| `PROTOCORE_ENABLE_GATEWAY` | `0` |  |
| `PROTOCORE_ENABLE_GHASH` | `1` |  |
| `PROTOCORE_ENABLE_GOOSE` | `0` |  |
| `PROTOCORE_ENABLE_GPIB` | `0` |  |
| `PROTOCORE_ENABLE_GPIO_MAP` | `0` |  |
| `PROTOCORE_ENABLE_GRAPHQL` | `0` |  |
| `PROTOCORE_ENABLE_GRPC_WEB` | `0` |  |
| `PROTOCORE_ENABLE_GUARDRAILS` | `0` |  |
| `PROTOCORE_ENABLE_HAAS_MDC` | `0` |  |
| `PROTOCORE_ENABLE_HAPPY_EYEBALLS` | `0` |  |
| `PROTOCORE_ENABLE_HART` | `0` |  |
| `PROTOCORE_ENABLE_HISLIP` | `0` |  |
| `PROTOCORE_ENABLE_HMAC_SHA256` | `1` |  |
| `PROTOCORE_ENABLE_HMAC_SHA512` | `1` |  |
| `PROTOCORE_ENABLE_HMMD` | `0` |  |
| `PROTOCORE_ENABLE_HOSTLINK` | `0` |  |
| `PROTOCORE_ENABLE_HOTSWAP` | `0` |  |
| `PROTOCORE_ENABLE_HTTP2` | `0` |  |
| `PROTOCORE_ENABLE_HTTP3` | `0` |  |
| `PROTOCORE_ENABLE_HTTP_CACHE` | `0` |  |
| `PROTOCORE_ENABLE_HTTP_CLIENT` | `0` |  |
| `PROTOCORE_ENABLE_HTTP_CLIENT_TLS` | `0` |  |
| `PROTOCORE_ENABLE_HTTP_DELIVERY` | `0` |  |
| `PROTOCORE_ENABLE_HTTP_PARSER` | `1` |  |
| `PROTOCORE_ENABLE_HTTP_ROUTE` | `0` |  |
| `PROTOCORE_ENABLE_HW_HEALTH` | `0` |  |
| `PROTOCORE_ENABLE_ICCP` | `0` |  |
| `PROTOCORE_ENABLE_IEC60870` | `0` |  |
| `PROTOCORE_ENABLE_IFACE_BRIDGE` | `0` |  |
| `PROTOCORE_ENABLE_IKEV2` | `0` |  |
| `PROTOCORE_ENABLE_INA219` | `0` |  |
| `PROTOCORE_ENABLE_INTERBUS` | `0` |  |
| `PROTOCORE_ENABLE_IOLINK` | `0` |  |
| `PROTOCORE_ENABLE_IPV6` | `0` |  |
| `PROTOCORE_ENABLE_IP_ALLOWLIST` | `0` |  |
| `PROTOCORE_ENABLE_J1939` | `0` |  |
| `PROTOCORE_ENABLE_J2735` | `0` |  |
| `PROTOCORE_ENABLE_JSON` | `0` |  |
| `PROTOCORE_ENABLE_JWT` | `0` |  |
| `PROTOCORE_ENABLE_KDF` | `1` |  |
| `PROTOCORE_ENABLE_KEEPALIVE` | `0` |  |
| `PROTOCORE_ENABLE_LD2410` | `0` |  |
| `PROTOCORE_ENABLE_LDC1614` | `0` |  |
| `PROTOCORE_ENABLE_LINK_MANAGER` | `0` |  |
| `PROTOCORE_ENABLE_LOGBUF` | `0` |  |
| `PROTOCORE_ENABLE_LONWORKS` | `0` |  |
| `PROTOCORE_ENABLE_LORA` | `0` |  |
| `PROTOCORE_ENABLE_LSV2` | `0` |  |
| `PROTOCORE_ENABLE_LWM2M` | `0` |  |
| `PROTOCORE_ENABLE_MBPLUS` | `0` |  |
| `PROTOCORE_ENABLE_MBUS` | `0` |  |
| `PROTOCORE_ENABLE_MD` | `1` |  |
| `PROTOCORE_ENABLE_MDNS` | `0` |  |
| `PROTOCORE_ENABLE_MDNS_ADAPTIVE` | `0` |  |
| `PROTOCORE_ENABLE_MELSEC` | `0` |  |
| `PROTOCORE_ENABLE_METRICS` | `0` |  |
| `PROTOCORE_ENABLE_MMS` | `0` |  |
| `PROTOCORE_ENABLE_MNT` | `0` |  |
| `PROTOCORE_ENABLE_MODBUS` | `0` |  |
| `PROTOCORE_ENABLE_MODBUS_MASTER` | `0` |  |
| `PROTOCORE_ENABLE_MPR121` | `0` |  |
| `PROTOCORE_ENABLE_MQTT` | `0` |  |
| `PROTOCORE_ENABLE_MQTT_SN` | `0` |  |
| `PROTOCORE_ENABLE_MQTT_TLS` | `0` |  |
| `PROTOCORE_ENABLE_MSGPACK` | `0` |  |
| `PROTOCORE_ENABLE_MTCONNECT` | `0` |  |
| `PROTOCORE_ENABLE_MTLS` | `0` |  |
| `PROTOCORE_ENABLE_MULTIPART` | `0` |  |
| `PROTOCORE_ENABLE_NATS` | `0` |  |
| `PROTOCORE_ENABLE_NEMA_TS2` | `0` |  |
| `PROTOCORE_ENABLE_NETADAPT` | `0` |  |
| `PROTOCORE_ENABLE_NMEA0183` | `0` |  |
| `PROTOCORE_ENABLE_NRF24` | `0` |  |
| `PROTOCORE_ENABLE_NTCIP` | `0` |  |
| `PROTOCORE_ENABLE_NTP` | `0` |  |
| `PROTOCORE_ENABLE_NTP_SERVER` | `0` |  |
| `PROTOCORE_ENABLE_NTRIP_CASTER` | `0` |  |
| `PROTOCORE_ENABLE_NTS` | `0` |  |
| `PROTOCORE_ENABLE_OAUTH2` | `0` |  |
| `PROTOCORE_ENABLE_OBSERVABILITY` | `0` |  |
| `PROTOCORE_ENABLE_OCIT` | `0` |  |
| `PROTOCORE_ENABLE_OIDC` | `0` |  |
| `PROTOCORE_ENABLE_OPCUA` | `0` |  |
| `PROTOCORE_ENABLE_OPCUA_CLIENT` | `0` |  |
| `PROTOCORE_ENABLE_OPENADR` | `0` |  |
| `PROTOCORE_ENABLE_OTA` | `0` |  |
| `PROTOCORE_ENABLE_OTA_ROLLBACK` | `0` |  |
| `PROTOCORE_ENABLE_PACKML` | `0` |  |
| `PROTOCORE_ENABLE_PARTITION_MONITOR` | `0` |  |
| `PROTOCORE_ENABLE_PCA9685` | `0` |  |
| `PROTOCORE_ENABLE_PER_IP_THROTTLE` | `0` |  |
| `PROTOCORE_ENABLE_PMBUS` | `0` |  |
| `PROTOCORE_ENABLE_PN532` | `0` |  |
| `PROTOCORE_ENABLE_POLY1305` | `1` |  |
| `PROTOCORE_ENABLE_POWERLINK` | `0` |  |
| `PROTOCORE_ENABLE_POWER_MGMT` | `0` |  |
| `PROTOCORE_ENABLE_PQC_KEX` | `0` |  |
| `PROTOCORE_ENABLE_PREEMPT_QUEUE` | `0` |  |
| `PROTOCORE_ENABLE_PROFIBUS` | `0` |  |
| `PROTOCORE_ENABLE_PROFINET` | `0` |  |
| `PROTOCORE_ENABLE_PROMISC` | `0` |  |
| `PROTOCORE_ENABLE_PROTOBUF` | `0` |  |
| `PROTOCORE_ENABLE_PROVISIONING` | `0` |  |
| `PROTOCORE_ENABLE_PROXY_PROTOCOL` | `0` |  |
| `PROTOCORE_ENABLE_PSRAM_POOL` | `0` |  |
| `PROTOCORE_ENABLE_PTP` | `0` |  |
| `PROTOCORE_ENABLE_RADIO_POWER` | `0` |  |
| `PROTOCORE_ENABLE_RADIO_SNIFF` | `0` |  |
| `PROTOCORE_ENABLE_RANGE` | `0` |  |
| `PROTOCORE_ENABLE_RAWL2` | `0` |  |
| `PROTOCORE_ENABLE_RAWMEMCPY` | `1` |  |
| `PROTOCORE_ENABLE_RCWL0516` | `0` |  |
| `PROTOCORE_ENABLE_REDIS` | `0` |  |
| `PROTOCORE_ENABLE_RELAY` | `0` |  |
| `PROTOCORE_ENABLE_RNG` | `1` |  |
| `PROTOCORE_ENABLE_ROAMING` | `0` |  |
| `PROTOCORE_ENABLE_ROBOTICS` | `0` |  |
| `PROTOCORE_ENABLE_RSA` | `1` |  |
| `PROTOCORE_ENABLE_RTC` | `0` |  |
| `PROTOCORE_ENABLE_S7COMM` | `0` |  |
| `PROTOCORE_ENABLE_SAFETY_SCL` | `0` |  |
| `PROTOCORE_ENABLE_SCPI` | `0` |  |
| `PROTOCORE_ENABLE_SDI12` | `0` |  |
| `PROTOCORE_ENABLE_SEN0192` | `0` |  |
| `PROTOCORE_ENABLE_SEP2` | `0` |  |
| `PROTOCORE_ENABLE_SERCOS` | `0` |  |
| `PROTOCORE_ENABLE_SHA1` | `1` |  |
| `PROTOCORE_ENABLE_SHA256` | `1` |  |
| `PROTOCORE_ENABLE_SHA512` | `1` |  |
| `PROTOCORE_ENABLE_SHT3X` | `0` |  |
| `PROTOCORE_ENABLE_SIGFOX` | `0` |  |
| `PROTOCORE_ENABLE_SIMATIC` | `0` |  |
| `PROTOCORE_ENABLE_SLEEP_SCHED` | `0` |  |
| `PROTOCORE_ENABLE_SMB` | `0` |  |
| `PROTOCORE_ENABLE_SMBUS` | `0` |  |
| `PROTOCORE_ENABLE_SMTP` | `0` |  |
| `PROTOCORE_ENABLE_SMTP_TLS` | `0` |  |
| `PROTOCORE_ENABLE_SNMP` | `0` |  |
| `PROTOCORE_ENABLE_SNMP_TRAP` | `0` |  |
| `PROTOCORE_ENABLE_SNMP_V3` | `0` |  |
| `PROTOCORE_ENABLE_SNP` | `0` |  |
| `PROTOCORE_ENABLE_SOCKPOOL` | `0` |  |
| `PROTOCORE_ENABLE_SOUTHBOUND` | `0` |  |
| `PROTOCORE_ENABLE_SPA_ROUTER` | `0` |  |
| `PROTOCORE_ENABLE_SQLITE` | `0` |  |
| `PROTOCORE_ENABLE_SSE` | `0` |  |
| `PROTOCORE_ENABLE_SSH` | `0` |  |
| `PROTOCORE_ENABLE_SSH_CLIENT` | `0` |  |
| `PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE` | `0` |  |
| `PROTOCORE_ENABLE_SSH_RSA` | `1` |  |
| `PROTOCORE_ENABLE_SSH_SCP` | `0` |  |
| `PROTOCORE_ENABLE_SSH_SFTP` | `0` |  |
| `PROTOCORE_ENABLE_SSH_ZLIB` | `0` |  |
| `PROTOCORE_ENABLE_STATS` | `0` |  |
| `PROTOCORE_ENABLE_STATSD` | `0` |  |
| `PROTOCORE_ENABLE_STOMP` | `0` |  |
| `PROTOCORE_ENABLE_SUNSPEC` | `0` |  |
| `PROTOCORE_ENABLE_SYSLOG` | `0` |  |
| `PROTOCORE_ENABLE_TCP` | `0` |  |
| `PROTOCORE_ENABLE_TCP_CLIENT` | `0` |  |
| `PROTOCORE_ENABLE_TELEMETRY` | `0` |  |
| `PROTOCORE_ENABLE_TELNET` | `0` |  |
| `PROTOCORE_ENABLE_THEMES` | `0` |  |
| `PROTOCORE_ENABLE_THREAD` | `0` |  |
| `PROTOCORE_ENABLE_TIME_SOURCE` | `0` |  |
| `PROTOCORE_ENABLE_TLS` | `0` |  |
| `PROTOCORE_ENABLE_TLS_POLICY` | `0` |  |
| `PROTOCORE_ENABLE_TLS_RESUMPTION` | `0` |  |
| `PROTOCORE_ENABLE_TLS_RPK` | `0` |  |
| `PROTOCORE_ENABLE_TOTP` | `0` |  |
| `PROTOCORE_ENABLE_TRACE_CAPTURE` | `0` |  |
| `PROTOCORE_ENABLE_UBX` | `0` |  |
| `PROTOCORE_ENABLE_UDP` | `0` |  |
| `PROTOCORE_ENABLE_UDP_TELEMETRY` | `0` |  |
| `PROTOCORE_ENABLE_UMATI` | `0` |  |
| `PROTOCORE_ENABLE_UPLOAD` | `0` |  |
| `PROTOCORE_ENABLE_UTMC` | `0` |  |
| `PROTOCORE_ENABLE_VL53L0X` | `0` |  |
| `PROTOCORE_ENABLE_VXI11` | `0` |  |
| `PROTOCORE_ENABLE_WAL` | `0` |  |
| `PROTOCORE_ENABLE_WAMP` | `0` |  |
| `PROTOCORE_ENABLE_WAVE` | `0` |  |
| `PROTOCORE_ENABLE_WEARLEVEL` | `0` |  |
| `PROTOCORE_ENABLE_WEBDAV` | `0` |  |
| `PROTOCORE_ENABLE_WEBHOOK` | `0` |  |
| `PROTOCORE_ENABLE_WEBSOCKET` | `0` |  |
| `PROTOCORE_ENABLE_WEB_ASSETS` | `0` |  |
| `PROTOCORE_ENABLE_WEB_TERMINAL` | `0` |  |
| `PROTOCORE_ENABLE_WIFI_SNIFFER` | `0` |  |
| `PROTOCORE_ENABLE_WISUN` | `0` |  |
| `PROTOCORE_ENABLE_WS_CLIENT` | `0` |  |
| `PROTOCORE_ENABLE_WS_CLIENT_TLS` | `0` |  |
| `PROTOCORE_ENABLE_WS_DEFLATE` | `0` |  |
| `PROTOCORE_ENABLE_X509` | `0` |  |
| `PROTOCORE_ENABLE_XMPP` | `0` |  |
| `PROTOCORE_ENABLE_ZIGBEE` | `0` |  |
| `PROTOCORE_ENABLE_ZWAVE` | `0` |  |

<!-- prettier-ignore-end -->

<!-- END GENERATED FEATURE-FLAGS -->

</details>

Illegal combinations (e.g. `MAX_WS_CONNS + MAX_SSE_CONNS > MAX_CONNS`) produce `#error` messages at compile time with a descriptive reason string.

## Configuration Overrides

All constants can be overridden using compiler build flags (e.g. `-DMAX_CONNS=6`). Default limits and sizes reside in [protocore_config.h](@ref protocore_config.h).

<details>
<summary><b>Expand Configuration constants and options</b></summary>

The full list of tunable `#define` constants and their defaults, scraped from
`src/protocore_config.h` by `tools/ci_tooling/generate/gen_readme_sections.py`. Override any
with a build flag (e.g. `-DMAX_CONNS=6`); illegal combinations are caught by `#error`
guards at compile time.

<!-- BEGIN GENERATED CONFIG-OVERRIDES (tools/ci_tooling/generate/gen_readme_sections.py) -->

<!-- prettier-ignore-start -->

| Constant | Default | Description |
| :------- | :-----: | :---------- |

<!-- prettier-ignore-end -->

<!-- END GENERATED CONFIG-OVERRIDES -->

**Runtime Config**

The connection idle timeout can be changed without a rebuild:

```c
const WebServerConfig cfg PROGMEM = { .conn_timeout_ms = 10000 }; // flash, no RAM cost
begin_http(80, &cfg);
```

Pass `NULL` to use the compile-time default [`CONN_TIMEOUT_MS`](@ref CONN_TIMEOUT_MS) (5000 ms).

</details>

## API Reference

<details>
<summary><b>Expand API Reference</b></summary>

**Lifecycle**

| Function                | Description                                                                                                          |
| ----------------------- | -------------------------------------------------------------------------------------------------------------------- |
| `proto_begin(cfg)`      | Open every port registered with `listen()`. Returns `PROTOCORE_OK` (1) on success, a negative error code on failure. |
| `begin_http(port, cfg)` | `listen(port)` + `proto_begin(cfg)`, for the single-port case.                                                       |
| `stop()`                | Abort all connections, close listener, reset all pools.                                                              |
| `restart(cfg)`          | `stop()` + `proto_begin()` on the same ports. Returns `-1` if called before any `listen()`.                          |
| `handle()`              | Call every `loop()`. Runs timeout sweep, event drain, and dispatch.                                                  |

**HTTP Routes**

| Function                                                         | Description                                                                                |
| ---------------------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| `on_http(path, method, handler)`                                 | Register a route. Trailing `*` enables prefix matching.                                    |
| `on_http_auth(path, method, handler, realm, user, pass, digest)` | Same, behind Basic or Digest auth (`PROTOCORE_ENABLE_AUTH`).                               |
| `on_not_found(handler)`                                          | Fallback handler; default sends 404.                                                       |
| `set_cors(origin)`                                               | Enable CORS and answer OPTIONS with 204. Pass `""` to disable.                             |
| `send_text(slot_id, code, content_type, payload)`                | Send a response with body and close the connection.                                        |
| `send_empty(slot_id, code)`                                      | Send a headers-only response and close the connection.                                     |
| `serve_file(slot_id, file_sys, fs_path, content_type)`           | Stream a file from a mount backend, in `file_serving.h` (`PROTOCORE_ENABLE_FILE_SERVING`). |

**WebSocket (PROTOCORE_ENABLE_WEBSOCKET)**

| Function                                        | Description                                 |
| ----------------------------------------------- | ------------------------------------------- |
| `on_ws(path, on_connect, on_message, on_close)` | Register a WebSocket route.                 |
| `ws_send_text(ws_id, text)`                     | Send a UTF-8 text frame to a client.        |
| `ws_send_binary(ws_id, data, len)`              | Send a binary frame to a client.            |
| `ws_disconnect(ws_id)`                          | Send Close frame and mark slot for cleanup. |

In `on_message`, read the received payload from `ws_pool[ws_id].buf` (length in `ws_pool[ws_id].payload_len`).

**SSE (PROTOCORE_ENABLE_SSE)**

| Function                                         | Description                                                  |
| ------------------------------------------------ | ------------------------------------------------------------ |
| `on_sse(path, on_connect)`                       | Register an SSE route.                                       |
| `protocore_sse_send(sse_id, data, event, id)`    | Push an event to one client. `event` and `id` may be `NULL`. |
| `protocore_sse_broadcast(path, data, event, id)` | Push an event to all clients on a path.                      |

**Diagnostic (PROTOCORE_ENABLE_DIAG)**

| Function        | Description                                                                                          |
| --------------- | ---------------------------------------------------------------------------------------------------- |
| `diag(slot_id)` | Send a JSON object with all active feature flags and configuration constants. Disable in production. |

**Handler Signatures**

```c
// HTTP
void handler(uint8_t slot_id, HttpReq *req);

// WebSocket (PROTOCORE_ENABLE_WEBSOCKET)
void ws_connect(uint8_t ws_id);
void ws_message(uint8_t ws_id);  // payload in ws_pool[ws_id].buf
void ws_close(uint8_t ws_id);

// SSE (PROTOCORE_ENABLE_SSE)
void sse_connect(uint8_t sse_id);
```

**HttpReq Fields**

| Field            | Type                              | Description                                                                                  |
| ---------------- | --------------------------------- | -------------------------------------------------------------------------------------------- |
| `method`         | `char[PROTOCORE_METHOD_BUF_SIZE]` | HTTP method string, e.g. `"GET"`                                                             |
| `path`           | `char[MAX_PATH_LEN]`              | URL path, e.g. `"/api/status"`                                                               |
| `version`        | [`HttpVersion`](@ref HttpVersion) | [`HTTP_10`](@ref HTTP_10), [`HTTP_11`](@ref HTTP_11), or [`HTTP_UNKNOWN`](@ref HTTP_UNKNOWN) |
| `query`          | `char[MAX_QUERY_LEN]`             | Raw query string (everything after `?`)                                                      |
| `query_params`   | `QueryParam[MAX_QUERY_PARAMS]`    | Parsed key=value pairs                                                                       |
| `query_count`    | `uint8_t`                         | Valid entries in `query_params[]`                                                            |
| `headers`        | `Header[MAX_HEADERS]`             | Captured header fields                                                                       |
| `header_count`   | `uint8_t`                         | Valid entries in `headers[]`                                                                 |
| `content_length` | `size_t`                          | Value of `Content-Length` header (0 if absent)                                               |
| `body`           | `uint8_t[BODY_BUF_SIZE+1]`        | Request body, always null-terminated                                                         |
| `body_len`       | `size_t`                          | Bytes stored in `body[]`                                                                     |

**Helper Functions**

```c
const char *http_get_header(const HttpReq *req, const char *key); // case-insensitive
const char *http_get_query (const HttpReq *req, const char *key); // case-sensitive
```

</details>

## RFC Compliance

The core HTTP/1.1 parser enforces RFC 7230 byte-by-byte; the dispatcher returns the
correct status codes (400/404/405/413/414/426/501) with `Allow` / `Sec-WebSocket-Version`
headers where required; the WebSocket layer enforces RFC 6455 framing. HTTP/2 (RFC 9113 +
HPACK RFC 7541) and the HTTP/3 stack (RFC 9114 over QUIC, RFC 9000) follow
their own specs, and every optional protocol is implemented against its authoritative
standard.

See **[RFC.md](RFC.md)** for the HTTP / WebSocket / error-response conformance tables and
**[STANDARDS.md](STANDARDS.md)** for the complete per-protocol standards map.

## SSH Support

ProtoCore includes a **complete SSH-2.0 server** -
banner exchange → `KEXINIT` negotiation → key exchange → `NEWKEYS` → user
authentication (**publickey** and password) → `ssh-connection` session channel,
with per-direction NEWKEYS and transparent in-session re-keys. Key exchange offers
Curve25519 ECDH (`curve25519-sha256`) and `diffie-hellman-group14-sha256`; host keys
are Ed25519 (`ssh-ed25519`) and RSA (`rsa-sha2-256` / `ssh-rsa`). All state is static
(BSS), the host private key never touches static scratch memory, and password auth can
be compiled out (`PROTOCORE_SSH_ALLOW_PASSWORD=0`) for publickey-only hardening.

See **[SSH.md](SSH.md)** for the feature summary, RFC/FIPS compliance
table, authentication/hardening details, and memory footprint, and
**[SECURITY.md](SECURITY.md)** for the security treatment.

## Utility Tools

Python tooling for generating documentation and building the embedded web assets. The
documentation generators run in CI (the Feature Tables workflow) so their output never
drifts; run any of them locally from the repo root.

<details>
<summary><b>Expand Utility Tools and Scripts Guide</b></summary>

**Documentation generators** (`tools/ci_tooling/generate/`)

| Script                   | Generates                                                                            |
| ------------------------ | ------------------------------------------------------------------------------------ |
| `gen_feature_tables.py`  | the README / docs feature tables from `FEATURES.md`                                  |
| `gen_readme_sections.py` | this file's feature-flag, configuration-override, source-tree, and footprint regions |
| `gen_configurator.py`    | the interactive `configurator.html` from `protocore_config.h`                            |
| `gen_flag_deps.py`       | the build-flag dependency diagram                                                    |
| `gen_api_flow.py`        | the core API-flow diagram                                                            |
| `gen_examples.py`        | the example index in `EXAMPLES.md`                                                   |
| `decorate_changelog.py`  | wraps each release in `CHANGELOG.md` in a collapsible block (CI)                     |

The suite's own generator is a subcommand of the test entry point: `python test/harness.py readme gen` refreshes the env matrix + per-test directory in [`test/README.md`](../test/README.md).

**Web-asset build** (`src/web_assets/wizard/`)

| Script               | Purpose                                                                                       |
| -------------------- | ----------------------------------------------------------------------------------------------- |
| `build_assets.py`    | compile the editable web sources (`src/web_assets/input/*`) into embedded C application assets |
| `gen_themes.py`      | build the theme CSS library + gallery from the palette sources                                |
| `gen_theme_blobs.py` | pack the runtime-selectable theme CSS into C blobs                                            |
| `gen_favicons.py`    | build the favicon library + gallery                                                           |

```bash
python -m tools.ci_tooling.generate.gen_readme_sections   # refresh this file's generated sections
python -m src.web_assets.wizard.build_assets              # rebuild the embedded web assets
```

</details>

## Testing

**2,300+ Unity tests** across the native suites, all runnable on a native x86/x64 host
(no hardware required). See **[TEST_REPORT.md](../test/TEST_REPORT.md)** for the current
per-suite breakdown and totals. Run a representative subset with:

```
python test/harness.py run native_ssh native_ssh_conn native_compliance
```

Every test activity starts at `test/harness.py`; `python test/harness.py help` is its whole surface.

See the **[test suite README](../test/README.md)** for the suite
breakdown, environment matrix, and per-test directory, and
**[TEST_REPORT.md](../test/TEST_REPORT.md)** for the latest results
(auto-generated by the _Test Report_ GitHub workflow).

## Documentation

Other documentation files in this repository:

<details>
<summary><b>View Documentation Reference Directory</b></summary>

**The house law** - the rules every `src/` file obeys. Each states the no-heap guarantee for its
own reason; they do not overlap by accident.

| Document                     | Contents                                                                  |
| ---------------------------- | ------------------------------------------------------------------------- |
| [SRC_LAW.md](SRC_LAW.md)     | The **why**: determinism and allocation law, derived from MISRA / AUTOSAR  |
| [tools/ci_tooling/README.md](../tools/ci_tooling/README.md) | The **tooling** law: how generators and checkers must be written, and why. |
| [SRCBANNED.md](SRCBANNED.md) | The **what**: constructs banned in `src/`, enforced by a checker           |
| [SYMBOLS.md](SYMBOLS.md)     | The **naming** law: prefixes, macros, enums, include guards                |

**Design and conformance:**

| Document                                         | Contents                                                       |
| ------------------------------------------------ | -------------------------------------------------------------- |
| [ARCHITECTURE.md](ARCHITECTURE.md)               | Layering, data piping, ownership, and the multi-vendor design   |
| [STANDARDS.md](STANDARDS.md)                     | Per-protocol map to the authoritative specification             |
| [AUDIT.md](AUDIT.md)                             | Per-standard conformance verdicts and the evidence behind them  |
| [INTEROP_MATRIX.md](INTEROP_MATRIX.md)           | Which features are judged by a third party, and by what         |
| [FEATURE_PERFORMANCE.md](FEATURE_PERFORMANCE.md) | Per-feature benchmarks, plus conjecture on open work            |
| [RFC.md](RFC.md)                                 | HTTP/1.1, WebSocket, and error-response RFC conformance tables  |
| [SSH.md](SSH.md)                                 | SSH-2.0 server: features, RFC/FIPS compliance, auth, memory     |
| [SECURITY.md](SECURITY.md)                       | Security posture (good/ok/bad) and per-feature treatment        |
| [CODEQL.md](CODEQL.md)                           | CodeQL static-analysis setup, coverage, findings disposition    |
| [SONARQUBE.md](SONARQUBE.md)                     | SonarCloud analysis: why it is CI-based, and the compile DB     |
| [HARDWARE_HOOKUP.md](HARDWARE_HOOKUP.md)         | Wiring and settings for codecs that talk to external hardware   |
| [reference/README.md](reference/README.md)       | SoC datasheets backing the per-variant board profiles           |

**Generated galleries** - regenerated from the tree, never hand-edited:

| Document                       | Contents                                                        |
| ------------------------------ | --------------------------------------------------------------- |
| [THEMES.md](THEMES.md)         | 112 servable page themes, with previews and the custom recipe   |
| [FAVICONS.md](FAVICONS.md)     | 288 favicons, 18 motifs x 16 palettes, as crisp SVG             |
| [FOOTPRINTS.md](FOOTPRINTS.md) | Per-example build footprints (flash and static RAM)             |

**Plan, history, and operations:**

| Document                                     | Contents                                                        |
| -------------------------------------------- | --------------------------------------------------------------- |
| [ROADMAP.md](ROADMAP.md)                     | Unfinished work only: Now / Next / Later / Partial              |
| [DELIVERED.md](DELIVERED.md)                 | Work that was undertaken and finished                           |
| [TODO.md](TODO.md)                           | Outstanding fixes and maintenance                               |
| [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) | Deliberate constraints and caveats                              |
| [TUNING.md](TUNING.md)                       | Performance tuning: worker count, core/affinity, poll knobs     |
| [test/README.md](../test/README.md)          | Test suites, environment matrix, per-test directory, how to run |
| [TEST_REPORT.md](../test/TEST_REPORT.md)     | Latest test results (auto-generated)                            |
| [CHANGELOG.md](CHANGELOG.md)                 | Release history                                                 |

</details>

### Generating Docs Locally

To generate the HTML API documentation locally, run the following command from the repository root:

```bash
doxygen docs/Doxyfile
```

The output will be generated in `docs/html/index.html`.

If you are viewing the offline version of this documentation, you can access the latest online version at the [GitHub Pages documentation site](https://dstroy0.github.io/ProtoCore/).

## Licensing & Commercial Use

This library is dual-licensed.

**Open Source.** This library is, and will **ALWAYS REMAIN, FULLY OPEN-SOURCE** under the AGPLv3 (or later). We commit to maintaining a fully featured, parity-matched open-source version available to everyone - from hobbyists and educators to professionals - without hiding any non-proprietary (e.g. custom protocols, intellectual property, confidential telemetry configurations, etc.) feature behind a commercial paywall. It will always be free to use under the AGPLv3 (or later) in any environment that complies with the AGPLv3 (or later) terms. See the `LICENSE` file.

**Commercial.** For teams and applications that cannot meet the AGPLv3 copyleft requirements, a commercial license is available. Contact: Douglas Quigg (dstroy0), dquigg123@gmail.com

**Educators.** Teaching with this? We'd love that. **SERIOUSLY**. Squirty is meant to keep children engaged on the docs page. The docs and styling are set up to appeal to them, hobbyists, and anyone who wants to learn but doesn't know how to style things or glue services together. The library documentation is extensive, extremely thorough, and useful to professionals as well as educators as a teaching tool/classroom prop. If sharing your source under the AGPLv3 isn't practical for a classroom or lab, or you have concerns that have stopped you from using copyleft licensed software before, email Douglas Quigg (dstroy0) at dquigg123@gmail.com from your school address and we'll see what we can do. ESP32 boards are cheap and a hands-on HTTP / IoT-edge stack is a great way into embedded networking, so we're glad to look at education-focused requests one by one. We can't promise an exception for every situation, but please ask. (This is just for genuine educational use; for products, see the commercial option above.) I can help you set up a github repo your students can push to that will help you review their submissions, and walk you through setting up flags for your rubric items. We really need to make an effort to get as many people as possible into the profession, looking at how things work, figuring out how they work on a deeper level, and entering the profession, we need their ideas, we need them now. All great discoveries have come from fresh perspective.

---

<p align="center">
  <img src="squirty.svg" alt="Squirty the Injection Squid" width="64" height="64"><br>
  <b>Squirty the Injection Squid</b>: the official library mascot.<br>
  <sub>Copyright &copy; Douglas Quigg (dstroy0). All rights reserved.</sub>
</p>
