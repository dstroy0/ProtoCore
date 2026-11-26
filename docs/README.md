# Documentation

A vendor agnostic, multi-protocol network server with a fully deterministic memory footprint, RFC 7230 compliant request parsing, and an OSI-layered architecture. It serves HTTP/1.1 and HTTP/2 (with HTTP/3 over QUIC, host-tested), WebSocket, and Server-Sent Events, with optional HTTPS/TLS, SSH, Telnet, SNMP, CoAP, Modbus TCP, MQTT, and OPC UA.

## Features

A compile-time menu grouped by the OSI layer each feature lives at, alphabetized within each layer: each cell is an optional `PROTOCORE_ENABLE_*` subsystem (core HTTP/1.1, routing, middleware, JSON, templating, and chunked responses are always on). **Hover an entry for its summary; click through to [FEATURES.md](FEATURES.md) for the full description.** The tables are generated from [FEATURES.md](FEATURES.md) by `tools/ci_tooling/generate/gen_feature_tables.py`, so they never drift.

<!-- BEGIN GENERATED FEATURE TABLES (tools/ci_tooling/generate/gen_feature_tables.py) -->

<!-- prettier-ignore-start -->

**261 features**, every one a compile-time `PROTOCORE_ENABLE_*` flag that is off unless you ask for it. Core HTTP/1.1 parsing, routing, middleware, JSON, templating and chunked responses are always on and are not flags.

<a href="https://dstroy0.github.io/ProtoCore/features.html" title="Browse every feature">
  <img alt="Feature map: the OSI stack and the feature groups on each layer" src="diagrams/features_map.svg" width="100%">
</a>

| Layer | Features | For example |
| --- | --- | --- |
| **Foundation** | 17 | Config IO, Config Store, Device ID, DMA Peripheral Ingest, … |
| **Physical & Data Link (L1-L2)** | 31 | ADS1115, BLE GATT, Bus Capture, CC1101, … |
| **Network (L3)** | 7 | Dns Resolver, DNS Wire Codec, Happy Eyeballs, IPv6, … |
| **Transport (L4)** | 8 | Accept Throttle, IP Allowlist, Keep-Alive, Per IP Throttle, … |
| **Session (L5)** | 5 | SSH, SSH Compression, SSH SCP, SSH SFTP, … |
| **Presentation (L6)** | 21 | Auth, Auth Lockout, CBOR, CloudEvents, … |
| **Application (L7)** | 172 | AD9238, Adaptive mDNS, ADS (Beckhoff), AMQP, … |

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

