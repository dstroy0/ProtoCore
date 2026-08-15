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
├── crypto/
│   ├── aead/
│   │   ├── aes128gcm.h
│   │   ├── aesccm.c
│   │   ├── aesccm.h
│   │   ├── aesgcm.c
│   │   ├── aesgcm.h
│   │   ├── chachapoly.c
│   │   └── chachapoly.h
│   ├── asymmetric/
│   │   ├── bignum.c
│   │   ├── bignum.h
│   │   ├── curve25519.c
│   │   ├── curve25519.h
│   │   ├── ecdsa.c
│   │   ├── ecdsa.h
│   │   ├── ed25519.c
│   │   ├── ed25519.h
│   │   ├── ed25519_comb_table.h
│   │   ├── fe25519.h
│   │   ├── rsa.c
│   │   └── rsa.h
│   ├── cipher/
│   │   ├── aes256ctr.c
│   │   ├── aes256ctr.h
│   │   ├── aes_block.h
│   │   ├── aes_sbox.h
│   │   ├── chacha20.c
│   │   └── chacha20.h
│   ├── hash/
│   │   ├── md.c
│   │   ├── md.h
│   │   ├── sha1.c
│   │   ├── sha1.h
│   │   ├── sha256.c
│   │   ├── sha256.h
│   │   ├── sha3.c
│   │   ├── sha3.h
│   │   ├── sha512.c
│   │   └── sha512.h
│   ├── kdf/
│   │   ├── hkdf.c
│   │   ├── hkdf.h
│   │   ├── kdf.c
│   │   └── kdf.h
│   ├── mac/
│   │   ├── aes_cmac.c
│   │   ├── aes_cmac.h
│   │   ├── ghash.h
│   │   ├── hmac_sha256.c
│   │   ├── hmac_sha256.h
│   │   ├── hmac_sha512.c
│   │   ├── hmac_sha512.h
│   │   ├── poly1305.c
│   │   └── poly1305.h
│   ├── pqc/
│   │   ├── mlkem.c
│   │   ├── mlkem.h
│   │   ├── sntrup761.c
│   │   └── sntrup761.h
│   ├── rng/  (rng.h, rng.c)
│   ├── crypto_opt.h
│   └── ct_eq.h
├── mmgr/
│   ├── arena.c
│   ├── arena.h
│   ├── bitio.c
│   ├── bitio.h
│   ├── bytes.c
│   ├── bytes.h
│   ├── dma.c
│   ├── dma.h
│   ├── endian.c
│   ├── endian.h
│   ├── float_bits.c
│   ├── float_bits.h
│   ├── membuild.c
│   ├── membuild.h
│   ├── plaintext.c
│   ├── plaintext.h
│   ├── protoframe.c
│   ├── protoframe.h
│   ├── protomem.c
│   ├── protomem.h
│   ├── protostr.c
│   ├── protostr.h
│   ├── psram_pool.c
│   ├── psram_pool.h
│   ├── rawmemcpy.c
│   ├── rawmemcpy.h
│   ├── ring.h
│   ├── secure.c
│   ├── secure.h
│   ├── span.c
│   ├── span.h
│   ├── swar.c
│   └── swar.h
├── network_drivers/
│   ├── application/
│   │   ├── file_serving/  (file_serving.h, file_serving.c)
│   │   ├── mdns_adaptive/  (mdns_adaptive.h, mdns_adaptive.c)
│   │   ├── mdns_service/  (mdns_service.h, mdns_service.c)
│   │   ├── ntp/
│   │   │   └── ntp.h
│   │   ├── ntp_server/  (ntp_server.h, ntp_server.c)
│   │   ├── ntp_service/  (ntp_service.h, ntp_service.c)
│   │   ├── nts/  (nts.h, nts.c)
│   │   ├── ptp/  (ptp.h, ptp.c)
│   │   ├── scp/
│   │   │   ├── scp.c
│   │   │   ├── scp.h
│   │   │   ├── ssh_scp.c
│   │   │   └── ssh_scp.h
│   │   ├── sftp/
│   │   │   ├── sftp.c
│   │   │   ├── sftp.h
│   │   │   ├── ssh_sftp.c
│   │   │   └── ssh_sftp.h
│   │   ├── smb/
│   │   │   ├── ntlm.c
│   │   │   ├── ntlm.h
│   │   │   ├── ntlmssp.c
│   │   │   ├── ntlmssp.h
│   │   │   ├── smb2.c
│   │   │   ├── smb2.h
│   │   │   ├── smb_client.c
│   │   │   ├── smb_client.h
│   │   │   ├── spnego.c
│   │   │   └── spnego.h
│   │   ├── upload_service/  (upload_service.h, upload_service.c)
│   │   ├── webdav/  (webdav.h, webdav.c)
│   │   ├── binary_asset_blobs.c
│   │   ├── binary_asset_blobs.h
│   │   ├── http_range.c
│   │   ├── http_range.h
│   │   ├── web_assets.c
│   │   └── web_assets.h
│   ├── datalink/
│   │   ├── datalink.c
│   │   ├── datalink.h
│   │   ├── roaming.c
│   │   └── roaming.h
│   ├── network/
│   │   ├── dns/
│   │   │   ├── dns.c
│   │   │   ├── dns.h
│   │   │   ├── dns_resolver.c
│   │   │   ├── dns_resolver.h
│   │   │   ├── dns_server.c
│   │   │   ├── dns_server.h
│   │   │   ├── dns_wire.c
│   │   │   └── dns_wire.h
│   │   ├── forward/  (forward.h, forward.c)
│   │   ├── network.c
│   │   └── network.h
│   ├── physical/
│   │   ├── physical.c
│   │   ├── physical.h
│   │   ├── radio_power.c
│   │   └── radio_power.h
│   ├── presentation/
│   │   ├── codec/
│   │   │   ├── base64/  (base64.h, base64.c)
│   │   │   ├── cbor/  (cbor.h, cbor.c)
│   │   │   ├── deflate/
│   │   │   │   ├── deflate.c
│   │   │   │   ├── deflate.h
│   │   │   │   ├── rfc1951.c
│   │   │   │   └── rfc1951.h
│   │   │   ├── hpack_prim/  (hpack_prim.h, hpack_prim.c)
│   │   │   ├── inflate/  (inflate.h, inflate.c)
│   │   │   ├── json/  (json.h, json.c)
│   │   │   ├── msgpack/  (msgpack.h, msgpack.c)
│   │   │   ├── multipart/  (multipart.h, multipart.c)
│   │   │   └── codec.h
│   │   ├── http/
│   │   │   ├── auth/  (auth.h, auth.c)
│   │   │   ├── http2/
│   │   │   │   ├── h2_conn.c
│   │   │   │   ├── h2_conn.h
│   │   │   │   ├── h2_frame.c
│   │   │   │   ├── h2_frame.h
│   │   │   │   ├── h2_server.c
│   │   │   │   ├── h2_server.h
│   │   │   │   ├── hpack.c
│   │   │   │   └── hpack.h
│   │   │   ├── http3/
│   │   │   │   ├── h3_conn.c
│   │   │   │   ├── h3_conn.h
│   │   │   │   ├── h3_frame.c
│   │   │   │   ├── h3_frame.h
│   │   │   │   ├── h3_server.c
│   │   │   │   ├── h3_server.h
│   │   │   │   ├── qpack.c
│   │   │   │   ├── qpack.h
│   │   │   │   ├── quic_conn.c
│   │   │   │   ├── quic_conn.h
│   │   │   │   ├── quic_crypto.c
│   │   │   │   ├── quic_crypto.h
│   │   │   │   ├── quic_frame.c
│   │   │   │   ├── quic_frame.h
│   │   │   │   ├── quic_packet.c
│   │   │   │   ├── quic_packet.h
│   │   │   │   ├── quic_server.c
│   │   │   │   ├── quic_server.h
│   │   │   │   ├── quic_tls.c
│   │   │   │   ├── quic_tls.h
│   │   │   │   ├── quic_tp.c
│   │   │   │   ├── quic_tp.h
│   │   │   │   ├── quic_varint.c
│   │   │   │   ├── quic_varint.h
│   │   │   │   ├── tls13_msg.c
│   │   │   │   └── tls13_msg.h
│   │   │   ├── http_parser/  (http_parser.h, http_parser.c)
│   │   │   ├── httpcache/  (httpcache.h, httpcache.c)
│   │   │   ├── route/  (http_route.h, http_route.c)
│   │   │   ├── sse/  (sse.h, sse.c)
│   │   │   ├── websocket/  (websocket.h, websocket.c)
│   │   │   ├── http.c
│   │   │   └── http.h
│   │   ├── security/
│   │   │   └── dtls/
│   │   │       ├── dtls_conn.c
│   │   │       ├── dtls_conn.h
│   │   │       ├── dtls_handshake.c
│   │   │       ├── dtls_handshake.h
│   │   │       ├── dtls_record.c
│   │   │       └── dtls_record.h
│   │   ├── ssh/
│   │   │   ├── app/
│   │   │   │   ├── client.c
│   │   │   │   ├── client.h
│   │   │   │   ├── server.c
│   │   │   │   └── server.h
│   │   │   ├── auth/  (auth.h, auth.c)
│   │   │   ├── client/  (client.h, client.c)
│   │   │   ├── connection/  (connection.h, connection.c)
│   │   │   ├── network/  (network.h, network.c)
│   │   │   ├── server/  (server.h, server.c)
│   │   │   ├── transport/
│   │   │   │   ├── comp.c
│   │   │   │   ├── comp.h
│   │   │   │   ├── extension.c
│   │   │   │   ├── extension.h
│   │   │   │   ├── inflate.c
│   │   │   │   ├── inflate.h
│   │   │   │   ├── phase_machine.c
│   │   │   │   ├── phase_machine.h
│   │   │   │   ├── ssh_kexhash.h
│   │   │   │   ├── ssh_rsa.c
│   │   │   │   ├── ssh_rsa.h
│   │   │   │   ├── transport.c
│   │   │   │   ├── transport.h
│   │   │   │   ├── zlib.c
│   │   │   │   └── zlib.h
│   │   │   ├── common.h
│   │   │   ├── ssh.c
│   │   │   └── ssh.h
│   │   ├── telnet/  (telnet.h, telnet.c)
│   │   ├── presentation.c
│   │   └── presentation.h
│   ├── session/  (session.h, session.c)
│   ├── tls/
│   │   ├── handshake/  (handshake.h, handshake.c)
│   │   ├── key_schedule/  (key_schedule.h, key_schedule.c)
│   │   ├── record/  (record.h, record.c)
│   │   ├── tls.c
│   │   └── tls.h
│   └── transport/
│       ├── diffserv/  (diffserv.h, diffserv.c)
│       ├── happy_eyeballs/  (happy_eyeballs.h, happy_eyeballs.c)
│       ├── net_addr/  (net_addr.h, net_addr.c)
│       ├── proxy_protocol/  (proxy_protocol.h, proxy_protocol.c)
│       ├── tcp/
│       │   ├── client/  (client.h, client.c)
│       │   ├── lower/  (lower.h, lower.c)
│       │   ├── protocol/  (protocol.h, protocol.c)
│       │   ├── server/  (server.h, server.c)
│       │   ├── common.h
│       │   ├── evt.h
│       │   ├── tcp.c
│       │   └── tcp.h
│       └── udp/
│           ├── client/  (client.h, client.c)
│           ├── server/  (server.h, server.c)
│           ├── common.h
│           ├── udp.c
│           └── udp.h
├── server/
│   ├── clock/  (clock.h, clock.c)
│   ├── core/
│   │   ├── guardrails/  (guardrails.h, guardrails.c)
│   │   ├── provisioning_service/  (provisioning_service.h, provisioning_service.c)
│   │   ├── exc_coredump.c
│   │   ├── exc_decoder.c
│   │   ├── exc_decoder.h
│   │   ├── failsafe.c
│   │   ├── failsafe.h
│   │   ├── logbuf.c
│   │   ├── logbuf.h
│   │   ├── power_mgmt.c
│   │   ├── power_mgmt.h
│   │   ├── preempt_queue.c
│   │   ├── preempt_queue.h
│   │   ├── proto_handler.h
│   │   ├── sleep_sched.c
│   │   ├── sleep_sched.h
│   │   ├── worker.c
│   │   └── worker.h
│   ├── io/
│   │   ├── middleware.c
│   │   ├── response.c
│   │   ├── webdav_handler.c
│   │   ├── webdav_handler.h
│   │   └── websocket_sse.c
│   ├── net/
│   │   ├── gateway/  (gateway.h, gateway.c)
│   │   ├── iface_bridge/
│   │   │   ├── iface_bridge.c
│   │   │   ├── iface_bridge.h
│   │   │   ├── iface_bridge_hw.c
│   │   │   └── iface_bridge_hw.h
│   │   ├── netadapt/  (netadapt.h, netadapt.c)
│   │   ├── relay/
│   │   │   ├── relay.c
│   │   │   ├── relay.h
│   │   │   ├── relay_listener.c
│   │   │   └── relay_listener.h
│   │   └── sockpool/  (sockpool.h, sockpool.c)
│   ├── peripherals/
│   │   ├── ad9238/  (ad9238.h, ad9238.c)
│   │   ├── ads1115/  (ads1115.h, ads1115.c)
│   │   ├── dmx/  (dmx.h, dmx.c)
│   │   ├── dshot/  (dshot.h, dshot.c)
│   │   ├── fdc2214/  (fdc2214.h, fdc2214.c)
│   │   ├── hmmd/  (hmmd.h, hmmd.c)
│   │   ├── ina219/  (ina219.h, ina219.c)
│   │   ├── ld2410/  (ld2410.h, ld2410.c)
│   │   ├── ldc1614/  (ldc1614.h, ldc1614.c)
│   │   ├── mpr121/  (mpr121.h, mpr121.c)
│   │   ├── pca9685/  (pca9685.h, pca9685.c)
│   │   ├── pn532/  (pn532.h, pn532.c)
│   │   ├── rcwl0516/  (rcwl0516.h, rcwl0516.c)
│   │   ├── rtc/  (rtc.h, rtc.c)
│   │   ├── sdi12/  (sdi12.h, sdi12.c)
│   │   ├── sen0192/  (sen0192.h, sen0192.c)
│   │   ├── sht3x/  (sht3x.h, sht3x.c)
│   │   ├── vl53l0x/  (vl53l0x.h, vl53l0x.c)
│   │   ├── i2c.h
│   │   ├── pmbus.c
│   │   ├── pmbus.h
│   │   ├── smbus.c
│   │   ├── smbus.h
│   │   ├── spi.h
│   │   └── uart.h
│   ├── security/
│   │   ├── audit_log/  (audit_log.h, audit_log.c)
│   │   ├── auth_lockout/  (auth_lockout.h, auth_lockout.c)
│   │   ├── csrf/  (csrf.h, csrf.c)
│   │   ├── forwarded_trust/  (forwarded_trust.h, forwarded_trust.c)
│   │   └── tls_policy/  (tls_policy.h, tls_policy.c)
│   ├── signaling/
│   │   ├── bus_capture.c
│   │   ├── bus_capture.h
│   │   ├── device_id.c
│   │   ├── device_id.h
│   │   ├── gpio_map.c
│   │   ├── gpio_map.h
│   │   ├── gpio_map_routes.c
│   │   ├── hw_health.c
│   │   ├── hw_health.h
│   │   ├── link_manager.c
│   │   ├── link_manager.h
│   │   ├── signaling.c
│   │   ├── signaling.h
│   │   ├── trace_capture.c
│   │   └── trace_capture.h
│   ├── storage/
│   │   ├── config_io/  (config_io.h, config_io.c)
│   │   ├── config_store/  (config_store.h, config_store.c)
│   │   ├── hotswap/  (hotswap.h, hotswap.c)
│   │   ├── partition_monitor/
│   │   │   ├── partition_monitor.c
│   │   │   ├── partition_monitor.h
│   │   │   └── partition_monitor_routes.c
│   │   ├── filesystem.c
│   │   ├── filesystem.h
│   │   ├── mnt.c
│   │   ├── mnt.h
│   │   ├── wearlevel.c
│   │   └── wearlevel.h
│   ├── update/
│   │   ├── ota_rollback.c
│   │   ├── ota_rollback.h
│   │   ├── ota_service.c
│   │   └── ota_service.h
│   ├── web/
│   │   ├── dashboard/
│   │   │   ├── dashboard.c
│   │   │   ├── dashboard.h
│   │   │   └── dashboard_routes.c
│   │   ├── edge_cache/
│   │   │   ├── edge_cache.c
│   │   │   ├── edge_cache.h
│   │   │   ├── edge_cache_proxy.c
│   │   │   ├── edge_cache_proxy.h
│   │   │   ├── edge_cache_sd.c
│   │   │   ├── edge_cache_sd.h
│   │   │   ├── edge_fetch.c
│   │   │   ├── edge_fetch.h
│   │   │   ├── edge_mesh.c
│   │   │   └── edge_mesh.h
│   │   ├── spa_router/  (spa_router.h, spa_router.c)
│   │   └── web_terminal/  (web_terminal.h, web_terminal.c)
│   ├── protocore_builtins.c
│   └── regex.c
├── services/
│   ├── energy/
│   │   ├── c37118/  (c37118.h, c37118.c)
│   │   ├── dnp3/  (dnp3.h, dnp3.c)
│   │   ├── goose/  (goose.h, goose.c)
│   │   ├── iccp/  (iccp.h, iccp.c)
│   │   ├── iec60870/  (iec60870.h, iec60870.c)
│   │   ├── mms/  (mms.h, mms.c)
│   │   ├── openadr/  (openadr.h, openadr.c)
│   │   ├── sep2/  (sep2.h, sep2.c)
│   │   └── sunspec/  (sunspec.h, sunspec.c)
│   ├── fieldbus/
│   │   ├── ads/  (ads.h, ads.c)
│   │   ├── bacnet/  (bacnet.h, bacnet.c)
│   │   ├── canopen/  (canopen.h, canopen.c)
│   │   ├── cclink/  (cclink.h, cclink.c)
│   │   ├── cia402/  (cia402.h, cia402.c)
│   │   ├── cip/  (cip.h, cip.c)
│   │   ├── cotp/  (cotp.h, cotp.c)
│   │   ├── devicenet/  (devicenet.h, devicenet.c)
│   │   ├── df1/  (df1.h, df1.c)
│   │   ├── directnet/  (directnet.h, directnet.c)
│   │   ├── enip/  (enip.h, enip.c)
│   │   ├── fins/  (fins.h, fins.c)
│   │   ├── hart/  (hart.h, hart.c)
│   │   ├── hostlink/  (hostlink.h, hostlink.c)
│   │   ├── interbus/  (interbus.h, interbus.c)
│   │   ├── iolink/  (iolink.h, iolink.c)
│   │   ├── j1939/  (j1939.h, j1939.c)
│   │   ├── lonworks/  (lonworks.h, lonworks.c)
│   │   ├── mbplus/  (mbplus.h, mbplus.c)
│   │   ├── mbus/  (mbus.h, mbus.c)
│   │   ├── melsec/  (melsec.h, melsec.c)
│   │   ├── modbus/
│   │   │   ├── modbus.c
│   │   │   ├── modbus.h
│   │   │   ├── modbus_master.c
│   │   │   └── modbus_master.h
│   │   ├── opcua/  (opcua.h, opcua.c)
│   │   ├── opcua_client/  (opcua_client.h, opcua_client.c)
│   │   ├── powerlink/  (powerlink.h, powerlink.c)
│   │   ├── profibus/  (profibus.h, profibus.c)
│   │   ├── profinet/  (profinet.h, profinet.c)
│   │   ├── rawl2/  (rawl2.h, rawl2.c)
│   │   ├── s7comm/  (s7comm.h, s7comm.c)
│   │   ├── sercos/  (sercos.h, sercos.c)
│   │   ├── simatic/  (simatic.h, simatic.c)
│   │   └── snp/  (snp.h, snp.c)
│   ├── file_transfer/
│   │   ├── ftp/
│   │   │   ├── ftp.c
│   │   │   ├── ftp.h
│   │   │   ├── ftp_session.c
│   │   │   └── ftp_session.h
│   │   └── http_delivery/
│   │       ├── http_delivery.c
│   │       ├── http_delivery.h
│   │       └── http_delivery_routes.c
│   ├── instrumentation/
│   │   ├── gpib/  (gpib.h, gpib.c)
│   │   ├── hislip/  (hislip.h, hislip.c)
│   │   ├── scpi/  (scpi.h, scpi.c)
│   │   └── vxi11/  (vxi11.h, vxi11.c)
│   ├── iot/
│   │   ├── amqp/  (amqp.h, amqp.c)
│   │   ├── cloudevents/  (cloudevents.h, cloudevents.c)
│   │   ├── coap/
│   │   │   ├── coap.c
│   │   │   ├── coap.h
│   │   │   ├── coaps.c
│   │   │   ├── coaps.h
│   │   │   ├── coaps_server.c
│   │   │   └── coaps_server.h
│   │   ├── dds/  (dds.h, dds.c)
│   │   ├── graphql/  (graphql.h, graphql.c)
│   │   ├── grpcweb/  (grpcweb.h, grpcweb.c)
│   │   ├── lwm2m/  (lwm2m_tlv.h, lwm2m_tlv.c)
│   │   ├── mqtt/
│   │   │   ├── mqtt.c
│   │   │   ├── mqtt.h
│   │   │   ├── mqtt_sn.c
│   │   │   └── mqtt_sn.h
│   │   ├── nats/  (nats.h, nats.c)
│   │   ├── protobuf/  (protobuf.h, protobuf.c)
│   │   ├── redis_resp/  (redis_resp.h, redis_resp.c)
│   │   ├── senml/  (senml.h, senml.c)
│   │   ├── sparkplug/  (sparkplug.h, sparkplug.c)
│   │   ├── statsd/  (statsd.h, statsd.c)
│   │   ├── stomp/  (stomp.h, stomp.c)
│   │   ├── telemetry/  (telemetry.h, telemetry.c)
│   │   ├── udp_telemetry/  (udp_telemetry.h, udp_telemetry.c)
│   │   ├── wamp/  (wamp.h, wamp.c)
│   │   └── xmpp/  (xmpp.h, xmpp.c)
│   ├── machine_tool/
│   │   ├── atc/  (atc.h, atc.c)
│   │   ├── dnc/
│   │   │   ├── dnc.c
│   │   │   ├── dnc.h
│   │   │   ├── dnc_stream.c
│   │   │   └── dnc_stream.h
│   │   ├── euromap77/  (euromap77.h, euromap77.c)
│   │   ├── fanuc_j519/  (fanuc_j519.h, fanuc_j519.c)
│   │   ├── focas/  (focas.h, focas.c)
│   │   ├── haas_mdc/  (haas_mdc.h, haas_mdc.c)
│   │   ├── lsv2/  (lsv2.h, lsv2.c)
│   │   ├── mtconnect/  (mtconnect.h, mtconnect.c)
│   │   ├── packml/  (packml.h, packml.c)
│   │   ├── robotics/  (robotics.h, robotics.c)
│   │   ├── safety_scl/  (safety_scl.h, safety_scl.c)
│   │   └── umati/  (umati.h, umati.c)
│   ├── net/
│   │   ├── flow_export/  (flow_export.h, flow_export.c)
│   │   ├── http_client/  (http_client.h, http_client.c)
│   │   ├── smtp/  (smtp.h, smtp.c)
│   │   ├── snmp/
│   │   │   ├── snmp_agent.c
│   │   │   ├── snmp_agent.h
│   │   │   ├── snmp_ber.c
│   │   │   ├── snmp_ber.h
│   │   │   ├── snmp_crypto.c
│   │   │   ├── snmp_crypto.h
│   │   │   ├── snmp_notify.c
│   │   │   ├── snmp_notify.h
│   │   │   ├── snmp_v3.c
│   │   │   └── snmp_v3.h
│   │   ├── syslog/  (syslog.h, syslog.c)
│   │   ├── webhook/  (webhook.h, webhook.c)
│   │   └── ws_client/  (ws_client.h, ws_client.c)
│   ├── radio/
│   │   ├── ble_gatt/  (ble_gatt.h, ble_gatt.c)
│   │   ├── cc1101/  (cc1101.h, cc1101.c)
│   │   ├── enocean/  (enocean.h, enocean.c)
│   │   ├── espnow/  (espnow.h, espnow.c)
│   │   ├── lora/  (lora.h, lora.c)
│   │   ├── nrf24/  (nrf24.h, nrf24.c)
│   │   ├── promisc/  (promisc.h, promisc.c)
│   │   ├── radio_sniff/  (radio_sniff.h, radio_sniff.c)
│   │   ├── sigfox/  (sigfox.h, sigfox.c)
│   │   ├── thread/  (thread.h, thread.c)
│   │   ├── wifi_sniffer/  (wifi_sniffer.h, wifi_sniffer.c)
│   │   ├── wisun/  (wisun.h, wisun.c)
│   │   ├── zigbee/  (zigbee.h, zigbee.c)
│   │   └── zwave/  (zwave.h, zwave.c)
│   ├── security/
│   │   ├── ikev2/
│   │   │   ├── ikev2.c
│   │   │   ├── ikev2.h
│   │   │   ├── ikev2_natt.c
│   │   │   └── ikev2_natt.h
│   │   ├── jwt/  (jwt.h, jwt.c)
│   │   ├── oauth2/  (oauth2.h, oauth2.c)
│   │   ├── oidc/  (oidc.h, oidc.c)
│   │   └── totp/  (totp.h, totp.c)
│   ├── southbound/
│   │   ├── sb_modbus.c
│   │   ├── sb_modbus.h
│   │   ├── southbound.c
│   │   └── southbound.h
│   ├── storage/
│   │   ├── dbm/  (dbm.h, dbm.c)
│   │   ├── docstore/  (docstore.h, docstore.c)
│   │   ├── sqlite/  (sqlite_format.h, sqlite_format.c)
│   │   └── wal/
│   │       ├── wal.c
│   │       ├── wal.h
│   │       ├── wal_fs.h
│   │       ├── wal_store.c
│   │       └── wal_store.h
│   ├── system/
│   │   ├── control/  (control.h, control.c)
│   │   └── esp/
│   │       ├── esp.c
│   │       ├── esp.h
│   │       ├── ipsec_db.c
│   │       └── ipsec_db.h
│   ├── timing_position/
│   │   ├── gnss/
│   │   │   ├── gnss_survey.c
│   │   │   ├── gnss_survey.h
│   │   │   ├── ntrip_caster.c
│   │   │   ├── ntrip_caster.h
│   │   │   ├── ntrip_caster_listener.c
│   │   │   ├── ntrip_caster_listener.h
│   │   │   ├── rtcm3.c
│   │   │   └── rtcm3.h
│   │   ├── nmea0183/  (nmea0183.h, nmea0183.c)
│   │   ├── nmea2000/  (nmea2000.h, nmea2000.c)
│   │   ├── time_source/  (time_source.h, time_source.c)
│   │   └── ubx/  (ubx.h, ubx.c)
│   └── transportation/
│       ├── j2735/  (j2735.h, j2735.c)
│       ├── nema_ts2/  (nema_ts2.h, nema_ts2.c)
│       ├── ntcip/  (ntcip.h, ntcip.c)
│       ├── ocit/  (ocit.h, ocit.c)
│       ├── utmc/  (utmc.h, utmc.c)
│       └── wave/  (wave.h, wave.c)
├── shared/
│   ├── can/
│   │   └── can.h
│   ├── crc/  (crc.h, crc.c)
│   ├── hex/  (hex.h, hex.c)
│   ├── http_date/  (http_date.h, http_date.c)
│   ├── ip/  (ip.h, ip.c)
│   ├── log/  (log.h, log.c)
│   ├── mime/
│   │   └── mime.h
│   ├── pcap/  (pcap.h, pcap.c)
│   ├── speed_opt/
│   │   └── speed_opt.h
│   ├── time_compat/  (time_compat.h, time_compat.c)
│   └── utf8/  (utf8.h, utf8.c)
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
├── protocore.c
├── protocore_config.h
└── protocore_types.h
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