The conceptual layer map above is a summary. The complete file layout is generated
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
│       ├── ns_contract.h
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
│   │   ├── ed25519_comb_table/
│   │   │   ├── CMakeLists.txt
│   │   │   └── ed25519_comb_table.h
│   │   ├── fe25519/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── fe25519.c
│   │   │   └── fe25519.h
│   │   ├── rsa/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── rsa.c
│   │   │   └── rsa.h
│   │   └── CMakeLists.txt
│   ├── cipher/
│   │   ├── aes256ctr/
│   │   │   ├── aes256ctr.c
│   │   │   ├── aes256ctr.h
│   │   │   └── CMakeLists.txt
│   │   ├── aes_block/
│   │   │   ├── aes_block.c
│   │   │   ├── aes_block.h
│   │   │   └── CMakeLists.txt
│   │   ├── aes_sbox/
│   │   │   ├── aes_sbox.h
│   │   │   └── CMakeLists.txt
│   │   ├── chacha20/
│   │   │   ├── chacha20.c
│   │   │   ├── chacha20.h
│   │   │   └── CMakeLists.txt
│   │   └── CMakeLists.txt
│   ├── ct_eq/
│   │   ├── CMakeLists.txt
│   │   ├── ct_eq.c
│   │   └── ct_eq.h
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
│   │   ├── x509_types/
│   │   │   ├── CMakeLists.txt
│   │   │   └── x509_types.h
│   │   ├── x509_verify/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── x509_verify.c
│   │   │   └── x509_verify.h
│   │   └── CMakeLists.txt
│   └── CMakeLists.txt
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
│   │   ├── stack/
│   │   │   ├── stack_net/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── stack_net.c
│   │   │   │   └── stack_net.h
│   │   │   ├── stack_tcp/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── stack_tcp.c
│   │   │   │   └── stack_tcp.h
│   │   │   ├── CMakeLists.txt
│   │   │   ├── stack.c
│   │   │   └── stack.h
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
│   │   ├── wifi_link/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── wifi_link.c
│   │   │   └── wifi_link.h
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
│   │   │   │   ├── http_route/
│   │   │   │   │   ├── CMakeLists.txt
│   │   │   │   │   ├── http_route.c
│   │   │   │   │   └── http_route.h
│   │   │   │   └── CMakeLists.txt
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
│   │   │   ├── common/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   └── common.h
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
│   │   │   ├── common/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   └── common.h
│   │   │   ├── evt/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   └── evt.h
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
│   │   │   ├── tcp.c
│   │   │   └── tcp.h
│   │   ├── udp/
│   │   │   ├── client/
│   │   │   │   ├── client.c
│   │   │   │   ├── client.h
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── common/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   └── common.h
│   │   │   ├── server/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── server.c
│   │   │   │   └── server.h
│   │   │   ├── CMakeLists.txt
│   │   │   ├── udp.c
│   │   │   └── udp.h
│   │   └── CMakeLists.txt
│   └── CMakeLists.txt
├── protocore/
│   ├── CMakeLists.txt
│   └── protocore.c
├── server/
│   ├── clock/
│   │   ├── clock.c
│   │   ├── clock.h
│   │   └── CMakeLists.txt
│   ├── core/
│   │   ├── exc_coredump/
│   │   │   ├── CMakeLists.txt
│   │   │   └── exc_coredump.c
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
│   │   ├── proto_handler/
│   │   │   ├── CMakeLists.txt
│   │   │   └── proto_handler.h
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
│   │   └── CMakeLists.txt
│   ├── io/
│   │   ├── http_clock/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── http_clock.c
│   │   │   └── http_clock.h
│   │   ├── middleware/
│   │   │   ├── CMakeLists.txt
│   │   │   └── middleware.c
│   │   ├── response/
│   │   │   ├── CMakeLists.txt
│   │   │   └── response.c
│   │   ├── webdav_handler/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── webdav_handler.c
│   │   │   └── webdav_handler.h
│   │   ├── websocket_sse/
│   │   │   ├── CMakeLists.txt
│   │   │   └── websocket_sse.c
│   │   └── CMakeLists.txt
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
│   │   ├── i2c/
│   │   │   ├── CMakeLists.txt
│   │   │   └── i2c.h
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
│   │   ├── spi/
│   │   │   ├── CMakeLists.txt
│   │   │   └── spi.h
│   │   ├── uart/
│   │   │   ├── CMakeLists.txt
│   │   │   └── uart.h
│   │   ├── vl53l0x/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── vl53l0x.c
│   │   │   └── vl53l0x.h
│   │   └── CMakeLists.txt
│   ├── protocore_builtins/
│   │   ├── CMakeLists.txt
│   │   └── protocore_builtins.c
│   ├── regex/
│   │   ├── CMakeLists.txt
│   │   └── regex.c
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
│   │   ├── gpio_map_routes/
│   │   │   ├── CMakeLists.txt
│   │   │   └── gpio_map_routes.c
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
│   │   └── CMakeLists.txt
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
│   │   │   ├── partition_monitor_routes/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   └── partition_monitor_routes.c
│   │   │   ├── CMakeLists.txt
│   │   │   ├── partition_monitor.c
│   │   │   └── partition_monitor.h
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
│   │   │   ├── dashboard_routes/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   └── dashboard_routes.c
│   │   │   ├── CMakeLists.txt
│   │   │   ├── dashboard.c
│   │   │   └── dashboard.h
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
│   └── CMakeLists.txt
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
│   │   │   ├── http_delivery_routes/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   └── http_delivery_routes.c
│   │   │   ├── CMakeLists.txt
│   │   │   ├── http_delivery.c
│   │   │   └── http_delivery.h
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
│   │   │   ├── lwm2m_tlv/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── lwm2m_tlv.c
│   │   │   │   └── lwm2m_tlv.h
│   │   │   └── CMakeLists.txt
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
│   │   │   ├── sqlite_format/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── sqlite_format.c
│   │   │   │   └── sqlite_format.h
│   │   │   └── CMakeLists.txt
│   │   ├── wal/
│   │   │   ├── wal/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── wal.c
│   │   │   │   └── wal.h
│   │   │   ├── wal_fs/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   └── wal_fs.h
│   │   │   ├── wal_store/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── wal_store.c
│   │   │   │   └── wal_store.h
│   │   │   └── CMakeLists.txt
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
| `SIGFOX` | `Drivers/SigfoxUplink` | 364,588 | 29,004 |
| `NMEA0183+UBX` | `Drivers/UbloxGnss` | 368,824 | 29,956 |
| `SEN0192` | `Drivers/Sen0192` | 382,916 | 46,092 |
| `DMA+PREEMPT_QUEUE+DMA_SIMULATE` | `Peripherals/DmaIngest` | 383,340 | 54,092 |
| `ZWAVE+GATEWAY` | `Drivers/ZWaveGateway` | 383,464 | 46,588 |
| `ENOCEAN+GATEWAY` | `Drivers/EnOceanGateway` | 383,548 | 46,612 |
| `ZIGBEE+GATEWAY` | `Drivers/ZigbeeGateway` | 384,252 | 46,868 |
| `DMA+PREEMPT_QUEUE+GATEWAY+DMA_SIMULATE` | `Drivers/RadioGateway` | 384,968 | 54,412 |
| `DMA+PREEMPT_QUEUE+FORWARD+DMA_SIMULATE` | `Foundation/InterfaceForward` | 385,636 | 55,220 |
| `LD2410` | `Drivers/Ld2410` | 385,676 | 46,540 |
| `THREAD+GATEWAY` | `Drivers/ThreadGateway` | 386,840 | 47,380 |
| `NRF24+GATEWAY` | `Drivers/Nrf24Gateway` | 387,376 | 46,332 |
| `LORA+GATEWAY` | `Drivers/LoRaGateway` | 387,632 | 46,332 |
| `PREEMPT_QUEUE` | `Foundation/PreemptQueue` | 388,384 | 48,940 |
| `PCA9685` | `Drivers/Pca9685` | 391,444 | 49,220 |
| `MPR121` | `Drivers/Mpr121` | 392,572 | 49,292 |
| `ADS1115` | `Drivers/Ads1115` | 392,736 | 49,196 |
| `INA219` | `Drivers/Ina219` | 392,916 | 49,228 |
| `SHT3X` | `Drivers/Sht3x` | 392,928 | 49,220 |
| `core/SSHCryptoSelfTest` | `L5-Session/SSHCryptoSelfTest` | 408,692 | 66,068 |
| `PN532+GATEWAY` | `Drivers/NfcGateway` | 414,608 | 47,788 |
| `ETHERNET` | `Peripherals/Ethernet` | 552,596 | 57,484 |
| `ETHERNET+ETH_W5500+ETH_W5500_CS+ETH_W5500_RST+ETH_W5500_INT+ETH_W5500_SCK+ETH_W5500_MISO+ETH_W5500_MOSI` | `Peripherals/EthernetW5500` | 588,762 | 58,128 |
| `IDEMIP` | `Foundation/IdemIpPing` | 863,396 | 119,624 |
| `WIFI_SNIFFER+PROMISC` | `Peripherals/WifiSniffer` | 921,964 | 63,984 |
| `STATSD+UDP` | `L7-Application/StatsdMetrics` | 922,868 | 71,016 |
| `SNMP+SNMP_TRAP+UDP` | `L7-Application/SnmpTrap` | 923,520 | 85,488 |
| `DNS_SERVER+UDP` | `L7-Application/DnsServer` | 925,576 | 71,824 |
| `NTP_SERVER+TIME_SOURCE+NMEA0183+NTP+UDP` | `L7-Application/NtpServer` | 928,004 | 70,896 |
| `COAP+COAP_BLOCK+COAP_MAX_PAYLOAD+UDP` | `L7-Application/CoapBlock` | 930,484 | 76,624 |
| `COAP+COAP_OBSERVE+UDP` | `L7-Application/CoapObserve` | 931,724 | 74,536 |
| `UDP_TELEMETRY+UDP` | `L7-Application/UdpTelemetry` | 932,456 | 70,648 |
| `IKEV2` | `L5-Session/IKEv2` | 942,796 | 84,736 |
| `DNC+TCP_CLIENT+DNS_RESOLVER` | `L7-Application/EthernetDnc` | 952,544 | 112,032 |
| `DNS_RESOLVER+TCP_CLIENT+LSV2` | `L7-Application/HeidenhainLsv2` | 952,616 | 112,672 |
| `DNS_RESOLVER+TCP_CLIENT+SCPI` | `L7-Application/Scpi` | 953,128 | 112,416 |
| `DNS_RESOLVER+TCP_CLIENT+HAAS_MDC` | `L7-Application/HaasMdc` | 954,208 | 112,056 |
| `DNS_RESOLVER+TCP_CLIENT+VXI11` | `L7-Application/Vxi11` | 954,460 | 112,800 |
| `MQTT+TCP_CLIENT+DNS_RESOLVER` | `L7-Application/MqttClient` | 956,008 | 112,704 |
| `RTC+TIME_SOURCE+NTP` | `Drivers/Rtc` | 957,884 | 86,960 |
| `HTTP_CLIENT+TCP_CLIENT+DNS_RESOLVER` | `L7-Application/HttpClient` | 958,016 | 115,704 |
| `core/Basic` | `Foundation/Basic` | 962,340 | 72,960 |
| `ACCEPT_THROTTLE` | `L4-Transport/AcceptThrottle` | 962,380 | 72,960 |
| `core/MediaStreaming` | `L7-Application/MediaStreaming` | 962,620 | 72,960 |
| `core/CORS` | `L7-Application/CORS` | 962,628 | 72,960 |
| `core/RegexRoutes` | `L7-Application/RegexRoutes` | 962,676 | 72,960 |
| `OTA_ROLLBACK` | `L7-Application/OtaRollback` | 962,720 | 72,992 |
| `core/PathParams` | `L7-Application/PathParams` | 962,780 | 72,960 |
| `core/Middleware` | `L7-Application/Middleware` | 962,800 | 72,968 |
| `core/NetEgress` | `L7-Application/NetEgress` | 962,836 | 72,960 |
| `core/ResponseHeaders` | `L7-Application/ResponseHeaders` | 962,992 | 72,960 |
| `RADIO_POWER+RADIO_WIFI_PS` | `L7-Application/RadioPower` | 963,020 | 73,040 |
| `LOGBUF` | `L7-Application/LogBuffer` | 963,096 | 76,584 |
| `PER_IP_THROTTLE` | `L4-Transport/PerIpThrottle` | 963,152 | 73,000 |
| `core/ChunkedResponse` | `L7-Application/ChunkedResponse` | 963,240 | 72,976 |
| `KEEPALIVE` | `L4-Transport/KeepAlive` | 963,284 | 72,960 |
| `GUARDRAILS` | `L7-Application/Guardrails` | 963,352 | 73,016 |
| `core/Templating` | `L7-Application/Templating` | 963,756 | 73,000 |
| `PARTITION_MONITOR` | `L7-Application/PartitionMonitor` | 963,764 | 73,040 |
| `core/InterfaceFilter` | `L7-Application/InterfaceFilter` | 964,104 | 72,960 |
| `core/Expert` | `Foundation/Expert` | 964,132 | 72,976 |
| `STATS+WEB_ASSETS` | `L7-Application/Stats` | 964,188 | 73,040 |
| `core/FormParams` | `L6-Presentation/FormParams` | 964,244 | 72,960 |
| `IP_ALLOWLIST` | `L4-Transport/IpAllowlist` | 964,296 | 73,000 |
| `DIAG` | `L7-Application/Diagnostics` | 964,364 | 72,960 |
| `SSE` | `L6-Presentation/ServerSentEvents` | 964,452 | 73,576 |
| `PACKML` | `L7-Application/PackML` | 964,488 | 73,008 |
| `MODBUS` | `L7-Application/ModbusTcp` | 964,604 | 73,352 |
| `CONTROL` | `L7-Application/PidTuning` | 964,720 | 81,192 |
| `IPV6` | `Foundation/IPv6` | 964,720 | 73,016 |
| `TELNET` | `L5-Session/Telnet` | 964,856 | 74,592 |
| `SIMATIC` | `L7-Application/SimaticSerial` | 965,080 | 74,688 |
| `MULTIPART` | `L6-Presentation/Multipart` | 965,088 | 72,976 |
| `MODBUS+MODBUS_MASTER` | `L7-Application/ModbusScan` | 965,400 | 73,360 |
| `SPA_ROUTER` | `L7-Application/SpaFallback` | 965,468 | 73,040 |
| `POWER_MGMT` | `L7-Application/PowerGovernor` | 965,648 | 73,100 |
| `GPIO_MAP` | `L7-Application/GpioMap` | 965,812 | 73,104 |
| `CBOR` | `L6-Presentation/Cbor` | 965,840 | 73,032 |
| `STATS+METRICS+WEB_ASSETS` | `L7-Application/PrometheusMetrics` | 966,288 | 73,088 |
| `GRAPHQL` | `L7-Application/GraphQL` | 966,464 | 78,232 |
| `MSGPACK` | `L6-Presentation/MsgPack` | 966,768 | 73,032 |
| `CONFIG_STORE+CONFIG_IO` | `L7-Application/ConfigExport` | 966,824 | 73,176 |
| `JSON` | `L6-Presentation/Json` | 966,888 | 73,168 |
| `SYSLOG+UDP` | `L7-Application/Syslog` | 967,468 | 80,480 |
| `TELEMETRY` | `L7-Application/Telemetry` | 970,432 | 73,264 |
| `COAP+UDP` | `L7-Application/CoAP` | 971,988 | 83,600 |
| `DNS_RESOLVER+TCP_CLIENT+ADS` | `L7-Application/AdsClient` | 973,028 | 113,112 |
| `SNMP+UDP` | `L7-Application/SNMP` | 973,616 | 93,408 |
| `core/Sysadmin` | `Foundation/Sysadmin` | 975,200 | 72,968 |
| `OPCUA` | `L7-Application/OpcUa` | 980,564 | 83,280 |
| `DEVICE_ID` | `L7-Application/DeviceUuid` | 984,660 | 114,192 |
| `TOTP` | `L7-Application/Totp` | 985,384 | 114,264 |
| `NTP+TIME_SOURCE` | `L7-Application/TimeSourceFallback` | 988,336 | 114,400 |
| `OPCUA+UMATI` | `L7-Application/Umati` | 988,496 | 83,424 |
| `OPCUA+ROBOTICS` | `L7-Application/Robotics` | 988,580 | 83,672 |
| `OPCUA+EUROMAP77` | `L7-Application/Euromap77` | 988,612 | 83,456 |
| `DNS_RESOLVER` | `L7-Application/DnsResolver` | 989,392 | 115,624 |
| `CSRF` | `L7-Application/Csrf` | 990,780 | 115,144 |
| `AUDIT_LOG` | `L7-Application/AuditLog` | 991,200 | 118,936 |
| `WEBSOCKET` | `L6-Presentation/WebSocket` | 991,272 | 117,056 |
| `JWT` | `L6-Presentation/JWTAuth` | 991,596 | 118,520 |
| `core/Advanced` | `Foundation/Advanced` | 992,128 | 114,240 |
| `AUTH` | `L6-Presentation/DigestAuth` | 994,784 | 119,296 |
| `WEB_ASSETS+WEB_TERMINAL+WEBSOCKET` | `L6-Presentation/WebTerminal` | 995,112 | 117,176 |
| `DEFLATE_RFC1951+WEBSOCKET+WS_DEFLATE` | `L6-Presentation/WebSocketCompression` | 996,172 | 117,184 |
| `AUTH+AUTH_LOCKOUT` | `L6-Presentation/AuthLockout` | 996,448 | 120,128 |
| `MDNS` | `L7-Application/mDNS` | 997,408 | 75,376 |
| `IFACE_BRIDGE` | `L7-Application/InterfaceBridge` | 998,558 | 78,044 |
| `MDNS+PROMISC+WIFI_SNIFFER+MDNS_ADAPTIVE` | `L7-Application/MdnsAdaptive` | 999,652 | 75,888 |
| `AUTH+AUTH_LOCKOUT+FORWARDED_TRUST` | `L6-Presentation/ForwardedTrust` | 1,000,660 | 120,224 |
| `MNT` | `L7-Application/Mnt` | 1,012,816 | 73,464 |
| `FILE_SERVING+MNT` | `L7-Application/FileServing` | 1,016,140 | 73,720 |
| `MNT+RANGE+FILE_SERVING` | `L7-Application/Range` | 1,018,664 | 73,720 |
| `ETAG+FILE_SERVING+MNT` | `L7-Application/ETag` | 1,019,876 | 73,776 |
| `HOTSWAP` | `L7-Application/HotSwapStorage` | 1,045,522 | 73,924 |
| `WEB_ASSETS+HTTP_DELIVERY+FILE_SERVING+RANGE+MNT` | `L7-Application/HttpDelivery` | 1,061,522 | 74,412 |

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
| `PROTOCORE_ENABLE_HTTP_CLOCK` | `0` | Auto-inject a `Date` response header (RFC 7231 7.1.1.2) when a wall-clock time is available. |
| `PROTOCORE_ENABLE_HTTP_DELIVERY` | `0` |  |
| `PROTOCORE_ENABLE_HTTP_PARSER` | `1` |  |
| `PROTOCORE_ENABLE_HTTP_ROUTE` | `0` |  |
| `PROTOCORE_ENABLE_HW_HEALTH` | `0` |  |
| `PROTOCORE_ENABLE_ICCP` | `0` |  |
| `PROTOCORE_ENABLE_IDEMIP` | `0` |  |
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
| `PROTOCORE_ENABLE_MSGPACK` | `0` |  |
| `PROTOCORE_ENABLE_MTCONNECT` | `0` |  |
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
`src/protocore_config.h` and `src/config/memory_sizing/buffer_sizing.h` by
`tools/ci_tooling/generate/gen_readme_sections.py`. Override any
with a build flag (e.g. `-DMAX_CONNS=6`); illegal combinations are caught by `#error`
guards at compile time.