The only post-`proto_begin()` allocation that can occur is inside the Arduino `fs::File` construction in the mount backend `serve_file()` reaches (`core_setup/hal/esp/esp_mnt_fs.cpp`), which is an Arduino FS implementation detail outside the library's control.

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
| `PROTOCORE_ENABLE_ACCEPT_THROTTLE` | `0` | Opt-in global accept-rate throttle (connection-flood defense). |
| `PROTOCORE_ENABLE_AD9238` | `0` | Enable the AD9238 SPI configuration-port codec (default off). |
| `PROTOCORE_ENABLE_ADS` | `0` | Beckhoff ADS / AMS protocol codec (`services/ads`). |
| `PROTOCORE_ENABLE_ADS1115` | `0` | TI ADS1115 16-bit ADC (I2C) - a precise external analog input. |
| `PROTOCORE_ENABLE_AMQP` | `0` | AMQP 0-9-1 frame codec (`services/amqp`). |
| `PROTOCORE_ENABLE_ATC` | `0` | Opt-in ATC (Advanced Traffic Controller) field-I/O interop snapshot. |
| `PROTOCORE_ENABLE_AUDIT_LOG` | `0` | Tamper-evident audit log. |
| `PROTOCORE_ENABLE_AUTH` | `0` | HTTP Basic Authentication per-route. |
| `PROTOCORE_ENABLE_AUTH_LOCKOUT` | `0` | Opt-in per-IP brute-force lockout for HTTP auth (requires PROTOCORE_ENABLE_AUTH). |
| `PROTOCORE_ENABLE_BACNET` | `0` | BACnet/IP BVLC + NPDU codec (`services/bacnet`). |
| `PROTOCORE_ENABLE_BLE_GATT` | `0` | Opt-in Bluetooth ATT protocol codec + GATT characteristic bridge. |
| `PROTOCORE_ENABLE_BUS_CAPTURE` | `0` | Wired field-bus listen-only capture. |
| `PROTOCORE_ENABLE_C37118` | `0` | IEEE C37.118.2 synchrophasor frame codec (`services/c37118`). |
| `PROTOCORE_ENABLE_CANOPEN` | `0` | CANopen (CiA 301) message codec (`services/canopen`). |
| `PROTOCORE_ENABLE_CBOR` | `0` | Zero-heap CBOR (RFC 8949) encoder for compact binary payloads. |
| `PROTOCORE_ENABLE_CC1101` | `0` | Opt-in CC1101 sub-GHz radio driver. |
| `PROTOCORE_ENABLE_CCLINK` | `0` | Opt-in CC-Link (CLPA) cyclic fieldbus frame codec. |
| `PROTOCORE_ENABLE_CIA402` | `0` | CiA 402 / IEC 61800-7-201 drive + motion profile (`services/cia402`). |
| `PROTOCORE_ENABLE_CIP` | `0` | CIP (Common Industrial Protocol) message codec (`services/cip`). |
| `PROTOCORE_ENABLE_CLOUDEVENTS` | `0` | CloudEvents v1.0 (CNCF) event envelope (structured JSON + binary headers). |
| `PROTOCORE_ENABLE_COAP` | `0` | CoAP server (RFC 7252) over UDP/5683. |
| `PROTOCORE_ENABLE_COAP_BLOCK` | `0` | CoAP block-wise transfer - RFC 7959 (requires PROTOCORE_ENABLE_COAP). |
| `PROTOCORE_ENABLE_COAP_OBSERVE` | `0` | CoAP resource observation - RFC 7641 (requires PROTOCORE_ENABLE_COAP). |
| `PROTOCORE_ENABLE_CONFIG_IO` | `0` | Opt-in schema-driven config export / restore. |
| `PROTOCORE_ENABLE_CONFIG_STORE` | `0` | Typed NVS configuration store (WiFi creds, IP config, ... |
| `PROTOCORE_ENABLE_CONTROL` | `0` | Closed-loop control law (`services/control`). |
| `PROTOCORE_ENABLE_COTP` | `0` | TPKT (RFC 1006) + COTP (X.224 class 0) frame codec (`services/cotp`). |
| `PROTOCORE_ENABLE_CSRF` | `0` | Opt-in CSRF protection for state-changing HTTP requests. |
| `PROTOCORE_ENABLE_DASHBOARD` | `0` | Real-time SVG dashboard (PROTOCORE_ENABLE_DASHBOARD; requires PROTOCORE_ENABLE_SSE). |
| `PROTOCORE_ENABLE_DBM` | `0` | Opt-in dbm: a log-structured hash key-value store on the WAL (PROTOCORE_ENABLE_DBM, requires WAL). |
| `PROTOCORE_ENABLE_DDS` | `0` | Opt-in DDS / RTPS wire-protocol codec. |
| `PROTOCORE_ENABLE_DEVICENET` | `0` | DeviceNet link-adaptation codec (`services/devicenet`). |
| `PROTOCORE_ENABLE_DEVICE_ID` | `0` | Stable device UUID derived from the chip MAC (RFC 4122 v5). |
| `PROTOCORE_ENABLE_DF1` | `0` | Allen-Bradley DF1 full-duplex frame codec (`services/df1`). |
| `PROTOCORE_ENABLE_DIAG` | `0` | Expose a diagnostic JSON endpoint via diag(). |
| `PROTOCORE_ENABLE_DIFFSERV` | `0` | Enable DiffServ QoS marking (RFC 2474) on outbound traffic. |
| `PROTOCORE_ENABLE_DIRECTNET` | `0` | Opt-in AutomationDirect / Koyo DirectNET serial frame codec. |
| `PROTOCORE_ENABLE_DMA` | `0` | Enable the DMA peripheral ingest / egress primitive (default off). |
| `PROTOCORE_ENABLE_DMX` | `0` | DMX512 + RDM (ANSI E1.20) lighting codec (`services/dmx`). |
| `PROTOCORE_ENABLE_DNC` | `0` | Opt-in CNC RS-232 DNC drip-feed codec. |
| `PROTOCORE_ENABLE_DNP3` | `0` | DNP3 (IEEE 1815) data-link frame codec (`services/dnp3`). |
| `PROTOCORE_ENABLE_DNS_RESOLVER` | `0` | Opt-in DNS resolver with answer verification. |
| `PROTOCORE_ENABLE_DNS_SERVER` | `0` | Authoritative DNS server (network_drivers/network/dns/dns_server) on UDP/53. |
| `PROTOCORE_ENABLE_DOCSTORE` | `0` | Opt-in local JSON document store on the WAL (PROTOCORE_ENABLE_DOCSTORE, requires DBM + WAL). |
| `PROTOCORE_ENABLE_DSHOT` | `0` | Opt-in DShot ESC throttle protocol codec. |
| `PROTOCORE_ENABLE_DTLS` | `0` | DTLS 1.3 datagram security (RFC 9147) - the record layer. |
| `PROTOCORE_ENABLE_EDGE_CACHE` | `0` | Opt-in CDN edge-cache tier (PROTOCORE_ENABLE_EDGE_CACHE, requires HTTP_CACHE). |
| `PROTOCORE_ENABLE_EDGE_MESH` | `0` | Opt-in mesh (sibling-cache) distribution for the edge cache. |
| `PROTOCORE_ENABLE_EDGE_ORIGIN_TLS` | `0` |  |
| `PROTOCORE_ENABLE_ENIP` | `0` | EtherNet/IP encapsulation codec (`services/enip`). |
| `PROTOCORE_ENABLE_ENOCEAN` | `0` | Enable the EnOcean ESP3 serial codec (default off). |
| `PROTOCORE_ENABLE_ESPNOW` | `0` | ESP-NOW peer messaging. |
| `PROTOCORE_ENABLE_ETAG` | `0` | Conditional GET (ETag + Last-Modified) for served files. |
| `PROTOCORE_ENABLE_ETHERNET` | `0` | Enable wired Ethernet bring-up (Physical.eth_init / Physical.eth_ready). |
| `PROTOCORE_ENABLE_EUROMAP77` | `0` | EUROMAP 77 (OPC 40077) - OPC UA for injection moulding machines (IMM <-> MES). |
| `PROTOCORE_ENABLE_EXC_DECODER` | `0` | Opt-in ESP32 panic / exception decoder for a live diagnostics panel. |
| `PROTOCORE_ENABLE_FAILSAFE` | `0` | Opt-in software watchdog: deadlock detection + fail-safe safe-state. |
| `PROTOCORE_ENABLE_FANUC_J519` | `0` | FANUC Stream Motion (option J519) UDP codec (`services/fanuc_j519`). |
| `PROTOCORE_ENABLE_FDC2214` | `0` | Opt-in FDC2114/2214 capacitance-to-digital field sensor. |
| `PROTOCORE_ENABLE_FILE_SERVING` | `0` | Static file serving via Arduino FS (LittleFS, SPIFFS, SD). |
| `PROTOCORE_ENABLE_FINS` | `0` | Omron FINS frame codec (`services/fins`). |
| `PROTOCORE_ENABLE_FLOW_EXPORT` | `0` | Flow-record export codec (`services/flow_export`). |
| `PROTOCORE_ENABLE_FOCAS` | `0` | FANUC FOCAS Ethernet protocol codec (`services/focas`). |
| `PROTOCORE_ENABLE_FORWARD` | `0` | Enable the interface forwarding plane (default off). |
| `PROTOCORE_ENABLE_FORWARDED_TRUST` | `0` | Believe a `Forwarded` / `X-Forwarded-For` client address only from a trusted upstream. |
| `PROTOCORE_ENABLE_FTP` | `0` | Opt-in FTP client wire codec. |
| `PROTOCORE_ENABLE_FTP_SESSION` | `0` | Opt-in FTP client session driver (PROTOCORE_ENABLE_FTP_SESSION, requires PROTOCORE_ENABLE_FTP). |
| `PROTOCORE_ENABLE_GATEWAY` | `0` | Enable the radio / wireless gateway bridge (default off). |
| `PROTOCORE_ENABLE_GOOSE` | `0` | Opt-in IEC 61850 GOOSE publisher codec. |
| `PROTOCORE_ENABLE_GPIB` | `0` | GPIB-over-LAN (Prologix-style) controller command codec (`services/gpib`). |
| `PROTOCORE_ENABLE_GPIO_MAP` | `0` | Opt-in browser GPIO pin-mapper / diagnostics endpoint. |
| `PROTOCORE_ENABLE_GRAPHQL` | `0` | GraphQL query subset. |
| `PROTOCORE_ENABLE_GRPC_WEB` | `0` | gRPC-Web message framing (`services/grpcweb`). |
| `PROTOCORE_ENABLE_GUARDRAILS` | `0` | Opt-in runtime heap/stack guardrails. |
| `PROTOCORE_ENABLE_HAAS_MDC` | `0` | Haas Machine Data Collection (MDC) Q-command codec (`services/haas_mdc`). |
| `PROTOCORE_ENABLE_HAPPY_EYEBALLS` | `0` | Opt-in dual-stack Happy Eyeballs destination selection. |
| `PROTOCORE_ENABLE_HART` | `0` | Opt-in HART / HART-IP process-instrument protocol codec. |
| `PROTOCORE_ENABLE_HISLIP` | `0` | HiSLIP (High-Speed LAN Instrument Protocol) message codec (`services/hislip`). |
| `PROTOCORE_ENABLE_HMMD` | `0` | Waveshare HMMD 24 GHz mmWave micro-motion radar codec (`services/hmmd`). |
| `PROTOCORE_ENABLE_HOSTLINK` | `0` | Omron Host Link (C-mode) frame codec (`services/hostlink`). |
| `PROTOCORE_ENABLE_HOTSWAP` | `0` | Opt-in removable-storage hot-swap safeties. |
| `PROTOCORE_ENABLE_HTTP2` | `0` | HTTP/2 (RFC 9113) over the version-agnostic request/response core. |
| `PROTOCORE_ENABLE_HTTP3` | `0` | HTTP/3 (RFC 9114) over QUIC (RFC 9000) - implemented, host-tested end-to-end (HW verification pending). |
| `PROTOCORE_ENABLE_HTTP_CACHE` | `0` | Opt-in HTTP Cache-Control directive helpers. |
| `PROTOCORE_ENABLE_HTTP_CLIENT` | `0` | Outbound HTTP(S) client (raw lwIP, optional client-side mbedTLS). |
| `PROTOCORE_ENABLE_HTTP_CLIENT_TLS` | `0` | HTTPS client support inside the HTTP client (needs PROTOCORE_ENABLE_TLS). |
| `PROTOCORE_ENABLE_HTTP_DELIVERY` | `0` | Opt-in HTTP delivery optimizations. |
| `PROTOCORE_ENABLE_HW_HEALTH` | `0` | Opt-in hardware-health diagnostics. |
| `PROTOCORE_ENABLE_ICCP` | `0` | Opt-in ICCP / TASE.2 (IEC 60870-6) inter-control-center telemetry codec. |
| `PROTOCORE_ENABLE_IEC60870` | `0` | IEC 60870-5-101 / -104 telecontrol (SCADA) codec (`services/iec60870`). |
| `PROTOCORE_ENABLE_IFACE_BRIDGE` | `0` | User-defined address:port -> hardware-bus bridge (server/net/iface_bridge). |
| `PROTOCORE_ENABLE_IKEV2` | `0` | IKEv2 (RFC 7296) message + payload codec (`services/ikev2`). |
| `PROTOCORE_ENABLE_INA219` | `0` | TI INA219 high-side current / power monitor (I2C). |
| `PROTOCORE_ENABLE_INTERBUS` | `0` | Opt-in INTERBUS summation-frame fieldbus codec. |
| `PROTOCORE_ENABLE_IOLINK` | `0` | IO-Link (SDCI, IEC 61131-9) data-link message codec (`services/iolink`). |
| `PROTOCORE_ENABLE_IPV6` | `0` | Enable IPv6 on the network interface (dual-stack). |
| `PROTOCORE_ENABLE_IP_ALLOWLIST` | `0` | Opt-in source-IP allowlist (accept-time firewall, IPv4 and IPv6). |
| `PROTOCORE_ENABLE_J1939` | `0` | SAE J1939 message codec (`services/j1939`). |
| `PROTOCORE_ENABLE_J2735` | `0` | Opt-in SAE J2735 V2X codec. |
| `PROTOCORE_ENABLE_JWT` | `0` | JWT bearer-token authentication (HS256). |
| `PROTOCORE_ENABLE_KEEPALIVE` | `0` | HTTP/1.1 persistent connections (keep-alive). |
| `PROTOCORE_ENABLE_LD2410` | `0` | HLK-LD2410 24 GHz mmWave presence / motion radar (UART). |
| `PROTOCORE_ENABLE_LDC1614` | `0` | Opt-in LDC1614 inductance-to-digital field sensor. |
| `PROTOCORE_ENABLE_LINK_MANAGER` | `0` | Opt-in multi-interface egress selection / failover policy. |
| `PROTOCORE_ENABLE_LOGBUF` | `0` | Opt-in fixed-RAM rotating log buffer with severity traps. |
| `PROTOCORE_ENABLE_LONWORKS` | `0` | Opt-in LonWorks / LON-IP (ISO/IEC 14908) network-variable codec. |
| `PROTOCORE_ENABLE_LORA` | `0` | Enable the LoRa (SX127x) radio codec + driver (default off). |
| `PROTOCORE_ENABLE_LSV2` | `0` | Heidenhain LSV/2 telegram codec (`services/lsv2`). |
| `PROTOCORE_ENABLE_LWM2M` | `0` | OMA LwM2M TLV codec (`services/lwm2m`). |
| `PROTOCORE_ENABLE_MBPLUS` | `0` | Opt-in Modbus Plus HDLC token-bus frame codec. |
| `PROTOCORE_ENABLE_MBUS` | `0` | Wired M-Bus (Meter-Bus, EN 13757) frame codec (`services/mbus`). |
| `PROTOCORE_ENABLE_MDNS` | `0` | mDNS / DNS-SD advertisement: `<name>.local` plus `_http._tcp` and any service added. |
| `PROTOCORE_ENABLE_MDNS_ADAPTIVE` | `0` | Opt-in adaptive mDNS beacon scheduling. |
| `PROTOCORE_ENABLE_MELSEC` | `0` | Mitsubishi MELSEC MC protocol (binary 3E) codec (`services/melsec`). |
| `PROTOCORE_ENABLE_METRICS` | `0` | Prometheus `/metrics` endpoint (text exposition format 0.0.4). |
| `PROTOCORE_ENABLE_MMS` | `0` | Opt-in IEC 61850 MMS PDU codec. |
| `PROTOCORE_ENABLE_MNT` | `0` | Mounted storage. |
| `PROTOCORE_ENABLE_MODBUS` | `0` | Modbus TCP slave/server (Modbus Application Protocol v1.1b3) on TCP/502. |
| `PROTOCORE_ENABLE_MODBUS_MASTER` | `0` | Opt-in Modbus master codec + register scanner. |
| `PROTOCORE_ENABLE_MODBUS_RTU` | `0` | Modbus RTU framing (serial / RS-485) over the same data model + PDU dispatch. |
| `PROTOCORE_ENABLE_MPR121` | `0` | NXP MPR121 12-channel capacitive-touch controller (I2C). |
| `PROTOCORE_ENABLE_MQTT` | `0` | MQTT 3.1.1 publish/subscribe client (raw lwIP, optional MQTTS over TLS). |
| `PROTOCORE_ENABLE_MQTT_SN` | `0` | MQTT-SN v1.2 wire codec (`services/iot/mqtt/mqtt_sn`). |
| `PROTOCORE_ENABLE_MQTT_TLS` | `0` | MQTTS: run the MQTT client over client-side TLS (needs PROTOCORE_ENABLE_TLS). |
| `PROTOCORE_ENABLE_MSGPACK` | `0` | Zero-heap MessagePack encoder and decoder for compact binary payloads. |
| `PROTOCORE_ENABLE_MTCONNECT` | `0` | Opt-in MTConnect agent response codec. |
| `PROTOCORE_ENABLE_MTLS` | `0` | Mutual TLS - require and verify a client certificate (mTLS). |
| `PROTOCORE_ENABLE_MULTIPART` | `0` | multipart/form-data body parser. |
| `PROTOCORE_ENABLE_NATS` | `0` | NATS client protocol codec (`services/nats`). |
| `PROTOCORE_ENABLE_NEMA_TS2` | `0` | Opt-in NEMA TS 2 traffic-cabinet SDLC frame codec. |
| `PROTOCORE_ENABLE_NETADAPT` | `0` | Opt-in network adaptation decisions. |
| `PROTOCORE_ENABLE_NMEA0183` | `0` | NMEA 0183 sentence codec (`services/nmea0183`). |
| `PROTOCORE_ENABLE_NMEA2000` | `0` | NMEA 2000 codec (`services/nmea2000`). |
| `PROTOCORE_ENABLE_NRF24` | `0` | Enable the nRF24L01+ radio driver (default off). |
| `PROTOCORE_ENABLE_NTCIP` | `0` | Opt-in NTCIP transportation-device object identifiers. |
| `PROTOCORE_ENABLE_NTP` | `0` | SNTP wall-clock time sync via the ESP-IDF SNTP client. |
| `PROTOCORE_ENABLE_NTP_SERVER` | `0` | NTP/SNTP time server (RFC 5905 / RFC 4330 server mode) on UDP/123 (services/protocore_ntp_server). |
| `PROTOCORE_ENABLE_NTRIP_CASTER` | `0` | GNSS RTK base station + NTRIP caster (services/timing_position/gnss). |
| `PROTOCORE_ENABLE_NTS` | `0` | Opt-in Network Time Security (NTS, RFC 8915) wire codec. |
| `PROTOCORE_ENABLE_OAUTH2` | `0` | OAuth2 token-endpoint client. |
| `PROTOCORE_ENABLE_OBSERVABILITY` | `0` | Transport-layer observability: connection event hook + counters. |
| `PROTOCORE_ENABLE_OCIT` | `0` | Opt-in OCIT-Outstations message codec. |
| `PROTOCORE_ENABLE_OIDC` | `0` | OpenID Connect ID-token verification, RS256. |
| `PROTOCORE_ENABLE_OPCUA` | `0` | OPC UA Binary server. |
| `PROTOCORE_ENABLE_OPCUA_CLIENT` | `0` | OPC UA Binary client. |
| `PROTOCORE_ENABLE_OPENADR` | `0` | Opt-in OpenADR 3.0 (Automated Demand Response) JSON codec. |
| `PROTOCORE_ENABLE_OTA` | `0` | Authenticated OTA firmware update (streaming POST to the ESP32 Update API). |
| `PROTOCORE_ENABLE_OTA_ROLLBACK` | `0` | Opt-in OTA rollback protection / soft-brick safeguard. |
| `PROTOCORE_ENABLE_PACKML` | `0` | PackML / OMAC packaging-machine state model (`services/packml`). |
| `PROTOCORE_ENABLE_PARTITION_MONITOR` | `0` | Opt-in flash partition-map monitor endpoint. |
| `PROTOCORE_ENABLE_PCA9685` | `0` | NXP PCA9685 16-channel 12-bit PWM / servo driver (I2C). |
| `PROTOCORE_ENABLE_PER_IP_THROTTLE` | `0` | Opt-in per-IP accept-rate throttle (connection-flood defense, keyed by source IPv4). |
| `PROTOCORE_ENABLE_PMBUS` | `0` | PMBus 1.3 power-management command set over SMBus. |
| `PROTOCORE_ENABLE_PN532` | `0` | Enable the PN532 NFC frame codec (default off). |
| `PROTOCORE_ENABLE_POWERLINK` | `0` | Opt-in Ethernet POWERLINK (EPSG) basic frame codec. |
| `PROTOCORE_ENABLE_POWER_MGMT` | `0` | Opt-in SoC power governor. |
| `PROTOCORE_ENABLE_PQC_KEX` | `0` | Post-quantum hybrid key exchange: ML-KEM-768 + X25519 (FIPS 203 / RFC 9370 combiner). |
| `PROTOCORE_ENABLE_PREEMPT_QUEUE` | `0` | Enable the preempting work queue primitive (default off). |
| `PROTOCORE_ENABLE_PROFIBUS` | `0` | Opt-in PROFIBUS-DP FDL telegram codec. |
| `PROTOCORE_ENABLE_PROFINET` | `0` | Opt-in PROFINET DCP (Discovery and Configuration Protocol) frame codec. |
| `PROTOCORE_ENABLE_PROMISC` | `0` | Wi-Fi promiscuous (monitor) capture. |
| `PROTOCORE_ENABLE_PROTOBUF` | `0` | Protocol Buffers wire codec (`services/protobuf`). |
| `PROTOCORE_ENABLE_PROVISIONING` | `0` | First-boot WiFi provisioning: softAP + captive-portal credentials form. |
| `PROTOCORE_ENABLE_PROXY_PROTOCOL` | `0` | HAProxy PROXY protocol codec (`services/proxy_protocol`). |
| `PROTOCORE_ENABLE_PSRAM_POOL` | `0` | Opt-in buffer placement policy (DRAM vs PSRAM) + SPI DMA ping-pong manager. |
| `PROTOCORE_ENABLE_PTP` | `0` | PTP / IEEE 1588-2008 (PTPv2) message codec + slave clock math (`services/ptp`). |
| `PROTOCORE_ENABLE_RADIO_POWER` | `0` | Opt-in radio power controls. |
| `PROTOCORE_ENABLE_RADIO_SNIFF` | `0` | Opt-in receive-only radio channel sniffer to pcap. |
| `PROTOCORE_ENABLE_RANGE` | `0` | HTTP Range requests / 206 Partial Content (requires PROTOCORE_ENABLE_FILE_SERVING or PROTOCORE_ENABLE_EDGE_CACHE). |
| `PROTOCORE_ENABLE_RAWL2` | `0` | Opt-in raw Layer-2 Ethernet frame codec. |
| `PROTOCORE_ENABLE_RCWL0516` | `0` | RCWL-0516 microwave Doppler presence sensor + the shared one-GPIO presence facade (`services/rcwl0516`). |
| `PROTOCORE_ENABLE_REDIS` | `0` | Redis RESP2/RESP3 wire codec (`services/iot/redis_resp`). |
| `PROTOCORE_ENABLE_RELAY` | `0` | Opt-in TCP relay / DNAT port forwarding. |
| `PROTOCORE_ENABLE_ROAMING` | `0` | Wi-Fi roaming decision layer (`services/roaming`). |
| `PROTOCORE_ENABLE_ROBOTICS` | `0` | OPC UA for Robotics information model. |
| `PROTOCORE_ENABLE_RTC` | `0` | I2C real-time-clock driver (DS1307 / DS3231) - a battery-backed time source. |
| `PROTOCORE_ENABLE_S7COMM` | `0` | Siemens S7comm PDU codec (`services/s7comm`). |
| `PROTOCORE_ENABLE_SAFETY_SCL` | `0` | IEC 61784-3 black-channel Safety Communication Layer primitives (`services/safety_scl`). |
| `PROTOCORE_ENABLE_SCPI` | `0` | SCPI / IEEE 488.2 instrument-control codec (`services/scpi`). |
| `PROTOCORE_ENABLE_SDI12` | `0` | SDI-12 sensor-bus codec (`services/sdi12`). |
| `PROTOCORE_ENABLE_SEN0192` | `0` | DFRobot SEN0192 10.525 GHz microwave Doppler motion sensor (single digital OUT line). |
| `PROTOCORE_ENABLE_SENML` | `0` | SenML (RFC 8428) measurement-pack builder (`services/senml`). |
| `PROTOCORE_ENABLE_SEP2` | `0` | Opt-in IEEE 2030.5 (Smart Energy Profile 2.0) resource codec. |
| `PROTOCORE_ENABLE_SERCOS` | `0` | Opt-in SERCOS III motion-bus telegram codec. |
| `PROTOCORE_ENABLE_SHT3X` | `0` | Sensirion SHT3x temperature / humidity sensor (I2C). |
| `PROTOCORE_ENABLE_SIGFOX` | `0` | Enable the Sigfox AT-command codec (default off). |
| `PROTOCORE_ENABLE_SIMATIC` | `0` | Siemens SIMATIC serial point-to-point: 3964R link + RK512 telegrams (`services/simatic`). |
| `PROTOCORE_ENABLE_SLEEP_SCHED` | `0` | Opt-in dynamic sleep-cycle scheduler. |
| `PROTOCORE_ENABLE_SMB` | `0` | Opt-in SMB2 client. |
| `PROTOCORE_ENABLE_SMBUS` | `0` | SMBus 3.1 transaction shapes over the shared I2C bus. |
| `PROTOCORE_ENABLE_SMTP` | `0` | Outbound SMTP client (RFC 5321) for device email alerts (services/net/smtp). |
| `PROTOCORE_ENABLE_SMTP_TLS` | `0` | Secure SMTP: run the mail client over client-side TLS (needs PROTOCORE_ENABLE_TLS). |
| `PROTOCORE_ENABLE_SNMP` | `0` | SNMP agent (v1/v2c, + v3 USM when PROTOCORE_ENABLE_SNMP_V3) over lwIP UDP. |
| `PROTOCORE_ENABLE_SNMP_TRAP` | `0` | Outbound SNMP notifications - traps and informs (requires PROTOCORE_ENABLE_SNMP). |
| `PROTOCORE_ENABLE_SNMP_V3` | `0` | Add SNMPv3 USM (auth via HMAC-SHA, privacy via AES-128-CFB). |
| `PROTOCORE_ENABLE_SNP` | `0` | Opt-in GE Fanuc SNP (Series Ninety Protocol) serial frame codec. |
| `PROTOCORE_ENABLE_SOCKPOOL` | `0` | Opt-in dynamic socket recycling: an LRU connection-slot pool. |
| `PROTOCORE_ENABLE_SOUTHBOUND` | `0` | Opt-in southbound protocol-driver framework. |
| `PROTOCORE_ENABLE_SPARKPLUG` | `0` | Sparkplug B payload + topic codec (`services/sparkplug`). |
| `PROTOCORE_ENABLE_SPA_ROUTER` | `0` | Opt-in single-page-app micro-routing decision. |
| `PROTOCORE_ENABLE_SQLITE` | `0` | Opt-in SQLite3 on-disk file-format reader. |
| `PROTOCORE_ENABLE_SSE` | `0` | Server-Sent Events push support. |
| `PROTOCORE_ENABLE_SSH` | `0` | SSH server support (RFC 4253/4252/4254). |
| `PROTOCORE_ENABLE_SSH_CLIENT` | `0` | Outbound SSH client + reverse tunnel (RFC 4254 §7.1 tcpip-forward, the `ssh -R` seam). |
| `PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE` | `0` | SSH keyboard-interactive authentication (RFC 4256), default off. |
| `PROTOCORE_ENABLE_SSH_SCP` | `0` | SCP server over SSH (the legacy RCP protocol via `exec "scp -t/-f"`). |
| `PROTOCORE_ENABLE_SSH_SFTP` | `0` | SFTP server subsystem over SSH (SSH_FXP_* v3, draft-ietf-secsh-filexfer-02). |
| `PROTOCORE_ENABLE_SSH_SNTRUP761` | `PROTOCORE_ENABLE_PQC_KEX` | Streamlined NTRU Prime sntrup761x25519-sha512@openssh.com SSH KEX (default: tracks ::PROTOCORE_ENABLE_PQC_KEX). |
| `PROTOCORE_ENABLE_SSH_ZLIB` | `0` | SSH server-to-client compression (`zlib@openssh.com` / `zlib`, RFC 4253 sec 6.2). |
| `PROTOCORE_ENABLE_STATS` | `0` | Runtime stats endpoint (uptime, request/error counts, pool usage, heap). |
| `PROTOCORE_ENABLE_STATSD` | `0` | Opt-in StatsD metrics client. |
| `PROTOCORE_ENABLE_STOMP` | `0` | STOMP 1.2 frame codec (`services/stomp`). |
| `PROTOCORE_ENABLE_SUNSPEC` | `0` | SunSpec Modbus device-information-model codec (`services/sunspec`). |
| `PROTOCORE_ENABLE_SYSLOG` | `0` | Syslog client (RFC 5424 over UDP). |
| `PROTOCORE_ENABLE_TELEMETRY` | `0` | Telemetry math helpers (moving-window stats, rate-of-change, totalizer). |
| `PROTOCORE_ENABLE_TELNET` | `0` | Telnet server support (RFC 854 / IAC option negotiation). |
| `PROTOCORE_ENABLE_THEMES` | `0` | Embed the theme stylesheet library as runtime-selectable blobs (default off). |
| `PROTOCORE_ENABLE_THREAD` | `0` | Enable the Thread spinel / HDLC-lite framing codec (default off). |
| `PROTOCORE_ENABLE_TIME_SOURCE` | `0` | Multi-source time fallback (NTP / RTC / GPS / ... |
| `PROTOCORE_ENABLE_TLS` | `0` | TLS (HTTPS/WSS) via mbedTLS with a static memory pool (ESP32-only). |
| `PROTOCORE_ENABLE_TLS_POLICY` | `0` | Opt-in TLS version negotiation + pinned cipher-suite policy. |
| `PROTOCORE_ENABLE_TLS_RESUMPTION` | `0` | TLS session resumption via RFC 5077 session tickets (requires PROTOCORE_ENABLE_TLS). |
| `PROTOCORE_ENABLE_TLS_RPK` | `0` | TLS Raw Public Keys (RFC 7250) - present a bare public key instead of an X.509 certificate. |
| `PROTOCORE_ENABLE_TOTP` | `0` | Opt-in TOTP two-factor auth (RFC 6238). |
| `PROTOCORE_ENABLE_TRACE_CAPTURE` | `0` | Enable the pre/post-trigger window assembler (default off). |
| `PROTOCORE_ENABLE_UBX` | `0` | u-blox UBX binary GNSS protocol codec (`services/ubx`). |
| `PROTOCORE_ENABLE_UDP_TELEMETRY` | `0` | Opt-in fire-and-forget UDP telemetry cast. |
| `PROTOCORE_ENABLE_UMATI` | `0` | umati - OPC UA for Machine Tools information model. |
| `PROTOCORE_ENABLE_UPLOAD` | `0` | Streaming file upload: POST a body straight to a file on the filesystem. |
| `PROTOCORE_ENABLE_UTMC` | `0` | Opt-in UTMC (Urban Traffic Management and Control) common-database codec. |
| `PROTOCORE_ENABLE_VL53L0X` | `0` | Opt-in VL53L0X optical time-of-flight ranging sensor. |
| `PROTOCORE_ENABLE_VXI11` | `0` | VXI-11 (TCP/IP Instrument Protocol) codec (`services/vxi11`). |
| `PROTOCORE_ENABLE_WAL` | `0` | Opt-in write-ahead store for atomic buffer-to-flash storage. |
| `PROTOCORE_ENABLE_WAMP` | `0` | WAMP messaging codec (`services/wamp`). |
| `PROTOCORE_ENABLE_WAVE` | `0` | Opt-in IEEE 1609 WAVE (WSMP + 1609.2 envelope) codec. |
| `PROTOCORE_ENABLE_WEARLEVEL` | `0` | Opt-in flash wear-leveling slot selector. |
| `PROTOCORE_ENABLE_WEBDAV` | `0` | WebDAV server (RFC 4918, class 1 + advisory locks) over the file system. |
| `PROTOCORE_ENABLE_WEBHOOK` | `0` | Opt-in outbound webhooks / IFTTT. |
| `PROTOCORE_ENABLE_WEBSOCKET` | `0` | WebSocket support (RFC 6455 framing + SHA-1/base64 handshake). |
| `PROTOCORE_ENABLE_WEB_TERMINAL` | `0` | Browser "web serial" terminal over WebSocket (src/server/web/web_terminal). |
| `PROTOCORE_ENABLE_WIFI_SNIFFER` | `0` | Opt-in 802.11 sniffer / traffic analyzer. |
| `PROTOCORE_ENABLE_WISUN` | `0` | Opt-in Wi-SUN FAN border-router connector. |
| `PROTOCORE_ENABLE_WS_CLIENT` | `0` | Outbound WebSocket client (RFC 6455 over raw lwIP, optional wss:// TLS). |
| `PROTOCORE_ENABLE_WS_CLIENT_TLS` | `0` | wss://: run the WebSocket client over client-side TLS (needs PROTOCORE_ENABLE_TLS). |
| `PROTOCORE_ENABLE_WS_DEFLATE` | `0` | WebSocket permessage-deflate (RFC 7692) - bidirectional compression. |
| `PROTOCORE_ENABLE_XMPP` | `0` | Opt-in XMPP (RFC 6120) stanza codec. |
| `PROTOCORE_ENABLE_ZIGBEE` | `0` | Enable the Zigbee EZSP / ASH framing codec (default off). |
| `PROTOCORE_ENABLE_ZWAVE` | `0` | Enable the Z-Wave Serial API frame codec (default off). |

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
| `PROTOCORE_FAILSAFE_MAX_LIFELINES` | `8` | Max monitored lifelines in the fail-safe registry (static, zero-heap). |
| `PROTOCORE_FILESYSTEM_PATH_MAX` | `256` | Largest absolute path the SFTP/SCP server resolves (mount root + request path). |
| `PROTOCORE_FTP_CHUNK` | `512` | Bytes staged per data-channel write when the session driver streams a payload. |
| `PROTOCORE_FTP_CMD_MAX` | `256` | Suggested FTP control-command buffer size. |
| `PROTOCORE_FTP_REPLY_BUF` | `512` | Control-reply accumulator for the FTP session driver. |
| `PROTOCORE_FTP_TIMEOUT_MS` | `8000` | Per-step timeout for the FTP session driver: connect, and each control reply. |
| `PROTOCORE_FWD_ACL_PATLEN` | `4` | Bytes an ACL entry can match (its pattern / mask length). |
| `PROTOCORE_FWD_INSPECT` | `0` | Build-time toggle for the forwarding-path inspection hook (default off, for cost + privacy). |
| `PROTOCORE_FWD_MAX_ACL` | `8` | Max ingress access-control entries (byte-pattern permit/deny; static). |
| `PROTOCORE_FWD_MAX_ROUTES` | `8` | Max policy routes (byte-pattern -> egress interface; static). |
| `PROTOCORE_FWD_MAX_RULES` | `8` | Max forwarding rules (src -> dst allow/deny + rate cap; static-allocated). |
| `PROTOCORE_GPIO_JSON_BUF` | `1024` | Stack buffer for the GPIO-map JSON (bytes). |
| `PROTOCORE_GPIO_MAX` | `40` | Maximum GPIO pins the mapper reports (BSS table). |
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
| `PROTOCORE_HTTP_CLIENT_BUF_SIZE` | `2048` | Receive buffer (and max response size) for the outbound HTTP client, bytes. |
| `PROTOCORE_HTTP_CLIENT_CT_BUF_SIZE` | `4096` | Ciphertext receive-ring size for the https:// client, bytes. |
| `PROTOCORE_HTTP_CLIENT_TIMEOUT_MS` | `8000` | Outbound HTTP client connect/response timeout in milliseconds. |
| `PROTOCORE_HTTP_EMIT_DATE` | `0` | Auto-inject a `Date` response header (RFC 7231 7.1.1.2) when a wall-clock time is available. |
| `PROTOCORE_INA219_CURRENT_LSB_UA` | `100` | Default INA219 current LSB in microamps per bit (calibration input). |
| `PROTOCORE_INA219_I2C_ADDR` | `0x40` | I2C address of the INA219 (0x40 default; the A0/A1 pins select 0x40..0x4F). |
| `PROTOCORE_INA219_SHUNT_MOHM` | `100` | Default INA219 shunt resistance in milliohms (calibration input). |
| `PROTOCORE_IP_ALLOWLIST_SLOTS` | `8` | Number of CIDR rules the source-IP allowlist can hold (BSS table). |
| `PROTOCORE_JWT_MAX_LEN` | `512` | Maximum accepted JWT length in bytes (header.payload.signature). |
| `PROTOCORE_KEEPALIVE_MAX_REQUESTS` | `100` | Maximum requests served on one keep-alive connection before it is closed. |
| `PROTOCORE_LD2410_BAUD` | `256000` | LD2410 UART baud rate (the module's fixed factory default is 256000). |
| `PROTOCORE_LD2410_UART` | `2` | UART unit the LD2410 is wired to. |
| `PROTOCORE_LOG_LINES` | `32` | Number of log lines retained in the ring. |
| `PROTOCORE_LOG_LINE_LEN` | `96` | Maximum length of one stored log line (bytes, including null). |
| `PROTOCORE_LORA_MAX_PAYLOAD` | `251` | Max LoRa payload bytes (SX127x FIFO is 256; RadioHead uses 251 + 4 header). |
| `PROTOCORE_MAX_UDP_LISTENERS` | `2` | Maximum simultaneously bound UDP ports (transport-layer UDP service). |
| `PROTOCORE_MDNS_LABEL_MAX` | `32` | Longest host label, service type or proto label the responder holds, NUL included. |
| `PROTOCORE_MDNS_MAX_SERVICES` | `4` | Services the responder advertises at once, `_http._tcp` included. |
| `PROTOCORE_MDNS_TXT_MAX` | `128` | Bytes of packed `key=value` TXT strings, each with its own length byte ahead of it. |
| `PROTOCORE_MDNS_TX_MAX` | `512` | Response datagram the responder composes. |
| `PROTOCORE_MODBUS_COILS` | `64` | Number of Modbus coils (FC 1/5/15), single-bit R/W (BSS, bit-packed). |
| `PROTOCORE_MODBUS_DISCRETE_INPUTS` | `64` | Number of Modbus discrete inputs (FC 2), single-bit read-only (BSS, bit-packed). |
| `PROTOCORE_MODBUS_HOLDING_REGS` | `64` | Number of Modbus holding registers (FC 3/6/16), 16-bit R/W (BSS). |
| `PROTOCORE_MODBUS_INPUT_REGS` | `64` | Number of Modbus input registers (FC 4), 16-bit read-only (BSS). |
| `PROTOCORE_MPR121_I2C_ADDR` | `0x5A` | I2C address of the MPR121 (0x5A default; 0x5B/0x5C/0x5D via the ADDR pin). |
| `PROTOCORE_MPR121_RELEASE_THRESHOLD` | `6` | MPR121 per-electrode release threshold (delta counts; should be below the touch threshold). |
| `PROTOCORE_MPR121_TOUCH_THRESHOLD` | `12` | MPR121 per-electrode touch threshold (delta counts from baseline; NXP AN3944 suggests ~4..12). |
| `PROTOCORE_MQTT_BUF_SIZE` | `1024` | MQTT packet buffer size in bytes (bounds one outgoing/incoming packet). |
| `PROTOCORE_MQTT_CONNECT_MS` | `8000` | What the whole MQTT connect is given, in milliseconds. |
| `PROTOCORE_MQTT_CT_BUF_SIZE` | `4096` | Ciphertext receive-ring size for MQTTS (draining ring; must exceed one TCP_MSS). |
| `PROTOCORE_MQTT_KEEPALIVE_S` | `30` | Default MQTT keep-alive interval in seconds (PINGREQ cadence / CONNECT field). |
| `PROTOCORE_MQTT_MAX_INFLIGHT` | `4` | Outbound QoS 1/2 in-flight slots (unacknowledged exchanges awaiting their acknowledgement). |
| `PROTOCORE_MQTT_MAX_TOPIC` | `128` | Maximum inbound MQTT topic length (including NUL) delivered to the callback. |
| `PROTOCORE_MQTT_RETRANSMIT_MS` | `5000` | Retransmit timeout (ms) for an unacknowledged in-flight QoS 1/2 message. |
| `PROTOCORE_MQTT_RX_QOS2_SLOTS` | `8` | Inbound QoS 2 packet-id de-duplication ring depth (PUBREC-acknowledged, awaiting PUBREL). |
| `PROTOCORE_MTLS_SUBJECT_MAX` | `128` | Maximum length of a verified mTLS peer subject DN string (incl. |
| `PROTOCORE_NEED_CLIENT` | `0` |  |
| `PROTOCORE_NEED_UDP` | `0` |  |
| `PROTOCORE_NRF24_PAYLOAD` | `32` | nRF24 fixed payload width in bytes (1..32; the chip's static payload size). |
| `PROTOCORE_NTP_CLIENT_PORT` | `1123` | Local UDP port the portable SNTP client asks from. |
| `PROTOCORE_NTP_SERVER_STRATUM` | `3` | Stratum the NTP server advertises (distance from a reference clock; 1-15). |
| `PROTOCORE_NTRIP_MAX_MOUNTS` | `2` | Max distinct mountpoints a single caster serves (each = one RTCM stream). |
| `PROTOCORE_NTRIP_MAX_ROVERS` | `4` | Max concurrent rover connections a caster serves corrections to (services/timing_position/gnss). |
| `PROTOCORE_NTRIP_MOUNT_MAX` | `32` | Max length (incl. |
| `PROTOCORE_NTRIP_REQ_MAX` | `512` | Max NTRIP client request size (bytes) the caster buffers while reading the request headers. |
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
| `PROTOCORE_POWER_MHZ_MAX` | `240` | CPU clock (MHz) when there is work to do. |
| `PROTOCORE_POWER_MHZ_MIN` | `80` | CPU clock (MHz) when idle, thermally throttled, or recovering from a brownout. |
| `PROTOCORE_POWER_RECOVER_MS` | `10000` | How long (ms) to hold the floor clock after a brownout reset before ramping back up. |
| `PROTOCORE_POWER_TEMP_COOL_C` | `70` | Die temperature (C) at/below which the throttle is released. |
| `PROTOCORE_POWER_TEMP_HOT_C` | `80` | Die temperature (C) at/above which the clock is throttled. |
| `PROTOCORE_PQ_DEPTH` | `16` | Capacity of the preempting queue in items (static-allocated). |
| `PROTOCORE_PQ_INTERNAL_PRIORITY` | `8` | Base FreeRTOS priority for the internal preempting lanes (DMA / forwarding / device access). |
| `PROTOCORE_PQ_ITEM_SIZE` | `32` | Bytes per preempting-queue item (the posted item must fit). |
| `PROTOCORE_PQ_STACK` | `4096` | Stack (bytes) for each preempting-queue processing task (ESP32). |
| `PROTOCORE_QUIC_MAX_CONNS` | `2` | Simultaneous HTTP/3 connections. |
| `PROTOCORE_RADIO_MAX_TX_DBM` | `0` | Max TX power cap in dBm (2..20); 0 = leave the platform default. |
| `PROTOCORE_RADIO_WIFI_PS` | `0` | WiFi modem-sleep mode: 0 = none (max perf), 1 = min modem, 2 = max modem. |
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
| `PROTOCORE_SEN0192_HOLD_MS` | `2000` | Presence is held this many ms after the last active (motion) sample before it clears. |
| `PROTOCORE_SEN0192_PIN` | `4` | GPIO the SEN0192 OUT line is wired to. |
| `PROTOCORE_SFTP_MAX_HANDLES` | `4` | Max concurrent open SFTP handles (files + dirs) per SSH connection. |
| `PROTOCORE_SFTP_MAX_READ` | `1024` | Largest PROTOCORE_SSH_FXP_DATA payload returned for one READ (a short read - the client re-requests). |
| `PROTOCORE_SFTP_PKT_BUF` | `2048` | SFTP packet-assembly buffer per SFTP channel (bytes); bounds one non-streamed request/response. |
| `PROTOCORE_SHT3X_I2C_ADDR` | `0x44` | I2C address of the SHT3x (0x44 with ADDR low; 0x45 with ADDR high). |
| `PROTOCORE_SIGFOX_MAX_PAYLOAD` | `12` | Maximum Sigfox uplink payload (the network caps a message at 12 bytes). |
| `PROTOCORE_SIMATIC_BLOCK_MAX` | `256` | 3964R block-body buffer size (built/received bytes: DLE-stuffed payload + DLE ETX + BCC). |
| `PROTOCORE_SIMATIC_QVZ_MS` | `2000` | 3964R QVZ (Quittungsverzugszeit): handshake acknowledge-delay timeout, ms. |
| `PROTOCORE_SIMATIC_ZVZ_MS` | `200` | 3964R ZVZ (Zeichenverzugszeit): inter-character timeout while receiving a block, ms. |
| `PROTOCORE_SMB_BUF` | `1024` | SMB2 client work-buffer size (bytes) for smb_client's request/response framing. |
| `PROTOCORE_SMTP_CT_BUF_SIZE` | `4096` | Ciphertext receive-ring size for SMTPS, bytes (only used when the message is TLS). |
| `PROTOCORE_SMTP_LINE_MAX` | `256` | Max length of one SMTP command / address line (bytes, incl. |
| `PROTOCORE_SMTP_MSG_MAX` | `2048` | Max size of the assembled DATA payload (headers + dot-stuffed body), bytes. |
| `PROTOCORE_SMTP_REPLY_MAX` | `512` | Max size of one (possibly multi-line) server reply held while parsing, bytes. |
| `PROTOCORE_SMTP_TIMEOUT_MS` | `10000` | SMTP connect / per-reply timeout in milliseconds. |
| `PROTOCORE_SNMP_TRAP_BUF_SIZE` | `1024` | Static datagram buffer for an outbound SNMP notification, bytes. |
| `PROTOCORE_SNMP_TRAP_MAX_VARBINDS` | `8` | Maximum extra variable-bindings (beyond sysUpTime/snmpTrapOID) in one notification. |
| `PROTOCORE_SPB_METRIC_MAX` | `256` | Max serialized size of one Sparkplug B metric submessage (stack temp, bytes). |
| `PROTOCORE_SSH_ALLOW_PASSWORD` | `1` | Allow SSH password authentication (default on). |
| `PROTOCORE_SSH_CLIENT_MAX_CHANNELS` | `4` |  |
| `PROTOCORE_SSH_FWD_CHUNK` | `1024` | Max bytes moved per forward channel per poll, target -> client (<= SSH_PKT_BUF_SIZE). |
| `PROTOCORE_SSH_FWD_CONNECT_MS` | `3000` | Blocking connect timeout (ms) when opening a forward target. |
| `PROTOCORE_SSH_FWD_HOST_MAX` | `64` | Maximum forward target hostname length including null terminator. |
| `PROTOCORE_SSH_FWD_MAX` | `2` | Maximum concurrent forwarded TCP connections (must be <= PROTOCORE_CLIENT_CONNS). |
| `PROTOCORE_SSH_MAX_CHANNELS` | `1` | Maximum concurrent SSH channels per connection (RFC 4254 multiplexing). |
| `PROTOCORE_SSH_PORT_FORWARD` | `0` | SSH TCP port forwarding (`direct-tcpip`, i.e. |
| `PROTOCORE_SSH_PTY_TERM_MAX` | `24` | Max stored TERM value from a pty-req (RFC 4254 sec 6.2). |
| `PROTOCORE_SSH_PW_CHANGE_COOLDOWN_MS` | `60000u` |  |
| `PROTOCORE_SSH_RFWD_BRIDGE_MAX` | `2` | Maximum concurrent bridged connections across all remote forwards. |
| `PROTOCORE_SSH_RFWD_MAX` | `1` | Maximum concurrent remote-forward listeners (`ssh -R` / `tcpip-forward`). |
| `PROTOCORE_SSH_ZLIB_ACK_DRAM` | `0` | Acknowledge placing the SSH compressor in internal DRAM (no PSRAM). |
| `PROTOCORE_SSH_ZLIB_IN_PSRAM` | `0` | Place the per-connection SSH compression state in external PSRAM (ESP32). |
| `PROTOCORE_SSH_ZLIB_MAX_IN` | `2048` | Largest uncompressed payload the s2c compressor accepts in one call (bytes). |
| `PROTOCORE_SSH_ZLIB_WINDOW` | `8192` | SSH s2c DEFLATE sliding-window size in bytes (max back-reference distance). |
| `PROTOCORE_STATSD_LINE_MAX` | `256` | Stack buffer for one StatsD line (bytes; caps metric name + value + tags). |
| `PROTOCORE_STATSD_PORT` | `8125` | Default StatsD collector UDP port (StatsD/Graphite standard). |
| `PROTOCORE_STOMP_MAX_HEADERS` | `16` | Max header lines parsed per STOMP frame (extras beyond this are ignored). |
| `PROTOCORE_SYSLOG_DEFAULT_PORT` | `514` | Default syslog collector UDP port (RFC 5426 well-known 514; overridable at runtime via Syslog.collector.port + Syslog.init and here for a non-standard collector). |
| `PROTOCORE_SYSLOG_FIELD_MAX` | `32` | Maximum syslog HOSTNAME / APP-NAME field length (including NUL). |
| `PROTOCORE_SYSLOG_MSG_MAX` | `256` | Maximum formatted syslog datagram length in bytes (RFC 5424 line). |
| `PROTOCORE_TCP_NODELAY` | `1` | Disable Nagle's algorithm (set TCP_NODELAY) on every accepted connection. |
| `PROTOCORE_TC_MAX_WINDOW_SAMPLES` | `4096` | Max samples a window may hold (pretrigger_samples + posttrigger_samples), static-allocated. |
| `PROTOCORE_THEMES_INCLUDE_TRADEMARKED` | `1` | Include the trademark-named themes in the embedded set (default on / open-source). |
| `PROTOCORE_THREAD_MAX_DATA` | `256` | Max spinel payload bytes carried in one HDLC-lite frame. |
| `PROTOCORE_TIME_SOURCE_MAX` | `4` | Maximum registered time sources. |
| `PROTOCORE_TLS13_KS_TERMS` | `12` |  |
| `PROTOCORE_TLS13_SECRET_LEN` | `32` |  |
| `PROTOCORE_TLS_ACK_MULTI_CONN_DRAM` | `0` | Acknowledge that a MAX_TLS_CONNS > 1 build has been sized to fit. |
| `PROTOCORE_TLS_ARENA_IN_PSRAM` | `0` | Place the TLS arena in external PSRAM instead of internal DRAM (ESP32). |
| `PROTOCORE_TLS_CONN_MSG_CAP` | `1024` |  |
| `PROTOCORE_TLS_CONN_REC_CAP` | `1024` |  |
| `PROTOCORE_TLS_CONN_STATE_CAP` | `2304` |  |
| `PROTOCORE_TLS_CONN_TERMS` | `5` |  |
| `PROTOCORE_TLS_MAX_FRAG_LEN` | `0` | Cap TLS records via the Maximum Fragment Length extension (RFC 6066). |
| `PROTOCORE_TLS_TICKET_LIFETIME_S` | `86400` | Session-ticket lifetime / key-rotation period in seconds (see PROTOCORE_ENABLE_TLS_RESUMPTION). |
| `PROTOCORE_TRUSTED_PROXY_MAX` | `2` | Number of trusted-upstream CIDR rules the forwarded-client resolver holds (BSS table). |
| `PROTOCORE_UDP_RX_BUF_SIZE` | `1472` | Largest UDP datagram a bound port accepts, in bytes. |
| `PROTOCORE_UDP_RX_RING` | `2048` | Per-slot UDP receive ring, in bytes. |
| `PROTOCORE_UDP_TELEMETRY_BUF` | `256` | Stack buffer for one telemetry line (bytes). |
| `PROTOCORE_UMATI_NS` | `1` | NamespaceIndex the umati MachineTool nodes live at (default 1). |
| `PROTOCORE_WEBDAV_BUF_SIZE` | `2048` | Buffer (BSS) for a WebDAV 207 Multi-Status response, in bytes (see PROTOCORE_ENABLE_WEBDAV). |
| `PROTOCORE_WEBDAV_MAX_ENTRIES` | `32` | Maximum children listed in a WebDAV Depth-1 PROPFIND (bounds the response). |
| `PROTOCORE_WEBDAV_MAX_PROPS` | `16` | Maximum properties echoed in a WebDAV PROPPATCH 207 response (bounds the response). |
| `PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS` | `14` | Channels tracked by the WiFi sniffer's per-channel survey. |
| `PROTOCORE_WORKER_CORE` | `1` | Core that worker 0 pins to (ESP32). |
| `PROTOCORE_WORKER_COUNT` | `1` | Number of server worker tasks (slots partitioned i % N). |
| `PROTOCORE_WORKER_POLL_TICKS` | `1` | Idle-sweep timeout, in FreeRTOS ticks, that a worker blocks between service iterations when no events are pending. |
| `PROTOCORE_WORKER_STACK_CURVE_MIN` | `12288` | Minimum worker-task stack (bytes) required once SSH is compiled in. |
| `PROTOCORE_WORKER_STACK_PQC_MIN` | `16384` |  |
| `PROTOCORE_WORKER_STACK_RSA_MIN` | `8192` | Minimum worker-task stack (bytes) required once an RSA-2048 verifier is compiled in (OIDC / SSH). |
| `PROTOCORE_WORKER_TASK_PRIORITY` | `5` | FreeRTOS priority for each server worker task (ESP32). |
| `PROTOCORE_WORK_AES256CTR` | `384` |  |
| `PROTOCORE_WORK_AESCCM` | `448` |  |
| `PROTOCORE_WORK_BIGNUM_HW` | `1024` |  |
| `PROTOCORE_WORK_BIGNUM_SW` | `1408` |  |
| `PROTOCORE_WORK_CHACHA20` | `192` |  |
| `PROTOCORE_WORK_CHACHAPOLY` | `64` |  |
| `PROTOCORE_WORK_MD` | `96` |  |
| `PROTOCORE_WORK_POLY1305` | `80` |  |
| `PROTOCORE_WS_CLIENT_BUF_SIZE` | `1024` | WebSocket client send/receive buffer size in bytes (bounds one frame). |
| `PROTOCORE_WS_CLIENT_CT_BUF_SIZE` | `4096` | Ciphertext receive-ring size for wss:// (draining ring; must exceed one TCP_MSS). |
| `PROTOCORE_WS_FRAG_SIZE` | `0` | WebSocket outbound fragmentation size (RFC 6455 sec 5.4), in payload bytes. |
| `PROTOCORE_ZIGBEE_MAX_DATA` | `128` | Max ASH payload bytes (an EZSP frame; the ASH data field caps near 128). |
| `PROTOCORE_ZWAVE_MAX_DATA` | `64` | Reject a Z-Wave frame whose declared length exceeds this data cap (sanity). |
| `PROTO_INDEX_BITS` | `32` | Bits in every offset, length and capacity the library declares (protocore_idx). |
| `PROTO_MAX_HANDLERS` | `12` | Size of the protocol-handler dispatch table; must exceed the largest ProtoConn id. |
| `QUERY_KEY_LEN` | `24` | Maximum query-parameter key length. |
| `QUERY_VAL_LEN` | `48` | Maximum query-parameter value length. |
| `RESP_HDR_BUF_SIZE` | `768` | Stack buffer for HTTP response header lines in send() / send_empty() / send_unauth() / serve_file(). |
| `RE_MAX_STEPS` | `2000` | Step budget for the regex route matcher (see on_regex()). |
| `RX_BUF_SIZE` | `1024` | Ring-buffer capacity in bytes per connection slot (feature floors enforced last, in core_setup/board_profiles/derived_sizing.h - a value below what an enabled feature needs is raised there). |
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