<!-- BEGIN GENERATED CONFIG-OVERRIDES (tools/ci_tooling/generate/gen_readme_sections.py) -->

<!-- prettier-ignore-start -->

| Constant | Default | Description |
| :------- | :-----: | :---------- |
| `BODY_BUF_SIZE` | `256` | Maximum request body bytes stored in `HttpReq::body`. |
| `CACHE_CONTROL_BUF_SIZE` | `64` | Size of the optional Cache-Control header line stored in PC. |
| `CHUNK_BUF_SIZE` | `1440` | Per-chunk staging buffer for send_chunked()'s ChunkSource (max bytes a source produces per call, hence the largest single chunk on the wire). |
| `CONN_TIMEOUT_MS` | `5000` | Compile-time default for connection idle timeout in milliseconds. |
| `CORS_HDR_BUF_SIZE` | `192` | Size of the pre-built CORS header block stored in PC. |
| `DIGEST_AUTH_HDR_MAX` | `384` | Capacity for the full `Authorization` header value (Digest auth). |
| `EXTRA_HDR_BUF_SIZE` | `256` | Per-connection buffer for app-supplied custom response headers and cookies. |
| `FILE_CHUNK_SIZE` | `1024` | Bytes read from the filesystem and passed to tcp_write() per loop(). |
| `JSON_MAX_DEPTH` | `8` | Maximum object/array nesting depth for the JsonWriter (see json.h). |
| `MAX_AUTH_LEN` | `32` | Maximum username or password length for HTTP Basic Authentication. |
| `MAX_BOUNDARY_LEN` | `72` | Maximum MIME boundary length (RFC 2046 allows up to 70 characters). |
| `MAX_CONNS` | `8` | Maximum simultaneous TCP connections (fixed static pool; ~3.95 KB of internal RAM per slot). |
| `MAX_HEADERS` | `8` | Maximum HTTP headers stored per request. |
| `MAX_KEY_LEN` | `32` | Maximum header field-name length (e.g. |
| `MAX_LISTENERS` | `3` | Maximum number of simultaneously active listener ports. |
| `MAX_MIDDLEWARE` | `4` | Maximum globally-registered middleware functions. |
| `MAX_MULTIPART_PARTS` | `4` | Maximum simultaneously parsed multipart parts per request. |
| `MAX_PATH_LEN` | `64` | Maximum URL path length (including leading `/`). |
| `MAX_PATH_PARAMS` | `4` | Maximum number of `:name` path parameters captured per route match. |
| `MAX_QUERY_LEN` | `128` | Maximum raw query-string length (everything after `?`). |
| `MAX_QUERY_PARAMS` | `8` | Maximum number of parsed query-string parameters. |
| `MAX_ROUTES` | `16` | Maximum simultaneously registered routes. |
| `MAX_SSE_CONNS` | `2` | Maximum simultaneous SSE connections. |
| `MAX_SSH_CONNS` | `1` | Maximum simultaneous SSH connections. |
| `MAX_TELNET_CONNS` | `2` | Maximum simultaneous Telnet connections. |
| `MAX_TLS_CONNS` | `1` | Maximum simultaneous TLS connections (each holds mbedTLS record buffers). |
| `MAX_VAL_LEN` | `48` | Maximum header field-value length. |
| `MAX_WS_CONNS` | `2` | Maximum simultaneous WebSocket connections. |
| `PROTOCORE_ACCEPT_THROTTLE_MAX` | `20` | Max accepted connections per throttle window (see PROTOCORE_ENABLE_ACCEPT_THROTTLE). |
| `PROTOCORE_ACCEPT_THROTTLE_WINDOW_MS` | `1000` | Throttle window length in milliseconds (see PROTOCORE_ENABLE_ACCEPT_THROTTLE). |
| `PROTOCORE_ADS1115_DIFFERENTIAL` | `0` | ADS1115 input mode: 0 = single-ended (AINx vs GND), 1 = differential. |
| `PROTOCORE_ADS1115_I2C_ADDR` | `0x48` | I2C address of the ADS1115 (0x48 with ADDR to GND; 0x49/0x4A/0x4B for VDD/SDA/SCL). |
| `PROTOCORE_AUTH_LOCKOUT_BASE_MS` | `1000` | First lockout duration in ms; doubles on each further failure. |
| `PROTOCORE_AUTH_LOCKOUT_MAX_MS` | `300000` | Maximum lockout duration in ms (the exponential backoff cap). |
| `PROTOCORE_AUTH_LOCKOUT_SLOTS` | `16` | Number of source IPs the auth lockout tracks (BSS bucket table). |
| `PROTOCORE_AUTH_LOCKOUT_THRESHOLD` | `5` | Consecutive failed auths from one IP before it is locked out. |
| `PROTOCORE_BASE64_SWAR` | `1` | Use the SWAR base64 decoder (classify 4 characters per 32-bit word). |
| `PROTOCORE_BRIDGE_MAX_DRAIN` | `8` | Chunks a STREAM target moves per poll before yielding, bounding the UART drain loop. |
| `PROTOCORE_BRIDGE_MAX_RULES` | `8` | Max concurrent address:port -> bus rules (server/net/iface_bridge). |
| `PROTOCORE_BRIDGE_STREAM_CHUNK` | `256` | STREAM (UART) pipe chunk size (bytes) for server/net/iface_bridge - one socket<->UART hop. |
| `PROTOCORE_BRIDGE_TXN_MAX` | `256` | Max write / read payload (bytes) per TRANSACTION frame (server/net/iface_bridge). |
| `PROTOCORE_BRIDGE_UART_TXN_MS` | `50` | UART TRANSACTION read window (ms): how long a write-then-read waits for the read_len reply. |
| `PROTOCORE_CLIENT_RX_BUF` | `8192` | Per-connection wire receive ring size (bytes). |
| `PROTOCORE_CLOSING_TIMEOUT_MS` | `2000` | Upper bound (ms) a slot may dwell in CONN_CLOSING after a graceful close before the idle sweep force-aborts it. |
| `PROTOCORE_COAPS_SERVER_BORROW` | `34816u` |  |
| `PROTOCORE_COAP_BLOCK1_MAX` | `1024` | Reassembly buffer for a block-wise (Block1) request upload, in bytes. |
| `PROTOCORE_COAP_BLOCK_SZX_MAX` | `6` | Largest block-size exponent (SZX) the server will use: block size = 2^(SZX+4) bytes, SZX 0..6 (16..1024). |
| `PROTOCORE_COAP_DEDUP_ENTRIES` | `4` | CoAP message de-duplication cache size (RFC 7252 sec 4.5). |
| `PROTOCORE_COAP_DEDUP_LIFETIME_MS` | `247000u` | How long (ms) a dedup entry stays fresh - RFC 7252 EXCHANGE_LIFETIME (~247 s) by default, past which a repeat Message-ID is treated as a new exchange. |
| `PROTOCORE_COAP_DEDUP_RESP_MAX` | `256` | Largest cached response the dedup cache retains per entry; a bigger response is not cached (a retransmission re-processes it, fine for the idempotent GET whose block-wise reply exceeds this). |
| `PROTOCORE_COAP_MAX_OBSERVERS` | `4` | Maximum simultaneous CoAP observers (one slot per observed resource per client). |
| `PROTOCORE_COAP_MAX_PATH` | `64` | Maximum reconstructed Uri-Path length, including separators and the leading '/'. |
| `PROTOCORE_COAP_MAX_PAYLOAD` | `256` | Maximum CoAP request/response payload in bytes. |
| `PROTOCORE_COAP_MAX_QUERY` | `64` | Maximum reconstructed Uri-Query length (segments joined by '&'). |
| `PROTOCORE_COAP_MAX_RESOURCES` | `8` | Maximum registered CoAP resources (the server's fixed routing table). |
| `PROTOCORE_COAP_OBSERVE_PORT` | `5683` | Default UDP port the CoAP observe transport notifies from (IANA well-known 5683). |
| `PROTOCORE_CONFIG_KEY_MAX` | `16` | Max key length incl. |
| `PROTOCORE_CONFIG_MAX_ENTRIES` | `16` | Max key/value entries in the host (test) config backend. |
| `PROTOCORE_CONFIG_VAL_MAX` | `2048` | Max value bytes per entry in the host (test) config backend. |
| `PROTOCORE_DASHBOARD_JSON_BUF` | `1024` | Stack buffer for the dashboard layout / values JSON (bytes). |
| `PROTOCORE_DASHBOARD_MAX_WIDGETS` | `16` | Maximum widgets in the dashboard table (BSS value array). |
| `PROTOCORE_DAV_MAX_DEPTH` | `8` | Deepest tree a WebDAV DELETE / COPY walks before refusing (see PROTOCORE_ENABLE_WEBDAV). |
| `PROTOCORE_DEBUG_CHECKS` | `0` | Compile the library's internal debug checks (default 0 = off). |
| `PROTOCORE_DEFER_QUEUE_DEPTH` | `8` | Depth of each worker's deferred-callback queue. |
| `PROTOCORE_DELIVERY_MANIFEST_BUF` | `512` | Buffer the precache manifest JSON is built into. |
| `PROTOCORE_DELIVERY_PRECACHE_MAX` | `16` | Most asset paths a service-worker precache manifest may list. |
| `PROTOCORE_DMA_BUF_SIZE` | `256` | Bytes per DMA transfer buffer (RX is double-buffered at this size). |
| `PROTOCORE_DMA_CHANNELS` | `2` | Number of DMA channels (static-allocated; each is one peripheral link). |
| `PROTOCORE_DNC_LEADER_LEN` | `32` | Default leader/trailer runout length for the DNC encoder. |
| `PROTOCORE_DNC_LINE_MAX` | `128` | Largest G-code block (one line) the DNC decoder reassembles. |
| `PROTOCORE_DNC_XOFF_MAX_POLLS` | `200000` | Safety cap on how many times the DNC stream engine polls the reverse channel while paused by an XOFF, before giving up with an I/O error. |
| `PROTOCORE_DNS_CLIENT_PORT` | `1153` | Local UDP port the portable resolver asks from and hears the answer on. |
| `PROTOCORE_DNS_NAME_MAX` | `128` | Max length of a queried/stored DNS name (bytes, incl NUL). |
| `PROTOCORE_DNS_SERVER_MAX_RECORDS` | `8` | Max A records in the DNS server's fixed table. |
| `PROTOCORE_DNS_SERVER_TTL` | `60` | TTL (seconds) the DNS server puts on its answers. |
| `PROTOCORE_DNS_TIMEOUT_MS` | `5000` |  |
| `PROTOCORE_DTLS_PMTU_DEFAULT` | `1232` | Largest datagram a DTLS handshake flight will put on the wire, before a connection overrides it (RFC 9147 sec 4.3). |
| `PROTOCORE_EDGE_CENC_MAX` | `32` | Stored Content-Encoding to replay (e.g. |
| `PROTOCORE_EDGE_CTYPE_MAX` | `64` | Stored Content-Type to replay. |
| `PROTOCORE_EDGE_ETAG_MAX` | `64` | Stored validator (ETag, quotes included). |
| `PROTOCORE_EDGE_LASTMOD_MAX` | `40` | Stored Last-Modified (RFC 1123 date). |
| `PROTOCORE_EM77_NS` | `1` | NamespaceIndex the EUROMAP 77 IMM_MES_Interface nodes live at (default 1). |
| `PROTOCORE_ENFORCE_HOST_HEADER` | `1` | Enforce the RFC 7230 §5.4 Host-header requirement (default on). |
| `PROTOCORE_ENOCEAN_MAX_DATA` | `512` | Reject an ESP3 telegram whose declared data length exceeds this (framing sanity). |
| `PROTOCORE_ETH_W5500` | `0` |  |
| `PROTOCORE_EXC_COREDUMP_CHUNK` | `512` | Chunk the core-dump image is streamed out of flash in. |
| `PROTOCORE_FAILSAFE_BORROW` | `256u` |  |
| `PROTOCORE_FAILSAFE_MAX_LIFELINES` | `8` | Max monitored lifelines in the fail-safe registry (static, zero-heap). |
| `PROTOCORE_FDC2214_I2C_ADDR` | `0x2A` | I2C address of the FDC2214, set by the ADDR pin: 0x2A when it is low, 0x2B when it is high. |
| `PROTOCORE_FILESYSTEM_BORROW` | `3072u` |  |
| `PROTOCORE_FILESYSTEM_PATH_MAX` | `256` | Largest absolute path the SFTP/SCP server resolves (mount root + request path). |
| `PROTOCORE_FILE_SERVING_BORROW` | `16u` |  |
| `PROTOCORE_FLOW_EXPORT_BORROW` | `64u` |  |
| `PROTOCORE_FTP_CHUNK` | `512` | Bytes staged per data-channel write when the session driver streams a payload. |
| `PROTOCORE_FTP_CMD_MAX` | `256` | Suggested FTP control-command buffer size. |
| `PROTOCORE_FTP_REPLY_BUF` | `512` | Control-reply accumulator for the FTP session driver. |
| `PROTOCORE_FTP_SESSION_BORROW` | `1536u` |  |
| `PROTOCORE_FTP_TIMEOUT_MS` | `8000` | Per-step timeout for the FTP session driver: connect, and each control reply. |
| `PROTOCORE_FWD_ACL_PATLEN` | `4` | Bytes an ACL entry can match (its pattern / mask length). |
| `PROTOCORE_FWD_INSPECT` | `0` | Build-time toggle for the forwarding-path inspection hook (default off, for cost + privacy). |
| `PROTOCORE_FWD_MAX_ACL` | `8` | Max ingress access-control entries (byte-pattern permit/deny; static). |
| `PROTOCORE_FWD_MAX_ROUTES` | `8` | Max policy routes (byte-pattern -> egress interface; static). |
| `PROTOCORE_FWD_MAX_RULES` | `8` | Max forwarding rules (src -> dst allow/deny + rate cap; static-allocated). |
| `PROTOCORE_GPIO_JSON_BUF` | `1024` | Stack buffer for the GPIO-map JSON (bytes). |
| `PROTOCORE_GPIO_MAX` | `40` | Maximum GPIO pins the mapper reports (BSS table). |
| `PROTOCORE_GRAPHQL_BORROW` | `5120u` |  |
| `PROTOCORE_GUARDRAILS_BORROW` | `16u` |  |
| `PROTOCORE_GUARDRAIL_FRAG_MIN_BLOCK` | `4096` | Largest-free-block floor (bytes); below this trips the fragmentation guardrail. |
| `PROTOCORE_GUARDRAIL_HEAP_MIN` | `8192` | Free-heap floor (bytes); below this trips the heap guardrail. |
| `PROTOCORE_GUARDRAIL_STACK_MIN` | `512` | Task remaining-stack floor (bytes); below this trips the stack guardrail. |
| `PROTOCORE_GW_MAX_PORTS` | `4` | Max southbound gateway ports (radios / buses; static-allocated). |
| `PROTOCORE_H2_HDR_BLOCK` | `4096` | Header-block reassembly buffer for HTTP/2 requests that span HEADERS + CONTINUATION frames (a single END_HEADERS frame decodes in place and needs no copy). |
| `PROTOCORE_H2_MAX_CONTINUATION` | `8` | CONTINUATION frames one header block may span (RFC 9113 sec 6.10). |
| `PROTOCORE_H2_MAX_FRAME` | `16384` | Largest HTTP/2 frame we accept, in bytes (advertised as SETTINGS_MAX_FRAME_SIZE). |
| `PROTOCORE_H2_MAX_STREAMS` | `8` | Max concurrent HTTP/2 streams per connection (advertised as MAX_CONCURRENT_STREAMS). |
| `PROTOCORE_H2_POOL_IN_PSRAM` | `0` | Place the HTTP/2 connection-engine pool in external PSRAM (ESP32). |
| `PROTOCORE_H3_CRYPTO_BUF` | `2048` | Maximum bytes of one QUIC/TLS handshake CRYPTO flight (RFC 9001). |
| `PROTOCORE_H3_MAX_STREAMS` | `8` | Maximum concurrent request streams per HTTP/3 connection. |
| `PROTOCORE_H3_QPACK_BLOCK` | `256` |  |
| `PROTOCORE_H3_QPACK_SCRATCH` | `512` |  |
| `PROTOCORE_HMMD_BAUD` | `115200` | HMMD UART baud rate (the module's factory default is 115200). |
| `PROTOCORE_HMMD_UART` | `2` | UART unit the HMMD is wired to. |
| `PROTOCORE_HOTSWAP_FAIL_THRESHOLD` | `3` | Consecutive I/O failures that declare a removable volume gone. |
| `PROTOCORE_HOTSWAP_PROBE_MS` | `2000` | Minimum gap between remount attempts while a volume is absent or faulted (ms). |
| `PROTOCORE_HPACK_MAX_ENTRIES` | `128` | Max HPACK dynamic-table entries (>= PROTOCORE_HPACK_TABLE_BYTES / 32, the min entry size). |
| `PROTOCORE_HPACK_TABLE_BYTES` | `4096` | Per-connection HPACK dynamic-table size in bytes (our decoder; advertised to the peer as SETTINGS_HEADER_TABLE_SIZE). |
| `PROTOCORE_HTTP3_PORT` | `443` | UDP port the HTTP/3 (QUIC) server binds by default (used by protocore_h3_cert). |
| `PROTOCORE_HTTP_BORROW` | `32u` |  |
| `PROTOCORE_HTTP_CLIENT_BORROW` | `3584u` |  |
| `PROTOCORE_HTTP_CLIENT_BUF_SIZE` | `2048` | Receive buffer (and max response size) for the outbound HTTP client, bytes. |
| `PROTOCORE_HTTP_CLIENT_TIMEOUT_MS` | `8000` | Outbound HTTP client connect/response timeout in milliseconds. |
| `PROTOCORE_HTTP_CLOCK_BORROW` | `32u` |  |
| `PROTOCORE_HTTP_PARSER_BORROW` | `32u` |  |
| `PROTOCORE_INA219_CURRENT_LSB_UA` | `100` | Default INA219 current LSB in microamps per bit (calibration input). |
| `PROTOCORE_INA219_I2C_ADDR` | `0x40` | I2C address of the INA219 (0x40 default; the A0/A1 pins select 0x40..0x4F). |
| `PROTOCORE_INA219_SHUNT_MOHM` | `100` | Default INA219 shunt resistance in milliohms (calibration input). |
| `PROTOCORE_IP_ALLOWLIST_SLOTS` | `8` | Number of CIDR rules the source-IP allowlist can hold (BSS table). |
| `PROTOCORE_JWT_MAX_LEN` | `512` | Maximum accepted JWT length in bytes (header.payload.signature). |
| `PROTOCORE_KEEPALIVE_MAX_REQUESTS` | `100` | Maximum requests served on one keep-alive connection before it is closed. |
| `PROTOCORE_LD2410_BAUD` | `256000` | LD2410 UART baud rate (the module's fixed factory default is 256000). |
| `PROTOCORE_LD2410_UART` | `2` | UART unit the LD2410 is wired to. |
| `PROTOCORE_LDC1614_I2C_ADDR` | `0x2A` | I2C address of the LDC1614, set by the ADDR pin: 0x2A when it is low, 0x2B when it is high. |
| `PROTOCORE_LOGBUF_BORROW` | `3584u` |  |
| `PROTOCORE_LOG_LINES` | `32` | Number of log lines retained in the ring. |
| `PROTOCORE_LOG_LINE_LEN` | `96` | Maximum length of one stored log line (bytes, including null). |
| `PROTOCORE_LORA_MAX_PAYLOAD` | `251` | Max LoRa payload bytes (SX127x FIFO is 256; RadioHead uses 251 + 4 header). |
| `PROTOCORE_LWM2M_TLV_BORROW` | `128u` |  |
| `PROTOCORE_MAX_UDP_LISTENERS` | `2` | Maximum simultaneously bound UDP ports (transport-layer UDP service). |
| `PROTOCORE_MDNS_ADAPTIVE_BORROW` | `96u` |  |
| `PROTOCORE_MDNS_LABEL_MAX` | `32` | Longest host label, service type or proto label the responder holds, NUL included. |
| `PROTOCORE_MDNS_MAX_SERVICES` | `4` | Services the responder advertises at once, `_http._tcp` included. |
| `PROTOCORE_MDNS_SERVICE_BORROW` | `320u` |  |
| `PROTOCORE_MDNS_TXT_MAX` | `128` | Bytes of packed `key=value` TXT strings, each with its own length byte ahead of it. |
| `PROTOCORE_MDNS_TX_MAX` | `512` | Response datagram the responder composes. |
| `PROTOCORE_MODBUS_COILS` | `64` | Number of Modbus coils (FC 1/5/15), single-bit R/W (BSS, bit-packed). |
| `PROTOCORE_MODBUS_DISCRETE_INPUTS` | `64` | Number of Modbus discrete inputs (FC 2), single-bit read-only (BSS, bit-packed). |
| `PROTOCORE_MODBUS_HOLDING_REGS` | `64` | Number of Modbus holding registers (FC 3/6/16), 16-bit R/W (BSS). |
| `PROTOCORE_MODBUS_INPUT_REGS` | `64` | Number of Modbus input registers (FC 4), 16-bit read-only (BSS). |
| `PROTOCORE_MPR121_I2C_ADDR` | `0x5A` | I2C address of the MPR121 (0x5A default; 0x5B/0x5C/0x5D via the ADDR pin). |
| `PROTOCORE_MPR121_RELEASE_THRESHOLD` | `6` | MPR121 per-electrode release threshold (delta counts; should be below the touch threshold). |
| `PROTOCORE_MPR121_TOUCH_THRESHOLD` | `12` | MPR121 per-electrode touch threshold (delta counts from baseline; NXP AN3944 suggests ~4..12). |
| `PROTOCORE_MQTT_BORROW` | `512u` |  |
| `PROTOCORE_MQTT_BUF_SIZE` | `1024` | MQTT packet buffer size in bytes (bounds one outgoing/incoming packet). |
| `PROTOCORE_MQTT_CONNECT_MS` | `8000` | What the whole MQTT connect is given, in milliseconds. |
| `PROTOCORE_MQTT_CT_BUF_SIZE` | `4096` | Ciphertext receive-ring size for MQTTS (draining ring; must exceed one TCP_MSS). |
| `PROTOCORE_MQTT_KEEPALIVE_S` | `30` | Default MQTT keep-alive interval in seconds (PINGREQ cadence / CONNECT field). |
| `PROTOCORE_MQTT_MAX_INFLIGHT` | `4` | Outbound QoS 1/2 in-flight slots (unacknowledged exchanges awaiting their acknowledgement). |
| `PROTOCORE_MQTT_MAX_TOPIC` | `128` | Maximum inbound MQTT topic length (including NUL) delivered to the callback. |
| `PROTOCORE_MQTT_RETRANSMIT_MS` | `5000` | Retransmit timeout (ms) for an unacknowledged in-flight QoS 1/2 message. |
| `PROTOCORE_MQTT_RX_QOS2_SLOTS` | `8` | Inbound QoS 2 packet-id de-duplication ring depth (PUBREC-acknowledged, awaiting PUBREL). |
| `PROTOCORE_MTLS_SUBJECT_MAX` | `128` | Maximum length of a verified mTLS peer subject DN string (incl. |
| `PROTOCORE_NRF24_PAYLOAD` | `32` | nRF24 fixed payload width in bytes (1..32; the chip's static payload size). |
| `PROTOCORE_NTP_CLIENT_PORT` | `1123` | Local UDP port the portable SNTP client asks from. |
| `PROTOCORE_NTP_SERVER_BORROW` | `16u` |  |
| `PROTOCORE_NTP_SERVER_STRATUM` | `3` | Stratum the NTP server advertises (distance from a reference clock; 1-15). |
| `PROTOCORE_NTP_SERVICE_BORROW` | `64u` |  |
| `PROTOCORE_NTRIP_MAX_MOUNTS` | `2` | Max distinct mountpoints a single caster serves (each = one RTCM stream). |
| `PROTOCORE_NTRIP_MAX_ROVERS` | `4` | Max concurrent rover connections a caster serves corrections to (services/timing_position/gnss). |
| `PROTOCORE_NTRIP_MOUNT_MAX` | `32` | Max length (incl. |
| `PROTOCORE_NTRIP_REQ_MAX` | `512` | Max NTRIP client request size (bytes) the caster buffers while reading the request headers. |
| `PROTOCORE_OAUTH2_BORROW` | `3072u` |  |
| `PROTOCORE_OIDC_MAX_LEN` | `1600` | Max accepted OIDC ID-token length (also sizes the Authorization buffer). |
| `PROTOCORE_OTA_CONFIRM_WINDOW_MS` | `30000` | Confirm window (ms): a pending image not confirmed within this rolls back. |
| `PROTOCORE_PARTITION_JSON_BUF` | `1024` | Stack buffer for the partition-map JSON (bytes). |
| `PROTOCORE_PARTITION_MAX` | `16` | Maximum partitions the monitor reports (BSS table). |
| `PROTOCORE_PCA9685_FREQ` | `50` | Default PWM output frequency in Hz (50 Hz suits hobby servos). |
| `PROTOCORE_PCA9685_I2C_ADDR` | `0x40` | I2C address of the PCA9685 (0x40 default; the six address pins select 0x40..0x7F). |
| `PROTOCORE_PER_IP_THROTTLE_MAX` | `10` | Max accepted connections per window from one source IP (see PROTOCORE_ENABLE_PER_IP_THROTTLE). |
| `PROTOCORE_PER_IP_THROTTLE_SLOTS` | `16` | Number of source IPv4 addresses tracked by the per-IP throttle (BSS bucket table). |
| `PROTOCORE_PER_IP_THROTTLE_WINDOW_MS` | `10000` | Per-IP throttle window length in milliseconds (see PROTOCORE_ENABLE_PER_IP_THROTTLE). |
| `PROTOCORE_PHY_MAX_IFACES` | `4` | Interfaces layer 1 can carry: wifi station and softAP, ethernet, a bridged bus, a radio. |
| `PROTOCORE_PLAINTEXT_SCRATCH` | `10240` |  |
| `PROTOCORE_PN532_MAX_DATA` | `254` | Reject a PN532 normal frame whose declared length exceeds this (framing sanity). |
| `PROTOCORE_POWER_BUSY_PCT` | `40` | Load percentage at/above which the ceiling clock is used. |
| `PROTOCORE_POWER_MGMT_BORROW` | `8u` |  |
| `PROTOCORE_POWER_MHZ_MAX` | `240` | CPU clock (MHz) when there is work to do. |
| `PROTOCORE_POWER_MHZ_MIN` | `80` | CPU clock (MHz) when idle, thermally throttled, or recovering from a brownout. |
| `PROTOCORE_POWER_RECOVER_MS` | `10000` | How long (ms) to hold the floor clock after a brownout reset before ramping back up. |
| `PROTOCORE_POWER_TEMP_COOL_C` | `70` | Die temperature (C) at/below which the throttle is released. |
| `PROTOCORE_POWER_TEMP_HOT_C` | `80` | Die temperature (C) at/above which the clock is throttled. |
| `PROTOCORE_PQ_DEPTH` | `16` | Capacity of the preempting queue in items (static-allocated). |
| `PROTOCORE_PQ_INTERNAL_PRIORITY` | `8` | Base FreeRTOS priority for the internal preempting lanes (DMA / forwarding / device access). |
| `PROTOCORE_PQ_ITEM_SIZE` | `32` | Bytes per preempting-queue item (the posted item must fit). |
| `PROTOCORE_PQ_STACK` | `4096` | Stack (bytes) for each preempting-queue processing task (ESP32). |
| `PROTOCORE_PROTOBUF_BORROW` | `512u` |  |
| `PROTOCORE_QUIC_INGEST_RING` | `8` | Datagrams buffered from the lwIP thread until the server poll drains them. |
| `PROTOCORE_QUIC_MAX_CONNS` | `2` | Simultaneous HTTP/3 connections. |
| `PROTOCORE_QUIC_MAX_DATAGRAM` | `1350` | Largest UDP payload the QUIC transport sends or accepts (conservative, under a 1500 MTU). |
| `PROTOCORE_RADIO_MAX_TX_DBM` | `0` | Max TX power cap in dBm (2..20); 0 = leave the platform default. |
| `PROTOCORE_RADIO_WIFI_PS` | `0` | WiFi modem-sleep mode: 0 = none (max perf), 1 = min modem, 2 = max modem. |
| `PROTOCORE_RCWL0516_BORROW` | `32u` |  |
| `PROTOCORE_RELAY_BUF` | `2048` | Per-direction relay buffer size (bytes) for server/net/relay. |
| `PROTOCORE_RELAY_CONNECT_MS` | `5000` | Blocking connect timeout (ms) when the relay listener dials the origin on a new inbound. |
| `PROTOCORE_RELAY_DRAIN_MAX` | `8` | Max protocore_relay_step passes per poll for the relay listener. |
| `PROTOCORE_RELAY_HOST_MAX` | `64` | Max origin hostname length (bytes, incl. |
| `PROTOCORE_RELAY_MAX_CONNS` | `4` | Max concurrent relayed connections (bridge table size) for the relay listener. |
| `PROTOCORE_RELAY_MAX_PUBLISH` | `4` | Max published relay ports (bind table size) for the relay listener. |
| `PROTOCORE_REQUEST_TIMEOUT_MS` | `10000` | Request-header read deadline in milliseconds (slow-loris defense). |
| `PROTOCORE_ROBOTICS_AXES` | `6` | Number of Axes the robotics MotionDevice exposes (default 6; must fit PROTOCORE_OPCUA_REF_MAX). |
| `PROTOCORE_ROBOTICS_NS` | `1` | NamespaceIndex the robotics MotionDeviceSystem nodes live at (default 1). |
| `PROTOCORE_RTC_I2C_ADDR` | `0x68` | I2C address of the RTC (DS1307/DS3231 are fixed at 0x68). |
| `PROTOCORE_SCPI_ERR_QUEUE` | `8` | SCPI error/event queue depth (entries). |
| `PROTOCORE_SEN0192_ACTIVE_HIGH` | `1` | SEN0192 OUT polarity: 1 = the OUT line reads HIGH on motion, 0 = active-LOW. |
| `PROTOCORE_SEN0192_BORROW` | `32u` |  |
| `PROTOCORE_SEN0192_HOLD_MS` | `2000` | Presence is held this many ms after the last active (motion) sample before it clears. |
| `PROTOCORE_SEN0192_PIN` | `4` | GPIO the SEN0192 OUT line is wired to. |
| `PROTOCORE_SFTP_MAX_HANDLES` | `4` | Max concurrent open SFTP handles (files + dirs) per SSH connection. |
| `PROTOCORE_SFTP_MAX_READ` | `1024` | Largest PROTOCORE_SSH_FXP_DATA payload returned for one READ (a short read - the client re-requests). |
| `PROTOCORE_SFTP_PKT_BUF` | `2048` | SFTP packet-assembly buffer per SFTP channel (bytes); bounds one non-streamed request/response. |
| `PROTOCORE_SHT3X_I2C_ADDR` | `0x44` | I2C address of the SHT3x (0x44 with ADDR low; 0x45 with ADDR high). |
| `PROTOCORE_SIGFOX_MAX_PAYLOAD` | `12` | Maximum Sigfox uplink payload (the network caps a message at 12 bytes). |
| `PROTOCORE_SIGNALING_BORROW` | `64u` |  |
| `PROTOCORE_SIMATIC_BLOCK_MAX` | `256` | 3964R block-body buffer size (built/received bytes: DLE-stuffed payload + DLE ETX + BCC). |
| `PROTOCORE_SIMATIC_QVZ_MS` | `2000` | 3964R QVZ (Quittungsverzugszeit): handshake acknowledge-delay timeout, ms. |
| `PROTOCORE_SIMATIC_ZVZ_MS` | `220` | 3964R ZVZ (Zeichenverzugszeit): inter-character timeout while receiving a block, ms. |
| `PROTOCORE_SMB_BUF` | `1024` | SMB2 client work-buffer size (bytes) for smb_client's request/response framing. |
| `PROTOCORE_SMTP_BORROW` | `3584u` |  |
| `PROTOCORE_SMTP_CT_BUF_SIZE` | `4096` | Ciphertext receive-ring size for SMTPS, bytes (only used when the message is TLS). |
| `PROTOCORE_SMTP_LINE_MAX` | `256` | Max length of one SMTP command / address line (bytes, incl. |
| `PROTOCORE_SMTP_MSG_MAX` | `2048` | Max size of the assembled DATA payload (headers + dot-stuffed body), bytes. |
| `PROTOCORE_SMTP_REPLY_MAX` | `512` | Max size of one (possibly multi-line) server reply held while parsing, bytes. |
| `PROTOCORE_SMTP_TIMEOUT_MS` | `10000` | SMTP connect / per-reply timeout in milliseconds. |
| `PROTOCORE_SNMP_AGENT_BORROW` | `13312u` |  |
| `PROTOCORE_SNMP_NOTIFY_BORROW` | `1536u` |  |
| `PROTOCORE_SNMP_TRAP_BUF_SIZE` | `1024` | Static datagram buffer for an outbound SNMP notification, bytes. |
| `PROTOCORE_SNMP_TRAP_MAX_VARBINDS` | `8` | Maximum extra variable-bindings (beyond sysUpTime/snmpTrapOID) in one notification. |
| `PROTOCORE_SNMP_V3_BORROW` | `9216u` |  |
| `PROTOCORE_SOUTHBOUND_BORROW` | `128u` |  |
| `PROTOCORE_SPARKPLUG_BORROW` | `512u` |  |
| `PROTOCORE_SPB_METRIC_MAX` | `256` | Max serialized size of one Sparkplug B metric submessage (stack temp, bytes). |
| `PROTOCORE_SSH_ALLOW_PASSWORD` | `1` | Allow SSH password authentication (default on). |
| `PROTOCORE_SSH_COMP_SLOT_BYTES` | `81000u` | One SSH connection's zlib@openssh.com compressor: both streams and their windows. |
| `PROTOCORE_SSH_FWD_CHUNK` | `1024` | Max bytes moved per forward channel per poll, target -> client (<= SSH_PKT_BUF_SIZE). |
| `PROTOCORE_SSH_FWD_CONNECT_MS` | `3000` | Blocking connect timeout (ms) when opening a forward target. |
| `PROTOCORE_SSH_FWD_HOST_MAX` | `64` | Maximum forward target hostname length including null terminator. |
| `PROTOCORE_SSH_FWD_MAX` | `2` | Maximum concurrent forwarded TCP connections (must be <= PROTOCORE_CLIENT_CONNS). |
| `PROTOCORE_SSH_PORT_FORWARD` | `0` | SSH TCP port forwarding (`direct-tcpip`, i.e. |
| `PROTOCORE_SSH_PTY_TERM_MAX` | `24` | Max stored TERM value from a pty-req (RFC 4254 sec 6.2). |
| `PROTOCORE_SSH_PW_CHANGE_COOLDOWN_MS` | `60000u` |  |
| `PROTOCORE_SSH_RFWD_BRIDGE_MAX` | `2` | Maximum concurrent bridged connections across all remote forwards. |
| `PROTOCORE_SSH_RFWD_MAX` | `1` | Maximum concurrent remote-forward listeners (`ssh -R` / `tcpip-forward`). |
| `PROTOCORE_SSH_SLOT_BYTES` | `20096u` | One connection's whole span: the wire, the session, the exchange, the packet and the rx regions end to end, which ssh.c hands out one slot at a time. |
| `PROTOCORE_SSH_ZLIB_ACK_DRAM` | `0` | Acknowledge placing the SSH compressor in internal DRAM (no PSRAM). |
| `PROTOCORE_SSH_ZLIB_IN_PSRAM` | `0` | Place the per-connection SSH compression state in external PSRAM (ESP32). |
| `PROTOCORE_SSH_ZLIB_MAX_IN` | `2048` | Largest uncompressed payload the s2c compressor accepts in one call (bytes). |
| `PROTOCORE_SSH_ZLIB_WINDOW` | `8192` | SSH s2c DEFLATE sliding-window size in bytes (max back-reference distance). |
| `PROTOCORE_STACK_NET_ARP_RETRY_MS` | `1000u` | The milliseconds between two ARP requests of one active open. |
| `PROTOCORE_STACK_NET_ARP_WAIT_MS` | `3000u` | The milliseconds that an active open waits for the ARP row of its next hop. |
| `PROTOCORE_STACK_NET_LISTENERS` | `4u` | The TCP control blocks of the raw API that listen, apart from one for each entry. |
| `PROTOCORE_STACK_NET_PBUFS` | `4u` | The packet buffers of the raw API, which carry received text to the transport. |
| `PROTOCORE_STACK_NET_PBUF_BYTES` | `1460u` | The octets of each packet buffer of the raw API. |
| `PROTOCORE_STACK_NET_RX_BYTES` | `2048u` | The octets of the receive ring of each entry, a power of two. |
| `PROTOCORE_STACK_NET_SEGMENT_BYTES` | `1028u` | The octets of each transmit segment of an entry: the count in front, and the text of one write. |
| `PROTOCORE_STACK_NET_TASK_CORE` | `0` | The core of that task. |
| `PROTOCORE_STACK_NET_TASK_STACK` | `8192` | The stack octets of the task that runs the pass of the stack. |
| `PROTOCORE_STACK_NET_UDP_PCBS` | `8u` | The UDP control blocks of the raw API, at most IDEMIP_UDP_PCBS. |
| `PROTOCORE_STACK_TCP_EVENTS` | `32u` | The StackTcpEvent records that the event ring of the TCP of the stack holds, a power of two. |
| `PROTOCORE_STACK_TCP_FIN_WAIT_2_MS` | `60000u` | The milliseconds that a TCB stays in FIN-WAIT-2 after its entry goes back. |
| `PROTOCORE_STACK_TCP_SEGMENTS` | `4u` | The segments of the transmit ring of each TCP connection, a power of two. |
| `PROTOCORE_STATSD_BORROW` | `512u` |  |
| `PROTOCORE_STATSD_LINE_MAX` | `256` | Stack buffer for one StatsD line (bytes; caps metric name + value + tags). |
| `PROTOCORE_STATSD_PORT` | `8125` | Default StatsD collector UDP port (StatsD/Graphite standard). |
| `PROTOCORE_STOMP_MAX_HEADERS` | `16` | Max header lines parsed per STOMP frame (extras beyond this are ignored). |
| `PROTOCORE_SYSLOG_BORROW` | `512u` |  |
| `PROTOCORE_SYSLOG_DEFAULT_PORT` | `514` | Default syslog collector UDP port (RFC 5426 well-known 514; overridable at runtime via Syslog.collector.port + Syslog.init and here for a non-standard collector). |
| `PROTOCORE_SYSLOG_FIELD_MAX` | `32` | Maximum syslog HOSTNAME / APP-NAME field length (including NUL). |
| `PROTOCORE_SYSLOG_MSG_MAX` | `256` | Maximum formatted syslog datagram length in bytes (RFC 5424 line). |
| `PROTOCORE_TCP_NODELAY` | `1` | Disable Nagle's algorithm (set TCP_NODELAY) on every accepted connection. |
| `PROTOCORE_TC_MAX_WINDOW_SAMPLES` | `4096` | Max samples a window may hold (pretrigger_samples + posttrigger_samples), static-allocated. |
| `PROTOCORE_THEMES_INCLUDE_TRADEMARKED` | `1` | Include the trademark-named themes in the embedded set (default on / open-source). |
| `PROTOCORE_THREAD_MAX_DATA` | `256` | Max spinel payload bytes carried in one HDLC-lite frame. |
| `PROTOCORE_TIME_SOURCE_MAX` | `4` | Maximum registered time sources. |
| `PROTOCORE_TLS_ACK_MULTI_CONN_DRAM` | `0` | Acknowledge that a MAX_TLS_CONNS > 1 build has been sized to fit. |
| `PROTOCORE_TLS_ARENA_IN_PSRAM` | `0` | Place the TLS arena in external PSRAM instead of internal DRAM (ESP32). |
| `PROTOCORE_TLS_MAX_FRAG_LEN` | `0` | Cap TLS records via the Maximum Fragment Length extension (RFC 6066). |
| `PROTOCORE_TLS_TICKET_LIFETIME_S` | `86400` | Session-ticket lifetime / key-rotation period in seconds (see PROTOCORE_ENABLE_TLS_RESUMPTION). |
| `PROTOCORE_TRACE_CAPTURE_BORROW` | `256u` |  |
| `PROTOCORE_TRUSTED_PROXY_MAX` | `2` | Number of trusted-upstream CIDR rules the forwarded-client resolver holds (BSS table). |
| `PROTOCORE_UDP_RX_BUF_SIZE` | `1472` | Largest UDP datagram a bound port accepts, in bytes. |
| `PROTOCORE_UDP_RX_RING` | `2048` | Per-slot UDP receive ring, in bytes. |
| `PROTOCORE_UDP_TELEMETRY_BORROW` | `128u` |  |
| `PROTOCORE_UDP_TELEMETRY_BUF` | `256` | Stack buffer for one telemetry line (bytes). |
| `PROTOCORE_UMATI_NS` | `1` | NamespaceIndex the umati MachineTool nodes live at (default 1). |
| `PROTOCORE_UPLOAD_SERVICE_BORROW` | `48u` |  |
| `PROTOCORE_VL53L0X_I2C_ADDR` | `0x29` | I2C address of the VL53L0X. |
| `PROTOCORE_WEBDAV_BUF_SIZE` | `2048` | Buffer (BSS) for a WebDAV 207 Multi-Status response, in bytes (see PROTOCORE_ENABLE_WEBDAV). |
| `PROTOCORE_WEBDAV_MAX_ENTRIES` | `32` | Maximum children listed in a WebDAV Depth-1 PROPFIND (bounds the response). |
| `PROTOCORE_WEBDAV_MAX_PROPS` | `16` | Maximum properties echoed in a WebDAV PROPPATCH 207 response (bounds the response). |
| `PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS` | `14` | Channels tracked by the WiFi sniffer's per-channel survey. |
| `PROTOCORE_WORKER_CORE` | `1` | Core that worker 0 pins to (ESP32). |
| `PROTOCORE_WORKER_COUNT` | `1` | Number of server worker tasks (slots partitioned i % N). |
| `PROTOCORE_WORKER_POLL_TICKS` | `1` | Idle-sweep timeout, in FreeRTOS ticks, that a worker blocks between service iterations when no events are pending. |
| `PROTOCORE_WORKER_STACK_CURVE_MIN` | `12288` | Minimum worker-task stack (bytes) required once SSH is compiled in. |
| `PROTOCORE_WORKER_STACK_RSA_MIN` | `8192` | Minimum worker-task stack (bytes) required once an RSA-2048 verifier is compiled in (OIDC / SSH). |
| `PROTOCORE_WORKER_TASK_PRIORITY` | `5` | FreeRTOS priority for each server worker task (ESP32). |
| `PROTOCORE_WS_CLIENT_BORROW` | `4608u` |  |
| `PROTOCORE_WS_CLIENT_BUF_SIZE` | `1024` | WebSocket client send/receive buffer size in bytes (bounds one frame). |
| `PROTOCORE_WS_CLIENT_CT_BUF_SIZE` | `4096` | Ciphertext receive-ring size for wss:// (draining ring; must exceed one TCP_MSS). |
| `PROTOCORE_WS_FRAG_SIZE` | `0` | WebSocket outbound fragmentation size (RFC 6455 sec 5.4), in payload bytes. |
| `PROTOCORE_ZIGBEE_MAX_DATA` | `128` | Max ASH payload bytes (an EZSP frame; the ASH data field caps near 128). |
| `PROTOCORE_ZWAVE_MAX_DATA` | `64` | Reject a Z-Wave frame whose declared length exceeds this data cap (sanity). |
| `QUERY_KEY_LEN` | `24` | Maximum query-parameter key length. |
| `QUERY_VAL_LEN` | `48` | Maximum query-parameter value length. |
| `RESP_HDR_BUF_SIZE` | `768` | Stack buffer for HTTP response header lines in send() / send_empty() / send_unauth() / serve_file(). |
| `RE_MAX_STEPS` | `2000` | Step budget for the regex route matcher (see on_regex()). |
| `RX_BUF_SIZE` | `1024` | Ring-buffer capacity in bytes per connection slot (feature floors enforced last, in derived_sizing.h - a value below what an enabled feature needs is raised there). |
| `SNMP_COMMUNITY_MAX` | `32` | Maximum SNMP community-string length (including null terminator). |
| `SNMP_MAX_MIB_ENTRIES` | `16` | Maximum registered MIB objects (the agent's fixed OID table). |
| `SNMP_MAX_OID_LEN` | `32` | Maximum sub-identifiers (arcs) in an SNMP object identifier. |
| `SNMP_MAX_VARBINDS` | `16` | Maximum variable bindings the agent will emit in one response. |
| `SNMP_MSG_BUF_SIZE` | `1472` | Static request/response datagram buffers for the SNMP UDP agent. |
| `SNMP_V3_ENGINEID_MAX` | `32` | Maximum SNMPv3 authoritative engine-ID length in bytes (RFC 3411 allows 5..32). |
| `SNMP_V3_USER_MAX` | `32` | Maximum SNMPv3 USM user-name length (including null terminator). |
| `SSE_BUF_SIZE` | `256` | Output buffer size in bytes for a single SSE event. |
| `SSH_AUTH_ALGO_MAX` | `20` | Max stored public-key algorithm name ("rsa-sha2-512", "ecdsa-sha2-nistp256", RFC 4253 sec 6.6). |
| `SSH_AUTH_PASS_MAX` | `64` | Max stored password length. |
| `SSH_AUTH_TIMEOUT_MS` | `600000u` | How long a connection may stay unauthenticated before it is disconnected, milliseconds. |
| `SSH_AUTH_USER_MAX` | `32` | Max stored user name (RFC 4252 imposes no limit; we cap for BSS). |
| `SSH_CHAN_WINDOW` | `32768u` | Initial receive window the SSH server advertises (RFC 4254 §5.1). |
| `SSH_KEXINIT_MAX` | `2048` | Max stored size of the CLIENT KEXINIT payload (I_C, for the exchange hash). |
| `SSH_MAX_AUTH_ATTEMPTS` | `6` | Maximum failed SSH authentication attempts per connection. |
| `SSH_MAX_PASSWORD_LEN` | `64` | Maximum SSH password length including null terminator. |
| `SSH_MAX_USERNAME_LEN` | `32` | Maximum SSH username length including null terminator. |
| `SSH_PKT_BUF_SIZE` | `2048` | Packet assembly buffer per SSH connection (bytes). |
| `SSH_REKEY_TIME_MS` | `3600000u` | Elapsed-time re-key trigger in milliseconds (RFC 4253 §9: "after each hour"). |
| `TELNET_BUF_SIZE` | `256` | Stack buffer for one Telnet I/O chunk. |
| `TERM_TX_BUF_SIZE` | `256` | Stack scratch for protocore_web_terminal_println() line building. |
| `WS_FRAME_SIZE` | `512` | Maximum WebSocket frame payload in bytes. |
| `WS_HDR_BUF_SIZE` | `256` | Stack buffer for the HTTP 101 Switching Protocols response sent during the WebSocket handshake. |

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

If you view the offline version of this documentation, you can access the latest online version at the [GitHub Pages documentation site](https://dstroy0.github.io/ProtoCore/).

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
